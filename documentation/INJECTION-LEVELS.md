# Integration and injection levels

This document separates what NEXUS can do **outside** the game from what would
be required **inside** the game. It exists to prevent over-claiming: a launcher
that can translate a shader has not thereby changed the running renderer.

## Level 0 — external management (implemented)

NEXUS runs as its own process. It can:

- inspect an APK, verify package/version/ABI, hash it and extract libraries;
- import/list/verify/remove installations without touching the source APK;
- select an execution strategy from measured host capabilities;
- translate and compile shaders offline;
- import texture packs and shader packs;
- generate deterministic worlds and test worldgen extensions;
- load script/native mods in the NEXUS runtime;
- expose all of this through the CLI, launcher and bridge.

This level never modifies the game.

## Level 1 — asset overlay (partially implemented)

**Works now:** NEXUS can read the real `assets/shaders`, `assets/materials`,
`assets/textures` trees, translate a shader, convert a texture pack and write
the results into an output directory. It can produce an overlay tree that
matches the game's asset paths.

**Not automatic:** placing that overlay into a running Android installation.
MCPE 0.14.3 reads its assets from the APK (or its extracted data directory,
depending on the runtime). Options, in order of safety:

1. Let the Android runtime mount/copy the overlay into the app's data directory
   and restart the game;
2. Build a locally patched copy of the APK for the user's own use (the original
   APK stays untouched; the derived APK is never distributed);
3. Use the game-side bridge helper to request a material/shader reload
   (Level 2).

Level 1 changes what the game loads at startup. It cannot hot-swap a shader in
an already-running GL context.

## Level 2 — cooperative bridge helper (host implemented, game side future)

**Works now:** `nexus-runtime` implements `nexus-bridge/1` over a Unix socket:
shader translation/analysis, MCPE asset inspection, worldgen sampling,
mod listing, event delivery and shutdown. The launcher can query it.

**Missing:** a helper inside the Android runtime/game process. That helper must
open the same protocol and translate requests into game operations. This is the
clean integration point: no memory patching, no undefined behavior, and the
game can refuse requests.

Required for the helper:

- an ABI-matched x86 build (`780140300`/`780140301`) and an Android runtime;
- a stable way to reach the game: a companion process with an exported API, a
  loader/libraries hook, or a small injected library;
- explicit user consent and a documented failure mode.

Without that helper, the bridge is fully functional but only controls NEXUS
services, not the game.

## Level 3 — native hooks (engine implemented, game hooks not)

**Works now:** the NEXUS mod engine has a C ABI, a content registry, a hook
manager and native module loading (`dlopen`). Script mods can register blocks,
items and events. The native ABI is disabled unless `--allow-native` is used.

**Game-side hooks are different.** To hook `libminecraftpe.so` you must:

1. confirm the exact x86 build and hash;
2. resolve symbols in that build (the supplied ARMv7 symbols are useful for
   names but not for addresses);
3. inject an ABI-correct library or patch a call site;
4. keep up with the game's GLES2 renderer and JNI lifecycle;
5. accept that this is version-specific and can crash or break the game.

The analysis in `analysis/` records candidate entry points
(`JNI_OnLoad`, renderer/level symbols, worldgen symbols) from the ARM library.
They are **analysis artifacts**, not a working hook table. No native hook into
the game is claimed.

## Level 4 — binary patching / redistribution (not implemented, not offered)

Patching `libminecraftpe.so` or the APK to force new shaders or mods is
technically possible but:

- it modifies a proprietary binary;
- the patched binary may not be redistributed;
- it is unstable across builds and architectures;
- it bypasses the game's own integrity expectations.

NEXUS does not ship a patcher and will not distribute patched game files.
Research on the user's own copy can be done with REA/Ghidra, but results must
stay local and legal.

## Summary

| Capability | Level | Status |
| --- | --- | --- |
| APK inspect/verify/install | 0 | implemented and tested |
| Shader translate/compile offline | 0 | implemented and tested |
| Texture pack convert | 0 | implemented and tested |
| Worldgen extension API | 0 | implemented and tested |
| Mod engine (NEXUS runtime) | 0/3 | implemented and tested |
| Asset overlay generation | 1 | implemented; installation into game manual |
| Bridge host and protocol | 2 | implemented and tested |
| Game-side bridge helper | 2 | not present |
| Hooks inside `libminecraftpe.so` | 3 | not present; requires x86 APK/ABI |
| Patched game binaries | 4 | not offered |
| Real game launch on this host | — | blocked (ARMv7 + no Android runtime) |
