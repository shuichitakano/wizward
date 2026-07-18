#include "game/game.hpp"

#include "pixel_twins/audio_system.hpp"

#include "pico/stdlib.h"

namespace {

// フレームバッファ、マップ、音源状態をスタックに置かない。
wizward::game::Game game;
pixel_twins::Controllers controllers;
pixel_twins::AudioSystem audioSystem;

} // namespace

int main() {
    stdio_init_all();
    if (!game.initialize()) {
        while (true) tight_loop_contents();
    }
    game.render();

    // 入力、LED転送、PCM DMAはボード仕様の確定後にこの境界へ接続する。
    // 共通更新はprocessInput()、tick()、render()だけを使用する。
    (void)controllers;
    (void)audioSystem;
    while (true) tight_loop_contents();
}
