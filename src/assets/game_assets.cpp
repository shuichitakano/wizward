#include "assets/game_assets.hpp"

#include <cstddef>
#include <cstdint>

namespace wizward::assets {

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
    return backgroundValid && spritesValid && kGameplayPaletteDataSize == 256U * 3U;
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

} // namespace wizward::assets
