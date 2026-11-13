NEXUS-MCPE

COMPLETE PROJECT SPECIFICATION

Minecraft Pocket Edition 0.14.3 – Linux Port, Modding Framework and Shader Translation Engine

---

1. PROJECT IDENTITY

You are the lead software architect, reverse engineer, C/C++ developer, graphics engineer, Linux engineer and build engineer responsible for developing NEXUS-MCPE.

Your mission is to create a complete Linux-compatible Minecraft Pocket Edition 0.14.3 platform with an advanced modding ecosystem.

This is a real engineering project, not a demonstration or a collection of mockups.

Main objective

Use existing Minecraft PE 0.14.3 PC porting work as a foundation, investigate its source code, adapt it to Ubuntu 22.04 and develop a complete modding framework around it.

The project must ultimately be reproducible and compilable through GitHub Actions.

Target environment

- Development OS: Ubuntu 22.04 LTS.
- Target architecture: Linux x86_64.
- Development language: C and C++ primarily.
- Build system: CMake and Ninja where appropriate.
- Reverse engineering: REA as the primary tool.
- Complementary reverse engineering: Ghidra.
- Version control: Git.
- Automated compilation: GitHub Actions.
- Final deliverable: NEXUS-MCPE-Linux.zip.

---

2. IMPORTANT WORKING DIRECTORY RULE

The current working directory is the root of the new NEXUS-MCPE project.

You MUST work inside this directory.

Before doing anything:

1. Run pwd.
2. Inspect the current directory.
3. Inspect existing files.
4. Identify the available disk space and system architecture.
5. Create the complete project directory structure inside the current directory.

DO NOT create the project in another directory.

DO NOT use arbitrary absolute paths.

DO NOT assume that the original project files are available locally.

DO NOT modify files outside the current project directory except when installing system dependencies or using explicitly configured external caches.

Create the complete directory architecture yourself.

The user must not need to create folders manually.

---

3. EXISTING PROJECTS AND SOURCE INVESTIGATION

Investigate these repositories:

PC port:
https://github.com/pthreegames/MCPE-0.14.3-PC

Ninecraft:
https://github.com/MCPI-Revival/Ninecraft

Ninecraft must be investigated as a possible source-code foundation.

The PC port repository may contain compiled Windows binaries rather than the complete source code of its Minecraft 0.14.3 modifications.

You MUST distinguish between:

- Original source code.
- Existing compatibility-layer source code.
- Compiled binaries.
- Proprietary game libraries.
- Assets.
- Documentation.

Tasks:

1. Clone the repositories.
2. Inspect every relevant directory.
3. Read the README files.
4. Inspect the build systems.
5. Inspect the source licenses.
6. Identify available platform implementations.
7. Identify Windows-specific dependencies.
8. Investigate the relationship between Ninecraft and the 0.14.3 PC port.
9. Determine which components can be reused.
10. Identify missing source code.

Do not claim that the complete PC port source code is available if it is not.

Do not blindly replace the original game with an unrelated Minecraft implementation.

Preserve the original Minecraft PE 0.14.3 game architecture wherever legally and technically possible.

---

4. DEVELOPMENT ENVIRONMENT

Prepare the Ubuntu 22.04 development environment.

Inspect the existing environment before installing anything.

Install and configure necessary tools:

- Git.
- Git LFS.
- GCC and G++.
- Clang and LLVM.
- CMake.
- Ninja.
- Make.
- Python 3.
- pip.
- Java JDK.
- Bun.
- REA.
- Ghidra if necessary.
- ADB.
- Binutils.
- ELF utilities.
- OpenSSL.
- Zlib.
- SDL2.
- OpenGL development libraries.
- X11 development libraries.
- Other dependencies justified by the project.

Use the appropriate installation methods.

REA MUST be installed and executed through bunx where applicable.

Do NOT use npm to install REA.

Verify installations by executing their version commands.

Create:

documentation/ENVIRONMENT.md

Record:

- Installed tools.
- Versions.
- Architecture.
- Dependencies.
- Installation instructions.
- Known limitations.

Do not reinstall tools unnecessarily.

---

5. ORIGINAL GAME ACQUISITION

Investigate how to obtain the original Minecraft PE 0.14.3 files from legally accessible sources.

Prioritize the historical x86 version when appropriate for the x86_64 Linux port.

Investigate version identifiers such as:

- 760140300. 
- 780140300. 
- 780140301. 

