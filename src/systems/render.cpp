#include "systems/render.h"
#include "components/position.h"
#include "components/renderable.h"
#include <algorithm>
#include <vector>

namespace render {

// Tile sprite mappings into tiles.png
// Layout: tiles.txt group N = spritesheet row (N-1), letter = column (a=0, b=1, ...)
// Walls: row 0-5, col 0 = top view, col 1 = side view
// Floors: row 6+ , col 0 = blank bg, col 1-3 = variants with bg, col 4-6 = no bg
// Features: row 16 = doors/stairs/traps
SpriteRef tile_sprite(TileType type, uint8_t variant) {
    int v = variant % 3;
    switch (type) {
        // Walls — show side view (col 1)
        case TileType::WALL_DIRT:         return {SHEET_TILES, 1,  0};
        case TileType::WALL_STONE_ROUGH:  return {SHEET_TILES, 1,  1};
        case TileType::WALL_STONE_BRICK:  return {SHEET_TILES, 1,  2}; // could use col 2 for variant
        case TileType::WALL_IGNEOUS:      return {SHEET_TILES, 1,  3};
        case TileType::WALL_LARGE_STONE:  return {SHEET_TILES, 1,  4};
        case TileType::WALL_CATACOMB:     return {SHEET_TILES, 1,  5};

        // Floors — col 1-3 are variants with background fill
        // (row = group - 1): group 7 = row 6, group 8 = row 7, etc.
        case TileType::FLOOR_STONE:       return {SHEET_TILES, 1 + v, 6};
        case TileType::FLOOR_GRASS:       return {SHEET_TILES, 1 + v, 7};
        case TileType::FLOOR_DIRT:        return {SHEET_TILES, 1 + v, 8};
        case TileType::FLOOR_BONE:        return {SHEET_TILES, 1 + v, 10};
        case TileType::FLOOR_RED_STONE:   return {SHEET_TILES, 1 + v, 11};

        // Features — group 17 = row 16
        // 17.a = door1 (col 0), 17.b = door2 (col 1)
        // 17.c = framed door shut (col 2), 17.d = framed door open (col 3)
        // 17.h = staircase down (col 7), 17.i = staircase up (col 8)
        case TileType::DOOR_CLOSED:       return {SHEET_TILES, 2, 16};
        case TileType::DOOR_OPEN:         return {SHEET_TILES, 3, 16};
        case TileType::STAIRS_DOWN:       return {SHEET_TILES, 7, 16};
        case TileType::STAIRS_UP:         return {SHEET_TILES, 8, 16};
        case TileType::WATER:             return {SHEET_TILES, 0, 12};

        default:                          return {SHEET_TILES, 0, 6}; // blank dark grey
    }
}

void draw_map(SDL_Renderer* renderer, const SpriteManager& sprites,
              const TileMap& map, const Camera& cam, int y_offset) {
    constexpr int TS = SpriteManager::TILE_SIZE;

    int start_x = std::max(0, cam.x);
    int start_y = std::max(0, cam.y);
    int end_x = std::min(map.width(), cam.x + cam.tiles_wide() + 1);
    int end_y = std::min(map.height(), cam.y + cam.tiles_high() + 1);

    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            auto& tile = map.at(x, y);

            int screen_x = (x - cam.x) * TS;
            int screen_y = (y - cam.y) * TS + y_offset;

            if (tile.visible) {
                auto ref = tile_sprite(tile.type, tile.variant);
                sprites.draw_sprite(renderer, ref.sheet, ref.col, ref.row,
                                    screen_x, screen_y);
            } else if (tile.explored) {
                auto ref = tile_sprite(tile.type, tile.variant);
                // Dimmed — explored but not currently visible
                SDL_Color dim = {100, 100, 120, 255};
                sprites.draw_sprite(renderer, ref.sheet, ref.col, ref.row,
                                    screen_x, screen_y, 1, dim);
            }
            // Unexplored tiles: draw nothing (black)
        }
    }
}

void draw_entities(SDL_Renderer* renderer, const SpriteManager& sprites,
                   World& world, const TileMap& map, const Camera& cam,
                   int y_offset) {
    constexpr int TS = SpriteManager::TILE_SIZE;

    // Collect visible entities and sort by z_order
    struct DrawCmd {
        int z_order;
        int sheet, sx, sy;
        int dx, dy;
        SDL_Color tint;
    };
    std::vector<DrawCmd> cmds;

    auto& positions = world.pool<Position>();
    for (size_t i = 0; i < positions.size(); i++) {
        Entity e = positions.entity_at(i);
        if (!world.has<Renderable>(e)) continue;

        auto& pos = positions.at_index(i);
        auto& rend = world.get<Renderable>(e);

        if (!map.in_bounds(pos.x, pos.y)) continue;
        if (!map.at(pos.x, pos.y).visible) continue;

        int screen_x = (pos.x - cam.x) * TS;
        int screen_y = (pos.y - cam.y) * TS + y_offset;

        cmds.push_back({rend.z_order, rend.sprite_sheet, rend.sprite_x, rend.sprite_y,
                         screen_x, screen_y, rend.tint});
    }

    // Sort: lower z_order drawn first (corpses under creatures)
    std::sort(cmds.begin(), cmds.end(),
              [](const DrawCmd& a, const DrawCmd& b) { return a.z_order < b.z_order; });

    for (auto& cmd : cmds) {
        sprites.draw_sprite(renderer, cmd.sheet, cmd.sx, cmd.sy,
                            cmd.dx, cmd.dy, 1, cmd.tint);
    }
}

} // namespace render
