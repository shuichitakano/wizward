#pragma once

#include "assets/embedded_assets.hpp"
#include "title_sprites.hpp"

#include "pixel_twins/framebuffer.hpp"
#include "pixel_twins/render_target.hpp"
#include "pixel_twins/sprite_asset.hpp"

#include <cstdint>

namespace wizward::assets {

class TitleAssets {
public:
    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool applyPalette(pixel_twins::Framebuffer& framebuffer) const noexcept;
    void drawScreen(pixel_twins::RenderTarget target) const noexcept;
    [[nodiscard]] bool makeLogo(std::int16_t logicalX,
                                std::int16_t logicalY,
                                pixel_twins::Sprite& result) const noexcept;

private:
    pixel_twins::SpriteAssetPackView logo_;
};

} // namespace wizward::assets
