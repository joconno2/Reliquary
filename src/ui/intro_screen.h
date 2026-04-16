#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>
#include "components/god.h"

// Cinematic intro sequence: black screen, text fades in line by line.
// Press any key to skip or advance.
class IntroScreen {
public:
    struct Line {
        std::string text;
        SDL_Color color;
        int delay_ms;      // time after previous line before this one starts fading in
        int fade_ms;        // duration of fade-in
    };

    void start(GodId god);
    bool handle_input(SDL_Event& event);
    void render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                int w, int h) const;

    bool is_done() const { return done_; }

private:
    std::vector<Line> lines_;
    Uint32 start_time_ = 0;
    bool done_ = false;
    bool skipping_ = false;
};
