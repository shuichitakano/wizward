#pragma once

#include "assets/embedded_assets.hpp"
#include "background.hpp"
#include "sprites.hpp"

#include "pixel_twins/background_asset.hpp"
#include "pixel_twins/framebuffer.hpp"
#include "pixel_twins/sprite_asset.hpp"

#include <cstdint>

namespace wizward::assets {

class GameAssets {
public:
    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool applyPalette(pixel_twins::Framebuffer& framebuffer) const noexcept;

    [[nodiscard]] const pixel_twins::BackgroundAssetPackView& background() const noexcept;
    [[nodiscard]] const pixel_twins::SpriteAssetPackView& sprites() const noexcept;

    [[nodiscard]] bool makeSprite(SpriteAssetId asset,
                                  std::uint8_t animationFrame,
                                  std::uint8_t directionRow,
                                  std::int16_t logicalX,
                                  std::int16_t logicalY,
                                  pixel_twins::Sprite& result) const noexcept;
    [[nodiscard]] std::uint8_t animationFrameCount(SpriteAssetId asset) const noexcept;
    [[nodiscard]] bool makeLoopingSprite(SpriteAssetId asset,
                                         std::uint32_t animationFrame,
                                         std::uint8_t directionRow,
                                         std::int16_t logicalX,
                                         std::int16_t logicalY,
                                         pixel_twins::Sprite& result) const noexcept;

private:
    pixel_twins::BackgroundAssetPackView background_;
    pixel_twins::SpriteAssetPackView sprites_;
};

} // namespace wizward::assets
