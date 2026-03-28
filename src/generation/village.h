#pragma once
#include "core/tilemap.h"
#include "core/rng.h"
#include "generation/dungeon.h"
#include <vector>

struct VillageResult {
    TileMap map;
    std::vector<Room> buildings; // interior rooms of buildings
    int start_x = 0, start_y = 0;
    int dungeon_x = 0, dungeon_y = 0; // stairs down to first dungeon
};

namespace village {

// Generate the starting village — outdoor area with a few buildings and a dungeon entrance
VillageResult generate(RNG& rng);

} // namespace village
