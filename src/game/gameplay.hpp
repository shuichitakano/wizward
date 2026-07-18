#pragma once

#include "world/world_map.hpp"

#include "pixel_twins/controller.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace wizward::game {

inline constexpr std::int32_t kWorldTileSize = 32;
inline constexpr float kPlayerRadius = 5.0F;
inline constexpr std::size_t kMaximumEnemies = 90;
inline constexpr std::size_t kMaximumPlayerBullets = 128;
inline constexpr std::size_t kMaximumXpGems = 128;
inline constexpr std::size_t kMaximumWindSlashes = 8;
inline constexpr std::size_t kMaximumThunderStrikes = 16;
inline constexpr std::size_t kMaximumEnemyBullets = 128;

enum class Facing : std::uint8_t {
    South,
    SouthEast,
    East,
    NorthEast,
    North,
    NorthWest,
    West,
    SouthWest,
};

enum class Perk : std::uint8_t {
    Light,
    Fire,
    Wind,
    Thunder,
    Ice,
    Orb,
    Familiar,
    Speed,
    MaxHp,
    Heal,
    Bomb,
};

enum class PlayerAttack : std::uint8_t {
    Light,
    Fire,
    Ice,
};

enum class EnemyKind : std::uint8_t {
    Imp,
    Bat,
    Skeleton,
    Golem,
    Archer,
    Wisp,
    Mage,
};

enum class EnemyBulletType : std::uint8_t {
    Arrow,
    Magic,
};

struct PlayerState {
    float x = 0.0F;
    float y = 0.0F;
    Facing facing = Facing::South;
    bool moving = false;
    std::int16_t hp = 30;
    std::int16_t maxHp = 30;
    std::uint16_t invulnerabilityTicks = 0;
    std::uint16_t lightCooldownTicks = 0;
    std::uint16_t fireCooldownTicks = 0;
    std::uint16_t windCooldownTicks = 0;
    std::uint16_t thunderCooldownTicks = 0;
    std::uint16_t iceCooldownTicks = 0;
    std::uint16_t orbCooldownTicks = 0;
    std::uint16_t xp = 0;
    std::uint8_t level = 1;
    std::uint8_t pendingPerkChoices = 0;
    std::uint8_t lightLevel = 1;
    std::uint8_t fireLevel = 0;
    std::uint8_t windLevel = 0;
    std::uint8_t thunderLevel = 0;
    std::uint8_t iceLevel = 0;
    std::uint8_t orbLevel = 0;
    std::uint8_t familiarLevel = 0;
    std::uint8_t speedLevel = 0;
    bool choosingPerk = false;
    std::array<Perk, 4> perkChoices{{Perk::Light, Perk::Fire, Perk::Wind, Perk::Thunder}};
    Perk perkFlash = Perk::Light;
    std::uint8_t perkFlashSlot = 0;
    std::uint8_t perkFlashTicks = 0;
    float orbAngle = 0.0F;
};

struct CameraState {
    float x = 0.0F;
    float y = 0.0F;
};

struct EnemyState {
    float x = 0.0F;
    float y = 0.0F;
    float radius = 5.0F;
    float speedPerTick = 0.0F;
    float phase = 0.0F;
    float dashVelocityX = 0.0F;
    float dashVelocityY = 0.0F;
    std::int16_t hp = 0;
    std::uint16_t slowTicks = 0;
    std::uint16_t bornTicks = 0;
    std::uint16_t spawnDelayTicks = 0;
    std::uint16_t attackCooldownTicks = 0;
    std::uint16_t attackAnimationTicks = 0;
    std::uint16_t dashTicks = 0;
    std::uint16_t deathTicks = 0;
    std::uint8_t xpValue = 2;
    EnemyKind kind = EnemyKind::Imp;
    Facing facing = Facing::South;
    bool active = false;
};

struct EnemyBulletState {
    float x = 0.0F;
    float y = 0.0F;
    float velocityX = 0.0F;
    float velocityY = 0.0F;
    float radius = 0.0F;
    std::uint16_t remainingTicks = 0;
    std::uint16_t ageTicks = 0;
    std::uint8_t damage = 0;
    EnemyBulletType type = EnemyBulletType::Arrow;
    bool active = false;
};

