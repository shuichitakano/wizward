#include "world/world_map.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

namespace wizward::world {
namespace {

using assets::BakedObjectId;
using assets::BoundaryId;
using assets::TerrainId;

constexpr std::uint8_t value(TerrainId terrain) noexcept {
    return static_cast<std::uint8_t>(terrain);
}

constexpr std::uint8_t value(BoundaryId boundary) noexcept {
    return static_cast<std::uint8_t>(boundary);
}

constexpr std::uint8_t value(BakedObjectId object) noexcept {
    return static_cast<std::uint8_t>(object);
}

std::uint32_t tileHash(std::int32_t x,
                       std::int32_t y,
                       std::uint32_t salt,
                       std::uint32_t seed) noexcept {
    auto n = static_cast<std::uint32_t>(x) * 374761393U
        + static_cast<std::uint32_t>(y) * 668265263U
        + salt * 1442695041U + seed * 1597334677U;
    n ^= n >> 13U;
    n *= 1274126177U;
    return n ^ (n >> 16U);
}

std::uint16_t clampX(std::int32_t x) noexcept {
    return static_cast<std::uint16_t>(std::clamp<std::int32_t>(x, 0, kMapColumns - 1));
}

std::uint16_t clampY(std::int32_t y) noexcept {
    return static_cast<std::uint16_t>(std::clamp<std::int32_t>(y, 0, kMapRows - 1));
}

TerrainId terrainAt(const TerrainWorkspace& terrain, std::int32_t x, std::int32_t y) noexcept {
    return terrain.get(clampX(x), clampY(y));
}

void paintCircle(TerrainWorkspace& terrain,
                 std::int32_t centerX,
                 std::int32_t centerY,
                 std::int32_t radius,
                 TerrainId valueToPaint,
                 bool preserveWater = true) noexcept {
    const auto minY = std::max<std::int32_t>(0, centerY - radius);
    const auto maxY = std::min<std::int32_t>(kMapRows - 1, centerY + radius);
    const auto minX = std::max<std::int32_t>(0, centerX - radius);
    const auto maxX = std::min<std::int32_t>(kMapColumns - 1, centerX + radius);
    const auto radiusSquared = radius * radius;
    for (auto y = minY; y <= maxY; ++y) {
        for (auto x = minX; x <= maxX; ++x) {
            const auto dx = x - centerX;
            const auto dy = y - centerY;
            if (dx * dx + dy * dy > radiusSquared) {
                continue;
            }
            if (preserveWater && terrain.get(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y))
                    == TerrainId::Water) {
                continue;
            }
            terrain.set(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y), valueToPaint);
        }
    }
}

void paintPath(TerrainWorkspace& terrain,
               std::int32_t x1,
               std::int32_t y1,
               std::int32_t x2,
               std::int32_t y2) noexcept {
    const auto dx = x2 - x1;
    const auto dy = y2 - y1;
    const auto steps = std::max(std::abs(dx), std::abs(dy));
    for (std::int32_t step = 0; step <= steps; ++step) {
        const auto x = x1 + (steps == 0 ? 0 : dx * step / steps);
        const auto y = y1 + (steps == 0 ? 0 : dy * step / steps);
        paintCircle(terrain, x, y, 3, TerrainId::Dirt);
        paintCircle(terrain, x, y, 1, TerrainId::Road);
    }
}

bool boundaryFor(TerrainId a, TerrainId b, BoundaryId& result) noexcept {
    const auto lower = value(a) < value(b) ? a : b;
    const auto upper = lower == a ? b : a;
    if (lower == TerrainId::Water && upper == TerrainId::Sand) {
        result = BoundaryId::WaterToSand;
    } else if (lower == TerrainId::Sand && upper == TerrainId::Grass) {
        result = BoundaryId::SandToGrass;
    } else if (lower == TerrainId::Grass && upper == TerrainId::Dirt) {
        result = BoundaryId::GrassToDirt;
    } else if (lower == TerrainId::Dirt && upper == TerrainId::Road) {
        result = BoundaryId::DirtToRoad;
    } else if (lower == TerrainId::Dirt && upper == TerrainId::Plaza) {
        result = BoundaryId::DirtToPlaza;
    } else if (lower == TerrainId::Road && upper == TerrainId::Plaza) {
        result = BoundaryId::RoadToPlaza;
    } else {
        return false;
    }
    return true;
}

