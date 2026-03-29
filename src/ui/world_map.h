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

    // Any key closes the map
    bool handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                const TileMap& map, int player_x, int player_y,
                int screen_w, int screen_h) const;

private:
    bool open_ = false;
    mutable Uint32 blink_timer_ = 0;
};
