#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

class HelpScreen {
public:
    HelpScreen() = default;

    void open() { open_ = true; }
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    bool handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                int w, int h) const;

private:
    bool open_ = false;
};
