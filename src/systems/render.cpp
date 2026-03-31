#include "systems/render.h"
#include "components/position.h"
#include "components/renderable.h"
#include <algorithm>
#include <vector>
#include <cmath>

namespace render {

// Tile sprite mappings into tiles.png
// Layout: tiles.txt group N = spritesheet row (N-1), letter = column (a=0, b=1, ...)
// Walls: row 0-5, col 0 = top view, col 1 = side view
// Floors: row 6+ , col 0 = blank bg, col 1-3 = variants with bg, col 4-6 = no bg
// Features: row 16 = doors/stairs/traps
static bool is_floor_type(TileType type) {
    return type == TileType::FLOOR_STONE || type == TileType::FLOOR_DIRT ||
           type == TileType::FLOOR_GRASS || type == TileType::FLOOR_BONE ||
           type == TileType::FLOOR_RED_STONE || type == TileType::FLOOR_SAND ||
           type == TileType::FLOOR_ICE;
}

// Floor tile row in spritesheet (tiles.txt group - 1)
static int floor_row(TileType type) {
    switch (type) {
        case TileType::FLOOR_STONE:     return 6;
        case TileType::FLOOR_GRASS:     return 7;
        case TileType::FLOOR_DIRT:      return 8;
        case TileType::FLOOR_BONE:      return 10;
        case TileType::FLOOR_RED_STONE: return 11;
        case TileType::FLOOR_SAND:      return 8;  // reuse dirt sprites with tint
        case TileType::FLOOR_ICE:       return 12;  // reuse blue stone floor
        default: return 6;
    }
}

// Sand and ice get color tints applied during rendering
static SDL_Color floor_tint(TileType type) {
    switch (type) {
        case TileType::FLOOR_SAND: return {220, 200, 140, 255}; // warm sandy tint
        case TileType::FLOOR_ICE:  return {160, 200, 240, 255}; // cold blue tint
        default: return {255, 255, 255, 255};
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
        case TileType::BRUSH:
            if (variant == 1) return {SHEET_TILES, 1, 25};
            if (variant == 2) return {SHEET_TILES, 3, 25};
            return {SHEET_TILES, 0, 25};
        case TileType::SHRINE:            return {SHEET_TILES, 5, 16}; // altar sprite

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

            // Blend two tints (visibility * floor color)
            auto blend_tint = [](SDL_Color a, SDL_Color b) -> SDL_Color {
                return {static_cast<Uint8>(a.r * b.r / 255),
                        static_cast<Uint8>(a.g * b.g / 255),
                        static_cast<Uint8>(a.b * b.b / 255), 255};
            };

            auto draw_tile = [&](SDL_Color tint) {
                if (is_floor_type(tile.type)) {
                    SDL_Color ft = blend_tint(tint, floor_tint(tile.type));
                    auto fs = floor_sprite(tile.type, tile.variant);
                    draw_sprite_scaled(fs.base.sheet, fs.base.col, fs.base.row, ft);
                    if (fs.has_overlay) {
                        draw_sprite_scaled(fs.overlay.sheet, fs.overlay.col,
                                           fs.overlay.row, ft);
                    }
                } else if (tile.type == TileType::TREE || tile.type == TileType::BRUSH) {
                    draw_sprite_scaled(SHEET_TILES, 0, 7, tint);
                    auto ref = tile_sprite(tile.type, tile.variant);
                    draw_sprite_scaled(ref.sheet, ref.col, ref.row, tint);
                } else if (tile.type == TileType::ROCK) {
                    // Rock on dirt base
                    draw_sprite_scaled(SHEET_TILES, 0, 8, tint);
                    draw_sprite_scaled(SHEET_TILES, 0, 18, tint); // large rock sprite
                } else if ((tile.type >= TileType::WALL_DIRT && tile.type <= TileType::WALL_CATACOMB)
                           || tile.type == TileType::WALL_WOOD) {
                    // Walls: side view if tile below is not a wall (player sees the face)
                    // top view if tile below is also a wall (looking down at it)
                    int top_col = 0, side_col = 1, wall_row = 0;
                    switch (tile.type) {
                        case TileType::WALL_DIRT:        wall_row = 0; break;
                        case TileType::WALL_STONE_ROUGH: wall_row = 1; break;
                        case TileType::WALL_STONE_BRICK: wall_row = 2; break;
                        case TileType::WALL_IGNEOUS:     wall_row = 3; break;
                        case TileType::WALL_LARGE_STONE: wall_row = 4; break;
                        case TileType::WALL_CATACOMB:    wall_row = 5; break;
                        case TileType::WALL_WOOD:        wall_row = 1; top_col = 2; side_col = 3; break;
                        default: wall_row = 2; break;
                    }
                    auto is_any_wall = [](TileType t) {
                        return (t >= TileType::WALL_DIRT && t <= TileType::WALL_CATACOMB)
                               || t == TileType::WALL_WOOD;
                    };
                    bool show_side = true;
                    if (y + 1 < map.height()) {
                        if (is_any_wall(map.at(x, y + 1).type))
                            show_side = false;
                    }
                    draw_sprite_scaled(SHEET_TILES, show_side ? side_col : top_col, wall_row, tint);
                } else if (tile.type == TileType::SHRINE) {
                    // Shrine: draw stone floor base + altar sprite on top
                    draw_sprite_scaled(SHEET_TILES, 0, 6, tint); // stone floor
                    auto ref = tile_sprite(tile.type, tile.variant);
                    draw_sprite_scaled(ref.sheet, ref.col, ref.row, tint);
                } else {
                    auto ref = tile_sprite(tile.type, tile.variant);
                    draw_sprite_scaled(ref.sheet, ref.col, ref.row, tint);
                }
            };

            // Region-tinted background fill (desert = brown, ice = light grey)
            {
                SDL_Color bg = {18, 20, 28, 255}; // default dark slate
                if (tile.type == TileType::FLOOR_SAND || (tile.explored && y > map.height() * 5 / 6))
                    bg = {35, 28, 18, 255}; // warm brown for desert
                else if (tile.type == TileType::FLOOR_ICE || (tile.explored && y < map.height() / 6))
                    bg = {45, 48, 55, 255}; // light grey for ice
                if (tile.explored || tile.visible) {
                    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, 255);
                    SDL_Rect fill = {screen_x, screen_y, TS, TS};
                    SDL_RenderFillRect(renderer, &fill);
                }
            }

            if (tile.visible) {
                draw_tile({255, 255, 255, 255});
                // Animated water overlay
                if (tile.type == TileType::WATER) {
                    int wf = static_cast<int>((SDL_GetTicks() / 200 + x * 3 + y * 7) % 6);
                    sprites.draw_sprite_sized(renderer, SHEET_ANIMATED, wf, 10,
                                              screen_x, screen_y, TS, {255, 255, 255, 120});
                }
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
        bool flip_h;
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
                         screen_x, screen_y, rend.tint, rend.flip_h});
    }

    std::sort(cmds.begin(), cmds.end(),
              [](const DrawCmd& a, const DrawCmd& b) { return a.z_order < b.z_order; });

    int anim_frame = static_cast<int>((SDL_GetTicks() / 150) % 6);

    for (auto& cmd : cmds) {
        int sx = cmd.sx, sy = cmd.sy;
        // Animated sprites: cycle columns as frames
        if (cmd.sheet == SHEET_ANIMATED) {
            sx = anim_frame;
        }
        sprites.draw_sprite_sized(renderer, cmd.sheet, sx, sy,
                                   cmd.dx, cmd.dy, TS, cmd.tint, cmd.flip_h);

        // Warm glow around light sources (torches, braziers, fire pits)
        if (cmd.sheet == SHEET_ANIMATED && (sy == 1 || sy == 3 || sy == 5 || sy == 7)) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            int gcx = cmd.dx + TS / 2;
            int gcy = cmd.dy + TS / 2;
            int flicker = 15 + (anim_frame % 3) * 5;
            // Draw concentric diamond/circle layers for soft circular glow
            int max_r = TS * 2;
            int layers = 8;
            for (int li = layers; li >= 1; li--) {
                int r = max_r * li / layers;
                int alpha = flicker * (layers - li + 1) / (layers + 2);
                SDL_SetRenderDrawColor(renderer, 255, 180, 80, static_cast<Uint8>(alpha));
                // Approximate circle with a clipped rect per row
                for (int dy = -r; dy <= r; dy += std::max(1, r / 8)) {
                    float frac = 1.0f - static_cast<float>(dy * dy) / static_cast<float>(r * r);
                    if (frac <= 0) continue;
                    int half_w = static_cast<int>(r * std::sqrt(frac));
                    SDL_Rect row = {gcx - half_w, gcy + dy, half_w * 2, std::max(1, r / 8)};
                    SDL_RenderFillRect(renderer, &row);
                }
            }
        }
    }
}

} // namespace render
