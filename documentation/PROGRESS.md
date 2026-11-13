# Progress and verified results

Only results that were actually executed in this workspace are listed here.
Nothing below claims that Minecraft PE itself was launched or modified.

## Verified

### Build

```
cmake -S . -B build -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DNEXUS_BUILD_TESTS=ON -DNEXUS_BUILD_RUNTIME=ON -DNEXUS_BUILD_LAUNCHER=ON
cmake --build build -j4
```

Result: all libraries and the `nexus`, `nexus-runtime`, `nexus-launcher`
executables link successfully with GCC and `-Wall -Wextra -Wpedantic`.

### Tests

```
ctest --test-dir build --output-on-failure
```

Result: **8/8 pass**

| Test | Content |
| --- | --- |
| `test_core` | SHA-256, strings, version map, filesystem, config |
| `test_shader` | translation, analysis, profiles, structural validation, multiversion compile, ES 1.00 precision injection |
| `test_texture` | PNG inspection, pack import, path mapping, garbage rejection |
| `test_worldgen` | determinism, seed changes, summaries, block mutation/cache, extensions |
| `test_bridge` | protocol roundtrip, agent/client, events, errors, connect failure |
| `test_mod` | manifest parsing, script load, block/item registration, failure paths |
| `test_mcpe` | uniform catalog, asset summary, material inheritance/comments, multiversion preparation |
| `integration_cli` | real executables end to end (see below) |

### Integration test

`tests/integration/cli.sh` verifies:

- `nexus version`, `nexus doctor --json`;
- deterministic worldgen (two runs are byte-identical) and JSON output;
- shader translate/analyze/compile, including a real MCPE `__multiversion__`
  source with `MAT4`/`POS4`;
- MCPE asset inspection with a commented material file and `child:parent`
  inheritance;
- `nexus-runtime` startup, socket creation, `ping`, `status`, `runtime.info`,
  `worldgen.sample`, `mcpe.assets`, `shutdown`;
- launcher `--list`, `--report`, `--profiles`;
- when the supplied APK is present: `apk info`, `apk verify` exit `3`,
  `install import/list/verify/remove`, launcher listing, and a before/after
  SHA-256 check proving the original APK is unchanged.

Result: **PASS integration**.

### Real APK

- SHA-256 `c4365666ac5c5a65af3c8a94d765e34dc2f525bbca787d9431e0aa091f70f0f6`
  before and after import.
- Package `com.mojang.minecraftpe`, version `0.14.3`, code `760140300`.
- ABI `armeabi-v7a`; no x86 libraries.
- `nexus apk verify` → exit `3`, verdict `valid-but-not-runnable-x86`
  (correct for this APK).

### Real assets

```
nexus mcpe info --assets original/assets
```

Result: 8 uniform groups, 26 material files, **230 passes**, 39 shader files,
52 referenced shaders, **0 missing referenced shaders**.

```
nexus shader compile <each shader>
```

Result: **38/39** compile. The sole failure is `normal_as_color.vertex`
(`vec3 / 2`), which strict GLSL ES 1.00 rejects and which no material
references. It is reported, not hidden.

### Host strategy

`nexus strategy minecraft-0.14.3.apk` on this host reports
`android-container-arm`, confidence `blocked`, with the bionic/ABI reasons.
This is the correct result for an ARMv7 APK on an aarch64 host without an ARM
Android userspace.

## Not verified / blocked

- Launching Minecraft PE on this host. The APK is ARMv7; the host is
  aarch64 Alpine with no Waydroid/Anbox/adb/wine/qemu. No launch is claimed.
- In-game shader replacement, live material reload, or world/block injection.
  These require a game-side helper and an x86 APK; see
  [`INJECTION-LEVELS.md`](INJECTION-LEVELS.md).
- Native hooks inside `libminecraftpe.so`. The ARM symbols in `analysis/` are
  useful for orientation but are not an x86 hook table.
- GitHub Actions execution. The workflow targets Ubuntu 22.04 x86_64; it was
  authored and linted here but cannot run on this aarch64 Alpine host.
- Windows PC-port execution. The port binary is external and not included.

## Next concrete steps

1. Obtain an authorized x86 APK (`780140300`/`780140301`) and run
   `nexus apk verify` + `nexus strategy` on it.
2. Install Waydroid/Anbox on an x86_64 host and verify the Android-container
   launch plan.
3. Implement a game-side bridge helper for material/shader reload and event
   reporting.
4. Add an x86-specific hook/symbol analysis pass; do not reuse ARM addresses.
5. Extend the asset-overlay installer for the selected Android runtime.
