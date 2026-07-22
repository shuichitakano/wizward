#include "game/gameplay.hpp"

#include "pixel_twins/framebuffer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace wizward::game {
namespace {

constexpr float kPlayerSpeedPerTick = 1.0F;
constexpr float kSpeedPerLevelPerTick = 10.0F / 60.0F;
constexpr float kLightSpeedPerTick = 145.0F / 60.0F;
constexpr float kFireSpeedPerTick = 205.0F / 60.0F;
constexpr float kLightSeekRange = 170.0F;
constexpr float kLightHomingRange = 95.0F;
constexpr float kXpPullRange = 52.0F;
constexpr float kXpCollectRange = 9.0F;
constexpr float kXpPullSpeedPerTick = 2.0F;
constexpr float kXpRecallRadius = 18.0F;
constexpr float kXpRecallSpeedPerTick = 8.0F;
constexpr float kLinkRange = 140.0F;
constexpr std::int16_t kAxisDeadzone = 4096;
constexpr std::int16_t kLightDamage = 6;
constexpr std::int16_t kContactDamage = 3;
constexpr std::uint16_t kContactInvulnerabilityTicks = 39;
constexpr std::uint16_t kLightLifetimeTicks = 75;

std::int16_t scaledValue(std::int16_t value, std::uint8_t percent) noexcept {
    return static_cast<std::int16_t>(std::max<std::int32_t>(1,
        (static_cast<std::int32_t>(value) * percent + 50) / 100));
}
constexpr float kCameraVerticalOffset = 16.0F;
constexpr float kMapPixelWidth = static_cast<float>(world::kMapColumns * kWorldTileSize);
constexpr float kMapPixelHeight = static_cast<float>(world::kMapRows * kWorldTileSize);
constexpr float kWorldCenter = 50.5F * static_cast<float>(kWorldTileSize);
constexpr float kTau = 6.2831853F;
constexpr float kEnemyRecycleRange = 320.0F;
constexpr float kEnemySpawnMinimumRange = 88.0F;
constexpr float kEnemySpawnMaximumRange = 220.0F;
constexpr float kEnemySwarmMinimumRange = 120.0F;
constexpr float kEnemySwarmMaximumRange = 200.0F;

struct AttackStats {
    std::uint8_t count;
    std::int16_t damage;
    float speed;
};

AttackStats attackStats(std::uint8_t level,
                        std::uint8_t baseCount,
                        std::int16_t baseDamage,
                        float baseSpeed,
                        std::int16_t damageStep,
                        float speedStep) noexcept {
    const auto points = static_cast<unsigned>(level > 0 ? level - 1U : 0U);
    return {
        static_cast<std::uint8_t>(baseCount + (points + 2U) / 3U),
        static_cast<std::int16_t>(baseDamage + static_cast<std::int16_t>((points + 1U) / 3U) * damageStep),
        baseSpeed + static_cast<float>(points / 3U) * speedStep,
    };
}

std::uint16_t cooldownTicks(float baseSeconds, float reductionSeconds,
                            std::uint8_t level, float minimumSeconds) noexcept {
    const auto seconds = std::max(minimumSeconds,
        baseSeconds - static_cast<float>(level) * reductionSeconds);
    return static_cast<std::uint16_t>(std::max(1.0F, std::round(seconds * 60.0F)));
}

float facingAngle(Facing facing) noexcept {
    constexpr std::array<float, 8> kAngles{{
        1.5707963F, 0.7853982F, 0.0F, -0.7853982F,
        -1.5707963F, -2.3561945F, 3.1415927F, 2.3561945F,
    }};
    return kAngles[static_cast<std::size_t>(facing)];
}

float randomUnit(std::uint32_t& state) noexcept {
    state = state * 1664525U + 1013904223U;
    return static_cast<float>(state & 0xffffU) / 65535.0F;
}

Facing facingFor(float x, float y) noexcept {
    constexpr float kDiagonalThreshold = 0.41421356F;
    const auto absoluteX = std::abs(x);
    const auto absoluteY = std::abs(y);
    if (absoluteX < absoluteY * kDiagonalThreshold) return y < 0.0F ? Facing::North : Facing::South;
    if (absoluteY < absoluteX * kDiagonalThreshold) return x < 0.0F ? Facing::West : Facing::East;
    if (y < 0.0F) return x < 0.0F ? Facing::NorthWest : Facing::NorthEast;
    return x < 0.0F ? Facing::SouthWest : Facing::SouthEast;
}

void updateCamera(CameraState& camera, const PlayerState& player) noexcept {
    const auto focusX = player.x;
    const auto focusY = player.y - kCameraVerticalOffset;
    camera.x = std::clamp(focusX - static_cast<float>(pixel_twins::kPanelWidth) * 0.5F,
                          0.0F, kMapPixelWidth - static_cast<float>(pixel_twins::kPanelWidth));
    camera.y = std::clamp(focusY - static_cast<float>(pixel_twins::kScreenHeight) * 0.5F,
                          0.0F, kMapPixelHeight - static_cast<float>(pixel_twins::kScreenHeight));
}

void updateBossIntroCameras(
    std::array<CameraState, pixel_twins::kControllerCount>& cameras,
    const std::array<PlayerState, pixel_twins::kControllerCount>& players,
    const EnemyState& boss,
    std::uint16_t elapsedTicks) noexcept {
    const auto smooth = [](float value) noexcept {
        const auto t = std::clamp(value, 0.0F, 1.0F);
        return t * t * (3.0F - 2.0F * t);
    };
    const auto toBoss = smooth(static_cast<float>(elapsedTicks) / 41.0F);
    const auto backToPlayer = smooth((static_cast<float>(elapsedTicks) - 132.0F) / 51.0F);
    for (std::size_t index = 0; index < cameras.size(); ++index) {
        const auto playerFocusX = players[index].x;
        const auto playerFocusY = players[index].y - kCameraVerticalOffset;
        auto focusX = playerFocusX + (boss.x - playerFocusX) * toBoss;
        auto focusY = playerFocusY + (boss.y - kCameraVerticalOffset - playerFocusY) * toBoss;
        focusX += (playerFocusX - focusX) * backToPlayer;
        focusY += (playerFocusY - focusY) * backToPlayer;
        if (elapsedTicks >= 75U && elapsedTicks < 104U) {
            const auto strength = 1.0F - static_cast<float>(elapsedTicks - 75U) / 29.0F;
            focusX += (elapsedTicks & 1U) != 0U ? -std::ceil(3.0F * strength)
                                                : std::ceil(3.0F * strength);
            focusY += (elapsedTicks & 2U) != 0U ? std::ceil(2.0F * strength)
                                                : -std::ceil(2.0F * strength);
        }
        cameras[index].x = std::clamp(focusX - static_cast<float>(pixel_twins::kPanelWidth) * 0.5F,
                                      0.0F, kMapPixelWidth - static_cast<float>(pixel_twins::kPanelWidth));
        cameras[index].y = std::clamp(focusY - static_cast<float>(pixel_twins::kScreenHeight) * 0.5F,
                                      0.0F, kMapPixelHeight - static_cast<float>(pixel_twins::kScreenHeight));
    }
}

void updateClearCameras(
    std::array<CameraState, pixel_twins::kControllerCount>& cameras,
    const std::array<PlayerState, pixel_twins::kControllerCount>& players,
    float clearX, float clearY, std::uint16_t elapsedTicks) noexcept {
    const auto smooth = [](float value) noexcept {
        const auto t = std::clamp(value, 0.0F, 1.0F);
        return t * t * (3.0F - 2.0F * t);
    };
    const auto toBoss = smooth(static_cast<float>(elapsedTicks) / 11.0F);
    const auto back = smooth((static_cast<float>(elapsedTicks) - 246.0F) / 51.0F);
    for (std::size_t index = 0; index < cameras.size(); ++index) {
        const auto playerX = players[index].x;
        const auto playerY = players[index].y - kCameraVerticalOffset;
        auto focusX = playerX + (clearX - playerX) * toBoss;
        auto focusY = playerY + (clearY - 10.0F - playerY) * toBoss;
        focusX += (playerX - focusX) * back;
        focusY += (playerY - focusY) * back;
        if (elapsedTicks >= 141U && elapsedTicks < 184U) {
            const auto strength = 1.0F - static_cast<float>(elapsedTicks - 141U) / 43.0F;
            focusX += (elapsedTicks & 1U) != 0U ? -std::ceil(5.0F * strength)
                                                : std::ceil(5.0F * strength);
            focusY += (elapsedTicks & 2U) != 0U ? std::ceil(4.0F * strength)
                                                : -std::ceil(4.0F * strength);
        }
        cameras[index].x = std::clamp(focusX - 80.0F, 0.0F, kMapPixelWidth - 160.0F);
        cameras[index].y = std::clamp(focusY - 60.0F, 0.0F, kMapPixelHeight - 120.0F);
    }
}

float playerSpeedPerTick(const PlayerState& player) noexcept {
    const auto speedLevels = static_cast<float>(player.speedLevel)
        + static_cast<float>(player.linkedUpgradeTenths[static_cast<std::size_t>(Perk::Speed)]) * 0.1F;
    return kPlayerSpeedPerTick + speedLevels * kSpeedPerLevelPerTick;
}

void movePlayer(PlayerState& player,
                const pixel_twins::ControllerState& controller,
                const world::WorldMap& map) noexcept {
    auto inputX = std::abs(controller.x) >= kAxisDeadzone ? static_cast<float>(controller.x) : 0.0F;
    auto inputY = std::abs(controller.y) >= kAxisDeadzone ? static_cast<float>(controller.y) : 0.0F;
    const auto length = std::sqrt(inputX * inputX + inputY * inputY);
    player.moving = length > 0.0F;
    if (!player.moving) return;

    inputX /= length;
    inputY /= length;
    player.facing = facingFor(inputX, inputY);
    const auto speed = playerSpeedPerTick(player);
    const auto nextX = player.x + inputX * speed;
    if (playerPositionIsWalkable(map, nextX, player.y)) player.x = nextX;
    const auto nextY = player.y + inputY * speed;
    if (playerPositionIsWalkable(map, player.x, nextY)) player.y = nextY;
}

float squaredDistance(float x1, float y1, float x2, float y2) noexcept {
    const auto dx = x2 - x1;
    const auto dy = y2 - y1;
    return dx * dx + dy * dy;
}

void normalize(float& x, float& y) noexcept {
    const auto length = std::sqrt(x * x + y * y);
    if (length <= 0.0F) return;
    x /= length;
    y /= length;
}

std::uint16_t impactLifetimeTicks(ImpactEffectType type) noexcept {
    switch (type) {
    case ImpactEffectType::Light:
    case ImpactEffectType::Wind: return 11;
    case ImpactEffectType::Orb:
    case ImpactEffectType::Familiar: return 12;
    case ImpactEffectType::Ice: return 21;
    case ImpactEffectType::CastSpark:
    case ImpactEffectType::Fire:
    case ImpactEffectType::Generic: return 13;
    }
    return 13;
}

void spawnImpact(std::array<ImpactEffectState, kMaximumImpactEffects>& effects,
                 ImpactEffectType type, float x, float y) noexcept {
    auto slot = std::find_if(effects.begin(), effects.end(),
        [](const ImpactEffectState& effect) { return !effect.active; });
    if (slot == effects.end()) {
        slot = std::max_element(effects.begin(), effects.end(),
            [](const ImpactEffectState& lhs, const ImpactEffectState& rhs) {
                return lhs.ageTicks < rhs.ageTicks;
            });
    }
    *slot = {x, y, 0, impactLifetimeTicks(type), type, true};
}

void updateImpacts(std::array<ImpactEffectState, kMaximumImpactEffects>& effects) noexcept {
    for (auto& effect : effects) {
        if (!effect.active) continue;
        ++effect.ageTicks;
        if (effect.ageTicks >= effect.lifetimeTicks) effect.active = false;
    }
}

std::uint16_t perkEffectLifetimeTicks(PerkEffectType type) noexcept {
    switch (type) {
    case PerkEffectType::Heal: return 44;
    case PerkEffectType::HpUp: return 46;
    case PerkEffectType::LevelUp: return 30;
    case PerkEffectType::Upgrade: return 34;
    }
    return 34;
}

void spawnPerkEffect(std::array<PerkEffectState, kMaximumPerkEffects>& effects,
                     PerkEffectType type, std::uint8_t owner,
                     std::uint32_t randomState) noexcept {
    auto slot = std::find_if(effects.begin(), effects.end(),
        [](const PerkEffectState& effect) { return !effect.active; });
    if (slot == effects.end()) {
        slot = std::max_element(effects.begin(), effects.end(),
            [](const PerkEffectState& lhs, const PerkEffectState& rhs) {
                return lhs.ageTicks < rhs.ageTicks;
            });
    }
    auto effectRandom = randomState
        ^ (static_cast<std::uint32_t>(owner) + 1U) * 2246822519U
        ^ (static_cast<std::uint32_t>(type) + 1U) * 3266489917U;
    effectRandom ^= effectRandom >> 15U;
    effectRandom *= 2246822519U;
    effectRandom ^= effectRandom >> 13U;
    constexpr float kTau = 6.2831853F;
    const auto seed = static_cast<float>(effectRandom >> 8U)
        / static_cast<float>(0x00ffffffU) * kTau;
    *slot = {0, perkEffectLifetimeTicks(type), seed, type, owner, true};
}

void updatePerkEffects(std::array<PerkEffectState, kMaximumPerkEffects>& effects) noexcept {
    for (auto& effect : effects) {
        if (!effect.active) continue;
        ++effect.ageTicks;
        if (effect.ageTicks >= effect.lifetimeTicks) effect.active = false;
    }
}

float aiDangerAt(
    float x, float y,
    const std::array<EnemyState, kMaximumEnemies>& enemies,
    const std::array<EnemyBulletState, kMaximumEnemyBullets>& enemyBullets) noexcept {
    auto danger = 0.0F;
    for (const auto& enemy : enemies) {
        if (!enemy.active || enemy.hp <= 0 || enemy.bornTicks > 0
            || enemy.spawnDelayTicks > 0) continue;
        const auto range = 100.0F + enemy.radius;
        const auto distanceSquared = squaredDistance(x, y, enemy.x, enemy.y);
        if (distanceSquared >= range * range) continue;
        const auto gap = range - std::sqrt(distanceSquared);
        danger += gap * gap * 0.08F;
    }
    for (const auto& bullet : enemyBullets) {
        if (!bullet.active || bullet.launchDelayTicks > 0) continue;
        const auto speedSquared = bullet.velocityX * bullet.velocityX
            + bullet.velocityY * bullet.velocityY;
        const auto towardX = x - bullet.x;
        const auto towardY = y - bullet.y;
        const auto closestTicks = speedSquared > 0.0F
            ? std::clamp((towardX * bullet.velocityX + towardY * bullet.velocityY)
                         / speedSquared, 0.0F, 42.0F)
            : 0.0F;
        const auto closestX = bullet.x + bullet.velocityX * closestTicks;
        const auto closestY = bullet.y + bullet.velocityY * closestTicks;
        const auto range = kPlayerRadius + bullet.radius + 22.0F;
        const auto distanceSquared = squaredDistance(x, y, closestX, closestY);
        if (distanceSquared >= range * range) continue;
        const auto gap = range - std::sqrt(distanceSquared);
        danger += gap * gap * 1.8F;
    }
    return danger;
}

float aiViewPenalty(float x, float y, const PlayerState& leader) noexcept {
    const auto dx = x - leader.x;
    const auto dy = y - leader.y;
    const auto softOverflow = std::max(0.0F, std::abs(dx) - 58.0F)
        + std::max(0.0F, -42.0F - dy)
        + std::max(0.0F, dy - 28.0F);
    const auto screenOverflow = std::max(0.0F, std::abs(dx) - 68.0F)
        + std::max(0.0F, -50.0F - dy)
        + std::max(0.0F, dy - 34.0F);
    return softOverflow * 4.0F
        + (screenOverflow > 0.0F ? 600.0F + screenOverflow * 30.0F : 0.0F);
}

float aiMoveScore(
    const PlayerState& player, float x, float y,
    float targetX, float targetY, const PlayerState& leader,
    const std::array<EnemyState, kMaximumEnemies>& enemies,
    const std::array<EnemyBulletState, kMaximumEnemyBullets>& enemyBullets,
    bool rescuing, float moved, float lookahead) noexcept {
    auto score = aiDangerAt(x, y, enemies, enemyBullets) * (rescuing ? 0.24F : 1.0F);
    score += std::sqrt(squaredDistance(x, y, targetX, targetY))
        * (rescuing ? 1.8F : 0.45F);
    const auto linkDistance = std::sqrt(squaredDistance(x, y, leader.x, leader.y));
    if (linkDistance > 100.0F) score += (linkDistance - 100.0F) * 0.8F;
    if (linkDistance > 125.0F) score += (linkDistance - 125.0F) * 6.0F;
    if (linkDistance > kLinkRange) score += 1000.0F + (linkDistance - kLinkRange) * 20.0F;
    score += aiViewPenalty(x, y, leader);
    score += std::max(0.0F, lookahead - moved) * 8.0F;
    if (moved > 0.05F) {
        auto directionX = x - player.x;
        auto directionY = y - player.y;
        normalize(directionX, directionY);
        const auto directionDot = directionX * player.aiDirectionX
            + directionY * player.aiDirectionY;
        score += (1.0F - directionDot) * 1.6F;
        if (player.aiStuckTicks > 5U) {
            auto towardX = targetX - player.x;
            auto towardY = targetY - player.y;
            normalize(towardX, towardY);
            const auto side = towardX * directionY - towardY * directionX;
            if (side * static_cast<float>(player.aiTurnSign) < 0.0F) score += 5.0F;
        }
    }
    return score;
}

void probeAiMovement(const world::WorldMap& map, float startX, float startY,
                     float dx, float dy, float& resultX, float& resultY) noexcept {
    constexpr float kProbeStep = 4.0F;
    const auto steps = std::max(1, static_cast<int>(std::ceil(
        std::max(std::abs(dx), std::abs(dy)) / kProbeStep)));
    const auto stepX = dx / static_cast<float>(steps);
    const auto stepY = dy / static_cast<float>(steps);
    resultX = startX;
    resultY = startY;
    for (auto index = 0; index < steps; ++index) {
        const auto x = resultX + stepX;
        if (playerPositionIsWalkable(map, x, resultY)) resultX = x;
        const auto y = resultY + stepY;
        if (playerPositionIsWalkable(map, resultX, y)) resultY = y;
    }
}

float moveAiPlayerSmart(
    PlayerState& player, float targetX, float targetY,
    const PlayerState& leader,
    const std::array<EnemyState, kMaximumEnemies>& enemies,
    const std::array<EnemyBulletState, kMaximumEnemyBullets>& enemyBullets,
    const world::WorldMap& map, bool rescuing) noexcept {
    constexpr float kLookahead = 22.0F;
    auto towardX = targetX - player.x;
    auto towardY = targetY - player.y;
    normalize(towardX, towardY);
    const auto currentDanger = aiDangerAt(player.x, player.y, enemies, enemyBullets);
    const auto urgent = currentDanger > 350.0F;
    const auto evading = currentDanger >= 80.0F;
    if (player.aiDecisionTicks > 0) --player.aiDecisionTicks;
    if (player.aiDecisionTicks == 0 || urgent) {
        std::array<std::array<float, 2>, 18> directions{};
        directions[0] = {{player.aiDesiredDirectionX, player.aiDesiredDirectionY}};
        directions[1] = {{towardX, towardY}};
        for (std::size_t index = 0; index < 16U; ++index) {
            const auto angle = static_cast<float>(index) * kTau / 16.0F;
            directions[index + 2U] = {{std::cos(angle), std::sin(angle)}};
        }
        auto bestScore = std::numeric_limits<float>::max();
        auto retainedScore = std::numeric_limits<float>::max();
        auto bestDirectionX = towardX;
        auto bestDirectionY = towardY;
        auto retainedDirectionX = player.aiDesiredDirectionX;
        auto retainedDirectionY = player.aiDesiredDirectionY;
        auto retained = false;
        for (std::size_t index = 0; index < directions.size(); ++index) {
            auto directionX = directions[index][0];
            auto directionY = directions[index][1];
            normalize(directionX, directionY);
            float probeX = player.x;
            float probeY = player.y;
            probeAiMovement(map, player.x, player.y,
                            directionX * kLookahead, directionY * kLookahead,
                            probeX, probeY);
            const auto moved = std::sqrt(squaredDistance(player.x, player.y, probeX, probeY));
            if (moved < 0.05F) continue;
            directionX = probeX - player.x;
            directionY = probeY - player.y;
            normalize(directionX, directionY);
            const auto score = aiMoveScore(player, probeX, probeY, targetX, targetY,
                                           leader, enemies, enemyBullets,
                                           rescuing, moved, kLookahead);
            if (index == 0U) {
                retained = true;
                retainedScore = score;
                retainedDirectionX = directionX;
                retainedDirectionY = directionY;
            }
            if (score >= bestScore) continue;
            bestScore = score;
            bestDirectionX = directionX;
            bestDirectionY = directionY;
        }
        const auto switchMargin = urgent ? 3.0F : (evading ? 10.0F : 18.0F);
        if (retained && retainedScore <= bestScore + switchMargin) {
            player.aiDesiredDirectionX = retainedDirectionX;
            player.aiDesiredDirectionY = retainedDirectionY;
        } else {
            player.aiDesiredDirectionX = bestDirectionX;
            player.aiDesiredDirectionY = bestDirectionY;
        }
        player.aiDecisionTicks = urgent ? 4U : (evading ? 11U : 21U);
    }
    const auto maximumTurn = urgent ? 14.0F / 60.0F
        : (evading ? 8.0F / 60.0F : 5.0F / 60.0F);
    const auto currentAngle = std::atan2(player.aiDirectionY, player.aiDirectionX);
    const auto desiredAngle = std::atan2(
        player.aiDesiredDirectionY, player.aiDesiredDirectionX);
    auto angleDifference = std::remainder(desiredAngle - currentAngle, kTau);
    if (std::abs(std::abs(angleDifference) - kTau * 0.5F) < 0.001F) {
        angleDifference = static_cast<float>(player.aiTurnSign) * kTau * 0.5F;
    }
    const auto movementAngle = currentAngle
        + std::clamp(angleDifference, -maximumTurn, maximumTurn);
    const auto movementX = std::cos(movementAngle);
    const auto movementY = std::sin(movementAngle);
    const auto beforeX = player.x;
    const auto beforeY = player.y;
    const auto step = playerSpeedPerTick(player);
    const auto x = player.x + movementX * step;
    if (playerPositionIsWalkable(map, x, player.y)) player.x = x;
    const auto y = player.y + movementY * step;
    if (playerPositionIsWalkable(map, player.x, y)) player.y = y;
    const auto moved = std::sqrt(squaredDistance(beforeX, beforeY, player.x, player.y));
    player.moving = moved > 0.05F;
    if (player.moving) {
        player.aiDirectionX = player.x - beforeX;
        player.aiDirectionY = player.y - beforeY;
        normalize(player.aiDirectionX, player.aiDirectionY);
        player.facing = facingFor(player.aiDirectionX, player.aiDirectionY);
    } else {
        player.aiDecisionTicks = 0;
    }
    return moved;
}

void followAiPartner(PlayerState& player, const PlayerState& leader,
                     const std::array<EnemyState, kMaximumEnemies>& enemies,
                     const std::array<EnemyBulletState, kMaximumEnemyBullets>& enemyBullets,
                     const world::WorldMap& map) noexcept {
    const auto rescuing = leader.hp <= 0;
    const auto formationX = rescuing ? leader.x : leader.x - 38.0F;
    const auto formationY = rescuing ? leader.y : leader.y + 24.0F;
    const auto distance = std::sqrt(squaredDistance(player.x, player.y, formationX, formationY));
    const auto stopDistance = rescuing ? kPlayerRadius * 2.0F + 6.0F : 8.0F;
    const auto currentDanger = aiDangerAt(player.x, player.y, enemies, enemyBullets);
    const auto shouldEvade = currentDanger >= 80.0F;
    if (rescuing && distance <= stopDistance) {
        player.moving = false;
        return;
    }
    if (!rescuing && !shouldEvade) {
        if (player.aiHoldingFormation && distance <= 24.0F) {
            player.moving = false;
            player.aiDecisionTicks = 0;
            return;
        }
        if (distance <= 12.0F) {
            player.aiHoldingFormation = true;
            player.moving = false;
            player.aiDecisionTicks = 0;
            return;
        }
        player.aiHoldingFormation = false;
    } else {
        player.aiHoldingFormation = false;
    }
    const auto moved = moveAiPlayerSmart(player, formationX, formationY, leader,
                                          enemies, enemyBullets, map, rescuing);
    if (moved < 0.2F && distance > stopDistance) {
        if (player.aiStuckTicks < 255U) ++player.aiStuckTicks;
        if (player.aiStuckTicks > 39U) {
            player.aiTurnSign = static_cast<std::int8_t>(-player.aiTurnSign);
            player.aiStuckTicks = 12U;
        }
    } else {
        player.aiStuckTicks = player.aiStuckTicks > 2U
            ? static_cast<std::uint8_t>(player.aiStuckTicks - 2U) : 0U;
    }
}

void moveAttractPlayer(
    PlayerState& player, std::size_t playerIndex, const PlayerState& partner,
    const std::array<EnemyState, kMaximumEnemies>& enemies,
    const std::array<EnemyBulletState, kMaximumEnemyBullets>& enemyBullets,
    const std::array<XpGemState, kMaximumXpGems>& xpGems,
    const world::WorldMap& map, std::uint32_t elapsedTicks) noexcept {
    if (partner.hp <= 0) {
        (void)moveAiPlayerSmart(player, partner.x, partner.y, partner,
                                enemies, enemyBullets, map, true);
        return;
    }
    const EnemyState* nearest = nullptr;
    auto nearestSquared = 190.0F * 190.0F;
    for (const auto& enemy : enemies) {
        if (!enemy.active || enemy.hp <= 0 || enemy.bornTicks > 0
            || enemy.spawnDelayTicks > 0) continue;
        const auto candidate = squaredDistance(player.x, player.y, enemy.x, enemy.y);
        if (candidate > nearestSquared) continue;
        nearestSquared = candidate;
        nearest = &enemy;
    }
    auto targetX = player.x;
    auto targetY = player.y;
    if (nearest != nullptr) {
        const auto gap = std::sqrt(nearestSquared);
        auto awayX = player.x - nearest->x;
        auto awayY = player.y - nearest->y;
        normalize(awayX, awayY);
        const auto orbitSign = playerIndex == 0 ? 1.0F : -1.0F;
        const auto tangentX = -awayY * orbitSign;
        const auto tangentY = awayX * orbitSign;
        const auto retreat = gap < 48.0F ? 1.25F : gap > 105.0F ? -0.35F : 0.12F;
        auto directionX = tangentX + awayX * retreat;
        auto directionY = tangentY + awayY * retreat;
        normalize(directionX, directionY);
        targetX += directionX * 72.0F;
        targetY += directionY * 72.0F;
    } else {
        const XpGemState* ownedXp = nullptr;
        auto xpDistance = 180.0F * 180.0F;
        for (const auto& gem : xpGems) {
            if (!gem.active || gem.owner != playerIndex) continue;
            const auto candidate = squaredDistance(player.x, player.y, gem.x, gem.y);
            if (candidate > xpDistance) continue;
            xpDistance = candidate;
            ownedXp = &gem;
        }
        if (ownedXp != nullptr) {
            targetX = ownedXp->x;
            targetY = ownedXp->y;
        } else {
            const auto sealIndex = static_cast<std::size_t>(
                (elapsedTicks / (7U * 60U) + playerIndex) % map.seals.size());
            targetX = (static_cast<float>(map.seals[sealIndex].x) + 0.5F) * kWorldTileSize;
            targetY = (static_cast<float>(map.seals[sealIndex].y) + 0.5F) * kWorldTileSize;
        }
    }
    (void)moveAiPlayerSmart(player, targetX, targetY, partner,
                            enemies, enemyBullets, map, false);
}

EnemyState* nearestEnemy(std::array<EnemyState, kMaximumEnemies>& enemies,
                         float x, float y, float range) noexcept {
    EnemyState* nearest = nullptr;
    auto nearestSquared = range * range;
    for (auto& enemy : enemies) {
        if (!enemy.active || enemy.bornTicks > 0 || enemy.spawnDelayTicks > 0) continue;
        const auto candidate = squaredDistance(x, y, enemy.x, enemy.y);
        if (candidate <= nearestSquared) {
            nearestSquared = candidate;
            nearest = &enemy;
        }
    }
    return nearest;
}

bool circlePositionIsWalkable(const world::WorldMap& map, float x, float y, float radius) noexcept {
    if (x < radius || y < radius || x >= kMapPixelWidth - radius || y >= kMapPixelHeight - radius) {
        return false;
    }
    return map.circleIsWalkable(x, y, radius);
}

void moveActor(EnemyState& enemy, float dx, float dy, const world::WorldMap& map) noexcept {
    const auto nextX = enemy.x + dx;
    if (circlePositionIsWalkable(map, nextX, enemy.y, enemy.radius)) enemy.x = nextX;
    const auto nextY = enemy.y + dy;
    if (circlePositionIsWalkable(map, enemy.x, nextY, enemy.radius)) enemy.y = nextY;
}

PlayerState* nearestLivingPlayer(
    std::array<PlayerState, pixel_twins::kControllerCount>& players, float x, float y) noexcept {
    PlayerState* result = nullptr;
    auto best = kMapPixelWidth * kMapPixelWidth + kMapPixelHeight * kMapPixelHeight;
    for (auto& player : players) {
        if (player.hp <= 0) continue;
        const auto candidate = squaredDistance(x, y, player.x, player.y);
        if (candidate < best) {
            best = candidate;
            result = &player;
        }
    }
    return result != nullptr ? result : &players[0];
}

bool spawnEnemyBullet(std::array<EnemyBulletState, kMaximumEnemyBullets>& bullets,
                      const EnemyState& enemy, EnemyBulletType type,
                      float directionX, float directionY) noexcept {
    const auto slot = std::find_if(bullets.begin(), bullets.end(),
        [](const EnemyBulletState& bullet) { return !bullet.active; });
    if (slot == bullets.end()) return false;
    const auto speed = type == EnemyBulletType::Arrow ? 82.0F / 60.0F
        : type == EnemyBulletType::BossFire ? 124.0F / 60.0F : 48.0F / 60.0F;
    slot->x = enemy.x;
    slot->y = enemy.y;
    slot->velocityX = directionX * speed;
    slot->velocityY = directionY * speed;
    slot->radius = type == EnemyBulletType::Arrow ? 2.0F
        : type == EnemyBulletType::BossFire ? 6.0F : 4.0F;
    slot->remainingTicks = type == EnemyBulletType::Arrow ? 132
        : type == EnemyBulletType::BossFire ? 210 : 192;
    slot->ageTicks = 0;
    slot->launchDelayTicks = 0;
    slot->damage = type == EnemyBulletType::Arrow ? 4
        : type == EnemyBulletType::BossFire ? 8 : 5;
    slot->type = type;
    slot->active = true;
    return true;
}

bool spawnBullet(std::array<PlayerBulletState, kMaximumPlayerBullets>& bullets,
                 const PlayerState& player,
                 std::uint8_t owner,
                 PlayerAttack type,
                 float angle,
                 std::int16_t damage,
                 float speed,
                 std::uint16_t lifetimeTicks,
                 float radius) noexcept {
    const auto slot = std::find_if(bullets.begin(), bullets.end(),
        [](const PlayerBulletState& bullet) { return !bullet.active; });
    if (slot == bullets.end()) return false;
    const auto directionX = std::cos(angle);
    const auto directionY = std::sin(angle);
    slot->x = player.x + directionX * 10.0F;
    slot->y = player.y + directionY * 10.0F;
    slot->velocityX = directionX * speed;
    slot->velocityY = directionY * speed;
    slot->speed = speed;
    slot->remainingTicks = lifetimeTicks;
    slot->ageTicks = 0;
    slot->damage = damage;
    slot->owner = owner;
    slot->type = type;
    slot->radius = radius;
    slot->active = true;
    return true;
}

bool spawnBulletAt(std::array<PlayerBulletState, kMaximumPlayerBullets>& bullets,
                   float x, float y, std::uint8_t owner, PlayerAttack type,
                   float directionX, float directionY, std::int16_t damage,
                   float speed, std::uint16_t lifetimeTicks, float radius) noexcept {
    const auto slot = std::find_if(bullets.begin(), bullets.end(),
        [](const PlayerBulletState& bullet) { return !bullet.active; });
    if (slot == bullets.end()) return false;
    slot->x = x;
    slot->y = y;
    slot->velocityX = directionX * speed;
    slot->velocityY = directionY * speed;
    slot->speed = speed;
    slot->remainingTicks = lifetimeTicks;
    slot->ageTicks = 0;
    slot->damage = damage;
    slot->owner = owner;
    slot->type = type;
    slot->radius = radius;
    slot->active = true;
    return true;
}

void killEnemy(EnemyState& enemy,
               std::uint8_t owner,
               std::array<XpGemState, kMaximumXpGems>& xpGems,
               std::array<std::uint32_t, pixel_twins::kControllerCount>& scores,
               std::array<std::uint32_t, pixel_twins::kControllerCount>& killCounts) noexcept {
    if (owner < killCounts.size()) ++killCounts[owner];
    if (enemy.kind == EnemyKind::Boss) {
        for (auto& score : scores) score += 5000;
    } else {
        auto gem = std::find_if(xpGems.begin(), xpGems.end(),
            [](const XpGemState& candidate) { return !candidate.active; });
        if (gem == xpGems.end()) {
            gem = std::max_element(xpGems.begin(), xpGems.end(),
                [](const XpGemState& lhs, const XpGemState& rhs) {
                    return lhs.ageTicks < rhs.ageTicks;
                });
        }
        *gem = {enemy.x, enemy.y, 0, enemy.xpValue, owner, 0xff, true};
        constexpr std::array<std::uint16_t, 7> kScores{{10, 15, 45, 100, 30, 25, 60}};
        if (owner < scores.size()) scores[owner] += kScores[static_cast<std::size_t>(enemy.kind)];
    }
    enemy.active = false;
    enemy.deathTicks = 16;
}

EnemyState makeEnemyState(EnemyKind kind, float x, float y,
                          std::uint32_t elapsedTicks, std::uint32_t& randomState,
                          bool spawning, const BalanceProfile& balance) noexcept {
    EnemyState enemy{};
    enemy.x = x;
    enemy.y = y;
    enemy.kind = kind;
    enemy.phase = randomUnit(randomState) * kTau;
    enemy.bornTicks = spawning ? 23 : 0;
    enemy.facing = Facing::South;
    enemy.active = true;
    const auto elapsedSeconds = static_cast<float>(elapsedTicks) / 60.0F;
    switch (kind) {
    case EnemyKind::Imp:
        enemy.radius = 5.0F; enemy.hp = 10; enemy.xpValue = 2;
        enemy.speedPerTick = (22.0F + randomUnit(randomState) * 16.0F + elapsedSeconds / 28.0F) / 60.0F;
        break;
    case EnemyKind::Bat:
        enemy.radius = 4.0F; enemy.hp = 8; enemy.xpValue = 3;
        enemy.speedPerTick = (38.0F + elapsedSeconds / 35.0F) / 60.0F;
        enemy.attackCooldownTicks = static_cast<std::uint16_t>(
            std::round((0.6F + randomUnit(randomState) * 0.8F) * 60.0F));
        break;
    case EnemyKind::Skeleton:
        enemy.radius = 8.0F; enemy.hp = 34; enemy.xpValue = 7; enemy.speedPerTick = 12.0F / 60.0F;
        break;
    case EnemyKind::Golem:
        enemy.radius = 10.0F; enemy.hp = 86; enemy.xpValue = 14; enemy.speedPerTick = 8.0F / 60.0F;
        break;
    case EnemyKind::Archer:
        enemy.radius = 5.0F; enemy.hp = 14; enemy.xpValue = 4; enemy.speedPerTick = 18.0F / 60.0F;
        enemy.attackCooldownTicks = static_cast<std::uint16_t>(
            std::round((1.4F + randomUnit(randomState) * 0.8F) * 60.0F));
        break;
    case EnemyKind::Wisp:
        enemy.radius = 4.0F; enemy.hp = 9; enemy.xpValue = 3;
        enemy.speedPerTick = (34.0F + elapsedSeconds / 45.0F) / 60.0F;
        break;
    case EnemyKind::Mage:
        enemy.radius = 6.0F; enemy.hp = 20; enemy.xpValue = 6; enemy.speedPerTick = 13.0F / 60.0F;
        enemy.attackCooldownTicks = static_cast<std::uint16_t>(
            std::round((0.6F + randomUnit(randomState) * 0.8F) * 60.0F));
        break;
    case EnemyKind::Boss:
        enemy.radius = 20.0F; enemy.hp = 900; enemy.xpValue = 0; enemy.speedPerTick = 24.0F / 60.0F;
        enemy.attackCooldownTicks = 72;
        break;
    }
    enemy.hp = scaledValue(enemy.hp, kind == EnemyKind::Boss
        ? balance.bossHpPercent : balance.enemyHpPercent);
    enemy.maxHp = enemy.hp;
    return enemy;
}

void fireBossRadial(EnemyState& enemy,
                    std::array<EnemyBulletState, kMaximumEnemyBullets>& bullets) noexcept {
    const auto count = static_cast<std::uint8_t>(enemy.hp < enemy.maxHp / 2 ? 8 : 6);
    const auto step = kTau / static_cast<float>(count);
    const auto base = (enemy.volleyIndex & 1U) != 0U ? step * 0.5F : 0.0F;
    ++enemy.volleyIndex;
    for (std::uint8_t direction = 0; direction < count; ++direction) {
        const auto angle = base + static_cast<float>(direction) * step;
        for (std::uint8_t chain = 0; chain < 3; ++chain) {
            const auto slot = std::find_if(bullets.begin(), bullets.end(),
                [](const EnemyBulletState& bullet) { return !bullet.active; });
            if (slot == bullets.end()) return;
            *slot = {};
            slot->x = enemy.x;
            slot->y = enemy.y - 12.0F;
            slot->velocityX = std::cos(angle) * (124.0F / 60.0F);
            slot->velocityY = std::sin(angle) * (124.0F / 60.0F);
            slot->radius = 6.0F;
            slot->remainingTicks = 210;
            slot->launchDelayTicks = static_cast<std::uint16_t>(chain * 5U);
            slot->damage = 8;
            slot->type = EnemyBulletType::BossFire;
            slot->active = true;
        }
    }
}

void moveEnemies(std::array<EnemyState, kMaximumEnemies>& enemies,
                 std::array<PlayerState, pixel_twins::kControllerCount>& players,
                 std::array<EnemyBulletState, kMaximumEnemyBullets>& enemyBullets,
                 const world::WorldMap& map,
                 std::uint32_t& randomState,
                 const BalanceProfile& balance) noexcept {
    for (auto& enemy : enemies) {
        if (!enemy.active) continue;
        if (enemy.spawnDelayTicks > 0) {
            --enemy.spawnDelayTicks;
            continue;
        }
        if (enemy.bornTicks > 0) {
            --enemy.bornTicks;
            continue;
        }
        const auto speed = enemy.speedPerTick * (enemy.slowTicks > 0 ? 0.42F : 1.0F);
        if (enemy.slowTicks > 0) --enemy.slowTicks;
        if (enemy.attackCooldownTicks > 0) --enemy.attackCooldownTicks;
        if (enemy.attackAnimationTicks > 0) --enemy.attackAnimationTicks;
        const auto beforeX = enemy.x;
        const auto beforeY = enemy.y;
        auto* target = nearestLivingPlayer(players, enemy.x, enemy.y);
        auto dx = target->x - enemy.x;
        auto dy = target->y - enemy.y;
        const auto targetDistance = std::sqrt(dx * dx + dy * dy);
        normalize(dx, dy);
        if (enemy.kind == EnemyKind::Boss) {
            moveActor(enemy, dx * speed, dy * speed, map);
            if (enemy.attackCooldownTicks == 0) {
                fireBossRadial(enemy, enemyBullets);
                enemy.attackCooldownTicks = enemy.hp < enemy.maxHp / 2 ? 81 : 105;
            }
        } else if (enemy.kind == EnemyKind::Bat) {
            if (enemy.dashTicks > 0) {
                --enemy.dashTicks;
                moveActor(enemy, enemy.dashVelocityX, enemy.dashVelocityY, map);
            } else {
                enemy.phase += 7.0F / 60.0F;
                const auto wave = std::sin(enemy.phase) * (28.0F / 60.0F);
                moveActor(enemy, dx * speed - dy * wave, dy * speed + dx * wave, map);
                if (enemy.attackCooldownTicks == 0 && targetDistance < 90.0F) {
                    enemy.dashTicks = 17;
                    enemy.dashVelocityX = dx * (155.0F / 60.0F);
                    enemy.dashVelocityY = dy * (155.0F / 60.0F);
                    enemy.attackCooldownTicks = static_cast<std::uint16_t>(
                        std::round((1.7F + randomUnit(randomState) * 0.8F) * 60.0F));
                }
            }
        } else if (enemy.kind == EnemyKind::Wisp) {
            enemy.phase += 4.4F / 60.0F;
            const auto waveX = std::sin(enemy.phase) * (42.0F / 60.0F);
            const auto waveY = std::sin(enemy.phase * 1.3F) * (42.0F / 60.0F);
            moveActor(enemy, dx * speed - dy * waveX, dy * speed + dx * waveY, map);
        } else if (enemy.kind == EnemyKind::Archer || enemy.kind == EnemyKind::Mage) {
            const auto nearRange = enemy.kind == EnemyKind::Archer ? 72.0F : 85.0F;
            const auto farRange = enemy.kind == EnemyKind::Archer ? 105.0F : 135.0F;
            if (targetDistance < nearRange) moveActor(enemy, -dx * speed, -dy * speed, map);
            else if (targetDistance > farRange) moveActor(enemy, dx * speed, dy * speed, map);
            const auto shotRange = enemy.kind == EnemyKind::Archer ? 150.0F : 165.0F;
            if (enemy.attackCooldownTicks == 0 && targetDistance < shotRange) {
                if (enemy.kind == EnemyKind::Archer) {
                    constexpr float kStep = kTau / 16.0F;
                    const auto angle = std::round(std::atan2(dy, dx) / kStep) * kStep;
                    dx = std::cos(angle);
                    dy = std::sin(angle);
                    enemy.facing = facingFor(dx, dy);
                    enemy.attackAnimationTicks = 20;
                }
                (void)spawnEnemyBullet(enemyBullets, enemy,
                    enemy.kind == EnemyKind::Archer ? EnemyBulletType::Arrow : EnemyBulletType::Magic,
                    dx, dy);
                const auto base = enemy.kind == EnemyKind::Archer ? 2.2F : 1.9F;
                enemy.attackCooldownTicks = static_cast<std::uint16_t>(
                    std::round((base + randomUnit(randomState) * 0.9F) * 60.0F));
            }
        } else {
            moveActor(enemy, dx * speed, dy * speed, map);
        }

        const auto movedX = enemy.x - beforeX;
        const auto movedY = enemy.y - beforeY;
        enemy.moving = std::sqrt(movedX * movedX + movedY * movedY) > 0.01F;
        if (enemy.moving) enemy.facing = facingFor(movedX, movedY);

        for (auto& player : players) {
            if (player.hp <= 0 || player.invulnerabilityTicks != 0) continue;
            const auto contactRange = enemy.radius + kPlayerRadius;
            if (squaredDistance(enemy.x, enemy.y, player.x, player.y) < contactRange * contactRange) {
                auto damage = kContactDamage;
                if (enemy.kind == EnemyKind::Boss) damage = 8;
                else if (enemy.kind == EnemyKind::Golem || enemy.kind == EnemyKind::Skeleton) damage = 6;
                else if (enemy.kind == EnemyKind::Bat) damage = 4;
                damage = scaledValue(damage, balance.incomingDamagePercent);
                player.hp = static_cast<std::int16_t>(std::max(0, player.hp - damage));
                player.invulnerabilityTicks = kContactInvulnerabilityTicks;
            }
        }
    }
}

bool fireLight(PlayerState& player,
               std::uint8_t owner,
               std::array<EnemyState, kMaximumEnemies>& enemies,
               std::array<PlayerBulletState, kMaximumPlayerBullets>& bullets,
               std::array<ImpactEffectState, kMaximumImpactEffects>& impacts) noexcept {
    auto* target = nearestEnemy(enemies, player.x, player.y, kLightSeekRange);
    if (target == nullptr) return false;
    auto dx = target->x - player.x;
    auto dy = target->y - player.y;
    normalize(dx, dy);
    const auto stats = attackStats(player.lightLevel, 1, kLightDamage, kLightSpeedPerTick,
                                   3, 24.0F / 60.0F);
    bool fired = false;
    const auto baseAngle = std::atan2(dy, dx);
    for (std::uint8_t index = 0; index < stats.count; ++index) {
        const auto angle = baseAngle + static_cast<float>(index) * kTau / stats.count;
        fired = spawnBullet(bullets, player, owner, PlayerAttack::Light, angle,
                            stats.damage, stats.speed, kLightLifetimeTicks, 2.0F) || fired;
    }
    if (fired) spawnImpact(impacts, ImpactEffectType::CastSpark,
                           player.x + dx * 10.0F, player.y + dy * 10.0F - 10.0F);
    return fired;
}

bool fireSpread(PlayerState& player,
                std::uint8_t owner,
                PlayerAttack type,
                const AttackStats& stats,
                float spread,
                std::uint16_t lifetimeTicks,
                float radius,
                std::array<PlayerBulletState, kMaximumPlayerBullets>& bullets,
                std::array<ImpactEffectState, kMaximumImpactEffects>& impacts) noexcept {
    const auto baseAngle = facingAngle(player.facing);
    bool fired = false;
    for (std::uint8_t index = 0; index < stats.count; ++index) {
        const auto offset = stats.count == 1 ? 0.0F
            : (static_cast<float>(index) - static_cast<float>(stats.count - 1U) * 0.5F) * spread;
        fired = spawnBullet(bullets, player, owner, type, baseAngle + offset,
                            stats.damage, stats.speed, lifetimeTicks, radius) || fired;
    }
    if (fired) spawnImpact(impacts, ImpactEffectType::CastSpark,
                           player.x + std::cos(baseAngle) * 10.0F,
                           player.y + std::sin(baseAngle) * 10.0F - 10.0F);
    return fired;
}

void spawnWindSlash(PlayerState& player, std::uint8_t owner,
                    std::array<WindSlashState, kMaximumWindSlashes>& slashes) noexcept {
    const auto slot = std::find_if(slashes.begin(), slashes.end(),
        [](const WindSlashState& slash) { return !slash.active; });
    if (slot == slashes.end()) return;
    const auto stats = attackStats(player.windLevel, 1, 15, 32.0F, 7, 4.0F);
    *slot = {};
    slot->owner = owner;
    slot->bladeCount = stats.count;
    slot->damage = stats.damage;
    slot->outerRadius = stats.speed;
    slot->startAngle = player.orbAngle;
    slot->remainingTicks = 28;
    slot->active = true;
}

void spawnThunder(PlayerState& player, std::uint8_t owner,
                  std::uint32_t& randomState,
                  std::array<ThunderStrikeState, kMaximumThunderStrikes>& strikes,
                  std::array<EnemyState, kMaximumEnemies>& enemies,
                  std::array<XpGemState, kMaximumXpGems>& xpGems,
                  std::array<std::uint32_t, pixel_twins::kControllerCount>& scores,
                  std::array<std::uint32_t, pixel_twins::kControllerCount>& killCounts) noexcept {
    const auto stats = attackStats(player.thunderLevel, 1, 12, 100.0F, 5, 8.0F);
    for (std::uint8_t strikeIndex = 0; strikeIndex < stats.count; ++strikeIndex) {
        const auto slot = std::find_if(strikes.begin(), strikes.end(),
            [](const ThunderStrikeState& strike) { return !strike.active; });
        if (slot == strikes.end()) return;
        randomState = randomState * 1664525U + 1013904223U;
        const auto angle = static_cast<float>(randomState & 0xffffU) * kTau / 65536.0F;
        randomState = randomState * 1664525U + 1013904223U;
        const auto distance = std::sqrt(static_cast<float>(randomState & 0xffffU) / 65535.0F)
            * stats.speed;
        slot->x = std::clamp(player.x + std::cos(angle) * distance, 16.0F, kMapPixelWidth - 16.0F);
        slot->y = std::clamp(player.y + std::sin(angle) * distance, 16.0F, kMapPixelHeight - 16.0F);
        slot->remainingTicks = 20;
        slot->ageTicks = 0;
        slot->active = true;
        for (auto& enemy : enemies) {
            if (!enemy.active) continue;
            const auto range = slot->radius + enemy.radius;
            if (squaredDistance(slot->x, slot->y, enemy.x, enemy.y) > range * range) continue;
            enemy.hp = static_cast<std::int16_t>(enemy.hp - stats.damage);
            if (enemy.hp <= 0) killEnemy(enemy, owner, xpGems, scores, killCounts);
        }
    }
}

void damageOrbs(PlayerState& player, std::uint8_t owner,
                std::array<EnemyState, kMaximumEnemies>& enemies,
                std::array<EnemyBulletState, kMaximumEnemyBullets>& enemyBullets,
                std::array<XpGemState, kMaximumXpGems>& xpGems,
                std::array<std::uint32_t, pixel_twins::kControllerCount>& scores,
                std::array<std::uint32_t, pixel_twins::kControllerCount>& killCounts,
                std::array<ImpactEffectState, kMaximumImpactEffects>& impacts) noexcept {
    const auto stats = attackStats(player.orbLevel, 1, 4, 22.0F, 2, 4.0F);
    for (std::uint8_t orbIndex = 0; orbIndex < stats.count; ++orbIndex) {
        const auto angle = player.orbAngle + static_cast<float>(orbIndex) * kTau / stats.count;
        const auto x = player.x + std::cos(angle) * stats.speed;
        const auto y = player.y + std::sin(angle) * stats.speed;
        for (auto& bullet : enemyBullets) {
            if (!bullet.active || bullet.type != EnemyBulletType::Arrow) continue;
            const auto range = 6.0F + bullet.radius;
            if (squaredDistance(x, y, bullet.x, bullet.y) <= range * range) bullet.active = false;
        }
        for (auto& enemy : enemies) {
            if (!enemy.active) continue;
            const auto range = enemy.radius + 4.0F;
            if (squaredDistance(x, y, enemy.x, enemy.y) >= range * range) continue;
            enemy.hp = static_cast<std::int16_t>(enemy.hp - stats.damage);
            spawnImpact(impacts, ImpactEffectType::Orb, enemy.x, enemy.y);
            if (enemy.hp <= 0) killEnemy(enemy, owner, xpGems, scores, killCounts);
        }
    }
}

void updateFamiliars(PlayerState& player, std::uint8_t owner,
                     std::array<EnemyState, kMaximumEnemies>& enemies,
                     std::array<PlayerBulletState, kMaximumPlayerBullets>& bullets) noexcept {
    if (player.familiarLevel == 0) {
        for (auto& familiar : player.familiars) familiar.active = false;
        return;
    }
    const auto stats = attackStats(player.familiarLevel, 1, 5, 135.0F / 60.0F,
                                   3, 18.0F / 60.0F);
    const auto count = std::min<std::uint8_t>(stats.count, kMaximumFamiliarsPerPlayer);
    const auto faceAngle = facingAngle(player.facing);
    const auto facingX = std::cos(faceAngle);
    const auto facingY = std::sin(faceAngle);
    const auto backX = -facingX;
    const auto backY = -facingY;
    const auto sideX = -facingY;
    const auto sideY = facingX;
    for (std::uint8_t index = 0; index < kMaximumFamiliarsPerPlayer; ++index) {
        auto& familiar = player.familiars[index];
        familiar.active = index < count;
        if (!familiar.active) continue;
        if (familiar.x == 0.0F && familiar.y == 0.0F) {
            familiar.x = player.x;
            familiar.y = player.y;
        }
        float sideOffset = 0.0F;
        float backOffset = 20.0F;
        if (count == 2) {
            sideOffset = index == 0 ? -22.0F : 22.0F;
            backOffset = 6.0F;
        } else if (count >= 3) {
            sideOffset = (static_cast<float>(index) - static_cast<float>(count - 1U) * 0.5F) * 18.0F;
            backOffset = index == 1 ? 22.0F : 8.0F;
        }
        const auto homeX = player.x + backX * backOffset + sideX * sideOffset;
        const auto homeY = player.y + backY * backOffset + sideY * sideOffset;
        auto targetX = homeX;
        auto targetY = homeY;
        const auto seekRange = 95.0F + static_cast<float>(player.familiarLevel / 3U) * 25.0F;
        if (auto* target = nearestEnemy(enemies, homeX, homeY, seekRange)) {
            auto dx = target->x - homeX;
            auto dy = target->y - homeY;
            normalize(dx, dy);
            targetX += dx * 22.0F;
            targetY += dy * 22.0F;
        }
        auto dx = targetX - familiar.x;
        auto dy = targetY - familiar.y;
        const auto distance = std::sqrt(dx * dx + dy * dy);
        if (distance > 0.0F) {
            normalize(dx, dy);
            const auto step = std::min(54.0F / 60.0F, distance);
            familiar.x += dx * step;
            familiar.y += dy * step;
            familiar.facing = facingFor(dx, dy);
        }
    }
    if (player.familiarCooldownTicks > 0) return;
    bool fired = false;
    const auto fireRange = 135.0F + static_cast<float>(player.familiarLevel / 3U) * 25.0F;
    for (auto& familiar : player.familiars) {
        if (!familiar.active) continue;
        auto* target = nearestEnemy(enemies, familiar.x, familiar.y, fireRange);
        if (target == nullptr) continue;
        auto dx = target->x - familiar.x;
        auto dy = target->y - familiar.y;
        normalize(dx, dy);
        familiar.facing = facingFor(dx, dy);
        fired = spawnBulletAt(bullets, familiar.x, familiar.y, owner, PlayerAttack::Familiar,
                              dx, dy, stats.damage, stats.speed, 63, 2.0F) || fired;
    }
    if (fired) {
        player.familiarCooldownTicks = cooldownTicks(0.7F, 0.035F,
                                                      player.familiarLevel, 0.26F);
    }
}

void processBombs(std::array<PlayerState, pixel_twins::kControllerCount>& players,
                  std::array<EnemyState, kMaximumEnemies>& enemies,
                  std::array<EnemyBulletState, kMaximumEnemyBullets>& enemyBullets,
                  std::array<XpGemState, kMaximumXpGems>& xpGems,
                  std::array<std::uint32_t, pixel_twins::kControllerCount>& scores,
                  std::array<std::uint32_t, pixel_twins::kControllerCount>& killCounts,
                  std::uint32_t randomState) noexcept {
    constexpr float kRadius = 96.0F;
    for (std::size_t playerIndex = 0; playerIndex < players.size(); ++playerIndex) {
        auto& player = players[playerIndex];
        if (player.bombEffectTicks > 0) --player.bombEffectTicks;
        if (!player.bombPending) continue;
        player.bombPending = false;
        player.bombEffectTicks = 34;
        player.bombEffectX = player.x;
        player.bombEffectY = player.y;
        player.bombEffectSeed = randomState
            ^ (static_cast<std::uint32_t>(playerIndex) + 1U) * 2246822519U;
        for (auto& enemy : enemies) {
            if (!enemy.active || enemy.bornTicks > 0) continue;
            const auto range = kRadius + enemy.radius;
            if (squaredDistance(player.x, player.y, enemy.x, enemy.y) > range * range) continue;
            enemy.hp = static_cast<std::int16_t>(enemy.hp - 300);
            if (enemy.hp <= 0) {
                killEnemy(enemy, static_cast<std::uint8_t>(playerIndex), xpGems, scores,
                          killCounts);
            }
        }
        for (auto& bullet : enemyBullets) {
            if (!bullet.active || (bullet.type != EnemyBulletType::Arrow
                && bullet.type != EnemyBulletType::Magic)) continue;
            const auto range = kRadius + bullet.radius;
            if (squaredDistance(player.x, player.y, bullet.x, bullet.y) <= range * range) {
                bullet.active = false;
            }
        }
    }
}

void updateBullets(std::array<PlayerBulletState, kMaximumPlayerBullets>& bullets,
                   std::array<EnemyState, kMaximumEnemies>& enemies,
                   std::array<EnemyBulletState, kMaximumEnemyBullets>& enemyBullets,
                   std::array<XpGemState, kMaximumXpGems>& xpGems,
                   std::array<std::uint32_t, pixel_twins::kControllerCount>& scores,
                   std::array<std::uint32_t, pixel_twins::kControllerCount>& killCounts,
                   std::array<ImpactEffectState, kMaximumImpactEffects>& impacts) noexcept {
    for (auto& bullet : bullets) {
        if (!bullet.active) continue;
        ++bullet.ageTicks;
        if (bullet.type == PlayerAttack::Light && bullet.ageTicks >= 10) {
          if (auto* target = nearestEnemy(enemies, bullet.x, bullet.y, kLightHomingRange)) {
            auto dx = target->x - bullet.x;
            auto dy = target->y - bullet.y;
            normalize(dx, dy);
            bullet.velocityX = bullet.velocityX * 0.92F + dx * bullet.speed * 0.08F;
            bullet.velocityY = bullet.velocityY * 0.92F + dy * bullet.speed * 0.08F;
            normalize(bullet.velocityX, bullet.velocityY);
            bullet.velocityX *= bullet.speed;
            bullet.velocityY *= bullet.speed;
          }
        }
        bullet.x += bullet.velocityX;
        bullet.y += bullet.velocityY;
        for (auto& enemyBullet : enemyBullets) {
            if (!enemyBullet.active || enemyBullet.type != EnemyBulletType::Arrow) continue;
            const auto range = bullet.radius + 3.0F + enemyBullet.radius;
            if (squaredDistance(bullet.x, bullet.y, enemyBullet.x, enemyBullet.y) <= range * range) {
                enemyBullet.active = false;
            }
        }
        if (bullet.remainingTicks > 0) --bullet.remainingTicks;
        if (bullet.remainingTicks == 0 || bullet.x < 0.0F || bullet.y < 0.0F
            || bullet.x >= kMapPixelWidth || bullet.y >= kMapPixelHeight) {
            bullet.active = false;
            continue;
        }
        for (auto& enemy : enemies) {
            if (!enemy.active) continue;
            const auto hitRange = enemy.radius + bullet.radius;
            if (squaredDistance(bullet.x, bullet.y, enemy.x, enemy.y) < hitRange * hitRange) {
                enemy.hp = static_cast<std::int16_t>(enemy.hp - bullet.damage);
                if (bullet.type == PlayerAttack::Ice) enemy.slowTicks = std::max<std::uint16_t>(enemy.slowTicks, 102);
                auto impactType = ImpactEffectType::Light;
                if (bullet.type == PlayerAttack::Fire) impactType = ImpactEffectType::Fire;
                else if (bullet.type == PlayerAttack::Ice) impactType = ImpactEffectType::Ice;
                else if (bullet.type == PlayerAttack::Familiar) impactType = ImpactEffectType::Familiar;
                spawnImpact(impacts, impactType, bullet.x, bullet.y - 10.0F);
                if (enemy.hp <= 0) {
                    killEnemy(enemy, bullet.owner, xpGems, scores, killCounts);
                }
                bullet.active = false;
                break;
            }
        }
    }
}

void updateWindSlashes(std::array<WindSlashState, kMaximumWindSlashes>& slashes,
                       std::array<PlayerState, pixel_twins::kControllerCount>& players,
                       std::array<EnemyState, kMaximumEnemies>& enemies,
                       std::array<EnemyBulletState, kMaximumEnemyBullets>& enemyBullets,
                       std::array<XpGemState, kMaximumXpGems>& xpGems,
                       std::array<std::uint32_t, pixel_twins::kControllerCount>& scores,
                       std::array<std::uint32_t, pixel_twins::kControllerCount>& killCounts,
                       std::array<ImpactEffectState, kMaximumImpactEffects>& impacts) noexcept {
    for (auto& slash : slashes) {
        if (!slash.active) continue;
        ++slash.ageTicks;
        if (slash.remainingTicks > 0) --slash.remainingTicks;
        if (slash.remainingTicks == 0 || slash.owner >= players.size()) {
            slash.active = false;
            continue;
        }
        const auto& owner = players[slash.owner];
        for (auto& bullet : enemyBullets) {
            if (!bullet.active || bullet.type != EnemyBulletType::Arrow) continue;
            const auto range = slash.outerRadius + bullet.radius;
            if (squaredDistance(owner.x, owner.y, bullet.x, bullet.y) <= range * range) {
                bullet.active = false;
            }
        }
        for (std::size_t enemyIndex = 0; enemyIndex < enemies.size(); ++enemyIndex) {
            if (slash.hitCooldownTicks[enemyIndex] > 0) --slash.hitCooldownTicks[enemyIndex];
            auto& enemy = enemies[enemyIndex];
            if (!enemy.active || slash.hitCooldownTicks[enemyIndex] > 0) continue;
            const auto distanceSquared = squaredDistance(owner.x, owner.y, enemy.x, enemy.y);
            const auto inner = std::max(0.0F, slash.innerRadius - enemy.radius);
            const auto outer = slash.outerRadius + enemy.radius;
            if (distanceSquared < inner * inner || distanceSquared > outer * outer) continue;
            enemy.hp = static_cast<std::int16_t>(enemy.hp - slash.damage);
            slash.hitCooldownTicks[enemyIndex] = 7;
            spawnImpact(impacts, ImpactEffectType::Wind, enemy.x, enemy.y);
            if (enemy.hp <= 0) killEnemy(enemy, slash.owner, xpGems, scores, killCounts);
        }
    }
}

void updateThunderStrikes(std::array<ThunderStrikeState, kMaximumThunderStrikes>& strikes) noexcept {
    for (auto& strike : strikes) {
        if (!strike.active) continue;
        ++strike.ageTicks;
        if (strike.remainingTicks > 0) --strike.remainingTicks;
        if (strike.remainingTicks == 0) strike.active = false;
    }
}

void updateEnemyBullets(std::array<EnemyBulletState, kMaximumEnemyBullets>& bullets,
                        std::array<PlayerState, pixel_twins::kControllerCount>& players,
                        const BalanceProfile& balance) noexcept {
    for (auto& bullet : bullets) {
        if (!bullet.active) continue;
        if (bullet.launchDelayTicks > 0) {
            --bullet.launchDelayTicks;
            if (bullet.launchDelayTicks > 0) continue;
        }
        ++bullet.ageTicks;
        bullet.x += bullet.velocityX;
        bullet.y += bullet.velocityY;
        if (bullet.remainingTicks > 0) --bullet.remainingTicks;
        if (bullet.remainingTicks == 0 || bullet.x < 0.0F || bullet.y < 0.0F
            || bullet.x >= kMapPixelWidth || bullet.y >= kMapPixelHeight) {
            bullet.active = false;
            continue;
        }
        for (auto& player : players) {
            if (player.hp <= 0 || player.invulnerabilityTicks > 0) continue;
            const auto range = bullet.radius + kPlayerRadius;
            if (squaredDistance(bullet.x, bullet.y, player.x, player.y) >= range * range) continue;
            const auto damage = scaledValue(bullet.damage, balance.incomingDamagePercent);
            player.hp = static_cast<std::int16_t>(std::max(0, player.hp - damage));
            player.invulnerabilityTicks = 33;
            bullet.active = false;
            break;
        }
    }
}

GameplayOutcome updateRevives(
    std::array<PlayerState, pixel_twins::kControllerCount>& players) noexcept {
    constexpr float kReviveRange = kPlayerRadius * 2.0F + 12.0F;
    constexpr std::int16_t kDonorCost = 2;
    constexpr std::int16_t kReviveHp = 2;
    for (std::size_t downedIndex = 0; downedIndex < players.size(); ++downedIndex) {
        auto& downed = players[downedIndex];
        auto& donor = players[1U - downedIndex];
        if (downed.hp > 0 || donor.hp <= kDonorCost) continue;
        if (squaredDistance(downed.x, downed.y, donor.x, donor.y)
            > kReviveRange * kReviveRange) continue;
        donor.hp = static_cast<std::int16_t>(std::max(1, donor.hp - kDonorCost));
        downed.hp = kReviveHp;
        downed.invulnerabilityTicks = 72;
        downed.hpRegenAccumulator = 0.0F;
    }
    return players[0].hp <= 0 && players[1].hp <= 0
        ? GameplayOutcome::Down : GameplayOutcome::Running;
}

std::uint8_t perkLevel(const PlayerState& player, Perk perk) noexcept {
    switch (perk) {
    case Perk::Light: return player.lightLevel;
    case Perk::Fire: return player.fireLevel;
    case Perk::Wind: return player.windLevel;
    case Perk::Thunder: return player.thunderLevel;
    case Perk::Ice: return player.iceLevel;
    case Perk::Orb: return player.orbLevel;
    case Perk::Familiar: return player.familiarLevel;
    case Perk::Speed: return player.speedLevel;
    case Perk::MaxHp: return player.maxHpLevel;
    case Perk::Heal:
    case Perk::Bomb: return 0;
    }
    return 0;
}

void rollPerks(PlayerState& player, std::uint32_t& randomState) noexcept {
    constexpr std::array<Perk, 11> kPool{{
        Perk::Light, Perk::Fire, Perk::Wind, Perk::Thunder, Perk::Ice,
        Perk::Orb, Perk::Familiar, Perk::Speed, Perk::MaxHp, Perk::Heal, Perk::Bomb,
    }};
    constexpr std::array<std::uint8_t, 11> kWeights{{5, 5, 4, 4, 4, 4, 4, 3, 3, 4, 3}};
    std::array<bool, kPool.size()> chosen{};
    for (std::size_t slot = 0; slot < player.perkChoices.size(); ++slot) {
        unsigned total = 0;
        for (std::size_t index = 0; index < kPool.size(); ++index) {
            const auto maximum = kPool[index] == Perk::Speed || kPool[index] == Perk::MaxHp ? 4U : 5U;
            const auto instant = kPool[index] == Perk::Heal || kPool[index] == Perk::Bomb;
            const auto capped = !instant && perkLevel(player, kPool[index]) >= maximum;
            if (!chosen[index] && !capped) total += kWeights[index];
        }
        if (total == 0) {
            player.perkChoices[slot] = Perk::Heal;
            continue;
        }
        randomState = randomState * 1664525U + 1013904223U;
        auto pick = static_cast<unsigned>((randomState >> 16U) % total);
        for (std::size_t index = 0; index < kPool.size(); ++index) {
            const auto maximum = kPool[index] == Perk::Speed || kPool[index] == Perk::MaxHp ? 4U : 5U;
            const auto instant = kPool[index] == Perk::Heal || kPool[index] == Perk::Bomb;
            const auto capped = !instant && perkLevel(player, kPool[index]) >= maximum;
            if (chosen[index] || capped) continue;
            if (pick < kWeights[index]) {
                player.perkChoices[slot] = kPool[index];
                chosen[index] = true;
                break;
            }
            pick -= kWeights[index];
        }
    }
}

void beginPerkChoice(PlayerState& player, std::uint32_t& randomState) noexcept {
    if (player.choosingPerk || player.pendingPerkChoices == 0) return;
    rollPerks(player, randomState);
    player.choosingPerk = true;
}

void gainXp(PlayerState& player, std::uint8_t owner, std::uint16_t amount,
            std::uint32_t& randomState,
            std::array<PerkEffectState, kMaximumPerkEffects>& perkEffects,
            Difficulty difficulty) noexcept {
    player.xp = static_cast<std::uint16_t>(player.xp + amount);
    auto leveledUp = false;
    while (player.xp >= xpNeededForLevel(player.level, difficulty)) {
        player.xp = static_cast<std::uint16_t>(
            player.xp - xpNeededForLevel(player.level, difficulty));
        if (player.level < 255) ++player.level;
        if (player.pendingPerkChoices < 255) ++player.pendingPerkChoices;
        leveledUp = true;
    }
    if (leveledUp) spawnPerkEffect(perkEffects, PerkEffectType::LevelUp, owner, randomState);
    beginPerkChoice(player, randomState);
}

void applyPerk(PlayerState& player, std::uint8_t owner, Perk perk,
               std::uint32_t& randomState,
               std::array<PerkEffectState, kMaximumPerkEffects>& perkEffects) noexcept {
    switch (perk) {
    case Perk::Light:
        if (player.lightLevel < 255) ++player.lightLevel;
        break;
    case Perk::Fire:
        if (player.fireLevel < 255) ++player.fireLevel;
        break;
    case Perk::Wind:
        if (player.windLevel < 255) ++player.windLevel;
        break;
    case Perk::Thunder:
        if (player.thunderLevel < 255) ++player.thunderLevel;
        break;
    case Perk::Ice:
        if (player.iceLevel < 255) ++player.iceLevel;
        break;
    case Perk::Orb:
        if (player.orbLevel < 255) ++player.orbLevel;
        break;
    case Perk::Familiar:
        if (player.familiarLevel < 255) ++player.familiarLevel;
        break;
    case Perk::Speed:
        if (player.speedLevel < 255) ++player.speedLevel;
        break;
    case Perk::MaxHp:
        if (player.maxHpLevel < 255) ++player.maxHpLevel;
        player.maxHp = static_cast<std::int16_t>(std::min<std::int32_t>(999, player.maxHp + 5));
        player.hp = static_cast<std::int16_t>(std::min<std::int32_t>(player.maxHp, player.hp + 5));
        break;
    case Perk::Heal:
        player.hp = static_cast<std::int16_t>(std::min<std::int32_t>(player.maxHp, player.hp + 26));
        break;
    case Perk::Bomb:
        player.bombPending = true;
        return;
    }
    const auto effect = perk == Perk::Heal ? PerkEffectType::Heal
        : perk == Perk::MaxHp ? PerkEffectType::HpUp : PerkEffectType::Upgrade;
    spawnPerkEffect(perkEffects, effect, owner, randomState);
    player.sharePending = true;
    player.sharePerk = perk;
}

void updateAutoPerkChoice(PlayerState& player,
                          std::uint8_t owner,
                          const std::array<EnemyState, kMaximumEnemies>& enemies,
                          std::uint32_t& randomState,
                          std::array<PerkEffectState, kMaximumPerkEffects>& perkEffects) noexcept {
    if (!player.choosingPerk) return;
    constexpr std::array<float, 11> kWeights{{5.0F, 5.0F, 4.0F, 4.0F, 4.0F, 4.0F,
                                               4.0F, 3.0F, 3.0F, 4.0F, 3.0F}};
    std::uint8_t nearbyEnemies = 0;
    for (const auto& enemy : enemies) {
        if (!enemy.active || enemy.bornTicks > 0) continue;
        if (squaredDistance(player.x, player.y, enemy.x, enemy.y) < 92.0F * 92.0F) ++nearbyEnemies;
    }
    std::array<float, 4> scores{};
    float total = 0.0F;
    for (std::size_t index = 0; index < player.perkChoices.size(); ++index) {
        const auto perk = player.perkChoices[index];
        const auto level = perkLevel(player, perk);
        const auto maximum = perk == Perk::Speed || perk == Perk::MaxHp ? 4U : 5U;
        const auto instant = perk == Perk::Heal || perk == Perk::Bomb;
        if (!instant && level >= maximum) continue;
        auto score = kWeights[static_cast<std::size_t>(perk)];
        if (perk == Perk::Heal) score = player.hp * 100 <= player.maxHp * 48 ? 9.0F : 0.8F;
        else if (perk == Perk::MaxHp) score = player.hp * 10 <= player.maxHp * 7 ? 7.0F : 3.0F;
        else if (perk == Perk::Bomb) score = nearbyEnemies >= 5 ? 8.0F : 1.2F;
        else score += static_cast<float>(std::max(0, 4 - static_cast<int>(level))) * 0.75F;
        scores[index] = score;
        total += score;
    }
    auto pick = randomUnit(randomState) * total;
    std::size_t selected = 0;
    for (std::size_t index = 0; index < scores.size(); ++index) {
        pick -= scores[index];
        if (pick <= 0.0F) {
            selected = index;
            break;
        }
    }
    constexpr std::array<std::uint8_t, 4> kSlotsByPackIndex{{1, 0, 2, 3}};
    player.perkFlash = player.perkChoices[selected];
    player.perkFlashSlot = kSlotsByPackIndex[selected];
    player.perkFlashTicks = 25;
    applyPerk(player, owner, player.perkChoices[selected], randomState, perkEffects);
    if (player.pendingPerkChoices > 0) --player.pendingPerkChoices;
    player.choosingPerk = false;
    beginPerkChoice(player, randomState);
}

void applyLinkedUpgrade(PlayerState& player, Perk perk) noexcept {
    const auto index = static_cast<std::size_t>(perk);
    if (index >= player.linkedUpgradeTenths.size()) return;
    auto& tenths = player.linkedUpgradeTenths[index];
    tenths = static_cast<std::uint8_t>(tenths + 3U);
    if (perk == Perk::MaxHp) {
        player.linkedHpHalfUnits = static_cast<std::uint8_t>(player.linkedHpHalfUnits + 3U);
        while (player.linkedHpHalfUnits >= 2U) {
            player.linkedHpHalfUnits = static_cast<std::uint8_t>(player.linkedHpHalfUnits - 2U);
            player.maxHp = static_cast<std::int16_t>(std::min<std::int32_t>(999, player.maxHp + 1));
            player.hp = static_cast<std::int16_t>(std::min<std::int32_t>(player.maxHp, player.hp + 1));
        }
    }
    while (tenths >= 10U) {
        tenths = static_cast<std::uint8_t>(tenths - 10U);
        switch (perk) {
        case Perk::Light: if (player.lightLevel < 255) ++player.lightLevel; break;
        case Perk::Fire: if (player.fireLevel < 255) ++player.fireLevel; break;
        case Perk::Wind: if (player.windLevel < 255) ++player.windLevel; break;
        case Perk::Thunder: if (player.thunderLevel < 255) ++player.thunderLevel; break;
        case Perk::Ice: if (player.iceLevel < 255) ++player.iceLevel; break;
        case Perk::Orb: if (player.orbLevel < 255) ++player.orbLevel; break;
        case Perk::Familiar: if (player.familiarLevel < 255) ++player.familiarLevel; break;
        case Perk::Speed: if (player.speedLevel < 255) ++player.speedLevel; break;
        case Perk::MaxHp: if (player.maxHpLevel < 255) ++player.maxHpLevel; break;
        case Perk::Heal:
        case Perk::Bomb: break;
        }
    }
}

void processLinkShares(std::array<PlayerState, pixel_twins::kControllerCount>& players,
                       std::array<PerkEffectState, kMaximumPerkEffects>& perkEffects,
                       std::uint32_t& randomState) noexcept {
    for (std::size_t ownerIndex = 0; ownerIndex < players.size(); ++ownerIndex) {
        auto& owner = players[ownerIndex];
        if (!owner.sharePending) continue;
        owner.sharePending = false;
        auto& partner = players[1U - ownerIndex];
        if (squaredDistance(owner.x, owner.y, partner.x, partner.y) > kLinkRange * kLinkRange) continue;
        if (owner.sharePerk == Perk::Heal) {
            partner.hp = static_cast<std::int16_t>(std::min<std::int32_t>(partner.maxHp, partner.hp + 18));
            spawnPerkEffect(perkEffects, PerkEffectType::Heal,
                            static_cast<std::uint8_t>(1U - ownerIndex), randomState);
        } else if (owner.sharePerk != Perk::Bomb) {
            applyLinkedUpgrade(partner, owner.sharePerk);
            const auto effect = owner.sharePerk == Perk::MaxHp
                ? PerkEffectType::HpUp : PerkEffectType::Upgrade;
            spawnPerkEffect(perkEffects, effect,
                            static_cast<std::uint8_t>(1U - ownerIndex), randomState);
        }
    }
}

void updateSealStones(
    const std::array<PlayerState, pixel_twins::kControllerCount>& players,
    const world::WorldMap& map,
    std::array<SealState, 3>& seals,
    std::array<std::uint32_t, pixel_twins::kControllerCount>& scores,
    std::uint32_t elapsedTicks,
    std::uint8_t& activeCount,
    std::uint16_t& noticeTicks,
    std::uint16_t& bossSpawnPendingTicks) noexcept {
    constexpr float kActivationRange = 42.0F;
    constexpr std::uint32_t kSealScore = 1000;
    for (std::size_t sealIndex = 0; sealIndex < seals.size(); ++sealIndex) {
        auto& seal = seals[sealIndex];
        if (seal.active) continue;
        const auto sealX = static_cast<float>(map.seals[sealIndex].x * kWorldTileSize)
            + static_cast<float>(kWorldTileSize) * 0.5F;
        const auto sealY = static_cast<float>(map.seals[sealIndex].y * kWorldTileSize)
            + static_cast<float>(kWorldTileSize) * 0.5F;
        for (std::size_t playerIndex = 0; playerIndex < players.size(); ++playerIndex) {
            const auto& player = players[playerIndex];
            if (squaredDistance(player.x, player.y, sealX, sealY)
                >= kActivationRange * kActivationRange) continue;
            seal.active = true;
            seal.activatedAtTicks = elapsedTicks;
            for (auto& score : scores) score += kSealScore;
            activeCount = static_cast<std::uint8_t>(activeCount + 1U);
            noticeTicks = 132;
            if (activeCount == seals.size()) bossSpawnPendingTicks = 53;
            break;
        }
    }
}

bool spawnBoss(std::array<EnemyState, kMaximumEnemies>& enemies,
               std::uint32_t elapsedTicks,
               std::uint32_t& randomState,
               const BalanceProfile& balance) noexcept {
    auto slot = std::find_if(enemies.begin(), enemies.end(),
        [](const EnemyState& enemy) { return !enemy.active && enemy.deathTicks == 0; });
    if (slot == enemies.end()) slot = std::prev(enemies.end());
    *slot = makeEnemyState(EnemyKind::Boss, kWorldCenter,
                           kWorldCenter, elapsedTicks, randomState, false, balance);
    return true;
}

void updatePerkChoice(PlayerState& player, const pixel_twins::ControllerState& controller,
                      std::uint8_t owner, std::uint32_t& randomState,
                      std::array<PerkEffectState, kMaximumPerkEffects>& perkEffects) noexcept {
    if (player.perkFlashTicks > 0) --player.perkFlashTicks;
    if (!player.choosingPerk) return;
    std::uint8_t choice = 255;
    std::uint8_t slot = 255;
    if (controller.isPressed(pixel_twins::ControllerButton::choiceLeft)) { choice = 1; slot = 0; }
    if (controller.isPressed(pixel_twins::ControllerButton::choiceUp)) { choice = 0; slot = 1; }
    if (controller.isPressed(pixel_twins::ControllerButton::choiceRight)) { choice = 2; slot = 2; }
    if (controller.isPressed(pixel_twins::ControllerButton::choiceDown)) { choice = 3; slot = 3; }
    if (choice >= player.perkChoices.size()) return;
    player.perkFlash = player.perkChoices[choice];
    player.perkFlashSlot = slot;
    player.perkFlashTicks = 25;
    applyPerk(player, owner, player.perkChoices[choice], randomState, perkEffects);
    if (player.pendingPerkChoices > 0) --player.pendingPerkChoices;
    player.choosingPerk = false;
    beginPerkChoice(player, randomState);
}

bool updateXpRecallCircle(std::array<XpGemState, kMaximumXpGems>& xpGems,
                          std::array<PlayerState, pixel_twins::kControllerCount>& players) noexcept {
    auto recalled = false;
    for (std::size_t playerIndex = 0; playerIndex < players.size(); ++playerIndex) {
        auto& player = players[playerIndex];
        if (player.xpRecallEffectTicks > 0) --player.xpRecallEffectTicks;
        const auto inside = player.hp > 0
            && squaredDistance(player.x, player.y, kWorldCenter, kWorldCenter)
                <= kXpRecallRadius * kXpRecallRadius;
        if (inside && !player.xpRecallInside) {
            auto count = std::size_t{0};
            for (auto& gem : xpGems) {
                if (!gem.active || gem.owner != playerIndex) continue;
                gem.recallPlayer = static_cast<std::uint8_t>(playerIndex);
                ++count;
            }
            if (count > 0) {
                player.xpRecallEffectTicks = 42;
                recalled = true;
            }
        }
        player.xpRecallInside = inside;
    }
    return recalled;
}

void updateXpGems(std::array<XpGemState, kMaximumXpGems>& xpGems,
                  std::array<PlayerState, pixel_twins::kControllerCount>& players,
                  std::array<std::uint32_t, pixel_twins::kControllerCount>& xpEarned,
                  std::uint32_t& randomState,
                  std::array<PerkEffectState, kMaximumPerkEffects>& perkEffects,
                  Difficulty difficulty) noexcept {
    for (auto& gem : xpGems) {
        if (!gem.active) continue;
        if (gem.ageTicks < std::numeric_limits<std::uint16_t>::max()) ++gem.ageTicks;
        PlayerState* target = nullptr;
        auto targetSquared = std::numeric_limits<float>::max();
        const auto recalled = gem.recallPlayer < players.size()
            ? &players[gem.recallPlayer] : nullptr;
        if (recalled != nullptr && recalled->hp > 0) {
            target = recalled;
            targetSquared = squaredDistance(gem.x, gem.y, target->x, target->y);
        } else {
            gem.recallPlayer = 0xff;
            targetSquared = kXpPullRange * kXpPullRange;
            for (auto& player : players) {
                if (player.hp <= 0) continue;
                const auto candidate = squaredDistance(gem.x, gem.y, player.x, player.y);
                if (candidate <= targetSquared) {
                    targetSquared = candidate;
                    target = &player;
                }
            }
        }
        if (target == nullptr) continue;
        if (targetSquared < kXpCollectRange * kXpCollectRange) {
            const auto owner = static_cast<std::uint8_t>(target - players.data());
            xpEarned[owner] += gem.value;
            gainXp(*target, owner, gem.value, randomState, perkEffects, difficulty);
            gem.active = false;
            continue;
        }
        auto dx = target->x - gem.x;
        auto dy = target->y - gem.y;
        normalize(dx, dy);
        const auto speed = recalled != nullptr && recalled->hp > 0
            ? kXpRecallSpeedPerTick : kXpPullSpeedPerTick;
        const auto step = std::min(std::sqrt(targetSquared), speed);
        gem.x += dx * step;
        gem.y += dy * step;
    }
}

EnemyKind rollEnemyKind(std::uint32_t elapsedTicks, std::uint32_t& randomState) noexcept {
    const auto seconds = static_cast<float>(elapsedTicks) / 60.0F;
    const std::array<float, 7> weights{{
        58.0F,
        seconds >= 30.0F ? 14.0F + std::min(12.0F, (seconds - 30.0F) / 40.0F) : 0.0F,
        seconds >= 60.0F ? 10.0F + std::min(14.0F, (seconds - 60.0F) / 55.0F) : 0.0F,
        seconds >= 180.0F ? 4.0F + std::min(8.0F, (seconds - 180.0F) / 90.0F) : 0.0F,
        seconds >= 90.0F ? 9.0F + std::min(11.0F, (seconds - 90.0F) / 70.0F) : 0.0F,
        seconds >= 150.0F ? 10.0F + std::min(10.0F, (seconds - 150.0F) / 70.0F) : 0.0F,
        seconds >= 210.0F ? 7.0F + std::min(10.0F, (seconds - 210.0F) / 80.0F) : 0.0F,
    }};
    constexpr std::array<EnemyKind, 7> kKinds{{
        EnemyKind::Imp, EnemyKind::Bat, EnemyKind::Skeleton, EnemyKind::Golem,
        EnemyKind::Archer, EnemyKind::Wisp, EnemyKind::Mage,
    }};
    float total = 0.0F;
    for (const auto weight : weights) total += weight;
    auto pick = randomUnit(randomState) * total;
    for (std::size_t index = 0; index < weights.size(); ++index) {
        pick -= weights[index];
        if (pick <= 0.0F) return kKinds[index];
    }
    return EnemyKind::Imp;
}

bool spawnEnemyNear(std::array<EnemyState, kMaximumEnemies>& enemies,
                    const PlayerState& focus, const world::WorldMap& map,
                    EnemyKind kind, std::uint32_t elapsedTicks,
                    std::uint32_t& randomState, const EnemyState* boss,
                    const BalanceProfile& balance,
                    std::uint16_t delayTicks = 0) noexcept {
    const auto slot = std::find_if(enemies.begin(), enemies.end(),
        [](const EnemyState& enemy) { return !enemy.active && enemy.deathTicks == 0; });
    if (slot == enemies.end()) return false;
    for (std::uint8_t attempt = 0; attempt < 40; ++attempt) {
        const auto angle = randomUnit(randomState) * kTau;
        const auto range = kEnemySpawnMinimumRange
            + randomUnit(randomState) * (kEnemySpawnMaximumRange - kEnemySpawnMinimumRange);
        const auto x = std::clamp(focus.x + std::cos(angle) * range, 20.0F, kMapPixelWidth - 20.0F);
        const auto y = std::clamp(focus.y + std::sin(angle) * range, 20.0F, kMapPixelHeight - 20.0F);
        if (boss != nullptr && squaredDistance(x, y, boss->x, boss->y) < 60.0F * 60.0F) continue;
        if (!circlePositionIsWalkable(map, x, y, 8.0F)) continue;
        *slot = makeEnemyState(kind, x, y, elapsedTicks, randomState, true, balance);
        slot->spawnDelayTicks = delayTicks;
        return true;
    }
    return false;
}

void recycleDistantEnemiesForSpawn(
    std::array<EnemyState, kMaximumEnemies>& enemies,
    const std::array<PlayerState, pixel_twins::kControllerCount>& players,
    std::uint8_t requestedCount) noexcept {
    const auto activeCount = static_cast<std::size_t>(std::count_if(
        enemies.begin(), enemies.end(),
        [](const EnemyState& enemy) { return enemy.active; }));
    auto needed = activeCount + requestedCount > kMaximumEnemies
        ? activeCount + requestedCount - kMaximumEnemies
        : 0U;
    while (needed > 0U) {
        EnemyState* candidate = nullptr;
        auto candidateDistanceSquared = kEnemyRecycleRange * kEnemyRecycleRange;
        for (auto& enemy : enemies) {
            if (!enemy.active || enemy.kind == EnemyKind::Boss) continue;
            auto closestDistanceSquared = std::numeric_limits<float>::max();
            for (const auto& player : players) {
                if (player.hp <= 0) continue;
                closestDistanceSquared = std::min(
                    closestDistanceSquared,
                    squaredDistance(enemy.x, enemy.y, player.x, player.y));
            }
            if (closestDistanceSquared > candidateDistanceSquared) {
                candidate = &enemy;
                candidateDistanceSquared = closestDistanceSquared;
            }
        }
        if (candidate == nullptr) return;
        *candidate = {};
        --needed;
    }
}

void spawnSwarm(std::array<EnemyState, kMaximumEnemies>& enemies,
                std::array<PlayerState, pixel_twins::kControllerCount>& players,
                const world::WorldMap& map, std::uint32_t elapsedTicks,
                std::uint32_t& randomState,
                const BalanceProfile& balance) noexcept {
    const auto seconds = elapsedTicks / 60U;
    std::array<EnemyKind, 5> options{{EnemyKind::Imp, EnemyKind::Bat, EnemyKind::Skeleton,
                                     EnemyKind::Wisp, EnemyKind::Golem}};
    std::size_t optionCount = seconds < 35U ? 1U : (seconds < 95U ? 2U : (seconds < 180U ? 3U : 5U));
    const auto kind = options[static_cast<std::size_t>(
        randomUnit(randomState) * static_cast<float>(optionCount)) % optionCount];
    const auto focusIndex = randomUnit(randomState) < 0.5F ? 0U : 1U;
    auto* focus = &players[players[focusIndex].hp > 0 ? focusIndex : 1U - focusIndex];
    const auto count = kind == EnemyKind::Golem ? 2U : (kind == EnemyKind::Skeleton ? 3U
        : (kind == EnemyKind::Bat || kind == EnemyKind::Wisp ? 6U : 8U));
    const auto baseAngle = randomUnit(randomState) * kTau;
    const auto range = kEnemySwarmMinimumRange
        + randomUnit(randomState) * (kEnemySwarmMaximumRange - kEnemySwarmMinimumRange);
    for (std::uint8_t index = 0; index < count; ++index) {
        const auto centeredIndex = static_cast<float>(index)
            - static_cast<float>(count - 1U) * 0.5F;
        const auto angle = baseAngle + centeredIndex * 0.18F;
        const auto offset = centeredIndex * 12.0F;
        const auto x = std::clamp(focus->x + std::cos(angle) * range - std::sin(angle) * offset,
                                  20.0F, kMapPixelWidth - 20.0F);
        const auto y = std::clamp(focus->y + std::sin(angle) * range + std::cos(angle) * offset,
                                  20.0F, kMapPixelHeight - 20.0F);
        if (!circlePositionIsWalkable(map, x, y, 8.0F)) continue;
        const auto slot = std::find_if(enemies.begin(), enemies.end(),
            [](const EnemyState& enemy) { return !enemy.active && enemy.deathTicks == 0; });
        if (slot == enemies.end()) return;
        *slot = makeEnemyState(kind, x, y, elapsedTicks, randomState, true, balance);
        slot->spawnDelayTicks = static_cast<std::uint16_t>(index * 2U);
    }
}

} // namespace

