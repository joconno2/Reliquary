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
static bool is_floor_type(TileType type) {
    return type == TileType::FLOOR_STONE || type == TileType::FLOOR_DIRT ||
           type == TileType::FLOOR_GRASS || type == TileType::FLOOR_BONE ||
           type == TileType::FLOOR_RED_STONE;
}

// Floor tile row in spritesheet (tiles.txt group - 1)
static int floor_row(TileType type) {
    switch (type) {
        case TileType::FLOOR_STONE:     return 6;
        case TileType::FLOOR_GRASS:     return 7;
        case TileType::FLOOR_DIRT:      return 8;
        case TileType::FLOOR_BONE:      return 10;
        case TileType::FLOOR_RED_STONE: return 11;
        default: return 6;
    }
}

// Two-layer floor: blank base + scattered detail overlay
FloorSprite floor_sprite(TileType type, uint8_t variant) {
    int row = floor_row(type);
    FloorSprite fs;
    fs.base = {SHEET_TILES, 0, row}; // col 0 = blank colored floor

    // variant 0 = no detail, just the blank floor (most common)
    // variant 1-2 = detail overlay using no-bg sprites (cols 4-5)
    if (variant > 0) {
        fs.overlay = {SHEET_TILES, 3 + (variant % 3), row};
        fs.has_overlay = true;
    } else {
        fs.overlay = {-1, 0, 0};
        fs.has_overlay = false;
    }
    return fs;
}

SpriteRef tile_sprite(TileType type, [[maybe_unused]] uint8_t variant) {
    switch (type) {
        // Walls — show side view (col 1)
        case TileType::WALL_DIRT:         return {SHEET_TILES, 1,  0};
        case TileType::WALL_STONE_ROUGH:  return {SHEET_TILES, 1,  1};
        case TileType::WALL_STONE_BRICK:  return {SHEET_TILES, 1,  2};
        case TileType::WALL_IGNEOUS:      return {SHEET_TILES, 1,  3};
        case TileType::WALL_LARGE_STONE:  return {SHEET_TILES, 1,  4};
        case TileType::WALL_CATACOMB:     return {SHEET_TILES, 1,  5};

        // Features — group 17 = row 16
        case TileType::DOOR_CLOSED:       return {SHEET_TILES, 2, 16};
        case TileType::DOOR_OPEN:         return {SHEET_TILES, 3, 16};
        case TileType::STAIRS_DOWN:       return {SHEET_TILES, 7, 16};
        case TileType::STAIRS_UP:         return {SHEET_TILES, 8, 16};
        case TileType::WATER:             return {SHEET_TILES, 0, 12};
        case TileType::TREE:              return {SHEET_TILES, 2, 25};
        case TileType::BRUSH:             return {SHEET_TILES, 0, 25}; // sapling/small bush

        // Floors handled by floor_sprite(), but provide fallback
        default:
            if (is_floor_type(type)) {
                return {SHEET_TILES, 0, floor_row(type)};
            }
            return {SHEET_TILES, 0, 6}; // blank dark grey
    }
}

void draw_map(SDL_Renderer* renderer, const SpriteManager& sprites,
              const TileMap& map, const Camera& cam, int y_offset) {
    int TS = cam.tile_size;
    // Scale factor for draw_sprite (how many times to multiply the 32px base)
    // Since draw_sprite takes an integer scale, we use a custom dest rect instead
    // when tile_size != 32

    int start_x = std::max(0, cam.x);
    int start_y = std::max(0, cam.y);
    int end_x = std::min(map.width(), cam.x + cam.tiles_wide() + 2);
    int end_y = std::min(map.height(), cam.y + cam.tiles_high() + 2);

    for (int y = start_y; y < end_y; y++) {
        for (int x = start_x; x < end_x; x++) {
            auto& tile = map.at(x, y);

            int screen_x = (x - cam.x) * TS;
            int screen_y = (y - cam.y) * TS + y_offset;

            auto draw_sprite_scaled = [&](int sheet, int col, int row, SDL_Color tint) {
                sprites.draw_sprite_sized(renderer, sheet, col, row,
                                          screen_x, screen_y, TS, tint);
            };

            auto draw_tile = [&](SDL_Color tint) {
                if (is_floor_type(tile.type)) {
                    auto fs = floor_sprite(tile.type, tile.variant);
                    draw_sprite_scaled(fs.base.sheet, fs.base.col, fs.base.row, tint);
                    if (fs.has_overlay) {
                        draw_sprite_scaled(fs.overlay.sheet, fs.overlay.col,
                                           fs.overlay.row, tint);
                    }
                } else if (tile.type == TileType::TREE || tile.type == TileType::BRUSH) {
                    draw_sprite_scaled(SHEET_TILES, 0, 7, tint);
                    auto ref = tile_sprite(tile.type, tile.variant);
                    draw_sprite_scaled(ref.sheet, ref.col, ref.row, tint);
                } else {
                    auto ref = tile_sprite(tile.type, tile.variant);
                    draw_sprite_scaled(ref.sheet, ref.col, ref.row, tint);
                }
            };

            if (tile.visible) {
                draw_tile({255, 255, 255, 255});
            } else if (tile.explored) {
                draw_tile({100, 100, 120, 255});
            }
        }
    }
}

void draw_entities(SDL_Renderer* renderer, const SpriteManager& sprites,
                   World& world, const TileMap& map, const Camera& cam,
                   int y_offset) {
    int TS = cam.tile_size;

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

    std::sort(cmds.begin(), cmds.end(),
              [](const DrawCmd& a, const DrawCmd& b) { return a.z_order < b.z_order; });

    for (auto& cmd : cmds) {
        sprites.draw_sprite_sized(renderer, cmd.sheet, cmd.sx, cmd.sy,
                                   cmd.dx, cmd.dy, TS, cmd.tint);
    }
}

} // namespace render
