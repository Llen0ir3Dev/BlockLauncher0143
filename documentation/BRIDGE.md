# NEXUS bridge protocol

The bridge connects the external launcher/tools with a runtime host or a helper
running alongside the game. It is intentionally small enough to implement in a
native agent, an Android-side helper, or a test harness.

## Transport

- Unix stream socket.
- Default path: `$NEXUS_HOME/run/nexus-runtime.sock`, falling back to
  `/tmp/nexus-runtime-<uid>.sock`.
- One UTF-8 JSON object per line (`\n` terminated).
- Maximum message size: 8 MiB (`kMaxMessageBytes`).
- Socket file mode is `0600`; the agent unlinks stale sockets before bind and
  removes the socket on shutdown.
- Implementation: `nexus_bridge` (`include/nexus/bridge/*.hpp`).

## Messages

| Direction | Shape |
| --- | --- |
| request | `{"id":"1","method":"shader.translate","params":{...}}` |
| response | `{"id":"1","result":{...}}` |
| error | `{"id":"1","error":{"code":-32601,"message":"unknown method","data":null}}` |
| event | `{"event":"runtime.ready","data":{...}}` |

Requests are answered in order per connection. Events are server-initiated and
may arrive between responses. Malformed input produces a parse error and never
throws out of the decoder.

### Error codes

| Code | Meaning |
| --- | --- |
| `-32700` | parse error |
| `-32600` | invalid request |
| `-32601` | method not found |
| `-32602` | invalid params |
| `-32603` | internal error |
| `-32000` | unavailable |
| `-32001` | timeout |
| `-32002` | denied |

## Methods implemented by `nexus-runtime`

| Method | Params | Result |
| --- | --- | --- |
| `ping` | — | `{"version":"0.1.0"}` |
| `status` | — | protocol/socket/running/client/message counters |
| `runtime.info` | — | name, version, game version, protocol, host info, mods, seed |
| `shader.translate` | `source`, `stage`, `target`, `quality`, `glslVersion` | translation report and `source` |
| `shader.analyze` | `source`, `stage`, `glslVersion` | analyzed uniforms/attributes/functions/issues |
| `mcpe.assets` | `assets` (optional if `--assets` was given) | uniforms, materials, shaders, missing shaders |
| `worldgen.sample` | `x`, `z` | height, biome, seed |
| `worldgen.chunk` | `x`, `z` | chunk summary (heights, blocks, ores, caves, biome) |
| `mods.list` | — | discovered/active mods and registry content |
| `events.emit` | `event`, `data` | number of mods that received the event |
| `shutdown` | — | `{"stopping":true}`; stops the host loop |

`shader.translate` and `shader.analyze` accept real MCPE multiversion sources.
Pass `glslVersion` (`100` or `300`) to select the branch; when omitted, 100 is
used.

## CLI

```sh
nexus bridge ping   --socket /tmp/nexus.sock
nexus bridge status --socket /tmp/nexus.sock --json
nexus bridge call runtime.info --socket /tmp/nexus.sock
nexus bridge call worldgen.sample --socket /tmp/nexus.sock --params '{"x":0,"z":0}'
nexus bridge call shutdown --socket /tmp/nexus.sock
```

## Library use

Server:

```cpp
nexus::bridge::BridgeAgentOptions options;
options.socket_path = "/tmp/example.sock";
nexus::bridge::BridgeAgent agent(options);
agent.register_method("example.echo",
    [](const std::string&, const nlohmann::json& params,
       nlohmann::json& result, nexus::bridge::BridgeError&,
       const nexus::bridge::BridgeRequestContext& context) {
        result = {{"echo", params.value("value", "")}, {"id", context.request_id}};
        return true;
    });
std::string error;
agent.start(error);
agent.publish("example.ready", {{"ok", true}});
```

Client:

```cpp
nexus::bridge::BridgeClientOptions options;
options.socket_path = "/tmp/example.sock";
options.timeout_ms = 2000;
nexus::bridge::BridgeClient client(options);
std::string error;
client.connect(error);
nlohmann::json result;
nexus::bridge::BridgeError call_error;
client.call("example.echo", {{"value", "hello"}}, result, call_error);
```

## Game-side helper (future / required for in-game changes)

The protocol is deliberately independent of the transport peer. An Android-side
helper can:

1. load in the game's Android runtime (companion process, wrapper, or a hook
   inside `libminecraftpe.so`);
2. open the same Unix socket (or a forwarded TCP/ADB channel if the Android
   runtime does not share the host filesystem);
3. forward validated requests such as "reload material", "apply shader
   overlay" and "register block" to the game;
4. emit events (`game.ready`, `world.loaded`, `crash`) back to the launcher.

Implementing that helper requires an ABI-matched x86 build and a stable hook
point. It is **not** included in this repository; see
[`INJECTION-LEVELS.md`](INJECTION-LEVELS.md).