bool playerPositionIsWalkable(const world::WorldMap& map, float x, float y) noexcept {
    if (x < kPlayerCollisionRadius || y < kPlayerCollisionRadius
        || x >= kMapPixelWidth - kPlayerCollisionRadius
        || y >= kMapPixelHeight - kPlayerCollisionRadius) {
        return false;
    }
    return map.circleIsWalkable(x, y, kPlayerCollisionRadius);
}

void GameplayState::pushSfx(SfxId id, float pan,
                            float pitchScale, float volumeScale) noexcept {
    if (sfxCueCount_ >= sfxCues_.size()) return;
    sfxCues_[sfxCueCount_++] = {id, pan, pitchScale, volumeScale};
}

void GameplayState::reset(const world::WorldMap& map, std::size_t startingPlayer,
                          Difficulty difficulty) noexcept {
    players_[0] = {kWorldCenter - 28.0F, kWorldCenter, Facing::East};
    players_[1] = {kWorldCenter + 28.0F, kWorldCenter, Facing::West};
    players_[0].aiDirectionX = 1.0F;
    players_[0].aiDirectionY = 0.0F;
    players_[0].aiDesiredDirectionX = 1.0F;
    players_[0].aiDesiredDirectionY = 0.0F;
    players_[0].aiTurnSign = 1;
    players_[1].aiDirectionX = -1.0F;
    players_[1].aiDirectionY = 0.0F;
    players_[1].aiDesiredDirectionX = -1.0F;
    players_[1].aiDesiredDirectionY = 0.0F;
    players_[1].aiTurnSign = -1;
    difficulty_ = difficulty;
    const auto balance = balanceProfile(difficulty_);
    for (auto& player : players_) {
        player.hp = balance.startingHp;
        player.maxHp = balance.startingHp;
    }
    enemies_.fill({});
    bullets_.fill({});
    xpGems_.fill({});
    windSlashes_.fill({});
    thunderStrikes_.fill({});
    impactEffects_.fill({});
    perkEffects_.fill({});
    enemyBullets_.fill({});
    const auto gameplaySeed = map.seed ^ 0x57495aU;
    randomState_ = gameplaySeed != 0U ? gameplaySeed : 0x57495aU;
    spawnCooldownTicks_ = 1;
    swarmCooldownTicks_ = static_cast<std::uint16_t>(
        28U * 60U * balance.spawnIntervalPercent / 100U);
    elapsedTicks_ = 0;
    scores_.fill(0);
    killCounts_.fill(0);
    xpEarned_.fill(0);
    manualPlayers_ = {{false, false}};
    manualPlayers_[std::min(startingPlayer, manualPlayers_.size() - 1U)] = true;
    seals_.fill({});
    sealNoticeTicks_ = 0;
    activeSealCount_ = 0;
    bossSpawnPendingTicks_ = 0;
    bossIntroTicks_ = 0;
    bossSpawned_ = false;
    attractMode_ = false;
    clearSequenceTicks_ = 0;
    clearX_ = 0.0F;
    clearY_ = 0.0F;
    clearFacing_ = Facing::South;
    outcome_ = GameplayOutcome::Running;
    sfxCueCount_ = 0;
    for (std::size_t index = 0; index < cameras_.size(); ++index) {
        updateCamera(cameras_[index], players_[index]);
    }
}

