#pragma once
#include "core/tilemap.h"
#include "core/rng.h"
#include <vector>

struct Room {
    int x, y, w, h;

    int cx() const { return x + w / 2; }
    int cy() const { return y + h / 2; }

    bool intersects(const Room& other, int pad = 1) const {
        return x - pad < other.x + other.w && x + w + pad > other.x &&
               y - pad < other.y + other.h && y + h + pad > other.y;
    }
};

struct DungeonParams {
    int width = 80;
    int height = 50;
    int room_min_w = 5;    // minimum room width
    int room_max_w = 12;   // maximum room width
    int room_min_h = 5;    // minimum room height
    int room_max_h = 12;   // maximum room height
    int max_rooms = 15;
    int corridor_width = 1; // width of carved corridors (1-3)
    TileType wall_type = TileType::WALL_STONE_BRICK;
    TileType floor_type = TileType::FLOOR_STONE;
};

struct DungeonResult {
    TileMap map;
    std::vector<Room> rooms;
    int start_x = 0, start_y = 0; // player spawn
    int stairs_x = 0, stairs_y = 0; // stairs down position
};

namespace dungeon {

// Generate a dungeon using BSP-style room placement + corridors
// If place_stairs_down is false, no downward staircase is placed (zone bottom).
DungeonResult generate(RNG& rng, const DungeonParams& params, bool place_stairs_down = true);

} // namespace dungeon
