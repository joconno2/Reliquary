#include "systems/particles.h"
#include <cstdlib>
#include <cmath>
#include <algorithm>

float ParticleSystem::randf() {
    return static_cast<float>(rand()) / RAND_MAX;
}

float ParticleSystem::randf_signed() {
    return randf() * 2.0f - 1.0f;
}

void ParticleSystem::burst(float wx, float wy, int count,
                            uint8_t r, uint8_t g, uint8_t b,
                            float speed, float lifetime, int size) {
    for (int i = 0; i < count; i++) {
        float angle = randf() * 6.283f;
        float spd = speed * (0.3f + randf() * 0.7f);
        float decay = 1.0f / (lifetime * (60.0f * (0.7f + randf() * 0.6f)));
        particles_.push_back({
            wx + 0.5f, wy + 0.5f,
            std::cos(angle) * spd, std::sin(angle) * spd,
            r, g, b, 1.0f, decay, size
        });
    }
}

void ParticleSystem::directional(float wx, float wy, float dx, float dy, int count,
                                  uint8_t r, uint8_t g, uint8_t b,
                                  float speed, float lifetime, int size) {
    float len = std::sqrt(dx * dx + dy * dy);
    if (len < 0.01f) { burst(wx, wy, count, r, g, b, speed, lifetime, size); return; }
    dx /= len; dy /= len;
    for (int i = 0; i < count; i++) {
        float spread = 0.4f;
        float vx = (dx + randf_signed() * spread) * speed * (0.5f + randf() * 0.5f);
        float vy = (dy + randf_signed() * spread) * speed * (0.5f + randf() * 0.5f);
        float decay = 1.0f / (lifetime * (60.0f * (0.7f + randf() * 0.6f)));
        particles_.push_back({
            wx + 0.5f, wy + 0.5f, vx, vy,
            r, g, b, 1.0f, decay, size
        });
    }
}

void ParticleSystem::rise(float wx, float wy, int count,
                           uint8_t r, uint8_t g, uint8_t b,
                           float lifetime, int size) {
    for (int i = 0; i < count; i++) {
        float vx = randf_signed() * 0.025f;
        float vy = -(0.025f + randf() * 0.04f);
        float decay = 1.0f / (lifetime * (60.0f * (0.7f + randf() * 0.6f)));
        particles_.push_back({
            wx + randf_signed() * 0.4f + 0.5f, wy + 0.5f,
            vx, vy,
            r, g, b, 1.0f, decay, size
        });
    }
}

void ParticleSystem::trail(float x0, float y0, float x1, float y1, int count,
                            uint8_t r, uint8_t g, uint8_t b, int size) {
    for (int i = 0; i < count; i++) {
        float t = static_cast<float>(i) / count;
        float px = x0 + (x1 - x0) * t + randf_signed() * 0.2f;
        float py = y0 + (y1 - y0) * t + randf_signed() * 0.2f;
        float decay = 1.0f / (0.4f * 60.0f * (0.8f + randf() * 0.4f));
        particles_.push_back({
            px + 0.5f, py + 0.5f,
            randf_signed() * 0.008f, randf_signed() * 0.008f,
            r, g, b, 0.6f + randf() * 0.4f, decay, size
        });
    }
}

// --- Presets (large, visible) ---

void ParticleSystem::blood(float wx, float wy) {
    burst(wx, wy, 12, 180, 30, 30, 0.08f, 0.6f, 6);
    burst(wx, wy, 6, 140, 20, 20, 0.05f, 0.5f, 4);
}

void ParticleSystem::hit_spark(float wx, float wy) {
    burst(wx, wy, 8, 220, 200, 160, 0.09f, 0.4f, 5);
}

void ParticleSystem::crit_flash(float wx, float wy) {
    burst(wx, wy, 20, 255, 255, 200, 0.14f, 0.5f, 8);
    burst(wx, wy, 12, 255, 200, 100, 0.16f, 0.6f, 6);
}

void ParticleSystem::death_burst(float wx, float wy) {
    burst(wx, wy, 25, 160, 40, 40, 0.1f, 0.8f, 7);
    burst(wx, wy, 10, 100, 80, 60, 0.07f, 1.0f, 5);
}

void ParticleSystem::heal_effect(float wx, float wy) {
    rise(wx, wy, 15, 100, 220, 100, 0.9f, 6);
    rise(wx, wy, 8, 160, 255, 160, 0.7f, 4);
}

void ParticleSystem::poison_effect(float wx, float wy) {
    rise(wx, wy, 10, 80, 200, 80, 0.6f, 6);
    burst(wx, wy, 5, 60, 160, 60, 0.04f, 0.5f, 4);
}

void ParticleSystem::burn_effect(float wx, float wy) {
    rise(wx, wy, 12, 255, 160, 40, 0.7f, 6);
    rise(wx, wy, 6, 255, 80, 20, 0.5f, 4);
}

void ParticleSystem::bleed_effect(float wx, float wy) {
    burst(wx, wy, 8, 200, 50, 50, 0.04f, 0.6f, 5);
}

void ParticleSystem::spell_effect(float wx, float wy, uint8_t r, uint8_t g, uint8_t b) {
    burst(wx, wy, 18, r, g, b, 0.1f, 0.7f, 6);
}

void ParticleSystem::levelup_effect(float wx, float wy) {
    rise(wx, wy, 25, 255, 220, 100, 1.2f, 7);
    rise(wx, wy, 15, 255, 255, 200, 1.0f, 5);
}

void ParticleSystem::gold_sparkle(float wx, float wy) {
    burst(wx, wy, 10, 255, 220, 50, 0.06f, 0.5f, 4);
}

void ParticleSystem::prayer_effect(float wx, float wy, uint8_t r, uint8_t g, uint8_t b) {
    rise(wx, wy, 20, r, g, b, 1.2f, 7);
    burst(wx, wy, 12, r, g, b, 0.07f, 0.8f, 5);
}

void ParticleSystem::arrow_trail(float x0, float y0, float x1, float y1) {
    trail(x0, y0, x1, y1, 12, 180, 160, 120, 4);
}

// --- Update & Render ---

void ParticleSystem::update() {
    for (auto& p : particles_) {
        p.x += p.vx;
        p.y += p.vy;
        p.vy += 0.001f;
        p.life -= p.decay;
    }
    particles_.erase(
        std::remove_if(particles_.begin(), particles_.end(),
            [](const Particle& p) { return p.life <= 0.0f; }),
        particles_.end());
}

void ParticleSystem::render(SDL_Renderer* renderer, int cam_x, int cam_y,
                             int tile_size, int y_offset) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (auto& p : particles_) {
        int sx = static_cast<int>((p.x - cam_x) * tile_size);
        int sy = static_cast<int>((p.y - cam_y) * tile_size) + y_offset;
        uint8_t alpha = static_cast<uint8_t>(std::max(0.0f, std::min(255.0f, p.life * 255.0f)));
        SDL_SetRenderDrawColor(renderer, p.r, p.g, p.b, alpha);
        SDL_Rect rect = {sx - p.size / 2, sy - p.size / 2, p.size, p.size};
        SDL_RenderFillRect(renderer, &rect);
    }
}
