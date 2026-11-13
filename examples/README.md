# Examples

| Path | Use |
| --- | --- |
| `mods/hello-world/` | minimal Duktape script mod (events + block/item registration) |
| `shader-packs/nexus-demo/` | Java/OptiFine-style shader pack for the importer/translator |
| `profiles/example.json` | launcher profile shape |

Try them:

```sh
nexus mod list --dir examples/mods
nexus mod test examples/mods/hello-world
nexus shader pack examples/shader-packs/nexus-demo --target gles100
nexus-runtime --headless --mods examples/mods --seconds 5
```

Profiles are read from `$NEXUS_HOME/profiles/`; copy `example.json` there and
edit `installId` to a real installation.
