#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/ecs.h"
#include "core/rng.h"
#include "core/spritesheet.h"
#include "components/item.h"
#include <vector>
#include <string>

enum class ShopAction {
    NONE,
    CLOSE,
    BUY,
    SELL,
};

struct ShopItem {
    Item item;
    int sprite_x = 0;
    int sprite_y = 0;
};

class ShopScreen {
public:
    ShopScreen() = default;

    void open(Entity player, World& world, RNG& rng, int* gold, int difficulty = 0);
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    ShopAction handle_input(SDL_Event& event);

    // Execute the pending action (buy/sell). Returns true if something happened.
    bool execute(World& world, int* gold);

    void render(SDL_Renderer* renderer, TTF_Font* font,
                const SpriteManager& sprites, World& world,
                int screen_w, int screen_h) const;

private:
    bool open_ = false;
    int selected_ = 0;
    Entity player_ = 0;
    int* gold_ = nullptr;
    bool buy_tab_ = true; // true = Buy, false = Sell
    std::vector<ShopItem> stock_;

    void generate_stock(RNG& rng, int difficulty = 0);
};
