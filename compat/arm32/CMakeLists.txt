# NEXUS-ARM-COMPAT - ARM32 Compatibility Layer
# Provides ARM32 execution environment for Minecraft PE 0.14.3 on x86_64 Linux

cmake_minimum_required(VERSION 3.16)
project(NEXUS_ARM_COMPAT LANGUAGES C CXX ASM)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find QEMU user-mode ARM emulator. Distributions ship either the fully
# static `qemu-arm-static` (preferred: bundle it in the AppImage) or the
# dynamically linked `qemu-arm`. The runtime also auto-detects at run time.
find_program(QEMU_ARM_STATIC qemu-arm-static
    HINTS /usr/bin /usr/local/bin /opt/bin
    DOC "Static QEMU ARM user-mode emulator"
)
find_program(QEMU_ARM_DYNAMIC qemu-arm
    HINTS /usr/bin /usr/local/bin /opt/bin
    DOC "QEMU ARM user-mode emulator"
)

if(QEMU_ARM_STATIC)
    set(QEMU_ARM ${QEMU_ARM_STATIC})
    message(STATUS "QEMU ARM (static): ${QEMU_ARM_STATIC}")
elseif(QEMU_ARM_DYNAMIC)
    set(QEMU_ARM ${QEMU_ARM_DYNAMIC})
    message(STATUS "QEMU ARM (dynamic): ${QEMU_ARM_DYNAMIC}")
else()
    message(WARNING "No QEMU ARM user-mode emulator found.")
    message(WARNING "Install qemu-user-static (preferred) or qemu-user to enable ARM32 execution.")
endif()

# Find ARM32 cross-compilation toolchain (for building ARM32 shims)
find_program(ARM_CC arm-linux-gnueabihf-gcc)
find_program(ARM_CXX arm-linux-gnueabihf-g++)
find_program(ARM_AS arm-linux-gnueabihf-as)
find_program(ARM_LD arm-linux-gnueabihf-ld)
find_program(ARM_OBJCOPY arm-linux-gnueabihf-objcopy)
find_program(ARM_STRIP arm-linux-gnueabihf-strip)

if(ARM_CC AND ARM_CXX)
    message(STATUS "ARM cross-compiler found: ${ARM_CC}")
    set(ARM_CROSS_COMPILE TRUE)
else()
    message(WARNING "ARM cross-compiler not found. Install gcc-arm-linux-gnueabihf for building ARM32 shims.")
    set(ARM_CROSS_COMPILE FALSE)
endif()

# Android NDK path (for bionic libc headers and the ARM32 guest runtime).
# The top-level NEXUS_ANDROID_NDK setting is the canonical knob; fall back to
# ANDROID_NDK_PATH for standalone builds of this subdirectory.
set(ANDROID_NDK_PATH "" CACHE PATH "Path to Android NDK")
if(NOT ANDROID_NDK_PATH AND NEXUS_ANDROID_NDK)
    set(ANDROID_NDK_PATH "${NEXUS_ANDROID_NDK}" CACHE PATH "Path to Android NDK" FORCE)
endif()
if(ANDROID_NDK_PATH AND EXISTS "${ANDROID_NDK_PATH}")
    message(STATUS "Android NDK found: ${ANDROID_NDK_PATH}")
elseif(NEXUS_BUILD_GUEST_ARM32)
    message(WARNING "Android NDK not configured. Set NEXUS_ANDROID_NDK for the ARM32 guest build.")
endif()

# ARM32 guest runtime (armeabi-v7a code executed by QEMU user-mode).
# This is cross-compiled with the NDK and is independent from the host build.
if(NEXUS_BUILD_GUEST_ARM32)
    add_custom_target(nexus_guest_arm32 ALL
        COMMAND ${CMAKE_COMMAND} -E env ANDROID_NDK=${NEXUS_ANDROID_NDK}
                bash ${CMAKE_CURRENT_SOURCE_DIR}/tools/build_guest_arm32.sh
                     --ndk ${NEXUS_ANDROID_NDK}
                     --out ${CMAKE_BINARY_DIR}/guest-arm32
                     --static-only
        COMMENT "Cross-compiling ARM32 guest runtime with the Android NDK"
        VERBATIM)
endif()

