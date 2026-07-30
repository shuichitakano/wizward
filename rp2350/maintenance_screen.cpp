#include "maintenance_screen.hpp"

#include "assets/wizward_font.hpp"

#include "pixel_twins/font.hpp"
#include "pixel_twins/primitives.hpp"
#include "pixel_twins/render_target.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <utility>

namespace wizward::rp2350 {
namespace {

constexpr std::array<std::string_view, 6> kMenuItems{{
    "INPUT TEST",
    "SOUND TEST",
    "STATISTICS",
    "COLOR BARS",
    "GAMMA",
    "RESET RANKING",
}};

constexpr std::array<std::string_view, 6> kSoundTestItems{{
    "STEREO L/R",
    "FIELD BGM",
    "ENDLESS BGM",
    "BOSS BGM",
    "VICTORY BGM",
    "NAME ENTRY BGM",
}};

constexpr pixel_twins::ColorIndex kRed = 2;
constexpr pixel_twins::ColorIndex kGreen = 3;
constexpr pixel_twins::ColorIndex kBlue = 4;
constexpr pixel_twins::ColorIndex kCyan = 5;
constexpr pixel_twins::ColorIndex kMagenta = 6;
constexpr pixel_twins::ColorIndex kYellow = 7;
constexpr pixel_twins::ColorIndex kGray = 8;
constexpr pixel_twins::ColorIndex kDarkGray = 9;

std::string_view formatUnsigned(std::uint32_t value, char (&buffer)[11]) noexcept {
    auto* end = buffer + sizeof(buffer);
    auto* cursor = end;
    do {
        *--cursor = static_cast<char>('0' + value % 10U);
        value /= 10U;
    } while (value != 0U);
    return {cursor, static_cast<std::size_t>(end - cursor)};
}

void drawText(pixel_twins::RenderTarget target, std::int16_t x, std::int16_t y,
              std::string_view text,
              pixel_twins::ColorIndex color = pixel_twins::kWhiteColor) noexcept {
    pixel_twins::drawText(target, assets::kWizwardFont, x, y, text, color, 6);
}

void drawValue(pixel_twins::RenderTarget target, std::int16_t y,
               std::string_view label, std::uint32_t value) noexcept {
    char buffer[11]{};
    drawText(target, 5, y, label, kCyan);
    drawText(target, 88, y, formatUnsigned(value, buffer));
}

bool pressed(const pixel_twins::Controllers& controllers,
             pixel_twins::ControllerButton button) noexcept {
    for (std::size_t player = 0; player < pixel_twins::kControllerCount; ++player) {
        if (controllers[player].isPressed(button)) return true;
    }
    return false;
}

void drawHeader(pixel_twins::RenderTarget target, std::string_view title) noexcept {
    pixel_twins::fillRectangle(target, 0, 0, 160, 12, kBlue);
    drawText(target, 4, 2, title);
}

void renderMenu(pixel_twins::RenderTarget target, std::uint8_t selection) noexcept {
    drawHeader(target, "MAINTENANCE");
    for (std::size_t index = 0; index < kMenuItems.size(); ++index) {
        const auto y = static_cast<std::int16_t>(18 + index * 15U);
        if (index == selection) {
            pixel_twins::fillRectangle(target, 3, y - 2, 154, 13, kDarkGray);
            drawText(target, 7, y, ">", kYellow);
        }
        drawText(target, 18, y, kMenuItems[index],
                 index == selection ? kYellow : pixel_twins::kWhiteColor);
    }
    drawText(target, 5, 108, "X/START:ENTER", kGray);
}

void renderSoundTest(pixel_twins::RenderTarget target,
                     std::uint8_t selection,
                     std::int8_t playing,
                     std::uint8_t stereoTicks) noexcept {
    drawHeader(target, "SOUND TEST");
    for (std::size_t index = 0; index < kSoundTestItems.size(); ++index) {
        const auto y = static_cast<std::int16_t>(17 + index * 14U);
        if (index == selection) {
            pixel_twins::fillRectangle(target, 3, y - 1, 154, 12, kDarkGray);
            drawText(target, 7, y, ">", kYellow);
        }
        drawText(target, 17, y, kSoundTestItems[index],
                 static_cast<std::int8_t>(index) == playing
                    ? kGreen
                    : index == selection ? kYellow : pixel_twins::kWhiteColor);
    }
    if (selection == 0U) {
        drawText(target, 17, 103,
                 stereoTicks < 60U ? "PLAYING: LEFT" : "PLAYING: RIGHT",
                 kGreen);
    } else {
        drawText(target, 5, 103, "X/START:PLAY", kGray);
    }
}

void renderInput(pixel_twins::RenderTarget target,
                 const pixel_twins::Controllers& controllers,
                 std::size_t player) noexcept {
    char title[] = "INPUT TEST P1";
    title[12] = static_cast<char>('1' + player);
    drawHeader(target, title);
    const auto& controller = controllers[player];
    drawText(target, 5, 18, controller.connected ? "USB CONNECTED" : "USB DISCONNECTED",
             controller.connected ? kGreen : kRed);
    char buffer[11]{};
    drawText(target, 5, 32, "STICK X", kCyan);
    if (controller.x < 0) {
        drawText(target, 82, 32, "-");
        drawText(target, 88, 32, formatUnsigned(
            static_cast<std::uint32_t>(-static_cast<std::int32_t>(controller.x)),
            buffer));
    } else {
        drawText(target, 82, 32, formatUnsigned(
            static_cast<std::uint32_t>(controller.x), buffer));
    }
    drawText(target, 5, 43, "STICK Y", kCyan);
    if (controller.y < 0) {
        drawText(target, 82, 43, "-");
        drawText(target, 88, 43, formatUnsigned(
            static_cast<std::uint32_t>(-static_cast<std::int32_t>(controller.y)),
            buffer));
    } else {
        drawText(target, 82, 43, formatUnsigned(
            static_cast<std::uint32_t>(controller.y), buffer));
    }
    constexpr std::array<std::pair<pixel_twins::ControllerButton,
                                   std::string_view>, 11> buttons{{
        {pixel_twins::ControllerButton::dpadLeft, "LEFT"},
        {pixel_twins::ControllerButton::dpadUp, "UP"},
        {pixel_twins::ControllerButton::dpadRight, "RIGHT"},
        {pixel_twins::ControllerButton::dpadDown, "DOWN"},
        {pixel_twins::ControllerButton::choiceLeft, "SQUARE"},
        {pixel_twins::ControllerButton::choiceUp, "TRIANGLE"},
        {pixel_twins::ControllerButton::choiceRight, "CIRCLE"},
        {pixel_twins::ControllerButton::choiceDown, "CROSS"},
        {pixel_twins::ControllerButton::start, "OPTIONS"},
        {pixel_twins::ControllerButton::back, "SHARE"},
        {pixel_twins::ControllerButton::system, "PS"},
    }};
    std::int16_t x = 5;
    std::int16_t y = 58;
    for (const auto& [button, label] : buttons) {
        const auto color = controller.isHeld(button) ? kYellow : kDarkGray;
        drawText(target, x, y, label, color);
        x = static_cast<std::int16_t>(x + 52);
        if (x > 110) {
            x = 5;
            y = static_cast<std::int16_t>(y + 12);
        }
    }
}

void renderStatistics(pixel_twins::RenderTarget target,
                      const game::Game& game) noexcept {
    drawHeader(target, "STATISTICS");
    const auto& stats = game.statistics();
    drawValue(target, 18, "TOTAL PLAYS", stats.totalPlays);
    drawValue(target, 31, "NORMAL", stats.normalPlays);
    drawValue(target, 44, "HARD", stats.hardPlays);
    drawValue(target, 57, "ENDLESS", stats.endlessPlays);
    drawValue(target, 70, "CLEARS", stats.clears);
    drawValue(target, 83, "PLAY MIN", stats.totalPlayTicks / 3600U);
    drawValue(target, 96, "RANKINGS", static_cast<std::uint32_t>(
        game.rankingCount() + game.endlessRankingCount()));
}

void renderColorBars(pixel_twins::RenderTarget target) noexcept {
    drawHeader(target, "COLOR BARS");
    constexpr std::array<pixel_twins::ColorIndex, 8> colors{{
        pixel_twins::kWhiteColor, kYellow, kCyan, kGreen,
        kMagenta, kRed, kBlue, pixel_twins::kDrawableBlackColor,
    }};
    for (std::size_t index = 0; index < colors.size(); ++index) {
        pixel_twins::fillRectangle(target, static_cast<std::int16_t>(index * 20U),
                                   12, 20, 78, colors[index]);
    }
    constexpr std::array<pixel_twins::ColorIndex, 8> grayColors{{
        pixel_twins::kDrawableBlackColor, 18, 20, 22, 24, 26, 28,
        pixel_twins::kWhiteColor,
    }};
    for (std::size_t index = 0; index < grayColors.size(); ++index) {
        pixel_twins::fillRectangle(target, static_cast<std::int16_t>(index * 20U),
                                   90, 20, 30, grayColors[index]);
    }
}

void renderGamma(pixel_twins::RenderTarget target,
                 std::uint16_t gammaTenths) noexcept {
    drawHeader(target, "GAMMA ADJUST");
    char gammaText[] = "GAMMA 2.2";
    gammaText[6] = static_cast<char>('0' + gammaTenths / 10U);
    gammaText[8] = static_cast<char>('0' + gammaTenths % 10U);
    drawText(target, 47, 20, gammaText, kYellow);
    drawText(target, 23, 34, "LEFT/RIGHT ADJUST", kGray);
    for (std::size_t index = 0; index < 16; ++index) {
        pixel_twins::fillRectangle(target,
            static_cast<std::int16_t>(index * 10U), 52, 10, 50,
            static_cast<std::uint8_t>(index + 16U));
    }
}

void renderReset(pixel_twins::RenderTarget target, bool complete) noexcept {
    drawHeader(target, "RESET RANKING");
    if (complete) {
        drawText(target, 37, 50, "RESET COMPLETE", kGreen);
    } else {
        drawText(target, 18, 40, "DELETE ALL RANKINGS?", kYellow);
        drawText(target, 22, 60, "X/START:CONFIRM", kRed);
    }
    drawText(target, 33, 100, "SHARE:BACK", kGray);
}

} // namespace

MaintenanceScreen::MaintenanceScreen(std::uint16_t gammaTenths) noexcept
    : gammaTenths_(std::clamp<std::uint16_t>(gammaTenths, 10U, 35U)) {}

MaintenanceAction MaintenanceScreen::update(
        const pixel_twins::Controllers& controllers, game::Game& game) noexcept {
    using pixel_twins::ControllerButton;
    MaintenanceAction action{};
    const auto inputExit = page_ == Page::Input
        && pressed(controllers, ControllerButton::back)
        && (controllers[0].isHeld(ControllerButton::system)
            || controllers[1].isHeld(ControllerButton::system));
    if (page_ != Page::Menu
        && ((page_ != Page::Input && pressed(controllers, ControllerButton::back))
            || inputExit)) {
        if (page_ == Page::Gamma) action.saveRequested = true;
        if (page_ == Page::SoundTest) {
            action.soundTest = SoundTestCommand::Stop;
            soundTestPlaying_ = -1;
        }
        page_ = Page::Menu;
        resetComplete_ = false;
        return action;
    }
    const auto confirm = pressed(controllers, ControllerButton::choiceDown)
        || pressed(controllers, ControllerButton::start);
    if (page_ == Page::Menu) {
        if (pressed(controllers, ControllerButton::dpadUp)) {
            menuIndex_ = menuIndex_ == 0
                ? static_cast<std::uint8_t>(kMenuItems.size() - 1U)
                : static_cast<std::uint8_t>(menuIndex_ - 1U);
        }
        if (pressed(controllers, ControllerButton::dpadDown)) {
            menuIndex_ = static_cast<std::uint8_t>(
                (menuIndex_ + 1U) % kMenuItems.size());
        }
        if (confirm) {
            page_ = static_cast<Page>(menuIndex_ + 1U);
            if (page_ == Page::SoundTest) {
                soundTestSelection_ = 0;
                soundTestPlaying_ = 0;
                soundTestTicks_ = 0;
            }
        }
    } else if (page_ == Page::SoundTest) {
        const auto previousSelection = soundTestSelection_;
        if (pressed(controllers, ControllerButton::dpadUp)) {
            soundTestSelection_ = soundTestSelection_ == 0
                ? static_cast<std::uint8_t>(kSoundTestItems.size() - 1U)
                : static_cast<std::uint8_t>(soundTestSelection_ - 1U);
        }
        if (pressed(controllers, ControllerButton::dpadDown)) {
            soundTestSelection_ = static_cast<std::uint8_t>(
                (soundTestSelection_ + 1U) % kSoundTestItems.size());
        }
        if (soundTestSelection_ != previousSelection) {
            action.soundTest = SoundTestCommand::Stop;
            soundTestPlaying_ = soundTestSelection_ == 0U ? 0 : -1;
            soundTestTicks_ = 0;
            return action;
        }
        if (soundTestSelection_ != 0U && confirm) {
            constexpr std::array<SoundTestCommand, 5> commands{{
                SoundTestCommand::Field,
                SoundTestCommand::Endless,
                SoundTestCommand::Boss,
                SoundTestCommand::Victory,
                SoundTestCommand::NameEntry,
            }};
            action.soundTest = commands[soundTestSelection_ - 1U];
            soundTestPlaying_ = static_cast<std::int8_t>(soundTestSelection_);
        }
        if (soundTestSelection_ != 0U) return action;
        if (soundTestTicks_ == 0U || soundTestTicks_ == 60U) {
            action.soundTest = soundTestTicks_ == 0U
                ? SoundTestCommand::StereoLeft
                : SoundTestCommand::StereoRight;
        }
        soundTestTicks_ = static_cast<std::uint8_t>(
            (soundTestTicks_ + 1U) % 120U);
    } else if (page_ == Page::Gamma) {
        if (pressed(controllers, ControllerButton::dpadLeft) && gammaTenths_ > 10U) {
            --gammaTenths_;
            action.gammaChanged = true;
        }
        if (pressed(controllers, ControllerButton::dpadRight) && gammaTenths_ < 35U) {
            ++gammaTenths_;
            action.gammaChanged = true;
        }
    } else if (page_ == Page::ResetRanking && confirm && !resetComplete_) {
        game.resetRankings();
        resetComplete_ = true;
        action.saveRequested = true;
    }
    return action;
}

void MaintenanceScreen::render(
        game::Game& game,
        const pixel_twins::Controllers& controllers) const noexcept {
    auto& buffer = game.framebuffer().drawBuffer();
    const auto full = pixel_twins::makeRenderTarget(buffer, pixel_twins::Screen::Full);
    pixel_twins::clear(full, pixel_twins::kDrawableBlackColor);
    for (std::size_t player = 0; player < pixel_twins::kControllerCount; ++player) {
        const auto target = pixel_twins::makeRenderTarget(
            buffer, player == 0 ? pixel_twins::Screen::Left : pixel_twins::Screen::Right);
        switch (page_) {
        case Page::Menu: renderMenu(target, menuIndex_); break;
        case Page::Input: renderInput(target, controllers, player); break;
        case Page::SoundTest:
            renderSoundTest(
                target, soundTestSelection_, soundTestPlaying_, soundTestTicks_);
            break;
        case Page::Statistics: renderStatistics(target, game); break;
        case Page::ColorBars: renderColorBars(target); break;
        case Page::Gamma: renderGamma(target, gammaTenths_); break;
        case Page::ResetRanking: renderReset(target, resetComplete_); break;
        }
    }
    game.framebuffer().flip();
}

void applyMaintenancePalette(pixel_twins::Framebuffer& framebuffer) noexcept {
    (void)framebuffer.setPaletteColor(pixel_twins::kDrawableBlackColor, {0, 0, 0});
    (void)framebuffer.setPaletteColor(kRed, {255, 0, 0});
    (void)framebuffer.setPaletteColor(kGreen, {0, 255, 0});
    (void)framebuffer.setPaletteColor(kBlue, {0, 0, 255});
    (void)framebuffer.setPaletteColor(kCyan, {0, 255, 255});
    (void)framebuffer.setPaletteColor(kMagenta, {255, 0, 255});
    (void)framebuffer.setPaletteColor(kYellow, {255, 255, 0});
    (void)framebuffer.setPaletteColor(kGray, {128, 128, 128});
    (void)framebuffer.setPaletteColor(kDarkGray, {48, 48, 48});
    for (std::uint16_t index = 0; index < 16U; ++index) {
        const auto value = static_cast<std::uint8_t>(index * 17U);
        (void)framebuffer.setPaletteColor(
            static_cast<std::uint8_t>(index + 16U), {value, value, value});
    }
    (void)framebuffer.setPaletteColor(pixel_twins::kWhiteColor, {255, 255, 255});
}

} // namespace wizward::rp2350
