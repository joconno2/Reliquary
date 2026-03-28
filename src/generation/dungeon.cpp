#include "generation/dungeon.h"

namespace dungeon {

// Assign floor variant: 0 = clean (most tiles), 1-2 = scattered detail
static uint8_t floor_variant(RNG& rng) {
    // ~65% clean, ~35% detail
    if (rng.chance(35)) return static_cast<uint8_t>(rng.range(1, 2));
    return 0;
}

static void carve_room(TileMap& map, const Room& room, TileType floor, RNG& rng) {
    for (int y = room.y; y < room.y + room.h; y++) {
        for (int x = room.x; x < room.x + room.w; x++) {
            if (map.in_bounds(x, y)) {
                auto& tile = map.at(x, y);
                tile.type = floor;
                tile.variant = floor_variant(rng);
            }
        }
    }
}

static void carve_h_tunnel(TileMap& map, int x1, int x2, int y, TileType floor, RNG& rng) {
    int start = std::min(x1, x2);
    int end = std::max(x1, x2);
    for (int x = start; x <= end; x++) {
        if (map.in_bounds(x, y)) {
            auto& tile = map.at(x, y);
            tile.type = floor;
            tile.variant = floor_variant(rng);
        }
    }
}

static void carve_v_tunnel(TileMap& map, int y1, int y2, int x, TileType floor, RNG& rng) {
    int start = std::min(y1, y2);
    int end = std::max(y1, y2);
    for (int y = start; y <= end; y++) {
        if (map.in_bounds(x, y)) {
            auto& tile = map.at(x, y);
            tile.type = floor;
            tile.variant = floor_variant(rng);
        }
    }
}

DungeonResult generate(RNG& rng, const DungeonParams& params) {
    DungeonResult result;
    result.map = TileMap(params.width, params.height);

    // Fill with walls
    for (int y = 0; y < params.height; y++) {
        for (int x = 0; x < params.width; x++) {
            result.map.at(x, y).type = params.wall_type;
        }
    }

    // Place rooms
    for (int i = 0; i < params.max_rooms * 3; i++) { // try more times than max
        if (static_cast<int>(result.rooms.size()) >= params.max_rooms) break;

        Room room;
        room.w = rng.range(params.room_min, params.room_max);
        room.h = rng.range(params.room_min, params.room_max);
        room.x = rng.range(1, params.width - room.w - 1);
        room.y = rng.range(1, params.height - room.h - 1);

        bool ok = true;
        for (auto& existing : result.rooms) {
            if (room.intersects(existing, 2)) {
                ok = false;
                break;
            }
        }

        if (!ok) continue;

        carve_room(result.map, room, params.floor_type, rng);

        // Connect to previous room
        if (!result.rooms.empty()) {
            auto& prev = result.rooms.back();
            if (rng.chance(50)) {
                carve_h_tunnel(result.map, prev.cx(), room.cx(), prev.cy(), params.floor_type, rng);
                carve_v_tunnel(result.map, prev.cy(), room.cy(), room.cx(), params.floor_type, rng);
            } else {
                carve_v_tunnel(result.map, prev.cy(), room.cy(), prev.cx(), params.floor_type, rng);
                carve_h_tunnel(result.map, prev.cx(), room.cx(), room.cy(), params.floor_type, rng);
            }
        }

        result.rooms.push_back(room);
    }

    // Player starts in first room center
    if (!result.rooms.empty()) {
        result.start_x = result.rooms.front().cx();
        result.start_y = result.rooms.front().cy();
    }

    // Stairs down in last room center
    if (result.rooms.size() >= 2) {
        result.stairs_x = result.rooms.back().cx();
        result.stairs_y = result.rooms.back().cy();
        result.map.at(result.stairs_x, result.stairs_y).type = TileType::STAIRS_DOWN;
    }

    // Place doors at room entrances (where corridor meets room edge)
    // Simple heuristic: check corridor tiles adjacent to rooms
    for (int y = 1; y < params.height - 1; y++) {
        for (int x = 1; x < params.width - 1; x++) {
            if (!result.map.is_walkable(x, y)) continue;

            // A door candidate: walkable tile with walls on two opposite sides
            // and walkable tiles on the other two
            bool wall_n = result.map.is_opaque(x, y - 1);
            bool wall_s = result.map.is_opaque(x, y + 1);
            bool wall_e = result.map.is_opaque(x + 1, y);
            bool wall_w = result.map.is_opaque(x - 1, y);

            bool is_door_spot = (wall_n && wall_s && !wall_e && !wall_w) ||
                                (!wall_n && !wall_s && wall_e && wall_w);

            if (is_door_spot && rng.chance(30)) {
                result.map.at(x, y).type = TileType::DOOR_CLOSED;
            }
        }
    }

    return result;
}

} // namespace dungeon
