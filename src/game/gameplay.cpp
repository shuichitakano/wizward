#include "game/gameplay.hpp"

#include "pixel_twins/framebuffer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace wizward::game {
namespace {

constexpr float kPlayerSpeedPerTick = 1.0F;
constexpr float kSpeedPerLevelPerTick = 10.0F / 60.0F;
constexpr float kEnemySpeedPerTick = 26.0F / 60.0F;
constexpr float kLightSpeedPerTick = 145.0F / 60.0F;
constexpr float kFireSpeedPerTick = 205.0F / 60.0F;
constexpr float kLightSeekRange = 170.0F;
constexpr float kLightHomingRange = 95.0F;
constexpr float kXpPullRange = 52.0F;
constexpr float kXpCollectRange = 9.0F;
constexpr float kXpPullSpeedPerTick = 2.0F;
constexpr std::int16_t kAxisDeadzone = 4096;
constexpr std::int16_t kSlimeHitPoints = 10;
constexpr std::int16_t kLightDamage = 6;
constexpr std::int16_t kContactDamage = 3;
constexpr std::uint16_t kContactInvulnerabilityTicks = 39;
constexpr std::uint16_t kLightLifetimeTicks = 75;
constexpr std::uint16_t kSpawnIntervalTicks = 75;
constexpr float kCameraVerticalOffset = 16.0F;
constexpr float kMapPixelWidth = static_cast<float>(world::kMapColumns * kWorldTileSize);
constexpr float kMapPixelHeight = static_cast<float>(world::kMapRows * kWorldTileSize);
constexpr float kTau = 6.2831853F;

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

std::uint16_t tileCoordinate(float value) noexcept {
    if (value <= 0.0F) return 0;
    return static_cast<std::uint16_t>(value / static_cast<float>(kWorldTileSize));
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
    const auto speed = kPlayerSpeedPerTick + static_cast<float>(player.speedLevel) * kSpeedPerLevelPerTick;
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

EnemyState* nearestEnemy(std::array<EnemyState, kMaximumEnemies>& enemies,
                         float x, float y, float range) noexcept {
    EnemyState* nearest = nullptr;
    auto nearestSquared = range * range;
    for (auto& enemy : enemies) {
        if (!enemy.active) continue;
        const auto candidate = squaredDistance(x, y, enemy.x, enemy.y);
        if (candidate <= nearestSquared) {
            nearestSquared = candidate;
            nearest = &enemy;
        }
    }
    return nearest;
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

void killEnemy(EnemyState& enemy,
               std::uint8_t owner,
               std::array<XpGemState, kMaximumXpGems>& xpGems,
               std::array<std::uint32_t, pixel_twins::kControllerCount>& scores) noexcept {
    const auto gem = std::find_if(xpGems.begin(), xpGems.end(),
        [](const XpGemState& candidate) { return !candidate.active; });
    if (gem != xpGems.end()) *gem = {enemy.x, enemy.y, 2, true};
    if (owner < scores.size()) scores[owner] += 10U;
    enemy.active = false;
}

void moveEnemies(std::array<EnemyState, kMaximumEnemies>& enemies,
                 std::array<PlayerState, pixel_twins::kControllerCount>& players,
                 const world::WorldMap& map) noexcept {
    for (auto& enemy : enemies) {
        if (!enemy.active) continue;
        const auto speed = enemy.slowTicks > 0 ? kEnemySpeedPerTick * 0.55F : kEnemySpeedPerTick;
        if (enemy.slowTicks > 0) --enemy.slowTicks;
        PlayerState* target = &players[0];
        if (players[1].hp > 0
            && (players[0].hp <= 0 || squaredDistance(enemy.x, enemy.y, players[1].x, players[1].y)
                                      < squaredDistance(enemy.x, enemy.y, players[0].x, players[0].y))) {
            target = &players[1];
        }
        auto dx = target->x - enemy.x;
        auto dy = target->y - enemy.y;
        normalize(dx, dy);
        enemy.facing = facingFor(dx, dy);
        const auto nextX = enemy.x + dx * speed;
        if (playerPositionIsWalkable(map, nextX, enemy.y)) enemy.x = nextX;
        const auto nextY = enemy.y + dy * speed;
        if (playerPositionIsWalkable(map, enemy.x, nextY)) enemy.y = nextY;

        for (auto& player : players) {
            if (player.hp <= 0 || player.invulnerabilityTicks != 0) continue;
            const auto contactRange = enemy.radius + kPlayerRadius;
            if (squaredDistance(enemy.x, enemy.y, player.x, player.y) < contactRange * contactRange) {
                player.hp = static_cast<std::int16_t>(std::max(0, player.hp - kContactDamage));
                player.invulnerabilityTicks = kContactInvulnerabilityTicks;
            }
        }
    }
}

bool fireLight(PlayerState& player,
               std::uint8_t owner,
               std::array<EnemyState, kMaximumEnemies>& enemies,
               std::array<PlayerBulletState, kMaximumPlayerBullets>& bullets) noexcept {
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
    return fired;
}

bool fireSpread(PlayerState& player,
                std::uint8_t owner,
                PlayerAttack type,
                const AttackStats& stats,
                float spread,
                std::uint16_t lifetimeTicks,
                float radius,
                std::array<PlayerBulletState, kMaximumPlayerBullets>& bullets) noexcept {
    const auto baseAngle = facingAngle(player.facing);
    bool fired = false;
    for (std::uint8_t index = 0; index < stats.count; ++index) {
        const auto offset = stats.count == 1 ? 0.0F
            : (static_cast<float>(index) - static_cast<float>(stats.count - 1U) * 0.5F) * spread;
        fired = spawnBullet(bullets, player, owner, type, baseAngle + offset,
                            stats.damage, stats.speed, lifetimeTicks, radius) || fired;
    }
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
                  std::array<std::uint32_t, pixel_twins::kControllerCount>& scores) noexcept {
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
            if (enemy.hp <= 0) killEnemy(enemy, owner, xpGems, scores);
        }
    }
}

void damageOrbs(PlayerState& player, std::uint8_t owner,
                std::array<EnemyState, kMaximumEnemies>& enemies,
                std::array<XpGemState, kMaximumXpGems>& xpGems,
                std::array<std::uint32_t, pixel_twins::kControllerCount>& scores) noexcept {
    const auto stats = attackStats(player.orbLevel, 1, 4, 22.0F, 2, 4.0F);
    for (std::uint8_t orbIndex = 0; orbIndex < stats.count; ++orbIndex) {
        const auto angle = player.orbAngle + static_cast<float>(orbIndex) * kTau / stats.count;
        const auto x = player.x + std::cos(angle) * stats.speed;
        const auto y = player.y + std::sin(angle) * stats.speed;
        for (auto& enemy : enemies) {
            if (!enemy.active) continue;
            const auto range = enemy.radius + 4.0F;
            if (squaredDistance(x, y, enemy.x, enemy.y) >= range * range) continue;
            enemy.hp = static_cast<std::int16_t>(enemy.hp - stats.damage);
            if (enemy.hp <= 0) killEnemy(enemy, owner, xpGems, scores);
        }
    }
}

void updateBullets(std::array<PlayerBulletState, kMaximumPlayerBullets>& bullets,
                   std::array<EnemyState, kMaximumEnemies>& enemies,
                   std::array<XpGemState, kMaximumXpGems>& xpGems,
                   std::array<std::uint32_t, pixel_twins::kControllerCount>& scores) noexcept {
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
                if (enemy.hp <= 0) {
                    killEnemy(enemy, bullet.owner, xpGems, scores);
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
                       std::array<XpGemState, kMaximumXpGems>& xpGems,
                       std::array<std::uint32_t, pixel_twins::kControllerCount>& scores) noexcept {
    for (auto& slash : slashes) {
        if (!slash.active) continue;
        ++slash.ageTicks;
        if (slash.remainingTicks > 0) --slash.remainingTicks;
        if (slash.remainingTicks == 0 || slash.owner >= players.size()) {
            slash.active = false;
            continue;
        }
        const auto& owner = players[slash.owner];
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
            if (enemy.hp <= 0) killEnemy(enemy, slash.owner, xpGems, scores);
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
    case Perk::MaxHp: return static_cast<std::uint8_t>((player.maxHp - 30) / 5);
    case Perk::Heal:
    case Perk::Bomb: return 0;
    }
    return 0;
}

void rollPerks(PlayerState& player, std::uint32_t& randomState) noexcept {
    // FAMILIAR and BOMB join this pool with their gameplay implementations.
    constexpr std::array<Perk, 9> kPool{{
        Perk::Light, Perk::Fire, Perk::Wind, Perk::Thunder, Perk::Ice,
        Perk::Orb, Perk::Speed, Perk::MaxHp, Perk::Heal,
    }};
    constexpr std::array<std::uint8_t, 9> kWeights{{5, 5, 4, 4, 4, 4, 3, 3, 4}};
    std::array<bool, kPool.size()> chosen{};
    for (std::size_t slot = 0; slot < player.perkChoices.size(); ++slot) {
        unsigned total = 0;
        for (std::size_t index = 0; index < kPool.size(); ++index) {
            const auto capped = kPool[index] != Perk::Heal && perkLevel(player, kPool[index]) >= 5;
            if (!chosen[index] && !capped) total += kWeights[index];
        }
        if (total == 0) {
            player.perkChoices[slot] = Perk::Heal;
            continue;
        }
        randomState = randomState * 1664525U + 1013904223U;
        auto pick = static_cast<unsigned>((randomState >> 16U) % total);
        for (std::size_t index = 0; index < kPool.size(); ++index) {
            const auto capped = kPool[index] != Perk::Heal && perkLevel(player, kPool[index]) >= 5;
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

void gainXp(PlayerState& player, std::uint16_t amount, std::uint32_t& randomState) noexcept {
    player.xp = static_cast<std::uint16_t>(player.xp + amount);
    while (player.xp >= xpNeededForLevel(player.level)) {
        player.xp = static_cast<std::uint16_t>(player.xp - xpNeededForLevel(player.level));
        if (player.level < 255) ++player.level;
        if (player.pendingPerkChoices < 255) ++player.pendingPerkChoices;
    }
    beginPerkChoice(player, randomState);
}

void applyPerk(PlayerState& player, Perk perk) noexcept {
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
        player.maxHp = static_cast<std::int16_t>(std::min<std::int32_t>(999, player.maxHp + 5));
        player.hp = static_cast<std::int16_t>(std::min<std::int32_t>(player.maxHp, player.hp + 5));
        break;
    case Perk::Heal:
        player.hp = static_cast<std::int16_t>(std::min<std::int32_t>(player.maxHp, player.hp + 26));
        break;
    case Perk::Bomb:
        break;
    }
}

void updatePerkChoice(PlayerState& player, const pixel_twins::ControllerState& controller,
                      std::uint32_t& randomState) noexcept {
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
    applyPerk(player, player.perkChoices[choice]);
    if (player.pendingPerkChoices > 0) --player.pendingPerkChoices;
    player.choosingPerk = false;
    beginPerkChoice(player, randomState);
}

void updateXpGems(std::array<XpGemState, kMaximumXpGems>& xpGems,
                  std::array<PlayerState, pixel_twins::kControllerCount>& players,
                  std::uint32_t& randomState) noexcept {
    for (auto& gem : xpGems) {
        if (!gem.active) continue;
        PlayerState* nearest = nullptr;
        auto nearestSquared = kXpPullRange * kXpPullRange;
        for (auto& player : players) {
            if (player.hp <= 0) continue;
            const auto candidate = squaredDistance(gem.x, gem.y, player.x, player.y);
            if (candidate <= nearestSquared) {
                nearestSquared = candidate;
                nearest = &player;
            }
        }
        if (nearest == nullptr) continue;
        if (nearestSquared < kXpCollectRange * kXpCollectRange) {
            gainXp(*nearest, gem.value, randomState);
            gem.active = false;
            continue;
        }
        auto dx = nearest->x - gem.x;
        auto dy = nearest->y - gem.y;
        normalize(dx, dy);
        gem.x += dx * kXpPullSpeedPerTick;
        gem.y += dy * kXpPullSpeedPerTick;
    }
}

} // namespace

bool playerPositionIsWalkable(const world::WorldMap& map, float x, float y) noexcept {
    if (x < kPlayerRadius || y < kPlayerRadius
        || x >= kMapPixelWidth - kPlayerRadius || y >= kMapPixelHeight - kPlayerRadius) {
        return false;
    }
    constexpr std::array<std::array<float, 2>, 8> kSamples{{
        {{-kPlayerRadius, 0.0F}}, {{kPlayerRadius, 0.0F}},
        {{0.0F, -kPlayerRadius}}, {{0.0F, kPlayerRadius}},
        {{-3.54F, -3.54F}}, {{3.54F, -3.54F}},
        {{3.54F, 3.54F}}, {{-3.54F, 3.54F}},
    }};
    for (const auto& sample : kSamples) {
        if (map.collides(tileCoordinate(x + sample[0]), tileCoordinate(y + sample[1]))) return false;
    }
    return true;
}

void GameplayState::reset(const world::WorldMap&) noexcept {
    constexpr float kCenter = 50.5F * static_cast<float>(kWorldTileSize);
    players_[0] = {kCenter - 28.0F, kCenter, Facing::East};
    players_[1] = {kCenter + 28.0F, kCenter, Facing::West};
    enemies_.fill({});
    bullets_.fill({});
    xpGems_.fill({});
    windSlashes_.fill({});
    thunderStrikes_.fill({});
    randomState_ = 0x57495aU;
    spawnCooldownTicks_ = 1;
    elapsedTicks_ = 0;
    scores_.fill(0);
    for (std::size_t index = 0; index < cameras_.size(); ++index) {
        updateCamera(cameras_[index], players_[index]);
    }
}

void GameplayState::tick(const pixel_twins::Controllers& controllers,
                         const world::WorldMap& map) noexcept {
    ++elapsedTicks_;
    for (std::size_t index = 0; index < players_.size(); ++index) {
        updatePerkChoice(players_[index], controllers[index], randomState_);
        movePlayer(players_[index], controllers[index], map);
        if (players_[index].invulnerabilityTicks > 0) --players_[index].invulnerabilityTicks;
        if (players_[index].lightCooldownTicks > 0) --players_[index].lightCooldownTicks;
        if (players_[index].fireCooldownTicks > 0) --players_[index].fireCooldownTicks;
        if (players_[index].windCooldownTicks > 0) --players_[index].windCooldownTicks;
        if (players_[index].thunderCooldownTicks > 0) --players_[index].thunderCooldownTicks;
        if (players_[index].iceCooldownTicks > 0) --players_[index].iceCooldownTicks;
        if (players_[index].orbCooldownTicks > 0) --players_[index].orbCooldownTicks;
        players_[index].orbAngle -= (8.1F + static_cast<float>(players_[index].orbLevel) * 0.72F) / 60.0F;
        updateCamera(cameras_[index], players_[index]);
    }
    if (spawnCooldownTicks_ > 0) --spawnCooldownTicks_;
    if (spawnCooldownTicks_ == 0) {
        randomState_ = randomState_ * 1664525U + 1013904223U;
        const auto angle = static_cast<float>(randomState_ & 0xffffU) * 6.2831853F / 65536.0F;
        const auto& focus = players_[(randomState_ >> 16U) & 1U];
        const auto spawnX = focus.x + std::cos(angle) * 120.0F;
        const auto spawnY = focus.y + std::sin(angle) * 120.0F;
        if (playerPositionIsWalkable(map, spawnX, spawnY)) (void)addEnemy(spawnX, spawnY);
        spawnCooldownTicks_ = kSpawnIntervalTicks;
    }
    moveEnemies(enemies_, players_, map);
    for (std::size_t index = 0; index < players_.size(); ++index) {
        auto& player = players_[index];
        if (player.hp > 0 && player.lightCooldownTicks == 0
            && fireLight(player, static_cast<std::uint8_t>(index), enemies_, bullets_)) {
            player.lightCooldownTicks = cooldownTicks(0.52F, 0.035F, player.lightLevel, 0.18F);
        }
        if (player.hp > 0 && player.fireLevel > 0 && player.fireCooldownTicks == 0) {
            const auto stats = attackStats(player.fireLevel, 1, 14, kFireSpeedPerTick, 5, 28.0F / 60.0F);
            if (fireSpread(player, static_cast<std::uint8_t>(index), PlayerAttack::Fire,
                           stats, 0.16F, 54, 3.0F, bullets_)) {
                player.fireCooldownTicks = cooldownTicks(0.72F, 0.04F, player.fireLevel, 0.24F);
            }
        }
        if (player.hp > 0 && player.windLevel > 0 && player.windCooldownTicks == 0) {
            spawnWindSlash(player, static_cast<std::uint8_t>(index), windSlashes_);
            player.windCooldownTicks = cooldownTicks(2.4F, 0.12F, player.windLevel, 1.25F);
        }
        if (player.hp > 0 && player.thunderLevel > 0 && player.thunderCooldownTicks == 0) {
            spawnThunder(player, static_cast<std::uint8_t>(index), randomState_, thunderStrikes_,
                         enemies_, xpGems_, scores_);
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
            damageOrbs(player, static_cast<std::uint8_t>(index), enemies_, xpGems_, scores_);
            player.orbCooldownTicks = 10;
        }
    }
    updateBullets(bullets_, enemies_, xpGems_, scores_);
    updateWindSlashes(windSlashes_, players_, enemies_, xpGems_, scores_);
    updateThunderStrikes(thunderStrikes_);
    updateXpGems(xpGems_, players_, randomState_);
}

void GameplayState::grantXp(std::size_t playerIndex, std::uint16_t amount) noexcept {
    if (playerIndex < players_.size()) gainXp(players_[playerIndex], amount, randomState_);
}

bool GameplayState::addEnemy(float x, float y) noexcept {
    const auto slot = std::find_if(enemies_.begin(), enemies_.end(), [](const EnemyState& enemy) {
        return !enemy.active;
    });
    if (slot == enemies_.end()) return false;
    *slot = {x, y, 5.0F, kSlimeHitPoints, 0, Facing::South, true};
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

std::uint16_t xpNeededForLevel(std::uint8_t level) noexcept {
    std::uint64_t numerator = 15;
    std::uint64_t denominator = 1;
    for (std::uint8_t current = 1; current < level && current < 12; ++current) {
        numerator *= 14U;
        denominator *= 10U;
    }
    const auto rounded = (numerator + denominator / 2U) / denominator;
    return static_cast<std::uint16_t>(std::min<std::uint64_t>(rounded, 65535U));
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
