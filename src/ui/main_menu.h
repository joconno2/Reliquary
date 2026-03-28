#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/spritesheet.h"

enum class MenuChoice {
    NONE,
    NEW_GAME,
    CONTINUE,
    LOAD,
    SETTINGS,
    QUIT
};

class MainMenu {
public:
    MainMenu() = default;

    MenuChoice handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* body, TTF_Font* title,
                TTF_Font* title_large, const SpriteManager& sprites, int w, int h) const;

    void set_can_continue(bool val) { can_continue_ = val; }

private:
    int selected_ = 0;
    bool can_continue_ = false;

    // Options: New Game, [Continue], Load, Settings, Quit
    int option_count() const { return can_continue_ ? 5 : 4; }
};
