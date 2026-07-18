#pragma once

#include <cstdint>

namespace wizward::assets {

enum class TerrainId : std::uint8_t {
    Water = 0,
    Sand = 1,
    Grass = 2,
    Dirt = 3,
    Road = 4,
    Plaza = 5,
};

enum class BoundaryId : std::uint8_t {
    WaterToSand = 0,
    SandToGrass = 1,
    GrassToDirt = 2,
    DirtToRoad = 3,
    DirtToPlaza = 4,
    RoadToPlaza = 5,
};

enum class BakedObjectId : std::uint8_t {
    RockGrass = 0,
    GrassClumpGrass = 1,
    FlowerWhiteGrass = 2,
    FlowerBlueGrass = 3,
    FlowerYellowGrass = 4,
    ShrubGrass = 5,
    PlazaPedestal = 6,
    SealInactivePlaza = 7,
    SealActivePlaza = 8,
};

inline constexpr std::uint8_t kBackgroundPatternCount = 123;
inline constexpr std::uint8_t kTerrainVariantCount = 4;
inline constexpr std::uint8_t kBoundaryMaskCount = 16;

} // namespace wizward::assets
