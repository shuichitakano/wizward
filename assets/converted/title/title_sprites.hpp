#pragma once

#include <cstdint>

namespace wizward::title_assets {

enum class SpriteAssetId : std::uint16_t {
    WizwardLogo104x20 = 0,
};

inline constexpr std::uint16_t kSpriteAssetCount = 1;

[[nodiscard]] constexpr std::uint16_t spriteAssetIndex(SpriteAssetId id) noexcept {
    return static_cast<std::uint16_t>(id);
}

} // namespace wizward::title_assets
