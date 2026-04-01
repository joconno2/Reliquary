#include "systems/particles.h"
#include "components/god.h"
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

void ParticleSystem::projectile(float x0, float y0, float x1, float y1, int count,
                                 uint8_t r, uint8_t g, uint8_t b, float speed, int size) {
    float dx = x1 - x0, dy = y1 - y0;
    float dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 0.01f) dist = 1.0f;
    float vx = (dx / dist) * speed;
    float vy = (dy / dist) * speed;
    float lifetime = dist / speed; // frames to reach target
    float decay = 1.0f / std::max(1.0f, lifetime);

    for (int i = 0; i < count; i++) {
        // Stagger spawn: particles start at source with slight time offset
        // Earlier particles are further along the path
        float t_offset = static_cast<float>(i) / count * 0.3f; // 30% spread
        float px = x0 + 0.5f + vx * t_offset * lifetime + randf_signed() * 0.15f;
        float py = y0 + 0.5f + vy * t_offset * lifetime + randf_signed() * 0.15f;
        // Add slight perpendicular jitter to velocity
        float jx = randf_signed() * speed * 0.1f;
        float jy = randf_signed() * speed * 0.1f;
        particles_.push_back({
            px, py,
            vx + jx, vy + jy,
            r, g, b, 0.8f + randf() * 0.2f, decay * (0.8f + randf() * 0.4f), size
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

void ParticleSystem::fall(float wx, float wy, int count,
                           uint8_t r, uint8_t g, uint8_t b,
                           float lifetime, int size) {
    for (int i = 0; i < count; i++) {
        float vx = randf_signed() * 0.015f;
        float vy = 0.02f + randf() * 0.03f; // downward
        float decay = 1.0f / (lifetime * (60.0f * (0.7f + randf() * 0.6f)));
        particles_.push_back({
            wx + randf_signed() * 0.3f + 0.5f, wy + 0.3f,
            vx, vy,
            r, g, b, 1.0f, decay, size
        });
    }
}

void ParticleSystem::drift(float wx, float wy, int count,
                            uint8_t r, uint8_t g, uint8_t b,
                            float lifetime, int size) {
    for (int i = 0; i < count; i++) {
        float angle = randf() * 6.283f;
        float spd = 0.01f + randf() * 0.015f; // slow outward
        float decay = 1.0f / (lifetime * (60.0f * (0.7f + randf() * 0.6f)));
        particles_.push_back({
            wx + randf_signed() * 0.2f + 0.5f, wy + randf_signed() * 0.2f + 0.5f,
            std::cos(angle) * spd, std::sin(angle) * spd,
            r, g, b, 0.7f + randf() * 0.3f, decay, size
        });
    }
}

void ParticleSystem::orbit(float wx, float wy, int count,
                            uint8_t r, uint8_t g, uint8_t b,
                            float radius, float lifetime, int size) {
    for (int i = 0; i < count; i++) {
        float angle = randf() * 6.283f;
        float ox = std::cos(angle) * radius;
        float oy = std::sin(angle) * radius;
        // velocity perpendicular to radius (tangential orbit)
        float spd = 0.02f + randf() * 0.01f;
        float vx = -std::sin(angle) * spd;
        float vy = std::cos(angle) * spd;
        float decay = 1.0f / (lifetime * (60.0f * (0.7f + randf() * 0.6f)));
        particles_.push_back({
            wx + 0.5f + ox, wy + 0.5f + oy,
            vx, vy,
            r, g, b, 0.6f + randf() * 0.4f, decay, size
        });
    }
}

void ParticleSystem::god_aura(GodId god, float wx, float wy) {
    if (god == GodId::NONE) return;
    auto& info = get_god_info(god);
    uint8_t r = info.color.r, g = info.color.g, b = info.color.b;

    switch (god) {
    case GodId::VETHRIK:
        // Pale bone-white motes drift upward
        rise(wx, wy, 2, r, g, b, 1.5f, 3);
        if (randf() < 0.3f) rise(wx, wy, 1, 200, 200, 180, 2.0f, 2);
        break;
    case GodId::THESSARKA:
        // Faint glyphs orbit the head
        orbit(wx, wy - 0.3f, 2, r, g, b, 0.35f, 1.5f, 2);
        break;
    case GodId::MORRETH:
        // Iron sparks at feet
        if (randf() < 0.4f)
            burst(wx, wy + 0.3f, 2, r, g, b, 0.03f, 0.4f, 2);
        break;
    case GodId::YASHKHET:
        // Blood drips downward
        fall(wx, wy, 2, r, g, b, 1.0f, 2);
        if (randf() < 0.2f) fall(wx, wy, 1, 160, 30, 30, 0.8f, 3);
        break;
    case GodId::KHAEL:
        // Green leaf/vine particles drift
        drift(wx, wy, 2, r, g, b, 1.5f, 2);
        if (randf() < 0.2f) rise(wx, wy, 1, 60, 160, 40, 1.8f, 3);
        break;
    case GodId::SOLETH:
        // Flickering pale-gold flame halo, embers rise
        rise(wx, wy, 2, r, g, b, 0.8f, 3);
        if (randf() < 0.3f) rise(wx, wy, 1, 255, 180, 60, 0.6f, 2);
        break;
    case GodId::IXUUL:
        // Glitch/distortion — random offset burst, sprite tear feel
        if (randf() < 0.5f) {
            float ox = randf_signed() * 0.3f;
            float oy = randf_signed() * 0.3f;
            burst(wx + ox, wy + oy, 3, r, g, b, 0.04f, 0.3f, 2);
        }
        break;
    case GodId::ZHAVEK:
        // Shadow trail — dark particles, flicker
        if (randf() < 0.5f) {
            drift(wx, wy, 2, r, g, b, 1.0f, 3);
            burst(wx, wy, 1, 30, 30, 50, 0.02f, 0.6f, 4);
        }
        break;
    case GodId::THALARA:
        // Blue-green ripple particles rise from feet, water droplets
        rise(wx, wy + 0.3f, 2, r, g, b, 1.2f, 2);
        if (randf() < 0.2f) fall(wx, wy, 1, 60, 140, 180, 0.6f, 2);
        break;
    case GodId::OSSREN:
        // Metallic gold sparks from armor, forge-ember glow
        if (randf() < 0.4f)
            burst(wx, wy, 2, r, g, b, 0.04f, 0.5f, 2);
        if (randf() < 0.15f)
            rise(wx, wy, 1, 255, 140, 40, 0.5f, 3);
        break;
    case GodId::LETHIS:
        // Soft purple mist, floating eye particles
        drift(wx, wy, 2, r, g, b, 2.0f, 3);
        if (randf() < 0.1f)
            rise(wx, wy - 0.2f, 1, 200, 160, 240, 1.5f, 2);
        break;
    case GodId::GATHRUUN:
        // Stone pebbles orbit feet, dust trail
        orbit(wx, wy + 0.3f, 2, r, g, b, 0.3f, 1.0f, 2);
        if (randf() < 0.2f)
            burst(wx, wy + 0.4f, 1, 140, 110, 80, 0.02f, 0.4f, 2);
        break;
    case GodId::SYTHARA:
        // Green spore drift outward, tiny mushroom pops
        drift(wx, wy, 2, r, g, b, 1.5f, 2);
        if (randf() < 0.15f)
            burst(wx, wy, 2, 80, 140, 40, 0.03f, 0.8f, 3);
        break;
    default:
        break;
    }
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
