#include "core/tilemap.h"
#include <cassert>

TileMap::TileMap(int w, int h) : width_(w), height_(h), tiles_(w * h) {}

bool TileMap::in_bounds(int x, int y) const {
    return x >= 0 && x < width_ && y >= 0 && y < height_;
}

Tile& TileMap::at(int x, int y) {
    assert(in_bounds(x, y));
    return tiles_[y * width_ + x];
}

const Tile& TileMap::at(int x, int y) const {
    assert(in_bounds(x, y));
    return tiles_[y * width_ + x];
}

bool TileMap::is_walkable(int x, int y) const {
    if (!in_bounds(x, y)) return false;
    auto type = tiles_[y * width_ + x].type;
    switch (type) {
        case TileType::VOID:
        case TileType::WALL_DIRT:
        case TileType::WALL_STONE_ROUGH:
        case TileType::WALL_STONE_BRICK:
        case TileType::WALL_IGNEOUS:
        case TileType::WALL_LARGE_STONE:
        case TileType::WALL_CATACOMB:
        case TileType::DOOR_CLOSED:
        case TileType::TREE:
            return false;
        default:
            return true;
    }
}

bool TileMap::is_opaque(int x, int y) const {
    if (!in_bounds(x, y)) return true;
    auto type = tiles_[y * width_ + x].type;
    switch (type) {
        case TileType::VOID:
        case TileType::WALL_DIRT:
        case TileType::WALL_STONE_ROUGH:
        case TileType::WALL_STONE_BRICK:
        case TileType::WALL_IGNEOUS:
        case TileType::WALL_LARGE_STONE:
        case TileType::WALL_CATACOMB:
        case TileType::DOOR_CLOSED:
        case TileType::TREE:
            return true;
        default:
            return false;
    }
}

void TileMap::clear(TileType fill) {
    for (auto& tile : tiles_) {
        tile.type = fill;
        tile.explored = false;
        tile.visible = false;
        tile.variant = 0;
    }
}
