#include "game/game.hpp"

#include "audio/bgm_data.hpp"
#include "audio/sfx_data.hpp"

#include "pixel_twins/rp2350/led_panel.hpp"
#include "pixel_twins/rp2350/pwm_audio.hpp"
#include "pixel_twins/rp2350/usb_controller.hpp"

#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/rand.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace {

// フレームバッファ、マップ、LED転送状態をスタックに置かない。
wizward::game::Game game;
pixel_twins::Controllers controllers;
pixel_twins::rp2350::LedPanelDriver ledPanel;
pixel_twins::rp2350::PwmAudioPlayer audioPlayer;
pixel_twins::rp2350::UsbControllerInput usbControllers;
alignas(8) std::array<std::uint32_t, 512> ledCoreStack{};

std::atomic<const pixel_twins::PixelBuffer*> publishedBuffer{nullptr};
std::atomic<std::uint32_t> publishedFrame{0};
std::atomic<std::uint32_t> consumedFrame{0};
std::atomic<std::uint32_t> paletteRevision{1};
std::atomic<bool> ledCoreReady{false};

// 実機デバッガからフレーム時間の内訳を確認する。
volatile std::uint32_t latestPresentUs = 0;
volatile std::uint32_t latestUpdateUs = 0;
volatile std::uint32_t latestRenderUs = 0;
volatile std::uint32_t latestFrameUs = 0;
volatile std::uint32_t maximumRenderUs = 0;

wizward::game::Difficulty readDifficultyDipSwitch() noexcept {
    // ボードI/O確定後、HARD用DIPSWの入力をこの境界へ接続する。
    return wizward::game::Difficulty::Easy;
}

bool applyAudioEvent(wizward::game::AudioEvent event) noexcept {
    switch (event) {
    case wizward::game::AudioEvent::None: return true;
    case wizward::game::AudioEvent::PlayField:
        return audioPlayer.playBgm(wizward::audio::kField);
    case wizward::game::AudioEvent::PlayEndless:
        return audioPlayer.playBgm(wizward::audio::kEndless);
    case wizward::game::AudioEvent::PlayBoss:
        return audioPlayer.playBgm(wizward::audio::kBoss);
    case wizward::game::AudioEvent::PlayVictory:
        return audioPlayer.playBgm(wizward::audio::kVictory);
    case wizward::game::AudioEvent::PlayNameEntry:
        return audioPlayer.playBgm(wizward::audio::kNameEntry);
    case wizward::game::AudioEvent::StopBgm:
        return audioPlayer.stopBgm();
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

bool applyUpdate(const wizward::game::UpdateResult& result) noexcept {
    if (!result.succeeded || !applyAudioEvent(result.audio)) return false;
    if (result.playStartSfx
        && !audioPlayer.playSfx(
            pixel_twins::makeSfxRequest(wizward::audio::kStart))) {
        return false;
    }
    for (std::size_t index = 0; index < result.sfxCueCount; ++index) {
        const auto& cue = result.sfxCues[index];
        auto request = pixel_twins::makeSfxRequest(sfxPreset(cue.id), cue.pan);
        request.voice.frequency *= cue.pitchScale;
        request.voice.endFrequency *= cue.pitchScale;
        request.voice.pitchCurveScale *= cue.pitchScale;
        request.voice.velocity *= cue.volumeScale;
        if (!audioPlayer.playSfx(request)) return false;
    }
    return true;
}

void ledCoreMain() noexcept {
    ledPanel.initialize();
    ledPanel.setPalette(game.framebuffer().palette());

    auto currentPaletteRevision = paletteRevision.load(std::memory_order_acquire);
    auto currentFrame = publishedFrame.load(std::memory_order_acquire);
    auto currentBuffer = publishedBuffer.load(std::memory_order_acquire);
    if (currentBuffer == nullptr || currentFrame == 0) {
        while (true) tight_loop_contents();
    }

    consumedFrame.store(currentFrame, std::memory_order_release);
    ledCoreReady.store(true, std::memory_order_release);

    while (true) {
        const auto presentStart = time_us_32();
        ledPanel.present(*currentBuffer);
        latestPresentUs = time_us_32() - presentStart;

        const auto nextFrame = publishedFrame.load(std::memory_order_acquire);
        if (nextFrame == currentFrame) continue;

        const auto nextPaletteRevision = paletteRevision.load(std::memory_order_acquire);
        if (nextPaletteRevision != currentPaletteRevision) {
            ledPanel.setPalette(game.framebuffer().palette());
            currentPaletteRevision = nextPaletteRevision;
        }
        currentBuffer = publishedBuffer.load(std::memory_order_acquire);
        currentFrame = nextFrame;

        // この時点で旧表示バッファをcore 0が再利用できる。
        consumedFrame.store(currentFrame, std::memory_order_release);
    }
}

} // namespace

int main() {
    stdio_init_all();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, true);

    if (!game.initialize(wizward::game::Scene::Title, get_rand_32(),
                         readDifficultyDipSwitch())) {
        while (true) tight_loop_contents();
    }
    game.render();

    // USB2がDMA15を予約してから、LED側のDMAをcore 1で動的確保する。
    if (!usbControllers.initialize()) {
        while (true) tight_loop_contents();
    }
    if (!audioPlayer.initialize()) {
        while (true) tight_loop_contents();
    }

    std::uint32_t frame = 1;
    publishedBuffer.store(
        &game.framebuffer().displayBuffer(), std::memory_order_relaxed);
    publishedFrame.store(frame, std::memory_order_release);
    multicore_launch_core1_with_stack(
        ledCoreMain, ledCoreStack.data(), sizeof(ledCoreStack));
    while (!ledCoreReady.load(std::memory_order_acquire)) {
        usbControllers.task();
        tight_loop_contents();
    }

    auto paletteScene = game.scene();
    constexpr std::uint32_t kHeartbeatFrames = 30;
    std::uint32_t heartbeatFrame = 0;
    bool heartbeatLed = true;

    // core 1のLED転送完了を60Hz更新の基準とする。
    // USBホストはcore 0のフレーム待ち時間にも進める。
    while (true) {
        const auto frameStart = time_us_32();
        usbControllers.task();
        usbControllers.update(controllers);
        if (!applyUpdate(game.processInput(controllers))
            || !applyUpdate(game.tick(controllers))) {
            while (true) tight_loop_contents();
        }

        if (game.scene() != paletteScene) {
            paletteScene = game.scene();
            paletteRevision.fetch_add(1, std::memory_order_release);
        }
        const auto updateEnd = time_us_32();
        game.render();
        const auto renderEnd = time_us_32();

        ++frame;
        publishedBuffer.store(
            &game.framebuffer().displayBuffer(), std::memory_order_relaxed);
        publishedFrame.store(frame, std::memory_order_release);
        while (consumedFrame.load(std::memory_order_acquire) != frame) {
            usbControllers.task();
            tight_loop_contents();
        }
        const auto frameEnd = time_us_32();

        latestUpdateUs = updateEnd - frameStart;
        latestRenderUs = renderEnd - updateEnd;
        latestFrameUs = frameEnd - frameStart;
        if (latestRenderUs > maximumRenderUs) maximumRenderUs = latestRenderUs;

        if (++heartbeatFrame == kHeartbeatFrames) {
            heartbeatFrame = 0;
            heartbeatLed = !heartbeatLed;
            gpio_put(PICO_DEFAULT_LED_PIN, heartbeatLed);
        }
    }
}
