find_package(GTest CONFIG REQUIRED)
include(GoogleTest)

# dg_add_test(<name> SOURCES ... DEPS ... [LABELS ...])
# One GoogleTest executable per suite. Every TEST() is registered with CTest individually as "<name>.<Suite>.<Test>",
# labelled "unit" plus LABELS, so `ctest -L determinism` or `-R HandlePool` selects precise subsets.
function(dg_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS;LABELS" ${ARGN})
    add_executable(${name} ${ARG_SOURCES} "${CMAKE_CURRENT_SOURCE_DIR}/support/TestEnvironment.cpp")
    target_link_libraries(${name} PRIVATE ${ARG_DEPS} GTest::gtest GTest::gtest_main)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    dg_configure_target(${name})
    set_target_properties(${name} PROPERTIES FOLDER "tests")

    # A list value would be split into separate set_tests_properties() arguments; escape it.
    set(labels unit ${ARG_LABELS})
    string(REPLACE ";" "\\;" labels "${labels}")
    # PRE_TEST discovery runs the binary at ctest time, not build time, so sanitizer and cross builds still build.
    gtest_discover_tests(${name}
        TEST_PREFIX "${name}."
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS "${labels}" TIMEOUT 300) # a hang fails one test in 5 minutes instead of stalling CI
endfunction()

dg_add_test(dg-core-tests SOURCES core/MemoryTest.cpp core/HandleTest.cpp core/LogTest.cpp DEPS dg::core LABELS determinism)
dg_add_test(dg-fixed-tests SOURCES math/FixedTest.cpp DEPS dg::math LABELS determinism)
dg_add_test(dg-detmath-tests SOURCES math/DetMathTest.cpp DEPS dg::math LABELS determinism)
dg_add_test(dg-vector-tests SOURCES math/VectorTest.cpp DEPS dg::math LABELS determinism)
# Same seed + inputs => same hash, plus a golden hash that must match on every platform.
dg_add_test(dg-sim-tests SOURCES sim/SimDeterminismTest.cpp DEPS dg::sim LABELS determinism)

# Smoke tests: each app boots, runs headless and exits cleanly.
add_test(NAME player.smoke COMMAND dg-player --headless --fast --ticks 120)
set_tests_properties(player.smoke PROPERTIES LABELS "smoke")
if(TARGET dg-server)
    add_test(NAME server.smoke COMMAND dg-server --fast --ticks 120)
    set_tests_properties(server.smoke PROPERTIES LABELS "smoke")
endif()
if(TARGET dg-editor)
    add_test(NAME editor.smoke COMMAND dg-editor --fast --ticks 120)
    set_tests_properties(editor.smoke PROPERTIES LABELS "smoke")
endif()
