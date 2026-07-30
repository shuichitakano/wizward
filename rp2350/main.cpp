#include "game/game.hpp"

#include "audio/bgm_data.hpp"
#include "audio/sfx_data.hpp"
#include "job_system.hpp"
#include "maintenance_screen.hpp"
#include "ranking_store.hpp"

#include "pixel_twins/rp2350/led_panel.hpp"
#include "pixel_twins/rp2350/pwm_audio.hpp"
#include "pixel_twins/rp2350/usb_controller.hpp"
#include "pixel_twins/rp2350/board_pins.hpp"

#include "hardware/sync.h"
#include "pico/flash.h"
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
wizward::rp2350::JobSystem jobSystem;
wizward::rp2350::RankingStore rankingStore;
alignas(8) std::array<std::uint32_t, 512> ledCoreStack{};

std::atomic<const pixel_twins::PixelBuffer*> publishedBuffer{nullptr};
std::atomic<std::uint32_t> publishedFrame{0};
std::atomic<std::uint32_t> consumedFrame{0};
std::atomic<std::uint32_t> paletteRevision{1};
std::atomic<std::uint16_t> ledGammaTenths{22};
std::atomic<bool> ledCoreReady{false};
std::atomic<bool> flashPauseRequested{false};
std::atomic<bool> flashPauseReady{false};
std::atomic<std::uint32_t> profiledPresentUs{0};
std::uint32_t gamePairIdleUs = 0;

std::uint32_t ledInterruptActiveUs() noexcept {
    return ledPanel.totalPresentActiveUs();
}

struct RenderChainBatch;

struct RenderChain {
    RenderChainBatch* batch = nullptr;
    void* stepContext = nullptr;
};

struct RenderChainBatch {
    wizward::rp2350::JobSystem* jobs = nullptr;
    wizward::game::ParallelExecutor::Step step = nullptr;
    std::atomic<std::uint32_t> remaining{2};
    std::array<RenderChain, pixel_twins::kControllerCount> chains{};
};

void finishRenderChain(RenderChainBatch& batch) noexcept {
    if (batch.remaining.fetch_sub(1, std::memory_order_release) == 1) {
        __sev();
    }
}

void runRenderChainStep(void* context) noexcept {
    auto& chain = *static_cast<RenderChain*>(context);
    auto& batch = *chain.batch;
    if (!batch.step(chain.stepContext)) {
        finishRenderChain(batch);
        return;
    }
    if (batch.jobs->trySubmit(
            runRenderChainStep, &chain, nullptr,
            wizward::rp2350::JobSystem::Category::Render)) {
        return;
    }
    while (batch.step(chain.stepContext)) {}
    finishRenderChain(batch);
}

void invokeRenderChains(
        void* context, wizward::game::ParallelExecutor::Step step,
        void* firstContext, void* secondContext) noexcept {
    auto& jobs = *static_cast<wizward::rp2350::JobSystem*>(context);
    RenderChainBatch batch;
    batch.jobs = &jobs;
    batch.step = step;
    batch.chains[0] = {&batch, firstContext};
    batch.chains[1] = {&batch, secondContext};
    for (auto& chain : batch.chains) {
        if (!jobs.trySubmit(
                runRenderChainStep, &chain, nullptr,
                wizward::rp2350::JobSystem::Category::Render)) {
            runRenderChainStep(&chain);
        }
    }
    while (batch.remaining.load(std::memory_order_acquire) != 0) {
        if (!jobs.tryRunOne()) __wfe();
    }
}

