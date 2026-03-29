#pragma once
#include <SDL2/SDL.h>
#include <vector>
#include <cstdint>

struct Particle {
    float x, y;           // world position (tile coords, fractional)
    float vx, vy;         // velocity (tiles per frame)
    uint8_t r, g, b;
    float life;           // remaining (0..1)
    float decay;          // life lost per frame
    int size;             // pixel size (1-4)
};

class ParticleSystem {
public:
    // Spawn helpers — all positions in tile coords
    void burst(float wx, float wy, int count,
               uint8_t r, uint8_t g, uint8_t b,
               float speed = 0.08f, float lifetime = 0.6f, int size = 2);

    void directional(float wx, float wy, float dx, float dy, int count,
                     uint8_t r, uint8_t g, uint8_t b,
                     float speed = 0.06f, float lifetime = 0.5f, int size = 2);

    void rise(float wx, float wy, int count,
              uint8_t r, uint8_t g, uint8_t b,
              float lifetime = 0.8f, int size = 2);

    void trail(float x0, float y0, float x1, float y1, int count,
               uint8_t r, uint8_t g, uint8_t b, int size = 1);

    // Presets
    void blood(float wx, float wy);
    void hit_spark(float wx, float wy);
    void crit_flash(float wx, float wy);
    void death_burst(float wx, float wy);
    void heal_effect(float wx, float wy);
    void poison_effect(float wx, float wy);
    void burn_effect(float wx, float wy);
    void bleed_effect(float wx, float wy);
    void spell_effect(float wx, float wy, uint8_t r, uint8_t g, uint8_t b);
    void levelup_effect(float wx, float wy);
    void gold_sparkle(float wx, float wy);
    void prayer_effect(float wx, float wy, uint8_t r, uint8_t g, uint8_t b);
    void arrow_trail(float x0, float y0, float x1, float y1);

    void update();
    void render(SDL_Renderer* renderer, int cam_x, int cam_y, int tile_size, int y_offset);

    bool empty() const { return particles_.empty(); }

private:
    std::vector<Particle> particles_;
    float randf(); // 0..1
    float randf_signed(); // -1..1
};
