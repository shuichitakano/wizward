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

constexpr std::uint32_t kTitleFrames = 150;

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
    if ((elapsedSeconds < 4U || (elapsedMinute > 0U && elapsedMinute < 5U && sinceMinute < 4U))
        && (elapsedTicks / 15U) % 2U == 0U) {
        char text[] = "5 MIN LEFT";
        text[0] = static_cast<char>('0' + (remain + 59U) / 60U);
        drawCenteredText(target, text, 80, 24);
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
            pixel_twins::fillRectangle(target,
                static_cast<std::int16_t>(kX + mx), static_cast<std::int16_t>(kY + my), 1, 1,
                map.collides(tx, ty) ? 29 : 30);
        }
    }
    pixel_twins::drawRectangle(target, kX, kY, kSize, kSize, 32);
    for (std::size_t index = 0; index < map.seals.size(); ++index) {
        const auto& seal = map.seals[index];
        const auto x = static_cast<std::int16_t>(kX + seal.x * kSize / world::kMapColumns);
        const auto y = static_cast<std::int16_t>(kY + seal.y * kSize / world::kMapRows);
        pixel_twins::fillCircle(target, x, y, 1, gameplay.seal(index).active ? 255 : 220);
    }
    for (std::size_t index = 0; index < pixel_twins::kControllerCount; ++index) {
        const auto& player = gameplay.player(index);
        const auto x = static_cast<std::int16_t>(kX + player.x * kSize
            / static_cast<float>(world::kMapColumns * kWorldTileSize));
        const auto y = static_cast<std::int16_t>(kY + player.y * kSize
            / static_cast<float>(world::kMapRows * kWorldTileSize));
        pixel_twins::fillCircle(target, x, y, index == viewer ? 2 : 1, index == 0 ? 19 : 20);
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
                float screenFootY) noexcept {
    pixel_twins::Sprite source{};
    if (!assets.makeLoopingSprite(id, frame, directionRow, x, y, source)) return;
    pixel_twins::SpriteEx sprite{x, y, width, height, source.sw, source.sh, source.p};
    constexpr float kSortMargin = 60.0F;
    const auto bucket = static_cast<std::uint16_t>(std::clamp(
        static_cast<std::int32_t>(screenFootY + kSortMargin), 0,
        static_cast<std::int32_t>(pixel_twins::kBucketCount - 1U)));
    (void)buckets.addSpriteEx(bucket, sprite);
}

PIXEL_TWINS_SRAM void drawTitle(pixel_twins::Framebuffer& framebuffer,
               const assets::TitleAssets& title,
               std::uint32_t frame) noexcept {
    auto left = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Left);
    auto right = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Right);
    title.drawScreen(left);
    title.drawScreen(right);
    pixel_twins::Sprite logo{};
    if (title.makeLogo(28, 18, logo)) pixel_twins::drawSprite(right, logo);
    if ((frame / 30U) % 2U == 0U) {
        pixel_twins::drawText(left, assets::kWizwardFont, 40, 98, "PRESS START", 255);
        pixel_twins::drawText(right, assets::kWizwardFont, 40, 98, "PRESS START", 255);
    }
}

