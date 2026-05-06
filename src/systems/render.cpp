#include "systems/render.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/death_anim.h"
#include "components/status_effect.h"
#include <algorithm>
#include <vector>
#include <cmath>

namespace render {

// ── Per-tile lighting ────────────────────────────────────────────────
void compute_lighting(TileMap& map, World& world, int ambient, const Camera& cam) {
    // Only compute for visible viewport + margin (performance)
    int margin = 4;
    int x0 = std::max(0, cam.x - margin);
    int y0 = std::max(0, cam.y - margin);
    int x1 = std::min(map.width(), cam.x + cam.tiles_wide() + margin);
    int y1 = std::min(map.height(), cam.y + cam.tiles_high() + margin);

    // Set ambient on all viewport tiles
    uint8_t amb = static_cast<uint8_t>(std::clamp(ambient, 40, 255));
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            map.at(x, y).brightness = amb;
        }
    }

    // Wall-adjacent ambient occlusion: tiles next to walls get slightly darker
    for (int y = y0; y < y1; y++) {
        for (int x = x0; x < x1; x++) {
            auto& tile = map.at(x, y);
            if (!tile.visible) continue;
            if (map.is_opaque(x, y)) continue; // walls don't get AO
            int walls = 0;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++)
                    if ((dx || dy) && map.in_bounds(x+dx, y+dy) && map.is_opaque(x+dx, y+dy))
                        walls++;
            if (walls > 0) {
                int reduction = std::min(30, walls * 6); // up to -30 brightness near walls
                tile.brightness = static_cast<uint8_t>(
                    std::max(40, static_cast<int>(tile.brightness) - reduction));
            }
        }
    }

    // Light sources: find all animated entities that are torches/braziers
    // Rows 1,3,5,7 on SHEET_ANIMATED are light sources
    auto& pos_pool = world.pool<Position>();
    for (size_t i = 0; i < pos_pool.size(); i++) {
        Entity e = pos_pool.entity_at(i);
        if (!world.has<Renderable>(e)) continue;
        auto& rend = world.get<Renderable>(e);
        if (rend.sprite_sheet != SHEET_ANIMATED) continue;
        int row = rend.sprite_y;
        if (row != 1 && row != 3 && row != 5 && row != 7 && row != 8) continue;

        auto& lpos = pos_pool.at_index(i);
        // Skip if far from viewport
        if (lpos.x < x0 - 6 || lpos.x > x1 + 6 || lpos.y < y0 - 6 || lpos.y > y1 + 6) continue;

        // Light radius: 5 tiles, brightness falls off with distance squared
        int light_r = 5;
        int light_strength = 120; // max brightness contribution at source
        for (int dy = -light_r; dy <= light_r; dy++) {
            for (int dx = -light_r; dx <= light_r; dx++) {
                int tx = lpos.x + dx, ty = lpos.y + dy;
                if (!map.in_bounds(tx, ty)) continue;
                if (tx < x0 || tx >= x1 || ty < y0 || ty >= y1) continue;
                int dist_sq = dx * dx + dy * dy;
                if (dist_sq > light_r * light_r) continue;
                // Inverse distance falloff (not squared, feels more natural)
                float falloff = 1.0f - static_cast<float>(dist_sq) / static_cast<float>(light_r * light_r);
                int add = static_cast<int>(light_strength * falloff);
                auto& t = map.at(tx, ty);
                t.brightness = static_cast<uint8_t>(std::min(255, static_cast<int>(t.brightness) + add));
            }
        }
    }

    // Player emits a small amount of light (carrying a torch conceptually)
    {
        int pr = 3; // player light radius
        int ps = 60; // player light strength
        for (int dy = -pr; dy <= pr; dy++) {
            for (int dx = -pr; dx <= pr; dx++) {
                int tx = cam.px + dx, ty = cam.py + dy;
                if (!map.in_bounds(tx, ty)) continue;
                if (tx < x0 || tx >= x1 || ty < y0 || ty >= y1) continue;
                int dist_sq = dx * dx + dy * dy;
                if (dist_sq > pr * pr) continue;
                float falloff = 1.0f - static_cast<float>(dist_sq) / static_cast<float>(pr * pr);
                int add = static_cast<int>(ps * falloff);
                auto& t = map.at(tx, ty);
                t.brightness = static_cast<uint8_t>(std::min(255, static_cast<int>(t.brightness) + add));
            }
        }
    }
}

