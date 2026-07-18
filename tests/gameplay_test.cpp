#include "game/gameplay.hpp"

#include "pixel_twins/controller.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {

pixel_twins::Controllers controllersWith(std::int16_t x, std::int16_t y, std::uint16_t buttons = 0) {
    pixel_twins::Controllers controllers;
    std::array<pixel_twins::ControllerSample, pixel_twins::kControllerCount> samples{};
    samples[0] = {x, y, buttons, true, true};
    controllers.update(samples);
    return controllers;
}

std::uint16_t choiceButton(std::size_t packIndex) {
    using pixel_twins::ControllerButton;
    constexpr std::array<ControllerButton, 4> kButtons{{
        ControllerButton::choiceUp, ControllerButton::choiceLeft,
        ControllerButton::choiceRight, ControllerButton::choiceDown,
    }};
    return pixel_twins::buttonMask(kButtons[packIndex]);
}

std::uint8_t perkLevel(const wizward::game::PlayerState& player, wizward::game::Perk perk) {
    using wizward::game::Perk;
    switch (perk) {
    case Perk::Light: return player.lightLevel;
    case Perk::Fire: return player.fireLevel;
    case Perk::Wind: return player.windLevel;
    case Perk::Thunder: return player.thunderLevel;
    case Perk::Ice: return player.iceLevel;
    case Perk::Orb: return player.orbLevel;
    case Perk::Familiar: return player.familiarLevel;
    case Perk::Speed: return player.speedLevel;
    case Perk::MaxHp: return static_cast<std::uint8_t>((player.maxHp - 30) / 5);
    case Perk::Heal:
    case Perk::Bomb: return 0;
    }
    return 0;
}

void grantPerk(wizward::game::GameplayState& gameplay,
               wizward::world::WorldMap& map,
               wizward::game::Perk wanted) {
    for (int attempt = 0; attempt < 24; ++attempt) {
        const auto before = perkLevel(gameplay.player(0), wanted);
        gameplay.grantXp(0, wizward::game::xpNeededForLevel(gameplay.player(0).level));
        const auto& choices = gameplay.player(0).perkChoices;
        std::size_t packIndex = 0;
        for (std::size_t index = 0; index < choices.size(); ++index) {
            if (choices[index] == wanted) {
                packIndex = index;
                break;
            }
        }
        gameplay.tick(controllersWith(0, 0, choiceButton(packIndex)), map);
        if (perkLevel(gameplay.player(0), wanted) > before) return;
    }
    assert(false);
}

std::size_t tileIndex(std::uint16_t x, std::uint16_t y) {
    return static_cast<std::size_t>(y) * wizward::world::kMapColumns + x;
}

} // namespace

