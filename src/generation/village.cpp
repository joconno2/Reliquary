#include "generation/village.h"

namespace village {

// Place a building: walls around perimeter, floor inside, door on one side
static Room place_building(TileMap& map, RNG& rng, int bx, int by, int bw, int bh) {
    // Walls
    for (int y = by; y < by + bh; y++) {
        for (int x = bx; x < bx + bw; x++) {
            if (!map.in_bounds(x, y)) continue;
            if (x == bx || x == bx + bw - 1 || y == by || y == by + bh - 1) {
                map.at(x, y).type = TileType::WALL_STONE_BRICK;
            } else {
                map.at(x, y).type = TileType::FLOOR_STONE;
                map.at(x, y).variant = rng.chance(30) ? static_cast<uint8_t>(rng.range(1, 2)) : 0;
            }
        }
    }

    // Door on south side (or another side if no room)
    int dx = bx + bw / 2;
    int dy = by + bh - 1;
    if (map.in_bounds(dx, dy)) {
        map.at(dx, dy).type = TileType::DOOR_CLOSED;
    }

    return {bx + 1, by + 1, bw - 2, bh - 2};
}

// Fill an area with scattered trees
static void place_trees(TileMap& map, RNG& rng, int x1, int y1, int x2, int y2, int density) {
    for (int y = y1; y < y2; y++) {
        for (int x = x1; x < x2; x++) {
            if (!map.in_bounds(x, y)) continue;
            if (map.at(x, y).type != TileType::FLOOR_GRASS) continue;
            if (rng.chance(density)) {
                map.at(x, y).type = TileType::TREE;
            }
        }
    }
}

VillageResult generate(RNG& rng) {
    constexpr int W = 60;
    constexpr int H = 40;

    VillageResult result;
    result.map = TileMap(W, H);

    // Fill with grass
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            auto& tile = result.map.at(x, y);
            tile.type = TileType::FLOOR_GRASS;
            tile.variant = rng.chance(25) ? static_cast<uint8_t>(rng.range(1, 2)) : 0;
        }
    }

    // Dirt paths — cross pattern through the village center
    int cx = W / 2;
    int cy = H / 2;

    // Horizontal path
    for (int x = 8; x < W - 8; x++) {
        for (int dy = -1; dy <= 0; dy++) {
            int y = cy + dy;
            if (result.map.in_bounds(x, y)) {
                result.map.at(x, y).type = TileType::FLOOR_DIRT;
                result.map.at(x, y).variant = rng.chance(40) ? static_cast<uint8_t>(rng.range(1, 2)) : 0;
            }
        }
    }

    // Vertical path
    for (int y = 6; y < H - 6; y++) {
        for (int dx = -1; dx <= 0; dx++) {
            int x = cx + dx;
            if (result.map.in_bounds(x, y)) {
                result.map.at(x, y).type = TileType::FLOOR_DIRT;
                result.map.at(x, y).variant = rng.chance(40) ? static_cast<uint8_t>(rng.range(1, 2)) : 0;
            }
        }
    }

    // Buildings — placed around the central crossroads
    // Shopkeeper's hut (northwest)
    result.buildings.push_back(
        place_building(result.map, rng, cx - 12, cy - 10, 7, 6));

    // Blacksmith (northeast)
    result.buildings.push_back(
        place_building(result.map, rng, cx + 4, cy - 10, 8, 6));

    // Temple (southwest)
    result.buildings.push_back(
        place_building(result.map, rng, cx - 12, cy + 3, 8, 7));

    // Inn (southeast)
    result.buildings.push_back(
        place_building(result.map, rng, cx + 4, cy + 3, 7, 6));

    // Forest edges — dense trees around the village perimeter
    place_trees(result.map, rng, 0, 0, W, 4, 50);       // north
    place_trees(result.map, rng, 0, H - 4, W, H, 50);   // south
    place_trees(result.map, rng, 0, 0, 5, H, 50);        // west
    place_trees(result.map, rng, W - 5, 0, W, H, 50);    // east

    // Scattered trees in open areas
    place_trees(result.map, rng, 5, 4, W - 5, H - 4, 8);

    // Dungeon entrance — a staircase on the east side of the village
    result.dungeon_x = W - 10;
    result.dungeon_y = cy;
    // Clear area around dungeon entrance
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int x = result.dungeon_x + dx;
            int y = result.dungeon_y + dy;
            if (result.map.in_bounds(x, y)) {
                result.map.at(x, y).type = TileType::FLOOR_STONE;
                result.map.at(x, y).variant = 0;
            }
        }
    }
    result.map.at(result.dungeon_x, result.dungeon_y).type = TileType::STAIRS_DOWN;

    // Player starts near the center crossroads
    result.start_x = cx;
    result.start_y = cy + 2;

    // Make sure start position is walkable
    if (result.map.in_bounds(result.start_x, result.start_y)) {
        result.map.at(result.start_x, result.start_y).type = TileType::FLOOR_DIRT;
    }

    return result;
}

} // namespace village
