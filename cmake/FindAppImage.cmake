# FindAppImage.cmake - Helper for creating AppImages with linuxdeploy
#
# Usage:
#   find_package(AppImage REQUIRED)
#   nexus_create_appimage(
#     TARGET nexus-launcher
#     APPIMAGE_NAME "NEXUS-MCPE-x86_64"
#     DESKTOP_FILE "${CMAKE_SOURCE_DIR}/resources/nexus-mcpe.desktop"
#     ICON_FILE "${CMAKE_SOURCE_DIR}/resources/nexus-mcpe.png"
#     EXTRA_FILES
#       "${CMAKE_SOURCE_DIR}/examples/mods" -> "usr/share/nexus-mcpe/mods"
#       "${CMAKE_SOURCE_DIR}/examples/shader-packs" -> "usr/share/nexus-mcpe/shader-packs"
#       "${CMAKE_SOURCE_DIR}/examples/profiles" -> "usr/share/nexus-mcpe/profiles"
#   )

include(FindPackageHandleStandardArgs)

# Check for linuxdeploy and appimagetool
find_program(LINUXDEPLOY_EXECUTABLE linuxdeploy
  DOC "linuxdeploy AppImage creator"
)
find_program(APPIMAGETOOL_EXECUTABLE appimagetool
  DOC "appimagetool AppImage packager"
)

# Try to find Qt plugin for linuxdeploy (optional)
find_program(LINUXDEPLOY_QT_PLUGIN_EXECUTABLE linuxdeploy-plugin-qt
  DOC "linuxdeploy Qt plugin"
)

set(APPIMAGE_FOUND FALSE)
if(LINUXDEPLOY_EXECUTABLE AND APPIMAGETOOL_EXECUTABLE)
  set(APPIMAGE_FOUND TRUE)
endif()

find_package_handle_standard_args(AppImage
  REQUIRED_VARS LINUXDEPLOY_EXECUTABLE APPIMAGETOOL_EXECUTABLE
)

