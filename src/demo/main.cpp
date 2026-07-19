#include "game/game.hpp"

#include "audio/bgm_data.hpp"
#include "audio/sfx_data.hpp"

#include "pixel_twins/audio_system.hpp"
#include "pixel_twins/sdl_audio.hpp"
#include "pixel_twins/sdl_controller.hpp"
#include "pixel_twins/sdl_presenter.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace {

constexpr auto kSimulationStep = std::chrono::microseconds(16667);

bool hasArgument(int argc, char** argv, std::string_view expected) noexcept {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == expected) return true;
    }
    return false;
}

std::uint32_t mapSeed(int argc, char** argv) noexcept {
    for (int index = 1; index < argc; ++index) {
        std::string_view argument(argv[index]);
        const char* value = nullptr;
        if (argument == "--seed" && index + 1 < argc) value = argv[++index];
        else if (argument.rfind("--seed=", 0) == 0) value = argv[index] + 7;
        if (value == nullptr) continue;
        char* end = nullptr;
        const auto parsed = std::strtoul(value, &end, 0);
        if (end != value && *end == '\0') return static_cast<std::uint32_t>(parsed);
    }
    const auto ticks = static_cast<std::uint64_t>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count());
    auto seed = static_cast<std::uint32_t>(ticks) ^ static_cast<std::uint32_t>(ticks >> 32U);
    seed ^= seed >> 16U;
    seed *= 2246822519U;
    seed ^= seed >> 13U;
    return seed != 0U ? seed : 1U;
}

bool applyAudioEvent(wizward::game::AudioEvent event,
                     pixel_twins::sdl::AudioPlayer& player) noexcept {
    switch (event) {
    case wizward::game::AudioEvent::None: return true;
    case wizward::game::AudioEvent::PlayField: return player.playBgm(wizward::audio::kField);
    case wizward::game::AudioEvent::PlayBoss: return player.playBgm(wizward::audio::kBoss);
    case wizward::game::AudioEvent::PlayVictory: return player.playBgm(wizward::audio::kVictory);
    case wizward::game::AudioEvent::PlayNameEntry: return player.playBgm(wizward::audio::kNameEntry);
    case wizward::game::AudioEvent::StopBgm: return player.stopBgm();
    }
    return false;
}

const pixel_twins::SfxPreset& sfxPreset(wizward::game::SfxId id) noexcept {
    using wizward::game::SfxId;
    switch (id) {
    case SfxId::UiMove: return wizward::audio::kUiMove;
    case SfxId::Start: return wizward::audio::kStart;
    case SfxId::LightCast: return wizward::audio::kLightCast;
    case SfxId::FireCast: return wizward::audio::kFireCast;
    case SfxId::WindCast: return wizward::audio::kWindCast;
    case SfxId::ThunderCast: return wizward::audio::kThunderCast;
    case SfxId::IceCast: return wizward::audio::kIceCast;
    case SfxId::FamiliarCast: return wizward::audio::kFamiliarCast;
    case SfxId::Hit: return wizward::audio::kHit;
    case SfxId::Deflect: return wizward::audio::kDeflect;
    case SfxId::Kill: return wizward::audio::kKill;
    case SfxId::PlayerDamage: return wizward::audio::kPlayerDamage;
    case SfxId::Xp: return wizward::audio::kXp;
    case SfxId::Level: return wizward::audio::kLevel;
    case SfxId::Heal: return wizward::audio::kHeal;
    case SfxId::HpUp: return wizward::audio::kHpUp;
    case SfxId::Bomb: return wizward::audio::kBomb;
    case SfxId::SealJingle: return wizward::audio::kSealJingle;
    case SfxId::BossImpact: return wizward::audio::kBossImpact;
    case SfxId::BossRock: return wizward::audio::kBossRock;
    case SfxId::EnemySpawn: return wizward::audio::kEnemySpawn;
    case SfxId::EnemyShoot: return wizward::audio::kEnemyShoot;
    case SfxId::BossShoot: return wizward::audio::kBossShoot;
    case SfxId::BossGather: return wizward::audio::kBossGather;
    case SfxId::BossDeathImpact: return wizward::audio::kBossDeathImpact;
    case SfxId::BossDeathBlast: return wizward::audio::kBossDeathBlast;
    case SfxId::Clear: return wizward::audio::kClear;
    case SfxId::Down: return wizward::audio::kDown;
    case SfxId::Revive: return wizward::audio::kRevive;
    case SfxId::GameOver: return wizward::audio::kGameOver;
    }
    return wizward::audio::kUiMove;
}