bool transitionNeighbor(const TerrainWorkspace& workspace,
                        std::int32_t x,
                        std::int32_t y,
                        TerrainId own,
                        TerrainId& result) noexcept {
    constexpr std::array<std::array<std::int8_t, 3>, 8> kSamples{{
        {{0, -1, 4}}, {{1, 0, 4}}, {{0, 1, 4}}, {{-1, 0, 4}},
        {{-1, -1, 1}}, {{1, -1, 1}}, {{1, 1, 1}}, {{-1, 1, 1}},
    }};
    std::array<std::uint8_t, 6> scores{};
    for (const auto& sample : kSamples) {
        const auto other = terrainAt(workspace, x + sample[0], y + sample[1]);
        BoundaryId ignored{};
        if (other != own && boundaryFor(own, other, ignored)) {
            scores[value(other)] = static_cast<std::uint8_t>(scores[value(other)] + sample[2]);
        }
    }
    std::uint8_t bestScore = 0;
    bool found = false;
    for (std::uint8_t index = 0; index < scores.size(); ++index) {
        if (scores[index] > bestScore || (scores[index] == bestScore && scores[index] != 0 && index > value(result))) {
            bestScore = scores[index];
            result = static_cast<TerrainId>(index);
            found = true;
        }
    }
    return found;
}

TerrainId cornerTerrain(const TerrainWorkspace& workspace,
                        std::int32_t cornerX,
                        std::int32_t cornerY,
                        TerrainId lower,
                        TerrainId upper) noexcept {
    const std::array<TerrainId, 4> samples{{
        terrainAt(workspace, cornerX - 1, cornerY - 1),
        terrainAt(workspace, cornerX, cornerY - 1),
        terrainAt(workspace, cornerX, cornerY),
        terrainAt(workspace, cornerX - 1, cornerY),
    }};
    std::uint8_t upperVotes = 0;
    std::uint8_t lowerVotes = 0;
    for (const auto sample : samples) {
        upperVotes = static_cast<std::uint8_t>(upperVotes + (sample == upper));
        lowerVotes = static_cast<std::uint8_t>(lowerVotes + (sample == lower));
    }
    if (upperVotes == lowerVotes) {
        return value(samples[2]) >= value(upper) ? upper : lower;
    }
    return upperVotes > lowerVotes ? upper : lower;
}

bool basePattern(const TerrainWorkspace& workspace,
                 std::uint16_t x,
                 std::uint16_t y,
                 std::uint32_t seed,
                 const pixel_twins::BackgroundAssetPackView& assets,
                 std::uint8_t& pattern) noexcept {
    const auto own = workspace.get(x, y);
    TerrainId other = own;
    BoundaryId boundary{};
    if (transitionNeighbor(workspace, x, y, own, other) && boundaryFor(own, other, boundary)) {
        const auto lower = value(own) < value(other) ? own : other;
        const auto upper = lower == own ? other : own;
        std::uint8_t mask = 0;
        const std::array<std::array<std::int8_t, 2>, 4> corners{{
            {{0, 0}}, {{1, 0}}, {{1, 1}}, {{0, 1}},
        }};
        for (std::uint8_t index = 0; index < corners.size(); ++index) {
            if (cornerTerrain(workspace,
                              static_cast<std::int32_t>(x) + corners[index][0],
                              static_cast<std::int32_t>(y) + corners[index][1],
                              lower,
                              upper) == upper) {
                mask = static_cast<std::uint8_t>(mask | (1U << index));
            }
        }
        const auto ownMask = own == upper ? 15U : 0U;
        if (mask != ownMask) {
            return assets.boundaryPattern(value(boundary), mask, pattern);
        }
    }
    const auto variant = static_cast<std::uint8_t>(tileHash(x, y, value(own), seed) % 4U);
    return assets.terrainPattern(value(own), variant, pattern);
}

bool putObject(WorldMap& map,
               std::uint16_t x,
               std::uint16_t y,
               BakedObjectId object,
               bool collision,
               const pixel_twins::BackgroundAssetPackView& assets) noexcept {
    std::uint8_t pattern = 0;
    if (!assets.objectPattern(value(object), pattern)) {
        return false;
    }
    map.tiles[static_cast<std::size_t>(y) * kMapColumns + x] =
        static_cast<std::uint8_t>(pattern | (collision ? kCollisionBit : 0U));
    return true;
}

bool plainGrass(const TerrainWorkspace& terrain, std::uint16_t x, std::uint16_t y) noexcept {
    if (terrain.get(x, y) != TerrainId::Grass || x == 0 || y == 0
        || x + 1 >= kMapColumns || y + 1 >= kMapRows) {
        return false;
    }
    return terrain.get(x - 1, y) == TerrainId::Grass
        && terrain.get(x + 1, y) == TerrainId::Grass
        && terrain.get(x, y - 1) == TerrainId::Grass
        && terrain.get(x, y + 1) == TerrainId::Grass;
}

} // namespace

