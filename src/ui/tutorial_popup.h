#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>

// Semi-modal tutorial popup. Pauses the game until dismissed.
class TutorialPopup {
public:
    void show(const char* title, const char* body);
    void dismiss() { open_ = false; }
    bool is_open() const { return open_; }

    // Returns true if the event was consumed (any key/click dismisses)
    bool handle_input(const SDL_Event& event);

    // Render centered on screen
    void render(SDL_Renderer* renderer, TTF_Font* font_body, TTF_Font* font_title,
                int screen_w, int screen_h);

private:
    bool open_ = false;
    std::string title_;
    std::string body_;
};
