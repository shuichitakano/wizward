#include "assets/game_assets.hpp"
#include "assets/title_assets.hpp"
#include "assets/wizward_font.hpp"
#include "audio/bgm_data.hpp"
#include "audio/sfx_data.hpp"
#include "world/world_map.hpp"

#include "pixel_twins/controller.hpp"
#include "pixel_twins/audio_system.hpp"
#include "pixel_twins/font.hpp"
#include "pixel_twins/framebuffer.hpp"
#include "pixel_twins/primitives.hpp"
#include "pixel_twins/render_target.hpp"
#include "pixel_twins/sdl_controller.hpp"
#include "pixel_twins/sdl_audio.hpp"
#include "pixel_twins/sdl_presenter.hpp"
#include "pixel_twins/sprite.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace {

constexpr std::int32_t kMapPixelWidth = wizward::world::kMapColumns * 8;
constexpr std::int32_t kMapPixelHeight = wizward::world::kMapRows * 8;
constexpr std::uint32_t kTitleFrames = 150;
constexpr auto kSimulationStep = std::chrono::microseconds(16667);

struct Camera {
    float x;
    float y;
};

enum class Scene : std::uint8_t {
    Title,
    Gameplay,
    Result,
};

void moveCamera(Camera& camera, const pixel_twins::ControllerState& controller) noexcept {
    constexpr float speed = 2.0F;
    constexpr float axisScale = speed / 32767.0F;
    camera.x += static_cast<float>(controller.x) * axisScale;
    camera.y += static_cast<float>(controller.y) * axisScale;
    camera.x = std::clamp(camera.x,
                          0.0F,
                          static_cast<float>(kMapPixelWidth - static_cast<std::int32_t>(pixel_twins::kPanelWidth)));
    camera.y = std::clamp(camera.y,
                          0.0F,
                          static_cast<float>(kMapPixelHeight - static_cast<std::int32_t>(pixel_twins::kScreenHeight)));
}

void drawAsset(pixel_twins::RenderTarget target,
               const wizward::assets::GameAssets& assets,
               wizward::assets::SpriteAssetId id,
               std::uint32_t frame,
               std::int16_t x,
               std::int16_t y) noexcept {
    pixel_twins::Sprite sprite{};
    if (assets.makeLoopingSprite(id, frame, 0, x, y, sprite)) {
        pixel_twins::drawSprite(target, sprite);
    }
}

void drawTitle(pixel_twins::Framebuffer& framebuffer,
               const wizward::assets::TitleAssets& title,
               std::uint32_t frame) noexcept {
    auto left = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Left);
    auto right = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Right);
    title.drawScreen(left);
    title.drawScreen(right);

    pixel_twins::Sprite logo{};
    if (title.makeLogo(28, 18, logo)) {
        pixel_twins::drawSprite(right, logo);
    }
    if ((frame / 30U) % 2U == 0U) {
        pixel_twins::drawText(left, wizward::assets::kWizwardFont, 40, 98, "PRESS START", 255);
        pixel_twins::drawText(right, wizward::assets::kWizwardFont, 40, 98, "PRESS START", 255);
    }
}