void TerrainWorkspace::fill(TerrainId terrain) noexcept {
    const auto nibble = static_cast<std::uint8_t>(value(terrain) & 0x0fU);
    packed_.fill(static_cast<std::uint8_t>(nibble | (nibble << 4U)));
}

TerrainId TerrainWorkspace::get(std::uint16_t x, std::uint16_t y) const noexcept {
    const auto cell = static_cast<std::size_t>(y) * kMapColumns + x;
    const auto byte = packed_[cell >> 1U];
    return static_cast<TerrainId>((cell & 1U) == 0 ? byte & 0x0fU : byte >> 4U);
}

void TerrainWorkspace::set(std::uint16_t x, std::uint16_t y, TerrainId terrain) noexcept {
    const auto cell = static_cast<std::size_t>(y) * kMapColumns + x;
    auto& byte = packed_[cell >> 1U];
    const auto nibble = static_cast<std::uint8_t>(value(terrain) & 0x0fU);
    byte = (cell & 1U) == 0
        ? static_cast<std::uint8_t>((byte & 0xf0U) | nibble)
        : static_cast<std::uint8_t>((byte & 0x0fU)
                                    | static_cast<std::uint8_t>(nibble << 4U));
}

std::uint8_t WorldMap::tile(std::uint16_t x, std::uint16_t y) const noexcept {
    if (x >= kMapColumns || y >= kMapRows) {
        return kCollisionBit;
    }
    return tiles[static_cast<std::size_t>(y) * kMapColumns + x];
}

bool WorldMap::collides(std::uint16_t x, std::uint16_t y) const noexcept {
    return (tile(x, y) & kCollisionBit) != 0;
}

pixel_twins::Background WorldMap::background(
    const pixel_twins::BackgroundAssetPackView& assets) const noexcept {
    return assets.makeBackground(kMapColumns, kMapRows, tiles.data(), kTileIndexMask);
}

void WorldMap::draw(pixel_twins::RenderTarget target,
                    const pixel_twins::BackgroundAssetPackView& assets,
                    std::int32_t sourceX,
                    std::int32_t sourceY) const noexcept {
    pixel_twins::drawBackground(target, background(assets), sourceX, sourceY);
}

bool WorldMap::activateSeal(
    std::uint8_t sealIndex,
    const pixel_twins::BackgroundAssetPackView& assets) noexcept {
    if (sealIndex >= seals.size()) {
        return false;
    }
    return putObject(*this,
                     seals[sealIndex].x,
                     seals[sealIndex].y,
                     BakedObjectId::SealActivePlaza,
                     true,
                     assets);
}

bool WorldMap::resetSeals(const pixel_twins::BackgroundAssetPackView& assets) noexcept {
    bool result = true;
    for (const auto& seal : seals) {
        result = putObject(*this, seal.x, seal.y, BakedObjectId::SealInactivePlaza,
                           true, assets) && result;
    }
    return result;
}

