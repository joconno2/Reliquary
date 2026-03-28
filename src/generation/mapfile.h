#pragma once
#include "core/tilemap.h"
#include "core/ecs.h"
#include <string>
#include <vector>

// Loads a hand-authored map from a text file.
// Each character = one tile. Map legend:
//
//   TERRAIN:
//   .  grass
//   ,  dirt path
//   :  stone floor
//   #  stone brick wall
//   T  tree (blocks movement + sight)
//   t  brush/undergrowth (walkable, doesn't block sight)
//   ~  water
//   "  tall grass (same as brush visually, walkable)
//
//   FEATURES:
//   +  closed door
//   >  stairs down
//   <  stairs up
//
//   NPCS (spawned as entities, tile becomes floor beneath):
//   @  player start
//   S  shopkeeper
//   B  blacksmith
//   P  priest/scholar
//   F  farmer
//   G  guard
//
//   SPACE or anything else = void (impassable, invisible)

struct MapEntity {
    char glyph;
    int x, y;
};

struct MapFileResult {
    TileMap map;
    int start_x = 0, start_y = 0;
    std::vector<MapEntity> entities; // NPCs and other spawned things
};

namespace mapfile {

// Load a map from a text file. Returns empty map on failure.
MapFileResult load(const std::string& path);

} // namespace mapfile
