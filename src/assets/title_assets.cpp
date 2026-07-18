#include "assets/title_assets.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace wizward::assets {
namespace {

constexpr std::uint16_t kTitleWidth = 160;
constexpr std::uint16_t kTitleHeight = 120;

bool applyTitlePalette(pixel_twins::Framebuffer& framebuffer) noexcept {
    if (kTitlePaletteDataSize != 256U * 3U
        || kTitlePaletteData[0] != 0 || kTitlePaletteData[1] != 0
        || kTitlePaletteData[2] != 0 || kTitlePaletteData[3] != 0
        || kTitlePaletteData[4] != 0 || kTitlePaletteData[5] != 0
        || kTitlePaletteData[765] != 255 || kTitlePaletteData[766] != 255
        || kTitlePaletteData[767] != 255) {
        return false;
    }
    for (std::uint16_t index = 2; index < 255; ++index) {
        const auto offset = static_cast<std::size_t>(index) * 3U;
        if (!framebuffer.setPaletteColor(
                static_cast<pixel_twins::ColorIndex>(index),
                pixel_twins::Rgb{kTitlePaletteData[offset],
                                  kTitlePaletteData[offset + 1U],
                                  kTitlePaletteData[offset + 2U]})) {
            return false;
        }
    }
    return true;
}

} // namespace

bool TitleAssets::initialize() noexcept {
    return kTitleScreenDataSize == static_cast<std::size_t>(kTitleWidth) * kTitleHeight
        && kTitlePaletteDataSize == 256U * 3U
        && logo_.reset(kTitleSpriteData, kTitleSpriteDataSize);
}

bool TitleAssets::valid() const noexcept {
    return logo_.valid()
        && kTitleScreenDataSize == static_cast<std::size_t>(kTitleWidth) * kTitleHeight
        && kTitlePaletteDataSize == 256U * 3U;
}

bool TitleAssets::applyPalette(pixel_twins::Framebuffer& framebuffer) const noexcept {
    return valid() && applyTitlePalette(framebuffer);
}

void TitleAssets::drawScreen(pixel_twins::RenderTarget target) const noexcept {
    if (!valid() || target.pixels == nullptr) {
        return;
    }
    const auto width = std::min(target.width, kTitleWidth);
    const auto height = std::min(target.height, kTitleHeight);
    for (std::uint16_t y = 0; y < height; ++y) {
        auto* destination = target.pixels
            + static_cast<std::size_t>(target.originY + y) * target.stride + target.originX;
        const auto* source = kTitleScreenData + static_cast<std::size_t>(y) * kTitleWidth;
        std::copy_n(source, width, destination);
    }
}

bool TitleAssets::makeLogo(std::int16_t logicalX,
                           std::int16_t logicalY,
                           pixel_twins::Sprite& result) const noexcept {
    return logo_.makeSpriteAt(
        title_assets::spriteAssetIndex(title_assets::SpriteAssetId::WizwardLogo104x20),
        0,
        0,
        logicalX,
        logicalY,
        result);
}

} // namespace wizward::assets
