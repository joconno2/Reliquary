#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <string>

struct FloatingText {
    float wx, wy;       // world tile coords
    float offset_y;     // pixel drift upward (negative = up)
    float life;         // 1.0 -> 0.0
    float decay;        // life lost per frame
    std::string text;
    SDL_Color color;
    bool big;           // use title font (for crits)
    float jitter_x;     // random horizontal offset in pixels
};

class FloatingTextSystem {
public:
    // Spawn a floating number at world position (tile coords)
    void spawn(float wx, float wy, const char* text, SDL_Color color, bool big = false);
    void spawn(float wx, float wy, int amount, SDL_Color color, bool big = false);

    // Call once per frame
    void update();

    // Render all active texts. cam_x/cam_y in tiles, tile_size in pixels.
    void render(SDL_Renderer* renderer, TTF_Font* font_body, TTF_Font* font_big,
                int cam_x, int cam_y, int tile_size, int y_offset);

    void clear() { texts_.clear(); }

private:
    std::vector<FloatingText> texts_;
    static constexpr int MAX_TEXTS = 32;
};
