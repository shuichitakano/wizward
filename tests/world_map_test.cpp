#include "assets/game_assets.hpp"
#include "world/world_map.hpp"

#include "pixel_twins/framebuffer.hpp"
#include "pixel_twins/render_target.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>

int main() {
    wizward::assets::GameAssets assets;
    assert(assets.initialize());
    wizward::world::TerrainWorkspace workspace;
    wizward::world::WorldMap first;
    wizward::world::MapGenerator generator;
    assert(generator.generate(12345, assets.background(), workspace, first));
    const auto assertNoTerrainJunctions = [](const wizward::world::TerrainWorkspace& terrain) {
        for (std::uint16_t cy = 1; cy < wizward::world::kMapRows; ++cy) {
            for (std::uint16_t cx = 1; cx < wizward::world::kMapColumns; ++cx) {
                std::array<bool, 6> terrains{};
                terrains[static_cast<std::size_t>(terrain.get(cx - 1, cy - 1))] = true;
                terrains[static_cast<std::size_t>(terrain.get(cx, cy - 1))] = true;
                terrains[static_cast<std::size_t>(terrain.get(cx, cy))] = true;
                terrains[static_cast<std::size_t>(terrain.get(cx - 1, cy))] = true;
                assert(std::count(terrains.begin(), terrains.end(), true) <= 2);
            }
        }
    };
    assertNoTerrainJunctions(workspace);

    for (const auto tile : first.tiles) {
        assert((tile & wizward::world::kTileIndexMask) < assets.background().patternCount());
    }
    assert(std::any_of(first.tiles.begin(), first.tiles.end(), [](std::uint8_t tile) {
        return (tile & wizward::world::kCollisionBit) != 0;
    }));
    constexpr std::array<std::uint16_t, 6> kDecorationCounts{{48, 42, 156, 66, 66, 66}};
    constexpr std::array<wizward::assets::BakedObjectId, 6> kDecorations{{
        wizward::assets::BakedObjectId::RockGrass,
        wizward::assets::BakedObjectId::ShrubGrass,
        wizward::assets::BakedObjectId::GrassClumpGrass,
        wizward::assets::BakedObjectId::FlowerWhiteGrass,
        wizward::assets::BakedObjectId::FlowerBlueGrass,
        wizward::assets::BakedObjectId::FlowerYellowGrass,
    }};
    std::size_t decorationCount = 0;
    for (std::size_t index = 0; index < kDecorations.size(); ++index) {
        std::uint8_t pattern = 0;
        assert(assets.background().objectPattern(
            static_cast<std::uint8_t>(kDecorations[index]), pattern));
        const auto actualCount = std::count_if(first.tiles.begin(), first.tiles.end(), [pattern](std::uint8_t tile) {
            return (tile & wizward::world::kTileIndexMask) == pattern;
        });
        decorationCount += static_cast<std::size_t>(actualCount);
        if (index < 2) assert(actualCount > 0);
        assert(actualCount <= kDecorationCounts[index]);
    }
    assert(decorationCount > 0);

    const auto background = first.background(assets.background());
    assert(background.width == wizward::world::kMapColumns);
    assert(background.height == wizward::world::kMapRows);
    assert(background.tilemap == first.tiles.data());
    assert(background.tileIndexMask == wizward::world::kTileIndexMask);
    bool foundWater = false;
    bool foundLand = false;
    for (std::uint16_t y = 0; y < wizward::world::kMapRows; ++y) {
        for (std::uint16_t x = 0; x < wizward::world::kMapColumns; ++x) {
            if (first.isWater(x, y)) {
                foundWater = true;
                assert(first.collides(x, y));
            } else {
                foundLand = true;
            }
        }
    }
    assert(foundWater);
    assert(foundLand);
    std::uint8_t fullLandCoastPattern = 0;
    assert(assets.background().boundaryPattern(
        static_cast<std::uint8_t>(wizward::assets::BoundaryId::WaterToSand), 15,
        fullLandCoastPattern));
    assert(std::none_of(first.tiles.begin(), first.tiles.end(), [fullLandCoastPattern](std::uint8_t tile) {
        return (tile & wizward::world::kTileIndexMask) == fullLandCoastPattern
            && (tile & wizward::world::kCollisionBit) != 0U;
    }));
    pixel_twins::Framebuffer framebuffer;
    auto target = pixel_twins::makeRenderTarget(framebuffer.drawBuffer(), pixel_twins::Screen::Left);
    first.draw(target, assets.background(), 0, 0);
    const auto firstPattern = static_cast<std::size_t>(
        first.tiles[0] & wizward::world::kTileIndexMask);
    assert(framebuffer.drawBuffer()[0]
           == assets.background().patterns()[firstPattern * 32U * 32U]);

    wizward::world::WorldMap same;
    assert(generator.generate(12345, assets.background(), workspace, same));
    assert(first.tiles == same.tiles);
    assert(first.seals == same.seals);

    wizward::world::WorldMap different;
    assert(generator.generate(54321, assets.background(), workspace, different));
    assert(first.tiles != different.tiles);
    for (std::uint32_t seed = 1; seed <= 16; ++seed) {
        wizward::world::WorldMap generated;
        assert(generator.generate(seed * 2654435761U, assets.background(), workspace, generated));
        assert(generated.seed == seed * 2654435761U);
        assertNoTerrainJunctions(workspace);
        assert(std::none_of(generated.tiles.begin(), generated.tiles.end(),
            [fullLandCoastPattern](std::uint8_t tile) {
                return (tile & wizward::world::kTileIndexMask) == fullLandCoastPattern
                    && (tile & wizward::world::kCollisionBit) != 0U;
            }));
    }

    const auto seal = first.seals[0];
    const auto inactive = first.tile(seal.x, seal.y);
    const auto sealLeft = static_cast<float>(seal.x * wizward::world::kMapTileSize);
    const auto sealTop = static_cast<float>(seal.y * wizward::world::kMapTileSize);
    assert(!first.circleIsWalkable(sealLeft + 16.0F, sealTop + 16.0F, 8.0F));
    assert(first.circleIsWalkable(sealLeft + 1.0F, sealTop + 1.0F, 2.0F));

    bool foundInterpolatedCoast = false;
    for (std::uint16_t y = 0; y < wizward::world::kMapRows && !foundInterpolatedCoast; ++y) {
        for (std::uint16_t x = 0; x < wizward::world::kMapColumns; ++x) {
            const auto left = static_cast<float>(x * wizward::world::kMapTileSize);
            const auto top = static_cast<float>(y * wizward::world::kMapTileSize);
            const std::array<bool, 4> samples{{
                first.terrainPointIsWalkable(left + 4.0F, top + 4.0F),
                first.terrainPointIsWalkable(left + 28.0F, top + 4.0F),
                first.terrainPointIsWalkable(left + 28.0F, top + 28.0F),
                first.terrainPointIsWalkable(left + 4.0F, top + 28.0F),
            }};
            foundInterpolatedCoast = std::any_of(samples.begin(), samples.end(),
                                                 [](bool value) { return value; })
                && std::any_of(samples.begin(), samples.end(),
                               [](bool value) { return !value; });
            if (foundInterpolatedCoast) break;
        }
    }
    assert(foundInterpolatedCoast);
    assert(first.activateSeal(0, assets.background()));
    assert(first.tile(seal.x, seal.y) != inactive);
    assert(first.collides(seal.x, seal.y));
    assert(!first.activateSeal(3, assets.background()));
    return 0;
}