# x86_64 host renderer.  The ARM32 guest forwards EGL/GLES2 over a Unix
# socket; this process executes those calls against the real Mesa driver.
# It is the only component allowed to touch the host's graphics stack.
pkg_check_modules(EGL QUIET IMPORTED_TARGET egl)
if(TARGET PkgConfig::EGL AND TARGET PkgConfig::GLES2)
    add_executable(nexus_gfx_host ${CMAKE_CURRENT_SOURCE_DIR}/host/gfx_host.cpp)
    nexus_target_defaults(nexus_gfx_host)
    target_include_directories(nexus_gfx_host PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/gfx
    )
    target_link_libraries(nexus_gfx_host PRIVATE
        PkgConfig::EGL
        PkgConfig::GLES2
        PkgConfig::SDL2
    )
else()
    message(STATUS "EGL/GLES2 development files not found; nexus_gfx_host disabled")
endif()

# Core compatibility library
file(GLOB_RECURSE ARM_COMPAT_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/shims/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/shims/*.c
    ${CMAKE_CURRENT_SOURCE_DIR}/loader/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/loader/*.c
)

if(ARM_COMPAT_SOURCES)
    add_library(nexus_arm_compat STATIC ${ARM_COMPAT_SOURCES})
    nexus_target_defaults(nexus_arm_compat)
    target_link_libraries(nexus_arm_compat PUBLIC nexus_core)
    target_compile_options(nexus_arm_compat PRIVATE
        -Wall -Wextra -Wpedantic
        -fPIC
        -DARM_COMPAT_BUILD
    )
    target_include_directories(nexus_arm_compat PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/shims
        ${CMAKE_CURRENT_SOURCE_DIR}/loader
        ${CMAKE_SOURCE_DIR}/include
    )
    # JNI headers.  The host-side compatibility library may only use a real
    # JDK header (which is glibc-compatible) or the bundled minimal declaration
    # header.  The Android NDK sysroot must NOT be added here: its jni.h pulls
    # in Android's inttypes.h/dlfcn.h, which conflict with glibc in a host
    # build.  The ARM32 guest is compiled separately with the NDK toolchain and
    # gets the real <jni.h> through the NDK compiler driver.
    find_path(JNI_H jni.h
        HINTS /usr/lib/jvm/default-java/include
              /usr/lib/jvm/java-11-openjdk/include
              /usr/lib/jvm/java-17-openjdk/include
              /usr/lib/jvm/java-21-openjdk/include
    )
    if(JNI_H)
        target_include_directories(nexus_arm_compat PUBLIC ${JNI_H})
        target_compile_definitions(nexus_arm_compat PUBLIC NEXUS_HAVE_SYSTEM_JNI)
        # jni.h includes platform-specific jni_md.h from a subdirectory
        # (e.g. include/linux/jni_md.h for OpenJDK on Linux).
        find_path(JNI_MD_H jni_md.h
            HINTS ${JNI_H}/linux
                  ${JNI_H}/darwin
                  ${JNI_H}/win32
        )
        if(JNI_MD_H)
            target_include_directories(nexus_arm_compat PUBLIC ${JNI_MD_H})
        endif()
    else()
        message(STATUS "No system JNI header found; using bundled jni_min.h declarations")
    endif()
endif()

# ARM32 loader executable (runs on x86_64, launches ARM32 binaries via QEMU)
add_executable(nexus_arm_loader
    ${CMAKE_CURRENT_SOURCE_DIR}/tools/arm_loader_cli.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/loader/arm_loader.cpp
)
nexus_target_defaults(nexus_arm_loader)
target_link_libraries(nexus_arm_loader PRIVATE
    nexus_core
    nexus_arm_compat
)

# Host-side ARM32 ELF inspector built from the same source as the guest
# loader.  It makes the loader's parser and relocation report testable and
# debuggable on the development host.
add_executable(nexus_elf_inspect ${CMAKE_CURRENT_SOURCE_DIR}/guest/elf_loader.c)
nexus_target_defaults(nexus_elf_inspect)

# ARM32 shim libraries (built for ARM32 if cross-compiler available)
if(ARM_CROSS_COMPILE)
    # These will be built separately with ARM toolchain
    message(STATUS "ARM32 shim libraries will be built with cross-compiler")
endif()

# Install headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/nexus/compat/arm32
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# Install loader
install(TARGETS nexus_arm_loader
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)