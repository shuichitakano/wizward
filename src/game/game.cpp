#include "game/game.hpp"

#include "assets/wizward_font.hpp"

#include "pixel_twins/font.hpp"
#include "pixel_twins/primitives.hpp"
#include "pixel_twins/render_target.hpp"
#include "pixel_twins/sprite.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <string_view>

namespace wizward::game {
namespace {

constexpr std::uint32_t kRankingResultDelayTicks = 156;
constexpr std::uint32_t kRankingInputTimeoutTicks = 1800;
constexpr std::uint16_t kResultContinueDelayTicks = 15;
constexpr std::uint16_t kResultAutoReturnTicks = 300;
constexpr std::uint32_t kTimeBonusPerSecond = 200;
constexpr std::string_view kRankingCharacters = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789.- ";

void copySfxCues(const GameplayState& gameplay, UpdateResult& result) noexcept {
    result.sfxCueCount = std::min(gameplay.sfxCueCount(), result.sfxCues.size());
    std::copy_n(gameplay.sfxCues().begin(), result.sfxCueCount, result.sfxCues.begin());
}

std::uint8_t fourDirectionRow(Facing facing) noexcept {
    switch (facing) {
    case Facing::South:
    case Facing::SouthEast:
    case Facing::SouthWest: return 0;
    case Facing::East: return 1;
    case Facing::North:
    case Facing::NorthEast:
    case Facing::NorthWest: return 2;
    case Facing::West: return 3;
    }
    return 0;
}

std::uint32_t perkIconFrame(Perk perk) noexcept {
    return static_cast<std::uint32_t>(perk);
}

PIXEL_TWINS_SRAM void drawPerkChoices(pixel_twins::RenderTarget target,
                     const assets::GameAssets& assets,
                     const PlayerState& player,
                     std::size_t viewer) noexcept {
    constexpr std::array<std::array<std::int16_t, 2>, 4> kCenters{{
        {{118, 92}}, {{132, 80}}, {{146, 92}}, {{132, 104}},
    }};
    constexpr std::array<std::uint8_t, 4> kPackIndices{{1, 0, 2, 3}};
    constexpr std::array<const char*, 4> kP1Labels{{"1", "2", "3", "4"}};
    constexpr std::array<const char*, 4> kP2Labels{{"7", "8", "9", "0"}};
    const auto& labels = viewer == 0 ? kP1Labels : kP2Labels;
    if (player.choosingPerk) {
        for (std::size_t slot = 0; slot < kCenters.size(); ++slot) {
            const auto packIndex = kPackIndices[slot];
            pixel_twins::Sprite sprite{};
            if (assets.makeLoopingSprite(assets::SpriteAssetId::PowerupIcons16Sheet,
                                         perkIconFrame(player.perkChoices[packIndex]), 0,
                                         static_cast<std::int16_t>(kCenters[slot][0] - 8),
                                         static_cast<std::int16_t>(kCenters[slot][1] - 8), sprite)) {
                pixel_twins::drawSprite(target, sprite);
            }
            pixel_twins::drawText(target, assets::kWizwardFont,
                                  static_cast<std::int16_t>(kCenters[slot][0] - 10),
                                  static_cast<std::int16_t>(kCenters[slot][1] - 8),
                                  labels[slot], 255);
        }
    }
    if (player.perkFlashTicks > 0 && (player.perkFlashTicks / 3U) % 2U == 0U) {
        const auto slot = player.perkFlashSlot;
        pixel_twins::Sprite sprite{};
        if (assets.makeLoopingSprite(assets::SpriteAssetId::PowerupIcons16Sheet,
                                     perkIconFrame(player.perkFlash), 0,
                                     static_cast<std::int16_t>(kCenters[slot][0] - 8),
                                     static_cast<std::int16_t>(kCenters[slot][1] - 8), sprite)) {
            pixel_twins::drawSprite(target, sprite);
        }
    }
    for (std::uint8_t index = 1; index < player.pendingPerkChoices; ++index) {
        pixel_twins::fillRectangle(target, static_cast<std::int16_t>(116 + index * 3U), 113, 1, 1, 15);
    }
}

PIXEL_TWINS_SRAM void drawWeaponLevels(pixel_twins::RenderTarget target,
                      const assets::GameAssets& assets,
                      const PlayerState& player) noexcept {
    constexpr std::array<std::uint8_t PlayerState::*, 7> kLevels{{
        &PlayerState::lightLevel, &PlayerState::fireLevel, &PlayerState::windLevel,
        &PlayerState::thunderLevel, &PlayerState::iceLevel, &PlayerState::orbLevel,
        &PlayerState::familiarLevel,
    }};
    std::size_t activeIndex = 0;
    for (std::size_t weapon = 0; weapon < kLevels.size(); ++weapon) {
        const auto level = player.*kLevels[weapon];
        if (level == 0) continue;
        const auto x = static_cast<std::int16_t>(4 + (activeIndex % 4U) * 18U);
        const auto y = static_cast<std::int16_t>(101 + (activeIndex / 4U) * 10U);
        pixel_twins::Sprite icon{};
        if (assets.makeLoopingSprite(assets::SpriteAssetId::PowerupIcons8Sheet,
                                     static_cast<std::uint32_t>(weapon), 0, x, y, icon)) {
            pixel_twins::drawSprite(target, icon);
        }
        char levelText[3]{};
        const auto digits = level >= 10 ? 2U : 1U;
        if (digits == 2U) levelText[0] = static_cast<char>('0' + level / 10U);
        levelText[digits - 1U] = static_cast<char>('0' + level % 10U);
        pixel_twins::drawText(target, assets::kWizwardFont,
                              static_cast<std::int16_t>(x + 10), y,
                              std::string_view(levelText, digits), 255);
        ++activeIndex;
    }
}

std::string_view formatUnsigned(std::uint32_t value, char (&buffer)[12]) noexcept {
    std::size_t digits = 1;
    for (auto remaining = value; remaining >= 10U; remaining /= 10U) ++digits;
    auto remaining = value;
    for (std::size_t index = digits; index > 0; --index) {
        buffer[index - 1U] = static_cast<char>('0' + remaining % 10U);
        remaining /= 10U;
    }
    return {buffer, digits};
}

PIXEL_TWINS_SRAM void drawRightAlignedText(pixel_twins::RenderTarget target,
                          std::string_view text,
                          std::int16_t right,
                          std::int16_t y,
                          std::int16_t stride = 6) noexcept {
    const auto width = static_cast<std::int16_t>(
        text.empty() ? 0 : (text.size() - 1U) * static_cast<std::size_t>(stride) + 8U);
    pixel_twins::drawText(target, assets::kWizwardFont,
                          static_cast<std::int16_t>(right - width), y, text, 18, stride);
}

PIXEL_TWINS_SRAM void drawCenteredText(pixel_twins::RenderTarget target,
                      std::string_view text,
                      std::int16_t center,
                      std::int16_t y,
                      std::int16_t stride = 6) noexcept {
    const auto width = static_cast<std::int16_t>(
        text.empty() ? 0 : (text.size() - 1U) * static_cast<std::size_t>(stride) + 8U);
    pixel_twins::drawText(target, assets::kWizwardFont,
                          static_cast<std::int16_t>(center - width / 2), y, text, 18, stride);
}

PIXEL_TWINS_SRAM void drawTimer(pixel_twins::RenderTarget target,
               std::uint32_t elapsedTicks) noexcept {
    constexpr std::uint32_t kGameSeconds = 300;
    const auto elapsedSeconds = elapsedTicks / 60U;
    const auto remain = elapsedSeconds < kGameSeconds ? kGameSeconds - elapsedSeconds : 0U;
    const auto elapsedMinute = elapsedSeconds / 60U;
    const auto sinceMinute = elapsedSeconds - elapsedMinute * 60U;
    const auto minuteNotice = elapsedSeconds < 4U
        || (elapsedMinute > 0U && elapsedMinute < 5U && sinceMinute <= 4U);
    if (minuteNotice) {
        if ((elapsedTicks / 15U) % 2U == 0U) {
            char text[] = "5 MIN LEFT";
            text[0] = static_cast<char>('0' + (remain + 59U) / 60U);
            drawCenteredText(target, text, 80, 24);
        }
        return;
    } else if (remain <= 60U && elapsedSeconds >= 4U) {
        char text[] = "0:00";
        text[2] = static_cast<char>('0' + (remain / 10U) % 10U);
        text[3] = static_cast<char>('0' + remain % 10U);
        drawCenteredText(target, text, 80, 24);
    }
}

PIXEL_TWINS_SRAM void drawMiniMap(pixel_twins::RenderTarget target,
                 const world::WorldMap& map,
                 const GameplayState& gameplay,
                 std::size_t viewer) noexcept {
    constexpr std::int16_t kX = 5;
    constexpr std::int16_t kY = 5;
    constexpr std::uint16_t kSize = 32;
    pixel_twins::fillRectangle(target, kX, kY, kSize, kSize, 0);
    for (std::uint16_t my = 0; my < kSize; ++my) {
        for (std::uint16_t mx = 0; mx < kSize; ++mx) {
            const auto tx = static_cast<std::uint16_t>(mx * world::kMapColumns / kSize);
            const auto ty = static_cast<std::uint16_t>(my * world::kMapRows / kSize);
            if (map.isWater(tx, ty)) continue;
            pixel_twins::fillRectangle(target,
                static_cast<std::int16_t>(kX + mx), static_cast<std::int16_t>(kY + my), 1, 1,
                map.collides(tx, ty) ? 29 : 30);
        }
    }
    pixel_twins::drawRectangle(target, kX, kY, kSize, kSize, 32);
    pixel_twins::fillRectangle(target,
        static_cast<std::int16_t>(kX + 50U * kSize / world::kMapColumns),
        static_cast<std::int16_t>(kY + 50U * kSize / world::kMapRows), 2, 2, 220);
    for (std::size_t index = 0; index < map.seals.size(); ++index) {
        const auto& seal = map.seals[index];
        const auto x = static_cast<std::int16_t>(kX + seal.x * kSize / world::kMapColumns);
        const auto y = static_cast<std::int16_t>(kY + seal.y * kSize / world::kMapRows);
        pixel_twins::fillCircle(target, x, y, 1, gameplay.seal(index).active ? 255 : 220);
    }
    // 重なった場合も、この画面を見ているプレイヤーの色と輪郭を前面に出す。
    for (std::size_t order = 0; order < pixel_twins::kControllerCount; ++order) {
        const auto index = (viewer + 1U + order) % pixel_twins::kControllerCount;
        const auto& player = gameplay.player(index);
        const auto x = static_cast<std::int16_t>(kX + player.x * kSize
            / static_cast<float>(world::kMapColumns * kWorldTileSize));
        const auto y = static_cast<std::int16_t>(kY + player.y * kSize
            / static_cast<float>(world::kMapRows * kWorldTileSize));
        pixel_twins::fillCircle(target, x, y, index == viewer ? 2 : 1, index == 0 ? 19 : 20);
    }
}

PIXEL_TWINS_SRAM void drawXpRecallCircle(pixel_twins::RenderTarget target,
                         const assets::GameAssets& assets,
                         const CameraState& camera,
                         const GameplayState& gameplay) noexcept {
    constexpr float kCenter = 50.5F * static_cast<float>(kWorldTileSize);
    pixel_twins::Sprite circle{};
    if (assets.makeSprite(assets::SpriteAssetId::PlazaRecallCircle32, 0, 0,
                          static_cast<std::int16_t>(std::round(kCenter - camera.x - 16.0F)),
                          static_cast<std::int16_t>(std::round(kCenter - camera.y - 16.0F)),
                          circle)) {
        pixel_twins::drawSprite(target, circle);
    }
    constexpr float kTau = 6.2831853F;
    for (std::size_t playerIndex = 0; playerIndex < pixel_twins::kControllerCount; ++playerIndex) {
        const auto ticks = gameplay.player(playerIndex).xpRecallEffectTicks;
        if (ticks == 0) continue;
        const auto progress = 1.0F - static_cast<float>(ticks) / 42.0F;
        const auto radius = 5.0F + progress * 18.0F;
        for (std::uint8_t particle = 0; particle < 8; ++particle) {
            const auto angle = static_cast<float>(particle) * kTau / 8.0F;
            pixel_twins::fillRectangle(target,
                static_cast<std::int16_t>(std::round(kCenter - camera.x
                    + std::cos(angle) * radius)),
                static_cast<std::int16_t>(std::round(kCenter - camera.y
                    + std::sin(angle) * radius * 0.55F)),
                2, 2, playerIndex == 0 ? 19 : 20);
        }
    }
}

float smoothStep(float value) noexcept {
    const auto t = std::clamp(value, 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

PIXEL_TWINS_SRAM void drawActiveSeals(pixel_twins::RenderTarget target,
                     const world::WorldMap& map,
                     const GameplayState& gameplay,
                     const CameraState& camera) noexcept {
    struct ParticleMotion {
        float xFrequency, yFrequency, speed, xRadius, yRadius, offset, twinkle;
    };
    constexpr std::array<ParticleMotion, 5> kMotions{{
        {3.0F, 2.0F, 1.38F, 14.0F, 7.0F, 0.1F, 9.0F},
        {2.0F, 5.0F, 1.02F, 12.0F, 8.0F, 1.7F, 11.0F},
        {4.0F, 3.0F, 0.84F, 15.0F, 6.0F, 3.1F, 13.0F},
        {5.0F, 4.0F, 0.66F, 10.0F, 9.0F, 4.2F, 10.0F},
        {3.0F, 5.0F, 0.55F, 13.0F, 8.0F, 5.4F, 12.0F},
    }};
    constexpr std::array<std::uint8_t, 3> kRisingColors{{25, 24, 26}};
    constexpr std::array<std::uint8_t, 3> kTrailColors{{177, 25, 55}};
    constexpr std::array<std::uint8_t, 4> kHeadColors{{26, 24, 148, 255}};
    const auto elapsed = static_cast<float>(gameplay.elapsedTicks()) / 60.0F;
    for (std::size_t sealIndex = 0; sealIndex < map.seals.size(); ++sealIndex) {
        const auto& state = gameplay.seal(sealIndex);
        if (!state.active) continue;
        const auto x = static_cast<std::int16_t>(std::round(
            static_cast<float>(map.seals[sealIndex].x * kWorldTileSize)
                + static_cast<float>(kWorldTileSize) * 0.5F - camera.x));
        const auto y = static_cast<std::int16_t>(std::round(
            static_cast<float>(map.seals[sealIndex].y * kWorldTileSize)
                + static_cast<float>(kWorldTileSize) * 0.5F - 7.0F - camera.y));
        const auto age = static_cast<float>(gameplay.elapsedTicks() - state.activatedAtTicks) / 60.0F;
        const auto formation = smoothStep(age / 0.52F);
        if (age < 0.68F) {
            const auto rise = smoothStep(age / 0.26F);
            const auto fade = 1.0F - smoothStep((age - 0.28F) / 0.4F);
            const auto height = static_cast<std::uint16_t>(std::max(2.0F, std::round(38.0F * rise * fade)));
            pixel_twins::fillRectangle(target, static_cast<std::int16_t>(x - 1),
                static_cast<std::int16_t>(y - static_cast<std::int16_t>(height)), 3,
                static_cast<std::uint16_t>(height + 3U), 25);
            pixel_twins::fillRectangle(target, x,
                static_cast<std::int16_t>(y - static_cast<std::int16_t>(height) - 2), 1,
                static_cast<std::uint16_t>(height + 4U), 26);
            const auto burst = smoothStep((age - 0.08F) / 0.42F);
            for (std::uint8_t ray = 0; ray < 8; ++ray) {
                const auto angle = static_cast<float>(ray) * 3.1415927F / 4.0F + 3.1415927F / 8.0F;
                const auto radius = 3.0F + burst * 18.0F;
                pixel_twins::fillRectangle(target,
                    static_cast<std::int16_t>(std::round(static_cast<float>(x) + std::cos(angle) * radius)),
                    static_cast<std::int16_t>(std::round(static_cast<float>(y) + std::sin(angle) * radius * 0.5F)),
                    2, 1, (ray & 1U) != 0U ? 24 : 148);
            }
        }
        if (formation <= 0.0F) continue;
        for (std::uint8_t beam = 0; beam < 3; ++beam) {
            const auto progress = std::fmod(elapsed * (1.08F + static_cast<float>(beam) * 0.13F)
                + static_cast<float>(sealIndex) * 0.23F + static_cast<float>(beam) * 0.31F, 1.0F);
            const auto beamX = static_cast<std::int16_t>(std::round(static_cast<float>(x)
                + std::sin(elapsed * 2.4F + static_cast<float>(sealIndex)
                    + static_cast<float>(beam) * 2.1F) * (2.0F + static_cast<float>(beam) * 2.0F) * formation));
            const auto beamY = static_cast<std::int16_t>(std::round(static_cast<float>(y)
                - (4.0F + progress * 25.0F) * formation));
            const auto height = static_cast<std::uint16_t>(std::max(2.0F,
                std::round((7.0F - progress * 3.0F) * formation)));
            pixel_twins::fillRectangle(target, beamX,
                static_cast<std::int16_t>(beamY - static_cast<std::int16_t>(height)),
                beam == 0 ? 2 : 1, height, kRisingColors[beam]);
            if (beam == 2 && height >= 4) {
                pixel_twins::fillRectangle(target, beamX,
                    static_cast<std::int16_t>(beamY - static_cast<std::int16_t>(height)), 1, 2, 148);
            }
        }
        for (std::size_t motionIndex = 0; motionIndex < kMotions.size(); ++motionIndex) {
            const auto& motion = kMotions[motionIndex];
            const auto phase = elapsed * motion.speed + motion.offset + static_cast<float>(sealIndex) * 0.41F;
            for (std::int8_t trail = 3; trail >= 1; --trail) {
                const auto trailPhase = phase - static_cast<float>(trail) * 0.055F;
                const auto px = static_cast<std::int16_t>(std::round(static_cast<float>(x)
                    + std::sin(trailPhase * motion.xFrequency) * motion.xRadius * formation));
                const auto py = static_cast<std::int16_t>(std::round(static_cast<float>(y)
                    + std::sin(trailPhase * motion.yFrequency) * motion.yRadius * formation));
                pixel_twins::fillRectangle(target, px, py, 1, 1,
                    kTrailColors[static_cast<std::size_t>(3 - trail)]);
            }
            const auto px = static_cast<std::int16_t>(std::round(static_cast<float>(x)
                + std::sin(phase * motion.xFrequency) * motion.xRadius * formation));
            const auto py = static_cast<std::int16_t>(std::round(static_cast<float>(y)
                + std::sin(phase * motion.yFrequency) * motion.yRadius * formation));
            const auto twinkle = static_cast<std::uint8_t>(
                static_cast<std::uint32_t>(elapsed * motion.twinkle
                    + static_cast<float>(motionIndex) * 1.7F + static_cast<float>(sealIndex)) % 7U);
            pixel_twins::fillRectangle(target, static_cast<std::int16_t>(px - 1),
                static_cast<std::int16_t>(py - 1), 2, 2,
                kHeadColors[(motionIndex + twinkle) % kHeadColors.size()]);
            if (twinkle <= 1U) {
                pixel_twins::fillRectangle(target, static_cast<std::int16_t>(px - 2), py, 1, 1, 255);
                pixel_twins::fillRectangle(target, static_cast<std::int16_t>(px + 2), py, 1, 1, 255);
                pixel_twins::fillRectangle(target, px, static_cast<std::int16_t>(py - 2), 1, 1, 255);
                pixel_twins::fillRectangle(target, px, static_cast<std::int16_t>(py + 2), 1, 1, 255);
            }
        }
    }
}

PIXEL_TWINS_SRAM void drawBossIntroShadow(pixel_twins::RenderTarget target,
                         const GameplayState& gameplay,
                         const CameraState& camera) noexcept {
    if (gameplay.bossIntroTicks() == 0 || gameplay.boss() == nullptr) return;
    const auto elapsedTicks = static_cast<std::uint16_t>(183U - gameplay.bossIntroTicks());
    if (elapsedTicks < 35U) return;
    const auto* boss = gameplay.boss();
    const auto growth = smoothStep((static_cast<float>(elapsedTicks) / 60.0F - 0.58F) / 0.67F);
    const auto x = static_cast<std::int16_t>(std::round(boss->x - camera.x));
    const auto y = static_cast<std::int16_t>(std::round(boss->y - camera.y + 1.0F));
    pixel_twins::fillEllipse(target, x, y,
        static_cast<std::uint16_t>(std::round(4.0F + growth * 15.0F)),
        static_cast<std::uint16_t>(std::round(2.0F + growth * 3.0F)),
        elapsedTicks >= 75U ? 118 : 130);
}

PIXEL_TWINS_SRAM void drawBossIntroOverlay(pixel_twins::RenderTarget target,
                          const GameplayState& gameplay,
                          const CameraState& camera) noexcept {
    if (gameplay.bossIntroTicks() == 0 || gameplay.boss() == nullptr) return;
    const auto elapsedTicks = static_cast<std::uint16_t>(183U - gameplay.bossIntroTicks());
    if (elapsedTicks < 75U) return;
    const auto impactTicks = static_cast<std::uint16_t>(elapsedTicks - 75U);
    const auto* boss = gameplay.boss();
    const auto x = static_cast<std::int16_t>(std::round(boss->x - camera.x));
    const auto y = static_cast<std::int16_t>(std::round(boss->y - camera.y));
    if (impactTicks < 30U) {
        const auto progress = smoothStep(static_cast<float>(impactTicks) / 30.0F);
        const auto radius = 10.0F + progress * 58.0F;
        const auto color = static_cast<std::uint8_t>(impactTicks < 11U ? 148U : 201U);
        pixel_twins::drawEllipse(target, x, y,
            static_cast<std::uint16_t>(std::round(radius)),
            static_cast<std::uint16_t>(std::max(3.0F, std::round(radius * 0.24F))), color);
        for (std::uint8_t ray = 0; ray < 12; ++ray) {
            const auto angle = static_cast<float>(ray) * 6.2831853F / 12.0F;
            const auto inner = 12.0F + progress * 8.0F;
            const auto outer = 24.0F + progress * 66.0F;
            pixel_twins::drawLine(target,
                static_cast<std::int16_t>(std::round(static_cast<float>(x) + std::cos(angle) * inner)),
                static_cast<std::int16_t>(std::round(static_cast<float>(y) + std::sin(angle) * inner * 0.34F)),
                static_cast<std::int16_t>(std::round(static_cast<float>(x) + std::cos(angle) * outer)),
                static_cast<std::int16_t>(std::round(static_cast<float>(y) + std::sin(angle) * outer * 0.34F)),
                color);
        }
    }
    if (impactTicks < 6U && (impactTicks / 2U) % 2U == 0U) {
        pixel_twins::fillRectangle(target, 0, 0, 160, 120, 148);
    }
}

PIXEL_TWINS_SRAM void drawClearSequence(pixel_twins::RenderTarget target,
                       const assets::GameAssets& assets,
                       const GameplayState& gameplay,
                       const CameraState& camera,
                       std::uint32_t frame) noexcept {
    if (!gameplay.clearSequenceActive()) return;
    const auto ticks = gameplay.clearSequenceTicks();
    const auto centerX = gameplay.clearX() - camera.x;
    const auto centerY = gameplay.clearY() - 10.0F - camera.y;
    const auto hash = [](std::uint32_t value) noexcept {
        value ^= value >> 16U;
        value *= 0x7feb352dU;
        value ^= value >> 15U;
        value *= 0x846ca68bU;
        return value ^ (value >> 16U);
    };
    const auto unit = [&hash](std::uint32_t value) noexcept {
        return static_cast<float>(hash(value) & 0xffffU) / 65535.0F;
    };
    if (ticks < 141U) {
        const auto shake = ((ticks * 46U / 60U) & 1U) == 0U ? (ticks > 87U ? -3 : -2)
                                                               : (ticks > 87U ? 3 : 2);
        pixel_twins::Sprite sprite{};
        if (assets.makeLoopingSprite(assets::SpriteAssetId::SealedStoneGuardianBossWalk48x483fSheet,
                                     frame / 8U, fourDirectionRow(gameplay.clearFacing()),
                                     static_cast<std::int16_t>(
                                         static_cast<std::int32_t>(std::round(gameplay.clearX() - camera.x - 24.0F))
                                         + shake),
                                     static_cast<std::int16_t>(std::round(gameplay.clearY() - camera.y - 42.0F)),
                                     sprite)) {
            pixel_twins::drawSprite(target, sprite);
        }
        const auto visibleRays = static_cast<std::uint8_t>(36U * ticks / 141U);
        for (std::uint8_t index = 0; index < visibleRays; ++index) {
            const auto start = 9.0F + unit(index * 17U + 1U) * 93.0F;
            if (static_cast<float>(ticks) < start) continue;
            if (((ticks + static_cast<std::uint16_t>(start)) / 3U) % 3U == 0U) continue;
            const auto angle = unit(index * 17U + 2U) * 6.2831853F;
            const auto inner = 8.0F + unit(index * 17U + 3U) * 20.0F;
            const auto targetOuter = 96.0F + unit(index * 17U + 4U) * 172.0F;
            const auto grow = smoothStep((static_cast<float>(ticks) - start) / 51.0F);
            const auto outer = inner + (targetOuter - inner) * grow;
            const auto color = unit(index * 17U + 5U) < 0.62F ? 13 : 148;
            pixel_twins::drawLine(target,
                static_cast<std::int16_t>(std::round(centerX + std::cos(angle) * outer)),
                static_cast<std::int16_t>(std::round(centerY + std::sin(angle) * outer)),
                static_cast<std::int16_t>(std::round(centerX + std::cos(angle) * inner)),
                static_cast<std::int16_t>(std::round(centerY + std::sin(angle) * inner)),
                static_cast<std::uint8_t>(color));
        }
        if (ticks >= 107U) {
            for (std::uint8_t ring = 0; ring < 5; ++ring) {
                const auto start = static_cast<std::uint16_t>(103U + ring * 5U);
                if (ticks <= start || ticks >= start + 25U) continue;
                const auto progress = smoothStep(static_cast<float>(ticks - start) / 25.0F);
                const auto radius = (150.0F + static_cast<float>(ring) * 22.0F) * (1.0F - progress)
                    + 8.0F * progress;
                pixel_twins::drawCircle(target,
                    static_cast<std::int16_t>(std::round(centerX)),
                    static_cast<std::int16_t>(std::round(centerY)),
                    static_cast<std::uint16_t>(std::round(radius)),
                    (ring & 1U) != 0U ? 12 : 13);
            }
        }
        return;
    }
    const auto age = static_cast<float>(ticks - 141U) / 60.0F;
    constexpr std::array<std::uint8_t, 4> kEnergyColors{{12, 13, 241, 148}};
    for (std::uint16_t index = 0; index < 112; ++index) {
        const auto lifetime = 1.15F + unit(index * 29U + 7U) * 1.05F;
        if (age > lifetime) continue;
        const auto angle = static_cast<float>(index) / 112.0F * 6.2831853F
            + unit(index * 29U + 8U) * 0.16F;
        const auto speed = 78.0F + unit(index * 29U + 9U) * 184.0F;
        const auto x = centerX + std::cos(angle) * speed * age;
        const auto y = centerY + std::sin(angle) * speed * age;
        const auto size = static_cast<std::uint16_t>(unit(index * 29U + 10U) < 0.28F ? 5U : 3U);
        pixel_twins::fillRectangle(target, static_cast<std::int16_t>(std::round(x)),
            static_cast<std::int16_t>(std::round(y)), size, size,
            kEnergyColors[index % kEnergyColors.size()]);
    }
    constexpr std::array<std::uint8_t, 4> kRockColors{{152, 33, 125, 130}};
    for (std::uint16_t index = 0; index < 72; ++index) {
        const auto lifetime = 1.45F + unit(index * 31U + 11U) * 1.1F;
        if (age > lifetime) continue;
        const auto angle = static_cast<float>(index) / 72.0F * 6.2831853F
            + (unit(index * 31U + 12U) - 0.5F) * 0.24F;
        const auto speed = 92.0F + unit(index * 31U + 13U) * 196.0F;
        const auto gravity = 148.0F + unit(index * 31U + 14U) * 96.0F;
        const auto x = centerX + (unit(index * 31U + 15U) - 0.5F) * 18.0F
            + std::cos(angle) * speed * age;
        const auto y = centerY + (unit(index * 31U + 16U) - 0.5F) * 10.0F
            + (std::sin(angle) * speed * 0.62F - 58.0F - unit(index * 31U + 17U) * 112.0F) * age
            + gravity * age * age * 0.5F;
        const auto size = static_cast<std::uint16_t>(4U + hash(index * 31U + 18U) % 5U);
        pixel_twins::fillRectangle(target, static_cast<std::int16_t>(std::round(x)),
            static_cast<std::int16_t>(std::round(y)), size, size, (index % 3U) == 0U ? 118 : 124);
        pixel_twins::fillRectangle(target, static_cast<std::int16_t>(std::round(x + 1.0F)),
            static_cast<std::int16_t>(std::round(y)), std::max<std::uint16_t>(1, size - 2U),
            std::max<std::uint16_t>(1, size - 2U), kRockColors[index % kRockColors.size()]);
    }
    if (ticks < 155U) {
        pixel_twins::fillRectangle(target, 0, 0, 160, 120,
            ((ticks - 141U) / 3U) % 2U == 0U ? 255 : 148);
    }
}

PIXEL_TWINS_SRAM void drawOffscreenPartnerArrow(pixel_twins::RenderTarget target,
                               const GameplayState& gameplay,
                               const CameraState& camera,
                               std::size_t viewer) noexcept {
    const auto& player = gameplay.player(viewer);
    const auto& partner = gameplay.player(1U - viewer);
    const auto screenX = partner.x - camera.x;
    const auto screenY = partner.y - camera.y;
    if (screenX >= 0.0F && screenX <= 160.0F && screenY >= 0.0F && screenY <= 120.0F) return;
    auto dx = partner.x - player.x;
    auto dy = partner.y - player.y;
    const auto length = std::sqrt(dx * dx + dy * dy);
    if (length <= 0.0F) return;
    dx /= length;
    dy /= length;
    const auto x = std::clamp(80.0F + dx * 70.0F, 8.0F, 152.0F);
    const auto y = std::clamp(60.0F + dy * 52.0F, 14.0F, 112.0F);
    pixel_twins::fillTriangle(target,
        static_cast<std::int16_t>(std::round(x + dx * 5.0F)),
        static_cast<std::int16_t>(std::round(y + dy * 5.0F)),
        static_cast<std::int16_t>(std::round(x - dy * 4.0F - dx * 3.0F)),
        static_cast<std::int16_t>(std::round(y + dx * 4.0F - dy * 3.0F)),
        static_cast<std::int16_t>(std::round(x + dy * 4.0F - dx * 3.0F)),
        static_cast<std::int16_t>(std::round(y - dx * 4.0F - dy * 3.0F)),
        viewer == 0 ? 20 : 19);
}

template<std::size_t Capacity, std::size_t ExCapacity>
PIXEL_TWINS_SRAM void queueAsset(pixel_twins::SpriteBuckets<Capacity, ExCapacity>& buckets,
                const assets::GameAssets& assets,
                assets::SpriteAssetId id,
                std::uint32_t frame,
                std::int16_t x,
                std::int16_t y,
                float screenFootY,
                std::uint8_t directionRow = 0) noexcept {
    pixel_twins::Sprite sprite{};
    if (!assets.makeLoopingSprite(id, frame, directionRow, x, y, sprite)) return;
    constexpr float kSortMargin = 60.0F;
    const auto bucket = static_cast<std::uint16_t>(std::clamp(
        static_cast<std::int32_t>(screenFootY + kSortMargin), 0,
        static_cast<std::int32_t>(pixel_twins::kBucketCount - 1U)));
    (void)buckets.addSprite(bucket, sprite);
}

template<std::size_t Capacity, std::size_t ExCapacity>
PIXEL_TWINS_SRAM void queueScaledAsset(
                pixel_twins::SpriteBuckets<Capacity, ExCapacity>& buckets,
                const assets::GameAssets& assets,
                assets::SpriteAssetId id,
                std::uint32_t frame,
                std::uint8_t directionRow,
                std::int16_t x,
                std::int16_t y,
                std::uint8_t width,
                std::uint8_t height,
                std::uint8_t logicalWidth,
                std::uint8_t logicalHeight,
                float screenFootY) noexcept {
    pixel_twins::Sprite source{};
    if (!assets.makeLoopingSprite(id, frame, directionRow, x, y, source)) return;
    const auto trimX = static_cast<std::int16_t>(source.dx - x);
    const auto trimY = static_cast<std::int16_t>(source.dy - y);
    const auto scaledLeft = static_cast<std::int16_t>(std::round(
        static_cast<float>(trimX) * width / logicalWidth));
    const auto scaledTop = static_cast<std::int16_t>(std::round(
        static_cast<float>(trimY) * height / logicalHeight));
    const auto scaledRight = static_cast<std::int16_t>(std::round(
        static_cast<float>(trimX + source.sw) * width / logicalWidth));
    const auto scaledBottom = static_cast<std::int16_t>(std::round(
        static_cast<float>(trimY + source.sh) * height / logicalHeight));
    if (scaledRight <= scaledLeft || scaledBottom <= scaledTop) return;
    pixel_twins::SpriteEx sprite{
        static_cast<std::int16_t>(x + scaledLeft),
        static_cast<std::int16_t>(y + scaledTop),
        static_cast<std::uint8_t>(scaledRight - scaledLeft),
        static_cast<std::uint8_t>(scaledBottom - scaledTop),
        source.sw, source.sh, source.p};
    constexpr float kSortMargin = 60.0F;
    const auto bucket = static_cast<std::uint16_t>(std::clamp(
        static_cast<std::int32_t>(screenFootY + kSortMargin), 0,
        static_cast<std::int32_t>(pixel_twins::kBucketCount - 1U)));
    (void)buckets.addSpriteEx(bucket, sprite);
}

struct PerkSpriteSpec {
    assets::SpriteAssetId asset;
    std::uint8_t frames;
    std::uint8_t width;
    std::uint8_t height;
};

PIXEL_TWINS_SRAM bool perkEffectProgress(float age, float start, float end,
                                        float& progress) noexcept {
    progress = (age - start) / (end - start);
    return progress >= 0.0F && progress < 1.0F;
}

PIXEL_TWINS_SRAM void drawPerkSpriteProgress(pixel_twins::RenderTarget target,
                                             const assets::GameAssets& assets,
                                             const CameraState& camera,
                                             PerkSpriteSpec spec, float progress,
                                             float x, float y,
                                             std::uint8_t row = 0) noexcept {
    if (progress < 0.0F || progress >= 1.0F) return;
    const auto frame = static_cast<std::uint32_t>(std::min<std::uint8_t>(
        static_cast<std::uint8_t>(spec.frames - 1U),
        static_cast<std::uint8_t>(progress * static_cast<float>(spec.frames))));
    pixel_twins::Sprite sprite{};
    if (assets.makeLoopingSprite(spec.asset, frame, row,
            static_cast<std::int16_t>(std::round(x - camera.x - spec.width * 0.5F)),
            static_cast<std::int16_t>(std::round(y - camera.y - spec.height * 0.5F)), sprite)) {
        pixel_twins::drawSprite(target, sprite);
    }
}

PIXEL_TWINS_SRAM void drawPerkSpriteWindow(pixel_twins::RenderTarget target,
                                           const assets::GameAssets& assets,
                                           const CameraState& camera,
                                           PerkSpriteSpec spec, float age,
                                           float start, float end, float x, float y,
                                           std::uint8_t row = 0) noexcept {
    float progress = 0.0F;
    if (!perkEffectProgress(age, start, end, progress)) return;
    drawPerkSpriteProgress(target, assets, camera, spec, progress, x, y, row);
}

template<std::size_t Capacity, std::size_t ExCapacity>
PIXEL_TWINS_SRAM void queuePerkSpriteWindow(
        pixel_twins::SpriteBuckets<Capacity, ExCapacity>& buckets,
        const assets::GameAssets& assets, const CameraState& camera,
        PerkSpriteSpec spec, float age, float start, float end,
        float x, float y, float sortY) noexcept {
    float progress = 0.0F;
    if (!perkEffectProgress(age, start, end, progress)) return;
    const auto frame = static_cast<std::uint32_t>(std::min<std::uint8_t>(
        static_cast<std::uint8_t>(spec.frames - 1U),
        static_cast<std::uint8_t>(progress * static_cast<float>(spec.frames))));
    queueAsset(buckets, assets, spec.asset, frame,
        static_cast<std::int16_t>(std::round(x - camera.x - spec.width * 0.5F)),
        static_cast<std::int16_t>(std::round(y - camera.y - spec.height * 0.5F)),
        sortY - camera.y);
}

PIXEL_TWINS_SRAM void drawPerkSpark(pixel_twins::RenderTarget target,
                                    const assets::GameAssets& assets,
                                    const CameraState& camera,
                                    float age, float start, float x, float y) noexcept {
    constexpr PerkSpriteSpec kWhiteSpark{
        assets::SpriteAssetId::CommonWhiteSpark6x64fSheet, 4, 6, 6};
    float progress = 0.0F;
    if (!perkEffectProgress(age, start, start + 0.24F, progress)) return;
    drawPerkSpriteProgress(target, assets, camera, kWhiteSpark, progress, x, y);
}

PIXEL_TWINS_SRAM float bombEffectRandom(std::uint32_t seed,
                                       std::uint8_t fragment,
                                       std::uint32_t salt) noexcept {
    auto value = seed ^ (static_cast<std::uint32_t>(fragment) + 1U) * 2246822519U
        ^ salt * 3266489917U;
    value ^= value >> 15U;
    value *= 2246822519U;
    value ^= value >> 13U;
    return static_cast<float>(value >> 8U) / static_cast<float>(0x00ffffffU);
}

template<std::size_t Capacity, std::size_t ExCapacity>
PIXEL_TWINS_SRAM void queuePerkEffectUnder(
        pixel_twins::SpriteBuckets<Capacity, ExCapacity>& buckets,
        const assets::GameAssets& assets, const CameraState& camera,
        const GameplayState& gameplay) noexcept {
    constexpr PerkSpriteSpec kHealRing{
        assets::SpriteAssetId::HealGroundRing32x165fSheet, 5, 32, 16};
    constexpr PerkSpriteSpec kHpRing{
        assets::SpriteAssetId::HpUpGroundRing32x165fSheet, 5, 32, 16};
    constexpr PerkSpriteSpec kLevelRing{
        assets::SpriteAssetId::LevelUpGroundRing40x205fSheet, 5, 40, 20};
    for (const auto& effect : gameplay.perkEffects()) {
        if (!effect.active || effect.owner >= pixel_twins::kControllerCount) continue;
        const auto& player = gameplay.player(effect.owner);
        const auto age = static_cast<float>(effect.ageTicks) / 60.0F;
        if (effect.type == PerkEffectType::Heal) {
            queuePerkSpriteWindow(buckets, assets, camera, kHealRing,
                                  age, 0.0F, 0.48F, player.x, player.y, player.y);
        } else if (effect.type == PerkEffectType::HpUp) {
            queuePerkSpriteWindow(buckets, assets, camera, kHpRing,
                                  age, 0.0F, 0.5F, player.x, player.y, player.y);
        } else {
            queuePerkSpriteWindow(buckets, assets, camera, kLevelRing,
                                  age, 0.0F, 0.36F, player.x, player.y, player.y);
        }
    }
}

PIXEL_TWINS_SRAM void drawPerkEffectOver(pixel_twins::RenderTarget target,
                                         const assets::GameAssets& assets,
                                         const CameraState& camera,
                                         const GameplayState& gameplay) noexcept {
    constexpr PerkSpriteSpec kHealCore{
        assets::SpriteAssetId::HealCore16x245fSheet, 5, 16, 24};
    constexpr PerkSpriteSpec kHpCore{
        assets::SpriteAssetId::HpUpCore20x206fSheet, 6, 20, 20};
    constexpr PerkSpriteSpec kHpMark{
        assets::SpriteAssetId::HpUpMark8x84fSheet, 4, 8, 8};
    constexpr PerkSpriteSpec kLevelCore{
        assets::SpriteAssetId::LevelUpCore24x246fSheet, 6, 24, 24};
    constexpr PerkSpriteSpec kLevelRay{
        assets::SpriteAssetId::LevelUpRay16x164f8dirSheet, 4, 16, 16};
    constexpr PerkSpriteSpec kRisingMote{
        assets::SpriteAssetId::CommonRisingMote6x84fSheet, 4, 6, 8};
    constexpr PerkSpriteSpec kWhiteSpark{
        assets::SpriteAssetId::CommonWhiteSpark6x64fSheet, 4, 6, 6};
    constexpr float kPi = 3.1415927F;
    for (const auto& effect : gameplay.perkEffects()) {
        if (!effect.active || effect.owner >= pixel_twins::kControllerCount) continue;
        const auto& player = gameplay.player(effect.owner);
        const auto age = static_cast<float>(effect.ageTicks) / 60.0F;
        const auto x = player.x;
        const auto y = player.y;
        if (effect.type == PerkEffectType::Heal) {
            drawPerkSpriteWindow(target, assets, camera, kHealCore,
                                 age, 0.02F, 0.48F, x, y - 14.0F);
            for (std::uint8_t index = 0; index < 6; ++index) {
                const auto start = 0.06F + static_cast<float>(index) * 0.055F;
                const auto progress = std::clamp((age - start) / 0.38F, 0.0F, 1.0F);
                if (progress <= 0.0F || progress >= 1.0F) continue;
                const auto moteX = x + static_cast<float>(static_cast<int>(index % 3U) - 1) * 7.0F
                    + std::sin(effect.seed + index * 1.8F + progress * 3.0F) * 2.0F;
                const auto moteY = y - 3.0F - progress * (24.0F + (index % 2U) * 6.0F);
                drawPerkSpriteProgress(target, assets, camera, kRisingMote,
                                       progress, moteX, moteY, 1);
            }
            drawPerkSpark(target, assets, camera, age, 0.14F, x - 9.0F, y - 18.0F);
            drawPerkSpark(target, assets, camera, age, 0.27F, x + 9.0F, y - 11.0F);
            drawPerkSpark(target, assets, camera, age, 0.38F, x, y - 29.0F);
        } else if (effect.type == PerkEffectType::HpUp) {
            drawPerkSpriteWindow(target, assets, camera, kHpCore,
                                 age, 0.03F, 0.54F, x, y - 15.0F);
            for (std::uint8_t index = 0; index < 4; ++index) {
                const auto start = 0.1F + static_cast<float>(index) * 0.06F;
                const auto progress = std::clamp((age - start) / 0.4F, 0.0F, 1.0F);
                if (progress <= 0.0F || progress >= 1.0F) continue;
                const auto side = index % 2U == 0U ? -1.0F : 1.0F;
                const auto markX = x + side * (6.0F + static_cast<float>(index / 2U) * 7.0F);
                const auto markY = y - 4.0F - progress * (22.0F + index * 2.0F);
                drawPerkSpriteProgress(target, assets, camera, kHpMark,
                                       progress, markX, markY, index % 2U);
            }
            for (std::uint8_t index = 0; index < 3; ++index) {
                const auto start = 0.18F + static_cast<float>(index) * 0.08F;
                const auto progress = std::clamp((age - start) / 0.34F, 0.0F, 1.0F);
                if (progress <= 0.0F || progress >= 1.0F) continue;
                drawPerkSpriteProgress(target, assets, camera, kRisingMote, progress,
                    x + static_cast<float>(static_cast<int>(index) - 1) * 8.0F,
                    y - 5.0F - progress * 24.0F, 2);
            }
            drawPerkSpark(target, assets, camera, age, 0.35F, x + 7.0F, y - 27.0F);
        } else if (effect.type == PerkEffectType::LevelUp) {
            drawPerkSpriteWindow(target, assets, camera, kLevelCore,
                                 age, 0.025F, 0.39F, x, y - 14.0F);
            for (std::uint8_t row = 0; row < 8; ++row) {
                const auto start = row % 2U == 0U ? 0.055F : 0.12F;
                drawPerkSpriteWindow(target, assets, camera, kLevelRay,
                                     age, start, start + 0.23F, x, y - 14.0F, row);
            }
            for (std::uint8_t index = 0; index < 4; ++index) {
                const auto start = 0.12F + static_cast<float>(index) * 0.035F;
                const auto progress = std::clamp((age - start) / 0.25F, 0.0F, 1.0F);
                if (progress <= 0.0F || progress >= 1.0F) continue;
                const auto angle = effect.seed + static_cast<float>(index) * kPi * 0.5F;
                drawPerkSpriteProgress(target, assets, camera, kWhiteSpark, progress,
                    x + std::cos(angle) * (7.0F + progress * 15.0F),
                    y - 14.0F + std::sin(angle) * (5.0F + progress * 10.0F));
            }
        } else {
            drawPerkSpriteWindow(target, assets, camera, kLevelCore,
                                 age, 0.025F, 0.4F, x, y - 14.0F);
            for (std::uint8_t index = 0; index < 5; ++index) {
                const auto start = 0.04F + static_cast<float>(index) * 0.055F;
                const auto progress = std::clamp((age - start) / 0.38F, 0.0F, 1.0F);
                if (progress <= 0.0F || progress >= 1.0F) continue;
                const auto moteX = x + static_cast<float>(static_cast<int>(index) - 2) * 6.0F
                    + std::sin(effect.seed + index * 1.7F + progress * 2.5F) * 2.0F;
                const auto moteY = y - 2.0F - progress * (24.0F + index % 2U * 7.0F);
                drawPerkSpriteProgress(target, assets, camera, kRisingMote,
                                       progress, moteX, moteY, 2);
            }
            drawPerkSpark(target, assets, camera, age, 0.16F, x - 10.0F, y - 18.0F);
            drawPerkSpark(target, assets, camera, age, 0.28F, x + 9.0F, y - 25.0F);
        }
    }
}

PIXEL_TWINS_SRAM void drawTitle(pixel_twins::Framebuffer& framebuffer,
               const assets::TitleAssets& title,
               std::uint32_t frame,
               Difficulty difficulty) noexcept {
    auto left = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Left);
    auto right = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Right);
    title.drawScreen(left);
    title.drawScreen(right);
    if (difficulty == Difficulty::Hard) {
        pixel_twins::drawText(right, assets::kWizwardFont, 132, 5, "HARD", 18, 6);
    }
    if ((frame / 30U) % 2U == 0U) {
        drawCenteredText(left, "PUSH ANY BUTTON", 80, 98);
        drawCenteredText(right, "PUSH ANY BUTTON", 80, 98);
    }
}

template<std::size_t Capacity, std::size_t ExCapacity>
PIXEL_TWINS_SRAM void drawGameplayPanel(pixel_twins::RenderTarget target,
                       const world::WorldMap& map,
                       const assets::GameAssets& assets,
                       const CameraState& cameraState,
                       const GameplayState& gameplay,
                       pixel_twins::SpriteBuckets<Capacity, ExCapacity>& spriteBuckets,
                       std::uint32_t frame,
                       std::size_t viewer,
                       bool showHud = true) noexcept {
    // The prototype rounds the camera once before drawing the world and every overlay.
    // Keep that shared pixel origin so fixed world details cannot drift by one pixel.
    const CameraState camera{std::round(cameraState.x), std::round(cameraState.y)};
    map.draw(target, assets.background(), static_cast<std::int32_t>(camera.x),
             static_cast<std::int32_t>(camera.y));
    drawXpRecallCircle(target, assets, camera, gameplay);
    drawActiveSeals(target, map, gameplay, camera);
    drawBossIntroShadow(target, gameplay, camera);
    spriteBuckets.reset();
    for (const auto& bullet : gameplay.bullets()) {
        if (!bullet.active) continue;
        auto asset = assets::SpriteAssetId::LightOrb88x83fSheet;
        std::int16_t halfSize = 4;
        std::uint8_t directionRow = 0;
        if (bullet.type == PlayerAttack::Fire) {
            asset = bullet.damage >= 19 ? assets::SpriteAssetId::Fireball1616x163fSheet
                                        : assets::SpriteAssetId::Fireball1212x123fSheet;
            halfSize = bullet.damage >= 19 ? 8 : 6;
        } else if (bullet.type == PlayerAttack::Ice) {
            asset = assets::SpriteAssetId::IceShard128dir12x128fSheet;
            halfSize = 6;
            directionRow = directionRow8(bullet.velocityX, bullet.velocityY);
        } else if (bullet.type == PlayerAttack::Familiar) {
            asset = assets::SpriteAssetId::FamiliarProjectile66x61fSheet;
            halfSize = 3;
        }
        queueAsset(spriteBuckets, assets, asset, frame / 6U,
                   static_cast<std::int16_t>(bullet.x - camera.x - halfSize),
                   static_cast<std::int16_t>(bullet.y - camera.y - 10.0F - halfSize),
                   bullet.y - camera.y, directionRow);
    }
    for (const auto& effect : gameplay.impactEffects()) {
        if (!effect.active || effect.lifetimeTicks == 0) continue;
        auto asset = assets::SpriteAssetId::ImpactBurst1616x163fSheet;
        std::uint8_t frameCount = 3;
        std::int16_t halfSize = 8;
        switch (effect.type) {
        case ImpactEffectType::CastSpark:
            asset = assets::SpriteAssetId::CastSpark1212x123fSheet;
            halfSize = 6;
            break;
        case ImpactEffectType::Light:
            asset = assets::SpriteAssetId::LightImpact1212x123fSheet;
            halfSize = 6;
            break;
        case ImpactEffectType::Fire:
            asset = assets::SpriteAssetId::FireImpact1616x163fSheet;
            break;
        case ImpactEffectType::Ice:
            asset = assets::SpriteAssetId::IceImpact1616x165fSheet;
            frameCount = 5;
            break;
        case ImpactEffectType::Familiar:
            asset = assets::SpriteAssetId::FamiliarImpact1212x123fSheet;
            halfSize = 6;
            break;
        case ImpactEffectType::Orb:
            asset = assets::SpriteAssetId::OrbImpact1212x123fSheet;
            halfSize = 6;
            break;
        case ImpactEffectType::Wind:
            asset = assets::SpriteAssetId::WindImpact1616x163fSheet;
            break;
        case ImpactEffectType::Generic:
            break;
        }
        const auto animationFrame = static_cast<std::uint32_t>(std::min<std::uint16_t>(
            static_cast<std::uint16_t>(frameCount - 1U),
            static_cast<std::uint16_t>(effect.ageTicks * frameCount / effect.lifetimeTicks)));
        queueAsset(spriteBuckets, assets, asset, animationFrame,
                   static_cast<std::int16_t>(effect.x - camera.x - halfSize),
                   static_cast<std::int16_t>(effect.y - camera.y - halfSize),
                   effect.y - camera.y);
    }
    for (const auto& gem : gameplay.xpGems()) {
        if (!gem.active) continue;
        const auto gemX = static_cast<std::int16_t>(gem.x - camera.x - 4.0F);
        const auto gemY = static_cast<std::int16_t>(gem.y - camera.y - 4.0F);
        constexpr float kSortMargin = 60.0F;
        const auto bucket = static_cast<std::uint16_t>(std::clamp(
            static_cast<std::int32_t>(gem.y - camera.y + kSortMargin), 0,
            static_cast<std::int32_t>(pixel_twins::kBucketCount - 1U)));
        pixel_twins::Sprite sprite{};
        if (assets.makeXpGemSprite(gem.owner, gemX, gemY, sprite)) {
            (void)spriteBuckets.addSprite(bucket, sprite);
        }
    }
    for (const auto& enemy : gameplay.enemies()) {
        if ((!enemy.active && enemy.deathTicks == 0) || enemy.spawnDelayTicks > 0) continue;
        const auto dying = !enemy.active && enemy.deathTicks > 0;
        auto drawEnemyY = enemy.y;
        if (enemy.kind == EnemyKind::Boss && gameplay.bossIntroTicks() > 0) {
            const auto elapsedTicks = static_cast<std::uint16_t>(183U - gameplay.bossIntroTicks());
            if (elapsedTicks < 54U) continue;
            const auto progress = std::clamp(static_cast<float>(elapsedTicks - 54U) / 21.0F, 0.0F, 1.0F);
            drawEnemyY = enemy.y - 132.0F * (1.0F - progress * progress * progress);
        }
        auto asset = assets::SpriteAssetId::SlimeEnemyWalk16x164fSmallerSheet;
        float anchorX = 8.0F;
        float anchorY = 12.0F;
        std::uint8_t spriteWidth = 16;
        std::uint8_t spriteHeight = 16;
        switch (enemy.kind) {
        case EnemyKind::Imp:
            if (dying) asset = assets::SpriteAssetId::SlimeEnemyDeath16x163fSmallerSheet;
            break;
        case EnemyKind::Bat:
            asset = dying ? assets::SpriteAssetId::BatEnemyLinelessDeath18x183fSheet
                          : assets::SpriteAssetId::BatEnemyLinelessWalk18x184fSheet;
            anchorX = 9.0F; anchorY = 10.0F; spriteWidth = 18; spriteHeight = 18; break;
        case EnemyKind::Skeleton:
            asset = dying ? assets::SpriteAssetId::SkeletonEnemyDeath24x243fSheet
                          : assets::SpriteAssetId::SkeletonEnemyWalk24x244fSheet;
            anchorX = 12.0F; anchorY = 20.0F; spriteWidth = 24; spriteHeight = 24; break;
        case EnemyKind::Golem:
            asset = dying ? assets::SpriteAssetId::GolemEnemyLinelessDeath24x243fSheet
                          : assets::SpriteAssetId::GolemEnemyLinelessWalk24x244fSheet;
            anchorX = 12.0F; anchorY = 20.0F; spriteWidth = 24; spriteHeight = 24; break;
        case EnemyKind::Archer:
            asset = dying ? assets::SpriteAssetId::GoblinArcherEnemyLinelessDeath16x163fSheet
                : enemy.attackAnimationTicks > 0
                ? assets::SpriteAssetId::GoblinArcherEnemyLinelessShoot16x164fSheet
                : assets::SpriteAssetId::GoblinArcherEnemyLinelessWalk16x164fSheet;
            anchorX = 8.0F; anchorY = 13.0F; break;
        case EnemyKind::Wisp:
            asset = dying ? assets::SpriteAssetId::WispEnemyLinelessDeath18x183fSheet
                          : assets::SpriteAssetId::WispEnemyLinelessWalk18x184fSheet;
            anchorX = 9.0F; anchorY = 10.0F; spriteWidth = 18; spriteHeight = 18; break;
        case EnemyKind::Mage:
            asset = dying ? assets::SpriteAssetId::MonsterMageEnemyLinelessDeath18x183fSheet
                          : assets::SpriteAssetId::MonsterMageEnemyLinelessWalk18x184fSheet;
            anchorX = 9.0F; anchorY = 15.0F; spriteWidth = 18; spriteHeight = 18; break;
        case EnemyKind::Boss:
            asset = assets::SpriteAssetId::SealedStoneGuardianBossWalk48x483fSheet;
            anchorX = 24.0F; anchorY = 42.0F; spriteWidth = 48; spriteHeight = 48; break;
        }
        const auto animationFrame = dying
            ? static_cast<std::uint32_t>(std::min<std::uint16_t>(2U,
                static_cast<std::uint16_t>((16U - enemy.deathTicks) * 3U / 16U)))
            : enemy.attackAnimationTicks > 0
            ? static_cast<std::uint32_t>((20U - enemy.attackAnimationTicks) / 5U)
            : enemy.moving || enemy.kind == EnemyKind::Boss ? frame / 8U : 0U;
        if (enemy.active && enemy.bornTicks > 0) {
            const auto progress = std::clamp(1.0F - static_cast<float>(enemy.bornTicks) / 23.0F,
                                             0.0F, 1.0F);
            const auto footX = static_cast<std::int16_t>(std::round(enemy.x - camera.x));
            const auto footY = static_cast<std::int16_t>(std::round(drawEnemyY - camera.y));
            const auto shadowWidth = static_cast<std::uint16_t>(std::max(2.0F, std::round(2.0F + progress * 8.0F)));
            pixel_twins::fillRectangle(target,
                static_cast<std::int16_t>(footX - static_cast<std::int16_t>(shadowWidth / 2U)),
                footY, shadowWidth, 2, 27);
            const auto smooth = [](float value) noexcept {
                const auto t = std::clamp(value, 0.0F, 1.0F);
                return t * t * (3.0F - 2.0F * t);
            };
            if (progress < 0.18F) {
                const auto lineHeight = static_cast<std::int16_t>(std::round(
                    8.0F + static_cast<float>(spriteHeight) * smooth(progress / 0.18F)));
                pixel_twins::fillRectangle(target, static_cast<std::int16_t>(footX - 1),
                                            static_cast<std::int16_t>(footY - lineHeight), 3,
                                            static_cast<std::uint16_t>(lineHeight), 17);
                pixel_twins::fillRectangle(target, footX,
                                            static_cast<std::int16_t>(footY - lineHeight - 2), 1,
                                            static_cast<std::uint16_t>(lineHeight + 3), 13);
                continue;
            }
            float scaleX = 1.0F;
            float scaleY = 1.0F;
            if (progress < 0.54F) {
                const auto p = smooth((progress - 0.18F) / 0.36F);
                scaleX = 0.08F + (0.72F - 0.08F) * p;
                scaleY = 1.5F + (1.12F - 1.5F) * p;
            } else if (progress < 0.76F) {
                const auto p = smooth((progress - 0.54F) / 0.22F);
                scaleX = 0.72F + (1.28F - 0.72F) * p;
                scaleY = 1.12F + (0.78F - 1.12F) * p;
            } else {
                const auto p = smooth((progress - 0.76F) / 0.24F);
                scaleX = 1.28F + (1.0F - 1.28F) * p;
                scaleY = 0.78F + (1.0F - 0.78F) * p;
            }
            const auto width = static_cast<std::uint8_t>(std::max(1.0F,
                std::round(static_cast<float>(spriteWidth) * scaleX)));
            const auto height = static_cast<std::uint8_t>(std::max(1.0F,
                std::round(static_cast<float>(spriteHeight) * scaleY)));
            queueScaledAsset(spriteBuckets, assets, asset, animationFrame,
                fourDirectionRow(enemy.facing),
                static_cast<std::int16_t>(std::round(enemy.x - camera.x - anchorX * scaleX)),
                static_cast<std::int16_t>(std::round(drawEnemyY - camera.y - anchorY * scaleY)),
                width, height, spriteWidth, spriteHeight, drawEnemyY - camera.y);
            continue;
        }
        queueAsset(spriteBuckets, assets, asset, animationFrame,
                   static_cast<std::int16_t>(enemy.x - camera.x - anchorX),
                   static_cast<std::int16_t>(drawEnemyY - camera.y - anchorY), drawEnemyY - camera.y,
                   fourDirectionRow(enemy.facing));
    }
    for (const auto& bullet : gameplay.enemyBullets()) {
        if (!bullet.active || bullet.launchDelayTicks > 0) continue;
        if (bullet.type == EnemyBulletType::Arrow) {
            auto angle = std::atan2(bullet.velocityY, bullet.velocityX);
            if (angle < 0.0F) angle += 6.2831853F;
            const auto directionFrame = static_cast<std::uint32_t>(
                static_cast<unsigned>(std::round(angle / (6.2831853F / 16.0F))) % 16U);
            queueAsset(spriteBuckets, assets, assets::SpriteAssetId::ArcherArrow12x416dir12x1216fSheet,
                       directionFrame,
                       static_cast<std::int16_t>(bullet.x - camera.x - 6.0F),
                       static_cast<std::int16_t>(bullet.y - camera.y - 6.0F), bullet.y - camera.y);
        } else if (bullet.type == EnemyBulletType::BossFire) {
            queueAsset(spriteBuckets, assets, assets::SpriteAssetId::BossBlueFireball1616x163fSheet,
                       frame / 7U,
                       static_cast<std::int16_t>(bullet.x - camera.x - 8.0F),
                       static_cast<std::int16_t>(bullet.y - camera.y - 8.0F), bullet.y - camera.y);
        } else {
            queueAsset(spriteBuckets, assets, assets::SpriteAssetId::EnemyMagicOrb88x83fSheet,
                       frame / 7U,
                       static_cast<std::int16_t>(bullet.x - camera.x - 4.0F),
                       static_cast<std::int16_t>(bullet.y - camera.y - 4.0F), bullet.y - camera.y);
        }
    }
    for (const auto& slash : gameplay.windSlashes()) {
        if (!slash.active || slash.owner >= pixel_twins::kControllerCount) continue;
        const auto& owner = gameplay.player(slash.owner);
        const auto sweep = static_cast<float>(slash.ageTicks) / 28.0F * 2.35F * 3.1415927F;
        const auto radius = (slash.innerRadius + slash.outerRadius) * 0.5F;
        for (std::uint8_t blade = 0; blade < slash.bladeCount; ++blade) {
            const auto angle = slash.startAngle + static_cast<float>(blade) * 6.2831853F
                / slash.bladeCount + sweep;
            const auto x = owner.x + std::cos(angle) * radius;
            const auto y = owner.y + std::sin(angle) * radius;
            const auto animationFrame = static_cast<std::uint32_t>(7U - std::min<std::uint16_t>(7U, slash.ageTicks / 2U));
            queueAsset(spriteBuckets, assets, assets::SpriteAssetId::WindSlash1616x168fSheet,
                       animationFrame,
                       static_cast<std::int16_t>(x - camera.x - 8.0F),
                       static_cast<std::int16_t>(y - camera.y - 18.0F), y - camera.y);
        }
    }
    constexpr std::array<std::array<std::int16_t, 4>, 3> kThunderConnections{{
        {{14, 0, 10, 59}}, {{10, 0, 12, 59}}, {{12, 0, 13, 59}},
    }};
    for (const auto& strike : gameplay.thunderStrikes()) {
        if (!strike.active) continue;
        auto connectX = strike.x - camera.x;
        auto connectY = strike.y - camera.y;
        for (std::uint8_t segment = 0; segment < 2; ++segment) {
            const auto animationFrame = static_cast<std::uint8_t>((strike.ageTicks / 3U + segment) % 3U);
            const auto& anchors = kThunderConnections[animationFrame];
            const auto drawX = static_cast<std::int16_t>(std::round(connectX - anchors[2]));
            const auto drawY = static_cast<std::int16_t>(std::round(connectY - anchors[3]));
            queueAsset(spriteBuckets, assets, assets::SpriteAssetId::ThunderPillar24x603fSheet,
                       animationFrame, drawX, drawY, strike.y - camera.y);
            connectX = static_cast<float>(drawX + anchors[0]);
            connectY = static_cast<float>(drawY + anchors[1]);
        }
        constexpr std::uint16_t kThunderImpactTicks = 10;
        if (strike.ageTicks < kThunderImpactTicks) {
            const auto impactFrame = static_cast<std::uint32_t>(std::min<std::uint16_t>(
                2U, static_cast<std::uint16_t>(strike.ageTicks * 3U / kThunderImpactTicks)));
            queueAsset(spriteBuckets, assets, assets::SpriteAssetId::ThunderImpact1616x163fSheet,
                       impactFrame,
                       static_cast<std::int16_t>(std::round(strike.x - camera.x - 8.0F)),
                       static_cast<std::int16_t>(std::round(strike.y - camera.y - 8.0F)),
                       strike.y - camera.y);
        }
    }
    queuePerkEffectUnder(spriteBuckets, assets, camera, gameplay);
    for (std::size_t playerIndex = 0; playerIndex < pixel_twins::kControllerCount; ++playerIndex) {
        const auto& player = gameplay.player(playerIndex);
        if (player.hp <= 0) continue;
        for (const auto& familiar : player.familiars) {
            if (!familiar.active) continue;
            queueAsset(spriteBuckets, assets, assets::SpriteAssetId::FamiliarPink1616x1620fSheet,
                       frame / 7U,
                       static_cast<std::int16_t>(familiar.x - camera.x - 8.0F),
                       static_cast<std::int16_t>(familiar.y - camera.y - 11.0F),
                       familiar.y - camera.y, fourDirectionRow(familiar.facing));
        }
    }
    for (std::size_t playerIndex = 0; playerIndex < pixel_twins::kControllerCount; ++playerIndex) {
        const auto& player = gameplay.player(playerIndex);
        if (player.orbLevel == 0) continue;
        const auto points = static_cast<unsigned>(player.orbLevel - 1U);
        const auto count = static_cast<std::uint8_t>(1U + (points + 2U) / 3U);
        const auto radius = 22.0F + static_cast<float>(points / 3U) * 4.0F;
        for (std::uint8_t orb = 0; orb < count; ++orb) {
            const auto angle = player.orbAngle + static_cast<float>(orb) * 6.2831853F / count;
            const auto x = player.x + std::cos(angle) * radius;
            const auto y = player.y + std::sin(angle) * radius;
            queueAsset(spriteBuckets, assets, assets::SpriteAssetId::OrbSatellite88x83fSheet,
                       frame / 7U,
                       static_cast<std::int16_t>(x - camera.x - 4.0F),
                       static_cast<std::int16_t>(y - camera.y - 14.0F), y - camera.y);
        }
    }
    for (std::size_t index = 0; index < pixel_twins::kControllerCount; ++index) {
        const auto& player = gameplay.player(index);
        if (player.invulnerabilityTicks > 0 && (player.invulnerabilityTicks / 3U) % 2U != 0U) continue;
        const auto downed = player.hp <= 0;
        const auto animationFrame = downed ? 0U : frame / (player.moving ? 7U : 10U);
        const auto anchorX = downed ? (index == 0 ? 14.0F : 16.0F)
                                         : (index == 0 ? 12.0F : 14.0F);
        constexpr float kPlayerAnchorY = 30.0F;
        const auto screenX = static_cast<std::int16_t>(player.x - camera.x - anchorX);
        const auto screenY = static_cast<std::int16_t>(player.y - camera.y - kPlayerAnchorY);
        const auto directionRow = static_cast<std::uint8_t>(player.facing);
        const auto asset = index == 0
            ? (downed ? assets::SpriteAssetId::GirlMageDowned28x321fSheet
                : (player.moving ? assets::SpriteAssetId::GirlMageWalk24x324fSheet
                                 : assets::SpriteAssetId::GirlMageIdle24x324fSheet))
            : (downed ? assets::SpriteAssetId::BoyMageDowned32x321fSheet
                : (player.moving ? assets::SpriteAssetId::BoyMageWalkStaff28x326fSheet
                                 : assets::SpriteAssetId::BoyMageIdleStaff28x324fSheet));
        queueAsset(spriteBuckets, assets, asset, animationFrame, screenX, screenY,
                   player.y - camera.y, directionRow);
    }
    spriteBuckets.draw(target);
    drawPerkEffectOver(target, assets, camera, gameplay);
    drawBossIntroOverlay(target, gameplay, camera);
    drawClearSequence(target, assets, gameplay, camera, frame);
    for (std::size_t playerIndex = 0; playerIndex < pixel_twins::kControllerCount; ++playerIndex) {
        const auto& player = gameplay.player(playerIndex);
        if (player.bombEffectTicks == 0) continue;
        constexpr PerkSpriteSpec kBombCore{
            assets::SpriteAssetId::BombCoreWave64x487fSheet, 7, 64, 48};
        constexpr PerkSpriteSpec kBombRay{
            assets::SpriteAssetId::BombRay16x161f8dirSheet, 1, 16, 16};
        constexpr PerkSpriteSpec kBombFragment{
            assets::SpriteAssetId::BombFragment8x84fSheet, 4, 8, 8};
        constexpr float kTau = 6.2831853F;
        constexpr float kPi = 3.1415927F;
        const auto age = static_cast<float>(34U - player.bombEffectTicks) / 60.0F;
        drawPerkSpriteWindow(target, assets, camera, kBombCore,
                             age, 0.035F, 0.49F,
                             player.bombEffectX, player.bombEffectY - 12.0F);
        for (std::uint8_t row = 0; row < 8; ++row) {
            const auto start = row % 2U == 0U ? 0.0F : 0.045F;
            const auto progress = std::clamp((age - start) / 0.42F, 0.0F, 1.0F);
            if (progress <= 0.0F || progress >= 1.0F) continue;
            const auto angle = static_cast<float>(row) * kPi / 4.0F;
            const auto travel = smoothStep(progress) * 96.0F;
            const auto x = player.bombEffectX + std::cos(angle) * travel;
            const auto y = player.bombEffectY - 12.0F + std::sin(angle) * travel;
            drawPerkSpriteProgress(target, assets, camera, kBombRay,
                                   progress, x, y, row);
        }
        for (std::uint8_t fragment = 0; fragment < 12; ++fragment) {
            const auto delay = 0.08F + static_cast<float>(fragment % 3U) * 0.025F;
            const auto progress = std::clamp((age - delay) / (0.52F - delay), 0.0F, 1.0F);
            if (progress <= 0.0F || progress >= 1.0F) continue;
            const auto angle = static_cast<float>(fragment) / 12.0F * kTau
                + (bombEffectRandom(player.bombEffectSeed, fragment, 0) - 0.5F) * 0.16F;
            const auto distance = 70.0F
                + bombEffectRandom(player.bombEffectSeed, fragment, 1) * 26.0F;
            const auto travel = smoothStep(progress) * distance;
            const auto x = player.bombEffectX + std::cos(angle) * travel;
            const auto y = player.bombEffectY - 10.0F
                + std::sin(angle) * travel * 0.56F - std::sin(progress * kPi) * 9.0F;
            drawPerkSpriteProgress(target, assets, camera, kBombFragment,
                                   progress, x, y, fragment % 3U);
        }
    }
    for (const auto& strike : gameplay.thunderStrikes()) {
        if (!strike.active) continue;
        pixel_twins::drawCircle(target,
            static_cast<std::int16_t>(std::round(strike.x - camera.x)),
            static_cast<std::int16_t>(std::round(strike.y - camera.y)),
            thunderShockwaveRadius(strike.ageTicks), 4);
    }
    if (!showHud) return;
    const auto& viewedPlayer = gameplay.player(viewer);
    const auto maxHpWidth = static_cast<std::uint16_t>(std::clamp<std::int16_t>(viewedPlayer.maxHp, 1, 60));
    const auto hpWidth = static_cast<std::uint16_t>(
        std::clamp<std::int16_t>(viewedPlayer.hp, 0, static_cast<std::int16_t>(maxHpWidth)));
    pixel_twins::fillRectangle(target, 49, 4, static_cast<std::uint16_t>(maxHpWidth + 2U), 4, 28);
    if (hpWidth > 0) pixel_twins::fillRectangle(target, 50, 5, hpWidth, 2, 9);
    pixel_twins::fillRectangle(target, 49, 7, 62, 4, 28);
    const auto xpNeed = gameplay.xpNeeded(viewedPlayer.level);
    const auto xpWidth = static_cast<std::uint16_t>(viewedPlayer.xp * 60U / xpNeed);
    if (xpWidth > 0) pixel_twins::fillRectangle(target, 50, 8, xpWidth, 2, 20);
    char scoreBuffer[12]{};
    drawRightAlignedText(target, formatUnsigned(gameplay.score(viewer), scoreBuffer), 155, 5);
    drawTimer(target, gameplay.elapsedTicks());
    if (const auto* boss = gameplay.boss()) {
        constexpr std::int16_t kBossBarX = 42;
        constexpr std::int16_t kBossBarY = 14;
        constexpr std::uint16_t kBossBarWidth = 78;
        pixel_twins::fillRectangle(target, kBossBarX, kBossBarY, kBossBarWidth, 6, 28);
        pixel_twins::fillRectangle(target, kBossBarX + 1, kBossBarY + 1, kBossBarWidth - 2U, 4, 34);
        const auto fill = static_cast<std::uint16_t>(std::clamp<std::int32_t>(
            static_cast<std::int32_t>(kBossBarWidth - 2U) * boss->hp / std::max<std::int16_t>(1, boss->maxHp),
            0, kBossBarWidth - 2U));
        if (fill > 0) pixel_twins::fillRectangle(target, kBossBarX + 1, kBossBarY + 1, fill, 4, 15);
    }
    if (gameplay.sealNoticeTicks() > 0) {
        char sealText[] = "SEAL 0/3";
        sealText[5] = static_cast<char>('0' + gameplay.activeSealCount());
        drawCenteredText(target, sealText, 80, 38);
    } else if (!gameplay.playerIsManual(viewer) && (gameplay.elapsedTicks() / 30U) % 2U == 0U) {
        drawCenteredText(target, "PUSH BUTTON TO JOIN", 80, 39);
    }
    drawWeaponLevels(target, assets, viewedPlayer);
    drawPerkChoices(target, assets, viewedPlayer, viewer);
    drawMiniMap(target, map, gameplay, viewer);
    drawOffscreenPartnerArrow(target, gameplay, camera, viewer);
}

std::string_view outcomeText(GameplayOutcome outcome) noexcept {
    if (outcome == GameplayOutcome::Down) return "GAME OVER";
    if (outcome == GameplayOutcome::TimeUp) return "TIME'S UP";
    if (outcome == GameplayOutcome::Clear) return "CONGRATULATIONS!";
    return "RESULT";
}

struct ResultRankingRow {
    std::array<char, 3> name{};
    std::uint32_t score = 0;
    std::uint8_t player = 0;
    bool pending = false;
    bool hard = false;
};

PIXEL_TWINS_SRAM void drawResultPanel(
    pixel_twins::RenderTarget target,
    const GameplayState& gameplay,
    GameplayOutcome outcome,
    std::uint32_t resultTicks,
    std::uint16_t continueTicks,
    std::size_t viewer,
    const std::array<std::uint32_t, pixel_twins::kControllerCount>& timeBonuses,
    const std::array<std::uint32_t, pixel_twins::kControllerCount>& finalScores,
    const std::array<RankingRecord, kRankingLimit>& rankings,
    std::size_t rankingCount,
    const std::array<RankingEntry, pixel_twins::kControllerCount>& entries) noexcept {
    const auto title = outcomeText(outcome);
    if (resultTicks < kRankingResultDelayTicks) {
        drawCenteredText(target, title, 80, 82);
        return;
    }
    drawCenteredText(target, title, 80, 5);
    char playerText[] = "P1 RESULT";
    playerText[1] = static_cast<char>('1' + viewer);
    pixel_twins::drawText(target, assets::kWizwardFont, 5, 20, playerText,
                          viewer == 0 ? 19 : 20, 6);
    pixel_twins::drawText(target, assets::kWizwardFont, 5, 32, "SCORE", 18, 6);
    pixel_twins::drawText(target, assets::kWizwardFont, 5, 44, "TIME", 18, 6);
    pixel_twins::drawText(target, assets::kWizwardFont, 5, 56, "TOTAL", 18, 6);
    char scoreText[12]{};
    char bonusText[12]{};
    char totalText[12]{};
    drawRightAlignedText(target, formatUnsigned(gameplay.score(viewer), scoreText), 78, 32);
    drawRightAlignedText(target, formatUnsigned(timeBonuses[viewer], bonusText), 78, 44);
    pixel_twins::drawText(target, assets::kWizwardFont, 42, 44, "+", 18, 6);
    drawRightAlignedText(target, formatUnsigned(finalScores[viewer], totalText), 78, 56);

    std::array<ResultRankingRow, kRankingLimit + pixel_twins::kControllerCount> board{};
    std::size_t boardCount = 0;
    for (std::size_t index = 0; index < rankingCount; ++index) {
        board[boardCount++] = {rankings[index].name, rankings[index].score,
                               rankings[index].player, false, rankings[index].hard};
    }
    for (std::size_t player = 0; player < entries.size(); ++player) {
        if (!entries[player].active || entries[player].submitted) continue;
        board[boardCount++] = {entries[player].name, finalScores[player],
                               static_cast<std::uint8_t>(player), true,
                               gameplay.difficulty() == Difficulty::Hard};
    }
    for (std::size_t index = 1; index < boardCount; ++index) {
        const auto row = board[index];
        auto destination = index;
        while (destination > 0 && board[destination - 1U].score < row.score) {
            board[destination] = board[destination - 1U];
            --destination;
        }
        board[destination] = row;
    }
    auto focusRank = entries[viewer].active ? static_cast<std::size_t>(entries[viewer].rank) : 0U;
    for (std::size_t index = 0; index < boardCount; ++index) {
        if (board[index].pending && board[index].player == viewer) focusRank = index;
    }
    const auto rowCount = std::min<std::size_t>(5, boardCount);
    const auto startRank = rowCount == 0 ? 0U
        : std::min(focusRank > 2 ? focusRank - 2U : 0U, boardCount - rowCount);
    pixel_twins::drawText(target, assets::kWizwardFont, 91, 20, "RANK", 25, 6);
    for (std::size_t rowIndex = 0; rowIndex < rowCount; ++rowIndex) {
        const auto rank = startRank + rowIndex;
        const auto y = static_cast<std::int16_t>(32 + rowIndex * 10U);
        char rowText[16]{};
        rowText[0] = static_cast<char>('0' + ((rank + 1U) / 10U) % 10U);
        rowText[1] = static_cast<char>('0' + (rank + 1U) % 10U);
        rowText[2] = board[rank].hard ? 'H' : '.';
        rowText[3] = board[rank].name[0];
        rowText[4] = board[rank].name[1];
        rowText[5] = board[rank].name[2];
        rowText[6] = ' ';
        char rankScore[12]{};
        auto scoreValue = board[rank].score;
        const auto abbreviated = scoreValue >= 100000U;
        if (abbreviated) scoreValue /= 1000U;
        const auto score = formatUnsigned(scoreValue, rankScore);
        for (std::size_t index = 0; index < score.size(); ++index) {
            rowText[7U + index] = score[index];
        }
        auto rowLength = 7U + score.size();
        if (abbreviated) rowText[rowLength++] = 'K';
        const auto color = static_cast<std::uint8_t>(
            rank == focusRank && entries[viewer].active ? 15 : 18);
        pixel_twins::drawText(target, assets::kWizwardFont, 84, y,
                              std::string_view(rowText, rowLength), color, 6);
    }
    const auto& entry = entries[viewer];
    if (entry.active && !entry.submitted) {
        char rankLabel[] = "RANK #01";
        rankLabel[6] = static_cast<char>('0' + ((entry.rank + 1U) / 10U) % 10U);
        rankLabel[7] = static_cast<char>('0' + (entry.rank + 1U) % 10U);
        pixel_twins::drawText(target, assets::kWizwardFont, 5, 68, rankLabel, 25, 6);
        if ((resultTicks / 20U) % 2U == 0U) drawCenteredText(target, "ENTER YOUR NAME", 80, 82);
        drawCenteredText(target, std::string_view(entry.name.data(), 3), 80, 96, 8);
        pixel_twins::drawText(target, assets::kWizwardFont,
                              static_cast<std::int16_t>(68 + entry.cursor * 8U), 106, "^", 18, 6);
    } else if (outcome == GameplayOutcome::Clear
               && continueTicks >= kResultContinueDelayTicks
               && (resultTicks / 30U) % 2U == 0U) {
        drawCenteredText(target, "PUSH ANY BUTTON", 80, 104);
    }
}

} // namespace

bool Game::initialize(Scene initialScene, std::uint32_t mapSeed,
                      Difficulty difficulty) noexcept {
    world::MapGenerator mapGenerator;
    mapSeedState_ = mapSeed != 0U ? mapSeed : 1U;
    difficulty_ = difficulty;
    if (!gameAssets_.initialize() || !titleAssets_.initialize()
        || !mapGenerator.generate(mapSeedState_, gameAssets_.background(), terrainWorkspace_, worldMap_)) {
        return false;
    }
    gameplay_.reset(worldMap_, startingPlayer_, difficulty_);
    scene_ = initialScene;
    paused_ = false;
    return scene_ == Scene::Title ? titleAssets_.applyPalette(framebuffer_)
                                  : gameAssets_.applyPalette(framebuffer_);
}

UpdateResult Game::changeScene(Scene scene, bool playStartSfx) noexcept {
    scene_ = scene;
    sceneFrame_ = 0;
    paused_ = false;
    resultContinueTicks_ = 0;
    nameEntryBgmStarted_ = false;
    if (scene_ == Scene::Gameplay) {
        mapSeedState_ = mapSeedState_ * 1664525U + 1013904223U;
        world::MapGenerator mapGenerator;
        if (!mapGenerator.generate(mapSeedState_, gameAssets_.background(),
                                   terrainWorkspace_, worldMap_)) {
            return {AudioEvent::StopBgm, playStartSfx, false};
        }
        gameplay_.reset(worldMap_, startingPlayer_, difficulty_);
        resultOutcome_ = GameplayOutcome::Running;
    }
    const bool paletteApplied = scene_ == Scene::Title
        ? titleAssets_.applyPalette(framebuffer_)
        : gameAssets_.applyPalette(framebuffer_);
    AudioEvent audio = AudioEvent::StopBgm;
    if (scene_ == Scene::Gameplay) audio = AudioEvent::PlayField;
    if (scene_ == Scene::Result && resultOutcome_ == GameplayOutcome::Clear) {
        audio = AudioEvent::PlayVictory;
    }
    return {audio, playStartSfx, paletteApplied};
}

UpdateResult Game::processInput(const pixel_twins::Controllers& controllers) noexcept {
    if (scene_ == Scene::Title) {
        for (std::uint8_t index = 0; index < pixel_twins::kControllerCount; ++index) {
            if (controllers[index].pressed == 0) continue;
            startingPlayer_ = index;
            return changeScene(Scene::Gameplay, true);
        }
        return {};
    }
    if (scene_ == Scene::Result) {
        if (sceneFrame_ < kRankingResultDelayTicks) return {};
        const auto hadPendingRanking = hasPendingRanking();
        updateRankingInput(controllers);
        if (hadPendingRanking) {
            for (std::size_t index = 0; index < pixel_twins::kControllerCount; ++index) {
                if (controllers[index].pressed == 0) continue;
                UpdateResult result{};
                result.sfxCues[0] = {SfxId::UiMove, index == 0 ? -0.32F : 0.32F};
                result.sfxCueCount = 1;
                return result;
            }
        }
        if (hasPendingRanking() || resultOutcome_ != GameplayOutcome::Clear
            || resultContinueTicks_ < kResultContinueDelayTicks) return {};
        for (std::size_t index = 0; index < pixel_twins::kControllerCount; ++index) {
            if (controllers[index].pressed != 0) return changeScene(Scene::Title, false);
        }
        return {};
    }
    if (gameplay_.bossIntroTicks() > 0) return {};
    for (std::size_t index = 0; index < pixel_twins::kControllerCount; ++index) {
        if (!gameplay_.playerIsManual(index)
            || !controllers[index].isPressed(pixel_twins::ControllerButton::start)) continue;
        paused_ = !paused_;
        UpdateResult result{};
        result.sfxCues[0] = {SfxId::UiMove, index == 0 ? -0.32F : 0.32F};
        result.sfxCueCount = 1;
        return result;
    }
    return {};
}

UpdateResult Game::tick(const pixel_twins::Controllers& controllers) noexcept {
    if (scene_ == Scene::Gameplay && !paused_) {
        const auto bossIntroBefore = gameplay_.bossIntroTicks();
        const auto clearBefore = gameplay_.clearSequenceActive();
        gameplay_.tick(controllers, worldMap_);
        UpdateResult tickResult{};
        copySfxCues(gameplay_, tickResult);
        if (bossIntroBefore == 0 && gameplay_.bossIntroTicks() > 0) {
            tickResult.audio = AudioEvent::StopBgm;
        } else if (bossIntroBefore > 0 && gameplay_.bossIntroTicks() == 0) {
            tickResult.audio = AudioEvent::PlayBoss;
        }
        if (!clearBefore && gameplay_.clearSequenceActive()) tickResult.audio = AudioEvent::StopBgm;
        for (std::uint8_t index = 0; index < worldMap_.seals.size(); ++index) {
            if (gameplay_.seal(index).active) {
                (void)worldMap_.activateSeal(index, gameAssets_.background());
            }
        }
        if (gameplay_.outcome() != GameplayOutcome::Running) {
            resultOutcome_ = gameplay_.outcome();
            finalizeResult();
            auto result = changeScene(Scene::Result, false);
            copySfxCues(gameplay_, result);
            ++frame_;
            ++sceneFrame_;
            return result;
        }
        ++frame_;
        ++sceneFrame_;
        return tickResult;
    } else if (scene_ == Scene::Result && sceneFrame_ >= kRankingResultDelayTicks) {
        if (resultOutcome_ != GameplayOutcome::Clear
            && hasPendingRanking() && !nameEntryBgmStarted_) {
            nameEntryBgmStarted_ = true;
            ++frame_;
            ++sceneFrame_;
            return {AudioEvent::PlayNameEntry};
        }
        if (hasPendingRanking()
            && sceneFrame_ >= kRankingResultDelayTicks + kRankingInputTimeoutTicks) {
            for (std::size_t player = 0; player < rankingEntries_.size(); ++player) {
                if (rankingEntries_[player].active && !rankingEntries_[player].submitted) {
                    submitRanking(player);
                }
            }
        }
        if (hasPendingRanking()) {
            resultContinueTicks_ = 0;
        } else if (resultContinueTicks_ < kResultAutoReturnTicks) {
            ++resultContinueTicks_;
        }
        if (resultOutcome_ != GameplayOutcome::Clear
            && resultContinueTicks_ >= kResultAutoReturnTicks) {
            auto result = changeScene(Scene::Title, false);
            ++frame_;
            ++sceneFrame_;
            return result;
        }
    }
    ++frame_;
    ++sceneFrame_;
    return {};
}

void Game::finalizeResult() noexcept {
    const auto remainingTicks = gameplay_.elapsedTicks() < 300U * 60U
        ? 300U * 60U - gameplay_.elapsedTicks() : 0U;
    const auto remainingSeconds = (remainingTicks + 59U) / 60U;
    const auto bonus = resultOutcome_ == GameplayOutcome::Clear
        ? remainingSeconds * kTimeBonusPerSecond : 0U;
    rankingEntries_.fill({});
    for (std::size_t player = 0; player < finalScores_.size(); ++player) {
        timeBonuses_[player] = bonus;
        finalScores_[player] = gameplay_.score(player) + bonus;
    }
    for (std::size_t player = 0; player < finalScores_.size(); ++player) {
        if (finalScores_[player] == 0) continue;
        std::size_t rank = 0;
        for (std::size_t index = 0; index < rankingCount_; ++index) {
            if (rankings_[index].score >= finalScores_[player]) ++rank;
        }
        for (std::size_t other = 0; other < player; ++other) {
            if (finalScores_[other] >= finalScores_[player]) ++rank;
        }
        for (std::size_t other = player + 1U; other < finalScores_.size(); ++other) {
            if (finalScores_[other] > finalScores_[player]) ++rank;
        }
        if (rank >= kRankingLimit) continue;
        rankingEntries_[player].active = true;
        rankingEntries_[player].rank = static_cast<std::uint8_t>(rank);
    }
}

bool Game::hasPendingRanking() const noexcept {
    return std::any_of(rankingEntries_.begin(), rankingEntries_.end(),
        [](const RankingEntry& entry) { return entry.active && !entry.submitted; });
}

void Game::submitRanking(std::size_t player) noexcept {
    auto& entry = rankingEntries_[player];
    if (!entry.active || entry.submitted) return;
    RankingRecord record{};
    record.name = entry.name;
    record.score = finalScores_[player];
    record.timeBonus = timeBonuses_[player];
    record.player = static_cast<std::uint8_t>(player);
    record.cleared = resultOutcome_ == GameplayOutcome::Clear;
    record.hard = difficulty_ == Difficulty::Hard;
    auto insertAt = std::size_t{0};
    while (insertAt < rankingCount_ && rankings_[insertAt].score >= record.score) ++insertAt;
    const auto newCount = std::min(kRankingLimit, rankingCount_ + 1U);
    for (auto index = newCount; index > insertAt + 1U; --index) {
        rankings_[index - 1U] = rankings_[index - 2U];
    }
    if (insertAt < kRankingLimit) rankings_[insertAt] = record;
    rankingCount_ = newCount;
    entry.submitted = true;
}

void Game::updateRankingInput(const pixel_twins::Controllers& controllers) noexcept {
    using pixel_twins::ControllerButton;
    for (std::size_t player = 0; player < rankingEntries_.size(); ++player) {
        auto& entry = rankingEntries_[player];
        if (!entry.active || entry.submitted) continue;
        const auto& controller = controllers[player];
        const auto rotate = [&](std::int32_t delta) noexcept {
            const auto current = std::string_view(kRankingCharacters).find(entry.name[entry.cursor]);
            const auto index = current == std::string_view::npos ? std::size_t{0} : current;
            const auto count = static_cast<std::int32_t>(kRankingCharacters.size());
            const auto next = (static_cast<std::int32_t>(index) + delta + count) % count;
            entry.name[entry.cursor] = kRankingCharacters[static_cast<std::size_t>(next)];
        };
        if (controller.isPressed(ControllerButton::dpadLeft)) rotate(-1);
        if (controller.isPressed(ControllerButton::dpadRight)) rotate(1);
        if (controller.isPressed(ControllerButton::choiceLeft)
            || controller.isPressed(ControllerButton::choiceUp)) {
            if (entry.cursor > 0) --entry.cursor;
        }
        if (controller.isPressed(ControllerButton::choiceRight)
            || controller.isPressed(ControllerButton::choiceDown)) {
            if (entry.cursor < 2) ++entry.cursor;
            else submitRanking(player);
        }
    }
}

void Game::render() noexcept {
    if (scene_ == Scene::Title) {
        drawTitle(framebuffer_, titleAssets_, frame_, difficulty_);
    } else if (scene_ == Scene::Gameplay) {
        const auto left = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Left);
        const auto right = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Right);
        drawGameplayPanel(left, worldMap_, gameAssets_, gameplay_.camera(0), gameplay_,
                          spriteBuckets_, frame_, 0);
        drawGameplayPanel(right, worldMap_, gameAssets_, gameplay_.camera(1), gameplay_,
                          spriteBuckets_, frame_, 1);
        if (paused_) {
            drawCenteredText(left, "PAUSED", 80, 84);
            drawCenteredText(right, "PAUSED", 80, 84);
        }
    } else {
        const auto left = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Left);
        const auto right = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Right);
        drawGameplayPanel(left, worldMap_, gameAssets_, gameplay_.camera(0), gameplay_,
                          spriteBuckets_, frame_, 0, false);
        drawGameplayPanel(right, worldMap_, gameAssets_, gameplay_.camera(1), gameplay_,
                          spriteBuckets_, frame_, 1, false);
        drawResultPanel(left, gameplay_, resultOutcome_, sceneFrame_, resultContinueTicks_, 0,
                        timeBonuses_, finalScores_, rankings_, rankingCount_, rankingEntries_);
        drawResultPanel(right, gameplay_, resultOutcome_, sceneFrame_, resultContinueTicks_, 1,
                        timeBonuses_, finalScores_, rankings_, rankingCount_, rankingEntries_);
    }
    framebuffer_.flip();
}

} // namespace wizward::game
