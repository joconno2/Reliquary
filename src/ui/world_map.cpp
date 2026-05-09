#include "ui/world_map.h"
#include "ui/ui_draw.h"
#include "core/input_glyphs.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

bool WorldMap::handle_input(SDL_Event& event) {
    if (!open_) return false;

    // Mouse wheel: zoom
    if (event.type == SDL_MOUSEWHEEL) {
        float step = event.wheel.y > 0 ? 0.25f : -0.25f;
        zoom_ = std::max(1.0f, std::min(4.0f, zoom_ + step));
        return true;
    }

    if (event.type == SDL_KEYDOWN) {
        auto sym = event.key.keysym.sym;
        // Pan with arrows/WASD
        float pan_speed = 30.0f / zoom_;
        switch (sym) {
            case SDLK_UP: case SDLK_w: case SDLK_k: cam_y_ -= pan_speed; return true;
            case SDLK_DOWN: case SDLK_s: case SDLK_j: cam_y_ += pan_speed; return true;
            case SDLK_LEFT: case SDLK_a: case SDLK_h: cam_x_ -= pan_speed; return true;
            case SDLK_RIGHT: case SDLK_d: case SDLK_l: cam_x_ += pan_speed; return true;
            // Zoom with +/-
            case SDLK_EQUALS: case SDLK_PLUS:
                zoom_ = std::min(4.0f, zoom_ + 0.25f); return true;
            case SDLK_MINUS:
                zoom_ = std::max(1.0f, zoom_ - 0.25f); return true;
            // Reset zoom
            case SDLK_HOME: case SDLK_0:
                zoom_ = 1.0f; return true;
            // Close on Escape, M, Q
            case SDLK_ESCAPE: case SDLK_m: case SDLK_q:
                open_ = false; return true;
            default:
                break;
        }
    }
    return true; // consume all events while map is open
}