// Tile sprite mappings into tiles.png
// Layout: tiles.txt group N = spritesheet row (N-1), letter = column (a=0, b=1, ...)
// Walls: row 0-5, col 0 = top view, col 1 = side view
// Floors: row 6+ , col 0 = blank bg, col 1-3 = variants with bg, col 4-6 = no bg
// Features: row 16 = doors/stairs/traps
static bool is_floor_type(TileType type) {
    return type == TileType::FLOOR_STONE || type == TileType::FLOOR_DIRT ||
           type == TileType::FLOOR_GRASS || type == TileType::FLOOR_BONE ||
           type == TileType::FLOOR_RED_STONE || type == TileType::FLOOR_SAND ||
           type == TileType::FLOOR_ICE || type == TileType::FLOOR_SNOW ||
           type == TileType::FLOOR_COBBLE || type == TileType::LAVA ||
           type == TileType::DEEP_WATER;
}

// Floor tile row in spritesheet (tiles.txt group - 1)
static int floor_row(TileType type) {
    switch (type) {
        case TileType::FLOOR_STONE:     return 6;
        case TileType::FLOOR_GRASS:     return 7;
        case TileType::FLOOR_DIRT:      return 8;
        case TileType::FLOOR_BONE:      return 10;
        case TileType::FLOOR_RED_STONE: return 11;
        case TileType::FLOOR_SAND:      return 7;  // row 8 in tiles.txt = row 7, cols 7-13
        case TileType::FLOOR_ICE:       return 12;  // blue stone floor (dungeons)
        case TileType::FLOOR_SNOW:      return 6;   // row 7 in tiles.txt = row 6, cols 7-13
        case TileType::FLOOR_COBBLE:    return 8;   // row 9 in tiles.txt = row 8, cols 7-13
        default: return 6;
    }
}

// Column offset for floor types that use the second half of a row
static int floor_col_offset(TileType type) {
    switch (type) {
        case TileType::FLOOR_SNOW: return 7;  // cols 7-13 on stone floor row
        case TileType::FLOOR_SAND: return 7;    // cols 7-13 on grass floor row
        case TileType::FLOOR_COBBLE: return 7;  // cols 7-13 on dirt floor row
        default: return 0;
    }
}

static SDL_Color floor_tint(TileType type) {
    switch (type) {
        case TileType::FLOOR_ICE:  return {160, 200, 240, 255}; // cold blue tint (dungeon ice)
        default: return {255, 255, 255, 255};
    }
}