void drawGameplayPanel(pixel_twins::RenderTarget target,
                       const wizward::world::WorldMap& map,
                       const wizward::assets::GameAssets& assets,
                       const Camera& camera,
                       std::uint32_t frame,
                       bool secondPlayer) noexcept {
    map.draw(target, assets.background(), static_cast<std::int32_t>(camera.x), static_cast<std::int32_t>(camera.y));
    const auto animationFrame = frame / 8U;

    if (secondPlayer) {
        drawAsset(target,
                  assets,
                  wizward::assets::SpriteAssetId::GirlMageWalk24x324fSheet,
                  animationFrame,
                  68,
                  52);
        drawAsset(target,
                  assets,
                  wizward::assets::SpriteAssetId::GolemEnemyLinelessWalk24x244fSheet,
                  animationFrame,
                  112,
                  52);
        drawAsset(target,
                  assets,
                  wizward::assets::SpriteAssetId::IceShard128dir12x128fSheet,
                  animationFrame,
                  100,
                  69);
        drawAsset(target,
                  assets,
                  wizward::assets::SpriteAssetId::LevelUpCore24x246fSheet,
                  animationFrame,
                  28,
                  72);
    } else {
        drawAsset(target,
                  assets,
                  wizward::assets::SpriteAssetId::BoyMageWalkStaff28x326fSheet,
                  animationFrame,
                  66,
                  52);
        drawAsset(target,
                  assets,
                  wizward::assets::SpriteAssetId::SlimeEnemyWalk16x164fSmallerSheet,
                  animationFrame,
                  116,
                  68);
        drawAsset(target,
                  assets,
                  wizward::assets::SpriteAssetId::Fireball1616x163fSheet,
                  animationFrame,
                  98,
                  67);
        drawAsset(target,
                  assets,
                  wizward::assets::SpriteAssetId::HealCore16x245fSheet,
                  animationFrame,
                  32,
                  70);
    }

    pixel_twins::drawText(target,
                          wizward::assets::kWizwardFont,
                          4,
                          4,
                          secondPlayer ? "P2  ARROWS" : "P1  WASD",
                          255);
}

void drawResult(pixel_twins::Framebuffer& framebuffer,
                const wizward::assets::GameAssets& assets) noexcept {
    const auto left = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Left);
    const auto right = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Right);
    pixel_twins::fillRectangle(left, 0, 0, 160, 120, 0);
    pixel_twins::fillRectangle(right, 0, 0, 160, 120, 0);
    pixel_twins::drawText(left, wizward::assets::kWizwardFont, 53, 48, "RESULT", 255);
    pixel_twins::drawText(right, wizward::assets::kWizwardFont, 53, 48, "RESULT", 255);
    (void)assets;
}

void drawGameplay(pixel_twins::Framebuffer& framebuffer,
                  const wizward::world::WorldMap& map,
                  const wizward::assets::GameAssets& assets,
                  const Camera& leftCamera,
                  const Camera& rightCamera,
                  std::uint32_t frame) noexcept {
    const auto left = pixel_twins::makeRenderTarget(
        framebuffer.drawBuffer(), pixel_twins::Screen::Left);
    const auto right = pixel_twins::makeRenderTarget(
        framebuffer.drawBuffer(), pixel_twins::Screen::Right);
    drawGameplayPanel(left, map, assets, leftCamera, frame, false);
    drawGameplayPanel(right, map, assets, rightCamera, frame, true);
}

bool hasArgument(int argc, char** argv, std::string_view expected) noexcept {
    for (int index = 1; index < argc; ++index) {
        if (std::string_view(argv[index]) == expected) return true;
    }
    return false;
}

} // namespace