void WorldMap::render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                       const TileMap& map, int player_x, int player_y,
                       int screen_w, int screen_h) const {
    if (!open_ || !font) return;

    int line_h = TTF_FontLineSkip(font);
    int mw = map.width();
    int mh = map.height();
    if (mw <= 0 || mh <= 0) return;

    // Legend height
    int legend_h = line_h * 2 + 8;

    int pad = 40;
    int avail_w = screen_w - pad * 2;
    int avail_h = screen_h - pad * 2 - 60 - legend_h;

    // Base scale (fit whole map to available area)
    float base_scale_x = static_cast<float>(avail_w) / mw;
    float base_scale_y = static_cast<float>(avail_h) / mh;
    float base_scale = std::min(base_scale_x, base_scale_y);
    if (base_scale < 0.5f) base_scale = 0.5f;

    // Apply user zoom
    float scale = base_scale * zoom_;

    // Viewport dimensions (pixels)
    int view_w = avail_w;
    int view_h = avail_h;

    // Panel covers full available area
    int panel_w = view_w + 20;
    int panel_h = view_h + 70 + legend_h;
    int px = (screen_w - panel_w) / 2;
    int py = (screen_h - panel_h) / 2;
    int map_x0 = px + 10;
    int map_y0 = py + 40;

    // Clamp camera to keep map edges in view
    float half_view_tiles_x = view_w / (2.0f * scale);
    float half_view_tiles_y = view_h / (2.0f * scale);
    cam_x_ = std::max(half_view_tiles_x, std::min(static_cast<float>(mw) - half_view_tiles_x, cam_x_));
    cam_y_ = std::max(half_view_tiles_y, std::min(static_cast<float>(mh) - half_view_tiles_y, cam_y_));

    // Tile range visible
    int tile_x0 = static_cast<int>(cam_x_ - half_view_tiles_x);
    int tile_y0 = static_cast<int>(cam_y_ - half_view_tiles_y);
    int tile_x1 = static_cast<int>(cam_x_ + half_view_tiles_x) + 1;
    int tile_y1 = static_cast<int>(cam_y_ + half_view_tiles_y) + 1;
    tile_x0 = std::max(0, tile_x0);
    tile_y0 = std::max(0, tile_y0);
    tile_x1 = std::min(mw, tile_x1);
    tile_y1 = std::min(mh, tile_y1);

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

    // Tile color mapping
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

    // Helper: tile coord to screen pixel
    auto to_screen_x = [&](float tx) -> int {
        return map_x0 + static_cast<int>((tx - cam_x_ + half_view_tiles_x) * scale);
    };
    auto to_screen_y = [&](float ty) -> int {
        return map_y0 + static_cast<int>((ty - cam_y_ + half_view_tiles_y) * scale);
    };

    // Clip to map viewport
    SDL_Rect clip = {map_x0, map_y0, view_w, view_h};
    SDL_RenderSetClipRect(renderer, &clip);

    // Render visible map tiles
    float ps = std::max(1.0f, scale);
    int step = std::max(1, static_cast<int>(1.0f / scale));

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
    for (int ty = tile_y0; ty < tile_y1; ty += step) {
        for (int tx = tile_x0; tx < tile_x1; tx += step) {
            auto& tile = map.at(tx, ty);
            SDL_Color col;
            if (tile.explored) {
                col = tile_color(tile.type);
            } else {
                col = {18, 18, 22, 255};
            }
            int sx = to_screen_x(static_cast<float>(tx));
            int sy = to_screen_y(static_cast<float>(ty));
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

    // Province labels (only at low zoom where they're readable)
    if (zoom_ <= 2.0f) {
        SDL_Color region_col = {180, 170, 150, 140};
        struct ProvLabel { float tx, ty; const char* name; };
        static const ProvLabel PROV_LABELS[] = {
            {450, 100, "The Frozen Marches"},
            {650, 250, "The Pale Reach"},
            {500, 375, "The Heartlands"},
            {300, 350, "The Greenwood"},
            {700, 375, "The Iron Coast"},
            {500, 550, "The Dust Provinces"},
        };
        for (auto& pl : PROV_LABELS) {
            int lx = to_screen_x(pl.tx);
            int ly = to_screen_y(pl.ty);
            if (lx > map_x0 - 100 && lx < map_x0 + view_w + 100 &&
                ly > map_y0 - 20 && ly < map_y0 + view_h + 20)
                ui::draw_text_centered(renderer, font, pl.name, region_col, lx, ly);
        }
    }

    // Dungeon markers (all 24, red dots)
    struct DungeonMarker { float tx, ty; const char* name; };
    static const DungeonMarker DUNGEONS[] = {
        {560, 375, "The Barrow"}, {650, 325, "Stonekeep"},
        {425, 475, "The Catacombs"}, {700, 375, "The Molten Depths"},
        {275, 275, "The Hollowgate"}, {500, 75, "The Sepulchre"},
        {113, 334, "The Crawl Warren"}, {149, 477, "The Grey Citadel"},
        {332, 170, "The Sunless Galleries"}, {918, 99, "The Dead Ossuary"},
        {448, 609, "The Ash Forge"}, {408, 113, "The Damp Basin"},
        {223, 198, "The Worm Warren"}, {401, 311, "The Broken Vault"},
        {227, 61, "The Deep Halls"}, {506, 149, "The Ossuary"},
        {671, 446, "The Slag Core"}, {131, 66, "The Salt Grotto"},
        {647, 548, "The Worm Tunnels"}, {331, 540, "The Silent Citadel"},
        {847, 372, "The Hollow Underhall"}, {880, 554, "The Grave Ossuary"},
        {271, 683, "The Cinder Core"}, {72, 467, "The Murk Grotto"},
    };
    SDL_Color dungeon_dot = {200, 60, 60, 255};
    for (auto& d : DUNGEONS) {
        int sx = to_screen_x(d.tx);
        int sy = to_screen_y(d.ty);
        SDL_SetRenderDrawColor(renderer, dungeon_dot.r, dungeon_dot.g, dungeon_dot.b, 255);
        int ds = std::max(3, static_cast<int>(scale * 1.5f));
        SDL_Rect dot = {sx - ds / 2, sy - ds / 2, ds, ds};
        SDL_RenderFillRect(renderer, &dot);
        // Show name when zoomed in
        if (zoom_ >= 2.0f) {
            ui::draw_text_clipped(renderer, font, d.name, {180, 140, 130, 200},
                                  sx + ds, sy - line_h / 2, 160);
        }
    }

    // Town markers (white dots with labels)
    struct TownMarker { float tx, ty; const char* name; };
    static const TownMarker TOWNS[] = {
        {500, 375, "Thornwall"}, {425, 475, "Millhaven"}, {725, 250, "Candlemere"},
        {525, 225, "Frostmere"}, {650, 335, "Greywatch"}, {400, 200, "Whitepeak"},
        {325, 400, "Bramblewood"}, {275, 275, "Hollowgate"}, {700, 375, "Ironhearth"},
        {500, 550, "Dustfall"},
    };
    SDL_Color town_label = {220, 210, 200, 255};
    for (auto& t : TOWNS) {
        int sx = to_screen_x(t.tx);
        int sy = to_screen_y(t.ty);
        SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
        int ds = std::max(3, static_cast<int>(scale * 2));
        SDL_Rect dot = {sx - ds / 2, sy - ds / 2, ds, ds};
        SDL_RenderFillRect(renderer, &dot);
        ui::draw_text_clipped(renderer, font, t.name, town_label, sx + ds, sy - line_h / 2, 150);
    }

    // Quest objective marker (green pulsing diamond)
    if (quest_x_ >= 0 && quest_y_ >= 0) {
        int qx = to_screen_x(static_cast<float>(quest_x_));
        int qy = to_screen_y(static_cast<float>(quest_y_));
        Uint32 now = SDL_GetTicks();
        int pulse = 180 + static_cast<int>(75.0f * std::sin(now * 0.004f));
        if (pulse > 255) pulse = 255;
        SDL_SetRenderDrawColor(renderer, 60, 220, 80, static_cast<Uint8>(pulse));
        int qs = std::max(5, static_cast<int>(scale * 3));
        for (int dy = -qs; dy <= qs; dy++) {
            int hw = qs - std::abs(dy);
            SDL_Rect row = {qx - hw, qy + dy, hw * 2 + 1, 1};
            SDL_RenderFillRect(renderer, &row);
        }
    }

    // Player position (bright yellow blinking dot)
    blink_timer_ = SDL_GetTicks();
    bool blink_on = (blink_timer_ / 400) % 2 == 0;
    if (blink_on) {
        int ppx = to_screen_x(static_cast<float>(player_x));
        int ppy = to_screen_y(static_cast<float>(player_y));
        int ds = std::max(5, static_cast<int>(scale * 3));
        SDL_SetRenderDrawColor(renderer, 255, 240, 60, 255);
        SDL_Rect pdot = {ppx - ds / 2, ppy - ds / 2, ds, ds};
        SDL_RenderFillRect(renderer, &pdot);
        SDL_SetRenderDrawColor(renderer, 200, 180, 40, 255);
        SDL_RenderDrawRect(renderer, &pdot);
    }

    // Remove clip rect
    SDL_RenderSetClipRect(renderer, nullptr);

    // Legend (bottom of panel, above hint)
    int legend_y = map_y0 + view_h + 8;
    int legend_x = px + 16;
    int swatch = std::max(8, line_h - 4);
    int spacing = 8;

    auto draw_legend_item = [&](int& x, int y, SDL_Color color, const char* label) {
        SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
        SDL_Rect sw_rect = {x, y + (line_h - swatch) / 2, swatch, swatch};
        SDL_RenderFillRect(renderer, &sw_rect);
        x += swatch + 4;
        SDL_Color text_col = {180, 170, 160, 255};
        ui::draw_text(renderer, font, label, text_col, x, y);
        int tw = 0, th = 0;
        TTF_SizeText(font, label, &tw, &th);
        x += tw + spacing * 2;
    };

    int lx = legend_x;
    draw_legend_item(lx, legend_y, {255, 255, 255, 255}, "Town");
    draw_legend_item(lx, legend_y, {200, 60, 60, 255}, "Dungeon");
    draw_legend_item(lx, legend_y, {255, 240, 60, 255}, "You");
    if (quest_x_ >= 0 && quest_y_ >= 0) {
        draw_legend_item(lx, legend_y, {60, 220, 80, 255}, "Quest");
    }

    // Hint
    SDL_Color hint_col = {120, 110, 100, 255};
    { auto* ig = InputGlyphs::get();
      char hbuf[128];
      if (ig && ig->using_gamepad())
          snprintf(hbuf, sizeof(hbuf), "D-Pad pan   LB/RB zoom   %s close", ig->cancel().c_str());
      else
          snprintf(hbuf, sizeof(hbuf), "[Arrows] pan   [+/-] zoom   [Esc/M] close");
      ui::draw_text_centered(renderer, font, hbuf, hint_col, screen_w / 2, py + panel_h - line_h - 6); }
}
