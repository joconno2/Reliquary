#include "generation/mapfile.h"
#include <fstream>
#include <cstdio>

namespace mapfile {

static TileType char_to_tile(char c) {
    switch (c) {
        case '.': return TileType::FLOOR_GRASS;
        case ',': return TileType::FLOOR_DIRT;
        case ':': return TileType::FLOOR_STONE;
        case '#': return TileType::WALL_STONE_BRICK;
        case 'w': return TileType::WALL_WOOD;
        case 'g': return TileType::WALL_GRASS;
        case 'n': return TileType::WALL_SANDSTONE;
        case 'T': return TileType::TREE;
        case 't': case '"': case 'b': case 'c': return TileType::BRUSH;
        case '~': return TileType::WATER;
        case 's': return TileType::FLOOR_SAND;
        case 'i': return TileType::FLOOR_ICE;
        case 'I': return TileType::FLOOR_SNOW;
        case 'p': return TileType::FLOOR_COBBLE;
        case 'R': return TileType::ROCK;
        case '+': return TileType::DOOR_CLOSED;
        case '>': return TileType::STAIRS_DOWN;
        case '<': return TileType::STAIRS_UP;
        // NPCs and player start are placed on dirt/grass
        case '@': case 'S': case 'B': case 'P': case 'F': case 'G':
        case 'W': case 'H': case 'M': case 'E':
            return TileType::FLOOR_DIRT;
        default:  return TileType::VOID;
    }
}

static bool is_entity_char(char c) {
    return c == '@' || c == 'S' || c == 'B' || c == 'P' || c == 'F' || c == 'G' || c == 'E'
        || c == 'W' || c == 'H' || c == 'M';
}

MapFileResult load(const std::string& path) {
    MapFileResult result;

    std::ifstream file(path);
    if (!file.is_open()) {
        fprintf(stderr, "Failed to open map file: %s\n", path.c_str());
        return result;
    }

    // Read all lines to determine dimensions
    std::vector<std::string> lines;
    std::string line;
    int max_width = 0;
    while (std::getline(file, line)) {
        if (static_cast<int>(line.size()) > max_width)
            max_width = static_cast<int>(line.size());
        lines.push_back(line);
    }

    int height = static_cast<int>(lines.size());
    if (height == 0 || max_width == 0) return result;

    result.map = TileMap(max_width, height);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < max_width; x++) {
            char c = (x < static_cast<int>(lines[y].size())) ? lines[y][x] : ' ';

            auto& tile = result.map.at(x, y);
            tile.type = char_to_tile(c);
            tile.variant = 0;

            // Brush variants: t=3-6 (crops), b=1 (tall grass), c=2 (flowers)
            if (tile.type == TileType::BRUSH) {
                if (c == 'b') tile.variant = 1;
                else if (c == 'c') tile.variant = 2;
                else tile.variant = 3 + ((x * 7 + y * 13) % 4); // random crop from row 20
            }

            // Add some floor variation
            if (tile.type == TileType::FLOOR_GRASS || tile.type == TileType::FLOOR_DIRT ||
                tile.type == TileType::FLOOR_STONE || tile.type == TileType::FLOOR_SAND ||
                tile.type == TileType::FLOOR_ICE) {
                // ~30% chance of detail variant
                // Use a simple hash for deterministic variation
                unsigned hash = static_cast<unsigned>(x * 7 + y * 13 + x * y);
                if (hash % 100 < 30) {
                    tile.variant = static_cast<uint8_t>(1 + hash % 2);
                }
            }

            if (c == '@') {
                result.start_x = x;
                result.start_y = y;
            }

            if (is_entity_char(c) && c != '@') {
                result.entities.push_back({c, x, y});
            }
        }
    }

    return result;
}

} // namespace mapfile
