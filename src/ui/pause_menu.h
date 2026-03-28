#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>

enum class PauseChoice {
    NONE,
    CONTINUE,
    SAVE,
    LOAD,
    SETTINGS,
    EXIT_TO_MENU
};

class PauseMenu {
public:
    PauseMenu() = default;

    void open() { open_ = true; selected_ = 0; }
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    PauseChoice handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* body, TTF_Font* title,
                int w, int h) const;

private:
    bool open_ = false;
    int selected_ = 0;
    static constexpr int OPTION_COUNT = 5;
};