Verify these identifiers against actual package metadata rather than assuming they are interchangeable.

Look for:

- Original APK.
- Original x86 native libraries.
- Required assets.
- Relevant configuration files.

Inspect existing local files before downloading anything.

For every downloaded file:

- Record its source.
- Verify its integrity.
- Identify its architecture.
- Calculate SHA256.
- Record its version and package information.

Never execute unknown downloaded binaries without inspection.

Never use pirated or unauthorized copies.

Never redistribute copyrighted Minecraft binaries, libraries or assets without authorization.

If a required proprietary file is missing, document the exact requirement and provide a private provisioning mechanism.

Do not stop all development simply because a proprietary asset is unavailable.

---

6. REVERSE ENGINEERING WITH REA

REA is the PRIMARY reverse-engineering tool.

Use it to investigate the original native libraries and the available PC port.

Analyze:

- ELF structures.
- Exported symbols.
- Imported functions.
- JNI entry points.
- Native dependencies.
- Rendering functions.
- Audio functions.
- Input handling.
- Filesystem operations.
- Networking.
- Game initialization.
- Memory structures.
- World generation.
- Block and entity systems.

Generate structured analysis reports.

Use Ghidra as a complementary tool when needed.

Never invent function names, offsets, structures or reverse-engineering results.

Document confidence levels for uncertain discoveries.

Create:

analysis/
analysis/reports/
analysis/symbols/
analysis/functions/
analysis/graphics/
analysis/world-generation/
analysis/native-dependencies/

Store the reports in the project.

---

7. LINUX PORT

The first major technical milestone is running the real Minecraft PE 0.14.3 client on Ubuntu 22.04.

Investigate the original PC port and Ninecraft implementation.

Identify all Windows-specific dependencies.

Replace or adapt components as necessary.

Investigate:

- Windows API.
- Linux filesystem access.
- Window management.
- Input handling.
- Audio.
- Graphics.
- Networking.
- Memory management.
- Threading.
- Dynamic library loading.

Investigate the feasibility of SDL2 for windowing and input.

Determine the actual graphics API and shader language used by the game.

Do not assume that an ARM Android library can run natively on x86_64.

Do not assume that a Windows executable can be directly compiled into Linux.

Do not create an unrelated substitute game.

The result must preserve the actual 0.14.3 game wherever possible.

Create a documented compatibility architecture.

---

8. NEXUS MOD ENGINE

Develop a modular extension framework.

The objective is to allow users to modify Minecraft PE 0.14.3 without manually patching the original game for every modification.

Implement:

Native extension system

- C/C++ extension support.
- Function hooks.
- Symbol resolution.
- Runtime compatibility checks.
- Native API.
- Extension loading.
- Extension lifecycle.
- Error reporting.

JavaScript scripting system

- Embedded JavaScript runtime.
- Script loading.
- Mod API.
- Event callbacks.
- Block manipulation.
- Entity manipulation.
- Item manipulation.
- Gameplay events.
- World events.
- Script configuration.
- Sandboxing.
- Error reporting.

Investigate suitable embedded JavaScript engines and their licenses.

Do not assume that JavaScript can directly access native game internals without an appropriate bridge.

Event system

Design an extensible event system.

Examples:

- OnGameStart.
- OnWorldLoad.
- OnWorldSave.
- OnBlockPlace.
- OnBlockBreak.
- OnEntitySpawn.
- OnEntityDeath.
- OnPlayerUpdate.
- OnTick.

Only expose events that can actually be implemented and verified.

---

9. ADVANCED SHADER TRANSLATION ENGINE

THIS IS ONE OF THE MOST IMPORTANT PARTS OF THE PROJECT.

We do NOT want a system that only loads shaders written specifically for NEXUS.

We want users to import existing Minecraft Java Edition shader packs and translate them into shaders compatible with the actual rendering architecture of Minecraft PE 0.14.3.

The objective is to translate existing shader implementations, not merely rename files or replace file extensions.

Supported source formats to investigate

- Minecraft Java shader packs.
- OptiFine shader packs.
- GLSL vertex shaders.
- GLSL fragment shaders.
- Shader configuration files.
- Shader uniforms.
- Shader attributes.
- Texture samplers.
- Rendering passes.
- Supported shader resources.

Compatibility must be determined from actual files and code.

9.1 Shader pack importer

Implement:

- ZIP importing.
- Folder importing.
- Shader pack detection.
- Pack structure analysis.
- File extraction.
- Configuration extraction.
- Shader metadata.
- Dependency detection.

9.2 GLSL parser

Develop a real shader parsing system.

Implement:

- GLSL lexer.
- GLSL parser.
- Abstract Syntax Tree.
- Expression analysis.
- Function analysis.
- Variable analysis.
- Uniform extraction.
- Attribute extraction.
- Shader version detection.
- Unsupported instruction detection.

Do not rely exclusively on regular expressions for complex shader translation.

9.3 Shader compatibility analyzer

Analyze differences between Minecraft Java shader systems and the actual PE 0.14.3 graphics pipeline.

Detect:

- Unsupported GLSL versions.
- Missing uniforms.
- Missing attributes.
- Unsupported rendering passes.
- OptiFine-specific variables.
- Unsupported built-in functions.
- Texture binding differences.
- Rendering pipeline incompatibilities.
- Missing depth or lighting information.

Generate a detailed compatibility report for each imported shader.

9.4 Translation engine

Create a modular translation engine.

It must:

1. Parse the original shader.
2. Build an AST.
3. Analyze the source shader.
4. Identify compatible functionality.
5. Map source variables to target variables.
6. Translate supported instructions.
7. Adapt uniform declarations.
8. Adapt attributes.
9. Adapt texture samplers.
10. Adapt rendering passes where possible.
11. Generate target shader code.
12. Validate the generated output.

Implement reusable translation rules.

The translation engine must be extensible so additional Java shader formats and rendering systems can be supported later.

9.5 Compatibility backend

Create a backend specifically for the actual graphics system used by the Linux port.

Determine its shader language and supported capabilities before implementation.

Investigate whether a GLSL-to-GLSL adaptation is sufficient or whether a different intermediate representation or compiler backend is needed.

Implement:

- Target shader generation.
- Shader compilation.
- Uniform binding.
- Texture binding.
- Attribute mapping.
- Rendering integration.
- Error handling.

Do not assume that modern Java Edition shaders are directly compatible with an older mobile rendering pipeline.

9.6 Shader application

The translated shaders must actually be applied to the running game.

A shader being translated successfully is NOT sufficient.

Implement the integration required to:

- Load generated shaders.
- Bind them to the appropriate rendering stages.
- Provide required uniforms.
- Provide texture inputs.
- Replace or extend the appropriate rendering operations.
- Validate the resulting rendering output.

The goal is a real visual effect inside Minecraft PE 0.14.3.

9.7 Shader compatibility levels

Provide three possible outcomes:

FULL:
The shader functionality is successfully translated.

PARTIAL:
Some effects are translated, but unsupported functionality is reported.

UNSUPPORTED:
The shader cannot currently be translated.

Never silently remove major visual effects.

9.8 Shader profiles

Provide:

- Performance.
- Balanced.
- Quality.

These profiles should adjust supported effects and rendering costs.

9.9 Shader tests

Create test shader packs with increasing complexity.

Start with simple compatible effects.

Then test:

- Color grading.
- Fog.
- Water.
- Lighting.
- Shadows.
- Multiple rendering passes.
- More complex OptiFine shader functionality.

Every successful translation must have a reproducible test.

---

10. TEXTURE ENGINE

Develop a complete texture pack management system.

Features:

- Import texture packs.
- ZIP support.
- Folder support.
- Texture replacement.
- Block textures.
- Entity textures.
- Item textures.
- UI textures.
- Texture validation.
- Texture previews.
- Pack metadata.
- Pack profiles.
- Resource overrides.

Allow users to import compatible Minecraft Java resource packs when technically possible.

Investigate differences between Java and PE resource formats.

If formats differ, implement an appropriate resource conversion system.

Do not assume that all Java resource packs are compatible.

---

11. WORLD GENERATION ENGINE

Develop an extension framework for modifying the actual world generation.

Investigate the original game generation algorithms through REA.

Identify the relevant native functions and data structures.

Support the following objectives:

- Biome customization.
- Terrain generation modification.
- Ore distribution.
- Cave generation.
- Structure generation.
- Surface generation.
- Configurable world generation parameters.
- Custom generation rules.

Allow extensions to register custom generation behavior where technically possible.

Do not merely create configuration files that are never used by the actual game.

Changes must affect real generated worlds.

---

12. BLOCK, ITEM AND ENTITY EXTENSIONS

Develop systems allowing mods to extend:

- Blocks.
- Items.
- Entities.
- Recipes where supported.
- Gameplay properties.
- Interactions.
- Behaviors.
- Textures.
- Sounds.

Investigate the original game architecture before designing these APIs.

Document limitations of the original 0.14.3 engine.

---

13. NEXUS LAUNCHER

Create a dedicated launcher.

The launcher should eventually provide:

- Game launching.
- Game configuration.
- Mod manager.
- Texture pack manager.
- Shader pack manager.
- Shader translation interface.
- Mod profiles.
- Script management.
- Logs.
- Crash reports.
- Runtime configuration.
- Compatibility information.

Use an appropriate Linux-compatible GUI framework.

Keep the launcher separate from the core game runtime.

The launcher must not be developed at the expense of the first working Linux game build.

---

14. REQUIRED PROJECT DIRECTORY STRUCTURE

Create this complete architecture automatically inside the current working directory.

Adapt it if the existing source code requires additional directories.

NEXUS-MCPE/
|
├── .github/
│   └── workflows/
│       ├── build.yml
│       ├── tests.yml
│       └── release.yml
|
├── original/
│   ├── apk/
│   ├── libraries/
│   ├── assets/
│   └── manifests/
|
├── external/
│   ├── ninecraft/
│   ├── pc-port/
│   └── dependencies/
|
├── analysis/
│   ├── reports/
│   ├── symbols/
│   ├── functions/
│   ├── graphics/
│   ├── audio/
│   └── world-generation/
|
├── engine/
│   ├── core/
│   ├── platform/
│   ├── graphics/
│   ├── audio/
│   ├── input/
│   ├── filesystem/
│   └── networking/
|
├── compatibility/
│   ├── linux/
│   ├── windows/
│   ├── android/
│   └── runtime/
|
├── mod-engine/
│   ├── native/
│   ├── scripting/
│   ├── events/
│   ├── hooks/
│   └── api/
|
├── shader-translator/
│   ├── importer/
│   ├── lexer/
│   ├── parser/
│   ├── ast/
│   ├── analyzer/
│   ├── converters/
│   ├── rules/
│   ├── backends/
│   ├── compiler/
│   ├── integration/
│   └── tests/
|
├── texture-engine/
│   ├── importer/
│   ├── converters/
│   ├── resources/
│   ├── validation/
│   └── previews/
|
├── world-generation/
│   ├── biomes/
│   ├── terrain/
│   ├── ores/
│   ├── structures/
│   └── extensions/
|
├── launcher/
│   ├── src/
│   ├── ui/
│   ├── configuration/
│   └── resources/
|
├── mods/
│   ├── examples/
│   ├── templates/
│   └── documentation/
|
├── scripts/
│   ├── setup/
│   ├── build/
│   ├── test/
│   └── package/
|
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── compatibility/
│   └── regression/
|
├── documentation/
│   ├── architecture/
│   ├── reverse-engineering/
│   ├── linux-port/
│   ├── shader-translation/
│   ├── modding/
│   ├── building/
│   └── PROGRESS.md
|
├── CMakeLists.txt
├── CMakePresets.json
├── README.md
├── LICENSE
├── .gitignore
└── build.sh

IMPORTANT:

Create the actual directory structure, not just this textual representation.

Create placeholder README files in directories that need documentation.

Do not create fake source implementations.

Use Git submodules or documented dependency retrieval where appropriate.

Keep proprietary game files outside public distribution packages.

---

15. GITHUB ACTIONS – COMPLETE BUILD SYSTEM

The final compilation must happen on GitHub.

Ubuntu 22.04 is our development environment.

GitHub Actions is our final build environment.

Create working GitHub workflows.

build.yml

Must:

1. Run on Ubuntu 22.04.
2. Check out the repository and its required submodules.
3. Install dependencies.
4. Configure CMake.
5. Build with Ninja.
6. Run tests.
7. Package the Linux output.
8. Upload build artifacts.

tests.yml

Run:

- Unit tests.
- Shader parser tests.
- Shader translation tests.
- Compatibility tests.
- Mod engine tests.
- Available integration tests.

release.yml

Create release archives when appropriate.

External proprietary dependencies

If the game requires proprietary original files:

- Use documented GitHub Secrets or private artifact provisioning.
- Never commit API keys or private credentials.
- Never assume GitHub can access files stored on the development machine.
- Provide clear instructions for supplying authorized game files.