void invokeGamePair(
        void* context,
        wizward::game::ParallelExecutor::Function first, void* firstContext,
        wizward::game::ParallelExecutor::Function second, void* secondContext) noexcept {
    auto& jobs = *static_cast<wizward::rp2350::JobSystem*>(context);
    wizward::rp2350::JobCounter counter;
    if (!jobs.trySubmit(
            first, firstContext, &counter,
            wizward::rp2350::JobSystem::Category::Game)) {
        first(firstContext);
    }
    if (!jobs.trySubmit(
            second, secondContext, &counter,
            wizward::rp2350::JobSystem::Category::Game)) {
        second(secondContext);
    }
    while (!counter.complete()) {
        const auto idleStart = time_us_32();
        if (jobs.tryRunOne()) continue;
        __wfe();
        gamePairIdleUs += time_us_32() - idleStart;
    }
}

// 実機デバッガからフレーム時間の内訳を確認する。
volatile std::uint32_t latestUpdateUs = 0;
volatile std::uint32_t latestRenderUs = 0;
volatile std::uint32_t latestFrameUs = 0;
volatile std::uint32_t maximumRenderUs = 0;
volatile std::uint32_t latestCore0RenderJobUs = 0;
volatile std::uint32_t latestCore1RenderJobUs = 0;
volatile std::uint32_t latestCore0GameActiveUs = 0;
volatile std::uint32_t latestCore1GameJobUs = 0;
volatile std::uint32_t latestCore0GameIdleUs = 0;
volatile std::uint32_t totalLedHoldScans = 0;

struct ProfileStamp {
    std::uint32_t timeUs;
    std::uint32_t audioUs;
};

ProfileStamp takeProfileStamp() noexcept {
    const auto interruptState = save_and_disable_interrupts();
    const ProfileStamp result{
        time_us_32(),
        audioPlayer.diagnostics().totalRenderUs,
    };
    restore_interrupts(interruptState);
    return result;
}

std::uint32_t sectionCpuUs(const ProfileStamp& start,
                           const ProfileStamp& end) noexcept {
    const auto wallUs = end.timeUs - start.timeUs;
    const auto audioUs = end.audioUs - start.audioUs;
    return wallUs > audioUs ? wallUs - audioUs : 0;
}

bool debugModeEnabled() noexcept {
    return !gpio_get(pixel_twins::rp2350::board::kConfig1Pin);
}

bool activeLowSwitchEnabled(std::uint8_t pin) noexcept {
    // Pad reset直後はpull-upが安定する前のLowを拾うことがある。
    sleep_us(1000);
    std::uint8_t lowSamples = 0;
    for (std::uint8_t sample = 0; sample < 8U; ++sample) {
        if (!gpio_get(pin)) ++lowSamples;
        sleep_us(250);
    }
    return lowSamples >= 6U;
}

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
    hard_assert(flash_safe_execute_core_init());
    ledPanel.initialize();
    ledPanel.setGamma(
        static_cast<float>(ledGammaTenths.load(std::memory_order_acquire)) / 10.0F);
    ledPanel.setPalette(game.framebuffer().palette());

    auto currentPaletteRevision = paletteRevision.load(std::memory_order_acquire);
    auto currentFrame = publishedFrame.load(std::memory_order_acquire);
    auto currentBuffer = publishedBuffer.load(std::memory_order_acquire);
    if (currentBuffer == nullptr || currentFrame == 0) {
        while (true) tight_loop_contents();
    }

    consumedFrame.store(currentFrame, std::memory_order_release);
    ledCoreReady.store(true, std::memory_order_release);
    hard_assert(ledPanel.startPresent(*currentBuffer));

    while (true) {
        if (flashPauseRequested.load(std::memory_order_acquire)) {
            if (ledPanel.holding()) {
                ledPanel.requestHoldStop();
                if (!jobSystem.tryRunOne()) __wfe();
                continue;
            }
            if (ledPanel.presenting()) {
                if (!jobSystem.tryRunOne()) __wfe();
                continue;
            }
            flashPauseReady.store(true, std::memory_order_release);
            __sev();
            while (flashPauseRequested.load(std::memory_order_acquire)) {
                __wfe();
            }
            flashPauseReady.store(false, std::memory_order_release);
            __sev();
            continue;
        }
        if (ledPanel.presenting()) {
            if (!jobSystem.tryRunOne()) __wfe();
            continue;
        }
        if (ledPanel.holding()) {
            if (publishedFrame.load(std::memory_order_acquire) != currentFrame) {
                ledPanel.requestHoldStop();
            }
            if (!jobSystem.tryRunOne()) __wfe();
            continue;
        }

        profiledPresentUs.store(
            ledPanel.lastPresentActiveUs(), std::memory_order_release);

        const auto nextFrame = publishedFrame.load(std::memory_order_acquire);
        if (nextFrame != currentFrame) {
            const auto nextPaletteRevision = paletteRevision.load(std::memory_order_acquire);
            if (nextPaletteRevision != currentPaletteRevision) {
                ledPanel.setGamma(
                    static_cast<float>(
                        ledGammaTenths.load(std::memory_order_acquire)) / 10.0F);
                ledPanel.setPalette(game.framebuffer().palette());
                currentPaletteRevision = nextPaletteRevision;
            }
            currentBuffer = publishedBuffer.load(std::memory_order_acquire);
            currentFrame = nextFrame;

            // この時点で旧表示バッファをcore 0が再利用できる。
            consumedFrame.store(currentFrame, std::memory_order_release);
            __sev();
            hard_assert(ledPanel.startPresent(*currentBuffer));
        } else {
            // 次フレームが遅れている間は旧表示のPWMだけを1走査ずつ継続する。
            // VSyncも輝度データも送らないため、ティアリングは発生しない。
            hard_assert(ledPanel.startHoldScan());
            ++totalLedHoldScans;
        }
    }
}

