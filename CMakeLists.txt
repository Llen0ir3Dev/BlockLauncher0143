cmake_minimum_required(VERSION 3.16)
project(NEXUS-MCPE VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# NEXUS-MCPE
#
# The project is split into small, testable libraries.  The proprietary
# Minecraft PE 0.14.3 files are NEVER part of this repository.  The runtime
# detects them at run time through a documented provisioning directory.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(NEXUS_BUILD_TESTS "Build unit and integration tests" ON)
option(NEXUS_BUILD_LAUNCHER "Build the SDL2 launcher" ON)
option(NEXUS_BUILD_RUNTIME "Build the runtime and shader demo" ON)
option(NEXUS_ENABLE_SCRIPTING "Embed the Duktape JavaScript engine" ON)
option(NEXUS_ENABLE_OPENGL "Build OpenGL shader compilation/integration support" ON)
option(NEXUS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(NEXUS_BUILD_GUEST_ARM32 "Cross-compile the ARM32 guest runtime with the Android NDK" OFF)

# ---------------------------------------------------------------------------
# Architecture contract
#
# Three architectures are involved and must never be confused:
#   * dev build host : whatever this machine is (e.g. aarch64 Termux/PRoot)
#   * target host    : Ubuntu 22.04 x86_64 (the released AppImage)
#   * guest          : Android ARM32 (armeabi-v7a) game code run via QEMU
#
# A successful build on the dev host is NOT an x86_64 build.  The release
# AppImage is produced on an x86_64 CI runner.
# ---------------------------------------------------------------------------
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
  set(NEXUS_HOST_ARCH "x86_64")
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64|ARM64)$")
  set(NEXUS_HOST_ARCH "aarch64")
else()
  set(NEXUS_HOST_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
endif()
message(STATUS "NEXUS host architecture: ${NEXUS_HOST_ARCH} (release artifact targets x86_64)")

set(NEXUS_ANDROID_NDK "${ANDROID_NDK_PATH}" CACHE PATH
    "Android NDK root used to cross-compile the ARM32 guest runtime")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "Build type" FORCE)
endif()

include(GNUInstallDirs)
find_package(Threads REQUIRED)
find_package(ZLIB REQUIRED)
find_package(PNG REQUIRED)
find_package(PkgConfig REQUIRED)

pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)
pkg_check_modules(GLES2 QUIET IMPORTED_TARGET glesv2)

if(NEXUS_ENABLE_OPENGL)
  find_package(OpenGL REQUIRED)
endif()

set(NEXUS_WARNING_FLAGS
  -Wall -Wextra -Wpedantic -Wno-unused-parameter
  -Wno-missing-field-initializers
)
if(NEXUS_WARNINGS_AS_ERRORS)
  list(APPEND NEXUS_WARNING_FLAGS -Werror)
endif()

function(nexus_target_defaults target)
  target_include_directories(${target} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${CMAKE_SOURCE_DIR}/compat/arm32/include
  )
  target_compile_options(${target} PRIVATE ${NEXUS_WARNING_FLAGS})
  target_compile_definitions(${target} PRIVATE
    NEXUS_VERSION="${PROJECT_VERSION}"
    NEXUS_BUILD_TYPE="${CMAKE_BUILD_TYPE}"
  )
endfunction()

