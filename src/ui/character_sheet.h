#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/ecs.h"
#include "core/spritesheet.h"

class CharacterSheet {
public:
    CharacterSheet() = default;

    void open(Entity player) { open_ = true; player_ = player; scroll_ = 0; }
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    // Returns true if should close
    bool handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                const SpriteManager& sprites, World& world,
                int screen_w, int screen_h) const;

private:
    bool open_ = false;
    Entity player_ = 0;
    mutable int scroll_ = 0;
};