bool applyUpdate(const wizward::game::UpdateResult& result,
                 pixel_twins::AudioSystem& audio,
                 pixel_twins::sdl::AudioPlayer& player) noexcept {
    if (!result.succeeded || !applyAudioEvent(result.audio, player)) return false;
    if (result.playStartSfx) {
        (void)audio.playSfx(pixel_twins::makeSfxRequest(wizward::audio::kStart));
    }
    for (std::size_t index = 0; index < result.sfxCueCount; ++index) {
        const auto& cue = result.sfxCues[index];
        auto request = pixel_twins::makeSfxRequest(sfxPreset(cue.id), cue.pan);
        request.voice.frequency *= cue.pitchScale;
        request.voice.endFrequency *= cue.pitchScale;
        request.voice.pitchCurveScale *= cue.pitchScale;
        request.voice.velocity *= cue.volumeScale;
        (void)audio.playSfx(request);
    }
    return true;
}

void updateBgmTrackTitle(pixel_twins::sdl::Presenter& presenter, std::uint8_t muteMask) noexcept {
    constexpr std::array<const char*, 7> kTrackNames{{
        "BASS", "DRUMS", "PERC", "SYNTH", "LEAD", "DELAY", "KEYS",
    }};
    std::array<char, 128> title{};
    auto offset = std::snprintf(title.data(), title.size(), "Wizward BGM");
    for (std::size_t track = 0; track < kTrackNames.size(); ++track) {
        if (offset < 0 || static_cast<std::size_t>(offset) >= title.size()) break;
        offset += std::snprintf(title.data() + offset, title.size() - static_cast<std::size_t>(offset),
                                "  F%zu:%s=%s", track + 1U, kTrackNames[track],
                                (muteMask & (1U << track)) != 0U ? "MUTE" : "ON");
    }
    presenter.setTitle(title.data());
}

// RP2350版と同様に、大きなゲーム状態をスタックへ置かない。
wizward::game::Game game;

} // namespace

int main(int argc, char** argv) {
    const auto once = hasArgument(argc, argv, "--once");
    const auto initialScene = hasArgument(argc, argv, "--gameplay")
        ? wizward::game::Scene::Gameplay : wizward::game::Scene::Title;
    const auto difficulty = hasArgument(argc, argv, "--hard")
        ? wizward::game::Difficulty::Hard : wizward::game::Difficulty::Easy;
    if (!game.initialize(initialScene, mapSeed(argc, argv), difficulty)) {
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
    std::uint8_t bgmTrackMuteMask = 0;
    updateBgmTrackTitle(presenter, bgmTrackMuteMask);
    auto previousTime = std::chrono::steady_clock::now();
    auto accumulatedTime = std::chrono::steady_clock::duration::zero();
    std::uint32_t presentedFrames = 0;
    while (presenter.processEvents(&controllerInput)) {
        const auto trackToggles = controllerInput.takeBgmTrackToggleMask();
        if (trackToggles != 0U) {
            bgmTrackMuteMask = static_cast<std::uint8_t>(bgmTrackMuteMask ^ trackToggles);
            if (!audioPlayer.setBgmTrackMuteMask(bgmTrackMuteMask)) return 1;
            updateBgmTrackTitle(presenter, bgmTrackMuteMask);
            std::fprintf(stderr, "BGM track mute mask: 0x%02x\n",
                         static_cast<unsigned>(bgmTrackMuteMask));
        }
        const auto frameStart = std::chrono::steady_clock::now();
        const auto currentTime = std::chrono::steady_clock::now();
        const auto maximumFrameTime = std::chrono::duration_cast<
            std::chrono::steady_clock::duration>(kSimulationStep * 4);
        accumulatedTime += std::min(currentTime - previousTime, maximumFrameTime);
        previousTime = currentTime;
        while (accumulatedTime >= kSimulationStep) {
            // Generate button edges immediately before the simulation consumes them.
            controllerInput.update(controllers);
            if (!applyUpdate(game.processInput(controllers), audioSystem, audioPlayer)) return 1;
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