void GameplayState::resetAttract(const world::WorldMap& map,
                                 Difficulty difficulty) noexcept {
    reset(map, 0, difficulty);
    attractMode_ = true;
    manualPlayers_.fill(false);
    elapsedTicks_ = 105U * 60U;
    spawnCooldownTicks_ = 0;
    swarmCooldownTicks_ = 5U * 60U;
    for (auto& player : players_) {
        player.level = 8;
        player.maxHp = std::max<std::int16_t>(player.maxHp, 50);
        player.hp = player.maxHp;
        player.lightLevel = 2;
        player.fireLevel = 2;
        player.windLevel = 2;
        player.thunderLevel = 1;
        player.iceLevel = 1;
        player.orbLevel = 1;
        player.familiarLevel = 1;
        player.speedLevel = 1;
    }
}

void GameplayState::tick(const pixel_twins::Controllers& controllers,
                         const world::WorldMap& map) noexcept {
    sfxCueCount_ = 0;
    if (outcome_ != GameplayOutcome::Running) return;
    const auto balance = balanceProfile(difficulty_);
    updateImpacts(impactEffects_);
    if (clearSequenceTicks_ > 0) {
        ++clearSequenceTicks_;
        constexpr std::array<std::uint16_t, 7> kGatherTicks{{22, 42, 60, 77, 92, 106, 119}};
        if (std::find(kGatherTicks.begin(), kGatherTicks.end(), clearSequenceTicks_)
            != kGatherTicks.end()) {
            pushSfx(SfxId::BossGather, 0.0F,
                    1.0F + static_cast<float>(clearSequenceTicks_) / 180.0F, 0.9F);
        }
        if (clearSequenceTicks_ == 141U) {
            pushSfx(SfxId::BossDeathImpact);
            pushSfx(SfxId::BossDeathBlast);
        }
        constexpr std::array<std::uint16_t, 7> kDebrisTicks{{146, 150, 154, 160, 166, 173, 182}};
        if (std::find(kDebrisTicks.begin(), kDebrisTicks.end(), clearSequenceTicks_)
            != kDebrisTicks.end()) pushSfx(SfxId::BossRock, 0.0F, 0.8F, 1.2F);
        if (clearSequenceTicks_ == 246U) {
            for (auto& player : players_) {
                player.facing = Facing::South;
                player.moving = false;
            }
        }
        updateClearCameras(cameras_, players_, clearX_, clearY_, clearSequenceTicks_);
        if (clearSequenceTicks_ >= 309U) {
            outcome_ = GameplayOutcome::Clear;
            pushSfx(SfxId::Clear);
        }
        return;
    }
    if (bossIntroTicks_ > 0) {
        --bossIntroTicks_;
        const auto elapsed = static_cast<std::uint16_t>(183U - bossIntroTicks_);
        if (elapsed == 75U) pushSfx(SfxId::BossImpact);
        constexpr std::array<std::uint16_t, 5> kRockTicks{{82, 86, 91, 97, 104}};
        if (std::find(kRockTicks.begin(), kRockTicks.end(), elapsed) != kRockTicks.end()) {
            pushSfx(SfxId::BossRock, (elapsed & 1U) != 0U ? -0.35F : 0.35F,
                    0.65F + static_cast<float>(elapsed % 11U) * 0.035F);
        }
        if (const auto* introBoss = boss()) {
            updateBossIntroCameras(cameras_, players_, *introBoss,
                                   elapsed);
        }
        return;
    }
    const auto playersBefore = players_;
    const auto manualBefore = manualPlayers_;
    std::array<bool, kMaximumEnemies> enemyActiveBefore{};
    std::array<std::int16_t, kMaximumEnemies> enemyHpBefore{};
    std::array<std::uint16_t, kMaximumEnemies> enemySpawnDelayBefore{};
    std::array<std::uint16_t, kMaximumEnemies> enemyDashBefore{};
    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        enemyActiveBefore[index] = enemies_[index].active;
        enemyHpBefore[index] = enemies_[index].hp;
        enemySpawnDelayBefore[index] = enemies_[index].spawnDelayTicks;
        enemyDashBefore[index] = enemies_[index].dashTicks;
    }
    std::array<bool, kMaximumEnemyBullets> enemyBulletBefore{};
    std::array<std::uint16_t, kMaximumEnemyBullets> enemyBulletDelayBefore{};
    std::array<std::uint16_t, kMaximumEnemyBullets> enemyBulletLifeBefore{};
    for (std::size_t index = 0; index < enemyBullets_.size(); ++index) {
        enemyBulletBefore[index] = enemyBullets_[index].active;
        enemyBulletDelayBefore[index] = enemyBullets_[index].launchDelayTicks;
        enemyBulletLifeBefore[index] = enemyBullets_[index].remainingTicks;
    }
    std::array<bool, kMaximumXpGems> xpGemBefore{};
    for (std::size_t index = 0; index < xpGems_.size(); ++index) {
        xpGemBefore[index] = xpGems_[index].active;
    }
    const auto sealCountBefore = activeSealCount_;
    const auto clearBefore = clearSequenceTicks_;
    ++elapsedTicks_;
    if (elapsedTicks_ >= 300U * 60U) {
        outcome_ = GameplayOutcome::TimeUp;
        pushSfx(SfxId::GameOver);
        return;
    }
    updatePerkEffects(perkEffects_);
    for (std::size_t index = 0; index < players_.size(); ++index) {
        if (!attractMode_ && !manualPlayers_[index] && (std::abs(controllers[index].x) >= kAxisDeadzone
            || std::abs(controllers[index].y) >= kAxisDeadzone
            || controllers[index].held != 0 || controllers[index].pressed != 0)) {
            // AI操作中に得た内部スコアを途中参加プレイヤーへ引き継がない。
            scores_[index] = 0;
            killCounts_[index] = 0;
            xpEarned_[index] = 0;
            manualPlayers_[index] = true;
        }
        if (players_[index].hp > 0) {
            if (manualPlayers_[index]) {
                updatePerkChoice(players_[index], controllers[index],
                                 static_cast<std::uint8_t>(index), randomState_, perkEffects_);
                movePlayer(players_[index], controllers[index], map);
            } else if (attractMode_) {
                updateAutoPerkChoice(players_[index], static_cast<std::uint8_t>(index),
                                     enemies_, randomState_, perkEffects_);
                moveAttractPlayer(players_[index], index, players_[1U - index], enemies_,
                                  enemyBullets_, xpGems_, map, elapsedTicks_ - 105U * 60U);
            } else {
                updateAutoPerkChoice(players_[index], static_cast<std::uint8_t>(index),
                                     enemies_, randomState_, perkEffects_);
                followAiPartner(players_[index], players_[1U - index],
                                enemies_, enemyBullets_, map);
            }
        }
        else players_[index].moving = false;
        if (players_[index].invulnerabilityTicks > 0) --players_[index].invulnerabilityTicks;
        if (players_[index].lightCooldownTicks > 0) --players_[index].lightCooldownTicks;
        if (players_[index].fireCooldownTicks > 0) --players_[index].fireCooldownTicks;
        if (players_[index].windCooldownTicks > 0) --players_[index].windCooldownTicks;
        if (players_[index].thunderCooldownTicks > 0) --players_[index].thunderCooldownTicks;
        if (players_[index].iceCooldownTicks > 0) --players_[index].iceCooldownTicks;
        if (players_[index].orbCooldownTicks > 0) --players_[index].orbCooldownTicks;
        if (players_[index].familiarCooldownTicks > 0) --players_[index].familiarCooldownTicks;
        if (players_[index].hp > 0) {
            players_[index].orbAngle -= (8.1F + static_cast<float>(players_[index].orbLevel) * 0.72F) / 60.0F;
            if (players_[index].hp < players_[index].maxHp) {
                players_[index].hpRegenAccumulator += 0.225F / 60.0F;
                if (players_[index].hpRegenAccumulator >= 1.0F) {
                    ++players_[index].hp;
                    players_[index].hpRegenAccumulator -= 1.0F;
                }
            } else {
                players_[index].hpRegenAccumulator = 0.0F;
            }
        }
        updateCamera(cameras_[index], players_[index]);
    }
    processLinkShares(players_, perkEffects_, randomState_);
    if (sealNoticeTicks_ > 0) --sealNoticeTicks_;
    updateSealStones(players_, map, seals_, scores_, elapsedTicks_, activeSealCount_, sealNoticeTicks_,
                     bossSpawnPendingTicks_);
    if (!bossSpawned_ && bossSpawnPendingTicks_ > 0) {
        --bossSpawnPendingTicks_;
        if (bossSpawnPendingTicks_ == 0) {
            bossSpawned_ = spawnBoss(enemies_, elapsedTicks_, randomState_, balance);
            if (bossSpawned_) {
                bossIntroTicks_ = 183;
                sealNoticeTicks_ = 0;
                return;
            }
        }
    }
    if (spawnCooldownTicks_ > 0) --spawnCooldownTicks_;
    if (swarmCooldownTicks_ > 0) --swarmCooldownTicks_;
    const auto enemyProgressTicks = std::max(
        elapsedTicks_, static_cast<std::uint32_t>(activeSealCount_) * 60U * 60U);
    const auto* activeBoss = boss();
    if (activeBoss == nullptr && swarmCooldownTicks_ == 0 && enemyCount() < kMaximumEnemies - 8U) {
        spawnSwarm(enemies_, players_, map, enemyProgressTicks, randomState_, balance);
        swarmCooldownTicks_ = static_cast<std::uint16_t>(
            std::round((34.0F + randomUnit(randomState_) * 24.0F) * 60.0F
                       * balance.spawnIntervalPercent / 100.0F));
    }
    if (spawnCooldownTicks_ == 0) {
        const auto requestedCount = static_cast<std::uint8_t>(
            2U + enemyProgressTicks / (45U * 60U));
        recycleDistantEnemiesForSpawn(enemies_, players_, requestedCount);
        const auto focusIndex = randomUnit(randomState_) < 0.5F ? 0U : 1U;
        const auto& focus = players_[players_[focusIndex].hp > 0 ? focusIndex : 1U - focusIndex];
        for (std::uint8_t index = 0; index < requestedCount; ++index) {
            (void)spawnEnemyNear(enemies_, focus, map,
                                 rollEnemyKind(enemyProgressTicks, randomState_),
                                 enemyProgressTicks, randomState_,
                                 activeBoss, balance);
        }
        const auto intervalSeconds = std::max(0.38F,
            1.25F - static_cast<float>(enemyProgressTicks) / 60.0F / 420.0F);
        spawnCooldownTicks_ = static_cast<std::uint16_t>(std::round(
            intervalSeconds * (activeBoss != nullptr ? 3.0F : 1.0F) * 60.0F
                * balance.spawnIntervalPercent / 100.0F));
    }
    moveEnemies(enemies_, players_, enemyBullets_, map, randomState_, balance);
    for (std::size_t index = 0; index < players_.size(); ++index) {
        auto& player = players_[index];
        if (player.hp > 0) {
            updateFamiliars(player, static_cast<std::uint8_t>(index), enemies_, bullets_);
        }
        if (player.hp > 0 && player.lightCooldownTicks == 0
            && fireLight(player, static_cast<std::uint8_t>(index), enemies_, bullets_,
                         impactEffects_)) {
            player.lightCooldownTicks = cooldownTicks(0.52F, 0.035F, player.lightLevel, 0.18F);
        }
        if (player.hp > 0 && player.fireLevel > 0 && player.fireCooldownTicks == 0) {
            const auto stats = attackStats(player.fireLevel, 1, 14, kFireSpeedPerTick, 5, 28.0F / 60.0F);
            if (fireSpread(player, static_cast<std::uint8_t>(index), PlayerAttack::Fire,
                           stats, 0.16F, 54, 3.0F, bullets_, impactEffects_)) {
                player.fireCooldownTicks = cooldownTicks(0.72F, 0.04F, player.fireLevel, 0.24F);
            }
        }
        if (player.hp > 0 && player.windLevel > 0 && player.windCooldownTicks == 0) {
            spawnWindSlash(player, static_cast<std::uint8_t>(index), windSlashes_);
            player.windCooldownTicks = cooldownTicks(2.4F, 0.12F, player.windLevel, 1.25F);
        }
        if (player.hp > 0 && player.thunderLevel > 0 && player.thunderCooldownTicks == 0) {
            spawnThunder(player, static_cast<std::uint8_t>(index), randomState_, thunderStrikes_,
                         enemies_, xpGems_, scores_, killCounts_);
            player.thunderCooldownTicks = cooldownTicks(1.05F, 0.045F, player.thunderLevel, 0.45F);
        }
        if (player.hp > 0 && player.iceLevel > 0 && player.iceCooldownTicks == 0) {
            const auto stats = attackStats(player.iceLevel, 1, 5, 130.0F / 60.0F, 2, 18.0F / 60.0F);
            bool fired = false;
            for (std::uint8_t shot = 0; shot < stats.count; ++shot) {
                randomState_ = randomState_ * 1664525U + 1013904223U;
                const auto angle = static_cast<float>(randomState_ & 0xffffU) * kTau / 65536.0F;
                fired = spawnBullet(bullets_, player, static_cast<std::uint8_t>(index),
                                    PlayerAttack::Ice, angle, stats.damage, stats.speed,
                                    63, 3.0F) || fired;
            }
            if (fired) player.iceCooldownTicks = cooldownTicks(1.25F, 0.05F, player.iceLevel, 0.55F);
        }
        if (player.hp > 0 && player.orbLevel > 0 && player.orbCooldownTicks == 0) {
            damageOrbs(player, static_cast<std::uint8_t>(index), enemies_, enemyBullets_,
                       xpGems_, scores_, killCounts_, impactEffects_);
            player.orbCooldownTicks = 10;
        }
    }
    processBombs(players_, enemies_, enemyBullets_, xpGems_, scores_, killCounts_, randomState_);
    updateBullets(bullets_, enemies_, enemyBullets_, xpGems_, scores_, killCounts_, impactEffects_);
    updateWindSlashes(windSlashes_, players_, enemies_, enemyBullets_, xpGems_, scores_,
                      killCounts_, impactEffects_);
    updateThunderStrikes(thunderStrikes_);
    updateEnemyBullets(enemyBullets_, players_, balance);
    const auto xpRecallStarted = updateXpRecallCircle(xpGems_, players_);
    updateXpGems(xpGems_, players_, xpEarned_, randomState_, perkEffects_, difficulty_);
    outcome_ = updateRevives(players_);
    for (auto& enemy : enemies_) {
        if (!enemy.active && enemy.deathTicks > 0) --enemy.deathTicks;
    }
    if (bossSpawned_ && boss() == nullptr && clearSequenceTicks_ == 0) {
        const auto defeated = std::find_if(enemies_.begin(), enemies_.end(), [](const EnemyState& enemy) {
            return enemy.kind == EnemyKind::Boss;
        });
        clearX_ = defeated != enemies_.end() ? defeated->x : kWorldCenter;
        clearY_ = defeated != enemies_.end() ? defeated->y : kWorldCenter;
        clearFacing_ = defeated != enemies_.end() ? defeated->facing : Facing::South;
        enemies_.fill({});
        bullets_.fill({});
        xpGems_.fill({});
        windSlashes_.fill({});
        thunderStrikes_.fill({});
        impactEffects_.fill({});
        perkEffects_.fill({});
        enemyBullets_.fill({});
        for (auto& player : players_) {
            if (player.hp <= 0) {
                player.hp = static_cast<std::int16_t>(std::max<std::int32_t>(2,
                    (static_cast<std::int32_t>(player.maxHp) * 35 + 99) / 100));
                player.invulnerabilityTicks = 120;
            }
        }
        outcome_ = GameplayOutcome::Running;
        clearSequenceTicks_ = 1;
    }

    constexpr std::array<float, pixel_twins::kControllerCount> kPlayerPans{{-0.32F, 0.32F}};
    bool playerDamaged = false;
    for (std::size_t player = 0; player < players_.size(); ++player) {
        const auto& before = playersBefore[player];
        const auto& after = players_[player];
        const auto pan = kPlayerPans[player];
        if (after.lightCooldownTicks > before.lightCooldownTicks) pushSfx(SfxId::LightCast, pan);
        if (after.fireCooldownTicks > before.fireCooldownTicks) pushSfx(SfxId::FireCast, pan);
        if (after.windCooldownTicks > before.windCooldownTicks) pushSfx(SfxId::WindCast, pan);
        if (after.thunderCooldownTicks > before.thunderCooldownTicks) pushSfx(SfxId::ThunderCast, pan);
        if (after.iceCooldownTicks > before.iceCooldownTicks) pushSfx(SfxId::IceCast, pan);
        if (after.familiarCooldownTicks > before.familiarCooldownTicks) pushSfx(SfxId::FamiliarCast, pan);
        if (before.hp > 0 && after.hp <= 0) {
            playerDamaged = true;
            pushSfx(SfxId::Down, pan);
        } else if (after.hp < before.hp) {
            playerDamaged = true;
            pushSfx(SfxId::PlayerDamage, pan);
        }
        else if (before.hp <= 0 && after.hp > 0) pushSfx(SfxId::Revive, pan);
        if (after.level > before.level) pushSfx(SfxId::Level, pan);
        if (after.perkFlashTicks > before.perkFlashTicks) {
            pushSfx(SfxId::UiMove, pan, 1.0F, 0.6F);
            if (after.perkFlash == Perk::Heal) pushSfx(SfxId::Heal, pan);
            else if (after.perkFlash == Perk::MaxHp) pushSfx(SfxId::HpUp, pan);
        }
        if (before.bombEffectTicks == 0 && after.bombEffectTicks > 0) pushSfx(SfxId::Bomb, pan);
        if (!manualBefore[player] && manualPlayers_[player]) pushSfx(SfxId::UiMove, pan);
    }
    bool hitPlayed = false;
    bool killPlayed = false;
    bool spawnPlayed = false;
    bool enemyShoot = false;
    for (std::size_t index = 0; index < enemies_.size(); ++index) {
        const auto& enemy = enemies_[index];
        if (!enemyActiveBefore[index] && enemy.active && enemy.bornTicks > 0
            && enemy.spawnDelayTicks == 0) spawnPlayed = true;
        if (enemyActiveBefore[index] && enemySpawnDelayBefore[index] > 0
            && enemy.spawnDelayTicks == 0) spawnPlayed = true;
        if (enemyActiveBefore[index] && !enemy.active && enemy.deathTicks > 0
            && enemy.kind != EnemyKind::Boss) {
            killPlayed = true;
            hitPlayed = true;
            spawnImpact(impactEffects_, ImpactEffectType::Generic, enemy.x, enemy.y);
        }
        if (enemyActiveBefore[index] && enemy.active && enemy.hp < enemyHpBefore[index]) hitPlayed = true;
        if (enemy.kind == EnemyKind::Bat && enemyDashBefore[index] == 0 && enemy.dashTicks > 0) {
            enemyShoot = true;
        }
    }
    if (spawnPlayed) pushSfx(SfxId::EnemySpawn);
    if (hitPlayed) pushSfx(SfxId::Hit);
    if (killPlayed) pushSfx(SfxId::Kill);
    bool bossShoot = false;
    bool deflected = false;
    for (std::size_t index = 0; index < enemyBullets_.size(); ++index) {
        const auto& bullet = enemyBullets_[index];
        const auto launched = (!enemyBulletBefore[index] && bullet.active && bullet.launchDelayTicks == 0)
            || (enemyBulletBefore[index] && enemyBulletDelayBefore[index] > 0
                && bullet.active && bullet.launchDelayTicks == 0);
        if (launched) {
            if (bullet.type == EnemyBulletType::BossFire) bossShoot = true;
            else enemyShoot = true;
        }
        if (enemyBulletBefore[index] && !bullet.active && enemyBulletLifeBefore[index] > 1U) {
            deflected = true;
        }
    }
    if (enemyShoot) pushSfx(SfxId::EnemyShoot);
    if (bossShoot) pushSfx(SfxId::BossShoot);
    if (deflected && !playerDamaged) pushSfx(SfxId::Deflect);
    bool xpCollected = false;
    for (std::size_t index = 0; index < xpGems_.size(); ++index) {
        xpCollected = xpCollected || (xpGemBefore[index] && !xpGems_[index].active);
    }
    if (xpCollected || xpRecallStarted) pushSfx(SfxId::Xp);
    if (activeSealCount_ > sealCountBefore) pushSfx(SfxId::SealJingle);
    for (const auto& seal : seals_) {
        if (!seal.active || elapsedTicks_ < seal.activatedAtTicks) continue;
        const auto age = elapsedTicks_ - seal.activatedAtTicks;
        const auto stage = static_cast<std::uint8_t>(std::count_if(
            seals_.begin(), seals_.end(), [&](const SealState& candidate) {
                return candidate.active && candidate.activatedAtTicks <= seal.activatedAtTicks;
            }));
        if ((stage == 1U && age == 9U) || (stage == 2U && age == 13U)
            || (stage == 3U && age == 12U)) pushSfx(SfxId::SealJingle, 0.0F, 988.0F / 659.0F);
        if ((stage == 2U && age == 7U) || (stage == 3U && age == 6U)) {
            pushSfx(SfxId::SealJingle, 0.0F, 784.0F / 659.0F);
        }
        if ((stage == 2U && age == 22U) || (stage == 3U && age == 20U)) {
            pushSfx(SfxId::SealJingle, 0.0F, 1175.0F / 659.0F);
        }
        if (stage == 3U && age == 30U) pushSfx(SfxId::SealJingle, 0.0F, 1568.0F / 659.0F);
    }
    if (clearBefore == 0 && clearSequenceTicks_ > 0) pushSfx(SfxId::BossGather);
    if (outcome_ == GameplayOutcome::Down) pushSfx(SfxId::GameOver);
}