# Function to create an AppImage
function(nexus_create_appimage)
  set(options)
  set(oneValueArgs TARGET APPIMAGE_NAME DESKTOP_FILE ICON_FILE OUTPUT_DIR QEMU_BINARY)
  set(multiValueArgs EXTRA_FILES LIBS_PLUGINS)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

  if(NOT ARG_TARGET)
    message(FATAL_ERROR "nexus_create_appimage: TARGET is required")
  endif()
  if(NOT ARG_APPIMAGE_NAME)
    message(FATAL_ERROR "nexus_create_appimage: APPIMAGE_NAME is required")
  endif()
  if(NOT ARG_DESKTOP_FILE)
    message(FATAL_ERROR "nexus_create_appimage: DESKTOP_FILE is required")
  endif()
  if(NOT ARG_ICON_FILE)
    message(FATAL_ERROR "nexus_create_appimage: ICON_FILE is required")
  endif()

  if(NOT APPIMAGE_FOUND)
    message(WARNING "AppImage tools not found (linuxdeploy, appimagetool). Skipping AppImage creation.")
    return()
  endif()

  # Output directory
  set(OUTPUT_DIR ${ARG_OUTPUT_DIR})
  if(NOT OUTPUT_DIR)
    set(OUTPUT_DIR ${CMAKE_BINARY_DIR}/appimage)
  endif()
  # The packaging target runs with this directory as its working directory and
  # linuxdeploy writes the resulting AppImage there, so it must exist before
  # the first build command executes.
  file(MAKE_DIRECTORY ${OUTPUT_DIR})

  # AppDir structure
  set(APPDIR ${OUTPUT_DIR}/AppDir)
  set(BIN_DIR ${APPDIR}/usr/bin)
  set(LIB_DIR ${APPDIR}/usr/lib)
  set(SHARE_DIR ${APPDIR}/usr/share)
  set(APPLICATIONS_DIR ${SHARE_DIR}/applications)
  set(ICONS_DIR ${SHARE_DIR}/icons/hicolor/256x256/apps)
  set(METADIR ${APPDIR}/usr/share/metainfo)

  # Extract basenames
  get_filename_component(DESKTOP_BASENAME ${ARG_DESKTOP_FILE} NAME)
  get_filename_component(ICON_BASENAME ${ARG_ICON_FILE} NAME)

  # Assemble the AppDir and package it with a single ordered custom target.
  # (add_custom_command(TARGET ... POST_BUILD) only works for real executables
  # and libraries, not for custom targets, so every step is a COMMAND here.)
  set(_cmds
    COMMAND ${CMAKE_COMMAND} -E make_directory ${OUTPUT_DIR}
    COMMAND ${CMAKE_COMMAND} -E make_directory
            ${BIN_DIR} ${LIB_DIR} ${APPLICATIONS_DIR} ${ICONS_DIR} ${METADIR}
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${ARG_TARGET}> ${BIN_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy ${ARG_DESKTOP_FILE} ${APPLICATIONS_DIR}/
    COMMAND ${CMAKE_COMMAND} -E copy ${ARG_ICON_FILE} ${ICONS_DIR}/${ICON_BASENAME}
    COMMAND ${CMAKE_COMMAND} -E copy ${ARG_DESKTOP_FILE} ${METADIR}/${DESKTOP_BASENAME}
    COMMAND ${CMAKE_COMMAND} -E rename
            ${METADIR}/${DESKTOP_BASENAME} ${METADIR}/${ARG_APPIMAGE_NAME}.metainfo.xml
  )

  # Copy the bundled ARM32 instruction translator (QEMU user-mode) if present.
  # It is shipped as a separate executable; NEXUS does not depend on an
  # external Android emulator.
  if(ARG_QEMU_BINARY AND EXISTS "${ARG_QEMU_BINARY}")
    list(APPEND _cmds
      COMMAND ${CMAKE_COMMAND} -E copy ${ARG_QEMU_BINARY} ${BIN_DIR}/)
  endif()

  # Copy extra files.  Expected format: "source -> destination".
  foreach(pair IN LISTS ARG_EXTRA_FILES)
    string(REGEX MATCH "^([^>]+)->(.+)$" _match "${pair}")
    if(CMAKE_MATCH_COUNT EQUAL 2)
      string(STRIP "${CMAKE_MATCH_1}" _src)
      string(STRIP "${CMAKE_MATCH_2}" _dst)
      list(APPEND _cmds
        COMMAND ${CMAKE_COMMAND} -E make_directory ${APPDIR}/${_dst}
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${_src} ${APPDIR}/${_dst})
    else()
      message(WARNING "Invalid EXTRA_FILES format: ${pair}. Expected 'source -> destination'")
    endif()
  endforeach()

  # Run linuxdeploy last; --output appimage also invokes appimagetool.
  list(APPEND _cmds
    COMMAND ${LINUXDEPLOY_EXECUTABLE}
      --appdir ${APPDIR}
      --executable ${BIN_DIR}/$<TARGET_FILE_NAME:${ARG_TARGET}>
      --desktop-file ${APPLICATIONS_DIR}/${DESKTOP_BASENAME}
      --icon-file ${ICONS_DIR}/${ICON_BASENAME}
      --output appimage
  )

  add_custom_target(${ARG_APPIMAGE_NAME}-appimage
    ${_cmds}
    WORKING_DIRECTORY ${OUTPUT_DIR}
    COMMENT "Assembling and packaging ${ARG_APPIMAGE_NAME}"
    VERBATIM
  )
  add_dependencies(${ARG_APPIMAGE_NAME}-appimage ${ARG_TARGET})

  # Add test target
  add_custom_target(test-${ARG_APPIMAGE_NAME}-appimage
    COMMAND ${OUTPUT_DIR}/${ARG_APPIMAGE_NAME}-x86_64.AppImage --version
    DEPENDS ${ARG_APPIMAGE_NAME}-appimage
    COMMENT "Testing ${ARG_APPIMAGE_NAME}.AppImage"
  )

  # Install the AppImage
  set(_install_bindir "${CMAKE_INSTALL_BINDIR}")
  if(NOT _install_bindir)
    set(_install_bindir bin)
  endif()
  install(FILES ${OUTPUT_DIR}/${ARG_APPIMAGE_NAME}-x86_64.AppImage
    DESTINATION ${_install_bindir}
    RENAME ${ARG_APPIMAGE_NAME}.AppImage
  )
endfunction()
