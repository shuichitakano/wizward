#include "game/game.hpp"

#include "assets/wizward_font.hpp"

#include "pixel_twins/font.hpp"
#include "pixel_twins/primitives.hpp"
#include "pixel_twins/render_target.hpp"
#include "pixel_twins/sprite.hpp"

#include <algorithm>
#include <cstdint>

namespace wizward::game {
namespace {

constexpr std::int32_t kMapPixelWidth = world::kMapColumns * 8;
constexpr std::int32_t kMapPixelHeight = world::kMapRows * 8;
constexpr std::uint32_t kTitleFrames = 150;

void moveCamera(Game::Camera& camera, const pixel_twins::ControllerState& controller) noexcept {
    constexpr float speed = 2.0F;
    constexpr float axisScale = speed / 32767.0F;
    camera.x += static_cast<float>(controller.x) * axisScale;
    camera.y += static_cast<float>(controller.y) * axisScale;
    camera.x = std::clamp(camera.x, 0.0F,
        static_cast<float>(kMapPixelWidth - static_cast<std::int32_t>(pixel_twins::kPanelWidth)));
    camera.y = std::clamp(camera.y, 0.0F,
        static_cast<float>(kMapPixelHeight - static_cast<std::int32_t>(pixel_twins::kScreenHeight)));
}

PIXEL_TWINS_SRAM void drawAsset(pixel_twins::RenderTarget target,
               const assets::GameAssets& assets,
               assets::SpriteAssetId id,
               std::uint32_t frame,
               std::int16_t x,
               std::int16_t y) noexcept {
    pixel_twins::Sprite sprite{};
    if (assets.makeLoopingSprite(id, frame, 0, x, y, sprite)) pixel_twins::drawSprite(target, sprite);
}

PIXEL_TWINS_SRAM void drawTitle(pixel_twins::Framebuffer& framebuffer,
               const assets::TitleAssets& title,
               std::uint32_t frame) noexcept {
    auto left = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Left);
    auto right = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Right);
    title.drawScreen(left);
    title.drawScreen(right);
    pixel_twins::Sprite logo{};
    if (title.makeLogo(28, 18, logo)) pixel_twins::drawSprite(right, logo);
    if ((frame / 30U) % 2U == 0U) {
        pixel_twins::drawText(left, assets::kWizwardFont, 40, 98, "PRESS START", 255);
        pixel_twins::drawText(right, assets::kWizwardFont, 40, 98, "PRESS START", 255);
    }
}

PIXEL_TWINS_SRAM void drawGameplayPanel(pixel_twins::RenderTarget target,
                       const world::WorldMap& map,
                       const assets::GameAssets& assets,
                       const Game::Camera& camera,
                       std::uint32_t frame,
                       bool secondPlayer) noexcept {
    map.draw(target, assets.background(), static_cast<std::int32_t>(camera.x),
             static_cast<std::int32_t>(camera.y));
    const auto animationFrame = frame / 8U;
    if (secondPlayer) {
        drawAsset(target, assets, assets::SpriteAssetId::GirlMageWalk24x324fSheet, animationFrame, 68, 52);
        drawAsset(target, assets, assets::SpriteAssetId::GolemEnemyLinelessWalk24x244fSheet, animationFrame, 112, 52);
        drawAsset(target, assets, assets::SpriteAssetId::IceShard128dir12x128fSheet, animationFrame, 100, 69);
        drawAsset(target, assets, assets::SpriteAssetId::LevelUpCore24x246fSheet, animationFrame, 28, 72);
    } else {
        drawAsset(target, assets, assets::SpriteAssetId::BoyMageWalkStaff28x326fSheet, animationFrame, 66, 52);
        drawAsset(target, assets, assets::SpriteAssetId::SlimeEnemyWalk16x164fSmallerSheet, animationFrame, 116, 68);
        drawAsset(target, assets, assets::SpriteAssetId::Fireball1616x163fSheet, animationFrame, 98, 67);
        drawAsset(target, assets, assets::SpriteAssetId::HealCore16x245fSheet, animationFrame, 32, 70);
    }
    pixel_twins::drawText(target, assets::kWizwardFont, 4, 4,
                          secondPlayer ? "P2  ARROWS" : "P1  WASD", 255);
}

} // namespace

bool Game::initialize(Scene initialScene) noexcept {
    world::MapGenerator mapGenerator;
    if (!gameAssets_.initialize() || !titleAssets_.initialize()
        || !mapGenerator.generate(0x57495aU, gameAssets_.background(), terrainWorkspace_, worldMap_)) {
        return false;
    }
    rightCamera_ = {
        static_cast<float>(static_cast<std::int32_t>(worldMap_.seals[0].x) * 8 - 80),
        static_cast<float>(static_cast<std::int32_t>(worldMap_.seals[0].y) * 8 - 60),
    };
    moveCamera(rightCamera_, pixel_twins::ControllerState{});
    scene_ = initialScene;
    return scene_ == Scene::Title ? titleAssets_.applyPalette(framebuffer_)
                                  : gameAssets_.applyPalette(framebuffer_);
}

UpdateResult Game::changeScene(Scene scene, bool playStartSfx) noexcept {
    scene_ = scene;
    sceneFrame_ = 0;
    const bool paletteApplied = scene_ == Scene::Title
        ? titleAssets_.applyPalette(framebuffer_)
        : gameAssets_.applyPalette(framebuffer_);
    AudioEvent audio = AudioEvent::StopBgm;
    if (scene_ == Scene::Gameplay) audio = AudioEvent::PlayField;
    if (scene_ == Scene::Result) audio = AudioEvent::PlayVictory;
    return {audio, playStartSfx, paletteApplied};
}

UpdateResult Game::processInput(const pixel_twins::Controllers& controllers) noexcept {
    if (!controllers[0].isPressed(pixel_twins::ControllerButton::start)) return {};
    const auto next = scene_ == Scene::Title ? Scene::Gameplay
        : (scene_ == Scene::Gameplay ? Scene::Result : Scene::Title);
    return changeScene(next, true);
}

UpdateResult Game::tick(const pixel_twins::Controllers& controllers) noexcept {
    if (scene_ == Scene::Title && sceneFrame_ == kTitleFrames) {
        auto result = changeScene(Scene::Gameplay, false);
        ++frame_;
        ++sceneFrame_;
        return result;
    }
    if (scene_ == Scene::Gameplay) {
        moveCamera(leftCamera_, controllers[0]);
        moveCamera(rightCamera_, controllers[1]);
    }
    ++frame_;
    ++sceneFrame_;
    return {};
}

void Game::render() noexcept {
    if (scene_ == Scene::Title) {
        drawTitle(framebuffer_, titleAssets_, frame_);
    } else if (scene_ == Scene::Gameplay) {
        const auto left = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Left);
        const auto right = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Right);
        drawGameplayPanel(left, worldMap_, gameAssets_, leftCamera_, frame_, false);
        drawGameplayPanel(right, worldMap_, gameAssets_, rightCamera_, frame_, true);
    } else {
        const auto left = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Left);
        const auto right = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Right);
        pixel_twins::fillRectangle(left, 0, 0, 160, 120, 0);
        pixel_twins::fillRectangle(right, 0, 0, 160, 120, 0);
        pixel_twins::drawText(left, assets::kWizwardFont, 53, 48, "RESULT", 255);
        pixel_twins::drawText(right, assets::kWizwardFont, 53, 48, "RESULT", 255);
    }
    framebuffer_.flip();
}

} // namespace wizward::game