bool savePersistentState() noexcept {
    // Flash erase中に音声DMAが二周すると、復帰後IRQが既に再チェインされた
    // 左chの完了を永久待ちする。DMAを停止してからcore lockoutへ入る。
    usbControllers.suspendPioHostForFlash();
    audioPlayer.suspendForFlash();
    flashPauseRequested.store(true, std::memory_order_release);
    __sev();
    while (!flashPauseReady.load(std::memory_order_acquire)) {
        __wfe();
    }
    const auto saved = rankingStore.save(game);
    flashPauseRequested.store(false, std::memory_order_release);
    __sev();
    usbControllers.resumePioHostAfterFlash();
    audioPlayer.resumeAfterFlash();
    if (saved) game.markRankingsSaved();
    return saved;
}

[[noreturn]] void runMaintenanceMode(
        wizward::rp2350::MaintenanceScreen& screen,
        std::uint32_t frame) noexcept {
    constexpr std::uint32_t kHeartbeatFrames = 30;
    std::uint32_t heartbeatFrame = 0;
    bool heartbeatLed = true;
    while (true) {
        usbControllers.task();
        usbControllers.update(controllers);
        const auto action = screen.update(controllers, game);
        if (action.gammaChanged) {
            rankingStore.setGammaTenths(screen.gammaTenths());
            ledGammaTenths.store(screen.gammaTenths(), std::memory_order_release);
            paletteRevision.fetch_add(1, std::memory_order_release);
        }
        if (action.saveRequested) (void)savePersistentState();
        using wizward::rp2350::SoundTestCommand;
        switch (action.soundTest) {
        case SoundTestCommand::None:
            break;
        case SoundTestCommand::Stop:
            (void)audioPlayer.stopBgm();
            break;
        case SoundTestCommand::StereoLeft:
            (void)audioPlayer.playSfx(pixel_twins::makeSfxRequest(
                wizward::audio::kSealJingle, -1.0F));
            break;
        case SoundTestCommand::StereoRight:
            (void)audioPlayer.playSfx(pixel_twins::makeSfxRequest(
                wizward::audio::kSealJingle, 1.0F));
            break;
        case SoundTestCommand::Field:
            (void)audioPlayer.playBgm(wizward::audio::kField);
            break;
        case SoundTestCommand::Endless:
            (void)audioPlayer.playBgm(wizward::audio::kEndless);
            break;
        case SoundTestCommand::Boss:
            (void)audioPlayer.playBgm(wizward::audio::kBoss);
            break;
        case SoundTestCommand::Victory:
            (void)audioPlayer.playBgm(wizward::audio::kVictory);
            break;
        case SoundTestCommand::NameEntry:
            (void)audioPlayer.playBgm(wizward::audio::kNameEntry);
            break;
        }
        screen.render(game, controllers);
        ++frame;
        publishedBuffer.store(
            &game.framebuffer().displayBuffer(), std::memory_order_relaxed);
        publishedFrame.store(frame, std::memory_order_release);
        while (consumedFrame.load(std::memory_order_acquire) != frame) {
            usbControllers.task();
            if (!jobSystem.tryRunOne()) __wfe();
        }
        if (++heartbeatFrame == kHeartbeatFrames) {
            heartbeatFrame = 0;
            heartbeatLed = !heartbeatLed;
            gpio_put(PICO_DEFAULT_LED_PIN, heartbeatLed);
        }
    }
}

} // namespace

