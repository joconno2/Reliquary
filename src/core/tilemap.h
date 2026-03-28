#pragma once
#include <vector>
#include <cstdint>

enum class TileType : uint8_t {
    VOID = 0,
    FLOOR_STONE,
    FLOOR_DIRT,
    FLOOR_GRASS,
    FLOOR_BONE,
    FLOOR_RED_STONE,
    WALL_DIRT,
    WALL_STONE_ROUGH,
    WALL_STONE_BRICK,
    WALL_IGNEOUS,
    WALL_LARGE_STONE,
    WALL_CATACOMB,
    DOOR_CLOSED,
    DOOR_OPEN,
    STAIRS_DOWN,
    STAIRS_UP,
    WATER,
    TREE,
    BRUSH,       // walkable vegetation — looks like forest but passable
    COUNT
};

struct Tile {
    TileType type = TileType::VOID;
    bool explored = false;
    bool visible = false;
    uint8_t variant = 0; // random variant for floor/wall variation
};

class TileMap {
public:
    TileMap() : width_(0), height_(0) {}
    TileMap(int w, int h);

    int width() const { return width_; }
    int height() const { return height_; }

    bool in_bounds(int x, int y) const;
    Tile& at(int x, int y);
    const Tile& at(int x, int y) const;

    bool is_walkable(int x, int y) const;
    bool is_opaque(int x, int y) const;

    void clear(TileType fill = TileType::VOID);

private:
    int width_, height_;
    std::vector<Tile> tiles_;
};
