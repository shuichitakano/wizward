cmake_minimum_required(VERSION 3.20)

project(wizward VERSION 0.1.0 LANGUAGES CXX)

option(WIZWARD_BUILD_TESTS "Wizwardのテストをビルドする" ON)
option(WIZWARD_BUILD_APP "SDLによるmacOS版アプリをビルドする" OFF)
option(WIZWARD_BUILD_DEMO "旧名。WIZWARD_BUILD_APPと同じ" OFF)
set(WIZWARD_PIXEL_TWINS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/pixel-twins"
    CACHE PATH "Pixel Twinsソースディレクトリ")
if(NOT EXISTS "${WIZWARD_PIXEL_TWINS_DIR}/CMakeLists.txt")
    get_filename_component(_wizward_sibling_pixel_twins "${CMAKE_CURRENT_SOURCE_DIR}/../pixel-twins" ABSOLUTE)
    if(EXISTS "${_wizward_sibling_pixel_twins}/CMakeLists.txt")
        set(WIZWARD_PIXEL_TWINS_DIR "${_wizward_sibling_pixel_twins}")
    else()
        message(FATAL_ERROR "Pixel Twinsサブモジュールを更新してください")
    endif()
endif()

set(PIXEL_TWINS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(PIXEL_TWINS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory("${WIZWARD_PIXEL_TWINS_DIR}" "${CMAKE_BINARY_DIR}/pixel-twins")

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(WIZWARD_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(WIZWARD_GAMEPLAY_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets/converted/gameplay")
set(WIZWARD_TITLE_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets/converted/title")
add_custom_command(
    OUTPUT "${WIZWARD_GENERATED_DIR}/embedded_assets.cpp"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${WIZWARD_GENERATED_DIR}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/embed_assets.py"
        --output "${WIZWARD_GENERATED_DIR}/embedded_assets.cpp"
        --background "${WIZWARD_GAMEPLAY_ASSETS}/background.bin"
        --sprites "${WIZWARD_GAMEPLAY_ASSETS}/sprites.bin"
        --palette "${WIZWARD_GAMEPLAY_ASSETS}/palette.bin"
        --title-screen "${WIZWARD_TITLE_ASSETS}/screen.bin"
        --title-sprites "${WIZWARD_TITLE_ASSETS}/logo.bin"
        --title-palette "${WIZWARD_TITLE_ASSETS}/palette.bin"
    DEPENDS
        tools/embed_assets.py
        "${WIZWARD_GAMEPLAY_ASSETS}/background.bin"
        "${WIZWARD_GAMEPLAY_ASSETS}/sprites.bin"
        "${WIZWARD_GAMEPLAY_ASSETS}/palette.bin"
        "${WIZWARD_TITLE_ASSETS}/screen.bin"
        "${WIZWARD_TITLE_ASSETS}/logo.bin"
        "${WIZWARD_TITLE_ASSETS}/palette.bin"
    VERBATIM
)

add_library(wizward_core
    src/audio/audio_assets.cpp
    src/audio/bgm_data.cpp
    src/audio/sfx_data.cpp
    src/assets/game_assets.cpp
    src/assets/title_assets.cpp
    src/assets/wizward_font.cpp
    src/game/game.cpp
    src/world/world_map.cpp
    "${WIZWARD_GENERATED_DIR}/embedded_assets.cpp"
)
add_library(wizward::core ALIAS wizward_core)
target_include_directories(wizward_core
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${WIZWARD_GAMEPLAY_ASSETS}"
        "${WIZWARD_TITLE_ASSETS}"
)
target_compile_features(wizward_core PUBLIC cxx_std_17)
target_compile_options(wizward_core PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
target_link_libraries(wizward_core PUBLIC pixel_twins::pixel_twins)
set_target_properties(wizward_core PROPERTIES CXX_EXTENSIONS OFF)

if(WIZWARD_BUILD_APP OR WIZWARD_BUILD_DEMO)
    add_subdirectory(
        "${WIZWARD_PIXEL_TWINS_DIR}/platform/sdl"
        "${CMAKE_BINARY_DIR}/pixel-twins-sdl"
    )
    add_executable(wizward src/demo/main.cpp)
    target_compile_features(wizward PRIVATE cxx_std_17)
    target_compile_options(wizward PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    target_link_libraries(wizward
        PRIVATE
            wizward::core
            pixel_twins::sdl
    )
    set_target_properties(wizward PROPERTIES CXX_EXTENSIONS OFF)
endif()

if(WIZWARD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