int main() {
    wizward::world::WorldMap map;
    wizward::game::GameplayState gameplay;
    gameplay.reset(map);

    const auto initial = gameplay.player(0);
    const auto moveRight = controllersWith(32767, 0);
    gameplay.tick(moveRight, map);
    assert(gameplay.player(0).x > initial.x);
    assert(gameplay.player(0).y == initial.y);
    assert(gameplay.player(0).facing == wizward::game::Facing::East);
    assert(gameplay.player(0).moving);
    assert(std::abs((gameplay.camera(0).x + 80.0F) - gameplay.player(0).x) < 0.01F);
    assert(std::abs((gameplay.camera(0).y + 60.0F) - (gameplay.player(0).y - 16.0F)) < 0.01F);

    const auto stopped = controllersWith(1000, -1000);
    gameplay.tick(stopped, map);
    assert(!gameplay.player(0).moving);
    assert(gameplay.player(0).facing == wizward::game::Facing::East);

    gameplay.reset(map);
    const auto beforeCollision = gameplay.player(0);
    const auto blockedX = static_cast<std::uint16_t>(beforeCollision.x / wizward::game::kWorldTileSize + 1.0F);
    const auto blockedY = static_cast<std::uint16_t>(beforeCollision.y / wizward::game::kWorldTileSize);
    map.tiles[tileIndex(blockedX, blockedY)] = wizward::world::kCollisionBit;
    for (int frame = 0; frame < 20; ++frame) gameplay.tick(moveRight, map);
    assert(gameplay.player(0).x <= static_cast<float>(blockedX * wizward::game::kWorldTileSize)
                                      - wizward::game::kPlayerRadius);

    assert(!wizward::game::playerPositionIsWalkable(map, 2.0F, 2.0F));

    map = {};
    gameplay.reset(map);
    gameplay.tick(controllersWith(0, 0), map);
    assert(gameplay.enemyCount() >= 2);

    gameplay.reset(map);
    const auto combatPlayer = gameplay.player(0);
    assert(gameplay.addEnemy(combatPlayer.x + 100.0F, combatPlayer.y));
    const auto idle = controllersWith(0, 0);
    gameplay.tick(idle, map);
    assert(gameplay.bulletCount() > 0);
    for (int frame = 0; frame < 60; ++frame) gameplay.tick(idle, map);
    assert(gameplay.enemies()[0].hp < 10 || !gameplay.enemies()[0].active);
    assert(gameplay.player(0).xp + gameplay.player(1).xp > 0);

    gameplay.reset(map);
    const auto contactPlayer = gameplay.player(0);
    assert(gameplay.addEnemy(contactPlayer.x, contactPlayer.y));
    gameplay.tick(idle, map);
    assert(gameplay.player(0).hp == 27);
    gameplay.tick(idle, map);
    assert(gameplay.player(0).hp == 27);
    assert(gameplay.player(0).invulnerabilityTicks > 0);

    gameplay.reset(map);
    const auto rangedTarget = gameplay.player(0);
    assert(gameplay.addEnemy(rangedTarget.x + 160.0F, rangedTarget.y,
                             wizward::game::EnemyKind::Mage));
    assert(gameplay.enemies()[0].hp == 20);
    assert(gameplay.enemies()[0].xpValue == 6);
    for (int frame = 0; frame < 120; ++frame) gameplay.tick(idle, map);
    assert(std::any_of(gameplay.enemyBullets().begin(), gameplay.enemyBullets().end(),
        [](const auto& bullet) { return bullet.active && bullet.type == wizward::game::EnemyBulletType::Magic; }));

    assert(wizward::game::xpNeededForLevel(1) == 15);
    assert(wizward::game::xpNeededForLevel(2) == 21);
    assert(wizward::game::xpNeededForLevel(3) == 29);
    assert(wizward::game::xpNeededForLevel(5) == 58);
    assert(wizward::game::directionRow8(1.0F, 0.0F) == 4);
    assert(wizward::game::directionRow8(1.0F, 1.0F) == 1);
    assert(wizward::game::directionRow8(0.0F, 1.0F) == 2);
    assert(wizward::game::directionRow8(-1.0F, 1.0F) == 3);
    assert(wizward::game::directionRow8(-1.0F, 0.0F) == 7);
    assert(wizward::game::directionRow8(-1.0F, -1.0F) == 5);
    assert(wizward::game::directionRow8(0.0F, -1.0F) == 6);
    assert(wizward::game::directionRow8(1.0F, -1.0F) == 0);
    assert(wizward::game::thunderShockwaveRadius(0) == 8);
    assert(wizward::game::thunderShockwaveRadius(20) == 30);

    gameplay.reset(map);
    gameplay.grantXp(0, 15);
    assert(gameplay.player(0).choosingPerk);
    assert(gameplay.player(0).level == 2);
    const auto selectedLeft = gameplay.player(0).perkChoices[1];
    const auto selectedLeftLevel = perkLevel(gameplay.player(0), selectedLeft);
    const auto chooseLeft = controllersWith(0, 0,
        pixel_twins::buttonMask(pixel_twins::ControllerButton::choiceLeft));
    gameplay.tick(chooseLeft, map);
    if (selectedLeft != wizward::game::Perk::Heal) {
        assert(perkLevel(gameplay.player(0), selectedLeft) == selectedLeftLevel + 1);
    }
    assert(!gameplay.player(0).choosingPerk);

    gameplay.grantXp(0, 21);
    const auto selectedUp = gameplay.player(0).perkChoices[0];
    const auto selectedUpLevel = perkLevel(gameplay.player(0), selectedUp);
    const auto chooseUp = controllersWith(0, 0,
        pixel_twins::buttonMask(pixel_twins::ControllerButton::choiceUp));
    gameplay.tick(chooseUp, map);
    if (selectedUp != wizward::game::Perk::Heal) {
        assert(perkLevel(gameplay.player(0), selectedUp) == selectedUpLevel + 1);
    }

    gameplay.reset(map);
    grantPerk(gameplay, map, wizward::game::Perk::Fire);
    grantPerk(gameplay, map, wizward::game::Perk::Wind);
    grantPerk(gameplay, map, wizward::game::Perk::Thunder);
    grantPerk(gameplay, map, wizward::game::Perk::Ice);
    grantPerk(gameplay, map, wizward::game::Perk::Orb);
    const auto attackPlayer = gameplay.player(0);
    assert(gameplay.addEnemy(attackPlayer.x + 22.0F, attackPlayer.y));
    gameplay.tick(idle, map);
    assert(std::any_of(gameplay.bullets().begin(), gameplay.bullets().end(),
        [](const auto& bullet) { return bullet.active && bullet.type == wizward::game::PlayerAttack::Fire; }));
    assert(std::any_of(gameplay.bullets().begin(), gameplay.bullets().end(),
        [](const auto& bullet) { return bullet.active && bullet.type == wizward::game::PlayerAttack::Ice; }));
    assert(std::any_of(gameplay.windSlashes().begin(), gameplay.windSlashes().end(),
        [](const auto& slash) { return slash.active; }));
    assert(std::any_of(gameplay.thunderStrikes().begin(), gameplay.thunderStrikes().end(),
        [](const auto& strike) { return strike.active; }));
    assert(gameplay.player(0).orbCooldownTicks > 0);

    wizward::world::WorldMap blockedMap;
    blockedMap.tiles.fill(wizward::world::kCollisionBit);
    gameplay.reset(blockedMap);
    for (int frame = 0; frame < 300 * 60; ++frame) gameplay.tick(idle, blockedMap);
    assert(gameplay.outcome() == wizward::game::GameplayOutcome::TimeUp);
    return 0;
}
