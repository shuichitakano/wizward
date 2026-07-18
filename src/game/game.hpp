#pragma once

#include "assets/game_assets.hpp"
#include "assets/title_assets.hpp"
#include "game/gameplay.hpp"
#include "world/world_map.hpp"

#include "pixel_twins/controller.hpp"
#include "pixel_twins/framebuffer.hpp"
#include "pixel_twins/platform.hpp"
#include "pixel_twins/sprite.hpp"

#include <cstdint>

namespace wizward::game {

enum class Scene : std::uint8_t {
    Title,
    Gameplay,
    Result,
};

enum class AudioEvent : std::uint8_t {
    None,
    PlayField,
    PlayVictory,
    StopBgm,
};

struct UpdateResult {
    AudioEvent audio = AudioEvent::None;
    bool playStartSfx = false;
    bool succeeded = true;
};

class Game {
public:
    [[nodiscard]] bool initialize(Scene initialScene = Scene::Title) noexcept;
    [[nodiscard]] UpdateResult processInput(const pixel_twins::Controllers& controllers) noexcept;
    [[nodiscard]] UpdateResult tick(const pixel_twins::Controllers& controllers) noexcept;
    void render() noexcept PIXEL_TWINS_SRAM;

    [[nodiscard]] pixel_twins::Framebuffer& framebuffer() noexcept { return framebuffer_; }
    [[nodiscard]] const pixel_twins::Framebuffer& framebuffer() const noexcept { return framebuffer_; }
    [[nodiscard]] Scene scene() const noexcept { return scene_; }

private:
    [[nodiscard]] UpdateResult changeScene(Scene scene, bool playStartSfx) noexcept;

    assets::GameAssets gameAssets_;
    assets::TitleAssets titleAssets_;
    world::TerrainWorkspace terrainWorkspace_;
    world::WorldMap worldMap_;
    pixel_twins::Framebuffer framebuffer_;
    GameplayState gameplay_;
    pixel_twins::SpriteBuckets<
        kMaximumEnemies + kMaximumPlayerBullets + kMaximumXpGems
            + kMaximumEnemyBullets
            + kMaximumWindSlashes * 3U + kMaximumThunderStrikes * 3U
            + pixel_twins::kControllerCount * 4U, kMaximumEnemies> spriteBuckets_;
    Scene scene_ = Scene::Title;
    std::uint32_t frame_ = 0;
    std::uint32_t sceneFrame_ = 0;
};

} // namespace wizward::game
