#include "ui/world_map.h"
#include "ui/ui_draw.h"
#include <cstdio>
#include <cmath>

bool WorldMap::handle_input(SDL_Event& event) {
    if (!open_) return false;

    if (event.type == SDL_KEYDOWN) {
        open_ = false;
        return true;
    }
    return false;
}

void WorldMap::render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                       const TileMap& map, int player_x, int player_y,
                       int screen_w, int screen_h) const {
    if (!open_ || !font) return;

    // Map is 2000x1500. Divide into chunks of 100x100 tiles.
    // That gives us a 20x15 grid of chunks.
    constexpr int CHUNK = 100;
    constexpr int GRID_W = 20;
    constexpr int GRID_H = 15;

    // Each chunk draws as a small rect. Scale to fit nicely on screen.
    int pixel_size = 24; // pixels per chunk
    int map_pixel_w = GRID_W * pixel_size;
    int map_pixel_h = GRID_H * pixel_size;

    // Panel dimensions (add padding for border and labels)
    int panel_w = map_pixel_w + 40;
    int panel_h = map_pixel_h + 80;
    int px = (screen_w - panel_w) / 2;
    int py = (screen_h - panel_h) / 2;

    // Darken background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, screen_w, screen_h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer, &overlay);

    // Panel
    ui::draw_panel(renderer, px, py, panel_w, panel_h);

    // Title
    int line_h = TTF_FontLineSkip(font);
    SDL_Color title_col = {200, 180, 140, 255};
    ui::draw_text_centered(renderer, font_title ? font_title : font,
                            "World Map", title_col, screen_w / 2, py + 8);

    int map_x0 = px + 20;
    int map_y0 = py + 36;

    // Color mapping for tile types
    auto tile_color = [](TileType type) -> SDL_Color {
        switch (type) {
            case TileType::FLOOR_GRASS:
            case TileType::BRUSH:
                return {60, 120, 50, 255};
            case TileType::TREE:
                return {30, 80, 25, 255};
            case TileType::FLOOR_SAND:
                return {200, 180, 100, 255};
            case TileType::FLOOR_ICE:
                return {180, 210, 240, 255};
            case TileType::WATER:
                return {40, 80, 180, 255};
            case TileType::FLOOR_DIRT:
                return {120, 90, 60, 255};
            case TileType::FLOOR_STONE:
            case TileType::WALL_STONE_BRICK:
            case TileType::WALL_STONE_ROUGH:
            case TileType::ROCK:
                return {100, 95, 90, 255};
            case TileType::STAIRS_DOWN:
            case TileType::DOOR_CLOSED:
            case TileType::DOOR_OPEN:
                return {100, 95, 90, 255};
            default:
                return {30, 30, 30, 255};
        }
    };

    // Sample each chunk: pick 5 points and take the most common tile type
    for (int cy = 0; cy < GRID_H; cy++) {
        for (int cx = 0; cx < GRID_W; cx++) {
            // Sample points within the chunk
            int base_tx = cx * CHUNK;
            int base_ty = cy * CHUNK;

            // Sample center and a few offsets
            int sample_offsets[][2] = {{50,50}, {25,25}, {75,75}, {25,75}, {75,25}};
            int counts[static_cast<int>(TileType::COUNT)] = {};

            for (auto& off : sample_offsets) {
                int tx = base_tx + off[0];
                int ty = base_ty + off[1];
                if (map.in_bounds(tx, ty)) {
                    auto tt = map.at(tx, ty).type;
                    counts[static_cast<int>(tt)]++;
                }
            }

            // Find dominant type
            TileType dominant = TileType::VOID;
            int best = 0;
            for (int t = 0; t < static_cast<int>(TileType::COUNT); t++) {
                if (counts[t] > best) {
                    best = counts[t];
                    dominant = static_cast<TileType>(t);
                }
            }

            SDL_Color col = tile_color(dominant);
            SDL_Rect r = {map_x0 + cx * pixel_size, map_y0 + cy * pixel_size,
                          pixel_size, pixel_size};
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
            SDL_RenderFillRect(renderer, &r);
        }
    }

    // Grid lines (subtle)
    SDL_SetRenderDrawColor(renderer, 40, 35, 30, 80);
    for (int cx = 0; cx <= GRID_W; cx++) {
        SDL_RenderDrawLine(renderer, map_x0 + cx * pixel_size, map_y0,
                           map_x0 + cx * pixel_size, map_y0 + map_pixel_h);
    }
    for (int cy = 0; cy <= GRID_H; cy++) {
        SDL_RenderDrawLine(renderer, map_x0, map_y0 + cy * pixel_size,
                           map_x0 + map_pixel_w, map_y0 + cy * pixel_size);
    }

    // Region labels
    SDL_Color region_col = {200, 190, 170, 120};
    // The Frozen North: rows 0-3 (y < 400 in tile coords = chunks 0-3)
    ui::draw_text_centered(renderer, font, "The Frozen North", region_col,
                            map_x0 + map_pixel_w / 2, map_y0 + 1 * pixel_size);
    // Temperate Heartlands: rows 4-10 (y 400-1100 = chunks 4-10)
    ui::draw_text_centered(renderer, font, "Temperate Heartlands", region_col,
                            map_x0 + map_pixel_w / 2, map_y0 + 7 * pixel_size);
    // The Southern Wastes: rows 11-14 (y 1100+ = chunks 11-14)
    ui::draw_text_centered(renderer, font, "The Southern Wastes", region_col,
                            map_x0 + map_pixel_w / 2, map_y0 + 13 * pixel_size);

    // Town data (subset of major towns)
    // Coordinates from generate_overworld.py: CX=1000, CY=750
    struct TownMarker {
        int tx, ty; // tile coords
        const char* name;
    };
    static const TownMarker TOWNS[] = {
        {1000, 750,  "Thornwall"},
        { 750, 650,  "Ashford"},
        {1300, 670,  "Greywatch"},
        { 850, 950,  "Millhaven"},
        {1200, 930,  "Stonehollow"},
        {1050, 450,  "Frostmere"},
        { 650, 800,  "Bramblewood"},
        {1400, 750,  "Ironhearth"},
    };
    constexpr int TOWN_COUNT = sizeof(TOWNS) / sizeof(TOWNS[0]);

    SDL_Color town_dot = {255, 255, 255, 255};
    SDL_Color town_label = {220, 210, 200, 255};

    for (int i = 0; i < TOWN_COUNT; i++) {
        int sx = map_x0 + (TOWNS[i].tx * pixel_size) / CHUNK;
        int sy = map_y0 + (TOWNS[i].ty * pixel_size) / CHUNK;

        // White dot
        SDL_Rect dot = {sx - 2, sy - 2, 5, 5};
        SDL_SetRenderDrawColor(renderer, town_dot.r, town_dot.g, town_dot.b, town_dot.a);
        SDL_RenderFillRect(renderer, &dot);

        // Label (offset slightly)
        ui::draw_text(renderer, font, TOWNS[i].name, town_label, sx + 4, sy - line_h / 2);
    }

    // Dungeon markers (subset)
    struct DungeonMarker {
        int tx, ty;
    };
    static const DungeonMarker DUNGEONS[] = {
        {1060, 750},   // Barrow near Thornwall
        {1120, 550},   // North of center
        { 800, 900},   // Southwest
        {1300, 850},   // East
        { 900, 500},   // North
        {1200, 1050},  // South-east
    };
    constexpr int DUNGEON_COUNT = sizeof(DUNGEONS) / sizeof(DUNGEONS[0]);

    SDL_Color dungeon_dot = {200, 60, 60, 255};
    for (int i = 0; i < DUNGEON_COUNT; i++) {
        int sx = map_x0 + (DUNGEONS[i].tx * pixel_size) / CHUNK;
        int sy = map_y0 + (DUNGEONS[i].ty * pixel_size) / CHUNK;

        // Red dot
        SDL_Rect dot = {sx - 2, sy - 2, 5, 5};
        SDL_SetRenderDrawColor(renderer, dungeon_dot.r, dungeon_dot.g, dungeon_dot.b, dungeon_dot.a);
        SDL_RenderFillRect(renderer, &dot);
    }

    // Player position — bright yellow blinking dot
    blink_timer_ = SDL_GetTicks();
    bool blink_on = (blink_timer_ / 400) % 2 == 0;
    if (blink_on) {
        int ppx = map_x0 + (player_x * pixel_size) / CHUNK;
        int ppy = map_y0 + (player_y * pixel_size) / CHUNK;

        SDL_SetRenderDrawColor(renderer, 255, 240, 60, 255);
        SDL_Rect pdot = {ppx - 3, ppy - 3, 7, 7};
        SDL_RenderFillRect(renderer, &pdot);
        // Outline for visibility
        SDL_SetRenderDrawColor(renderer, 200, 180, 40, 255);
        SDL_RenderDrawRect(renderer, &pdot);
    }

    // Hint at bottom
    SDL_Color hint_col = {120, 110, 100, 255};
    ui::draw_text_centered(renderer, font, "[any key] close",
                            hint_col, screen_w / 2, py + panel_h - line_h - 10);
}