int main(int argc, char** argv) {
    const auto once = hasArgument(argc, argv, "--once");
    Scene scene = hasArgument(argc, argv, "--gameplay") ? Scene::Gameplay : Scene::Title;

    wizward::assets::GameAssets gameAssets;
    wizward::assets::TitleAssets titleAssets;
    wizward::world::TerrainWorkspace terrainWorkspace;
    wizward::world::WorldMap worldMap;
    wizward::world::MapGenerator mapGenerator;
    if (!gameAssets.initialize() || !titleAssets.initialize()
        || !mapGenerator.generate(0x57495aU, gameAssets.background(), terrainWorkspace, worldMap)) {
        std::fputs("Wizwardアセットまたはマップの初期化に失敗しました\n", stderr);
        return 1;
    }

    pixel_twins::Framebuffer framebuffer;
    if (!(scene == Scene::Title ? titleAssets.applyPalette(framebuffer)
                                : gameAssets.applyPalette(framebuffer))) {
        std::fputs("パレットの初期化に失敗しました\n", stderr);
        return 1;
    }

    Camera leftCamera{320.0F, 340.0F};
    Camera rightCamera{
        static_cast<float>(static_cast<std::int32_t>(worldMap.seals[0].x) * 8 - 80),
        static_cast<float>(static_cast<std::int32_t>(worldMap.seals[0].y) * 8 - 60),
    };
    moveCamera(rightCamera, pixel_twins::ControllerState{});

    pixel_twins::sdl::Presenter presenter(4, !once);
    pixel_twins::sdl::ControllerInput controllerInput;
    pixel_twins::AudioSystem audioSystem;
    pixel_twins::sdl::AudioPlayer audioPlayer(audioSystem);
    if (scene == Scene::Gameplay) (void)audioPlayer.playBgm(wizward::audio::kField);
    pixel_twins::Controllers controllers;
    std::uint32_t frame = 0;
    std::uint32_t sceneFrame = 0;
    auto previousTime = std::chrono::steady_clock::now();
    auto accumulatedTime = std::chrono::steady_clock::duration::zero();
    float lastFrameMilliseconds = 0.0F;
    std::uint32_t presentedFrames = 0;
    while (presenter.processEvents(&controllerInput)) {
        const auto frameStart = std::chrono::steady_clock::now();
        const auto currentTime = std::chrono::steady_clock::now();
        const auto maximumFrameTime = std::chrono::duration_cast<
            std::chrono::steady_clock::duration>(kSimulationStep * 4);
        accumulatedTime += std::min(currentTime - previousTime, maximumFrameTime);
        previousTime = currentTime;
        controllerInput.update(controllers);
        if (controllers[0].isPressed(pixel_twins::ControllerButton::start)) {
            scene = scene == Scene::Title ? Scene::Gameplay
                : (scene == Scene::Gameplay ? Scene::Result : Scene::Title);
            sceneFrame = 0;
            const auto paletteApplied = scene == Scene::Title
                ? titleAssets.applyPalette(framebuffer)
                : gameAssets.applyPalette(framebuffer);
            if (!paletteApplied) return 1;
            (void)audioSystem.playSfx(pixel_twins::makeSfxRequest(wizward::audio::kStart));
            if (scene == Scene::Gameplay) (void)audioPlayer.playBgm(wizward::audio::kField);
            else if (scene == Scene::Result) (void)audioPlayer.playBgm(wizward::audio::kVictory);
            else (void)audioPlayer.stopBgm();
        }
        while (accumulatedTime >= kSimulationStep) {
            if (scene == Scene::Title && sceneFrame == kTitleFrames) {
                scene = Scene::Gameplay;
                sceneFrame = 0;
                if (!gameAssets.applyPalette(framebuffer)) return 1;
                (void)audioPlayer.playBgm(wizward::audio::kField);
            }
            if (scene == Scene::Gameplay) {
                moveCamera(leftCamera, controllers[0]);
                moveCamera(rightCamera, controllers[1]);
            }
            ++frame;
            ++sceneFrame;
            accumulatedTime -= kSimulationStep;
        }

        if (scene == Scene::Title) {
            drawTitle(framebuffer, titleAssets, frame);
        } else if (scene == Scene::Gameplay) {
            drawGameplay(framebuffer, worldMap, gameAssets, leftCamera, rightCamera, frame);
        } else {
            drawResult(framebuffer, gameAssets);
        }
        framebuffer.flip();
        presenter.present(framebuffer);
        const auto frameEnd = std::chrono::steady_clock::now();
        lastFrameMilliseconds = std::chrono::duration<float, std::milli>(frameEnd - frameStart).count();
        ++presentedFrames;
        if (presentedFrames % 60U == 0U) {
            std::fprintf(stderr, "frame: %.2f ms, audio: %s\n",
                         static_cast<double>(lastFrameMilliseconds),
                         audioPlayer.healthy() ? "ok" : "error");
        }
        if (once) break;
    }
    return 0;
}
