#include "core/spritesheet.h"
#include <SDL2/SDL_image.h>
#include <cstdio>
#include <algorithm>

SpriteManager::~SpriteManager() {
    for (auto& s : sheets_) {
        if (s.texture) SDL_DestroyTexture(s.texture);
    }
}

bool SpriteManager::load_all(SDL_Renderer* renderer, const std::string& dir) {
    sheets_.resize(SHEET_COUNT);

    struct SheetDef {
        int index;
        const char* filename;
    };

    SheetDef defs[] = {
        {SHEET_ROGUES,         "rogues.png"},
        {SHEET_MONSTERS,       "monsters.png"},
        {SHEET_ANIMALS,        "animals.png"},
        {SHEET_ITEMS,          "items.png"},
        {SHEET_ITEMS_PALETTE,  "items-palette-swaps.png"},
        {SHEET_TILES,          "tiles.png"},
        {SHEET_ANIMATED,       "animated-tiles.png"},
        {SHEET_AUTOTILES,      "autotiles.png"},
    };

    for (auto& def : defs) {
        std::string path = dir + "/" + def.filename;
        if (!load_sheet(renderer, path, def.index)) {
            fprintf(stderr, "Failed to load spritesheet: %s\n", path.c_str());
            return false;
        }
    }

    // Create horizontally flipped copies by reloading PNGs and flipping pixels directly
    // This avoids render target / pixel format issues with sdl2-compat
    const char* filenames[] = {
        "rogues.png", "monsters.png", "animals.png", "items.png",
        "items-palette-swaps.png", "tiles.png", "animated-tiles.png", "autotiles.png",
    };

    sheets_.resize(SHEET_COUNT * 2);
    for (int i = 0; i < SHEET_COUNT; i++) {
        std::string path = dir + "/" + filenames[i];
        SDL_Surface* surface = IMG_Load(path.c_str());
        if (!surface) continue;

        // Convert to 32-bit RGBA for easy pixel manipulation
        SDL_Surface* rgba = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA32, 0);
        SDL_FreeSurface(surface);
        if (!rgba) continue;

        // Flip each 32x32 tile horizontally
        SDL_LockSurface(rgba);
        auto* pixels = static_cast<uint32_t*>(rgba->pixels);
        int pw = rgba->w;
        int ph = rgba->h;
        int pitch_px = rgba->pitch / 4;
        for (int ty = 0; ty < ph; ty += TILE_SIZE) {
            for (int tx = 0; tx < pw; tx += TILE_SIZE) {
                for (int row = ty; row < ty + TILE_SIZE && row < ph; row++) {
                    for (int col = 0; col < TILE_SIZE / 2; col++) {
                        int left = row * pitch_px + tx + col;
                        int right = row * pitch_px + tx + (TILE_SIZE - 1 - col);
                        if (tx + TILE_SIZE - 1 - col < pw) {
                            std::swap(pixels[left], pixels[right]);
                        }
                    }
                }
            }
        }
        SDL_UnlockSurface(rgba);

        SDL_Texture* flipped = SDL_CreateTextureFromSurface(renderer, rgba);
        SDL_FreeSurface(rgba);
        if (!flipped) continue;

        SDL_SetTextureBlendMode(flipped, SDL_BLENDMODE_BLEND);
        int fi = SHEET_COUNT + i;
        sheets_[fi].texture = flipped;
        sheets_[fi].pixel_w = sheets_[i].pixel_w;
        sheets_[fi].pixel_h = sheets_[i].pixel_h;
    }

    return true;
}

bool SpriteManager::load_sheet(SDL_Renderer* renderer, const std::string& path, int index) {
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface) {
        fprintf(stderr, "IMG_Load error: %s\n", IMG_GetError());
        return false;
    }

    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer, surface);
    sheets_[index].pixel_w = surface->w;
    sheets_[index].pixel_h = surface->h;
    SDL_FreeSurface(surface);

    if (!texture) {
        fprintf(stderr, "SDL_CreateTextureFromSurface error: %s\n", SDL_GetError());
        return false;
    }

    // Enable alpha blending
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    sheets_[index].texture = texture;
    return true;
}

void SpriteManager::draw_sprite(SDL_Renderer* renderer, int sheet, int col, int row,
                                 int dest_x, int dest_y, int scale,
                                 SDL_Color tint) const {
    if (sheet < 0 || sheet >= static_cast<int>(sheets_.size())) return;
    auto& s = sheets_[sheet];
    if (!s.texture) return;

    SDL_Rect src = {col * TILE_SIZE, row * TILE_SIZE, TILE_SIZE, TILE_SIZE};
    SDL_Rect dst = {dest_x, dest_y, TILE_SIZE * scale, TILE_SIZE * scale};

    SDL_SetTextureColorMod(s.texture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(s.texture, tint.a);
    SDL_RenderCopy(renderer, s.texture, &src, &dst);
    // Reset
    SDL_SetTextureColorMod(s.texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(s.texture, 255);
}

void SpriteManager::draw_sprite_sized(SDL_Renderer* renderer, int sheet, int col, int row,
                                       int dest_x, int dest_y, int dest_size,
                                       SDL_Color tint, bool flip_h) const {
    if (sheet < 0 || sheet >= static_cast<int>(sheets_.size())) return;
    auto& s = sheets_[sheet];
    if (!s.texture) return;

    SDL_Rect src = {col * TILE_SIZE, row * TILE_SIZE, TILE_SIZE, TILE_SIZE};
    SDL_Rect dst = {dest_x, dest_y, dest_size, dest_size};

    // Use pre-flipped sheet if flip requested
    int actual_sheet = sheet;
    if (flip_h) {
        int flipped_idx = FLIPPED_OFFSET + sheet;
        if (flipped_idx < static_cast<int>(sheets_.size()) && sheets_[flipped_idx].texture) {
            actual_sheet = flipped_idx;
        }
    }
    auto& actual = sheets_[actual_sheet];

    SDL_SetTextureColorMod(actual.texture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(actual.texture, tint.a);
    SDL_RenderCopy(renderer, actual.texture, &src, &dst);
    SDL_SetTextureColorMod(actual.texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(actual.texture, 255);
}

int SpriteManager::sheet_cols(int sheet) const {
    if (sheet < 0 || sheet >= static_cast<int>(sheets_.size())) return 0;
    return sheets_[sheet].pixel_w / TILE_SIZE;
}

int SpriteManager::sheet_rows(int sheet) const {
    if (sheet < 0 || sheet >= static_cast<int>(sheets_.size())) return 0;
    return sheets_[sheet].pixel_h / TILE_SIZE;
}
