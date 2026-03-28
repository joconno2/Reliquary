#include "core/spritesheet.h"
#include <SDL2/SDL_image.h>
#include <cstdio>

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
                                       SDL_Color tint) const {
    if (sheet < 0 || sheet >= static_cast<int>(sheets_.size())) return;
    auto& s = sheets_[sheet];
    if (!s.texture) return;

    SDL_Rect src = {col * TILE_SIZE, row * TILE_SIZE, TILE_SIZE, TILE_SIZE};
    SDL_Rect dst = {dest_x, dest_y, dest_size, dest_size};

    SDL_SetTextureColorMod(s.texture, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(s.texture, tint.a);
    SDL_RenderCopy(renderer, s.texture, &src, &dst);
    SDL_SetTextureColorMod(s.texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(s.texture, 255);
}

int SpriteManager::sheet_cols(int sheet) const {
    if (sheet < 0 || sheet >= static_cast<int>(sheets_.size())) return 0;
    return sheets_[sheet].pixel_w / TILE_SIZE;
}

int SpriteManager::sheet_rows(int sheet) const {
    if (sheet < 0 || sheet >= static_cast<int>(sheets_.size())) return 0;
    return sheets_[sheet].pixel_h / TILE_SIZE;
}
