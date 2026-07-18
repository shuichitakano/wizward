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

    assert(game.initialize());
    assert(game.scene() == Scene::Title);
    const auto idle = idleControllers();
    for (std::uint16_t frame = 0; frame < 240; ++frame) {
        const auto result = game.tick(idle);
        assert(result.succeeded);
        assert(game.scene() == Scene::Title);
    }

    const auto player2Start = pressedController(1, ControllerButton::choiceRight);
    const auto startResult = game.processInput(player2Start);
    assert(startResult.succeeded);
    assert(startResult.audio == AudioEvent::PlayField);
    assert(startResult.playStartSfx);
    assert(game.scene() == Scene::Gameplay);
    assert(!game.gameplay().playerIsManual(0));
    assert(game.gameplay().playerIsManual(1));

    const auto player1Join = pressedController(0, ControllerButton::start);
    const auto joinResult = game.processInput(player1Join);
    assert(joinResult.succeeded);
    assert(!game.paused());
    (void)game.tick(player1Join);
    assert(game.gameplay().playerIsManual(0));

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
    return 0;
}
