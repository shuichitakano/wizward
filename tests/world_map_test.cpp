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

    for (const auto tile : first.tiles) {
        assert((tile & wizward::world::kTileIndexMask) < assets.background().patternCount());
    }
    assert(std::any_of(first.tiles.begin(), first.tiles.end(), [](std::uint8_t tile) {
        return (tile & wizward::world::kCollisionBit) != 0;
    }));
    const auto background = first.background(assets.background());
    assert(background.width == wizward::world::kMapColumns);
    assert(background.height == wizward::world::kMapRows);
    assert(background.tilemap == first.tiles.data());
    assert(background.tileIndexMask == wizward::world::kTileIndexMask);
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

    const auto seal = first.seals[0];
    const auto inactive = first.tile(seal.x, seal.y);
    assert(first.activateSeal(0, assets.background()));
    assert(first.tile(seal.x, seal.y) != inactive);
    assert(first.collides(seal.x, seal.y));
    assert(!first.activateSeal(3, assets.background()));
    return 0;
}
