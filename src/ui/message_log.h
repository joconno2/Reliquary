#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>

struct Message {
    std::string text;
    SDL_Color color;
    int turn; // game turn when message was created
};

class MessageLog {
public:
    MessageLog() = default;

    void add(const std::string& text, SDL_Color color = {200, 200, 200, 255});
    void set_turn(int turn) { current_turn_ = turn; }
    void scroll_up();
    void scroll_down();
    void scroll_to_bottom();

    // Render the log at the given screen rect using the provided font
    void render(SDL_Renderer* renderer, TTF_Font* font,
                int x, int y, int w, int h) const;

    size_t size() const { return messages_.size(); }

private:
    std::vector<Message> messages_;
    int scroll_offset_ = 0;
    int current_turn_ = 0;
};