// Two-layer floor: blank base + scattered detail overlay
FloorSprite floor_sprite(TileType type, uint8_t variant) {
    int row = floor_row(type);
    int off = floor_col_offset(type);
    FloorSprite fs;
    fs.base = {SHEET_TILES, off + 0, row}; // col 0 (or 7) = blank colored floor

    // variant 0 = no detail, just the blank floor (most common)
    // variant 1-2 = detail overlay using no-bg sprites (cols 4-5, or 11-12)
    if (variant > 0) {
        fs.overlay = {SHEET_TILES, off + 3 + (variant % 3), row};
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
        case TileType::LAVA:              return {SHEET_TILES, 0, 11}; // red stone floor
        case TileType::DEEP_WATER:        return {SHEET_TILES, 0, 12}; // blue stone floor
        case TileType::TREE:              return {SHEET_TILES, 2, 25};
        case TileType::BRUSH:
            if (variant == 1) return {SHEET_TILES, 1, 25}; // small tree
            if (variant == 2) return {SHEET_TILES, 3, 25}; // flowers
            // Variants 3-6: crops from row 20 (buckwheat, flax, papyrus, kenaf)
            return {SHEET_TILES, static_cast<int>((variant - 3) % 4), 19};
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
                           || tile.type == TileType::WALL_WOOD
                           || tile.type == TileType::WALL_GRASS
                           || tile.type == TileType::WALL_SANDSTONE
                           || tile.type == TileType::WALL_ICE) {
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
                        case TileType::WALL_GRASS:       wall_row = 3; top_col = 2; side_col = 3; break;
                        case TileType::WALL_SANDSTONE:   wall_row = 4; top_col = 2; side_col = 3; break;
                        case TileType::WALL_ICE:         wall_row = 0; top_col = 3; side_col = 4; break;
                        default: wall_row = 2; break;
                    }
                    auto is_any_wall = [](TileType t) {
                        return (t >= TileType::WALL_DIRT && t <= TileType::WALL_CATACOMB)
                               || t == TileType::WALL_WOOD
                               || t == TileType::WALL_GRASS
                               || t == TileType::WALL_SANDSTONE
                               || t == TileType::WALL_ICE;
                    };
                    bool show_side = true;
                    if (y + 1 < map.height()) {
                        if (is_any_wall(map.at(x, y + 1).type))
                            show_side = false;
                    }
                    // Province-based subtle wall tinting (overworld towns)
                    SDL_Color wall_tint = tint;
                    if (map.width() > 500) { // only on overworld
                        int dx = x - map.width() / 2, dy = y - map.height() / 2;
                        // Province god colors
                        SDL_Color prov_colors[] = {
                            {255,230,140,255}, // Pale Reach (Soleth gold)
                            {180,160,120,255}, // Frozen Marches (Gathruun brown)
                            {220,200,170,255}, // Heartlands (Morreth warm)
                            {140,200,140,255}, // Greenwood (Khael green)
                            {230,200,120,255}, // Iron Coast (Ossren gold)
                            {160,200,140,255}, // Dust Provinces (Sythara pale green)
                        };
                        int prov = 2; // default heartlands
                        if (dy < -350) prov = 1;
                        else if (dy < -100) prov = 0;
                        else if (dy > 250) prov = 5;
                        else if (dx < -200) prov = 3;
                        else if (dx > 200) prov = 4;
                        auto& pc = prov_colors[prov];
                        // Subtle blend: 80% original, 20% province color
                        wall_tint.r = static_cast<Uint8>((wall_tint.r * 4 + pc.r) / 5);
                        wall_tint.g = static_cast<Uint8>((wall_tint.g * 4 + pc.g) / 5);
                        wall_tint.b = static_cast<Uint8>((wall_tint.b * 4 + pc.b) / 5);
                    }
                    draw_sprite_scaled(SHEET_TILES, show_side ? side_col : top_col, wall_row, wall_tint);
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
                // Use per-tile brightness from lighting system
                Uint8 b = tile.brightness;
                // FOV edge fade: outer 40% of radius dims further
                int fdx = x - cam.px;
                int fdy = y - cam.py;
                int dist_sq = fdx * fdx + fdy * fdy;
                int fov_r_sq = cam.fov_r * cam.fov_r;
                if (fov_r_sq > 0 && dist_sq > fov_r_sq * 60 / 100) {
                    float t = static_cast<float>(dist_sq - fov_r_sq * 60 / 100) /
                              static_cast<float>(fov_r_sq * 40 / 100);
                    if (t > 1.0f) t = 1.0f;
                    b = static_cast<Uint8>(std::max(40, static_cast<int>(b) - static_cast<int>(t * 60)));
                }
                draw_tile({b, b, b, 255});
                // Animated water overlay (overworld + dungeon deep water)
                if (tile.type == TileType::WATER || tile.type == TileType::DEEP_WATER) {
                    int wf = static_cast<int>((SDL_GetTicks() / 200 + x * 3 + y * 7) % 6);
                    Uint8 wa = static_cast<Uint8>(std::min(120, static_cast<int>(b) * 120 / 255));
                    sprites.draw_sprite_sized(renderer, SHEET_ANIMATED, wf, 10,
                                              screen_x, screen_y, TS, {b, b, b, wa});
                }
                // Magma: poison bubbles overlay (row 11) on red stone base, swap to red version later
                if (tile.type == TileType::LAVA) {
                    int lf = static_cast<int>((SDL_GetTicks() / 200 + x * 5 + y * 3) % 6);
                    Uint8 la = static_cast<Uint8>(std::min(140, static_cast<int>(b) * 140 / 255));
                    sprites.draw_sprite_sized(renderer, SHEET_ANIMATED, lf, 11,
                                              screen_x, screen_y, TS, {255, 120, 40, la});
                }
            } else if (tile.explored) {
                draw_tile({65, 63, 72, 255}); // dark, barely visible memory
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

        SDL_Color tint = rend.tint;
        if (world.has<DeathAnim>(e)) {
            auto& da = world.get<DeathAnim>(e);
            float t = da.timer / da.duration; // 0..1 progress
            if (t < 0.25f) {
                // Flash white for first 25%
                tint = {255, 255, 255, 255};
            } else {
                // Fade alpha from 255 to 0 over remaining 75%
                float fade = 1.0f - (t - 0.25f) / 0.75f;
                if (fade < 0.0f) fade = 0.0f;
                tint.a = static_cast<Uint8>(fade * 255.0f);
            }
        }

        // Status effect tinting on entities with active effects
        if (world.has<StatusEffects>(e) && !world.has<DeathAnim>(e)) {
            auto& fx = world.get<StatusEffects>(e);
            if (!fx.effects.empty()) {
                // Blend toward the dominant status color
                auto st = fx.effects[0].type;
                uint8_t sr = tint.r, sg = tint.g, sb = tint.b;
                switch (st) {
                    case StatusType::POISON:  sr = 80; sg = 220; sb = 80; break;
                    case StatusType::BURN:    sr = 255; sg = 140; sb = 40; break;
                    case StatusType::BLEED:   sr = 220; sg = 60; sb = 60; break;
                    case StatusType::FROZEN:  sr = 140; sg = 200; sb = 255; break;
                    case StatusType::STUNNED: sr = 255; sg = 255; sb = 100; break;
                    case StatusType::CONFUSED:sr = 200; sg = 100; sb = 255; break;
                    case StatusType::BLIND:   sr = 80; sg = 80; sb = 80; break;
                    case StatusType::FEARED:  sr = 255; sg = 255; sb = 255; break;
                }
                // 40% blend toward status color (visible but not overwhelming)
                tint.r = static_cast<uint8_t>(tint.r * 60 / 100 + sr * 40 / 100);
                tint.g = static_cast<uint8_t>(tint.g * 60 / 100 + sg * 40 / 100);
                tint.b = static_cast<uint8_t>(tint.b * 60 / 100 + sb * 40 / 100);
            }
        }

        cmds.push_back({rend.z_order, rend.sprite_sheet, rend.sprite_x, rend.sprite_y,
                         screen_x, screen_y, tint, rend.flip_h});
    }

    std::sort(cmds.begin(), cmds.end(),
              [](const DrawCmd& a, const DrawCmd& b) { return a.z_order < b.z_order; });

    Uint32 ticks = SDL_GetTicks();

    for (auto& cmd : cmds) {
        int sx = cmd.sx, sy = cmd.sy;
        // Animated sprites: cycle columns as frames (offset by position for desync)
        if (cmd.sheet == SHEET_ANIMATED && (sy == 1 || sy == 5 || sy == 8)) {
            int offset = (cmd.dx * 7 + cmd.dy * 13) & 0xFF;
            sx = static_cast<int>(((ticks + offset * 40) / 150) % 6);
        }

        // Legendary/Relic glow: only z_order 2-3 (items), not 5+ (creatures)
        if (cmd.z_order >= 2 && cmd.z_order <= 3) {
            float pulse = 0.5f + 0.5f * sinf(ticks * 0.004f + cmd.dx * 0.1f);
            int glow_alpha = static_cast<int>(40 + 50 * pulse);
            int glow_r = static_cast<int>(TS * (0.6f + 0.3f * pulse));
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            // Draw concentric glow circles
            for (int ring = glow_r; ring > glow_r / 3; ring -= 2) {
                int alpha = glow_alpha * ring / glow_r;
                SDL_SetRenderDrawColor(renderer, cmd.tint.r, cmd.tint.g, cmd.tint.b,
                                       static_cast<Uint8>(alpha));
                int cx = cmd.dx + TS / 2, cy = cmd.dy + TS / 2;
                // Approximate circle with 4 rects
                SDL_Rect gr = {cx - ring / 2, cy - ring / 4, ring, ring / 2};
                SDL_RenderFillRect(renderer, &gr);
            }
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
        }

        sprites.draw_sprite_sized(renderer, cmd.sheet, sx, sy,
                                   cmd.dx, cmd.dy, TS, cmd.tint, cmd.flip_h);
    }
}

} // namespace render
