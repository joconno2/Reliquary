#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include "core/spritesheet.h"
#include "save/save.h"

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
    mutable std::vector<SDL_Rect> option_rects_; // populated during render for mouse hit-testing

    // Options: New Game, [Continue], Load, Settings, Quit
    int option_count() const { return can_continue_ ? 5 : 4; }

    MenuChoice choice_for_index(int idx) const;
};