template<std::size_t Capacity, std::size_t ExCapacity>
PIXEL_TWINS_SRAM void drawGameplayPanel(pixel_twins::RenderTarget target,
                       const world::WorldMap& map,
                       const assets::GameAssets& assets,
                       const CameraState& camera,
                       const GameplayState& gameplay,
                       pixel_twins::SpriteBuckets<Capacity, ExCapacity>& spriteBuckets,
                       std::uint32_t frame,
                       std::size_t viewer) noexcept {
    map.draw(target, assets.background(), static_cast<std::int32_t>(camera.x),
             static_cast<std::int32_t>(camera.y));
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
    for (const auto& gem : gameplay.xpGems()) {
        if (!gem.active) continue;
        queueAsset(spriteBuckets, assets, assets::SpriteAssetId::XpGem88x81fSheet, 0,
                   static_cast<std::int16_t>(gem.x - camera.x - 4.0F),
                   static_cast<std::int16_t>(gem.y - camera.y - 4.0F), gem.y - camera.y);
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
            : frame / 8U;
        if (enemy.active && enemy.bornTicks > 0) {
            const auto progress = std::clamp(1.0F - static_cast<float>(enemy.bornTicks) / 23.0F,
                                             0.0F, 1.0F);
            const auto footX = static_cast<std::int16_t>(std::round(enemy.x - camera.x));
            const auto footY = static_cast<std::int16_t>(std::round(drawEnemyY - camera.y));
            const auto shadowWidth = static_cast<std::uint16_t>(std::max(2.0F, std::round(2.0F + progress * 8.0F)));
            pixel_twins::fillRectangle(target,
                static_cast<std::int16_t>(footX - static_cast<std::int16_t>(shadowWidth / 2U)),
                footY, shadowWidth, 2, 1);
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
                                            static_cast<std::uint16_t>(lineHeight + 3), 18);
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
                width, height, drawEnemyY - camera.y);
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
    drawBossIntroOverlay(target, gameplay, camera);
    drawClearSequence(target, assets, gameplay, camera, frame);
    for (std::size_t playerIndex = 0; playerIndex < pixel_twins::kControllerCount; ++playerIndex) {
        const auto& player = gameplay.player(playerIndex);
        if (player.bombEffectTicks == 0) continue;
        const auto age = static_cast<std::uint16_t>(34U - player.bombEffectTicks);
        const auto centerX = static_cast<std::int16_t>(std::round(player.bombEffectX - camera.x));
        const auto centerY = static_cast<std::int16_t>(std::round(player.bombEffectY - camera.y));
        pixel_twins::Sprite sprite{};
        if (age >= 4U && age < 29U) {
            const auto waveFrame = static_cast<std::uint32_t>(std::min<std::uint16_t>(
                5U, static_cast<std::uint16_t>((age - 4U) * 6U / 25U)));
            if (assets.makeLoopingSprite(assets::SpriteAssetId::BombGroundWave64x326fSheet,
                                         waveFrame, 0,
                                         static_cast<std::int16_t>(centerX - 32),
                                         static_cast<std::int16_t>(centerY - 16), sprite)) {
                pixel_twins::drawSprite(target, sprite);
            }
        }
        if (age >= 2U && age < 30U) {
            const auto coreFrame = static_cast<std::uint32_t>(std::min<std::uint16_t>(
                6U, static_cast<std::uint16_t>((age - 2U) * 7U / 28U)));
            if (assets.makeLoopingSprite(assets::SpriteAssetId::BombCore48x487fSheet,
                                         coreFrame, 0,
                                         static_cast<std::int16_t>(centerX - 24),
                                         static_cast<std::int16_t>(centerY - 36), sprite)) {
                pixel_twins::drawSprite(target, sprite);
            }
        }
        for (std::uint8_t row = 0; row < 8; ++row) {
            const auto start = static_cast<std::uint16_t>((row % 2U) == 0U ? 0U : 3U);
            if (age < start || age >= start + 13U) continue;
            const auto rayFrame = static_cast<std::uint32_t>(std::min<std::uint16_t>(
                3U, static_cast<std::uint16_t>((age - start) * 4U / 13U)));
            if (assets.makeLoopingSprite(assets::SpriteAssetId::BombRay16x164f8dirSheet,
                                         rayFrame, row,
                                         static_cast<std::int16_t>(centerX - 8),
                                         static_cast<std::int16_t>(centerY - 20), sprite)) {
                pixel_twins::drawSprite(target, sprite);
            }
        }
    }
    for (const auto& strike : gameplay.thunderStrikes()) {
        if (!strike.active) continue;
        pixel_twins::drawCircle(target,
            static_cast<std::int16_t>(std::round(strike.x - camera.x)),
            static_cast<std::int16_t>(std::round(strike.y - camera.y)),
            thunderShockwaveRadius(strike.ageTicks), 4);
    }
    const auto& viewedPlayer = gameplay.player(viewer);
    const auto maxHpWidth = static_cast<std::uint16_t>(std::clamp<std::int16_t>(viewedPlayer.maxHp, 1, 60));
    const auto hpWidth = static_cast<std::uint16_t>(
        std::clamp<std::int16_t>(viewedPlayer.hp, 0, static_cast<std::int16_t>(maxHpWidth)));
    pixel_twins::fillRectangle(target, 49, 4, static_cast<std::uint16_t>(maxHpWidth + 2U), 4, 28);
    if (hpWidth > 0) pixel_twins::fillRectangle(target, 50, 5, hpWidth, 2, 9);
    pixel_twins::fillRectangle(target, 49, 7, 62, 4, 28);
    const auto xpNeed = xpNeededForLevel(viewedPlayer.level);
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

} // namespace

