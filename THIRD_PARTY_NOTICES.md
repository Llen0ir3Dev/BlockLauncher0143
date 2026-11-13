# Third-party notices

NEXUS-MCPE vendors or links the following components. Their licenses apply to
those components; the original NEXUS code is MIT licensed (see `LICENSE`).

## Vendored

### Duktape 2.7.0

- Path: `third_party/duktape/`
- License: MIT
- Copyright: Sami Vaarala and Duktape contributors
- https://duktape.org/

### nlohmann/json

- Path: `third_party/nlohmann/`
- License: MIT
- Copyright: Niels Lohmann and contributors
- https://github.com/nlohmann/json

## Linked system libraries

These are not vendored; the build requires the system development packages.

| Component | License | Use |
| --- | --- | --- |
| SDL2 | zlib | window, input, GL context |
| zlib | zlib | ZIP/deflate |
| libpng | libpng/zlib | PNG inspection and previews |
| OpenGL / EGL | system | shader validation and the optional demo |
| glslangValidator | BSD-3-Clause (Khronos) | optional real shader compilation |

## Not included

Minecraft, Minecraft PE, Mojang assets, the game APK, extracted game libraries
and the Windows PC port are **not** part of this repository or its release
archives. Users must supply their own legally obtained files.