struct PlayerBulletState {
    float x = 0.0F;
    float y = 0.0F;
    float velocityX = 0.0F;
    float velocityY = 0.0F;
    float speed = 0.0F;
    std::uint16_t remainingTicks = 0;
    std::uint16_t ageTicks = 0;
    std::int16_t damage = 0;
    std::uint8_t owner = 0;
    PlayerAttack type = PlayerAttack::Light;
    float radius = 2.0F;
    bool active = false;
};

struct WindSlashState {
    std::uint8_t owner = 0;
    std::uint8_t bladeCount = 0;
    std::int16_t damage = 0;
    float innerRadius = 16.0F;
    float outerRadius = 32.0F;
    float startAngle = 0.0F;
    std::uint16_t remainingTicks = 0;
    std::uint16_t ageTicks = 0;
    std::array<std::uint8_t, kMaximumEnemies> hitCooldownTicks{};
    bool active = false;
};

struct ThunderStrikeState {
    float x = 0.0F;
    float y = 0.0F;
    float radius = 24.0F;
    std::uint16_t remainingTicks = 0;
    std::uint16_t ageTicks = 0;
    bool active = false;
};

struct XpGemState {
    float x = 0.0F;
    float y = 0.0F;
    std::uint8_t value = 0;
    bool active = false;
};

class GameplayState {
public:
    void reset(const world::WorldMap& map) noexcept;
    void tick(const pixel_twins::Controllers& controllers, const world::WorldMap& map) noexcept;
    [[nodiscard]] bool addEnemy(float x, float y, EnemyKind kind = EnemyKind::Imp) noexcept;
    void grantXp(std::size_t playerIndex, std::uint16_t amount) noexcept;

    [[nodiscard]] const PlayerState& player(std::size_t index) const noexcept {
        return players_[index];
    }
    [[nodiscard]] const CameraState& camera(std::size_t index) const noexcept {
        return cameras_[index];
    }
    [[nodiscard]] const std::array<EnemyState, kMaximumEnemies>& enemies() const noexcept {
        return enemies_;
    }
    [[nodiscard]] const std::array<PlayerBulletState, kMaximumPlayerBullets>& bullets() const noexcept {
        return bullets_;
    }
    [[nodiscard]] const std::array<XpGemState, kMaximumXpGems>& xpGems() const noexcept {
        return xpGems_;
    }
    [[nodiscard]] const std::array<WindSlashState, kMaximumWindSlashes>& windSlashes() const noexcept {
        return windSlashes_;
    }
    [[nodiscard]] const std::array<ThunderStrikeState, kMaximumThunderStrikes>& thunderStrikes() const noexcept {
        return thunderStrikes_;
    }
    [[nodiscard]] const std::array<EnemyBulletState, kMaximumEnemyBullets>& enemyBullets() const noexcept {
        return enemyBullets_;
    }
    [[nodiscard]] std::size_t enemyCount() const noexcept;
    [[nodiscard]] std::size_t bulletCount() const noexcept;
    [[nodiscard]] std::uint32_t elapsedTicks() const noexcept { return elapsedTicks_; }
    [[nodiscard]] std::uint32_t score(std::size_t playerIndex) const noexcept {
        return scores_[playerIndex];
    }

private:
    std::array<PlayerState, pixel_twins::kControllerCount> players_{};
    std::array<CameraState, pixel_twins::kControllerCount> cameras_{};
    std::array<EnemyState, kMaximumEnemies> enemies_{};
    std::array<PlayerBulletState, kMaximumPlayerBullets> bullets_{};
    std::array<XpGemState, kMaximumXpGems> xpGems_{};
    std::array<WindSlashState, kMaximumWindSlashes> windSlashes_{};
    std::array<ThunderStrikeState, kMaximumThunderStrikes> thunderStrikes_{};
    std::array<EnemyBulletState, kMaximumEnemyBullets> enemyBullets_{};
    std::uint32_t randomState_ = 1;
    std::uint16_t spawnCooldownTicks_ = 0;
    std::uint16_t swarmCooldownTicks_ = 0;
    std::uint32_t elapsedTicks_ = 0;
    std::array<std::uint32_t, pixel_twins::kControllerCount> scores_{};
};

[[nodiscard]] bool playerPositionIsWalkable(
    const world::WorldMap& map, float x, float y) noexcept;
[[nodiscard]] std::uint16_t xpNeededForLevel(std::uint8_t level) noexcept;
[[nodiscard]] std::uint8_t directionRow8(float x, float y) noexcept;
[[nodiscard]] std::uint16_t thunderShockwaveRadius(std::uint16_t ageTicks) noexcept;

} // namespace wizward::game
