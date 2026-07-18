#pragma once

#include "assets/game_assets.hpp"
#include "assets/title_assets.hpp"
#include "world/world_map.hpp"

#include "pixel_twins/controller.hpp"
#include "pixel_twins/framebuffer.hpp"
#include "pixel_twins/platform.hpp"

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
    struct Camera {
        float x;
        float y;
    };

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
    Camera leftCamera_{320.0F, 340.0F};
    Camera rightCamera_{};
    Scene scene_ = Scene::Title;
    std::uint32_t frame_ = 0;
    std::uint32_t sceneFrame_ = 0;
};

} // namespace wizward::game
