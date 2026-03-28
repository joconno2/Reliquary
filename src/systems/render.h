#pragma once
#include "core/tilemap.h"
#include "core/spritesheet.h"
#include "core/ecs.h"

struct Camera {
    int x = 0, y = 0; // top-left tile coordinate
    int viewport_w = 0, viewport_h = 0; // in pixels
    int tile_size = 48; // rendered tile size (base 32 * scale)

    int tiles_wide() const { return viewport_w / tile_size; }
    int tiles_high() const { return viewport_h / tile_size; }

    void center_on(int tx, int ty) {
        x = tx - tiles_wide() / 2;
        y = ty - tiles_high() / 2;
    }
};

namespace render {

struct SpriteRef {
    int sheet;
    int col;
    int row;
};

struct FloorSprite {
    SpriteRef base;
    SpriteRef overlay;
    bool has_overlay;
};

SpriteRef tile_sprite(TileType type, uint8_t variant);

void draw_map(SDL_Renderer* renderer, const SpriteManager& sprites,
              const TileMap& map, const Camera& cam, int y_offset = 0);

void draw_entities(SDL_Renderer* renderer, const SpriteManager& sprites,
                   World& world, const TileMap& map, const Camera& cam,
                   int y_offset = 0);

} // namespace render