bool MapGenerator::generate(
    std::uint32_t seed,
    const pixel_twins::BackgroundAssetPackView& assets,
    TerrainWorkspace& workspace,
    WorldMap& result) const noexcept {
    if (!assets.valid() || assets.patternCount() > 128
        || assets.terrainCount() != 6 || assets.variantsPerTerrain() != 4
        || assets.boundaryCount() != 6 || assets.masksPerBoundary() != 16
        || assets.objectCount() != 9) {
        return false;
    }
    workspace.fill(TerrainId::Water);
    result.seed = seed;
    const auto centerX = 50;
    const auto centerY = 50;
    for (std::uint16_t y = 0; y < kMapRows; ++y) {
        for (std::uint16_t x = 0; x < kMapColumns; ++x) {
            const auto dx = static_cast<std::int32_t>(x) - centerX;
            const auto dy = static_cast<std::int32_t>(y) - centerY;
            const auto edgeNoise = static_cast<std::int32_t>(tileHash(x / 2, y / 2, 101, seed) % 11U) - 5;
            const auto score = dx * dx * 100 / (43 * 43) + dy * dy * 100 / (40 * 40);
            if (score <= 100 + edgeNoise) {
                workspace.set(x, y, TerrainId::Grass);
            }
        }
    }
    for (std::uint16_t y = 0; y < kMapRows; ++y) {
        for (std::uint16_t x = 0; x < kMapColumns; ++x) {
            if (workspace.get(x, y) != TerrainId::Grass) {
                continue;
            }
            bool coast = false;
            for (std::int32_t oy = -2; oy <= 2 && !coast; ++oy) {
                for (std::int32_t ox = -2; ox <= 2; ++ox) {
                    if (terrainAt(workspace, static_cast<std::int32_t>(x) + ox,
                                  static_cast<std::int32_t>(y) + oy) == TerrainId::Water) {
                        coast = true;
                        break;
                    }
                }
            }
            if (coast) {
                workspace.set(x, y, TerrainId::Sand);
            }
        }
    }

    constexpr std::array<SealCell, 3> kBaseSeals{{
        SealCell{24, 28}, SealCell{76, 30}, SealCell{50, 77},
    }};
    for (std::uint8_t index = 0; index < result.seals.size(); ++index) {
        const auto jitterX = static_cast<std::int32_t>(tileHash(index, 0, 701, seed) % 9U) - 4;
        const auto jitterY = static_cast<std::int32_t>(tileHash(index, 0, 1701, seed) % 9U) - 4;
        result.seals[index] = SealCell{
            static_cast<std::uint8_t>(static_cast<std::int32_t>(kBaseSeals[index].x) + jitterX),
            static_cast<std::uint8_t>(static_cast<std::int32_t>(kBaseSeals[index].y) + jitterY),
        };
        paintPath(workspace, centerX, centerY, result.seals[index].x, result.seals[index].y);
        paintCircle(workspace, result.seals[index].x, result.seals[index].y, 4, TerrainId::Dirt);
        paintCircle(workspace, result.seals[index].x, result.seals[index].y, 3, TerrainId::Plaza);
    }
    paintCircle(workspace, centerX, centerY, 7, TerrainId::Dirt);
    paintCircle(workspace, centerX, centerY, 5, TerrainId::Road);
    paintCircle(workspace, centerX, centerY, 4, TerrainId::Plaza);

    for (std::uint16_t y = 0; y < kMapRows; ++y) {
        for (std::uint16_t x = 0; x < kMapColumns; ++x) {
            std::uint8_t pattern = 0;
            if (!basePattern(workspace, x, y, seed, assets, pattern)) {
                return false;
            }
            const auto collision = workspace.get(x, y) == TerrainId::Water;
            result.tiles[static_cast<std::size_t>(y) * kMapColumns + x] =
                static_cast<std::uint8_t>(pattern | (collision ? kCollisionBit : 0U));
        }
    }

    for (const auto seal : result.seals) {
        if (!putObject(result, seal.x, seal.y, BakedObjectId::SealInactivePlaza, true, assets)) {
            return false;
        }
    }
    constexpr std::array<std::array<std::int8_t, 2>, 4> kPedestals{{
        {{-3, -3}}, {{3, -3}}, {{-3, 3}}, {{3, 3}},
    }};
    for (const auto offset : kPedestals) {
        if (!putObject(result,
                       static_cast<std::uint16_t>(centerX + offset[0]),
                       static_cast<std::uint16_t>(centerY + offset[1]),
                       BakedObjectId::PlazaPedestal,
                       true,
                       assets)) {
            return false;
        }
    }
    std::array<std::uint16_t, 6> placed{};
    constexpr std::array<std::uint16_t, 6> kLimits{{24, 18, 64, 28, 28, 28}};
    constexpr std::array<BakedObjectId, 6> kObjects{{
        BakedObjectId::RockGrass,
        BakedObjectId::ShrubGrass,
        BakedObjectId::GrassClumpGrass,
        BakedObjectId::FlowerWhiteGrass,
        BakedObjectId::FlowerBlueGrass,
        BakedObjectId::FlowerYellowGrass,
    }};
    for (std::uint16_t y = 2; y + 2 < kMapRows; ++y) {
        for (std::uint16_t x = 2; x + 2 < kMapColumns; ++x) {
            if (!plainGrass(workspace, x, y)) {
                continue;
            }
            const auto random = tileHash(x, y, 2301, seed);
            const auto objectIndex = static_cast<std::uint8_t>(random % 97U);
            std::uint8_t slot = 0xff;
            if (objectIndex == 0) slot = 0;
            else if (objectIndex == 1) slot = 1;
            else if (objectIndex <= 4) slot = 2;
            else if (objectIndex == 5) slot = 3;
            else if (objectIndex == 6) slot = 4;
            else if (objectIndex == 7) slot = 5;
            if (slot >= kObjects.size() || placed[slot] >= kLimits[slot]) {
                continue;
            }
            const auto collision = slot <= 1;
            if (!putObject(result, x, y, kObjects[slot], collision, assets)) {
                return false;
            }
            ++placed[slot];
        }
    }
    return true;
}

} // namespace wizward::world
