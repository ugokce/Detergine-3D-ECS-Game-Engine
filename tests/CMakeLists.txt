# Determinism unit test: same seed + inputs => same hash, and a golden hash that must match on every platform.
add_executable(dg-sim-tests SimDeterminismTest.cpp)
target_link_libraries(dg-sim-tests PRIVATE dg::sim)
dg_configure_target(dg-sim-tests)
set_target_properties(dg-sim-tests PROPERTIES FOLDER "tests")
add_test(NAME sim.determinism COMMAND dg-sim-tests)
set_tests_properties(sim.determinism PROPERTIES LABELS "determinism")

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
