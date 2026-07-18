#include "game/game.hpp"

#include "audio/bgm_data.hpp"
#include "audio/sfx_data.hpp"

#include "pixel_twins/audio_system.hpp"
#include "pixel_twins/sdl_audio.hpp"
#include "pixel_twins/sdl_controller.hpp"
#include "pixel_twins/sdl_presenter.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace {

constexpr auto kSimulationStep = std::chrono::microseconds(16667);

bool hasArgument(int argc, char** argv, std::string_view expected) noexcept {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == expected) return true;
    }
    return false;
}

bool applyAudioEvent(wizward::game::AudioEvent event,
                     pixel_twins::sdl::AudioPlayer& player) noexcept {
    switch (event) {
    case wizward::game::AudioEvent::None: return true;
    case wizward::game::AudioEvent::PlayField: return player.playBgm(wizward::audio::kField);
    case wizward::game::AudioEvent::PlayVictory: return player.playBgm(wizward::audio::kVictory);
    case wizward::game::AudioEvent::StopBgm: return player.stopBgm();
    }
    return false;
}

bool applyUpdate(const wizward::game::UpdateResult& result,
                 pixel_twins::AudioSystem& audio,
                 pixel_twins::sdl::AudioPlayer& player) noexcept {
    if (!result.succeeded || !applyAudioEvent(result.audio, player)) return false;
    if (result.playStartSfx) {
        (void)audio.playSfx(pixel_twins::makeSfxRequest(wizward::audio::kStart));
    }
    return true;
}

// RP2350版と同様に、大きなゲーム状態をスタックへ置かない。
wizward::game::Game game;

} // namespace

int main(int argc, char** argv) {
    const auto once = hasArgument(argc, argv, "--once");
    const auto initialScene = hasArgument(argc, argv, "--gameplay")
        ? wizward::game::Scene::Gameplay : wizward::game::Scene::Title;
    if (!game.initialize(initialScene)) {
        std::fputs("Wizwardアセットまたはマップの初期化に失敗しました\n", stderr);
        return 1;
    }

    pixel_twins::sdl::Presenter presenter(4, !once);
    pixel_twins::sdl::ControllerInput controllerInput;
    pixel_twins::AudioSystem audioSystem;
    pixel_twins::sdl::AudioPlayer audioPlayer(audioSystem);
    if (initialScene == wizward::game::Scene::Gameplay
        && !audioPlayer.playBgm(wizward::audio::kField)) return 1;
    pixel_twins::Controllers controllers;
    auto previousTime = std::chrono::steady_clock::now();
    auto accumulatedTime = std::chrono::steady_clock::duration::zero();
    std::uint32_t presentedFrames = 0;
    while (presenter.processEvents(&controllerInput)) {
        const auto frameStart = std::chrono::steady_clock::now();
        const auto currentTime = std::chrono::steady_clock::now();
        const auto maximumFrameTime = std::chrono::duration_cast<
            std::chrono::steady_clock::duration>(kSimulationStep * 4);
        accumulatedTime += std::min(currentTime - previousTime, maximumFrameTime);
        previousTime = currentTime;
        controllerInput.update(controllers);
        if (!applyUpdate(game.processInput(controllers), audioSystem, audioPlayer)) return 1;
        while (accumulatedTime >= kSimulationStep) {
            if (!applyUpdate(game.tick(controllers), audioSystem, audioPlayer)) return 1;
            accumulatedTime -= kSimulationStep;
        }

        game.render();
        presenter.present(game.framebuffer());
        ++presentedFrames;
        if (presentedFrames % 60U == 0U) {
            const auto elapsed = std::chrono::duration<float, std::milli>(
                std::chrono::steady_clock::now() - frameStart).count();
            std::fprintf(stderr, "frame: %.2f ms, audio: %s\n",
                         static_cast<double>(elapsed), audioPlayer.healthy() ? "ok" : "error");
        }
        if (once) break;
    }
    return 0;
}
