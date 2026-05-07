#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/tilemap.h"

class WorldMap {
public:
    WorldMap() = default;

    void toggle() { open_ = !open_; }
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    // Set optional quest objective marker (call before render, -1 = no marker)
    void set_quest_target(int x, int y) { quest_x_ = x; quest_y_ = y; }

    // Center camera on player when opening
    void center_on(int x, int y) { cam_x_ = static_cast<float>(x); cam_y_ = static_cast<float>(y); }

    bool handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                const TileMap& map, int player_x, int player_y,
                int screen_w, int screen_h) const;

private:
    bool open_ = false;
    int quest_x_ = -1, quest_y_ = -1;
    mutable float cam_x_ = 500.0f, cam_y_ = 375.0f; // camera center (tile coords)
    mutable float zoom_ = 1.0f; // 1.0 = fit-to-screen, >1 = zoomed in
    mutable Uint32 blink_timer_ = 0;
};
