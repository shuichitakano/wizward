#include "world/world_map.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cmath>

namespace wizward::world {
namespace {

constexpr std::uint8_t kCollisionShapeNone = 0;
constexpr std::uint8_t kCollisionShapeWater = 1;
constexpr std::uint8_t kCollisionShapeDecoration = 2;
constexpr std::uint8_t kCollisionShapeCoastBase = 0x10;

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

struct MapPoint { float x; float y; };
struct IslandNode { MapPoint point; float radius; std::uint16_t salt; };
struct IslandSegment { MapPoint from; MapPoint to; float radius; std::uint16_t salt; };

float seededUnit(std::uint32_t salt, std::uint32_t seed) noexcept {
    return static_cast<float>(tileHash(static_cast<std::int32_t>(salt),
                                       static_cast<std::int32_t>(seed & 0xffffU), 991U, seed))
        / static_cast<float>(0xffffffffU) * 2.0F - 1.0F;
}

float seededRange(std::uint32_t salt, float minimum, float maximum,
                  std::uint32_t seed) noexcept {
    return minimum + (seededUnit(salt, seed) * 0.5F + 0.5F) * (maximum - minimum);
}

float edgeNoise(std::int32_t x, std::int32_t y, std::uint32_t salt,
                std::uint32_t seed) noexcept {
    const auto a = static_cast<float>(tileHash(x, y, salt, seed))
        / static_cast<float>(0xffffffffU) * 2.0F - 1.0F;
    const auto b = static_cast<float>(tileHash(x + 17, y - 9, salt + 101U, seed))
        / static_cast<float>(0xffffffffU) * 2.0F - 1.0F;
    return a * 0.65F + b * 0.35F;
}

MapPoint rotatePoint(MapPoint point, float angle, float radiusScale) noexcept {
    constexpr MapPoint kCenter{50.0F, 50.0F};
    const auto dx = (point.x - kCenter.x) * radiusScale;
    const auto dy = (point.y - kCenter.y) * radiusScale;
    const auto cosine = std::cos(angle);
    const auto sine = std::sin(angle);
    return {kCenter.x + dx * cosine - dy * sine,
            kCenter.y + dx * sine + dy * cosine};
}

MapPoint jitterPoint(MapPoint point, float amount, std::uint32_t salt,
                     std::uint32_t seed) noexcept {
    return {std::clamp(point.x + seededUnit(salt, seed) * amount, 5.0F, 95.0F),
            std::clamp(point.y + seededUnit(salt + 1009U, seed) * amount, 5.0F, 95.0F)};
}

float distanceToSegment(MapPoint point, MapPoint from, MapPoint to) noexcept {
    const auto dx = to.x - from.x;
    const auto dy = to.y - from.y;
    const auto lengthSquared = dx * dx + dy * dy;
    if (lengthSquared <= 0.0F) return std::hypot(point.x - from.x, point.y - from.y);
    const auto t = std::clamp(((point.x - from.x) * dx + (point.y - from.y) * dy)
                                  / lengthSquared,
                              0.0F, 1.0F);
    return std::hypot(point.x - (from.x + dx * t), point.y - (from.y + dy * t));
}

void paintSegment(TerrainWorkspace& terrain, MapPoint from, MapPoint to,
                  float radius, TerrainId paint, bool preservePlaza = false) noexcept {
    for (std::uint16_t y = 0; y < kMapRows; ++y) {
        for (std::uint16_t x = 0; x < kMapColumns; ++x) {
            const auto current = terrain.get(x, y);
            if (current == TerrainId::Water || (preservePlaza && current == TerrainId::Plaza)) continue;
            if ((paint == TerrainId::Grass && value(current) >= value(TerrainId::Dirt))
                || (paint == TerrainId::Dirt && value(current) >= value(TerrainId::Road))
                || (paint == TerrainId::Road && value(current) >= value(TerrainId::Plaza))) continue;
            const MapPoint point{static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F};
            if (distanceToSegment(point, from, to) <= radius) terrain.set(x, y, paint);
        }
    }
}

void paintEllipse(TerrainWorkspace& terrain, MapPoint center, float radiusX,
                  float radiusY, TerrainId paint, std::uint32_t seed) noexcept {
    for (std::uint16_t y = 0; y < kMapRows; ++y) {
        for (std::uint16_t x = 0; x < kMapColumns; ++x) {
            if (terrain.get(x, y) == TerrainId::Water) continue;
            const auto nx = (static_cast<float>(x) + 0.5F - center.x) / radiusX;
            const auto ny = (static_cast<float>(y) + 0.5F - center.y) / radiusY;
            if (std::hypot(nx, ny) + edgeNoise(x, y, 8801U, seed) * 0.10F <= 1.0F) {
                terrain.set(x, y, paint);
            }
        }
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

bool transitionFill(TerrainId a, TerrainId b, TerrainId& fill) noexcept {
    BoundaryId ignored{};
    if (a == b || boundaryFor(a, b, ignored)) return false;
    const auto lower = value(a) < value(b) ? a : b;
    const auto upper = lower == a ? b : a;
    if (lower == TerrainId::Water && upper != TerrainId::Sand) {
        fill = TerrainId::Sand;
    } else if (lower == TerrainId::Sand && value(upper) >= value(TerrainId::Dirt)) {
        fill = TerrainId::Grass;
    } else if (lower == TerrainId::Grass && value(upper) >= value(TerrainId::Road)) {
        fill = TerrainId::Dirt;
    } else {
        return false;
    }
    return true;
}

void normalizeTerrain(TerrainWorkspace& terrain,
                      std::array<std::uint8_t, kMapTileCount>& scratch) noexcept {
    constexpr std::array<std::array<std::int8_t, 2>, 4> kNeighbors{{
        {{0, -1}}, {{1, 0}}, {{0, 1}}, {{-1, 0}},
    }};
    for (std::uint8_t pass = 0; pass < 4; ++pass) {
        scratch.fill(0xffU);
        bool changed = false;
        for (std::int32_t y = 0; y < kMapRows; ++y) {
            for (std::int32_t x = 0; x < kMapColumns; ++x) {
                const auto own = terrain.get(static_cast<std::uint16_t>(x), static_cast<std::uint16_t>(y));
                for (const auto offset : kNeighbors) {
                    const auto nx = x + offset[0];
                    const auto ny = y + offset[1];
                    if (nx < 0 || ny < 0 || nx >= kMapColumns || ny >= kMapRows) continue;
                    TerrainId fill{};
                    if (!transitionFill(own, terrain.get(static_cast<std::uint16_t>(nx),
                                                         static_cast<std::uint16_t>(ny)), fill)) continue;
                    scratch[static_cast<std::size_t>(y) * kMapColumns
                            + static_cast<std::size_t>(x)] = value(fill);
                    scratch[static_cast<std::size_t>(ny) * kMapColumns
                            + static_cast<std::size_t>(nx)] = value(fill);
                    changed = true;
                }
            }
        }
        if (!changed) return;
        for (std::uint16_t y = 0; y < kMapRows; ++y) {
            for (std::uint16_t x = 0; x < kMapColumns; ++x) {
                const auto update = scratch[static_cast<std::size_t>(y) * kMapColumns + x];
                if (update == 0xffU) continue;
                terrain.set(x, y, static_cast<TerrainId>(update));
            }
        }
    }
}

void smoothTerrain(TerrainWorkspace& terrain,
                   std::array<std::uint8_t, kMapTileCount>& scratch) noexcept {
    for (std::uint8_t pass = 0; pass < 2; ++pass) {
        scratch.fill(0xffU);
        for (std::uint16_t y = 1; y + 1 < kMapRows; ++y) {
            for (std::uint16_t x = 1; x + 1 < kMapColumns; ++x) {
                std::array<std::uint8_t, 6> counts{};
                for (std::int8_t oy = -1; oy <= 1; ++oy) {
                    for (std::int8_t ox = -1; ox <= 1; ++ox) {
                        if (ox == 0 && oy == 0) continue;
                        ++counts[value(terrain.get(static_cast<std::uint16_t>(x + ox),
                                                   static_cast<std::uint16_t>(y + oy)))];
                    }
                }
                const auto current = terrain.get(x, y);
                auto dominant = current;
                std::uint8_t bestCount = 0;
                for (std::uint8_t index = 0; index < counts.size(); ++index) {
                    if (counts[index] > bestCount || (counts[index] == bestCount
                                                       && index > value(dominant))) {
                        dominant = static_cast<TerrainId>(index);
                        bestCount = counts[index];
                    }
                }
                if (dominant != current && counts[value(current)] <= 1U && bestCount >= 5U) {
                    scratch[static_cast<std::size_t>(y) * kMapColumns + x] = value(dominant);
                }
            }
        }
        for (std::uint16_t y = 1; y + 1 < kMapRows; ++y) {
            for (std::uint16_t x = 1; x + 1 < kMapColumns; ++x) {
                const auto update = scratch[static_cast<std::size_t>(y) * kMapColumns + x];
                if (update != 0xffU) terrain.set(x, y, static_cast<TerrainId>(update));
            }
        }
    }
    normalizeTerrain(terrain, scratch);
}

bool nearBakedObject(const WorldMap& map, std::int32_t x, std::int32_t y,
                     std::int32_t radius,
                     const std::array<bool, 128>& objectPatterns) noexcept {
    for (auto oy = -radius; oy <= radius; ++oy) {
        for (auto ox = -radius; ox <= radius; ++ox) {
            const auto nx = x + ox;
            const auto ny = y + oy;
            if (nx < 0 || ny < 0 || nx >= kMapColumns || ny >= kMapRows) continue;
            if (objectPatterns[map.tile(static_cast<std::uint16_t>(nx),
                                        static_cast<std::uint16_t>(ny)) & kTileIndexMask]) return true;
        }
    }
    return false;
}

bool placeSparseObjects(WorldMap& map, const TerrainWorkspace& terrain,
                        BakedObjectId object, std::uint16_t count,
                        std::uint32_t salt, std::int32_t minimumDistance,
                        bool collision, const std::array<bool, 128>& objectPatterns,
                        const pixel_twins::BackgroundAssetPackView& assets) noexcept {
    for (std::uint16_t placed = 0; placed < count; ++placed) {
        auto bestScore = 0xffffffffU;
        std::uint16_t bestX = 0;
        std::uint16_t bestY = 0;
        bool found = false;
        for (std::uint16_t y = 2; y + 2 < kMapRows; ++y) {
            for (std::uint16_t x = 2; x + 2 < kMapColumns; ++x) {
                if (!plainGrass(terrain, x, y)
                    || nearBakedObject(map, x, y, minimumDistance, objectPatterns)) continue;
                const auto score = tileHash(x, y, salt, map.seed);
                if (!found || score < bestScore) {
                    found = true;
                    bestScore = score;
                    bestX = x;
                    bestY = y;
                }
            }
        }
        if (!found) return true;
        if (!putObject(map, bestX, bestY, object, collision, assets)) return false;
    }
    return true;
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

bool WorldMap::isWater(std::uint16_t x, std::uint16_t y) const noexcept {
    const auto tileValue = tile(x, y);
    return patternCollisionShapes[tileValue & kTileIndexMask] == kCollisionShapeWater;
}

bool WorldMap::collides(std::uint16_t x, std::uint16_t y) const noexcept {
    return (tile(x, y) & kCollisionBit) != 0;
}

bool WorldMap::terrainPointIsWalkable(float x, float y) const noexcept {
    if (x < 0.0F || y < 0.0F
        || x >= static_cast<float>(kMapColumns * kMapTileSize)
        || y >= static_cast<float>(kMapRows * kMapTileSize)) return false;
    const auto tx = static_cast<std::uint16_t>(x / static_cast<float>(kMapTileSize));
    const auto ty = static_cast<std::uint16_t>(y / static_cast<float>(kMapTileSize));
    const auto tileValue = tile(tx, ty);
    const auto shape = patternCollisionShapes[tileValue & kTileIndexMask];
    if (shape == kCollisionShapeNone && (tileValue & kCollisionBit) != 0U) return false;
    if (shape == kCollisionShapeWater) return false;
    if ((shape & 0xf0U) != kCollisionShapeCoastBase) return true;
    const auto mask = static_cast<std::uint8_t>(shape & 0x0fU);
    const auto u = (x - static_cast<float>(tx * kMapTileSize)) / static_cast<float>(kMapTileSize);
    const auto v = (y - static_cast<float>(ty * kMapTileSize)) / static_cast<float>(kMapTileSize);
    const auto topLeft = (mask & 1U) != 0U ? 1.0F : 0.0F;
    const auto topRight = (mask & 2U) != 0U ? 1.0F : 0.0F;
    const auto bottomRight = (mask & 4U) != 0U ? 1.0F : 0.0F;
    const auto bottomLeft = (mask & 8U) != 0U ? 1.0F : 0.0F;
    const auto land = topLeft * (1.0F - u) * (1.0F - v) + topRight * u * (1.0F - v)
        + bottomRight * u * v + bottomLeft * (1.0F - u) * v;
    return land >= 0.5F;
}

bool WorldMap::circleIsWalkable(float x, float y, float radius) const noexcept {
    if (!terrainPointIsWalkable(x, y)) return false;
    const auto minX = static_cast<std::int32_t>(std::floor((x - radius) / kMapTileSize));
    const auto maxX = static_cast<std::int32_t>(std::floor((x + radius) / kMapTileSize));
    const auto minY = static_cast<std::int32_t>(std::floor((y - radius) / kMapTileSize));
    const auto maxY = static_cast<std::int32_t>(std::floor((y + radius) / kMapTileSize));
    for (auto ty = minY; ty <= maxY; ++ty) {
        for (auto tx = minX; tx <= maxX; ++tx) {
            if (tx < 0 || ty < 0 || tx >= kMapColumns || ty >= kMapRows) return false;
            const auto tileValue = tile(static_cast<std::uint16_t>(tx), static_cast<std::uint16_t>(ty));
            const auto shape = patternCollisionShapes[tileValue & kTileIndexMask];
            const auto legacyFullTile = shape == kCollisionShapeNone
                && (tileValue & kCollisionBit) != 0U;
            if (shape != kCollisionShapeDecoration && !legacyFullTile) continue;
            const auto inset = legacyFullTile ? 0.0F : 10.0F;
            const auto left = static_cast<float>(tx * kMapTileSize) + inset;
            const auto top = static_cast<float>(ty * kMapTileSize) + inset;
            const auto right = static_cast<float>((tx + 1) * kMapTileSize) - inset;
            const auto bottom = static_cast<float>((ty + 1) * kMapTileSize) - inset;
            const auto nearestX = std::clamp(x, left, right);
            const auto nearestY = std::clamp(y, top, bottom);
            const auto dx = x - nearestX;
            const auto dy = y - nearestY;
            if (dx * dx + dy * dy < radius * radius) return false;
        }
    }
    return true;
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
    result.patternCollisionShapes.fill(kCollisionShapeNone);
    for (std::uint8_t variant = 0; variant < 4; ++variant) {
        std::uint8_t pattern = 0;
        if (!assets.terrainPattern(value(TerrainId::Water), variant, pattern)) return false;
        result.patternCollisionShapes[pattern] = kCollisionShapeWater;
    }
    for (std::uint8_t mask = 1; mask < 15; ++mask) {
        std::uint8_t pattern = 0;
        if (!assets.boundaryPattern(value(BoundaryId::WaterToSand), mask, pattern)) return false;
        result.patternCollisionShapes[pattern] = static_cast<std::uint8_t>(kCollisionShapeCoastBase | mask);
    }
    constexpr std::array<BakedObjectId, 5> kCollisionObjects{{
        BakedObjectId::RockGrass, BakedObjectId::ShrubGrass, BakedObjectId::PlazaPedestal,
        BakedObjectId::SealInactivePlaza, BakedObjectId::SealActivePlaza,
    }};
    for (const auto object : kCollisionObjects) {
        std::uint8_t pattern = 0;
        if (!assets.objectPattern(value(object), pattern)) return false;
        result.patternCollisionShapes[pattern] = kCollisionShapeDecoration;
    }

    constexpr MapPoint kCenter{50.0F, 50.0F};
    constexpr std::array<MapPoint, 3> kBaseSeals{{
        {16.875F, 22.5F}, {82.5F, 25.3125F}, {49.375F, 82.5F},
    }};
    constexpr std::array<std::array<MapPoint, 4>, 3> kBaseRoutes{{
        {{{40.9375F, 43.125F}, {33.4375F, 38.125F}, {27.8125F, 32.5F}, {21.875F, 31.25F}}},
        {{{56.875F, 40.625F}, {63.4375F, 43.125F}, {70.3125F, 35.0F}, {76.5625F, 33.4375F}}},
        {{{47.5F, 56.25F}, {40.625F, 61.25F}, {44.375F, 69.6875F}, {52.1875F, 74.0625F}}},
    }};
    std::array<std::array<MapPoint, 5>, 3> routes{};
    const auto phase = seededRange(7001U, 0.0F, 6.2831853F, seed);
    for (std::uint8_t stone = 0; stone < result.seals.size(); ++stone) {
        auto point = rotatePoint(kBaseSeals[stone], phase,
                                 1.0F + seededUnit(stone * 97U + 3001U, seed) * 0.09F);
        point = jitterPoint(point, 4.75F, stone * 97U, seed);
        result.seals[stone] = {
            static_cast<std::uint8_t>(std::floor(point.x)),
            static_cast<std::uint8_t>(std::floor(point.y)),
        };
        for (std::uint8_t index = 0; index < 4; ++index) {
            auto route = rotatePoint(kBaseRoutes[stone][index], phase,
                1.0F + seededUnit(stone * 97U + index * 19U + 5003U, seed) * 0.08F);
            routes[stone][index] = jitterPoint(route, 4.0F, stone * 97U + index * 19U + 7U, seed);
        }
        routes[stone][4] = {static_cast<float>(result.seals[stone].x) + 0.5F,
                            static_cast<float>(result.seals[stone].y) + 0.5F};
    }

    std::array<IslandNode, 8> nodes{{
        {kCenter, 15.625F, 101},
        {routes[0][4], 12.8125F, 137}, {routes[1][4], 13.75F, 173},
        {routes[2][4], 13.4375F, 211}, {}, {}, {}, {},
    }};
    const auto supportPhase = seededRange(8101U, 0.0F, 6.2831853F, seed);
    for (std::uint8_t index = 0; index < 4; ++index) {
        const auto angle = supportPhase + static_cast<float>(index) * 1.5707963F
            + seededUnit(8200U + index * 31U, seed) * 0.48F;
        const auto distance = seededRange(8300U + index * 37U, 16.25F, 27.5F, seed);
        nodes[index + 4] = {
            {std::clamp(kCenter.x + std::cos(angle) * distance, 5.0F, 95.0F),
             std::clamp(kCenter.y + std::sin(angle) * distance, 5.0F, 95.0F)},
            seededRange(8400U + index * 41U, 9.0625F, 15.0F, seed),
            static_cast<std::uint16_t>(251U + index * 43U),
        };
    }
    std::array<IslandSegment, 15> segments{};
    std::uint8_t segmentCount = 0;
    for (const auto& route : routes) {
        auto from = kCenter;
        for (const auto to : route) {
            segments[segmentCount] = {from, to, 10.75F,
                static_cast<std::uint16_t>(401U + segmentCount * 29U)};
            ++segmentCount;
            from = to;
        }
    }
    for (std::uint16_t y = 0; y < kMapRows; ++y) {
        for (std::uint16_t x = 0; x < kMapColumns; ++x) {
            const MapPoint point{static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F};
            auto strength = -1000.0F;
            for (const auto& node : nodes) {
                const auto noise = edgeNoise(x, y, node.salt, seed) * 0.14F
                    + edgeNoise(x / 3, y / 3, node.salt + 503U, seed) * 0.08F;
                strength = std::max(strength,
                    1.0F - std::hypot(point.x - node.point.x, point.y - node.point.y) / node.radius + noise);
            }
            for (const auto& segment : segments) {
                const auto noise = edgeNoise(x, y, segment.salt, seed) * 0.12F
                    + edgeNoise(x / 2, y / 2, segment.salt + 607U, seed) * 0.08F;
                strength = std::max(strength,
                    1.0F - distanceToSegment(point, segment.from, segment.to) / segment.radius + noise);
            }
            if (strength > 0.0F) workspace.set(x, y, TerrainId::Grass);
        }
    }
    for (std::uint16_t y = 0; y < kMapRows; ++y) {
        for (std::uint16_t x = 0; x < kMapColumns; ++x) {
            const MapPoint point{static_cast<float>(x) + 0.5F, static_cast<float>(y) + 0.5F};
            if (std::hypot(point.x - kCenter.x, point.y - kCenter.y) <= 8.125F) {
                workspace.set(x, y, TerrainId::Grass);
            }
            for (const auto seal : result.seals) {
                if (std::hypot(point.x - (static_cast<float>(seal.x) + 0.5F),
                               point.y - (static_cast<float>(seal.y) + 0.5F)) <= 5.625F) {
                    workspace.set(x, y, TerrainId::Grass);
                }
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

    for (const auto& route : routes) {
        auto from = kCenter;
        for (const auto to : route) {
            paintSegment(workspace, from, to, 4.625F, TerrainId::Grass);
            paintSegment(workspace, from, to, 2.75F, TerrainId::Dirt);
            paintSegment(workspace, from, to, 1.25F, TerrainId::Road);
            from = to;
        }
    }
    normalizeTerrain(workspace, result.tiles);
    smoothTerrain(workspace, result.tiles);
    paintEllipse(workspace, kCenter, 7.25F, 7.25F, TerrainId::Dirt, seed);
    paintEllipse(workspace, kCenter, 6.125F, 6.125F, TerrainId::Road, seed);
    paintEllipse(workspace, kCenter, 4.875F, 4.875F, TerrainId::Plaza, seed);
    for (const auto seal : result.seals) {
        paintEllipse(workspace,
                     {static_cast<float>(seal.x) + 0.5F, static_cast<float>(seal.y) + 0.5F},
                     4.5F, 3.625F, TerrainId::Plaza, seed);
    }
    for (const auto& route : routes) {
        auto from = kCenter;
        for (const auto to : route) {
            paintSegment(workspace, from, to, 4.25F, TerrainId::Grass, true);
            paintSegment(workspace, from, to, 2.375F, TerrainId::Dirt, true);
            paintSegment(workspace, from, to, 1.125F, TerrainId::Road, true);
            from = to;
        }
    }

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
    std::array<bool, 128> objectPatterns{};
    for (std::uint8_t index = 0; index < assets.objectCount(); ++index) {
        std::uint8_t pattern = 0;
        if (!assets.objectPattern(index, pattern)) return false;
        objectPatterns[pattern] = true;
    }
    constexpr std::array<std::array<std::int8_t, 2>, 8> kPedestals{{
        {{0, -3}}, {{2, -2}}, {{3, 0}}, {{2, 2}},
        {{0, 3}}, {{-2, 2}}, {{-3, 0}}, {{-2, -2}},
    }};
    for (const auto offset : kPedestals) {
        const auto x = static_cast<std::uint16_t>(50 + offset[0]);
        const auto y = static_cast<std::uint16_t>(50 + offset[1]);
        if (workspace.get(x, y) != TerrainId::Plaza) continue;
        if (!putObject(result,
                       x, y,
                       BakedObjectId::PlazaPedestal,
                       true,
                       assets)) {
            return false;
        }
    }
    constexpr std::array<std::uint16_t, 6> kLimits{{48, 42, 156, 66, 66, 66}};
    constexpr std::array<std::uint16_t, 6> kSalts{{2301, 2302, 2303, 2304, 2305, 2306}};
    constexpr std::array<std::int32_t, 6> kDistances{{2, 2, 1, 1, 1, 1}};
    constexpr std::array<BakedObjectId, 6> kObjects{{
        BakedObjectId::RockGrass,
        BakedObjectId::ShrubGrass,
        BakedObjectId::GrassClumpGrass,
        BakedObjectId::FlowerWhiteGrass,
        BakedObjectId::FlowerBlueGrass,
        BakedObjectId::FlowerYellowGrass,
    }};
    for (std::uint8_t slot = 0; slot < kObjects.size(); ++slot) {
        if (!placeSparseObjects(result, workspace, kObjects[slot], kLimits[slot],
                                kSalts[slot], kDistances[slot], slot <= 1,
                                objectPatterns, assets)) return false;
    }
    return true;
}

} // namespace wizward::world
