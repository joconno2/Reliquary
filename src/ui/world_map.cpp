#include "ui/world_map.h"
#include "ui/ui_draw.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

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

    int line_h = TTF_FontLineSkip(font);
    int mw = map.width();
    int mh = map.height();
    if (mw <= 0 || mh <= 0) return;

    // Fit the map into the screen with padding
    int pad = 40;
    int avail_w = screen_w - pad * 2;
    int avail_h = screen_h - pad * 2 - 60; // room for title + hint

    // Pixels per tile — scale to fit, at least 1px
    float scale_x = static_cast<float>(avail_w) / mw;
    float scale_y = static_cast<float>(avail_h) / mh;
    float scale = std::min(scale_x, scale_y);
    if (scale < 1.0f) scale = 1.0f;

    int map_pw = static_cast<int>(mw * scale);
    int map_ph = static_cast<int>(mh * scale);

    int panel_w = map_pw + 20;
    int panel_h = map_ph + 70;
    int px = (screen_w - panel_w) / 2;
    int py = (screen_h - panel_h) / 2;
    int map_x0 = px + 10;
    int map_y0 = py + 40;

    // Darken background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, screen_w, screen_h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
    SDL_RenderFillRect(renderer, &overlay);

    // Panel
    ui::draw_panel(renderer, px, py, panel_w, panel_h);

    // Title
    SDL_Color title_col = {200, 180, 140, 255};
    ui::draw_text_centered(renderer, font_title ? font_title : font,
                            "World Map", title_col, screen_w / 2, py + 8);

    // Color mapping
    auto tile_color = [](TileType type) -> SDL_Color {
        switch (type) {
            case TileType::FLOOR_GRASS:
            case TileType::BRUSH:       return {55, 110, 45, 255};
            case TileType::TREE:        return {30, 75, 22, 255};
            case TileType::FLOOR_SAND:  return {190, 170, 95, 255};
            case TileType::FLOOR_ICE:   return {175, 205, 235, 255};
            case TileType::WATER:       return {35, 70, 170, 255};
            case TileType::FLOOR_DIRT:  return {110, 85, 55, 255};
            case TileType::FLOOR_STONE:
            case TileType::WALL_STONE_BRICK:
            case TileType::WALL_STONE_ROUGH:
            case TileType::WALL_WOOD:
            case TileType::ROCK:        return {95, 90, 85, 255};
            case TileType::FLOOR_BONE:  return {140, 130, 120, 255};
            case TileType::FLOOR_RED_STONE: return {130, 70, 60, 255};
            case TileType::DOOR_CLOSED:
            case TileType::DOOR_OPEN:   return {120, 100, 70, 255};
            case TileType::STAIRS_DOWN: return {180, 160, 60, 255};
            default:                    return {25, 25, 25, 255};
        }
    };

    // Render the map — every tile gets a pixel (or scaled rect)
    // For performance with large maps, sample every Nth tile
    int step = std::max(1, static_cast<int>(1.0f / scale));
    float ps = std::max(1.0f, scale); // pixel size per sample

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    for (int ty = 0; ty < mh; ty += step) {
        for (int tx = 0; tx < mw; tx += step) {
            auto& tile = map.at(tx, ty);
            // Only show explored tiles (fog of war)
            SDL_Color col;
            if (tile.explored) {
                col = tile_color(tile.type);
            } else {
                col = {18, 18, 22, 255};
            }
            int sx = map_x0 + static_cast<int>(tx * scale);
            int sy = map_y0 + static_cast<int>(ty * scale);
            int sw = static_cast<int>(ps);
            int sh = static_cast<int>(ps);
            if (sw < 1) sw = 1;
            if (sh < 1) sh = 1;
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, col.a);
            SDL_Rect r = {sx, sy, sw, sh};
            SDL_RenderFillRect(renderer, &r);
        }
    }

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Town markers
    struct TownMarker { int tx, ty; const char* name; };
    static const TownMarker TOWNS[] = {
        {1000, 750, "Thornwall"}, {750, 650, "Ashford"}, {1300, 670, "Greywatch"},
        {850, 950, "Millhaven"}, {1200, 930, "Stonehollow"}, {1050, 450, "Frostmere"},
        {650, 800, "Bramblewood"}, {1400, 750, "Ironhearth"}, {1000, 1100, "Dustfall"},
        {800, 400, "Whitepeak"}, {1250, 1100, "Drywell"}, {550, 550, "Hollowgate"},
        {1450, 500, "Candlemere"}, {900, 1200, "Sandmoor"}, {1100, 300, "Glacierveil"},
        {700, 1050, "Tanglewood"}, {1350, 1000, "Redrock"}, {1150, 550, "Ravenshold"},
        {600, 700, "Fenwatch"}, {1500, 850, "Endgate"},
    };

    SDL_Color town_label = {220, 210, 200, 255};
    for (auto& t : TOWNS) {
        int sx = map_x0 + static_cast<int>(t.tx * scale);
        int sy = map_y0 + static_cast<int>(t.ty * scale);

        // White dot
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        int ds = std::max(3, static_cast<int>(scale * 2));
        SDL_Rect dot = {sx - ds / 2, sy - ds / 2, ds, ds};
        SDL_RenderFillRect(renderer, &dot);

        // Label
        ui::draw_text(renderer, font, t.name, town_label, sx + ds, sy - line_h / 2);
    }

    // Dungeon markers (red dots)
    struct DungeonMarker { int tx, ty; };
    static const DungeonMarker DUNGEONS[] = {
        {1060, 750}, {800, 650}, {1200, 640}, {1100, 500}, {850, 900},
        {1450, 800}, {1500, 550}, {600, 550}, {1000, 150},
    };
    SDL_Color dungeon_dot = {200, 60, 60, 255};
    for (auto& d : DUNGEONS) {
        int sx = map_x0 + static_cast<int>(d.tx * scale);
        int sy = map_y0 + static_cast<int>(d.ty * scale);
        SDL_SetRenderDrawColor(renderer, dungeon_dot.r, dungeon_dot.g, dungeon_dot.b, 255);
        int ds = std::max(3, static_cast<int>(scale * 1.5f));
        SDL_Rect dot = {sx - ds / 2, sy - ds / 2, ds, ds};
        SDL_RenderFillRect(renderer, &dot);
    }

    // Player position — bright yellow blinking dot
    blink_timer_ = SDL_GetTicks();
    bool blink_on = (blink_timer_ / 400) % 2 == 0;
    if (blink_on) {
        int ppx = map_x0 + static_cast<int>(player_x * scale);
        int ppy = map_y0 + static_cast<int>(player_y * scale);
        int ds = std::max(5, static_cast<int>(scale * 3));
        SDL_SetRenderDrawColor(renderer, 255, 240, 60, 255);
        SDL_Rect pdot = {ppx - ds / 2, ppy - ds / 2, ds, ds};
        SDL_RenderFillRect(renderer, &pdot);
        SDL_SetRenderDrawColor(renderer, 200, 180, 40, 255);
        SDL_RenderDrawRect(renderer, &pdot);
    }

    // Region labels (faint)
    SDL_Color region_col = {180, 170, 150, 80};
    ui::draw_text_centered(renderer, font, "The Frozen North", region_col,
                            screen_w / 2, map_y0 + static_cast<int>(100 * scale));
    ui::draw_text_centered(renderer, font, "Temperate Heartlands", region_col,
                            screen_w / 2, map_y0 + static_cast<int>(375 * scale));
    ui::draw_text_centered(renderer, font, "The Southern Wastes", region_col,
                            screen_w / 2, map_y0 + static_cast<int>(650 * scale));

    // Hint
    SDL_Color hint_col = {120, 110, 100, 255};
    ui::draw_text_centered(renderer, font, "[any key] close",
                            hint_col, screen_w / 2, py + panel_h - line_h - 10);
}
