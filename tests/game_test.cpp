#include "game/game.hpp"

#include "pixel_twins/controller.hpp"

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace {

pixel_twins::Controllers pressedController(
    std::size_t player, pixel_twins::ControllerButton button) noexcept {
    pixel_twins::Controllers controllers;
    std::array<pixel_twins::ControllerSample, pixel_twins::kControllerCount> samples{};
    samples[player].connected = true;
    samples[player].gamepad = true;
    samples[player].buttons = pixel_twins::buttonMask(button);
    controllers.update(samples);
    return controllers;
}

pixel_twins::Controllers pressedChord(
    std::size_t player,
    pixel_twins::ControllerButton first,
    pixel_twins::ControllerButton second) noexcept {
    pixel_twins::Controllers controllers;
    std::array<pixel_twins::ControllerSample, pixel_twins::kControllerCount> samples{};
    samples[player].connected = true;
    samples[player].gamepad = true;
    samples[player].buttons = static_cast<std::uint16_t>(
        pixel_twins::buttonMask(first) | pixel_twins::buttonMask(second));
    controllers.update(samples);
    return controllers;
}

pixel_twins::Controllers idleControllers() noexcept {
    pixel_twins::Controllers controllers;
    std::array<pixel_twins::ControllerSample, pixel_twins::kControllerCount> samples{};
    controllers.update(samples);
    return controllers;
}

wizward::game::Game game;

} // namespace

