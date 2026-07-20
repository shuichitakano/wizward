#include "assets/game_assets.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace wizward::assets {
namespace {

std::uint8_t nearestPaletteIndex(std::uint8_t red, std::uint8_t green,
                                 std::uint8_t blue) noexcept {
    auto best = std::uint8_t{2};
    auto bestDistance = std::uint32_t{0xffffffffU};
    for (std::uint16_t index = 2; index < 255; ++index) {
        const auto offset = static_cast<std::size_t>(index) * 3U;
        const auto dr = static_cast<std::int32_t>(kGameplayPaletteData[offset]) - red;
        const auto dg = static_cast<std::int32_t>(kGameplayPaletteData[offset + 1U]) - green;
        const auto db = static_cast<std::int32_t>(kGameplayPaletteData[offset + 2U]) - blue;
        const auto distance = static_cast<std::uint32_t>(dr * dr + dg * dg + db * db);
        if (distance < bestDistance) {
            best = static_cast<std::uint8_t>(index);
            bestDistance = distance;
        }
    }
    return best;
}

} // namespace

bool GameAssets::initialize() noexcept {
    const auto backgroundValid = background_.resetSplit(
        kGameplayBackgroundMetadata,
        kGameplayBackgroundMetadataSize,
        kGameplayBackgroundPixels,
        kGameplayBackgroundPixelsSize);
    const auto spritesValid = sprites_.resetSplit(
        kGameplaySpriteMetadata,
        kGameplaySpriteMetadataSize,
        kGameplaySpritePixels,
        kGameplaySpritePixelsSize);
    if (!backgroundValid || !spritesValid || kGameplayPaletteDataSize != 256U * 3U) return false;
    pixel_twins::Sprite source{};
    if (!sprites_.makeSpriteAt(spriteAssetIndex(SpriteAssetId::XpGem88x81fSheet),
                               0, 0, 0, 0, source)
        || source.sw != 8 || source.sh != 8) return false;
    constexpr std::array<std::array<std::array<std::uint8_t, 3>, 3>, 2> kPalettes{{
        {{{{67, 22, 34}}, {{205, 55, 67}}, {{255, 145, 104}}}},
        {{{{20, 50, 100}}, {{52, 115, 211}}, {{132, 211, 255}}}},
    }};
    for (std::size_t owner = 0; owner < xpGemPatterns_.size(); ++owner) {
        std::array<std::uint8_t, 3> tint{};
        for (std::size_t shade = 0; shade < tint.size(); ++shade) {
            tint[shade] = nearestPaletteIndex(kPalettes[owner][shade][0],
                                              kPalettes[owner][shade][1],
                                              kPalettes[owner][shade][2]);
        }
        for (std::size_t pixel = 0; pixel < xpGemPatterns_[owner].size(); ++pixel) {
            const auto sourceIndex = source.p[pixel];
            if (sourceIndex == pixel_twins::kTransparentColor) {
                xpGemPatterns_[owner][pixel] = pixel_twins::kTransparentColor;
                continue;
            }
            const auto offset = static_cast<std::size_t>(sourceIndex) * 3U;
            const auto brightness = std::max({kGameplayPaletteData[offset],
                                              kGameplayPaletteData[offset + 1U],
                                              kGameplayPaletteData[offset + 2U]});
            xpGemPatterns_[owner][pixel] = tint[brightness < 90 ? 0 : brightness < 210 ? 1 : 2];
        }
    }
    return true;
}

bool GameAssets::valid() const noexcept {
    return background_.valid() && sprites_.valid() && kGameplayPaletteDataSize == 256U * 3U;
}

bool GameAssets::applyPalette(pixel_twins::Framebuffer& framebuffer) const noexcept {
    if (!valid() || kGameplayPaletteData[0] != 0 || kGameplayPaletteData[1] != 0
        || kGameplayPaletteData[2] != 0 || kGameplayPaletteData[3] != 0
        || kGameplayPaletteData[4] != 0 || kGameplayPaletteData[5] != 0
        || kGameplayPaletteData[765] != 255 || kGameplayPaletteData[766] != 255
        || kGameplayPaletteData[767] != 255) {
        return false;
    }
    for (std::uint16_t index = 2; index < 255; ++index) {
        const auto offset = static_cast<std::size_t>(index) * 3U;
        if (!framebuffer.setPaletteColor(
                static_cast<pixel_twins::ColorIndex>(index),
                pixel_twins::Rgb{kGameplayPaletteData[offset],
                                  kGameplayPaletteData[offset + 1U],
                                  kGameplayPaletteData[offset + 2U]})) {
            return false;
        }
    }
    return true;
}

const pixel_twins::BackgroundAssetPackView& GameAssets::background() const noexcept {
    return background_;
}

const pixel_twins::SpriteAssetPackView& GameAssets::sprites() const noexcept {
    return sprites_;
}

bool GameAssets::makeSprite(SpriteAssetId asset,
                            std::uint8_t animationFrame,
                            std::uint8_t directionRow,
                            std::int16_t logicalX,
                            std::int16_t logicalY,
                            pixel_twins::Sprite& result) const noexcept {
    return sprites_.makeSpriteAt(spriteAssetIndex(asset),
                                 animationFrame,
                                 directionRow,
                                 logicalX,
                                 logicalY,
                                 result);
}

std::uint8_t GameAssets::animationFrameCount(SpriteAssetId asset) const noexcept {
    pixel_twins::SpriteAssetInfo info{};
    if (!sprites_.assetInfo(spriteAssetIndex(asset), info)) {
        return 0;
    }
    return info.columns;
}

bool GameAssets::makeLoopingSprite(SpriteAssetId asset,
                                   std::uint32_t animationFrame,
                                   std::uint8_t directionRow,
                                   std::int16_t logicalX,
                                   std::int16_t logicalY,
                                   pixel_twins::Sprite& result) const noexcept {
    const auto frameCount = animationFrameCount(asset);
    if (frameCount == 0) {
        return false;
    }
    return makeSprite(asset,
                      static_cast<std::uint8_t>(animationFrame % frameCount),
                      directionRow,
                      logicalX,
                      logicalY,
                      result);
}

bool GameAssets::makeXpGemSprite(std::uint8_t owner,
                                 std::int16_t logicalX,
                                 std::int16_t logicalY,
                                 pixel_twins::Sprite& result) const noexcept {
    if (owner >= xpGemPatterns_.size()) return false;
    result = pixel_twins::Sprite(logicalX, logicalY, 8, 8, xpGemPatterns_[owner].data());
    return true;
}

} // namespace wizward::assets
