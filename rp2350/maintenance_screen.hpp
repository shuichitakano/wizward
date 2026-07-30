#pragma once

#include "game/game.hpp"

#include "pixel_twins/controller.hpp"

#include <cstdint>

namespace wizward::rp2350 {

enum class SoundTestCommand : std::uint8_t {
    None,
    Stop,
    StereoLeft,
    StereoRight,
    Field,
    Endless,
    Boss,
    Victory,
    NameEntry,
};

struct MaintenanceAction {
    bool gammaChanged = false;
    bool saveRequested = false;
    SoundTestCommand soundTest = SoundTestCommand::None;
};

class MaintenanceScreen {
public:
    explicit MaintenanceScreen(std::uint16_t gammaTenths) noexcept;

    MaintenanceAction update(
        const pixel_twins::Controllers& controllers,
        game::Game& game) noexcept;
    void render(game::Game& game,
                const pixel_twins::Controllers& controllers) const noexcept;

    [[nodiscard]] std::uint16_t gammaTenths() const noexcept {
        return gammaTenths_;
    }

private:
    enum class Page : std::uint8_t {
        Menu,
        Input,
        SoundTest,
        Statistics,
        ColorBars,
        Gamma,
        ResetRanking,
    };

    Page page_ = Page::Menu;
    std::uint8_t menuIndex_ = 0;
    std::uint16_t gammaTenths_ = 22;
    std::uint8_t soundTestTicks_ = 0;
    std::uint8_t soundTestSelection_ = 0;
    std::int8_t soundTestPlaying_ = -1;
    bool resetComplete_ = false;
};

void applyMaintenancePalette(pixel_twins::Framebuffer& framebuffer) noexcept;

} // namespace wizward::rp2350