void GameplayState::grantXp(std::size_t playerIndex, std::uint16_t amount) noexcept {
    if (playerIndex < players_.size()) {
        xpEarned_[playerIndex] += amount;
        gainXp(players_[playerIndex], static_cast<std::uint8_t>(playerIndex), amount,
               randomState_, perkEffects_, difficulty_);
    }
}

bool GameplayState::addEnemy(float x, float y, EnemyKind kind) noexcept {
    const auto slot = std::find_if(enemies_.begin(), enemies_.end(), [](const EnemyState& enemy) {
        return !enemy.active && enemy.deathTicks == 0;
    });
    if (slot == enemies_.end()) return false;
    *slot = makeEnemyState(kind, x, y, elapsedTicks_, randomState_, false,
                           balanceProfile(difficulty_));
    return true;
}

std::size_t GameplayState::enemyCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(enemies_.begin(), enemies_.end(),
        [](const EnemyState& enemy) { return enemy.active; }));
}

std::size_t GameplayState::bulletCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(bullets_.begin(), bullets_.end(),
        [](const PlayerBulletState& bullet) { return bullet.active; }));
}

const EnemyState* GameplayState::boss() const noexcept {
    const auto result = std::find_if(enemies_.begin(), enemies_.end(), [](const EnemyState& enemy) {
        return enemy.active && enemy.kind == EnemyKind::Boss;
    });
    return result == enemies_.end() ? nullptr : &*result;
}

