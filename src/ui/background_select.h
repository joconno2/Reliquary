#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "components/background.h"

class BackgroundSelectScreen {
public:
    BackgroundSelectScreen() = default;

    void reset() { selected_ = 0; confirmed_ = false; }
    bool is_confirmed() const { return confirmed_; }
    BackgroundId get_selected() const { return static_cast<BackgroundId>(selected_); }

    // Returns true if input was consumed
    bool handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font, int w, int h) const;

private:
    int selected_ = 0;
    bool confirmed_ = false;
    mutable int row_h_ = 0;
    mutable int list_y_ = 0;
};