bool Game::initialize(Scene initialScene) noexcept {
    world::MapGenerator mapGenerator;
    if (!gameAssets_.initialize() || !titleAssets_.initialize()
        || !mapGenerator.generate(0x57495aU, gameAssets_.background(), terrainWorkspace_, worldMap_)) {
        return false;
    }
    gameplay_.reset(worldMap_);
    scene_ = initialScene;
    return scene_ == Scene::Title ? titleAssets_.applyPalette(framebuffer_)
                                  : gameAssets_.applyPalette(framebuffer_);
}

UpdateResult Game::changeScene(Scene scene, bool playStartSfx) noexcept {
    scene_ = scene;
    sceneFrame_ = 0;
    if (scene_ == Scene::Gameplay) {
        (void)worldMap_.resetSeals(gameAssets_.background());
        gameplay_.reset(worldMap_);
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
    if (!controllers[0].isPressed(pixel_twins::ControllerButton::start)) return {};
    const auto next = scene_ == Scene::Title ? Scene::Gameplay
        : (scene_ == Scene::Gameplay ? Scene::Result : Scene::Title);
    return changeScene(next, true);
}

UpdateResult Game::tick(const pixel_twins::Controllers& controllers) noexcept {
    if (scene_ == Scene::Title && sceneFrame_ == kTitleFrames) {
        auto result = changeScene(Scene::Gameplay, false);
        ++frame_;
        ++sceneFrame_;
        return result;
    }
    if (scene_ == Scene::Gameplay) {
        gameplay_.tick(controllers, worldMap_);
        for (std::uint8_t index = 0; index < worldMap_.seals.size(); ++index) {
            if (gameplay_.seal(index).active) {
                (void)worldMap_.activateSeal(index, gameAssets_.background());
            }
        }
        if (gameplay_.outcome() != GameplayOutcome::Running) {
            resultOutcome_ = gameplay_.outcome();
            auto result = changeScene(Scene::Result, false);
            ++frame_;
            ++sceneFrame_;
            return result;
        }
    }
    ++frame_;
    ++sceneFrame_;
    return {};
}

void Game::render() noexcept {
    if (scene_ == Scene::Title) {
        drawTitle(framebuffer_, titleAssets_, frame_);
    } else if (scene_ == Scene::Gameplay) {
        const auto left = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Left);
        const auto right = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Right);
        drawGameplayPanel(left, worldMap_, gameAssets_, gameplay_.camera(0), gameplay_,
                          spriteBuckets_, frame_, 0);
        drawGameplayPanel(right, worldMap_, gameAssets_, gameplay_.camera(1), gameplay_,
                          spriteBuckets_, frame_, 1);
    } else {
        const auto left = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Left);
        const auto right = pixel_twins::makeRenderTarget(framebuffer_.drawBuffer(), pixel_twins::Screen::Right);
        pixel_twins::fillRectangle(left, 0, 0, 160, 120, 0);
        pixel_twins::fillRectangle(right, 0, 0, 160, 120, 0);
        const auto text = resultOutcome_ == GameplayOutcome::Down
            ? std::string_view{"GAME OVER"}
            : (resultOutcome_ == GameplayOutcome::TimeUp
                ? std::string_view{"TIME UP"}
                : (resultOutcome_ == GameplayOutcome::Clear
                    ? std::string_view{"CONGRATULATIONS!"} : std::string_view{"RESULT"}));
        drawCenteredText(left, text, 80, 48);
        drawCenteredText(right, text, 80, 48);
    }
    framebuffer_.flip();
}

} // namespace wizward::game
