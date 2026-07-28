#pragma once

#include "pixel_twins/sprite_asset.hpp"

#include <cstddef>
#include <cstdint>

namespace wizward::assets {

extern const std::uint8_t kGameplayBackgroundMetadata[];
extern const std::size_t kGameplayBackgroundMetadataSize;
extern const std::uint8_t kGameplayBackgroundPixels[];
extern const std::size_t kGameplayBackgroundPixelsSize;
extern const std::uint8_t kGameplaySpriteMetadata[];
extern const std::size_t kGameplaySpriteMetadataSize;
extern const pixel_twins::SpritePixelRegion kGameplaySpritePixelRegions[];
extern const std::size_t kGameplaySpritePixelRegionCount;
extern const std::uint8_t kGameplayPaletteData[];
extern const std::size_t kGameplayPaletteDataSize;
extern const std::uint8_t kTitleScreenData[];
extern const std::size_t kTitleScreenDataSize;
extern const std::uint8_t kTitlePaletteData[];
extern const std::size_t kTitlePaletteDataSize;
extern const std::uint8_t kAttractP1ScreenData[];
extern const std::size_t kAttractP1ScreenDataSize;
extern const std::uint8_t kAttractP2ScreenData[];
extern const std::size_t kAttractP2ScreenDataSize;
extern const std::uint8_t kAttractPaletteData[];
extern const std::size_t kAttractPaletteDataSize;

} // namespace wizward::assets
