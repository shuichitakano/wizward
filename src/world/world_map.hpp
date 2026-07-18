#pragma once

#include "background.hpp"

#include "pixel_twins/background.hpp"
#include "pixel_twins/background_asset.hpp"
#include "pixel_twins/render_target.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace wizward::world {

inline constexpr std::uint16_t kMapColumns = 100;
inline constexpr std::uint16_t kMapRows = 100;
inline constexpr std::uint8_t kTileIndexMask = 0x7f;
inline constexpr std::uint8_t kCollisionBit = 0x80;
inline constexpr std::size_t kMapTileCount =
    static_cast<std::size_t>(kMapColumns) * kMapRows;

class TerrainWorkspace {
public:
    void fill(assets::TerrainId terrain) noexcept;
    [[nodiscard]] assets::TerrainId get(std::uint16_t x, std::uint16_t y) const noexcept;
    void set(std::uint16_t x, std::uint16_t y, assets::TerrainId terrain) noexcept;

private:
    std::array<std::uint8_t, (kMapTileCount + 1U) / 2U> packed_{};
};

static_assert(sizeof(TerrainWorkspace) == 5000, "地形ワークは4bit/セルで保持する");

struct SealCell {
    std::uint8_t x;
    std::uint8_t y;

    [[nodiscard]] friend constexpr bool operator==(SealCell left, SealCell right) noexcept {
        return left.x == right.x && left.y == right.y;
    }
};

struct WorldMap {
    std::array<std::uint8_t, kMapTileCount> tiles{};
    std::array<SealCell, 3> seals{};
    std::uint32_t seed = 0;

    [[nodiscard]] std::uint8_t tile(std::uint16_t x, std::uint16_t y) const noexcept;
    [[nodiscard]] bool collides(std::uint16_t x, std::uint16_t y) const noexcept;
    [[nodiscard]] pixel_twins::Background background(
        const pixel_twins::BackgroundAssetPackView& assets) const noexcept;
    void draw(pixel_twins::RenderTarget target,
              const pixel_twins::BackgroundAssetPackView& assets,
              std::int32_t sourceX,
              std::int32_t sourceY) const noexcept;
    [[nodiscard]] bool activateSeal(
        std::uint8_t sealIndex,
        const pixel_twins::BackgroundAssetPackView& assets) noexcept;
};

static_assert(sizeof(WorldMap) <= 10016, "常駐マップは10KB程度に収める");

class MapGenerator {
public:
    [[nodiscard]] bool generate(
        std::uint32_t seed,
        const pixel_twins::BackgroundAssetPackView& assets,
        TerrainWorkspace& workspace,
        WorldMap& result) const noexcept;
};

} // namespace wizward::world