# ---------------------------------------------------------------------------
# Core library (no SDL, no OpenGL): logging, files, zip, json, config,
# events, hashing, timing.
# ---------------------------------------------------------------------------
file(GLOB_RECURSE NEXUS_CORE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/core/*.cpp
)
add_library(nexus_core STATIC ${NEXUS_CORE_SOURCES})
nexus_target_defaults(nexus_core)
target_link_libraries(nexus_core PUBLIC Threads::Threads ZLIB::ZLIB)
target_link_libraries(nexus_core PRIVATE PNG::PNG)

# ---------------------------------------------------------------------------
# Shader translation engine
# ---------------------------------------------------------------------------
file(GLOB_RECURSE NEXUS_SHADER_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/shader/*.cpp
)
add_library(nexus_shader STATIC ${NEXUS_SHADER_SOURCES})
nexus_target_defaults(nexus_shader)
target_link_libraries(nexus_shader PUBLIC nexus_core)

# ---------------------------------------------------------------------------
# Texture / resource pack engine
# ---------------------------------------------------------------------------
file(GLOB_RECURSE NEXUS_TEXTURE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/texture/*.cpp
)
add_library(nexus_texture STATIC ${NEXUS_TEXTURE_SOURCES})
nexus_target_defaults(nexus_texture)
target_link_libraries(nexus_texture PUBLIC nexus_core PNG::PNG)

# ---------------------------------------------------------------------------
# World generation engine
# ---------------------------------------------------------------------------
file(GLOB_RECURSE NEXUS_WORLDGEN_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/worldgen/*.cpp
)
add_library(nexus_worldgen STATIC ${NEXUS_WORLDGEN_SOURCES})
nexus_target_defaults(nexus_worldgen)
target_link_libraries(nexus_worldgen PUBLIC nexus_core)

# ---------------------------------------------------------------------------
# Mod engine: native loading, hooks, events, scripting
# ---------------------------------------------------------------------------
file(GLOB_RECURSE NEXUS_MOD_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mod/*.cpp
)
add_library(nexus_mod STATIC ${NEXUS_MOD_SOURCES})
nexus_target_defaults(nexus_mod)
target_link_libraries(nexus_mod PUBLIC nexus_core nexus_worldgen ${CMAKE_DL_LIBS})
target_include_directories(nexus_mod PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/duktape)

if(NEXUS_ENABLE_SCRIPTING)
  add_library(nexus_duktape STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/duktape/duktape.c
  )
  target_include_directories(nexus_duktape PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/duktape)
  target_link_libraries(nexus_mod PUBLIC nexus_duktape)
  target_compile_definitions(nexus_mod PUBLIC NEXUS_ENABLE_SCRIPTING=1)
else()
  target_compile_definitions(nexus_mod PUBLIC NEXUS_ENABLE_SCRIPTING=0)
endif()

# ---------------------------------------------------------------------------
# Platform layer (SDL2) and optional OpenGL helpers
# ---------------------------------------------------------------------------
file(GLOB_RECURSE NEXUS_PLATFORM_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/platform/*.cpp
)
add_library(nexus_platform STATIC ${NEXUS_PLATFORM_SOURCES})
nexus_target_defaults(nexus_platform)
target_link_libraries(nexus_platform PUBLIC nexus_core PkgConfig::SDL2)

file(GLOB_RECURSE NEXUS_GRAPHICS_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/graphics/*.cpp
)
add_library(nexus_graphics STATIC ${NEXUS_GRAPHICS_SOURCES})
nexus_target_defaults(nexus_graphics)
target_link_libraries(nexus_graphics PUBLIC nexus_core nexus_platform nexus_shader)
if(NEXUS_ENABLE_OPENGL)
  target_link_libraries(nexus_graphics PUBLIC OpenGL::GL)
  target_compile_definitions(nexus_graphics PUBLIC NEXUS_ENABLE_OPENGL=1)
else()
  target_compile_definitions(nexus_graphics PUBLIC NEXUS_ENABLE_OPENGL=0)
endif()

# ---------------------------------------------------------------------------
# Bridge: newline-delimited JSON transport between launcher and runtime
# ---------------------------------------------------------------------------
file(GLOB_RECURSE NEXUS_BRIDGE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bridge/*.cpp
)
add_library(nexus_bridge STATIC ${NEXUS_BRIDGE_SOURCES})
nexus_target_defaults(nexus_bridge)
target_link_libraries(nexus_bridge PUBLIC nexus_core Threads::Threads)

# ---------------------------------------------------------------------------
# ARM32 Compatibility Layer (NEXUS-ARM-COMPAT)
# ---------------------------------------------------------------------------
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/compat/arm32)

# ---------------------------------------------------------------------------
# Minecraft PE asset integration (uniforms, materials, multiversion shaders)
# ---------------------------------------------------------------------------
file(GLOB_RECURSE NEXUS_MCPE_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/mcpe/*.cpp
)
add_library(nexus_mcpe STATIC ${NEXUS_MCPE_SOURCES})
nexus_target_defaults(nexus_mcpe)
target_link_libraries(nexus_mcpe PUBLIC nexus_core nexus_shader nexus_arm_compat)

# ---------------------------------------------------------------------------
# Command line tool
# ---------------------------------------------------------------------------
add_executable(nexus src/cli/main.cpp)
nexus_target_defaults(nexus)
target_link_libraries(nexus PRIVATE
  nexus_core nexus_shader nexus_texture nexus_worldgen nexus_mod
  nexus_platform nexus_bridge nexus_mcpe
)

if(NEXUS_BUILD_RUNTIME)
  add_executable(nexus-runtime src/runtime/main.cpp)
  nexus_target_defaults(nexus-runtime)
  target_link_libraries(nexus-runtime PRIVATE
    nexus_core nexus_platform nexus_graphics nexus_shader
    nexus_texture nexus_worldgen nexus_mod nexus_bridge nexus_mcpe
  )
endif()

if(NEXUS_BUILD_LAUNCHER)
  add_executable(nexus-launcher src/launcher/main.cpp)
  nexus_target_defaults(nexus-launcher)
  target_link_libraries(nexus-launcher PRIVATE
    nexus_core nexus_platform nexus_shader nexus_texture nexus_bridge)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(NEXUS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

install(TARGETS nexus RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(NEXUS_BUILD_RUNTIME)
  install(TARGETS nexus-runtime RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(NEXUS_BUILD_LAUNCHER)
  install(TARGETS nexus-launcher RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(DIRECTORY examples/mods/ DESTINATION ${CMAKE_INSTALL_DATADIR}/nexus-mcpe/mods)
install(DIRECTORY examples/shader-packs/ DESTINATION ${CMAKE_INSTALL_DATADIR}/nexus-mcpe/shader-packs)
install(DIRECTORY examples/profiles/ DESTINATION ${CMAKE_INSTALL_DATADIR}/nexus-mcpe/profiles)
install(DIRECTORY documentation/ DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/nexus-mcpe)
install(FILES README.md LICENSE THIRD_PARTY_NOTICES.md
        DESTINATION ${CMAKE_INSTALL_DATADIR}/doc/nexus-mcpe)

# ---------------------------------------------------------------------------
# AppImage creation
# ---------------------------------------------------------------------------
option(NEXUS_BUILD_APPIMAGE "Build AppImage package" OFF)

if(NEXUS_BUILD_APPIMAGE)
  if(NOT NEXUS_HOST_ARCH STREQUAL "x86_64")
    message(FATAL_ERROR
      "The release AppImage must be built on an x86_64 host, but this host is "
      "${NEXUS_HOST_ARCH}. Use the GitHub Actions ubuntu-22.04 runner (or an "
      "x86_64 machine); a local aarch64 build only validates logic.")
  endif()

  list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")
  find_package(AppImage QUIET)
  find_program(NEXUS_QEMU_ARM qemu-arm-static
    HINTS /usr/bin /usr/local/bin
    DOC "Static QEMU ARM user-mode emulator to bundle in the AppImage")

  if(AppImage_FOUND AND NEXUS_BUILD_LAUNCHER)
    set(NEXUS_APPIMAGE_EXTRA_FILES
      "${CMAKE_SOURCE_DIR}/examples/mods -> usr/share/nexus-mcpe/mods"
      "${CMAKE_SOURCE_DIR}/examples/shader-packs -> usr/share/nexus-mcpe/shader-packs"
      "${CMAKE_SOURCE_DIR}/examples/profiles -> usr/share/nexus-mcpe/profiles"
      "${CMAKE_SOURCE_DIR}/documentation -> usr/share/doc/nexus-mcpe"
    )
    if(NEXUS_BUILD_GUEST_ARM32)
      # Always include the guest runtime directory; the custom target below
      # both builds it and makes it exist before the AppImage is assembled.
      list(APPEND NEXUS_APPIMAGE_EXTRA_FILES
        "${CMAKE_BINARY_DIR}/guest-arm32 -> usr/share/nexus-mcpe/compat/arm32")
    endif()

    set(NEXUS_APPIMAGE_EXTRA_BINARIES nexus)
    if(TARGET nexus-runtime)
      list(APPEND NEXUS_APPIMAGE_EXTRA_BINARIES nexus-runtime)
    endif()
    if(TARGET nexus_arm_loader)
      list(APPEND NEXUS_APPIMAGE_EXTRA_BINARIES nexus_arm_loader)
    endif()

    nexus_create_appimage(
      TARGET nexus-launcher
      APPIMAGE_NAME "NEXUS-MCPE-x86_64"
      DESKTOP_FILE "${CMAKE_SOURCE_DIR}/resources/nexus-mcpe.desktop"
      ICON_FILE "${CMAKE_SOURCE_DIR}/resources/nexus-mcpe.png"
      OUTPUT_DIR "${CMAKE_BINARY_DIR}/appimage"
      QEMU_BINARY "${NEXUS_QEMU_ARM}"
      EXTRA_BINARIES
        ${NEXUS_APPIMAGE_EXTRA_BINARIES}
      EXTRA_FILES
        ${NEXUS_APPIMAGE_EXTRA_FILES}
    )
    if(TARGET nexus_guest_arm32)
      add_dependencies(NEXUS-MCPE-x86_64-appimage nexus_guest_arm32)
    endif()
  elseif(NEXUS_BUILD_LAUNCHER)
    message(WARNING "AppImage tools (linuxdeploy, appimagetool) not found. Skipping AppImage creation.")
  endif()
endif()
