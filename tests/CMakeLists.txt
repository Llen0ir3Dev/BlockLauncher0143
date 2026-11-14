# Unit tests.  Every test is a standalone executable so a failure in one
# subsystem cannot mask another.  No proprietary game files are required.

set(NEXUS_TEST_TARGETS "")

function(nexus_add_test name source)
  add_executable(${name} ${source})
  nexus_target_defaults(${name})
  target_link_libraries(${name} PRIVATE ${ARGN})
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES TIMEOUT 120)
  set(NEXUS_TEST_TARGETS ${NEXUS_TEST_TARGETS} ${name} PARENT_SCOPE)
endfunction()

nexus_add_test(test_core unit/test_core.cpp nexus_core)
nexus_add_test(test_shader unit/test_shader.cpp nexus_shader)
nexus_add_test(test_texture unit/test_texture.cpp nexus_texture)
nexus_add_test(test_worldgen unit/test_worldgen.cpp nexus_worldgen)
nexus_add_test(test_bridge unit/test_bridge.cpp nexus_bridge)
nexus_add_test(test_mod unit/test_mod.cpp nexus_mod)
nexus_add_test(test_mcpe unit/test_mcpe.cpp nexus_mcpe)
nexus_add_test(test_game_source unit/test_game_source.cpp nexus_platform)

# End-to-end test through the built executables: CLI, shader pipeline, MCPE
# assets, worldgen, bridge roundtrip and launcher headless modes.  The real
# user-supplied APK is used only when it is present in the source directory.
if(TARGET nexus AND TARGET nexus-runtime AND TARGET nexus-launcher)
  add_test(
    NAME integration_cli
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/integration/cli.sh
            $<TARGET_FILE:nexus>
            $<TARGET_FILE:nexus-runtime>
            $<TARGET_FILE:nexus-launcher>
            ${CMAKE_SOURCE_DIR})
  set_tests_properties(integration_cli PROPERTIES TIMEOUT 300)
endif()
