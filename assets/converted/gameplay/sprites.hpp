#pragma once

#include <cstdint>

namespace wizward::assets {

enum class SpriteAssetId : std::uint16_t {
    BossBlueFireball1616x163fSheet = 0,
    CastSpark1212x123fSheet = 1,
    FamiliarImpact1212x123fSheet = 2,
    FamiliarPink1616x1620fSheet = 3,
    FamiliarProjectile66x61fSheet = 4,
    FireImpact1616x163fSheet = 5,
    Fireball1212x123fSheet = 6,
    Fireball1616x163fSheet = 7,
    IceImpact1616x165fSheet = 8,
    IceShard128dir12x128fSheet = 9,
    ImpactBurst1616x163fSheet = 10,
    LightImpact1212x123fSheet = 11,
    LightOrb88x83fSheet = 12,
    OrbImpact1212x123fSheet = 13,
    OrbSatellite88x83fSheet = 14,
    ThunderImpact1616x163fSheet = 15,
    ThunderPillar24x603fSheet = 16,
    WindImpact1616x163fSheet = 17,
    WindSlash1616x168fSheet = 18,
    ArcherArrow12x416dir12x1216fSheet = 19,
    EnemyMagicOrb88x83fSheet = 20,
    BatEnemyLinelessDeath18x183fSheet = 21,
    BatEnemyLinelessWalk18x184fSheet = 22,
    GoblinArcherEnemyLinelessDeath16x163fSheet = 23,
    GoblinArcherEnemyLinelessShoot16x164fSheet = 24,
    GoblinArcherEnemyLinelessWalk16x164fSheet = 25,
    GolemEnemyLinelessDeath24x243fSheet = 26,
    GolemEnemyLinelessWalk24x244fSheet = 27,
    MonsterMageEnemyLinelessDeath18x183fSheet = 28,
    MonsterMageEnemyLinelessWalk18x184fSheet = 29,
    SealedStoneGuardianBossWalk48x483fSheet = 30,
    SkeletonEnemyDeath24x243fSheet = 31,
    SkeletonEnemyWalk24x244fSheet = 32,
    SlimeEnemyDeath16x163fSmallerSheet = 33,
    SlimeEnemyWalk16x164fSmallerSheet = 34,
    WispEnemyLinelessDeath18x183fSheet = 35,
    WispEnemyLinelessWalk18x184fSheet = 36,
    PowerupIcons16Sheet = 37,
    PowerupIcons8Sheet = 38,
    XpGem88x81fSheet = 39,
    PlazaRecallCircle32 = 40,
    BombCoreWave64x487fSheet = 41,
    BombFragment8x84fSheet = 42,
    BombRay16x161f8dirSheet = 43,
    CommonRisingMote6x84fSheet = 44,
    CommonWhiteSpark6x64fSheet = 45,
    HealCore16x245fSheet = 46,
    HealGroundRing32x165fSheet = 47,
    HpUpCore20x206fSheet = 48,
    HpUpGroundRing32x165fSheet = 49,
    HpUpMark8x84fSheet = 50,
    LevelUpCore24x246fSheet = 51,
    LevelUpGroundRing40x205fSheet = 52,
    LevelUpRay16x164f8dirSheet = 53,
    BoyMageDowned32x321fSheet = 54,
    BoyMageIdleStaff28x324fSheet = 55,
    BoyMageWalkStaff28x326fSheet = 56,
    GirlMageDowned28x321fSheet = 57,
    GirlMageIdle24x324fSheet = 58,
    GirlMageWalk24x324fSheet = 59,
};

inline constexpr std::uint16_t kSpriteAssetCount = 60;

[[nodiscard]] constexpr std::uint16_t spriteAssetIndex(SpriteAssetId id) noexcept {
    return static_cast<std::uint16_t>(id);
}

} // namespace wizward::assets
