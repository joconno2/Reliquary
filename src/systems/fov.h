#pragma once
#include "core/tilemap.h"

// Recursive shadowcasting FOV
// Based on the algorithm from RogueBasin / libtcod
namespace fov {

// Compute FOV from (ox, oy) with given radius.
// Clears all visible flags, then sets visible + explored on seen tiles.
void compute(TileMap& map, int ox, int oy, int radius);

} // namespace fov