The workflow must fail clearly when an essential external dependency is missing.

Do not silently build an empty or fake executable.

---

16. FINAL ZIP – ABSOLUTE REQUIREMENT

At the end of the project preparation and development work, you MUST generate:

NEXUS-MCPE-Linux.zip

This is mandatory.

The ZIP must be designed to be uploaded to a new GitHub repository.

It must include:

- Complete project directory structure.
- All original project source code that can legally be redistributed.
- All new source code.
- Build configuration.
- GitHub Actions workflows.
- Dependency installation scripts.
- Tests.
- Documentation.
- Shader translation system.
- Mod engine.
- Texture engine.
- Launcher source.
- Configuration templates.
- Git submodule declarations.
- External dependency setup instructions.

Do not include:

- Unauthorized copyrighted Minecraft files.
- Private API keys.
- Passwords.
- Local machine secrets.
- Temporary build caches.
- Unnecessary generated binaries.

ZIP verification

Before delivery:

1. Generate the ZIP.
2. Calculate SHA256.
3. List the archive contents.
4. Check for missing project files.
5. Verify that GitHub workflow files are included.
6. Extract the ZIP into a clean temporary directory.
7. Verify that the project structure survives extraction.
8. Test the documented GitHub build preparation.
9. Document any required private dependencies.

Final filename:

NEXUS-MCPE-Linux.zip

Also create:

NEXUS-MCPE-Linux-SHA256.txt

Never claim that a build or test passed if it was not actually executed.

---

17. DEVELOPMENT MILESTONES

Work in this order.

MILESTONE 1:
Inspect Ubuntu and install development dependencies.

MILESTONE 2:
Clone and analyze Ninecraft and the existing PC port.

MILESTONE 3:
Create the complete project architecture.

MILESTONE 4:
Investigate the original 0.14.3 client and required dependencies.

MILESTONE 5:
Establish a technically valid Linux porting strategy.

MILESTONE 6:
Compile the first working Linux runtime.

MILESTONE 7:
Launch and verify the real Minecraft PE 0.14.3 client.

MILESTONE 8:
Implement texture pack loading.

MILESTONE 9:
Implement the shader parser and analyzer.

MILESTONE 10:
Translate and compile a simple Java shader.

MILESTONE 11:
Apply the translated shader to the actual game.

MILESTONE 12:
Implement the native and JavaScript mod engines.

MILESTONE 13:
Implement world generation extensions.

MILESTONE 14:
Develop the launcher.

MILESTONE 15:
Complete GitHub Actions.

MILESTONE 16:
Generate and validate the final ZIP.

---

18. AUTONOMOUS WORKING RULES

You must work autonomously.

Do not stop after writing documentation.

Do not stop after installing dependencies.

Do not stop after creating folders.

Do not ask the user to create project files manually.

Do not ask the user to transfer files from Android.

Download and clone available public repositories directly from Ubuntu.

Do not repeatedly ask for permission to proceed with ordinary development tasks.

If a required dependency or source is missing:

1. Investigate alternatives.
2. Document the blocker.
3. Continue independent work.
4. Never fabricate missing source code or functionality.

Maintain:

documentation/PROGRESS.md

After each milestone, record:

- Completed work.
- Files created.
- Commands executed.
- Tests performed.
- Test results.
- Remaining work.
- Blockers.

Provide concise progress reports after each milestone.

Continue automatically through all feasible milestones.

Do not abandon the final ZIP requirement even if some advanced features remain incomplete.

The ZIP must accurately document implemented features and unfinished work.

---

19. FIRST ACTION

START NOW.

Your first task is:

1. Inspect the current working directory.
2. Inspect Ubuntu 22.04.
3. Verify available disk space.
4. Verify network connectivity.
5. Install the required development environment.
6. Verify REA through bunx.
7. Clone and inspect the two existing repositories.
8. Create the entire NEXUS-MCPE directory structure.
9. Create documentation/PROGRESS.md.
10. Begin the actual technical investigation.

Do not merely describe what you intend to do.

Execute the work.

The ultimate objective is:

A reproducible, GitHub-buildable Linux platform based on the real Minecraft PE 0.14.3 game, with a native modding ecosystem, texture support, world generation extensions and an advanced Minecraft Java shader translation engine.

FINAL DELIVERABLE:

NEXUS-MCPE-Linux.zip

This ZIP must be ready to upload to GitHub so that the final compilation can be performed through GitHub Actions.
