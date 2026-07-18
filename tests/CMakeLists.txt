add_executable(wizward_asset_tests asset_test.cpp)
target_link_libraries(wizward_asset_tests PRIVATE wizward::core)
target_compile_options(wizward_asset_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
add_test(NAME wizward_asset_tests COMMAND wizward_asset_tests)

add_executable(wizward_world_map_tests world_map_test.cpp)
target_link_libraries(wizward_world_map_tests PRIVATE wizward::core)
target_compile_options(wizward_world_map_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
add_test(NAME wizward_world_map_tests COMMAND wizward_world_map_tests)