std::uint16_t xpNeededForLevel(std::uint8_t level, Difficulty difficulty) noexcept {
    std::uint64_t numerator = 15;
    std::uint64_t denominator = 1;
    for (std::uint8_t current = 1; current < level && current < 12; ++current) {
        numerator *= 14U;
        denominator *= 10U;
    }
    const auto rounded = (numerator + denominator / 2U) / denominator;
    const auto scaled = (rounded * balanceProfile(difficulty).xpNeedPercent + 50U) / 100U;
    return static_cast<std::uint16_t>(std::clamp<std::uint64_t>(scaled, 1U, 65535U));
}

std::uint16_t GameplayState::xpNeeded(std::uint8_t level) const noexcept {
    return xpNeededForLevel(level, difficulty_);
}

std::uint8_t directionRow8(float x, float y) noexcept {
    constexpr std::array<std::uint8_t, 8> kRows{{4, 1, 2, 3, 7, 5, 6, 0}};
    auto angle = std::atan2(y, x);
    if (angle < 0.0F) angle += kTau;
    const auto sector = static_cast<std::uint8_t>(
        static_cast<unsigned>(std::round(angle / (kTau / 8.0F))) % 8U);
    return kRows[sector];
}

std::uint16_t thunderShockwaveRadius(std::uint16_t ageTicks) noexcept {
    constexpr float kStrikeTicks = 20.0F;
    const auto progress = std::min(1.0F, static_cast<float>(ageTicks) / kStrikeTicks);
    return static_cast<std::uint16_t>(std::round(24.0F * (0.35F + progress * 0.9F)));
}

} // namespace wizward::game
