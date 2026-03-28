#pragma once
#include "core/tilemap.h"
#include "core/spritesheet.h"
#include "core/ecs.h"

struct Camera {
    int x = 0, y = 0; // top-left tile coordinate
    int viewport_w = 0, viewport_h = 0; // in pixels

    int tiles_wide() const { return viewport_w / SpriteManager::TILE_SIZE; }
    int tiles_high() const { return viewport_h / SpriteManager::TILE_SIZE; }

    void center_on(int tx, int ty) {
        x = tx - tiles_wide() / 2;
        y = ty - tiles_high() / 2;
    }
};

namespace render {

// Get the sprite coordinates for a tile type
struct SpriteRef {
    int sheet;
    int col;
    int row;
};

SpriteRef tile_sprite(TileType type, uint8_t variant);

// Render the visible tilemap
void draw_map(SDL_Renderer* renderer, const SpriteManager& sprites,
              const TileMap& map, const Camera& cam);

// Render all entities with Position + Renderable
void draw_entities(SDL_Renderer* renderer, const SpriteManager& sprites,
                   World& world, const TileMap& map, const Camera& cam);

} // namespace render
