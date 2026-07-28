#pragma once

#include "assets/embedded_assets.hpp"

#include "pixel_twins/framebuffer.hpp"
#include "pixel_twins/platform.hpp"
#include "pixel_twins/render_target.hpp"

#include <cstddef>
#include <cstdint>

namespace wizward::assets {

class TitleAssets {
public:
    [[nodiscard]] bool initialize() noexcept;
    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool applyPalette(pixel_twins::Framebuffer& framebuffer) const noexcept;
    [[nodiscard]] bool applyAttractPalette(pixel_twins::Framebuffer& framebuffer) const noexcept;
    void drawScreen(pixel_twins::RenderTarget target) const noexcept PIXEL_TWINS_SRAM;
    void drawAttractScreen(pixel_twins::RenderTarget target,
                           std::size_t player) const noexcept PIXEL_TWINS_SRAM;
};

} // namespace wizward::assets