int main() {
    stdio_init_all();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);
    gpio_put(PICO_DEFAULT_LED_PIN, true);
    gpio_init(pixel_twins::rp2350::board::kConfig1Pin);
    gpio_set_dir(pixel_twins::rp2350::board::kConfig1Pin, GPIO_IN);
    gpio_pull_up(pixel_twins::rp2350::board::kConfig1Pin);
    gpio_init(pixel_twins::rp2350::board::kConfig2Pin);
    gpio_set_dir(pixel_twins::rp2350::board::kConfig2Pin, GPIO_IN);
    gpio_pull_up(pixel_twins::rp2350::board::kConfig2Pin);
    const auto maintenanceMode = activeLowSwitchEnabled(
        pixel_twins::rp2350::board::kConfig2Pin);

    if (!game.initialize(wizward::game::Scene::Title, get_rand_32(),
                         readDifficultyDipSwitch())) {
        while (true) tight_loop_contents();
    }
    rankingStore.load(game);
    ledGammaTenths.store(rankingStore.gammaTenths(), std::memory_order_relaxed);
    wizward::rp2350::MaintenanceScreen maintenanceScreen{
        rankingStore.gammaTenths()};
    if (maintenanceMode) {
        wizward::rp2350::applyMaintenancePalette(game.framebuffer());
        maintenanceScreen.render(game, controllers);
    } else {
        game.render();
    }

    // USB2がDMA15を予約してから、LED側のDMAをcore 1で動的確保する。
    if (!usbControllers.initialize()) {
        while (true) tight_loop_contents();
    }
    if (!audioPlayer.initialize()) {
        while (true) tight_loop_contents();
    }
    jobSystem.initialize(ledInterruptActiveUs);
    const wizward::game::ParallelExecutor renderExecutor{
        &jobSystem, invokeGamePair, invokeRenderChains};

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
    if (maintenanceMode) runMaintenanceMode(maintenanceScreen, frame);

    auto paletteScene = game.scene();
    constexpr std::uint32_t kHeartbeatFrames = 30;
    std::uint32_t heartbeatFrame = 0;
    bool heartbeatLed = true;
    wizward::game::PerformanceOverlay performanceOverlay{};
    auto fpsWindowStart = time_us_32();
    std::uint32_t fpsWindowFrames = 0;

    // core 1のLED転送完了を60Hz更新の基準とする。
    // USBホストはcore 0のフレーム待ち時間にも進める。
    while (true) {
        const auto frameStart = takeProfileStamp();
        usbControllers.task();
        usbControllers.update(controllers);
        const auto debugMode = debugModeEnabled();
        game.setDebugMode(debugMode);
        const auto updateStart = takeProfileStamp();
        gamePairIdleUs = 0;
        const auto inputResult = game.processInput(controllers);
        const auto tickResult = game.tick(controllers, &renderExecutor);
        if (!applyUpdate(inputResult) || !applyUpdate(tickResult)) {
            while (true) tight_loop_contents();
        }
        const auto sceneChanged = game.scene() != paletteScene;
        if (sceneChanged
            && game.scene() == wizward::game::Scene::Title
            && game.rankingsDirty()) {
            (void)savePersistentState();
        }
        if (sceneChanged) {
            paletteScene = game.scene();
            paletteRevision.fetch_add(1, std::memory_order_release);
        }
        const auto updateEnd = takeProfileStamp();
        performanceOverlay.enabled = debugMode;
        game.setPerformanceOverlay(performanceOverlay);
        game.render(&renderExecutor);
        const auto renderEnd = takeProfileStamp();

        ++frame;
        publishedBuffer.store(
            &game.framebuffer().displayBuffer(), std::memory_order_relaxed);
        publishedFrame.store(frame, std::memory_order_release);
        const auto workEnd = takeProfileStamp();
        while (consumedFrame.load(std::memory_order_acquire) != frame) {
            if (!jobSystem.tryRunOne()) __wfe();
        }
        const auto frameEnd = takeProfileStamp();

        const auto updateUs = sectionCpuUs(updateStart, updateEnd);
        const auto renderUs = sectionCpuUs(updateEnd, renderEnd);
        const auto audioUs = frameEnd.audioUs - frameStart.audioUs;
        const auto frameUs = frameEnd.timeUs - frameStart.timeUs;
        const auto preWaitCpuUs = sectionCpuUs(frameStart, workEnd);
        const auto accountedCpuUs = updateUs + renderUs;
        const auto otherUs =
            preWaitCpuUs > accountedCpuUs ? preWaitCpuUs - accountedCpuUs : 0;
        const auto core0Jobs = jobSystem.takeProfile(0);
        const auto core1Jobs = jobSystem.takeProfile(1);
        latestCore0RenderJobUs = core0Jobs.renderUs;
        latestCore1RenderJobUs = core1Jobs.renderUs;
        latestCore0GameActiveUs =
            updateUs > gamePairIdleUs ? updateUs - gamePairIdleUs : 0;
        latestCore1GameJobUs = core1Jobs.gameUs;
        latestCore0GameIdleUs = gamePairIdleUs;
        performanceOverlay.cores[0] = {
            core0Jobs.renderUs != 0 ? core0Jobs.renderUs : renderUs,
            audioUs,
            latestCore0GameActiveUs,
            otherUs,
        };
        performanceOverlay.cores[1] = {
            core1Jobs.renderUs,
            core1Jobs.audioUs,
            core1Jobs.gameUs,
            core1Jobs.otherUs,
            profiledPresentUs.load(std::memory_order_acquire),
        };

        ++fpsWindowFrames;
        const auto fpsWindowUs = frameEnd.timeUs - fpsWindowStart;
        if (fpsWindowUs >= 500'000U) {
            const auto numerator =
                static_cast<std::uint64_t>(fpsWindowFrames) * 10'000'000U;
            performanceOverlay.fpsTenths = static_cast<std::uint16_t>(
                (numerator + fpsWindowUs / 2U) / fpsWindowUs);
            fpsWindowStart = frameEnd.timeUs;
            fpsWindowFrames = 0;
        }

        latestUpdateUs = updateUs;
        latestRenderUs = renderUs;
        latestFrameUs = frameUs;
        if (latestRenderUs > maximumRenderUs) maximumRenderUs = latestRenderUs;

        if (++heartbeatFrame == kHeartbeatFrames) {
            heartbeatFrame = 0;
            heartbeatLed = !heartbeatLed;
            gpio_put(PICO_DEFAULT_LED_PIN, heartbeatLed);
        }
    }
}
