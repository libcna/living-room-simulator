# Simulation tests: no graphics device needed, so they run anywhere the
# project builds (ctest --test-dir build).
add_executable(cna_room_sim_tests SimTests.cpp)
target_link_libraries(cna_room_sim_tests PRIVATE CnaRoom::Core CnaRoom::Warnings)
add_test(NAME sim COMMAND cna_room_sim_tests)

add_executable(cna_room_baker_tests BakerTests.cpp)
target_link_libraries(cna_room_baker_tests PRIVATE CnaRoom::Core CnaRoom::Warnings)
add_test(NAME bakers COMMAND cna_room_baker_tests)

add_executable(cna_room_reflection_tests ReflectionTests.cpp)
target_link_libraries(cna_room_reflection_tests PRIVATE CnaRoom::Core CnaRoom::Warnings)
add_test(NAME reflections COMMAND cna_room_reflection_tests)

add_executable(cna_room_cube_tests CubeTests.cpp)
target_link_libraries(cna_room_cube_tests PRIVATE CnaRoom::Core CnaRoom::Warnings)
add_test(NAME cubes COMMAND cna_room_cube_tests)

# A headless render: the binary starts, draws three frames at 320x180 and writes
# a screenshot that is not black. Skipped where xvfb-run is missing.
add_test(NAME render-smoke COMMAND "${CMAKE_CURRENT_SOURCE_DIR}/render-smoke.sh" "$<TARGET_FILE:cna-room>" "${CMAKE_BINARY_DIR}/render-smoke.png")
set_tests_properties(render-smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 600)
