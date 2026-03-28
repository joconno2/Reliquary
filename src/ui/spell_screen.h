#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/ecs.h"
#include "components/spellbook.h"

enum class SpellAction {
    NONE,
    CLOSE,
    CAST, // cast selected spell
};

class SpellScreen {
public:
    SpellScreen() = default;

    void open(Entity player) { open_ = true; selected_ = 0; player_ = player; }
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    SpellAction handle_input(SDL_Event& event);
    SpellId get_selected_spell(World& world) const;

    void render(SDL_Renderer* renderer, TTF_Font* font,
                World& world, int screen_w, int screen_h) const;

private:
    bool open_ = false;
    int selected_ = 0;
    Entity player_ = 0;
};
