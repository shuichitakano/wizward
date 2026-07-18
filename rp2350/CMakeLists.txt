cmake_minimum_required(VERSION 3.20)

if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATHにPico SDKのパスを設定してください")
endif()
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(wizward_rp2350 LANGUAGES C CXX ASM)
set(PICO_BOARD pico2 CACHE STRING "RP2350搭載ボード")
pico_sdk_init()

set(WIZWARD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WIZWARD_BUILD_APP OFF CACHE BOOL "" FORCE)
set(WIZWARD_BUILD_DEMO OFF CACHE BOOL "" FORCE)
add_subdirectory(.. wizward)

add_executable(wizward_rp2350 main.cpp)
target_link_libraries(wizward_rp2350 PRIVATE wizward::core pixel_twins::rp2350)
target_compile_features(wizward_rp2350 PRIVATE cxx_std_17)
pico_add_extra_outputs(wizward_rp2350)
