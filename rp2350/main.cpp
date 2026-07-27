#include "game/game.hpp"

#include "pixel_twins/rp2350/led_panel.hpp"
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
    if (!game.initialize(wizward::game::Scene::Title, get_rand_32(),
                         readDifficultyDipSwitch())) {
        while (true) tight_loop_contents();
    }
    game.render();

    // USB2がDMA15を予約してから、LED側のDMAをcore 1で動的確保する。
    if (!usbControllers.initialize()) {
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

    // core 1のLED転送完了を60Hz更新の基準とする。
    // USBホストはcore 0のフレーム待ち時間にも進める。
    while (true) {
        const auto frameStart = time_us_32();
        usbControllers.task();
        usbControllers.update(controllers);
        const auto inputResult = game.processInput(controllers);
        const auto tickResult = game.tick(controllers);
        if (!inputResult.succeeded || !tickResult.succeeded) {
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
    }
}
