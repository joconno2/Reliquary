#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include <unordered_map>

// Sprite sheet IDs — indices into the loaded sheets array
enum SpriteSheet : int {
    SHEET_ROGUES = 0,
    SHEET_MONSTERS,
    SHEET_ANIMALS,
    SHEET_ITEMS,
    SHEET_ITEMS_PALETTE,
    SHEET_TILES,
    SHEET_ANIMATED,
    SHEET_AUTOTILES,
    SHEET_COUNT
};

class SpriteManager {
public:
    SpriteManager() = default;
    ~SpriteManager();

    // No copy
    SpriteManager(const SpriteManager&) = delete;
    SpriteManager& operator=(const SpriteManager&) = delete;

    bool load_all(SDL_Renderer* renderer, const std::string& asset_dir);

    // Render a single 32x32 tile from a spritesheet (integer scale multiplier)
    void draw_sprite(SDL_Renderer* renderer, int sheet, int col, int row,
                     int dest_x, int dest_y, int scale = 1,
                     SDL_Color tint = {255, 255, 255, 255}) const;

    // Render a 32x32 source tile to an arbitrary destination size
    void draw_sprite_sized(SDL_Renderer* renderer, int sheet, int col, int row,
                            int dest_x, int dest_y, int dest_size,
                            SDL_Color tint = {255, 255, 255, 255}) const;

    // Get dimensions of a sheet in tiles
    int sheet_cols(int sheet) const;
    int sheet_rows(int sheet) const;

    static constexpr int TILE_SIZE = 32;

private:
    struct Sheet {
        SDL_Texture* texture = nullptr;
        int pixel_w = 0;
        int pixel_h = 0;
    };

    std::vector<Sheet> sheets_;

    bool load_sheet(SDL_Renderer* renderer, const std::string& path, int index);
};
