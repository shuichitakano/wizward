cmake_minimum_required(VERSION 3.20)

set(PICO_BOARD pico2 CACHE STRING "RP2350搭載ボード")
if(NOT DEFINED ENV{PICO_SDK_PATH})
    message(FATAL_ERROR "PICO_SDK_PATHにPico SDKのパスを設定してください")
endif()
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(wizward_rp2350 LANGUAGES C CXX ASM)
pico_sdk_init()

set(WIZWARD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WIZWARD_BUILD_APP OFF CACHE BOOL "" FORCE)
set(WIZWARD_BUILD_DEMO OFF CACHE BOOL "" FORCE)
set(WIZWARD_BUILD_AUDIO_DATA ON CACHE BOOL "" FORCE)
add_subdirectory(.. wizward)

add_executable(wizward_rp2350 main.cpp)
target_link_libraries(
    wizward_rp2350
    PRIVATE
        wizward::core
        pixel_twins::rp2350
        pico_multicore
        pico_rand
)
target_compile_features(wizward_rp2350 PRIVATE cxx_std_17)
target_compile_definitions(
    wizward_rp2350
    PRIVATE
        # ゲーム単体の最大約7.1KiBに、音声DMA/USB割り込みのネスト余裕を加える。
        PICO_STACK_SIZE=0x3000
        # core 1はmain.cppの独立した2KiBスタックを明示的に使う。
        PICO_CORE1_STACK_SIZE=0
)
pico_add_extra_outputs(wizward_rp2350)
