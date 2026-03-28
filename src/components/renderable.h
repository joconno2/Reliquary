#pragma once
#include <SDL2/SDL.h>

struct Renderable {
    int sprite_sheet = 0; // index into loaded spritesheets
    int sprite_x = 0;     // tile column in spritesheet
    int sprite_y = 0;     // tile row in spritesheet
    SDL_Color tint = {255, 255, 255, 255};
    int z_order = 0;      // higher = drawn later (on top)
};