int main() {
    using pixel_twins::ControllerButton;
    using wizward::game::AudioEvent;
    using wizward::game::Scene;

    assert(game.initialize(Scene::Title, 123U, wizward::game::Difficulty::Easy));
    game.render();
    std::array<std::uint8_t, 8U * 32U> normalLabelPixels{};
    for (std::size_t y = 5; y < 13; ++y) {
        const auto row = y * pixel_twins::kScreenWidth;
        for (std::size_t x = 128; x < 160; ++x) {
            normalLabelPixels[(y - 5U) * 32U + x - 128U] =
                game.framebuffer().displayBuffer()[row + x];
        }
    }
    wizward::game::PerformanceOverlay overlay{};
    overlay.enabled = true;
    overlay.fpsTenths = 600;
    overlay.cores[0] = {4167, 4167, 4167, 4166};
    overlay.cores[1] = {8333, 0, 0, 0};
    game.setPerformanceOverlay(overlay);
    game.render();
    const auto& overlayPixels = game.framebuffer().displayBuffer();
    const auto core0Row = std::size_t{1} * pixel_twins::kScreenWidth;
    const auto core1Row = std::size_t{3} * pixel_twins::kScreenWidth;
    assert(overlayPixels[core0Row + 2] != overlayPixels[core0Row + 42]);
    assert(overlayPixels[core0Row + 42] != overlayPixels[core0Row + 81]);
    assert(overlayPixels[core0Row + 81] != overlayPixels[core0Row + 121]);
    assert(overlayPixels[core0Row + 121] == pixel_twins::kWhiteColor);
    assert(overlayPixels[core1Row + 80] != pixel_twins::kDrawableBlackColor);
    assert(overlayPixels[core1Row + 81] == pixel_twins::kDrawableBlackColor);

    assert(game.initialize(Scene::Title, 123U, wizward::game::Difficulty::Hard));
    game.render();
    const auto& hardTitlePixels = game.framebuffer().displayBuffer();
    bool hardLabelChangedLeftPanel = false;
    bool hardLabelChangedRightPanel = false;
    for (std::size_t y = 5; y < 13; ++y) {
        const auto row = y * pixel_twins::kScreenWidth;
        for (std::size_t x = 128; x < 160; ++x) {
            const auto normalPixel = normalLabelPixels[(y - 5U) * 32U + x - 128U];
            hardLabelChangedLeftPanel = hardLabelChangedLeftPanel
                || hardTitlePixels[row + x] != normalPixel;
            hardLabelChangedRightPanel = hardLabelChangedRightPanel
                || hardTitlePixels[row + pixel_twins::kPanelWidth + x] != normalPixel;
        }
    }
    assert(hardLabelChangedLeftPanel);
    assert(hardLabelChangedRightPanel);

    assert(game.initialize());
    assert(game.difficulty() == wizward::game::Difficulty::Easy);
    assert(game.setDifficulty(wizward::game::Difficulty::Hard));
    assert(game.difficulty() == wizward::game::Difficulty::Hard);
    assert(game.setDifficulty(wizward::game::Difficulty::Easy));
    const auto initialMapSeed = game.mapSeed();
    assert(game.scene() == Scene::Title);
    const auto share = pressedController(0, ControllerButton::back);
    auto modeResult = game.processInput(share);
    assert(modeResult.succeeded);
    assert(modeResult.sfxCueCount == 1);
    assert(game.scene() == Scene::Title);
    assert(game.difficulty() == wizward::game::Difficulty::Hard);
    modeResult = game.processInput(share);
    assert(game.scene() == Scene::Title);
    assert(game.difficulty() == wizward::game::Difficulty::Endless);
    modeResult = game.processInput(share);
    assert(game.scene() == Scene::Title);
    assert(game.difficulty() == wizward::game::Difficulty::Easy);
    game.render();
    const auto& titlePixels = game.framebuffer().displayBuffer();
    for (std::size_t y = 0; y < pixel_twins::kScreenHeight; ++y) {
        const auto row = y * pixel_twins::kScreenWidth;
        for (std::size_t x = 0; x < pixel_twins::kPanelWidth; ++x) {
            assert(titlePixels[row + x] == titlePixels[row + pixel_twins::kPanelWidth + x]);
        }
    }
    const auto idle = idleControllers();
    for (std::uint16_t frame = 0; frame < 240; ++frame) {
        const auto result = game.tick(idle);
        assert(result.succeeded);
        assert(game.scene() == Scene::Title);
    }

    wizward::game::Game attractGame;
    assert(attractGame.initialize(Scene::Title, 0x13572468U));
    for (std::uint16_t frame = 0; frame < 479U; ++frame) {
        (void)attractGame.tick(idle);
        assert(attractGame.scene() == Scene::Title);
    }
    auto attractResult = attractGame.tick(idle);
    assert(attractResult.succeeded);
    assert(attractResult.audio == AudioEvent::StopBgm);
    assert(attractGame.scene() == Scene::AttractRanking);
    attractGame.render();
    const auto& rankingPixels = attractGame.framebuffer().displayBuffer();
    bool rankingPanelsDiffer = false;
    for (std::size_t index = 0; index < pixel_twins::kPanelWidth * pixel_twins::kScreenHeight;
         ++index) {
        const auto y = index / pixel_twins::kPanelWidth;
        const auto x = index % pixel_twins::kPanelWidth;
        rankingPanelsDiffer = rankingPanelsDiffer
            || rankingPixels[y * pixel_twins::kScreenWidth + x]
                != rankingPixels[y * pixel_twins::kScreenWidth + pixel_twins::kPanelWidth + x];
    }
    assert(rankingPanelsDiffer);
    for (std::uint16_t frame = 0; frame < 598U; ++frame) {
        (void)attractGame.tick(idle);
        assert(attractGame.scene() == Scene::AttractRanking);
    }
    attractResult = attractGame.tick(idle);
    assert(attractResult.succeeded);
    assert(attractResult.audio == AudioEvent::StopBgm);
    assert(attractResult.playStartSfx);
    assert(attractGame.scene() == Scene::AttractDemo);
    assert(attractGame.gameplay().elapsedTicks() == 105U * 60U);
    assert(!attractGame.gameplay().playerIsManual(0));
    assert(!attractGame.gameplay().playerIsManual(1));
    assert(attractGame.gameplay().player(0).level == 8U);
    assert(attractGame.gameplay().player(1).fireLevel == 2U);
    const auto demoStart = pressedController(1, ControllerButton::choiceRight);
    const auto demoStartResult = attractGame.processInput(demoStart);
    assert(demoStartResult.audio == AudioEvent::StopBgm);
    assert(!demoStartResult.playStartSfx);
    assert(attractGame.scene() == Scene::Title);
    const auto titleStartResult = attractGame.processInput(demoStart);
    assert(titleStartResult.audio == AudioEvent::PlayField);
    assert(titleStartResult.playStartSfx);
    assert(attractGame.scene() == Scene::Gameplay);

    assert(attractGame.initialize(Scene::Title, 0x24681357U));
    for (std::uint16_t frame = 0; frame < 480U + 599U; ++frame) {
        (void)attractGame.tick(idle);
    }
    assert(attractGame.scene() == Scene::AttractDemo);
    for (std::uint16_t frame = 0; frame < 1798U; ++frame) {
        (void)attractGame.tick(idle);
        assert(attractGame.scene() == Scene::AttractDemo);
    }
    attractResult = attractGame.tick(idle);
    assert(attractResult.audio == AudioEvent::StopBgm);
    assert(attractGame.scene() == Scene::Title);
    assert(attractGame.rankingCount() == 0U);

    const auto player2Start = pressedController(1, ControllerButton::choiceRight);
    const auto startResult = game.processInput(player2Start);
    assert(startResult.succeeded);
    assert(startResult.audio == AudioEvent::PlayField);
    assert(startResult.playStartSfx);
    assert(game.scene() == Scene::Gameplay);
    assert(!game.setDifficulty(wizward::game::Difficulty::Hard));
    assert(game.difficulty() == wizward::game::Difficulty::Easy);
    assert(game.gameplay().player(0).maxHp == 40);
    assert(game.mapSeed() != initialMapSeed);
    assert(!game.gameplay().playerIsManual(0));
    assert(game.gameplay().playerIsManual(1));

    const auto player1Join = pressedController(0, ControllerButton::start);
    const auto joinResult = game.processInput(player1Join);
    assert(joinResult.succeeded);
    assert(!game.paused());
    (void)game.tick(player1Join);
    assert(game.gameplay().playerIsManual(0));

    const auto debugFullPower = pressedChord(
        0, ControllerButton::system, ControllerButton::dpadUp);
    (void)game.processInput(debugFullPower);
    assert(game.gameplay().player(0).fireLevel == 0U);
    game.setDebugMode(true);
    const auto fullPowerResult = game.processInput(debugFullPower);
    assert(fullPowerResult.sfxCueCount == 1U);
    assert(game.gameplay().player(0).lightLevel == 5U);
    assert(game.gameplay().player(0).fireLevel == 5U);
    assert(game.gameplay().player(0).familiarLevel == 5U);
    assert(game.gameplay().player(0).speedLevel == 4U);
    assert(game.gameplay().player(0).maxHpLevel == 4U);
    assert(game.gameplay().player(0).hp == game.gameplay().player(0).maxHp);

    const auto debugCharge = pressedChord(
        0, ControllerButton::system, ControllerButton::dpadLeft);
    (void)game.processInput(debugCharge);
    assert(game.gameplay().player(0).choosingPerk);

    const auto scheduleBefore = game.gameplay().elapsedTicks();
    const auto debugAdvance = pressedChord(
        0, ControllerButton::system, ControllerButton::dpadRight);
    (void)game.processInput(debugAdvance);
    assert(game.gameplay().elapsedTicks() == scheduleBefore + 60U * 60U);

    const auto pause = pressedController(1, ControllerButton::start);
    (void)game.processInput(pause);
    assert(game.paused());
    const auto pausedTicks = game.gameplay().elapsedTicks();
    (void)game.tick(idle);
    assert(game.gameplay().elapsedTicks() == pausedTicks);

    (void)game.processInput(pause);
    assert(!game.paused());
    (void)game.tick(idle);
    assert(game.gameplay().elapsedTicks() == pausedTicks + 1U);

    for (std::uint32_t tick = 0; tick < 300U * 60U && game.scene() == Scene::Gameplay; ++tick) {
        (void)game.tick(idle);
    }
    assert(game.scene() == Scene::Result);
    for (std::size_t player = 0; player < pixel_twins::kControllerCount; ++player) {
        assert(game.timeBonus(player) == 0U);
        assert(game.finalScore(player) == game.gameplay().score(player));
    }
    game.render();
    const auto earlyContinue = pressedController(0, ControllerButton::choiceRight);
    (void)game.processInput(earlyContinue);
    assert(game.scene() == Scene::Result);
    bool playedNameEntry = false;
    for (std::uint16_t tick = 0; tick < 160U; ++tick) {
        const auto result = game.tick(idle);
        playedNameEntry = playedNameEntry || result.audio == AudioEvent::PlayNameEntry;
    }
    assert(playedNameEntry == (game.rankingEntry(0).active || game.rankingEntry(1).active));

    std::size_t submitted = 0;
    for (std::size_t player = 0; player < pixel_twins::kControllerCount; ++player) {
        if (!game.rankingEntry(player).active) continue;
        (void)game.processInput(pressedController(player, ControllerButton::dpadRight));
        (void)game.processInput(pressedController(player, ControllerButton::choiceRight));
        (void)game.processInput(pressedController(player, ControllerButton::choiceRight));
        (void)game.processInput(pressedController(player, ControllerButton::choiceRight));
        assert(game.rankingEntry(player).submitted);
        ++submitted;
    }
    assert(game.rankingCount() == submitted);
    for (std::uint16_t tick = 0; tick < 300U && game.scene() == Scene::Result; ++tick) {
        (void)game.tick(idle);
    }
    assert(game.scene() == Scene::Title);

    wizward::game::Game aiResultGame;
    assert(aiResultGame.initialize(Scene::Gameplay, 0x2468ace0U));
    assert(aiResultGame.gameplay().playerIsManual(0));
    assert(!aiResultGame.gameplay().playerIsManual(1));
    for (std::uint32_t tick = 0;
         tick < 300U * 60U && aiResultGame.scene() == Scene::Gameplay; ++tick) {
        (void)aiResultGame.tick(idle);
    }
    assert(aiResultGame.scene() == Scene::Result);
    assert(aiResultGame.finalScore(1) == 0U);
    assert(aiResultGame.timeBonus(1) == 0U);
    assert(!aiResultGame.rankingEntry(1).active);
    return 0;
}
