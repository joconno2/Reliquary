#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/ecs.h"
#include "core/spritesheet.h"
#include "ui/message_log.h"

enum class InvAction {
    NONE,
    CLOSE,
    EQUIP,
    USE,
    DROP,
};

enum class InvSortMode {
    DEFAULT,    // insertion order
    BY_TYPE,    // weapons, armor, accessories, consumables, other
    BY_RARITY,  // relic > legendary > rare > magic > common
    BY_VALUE,   // gold value descending
    COUNT
};

class InventoryScreen {
public:
    InventoryScreen() = default;

    void open(Entity player) { open_ = true; selected_ = 0; player_ = player; }
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    InvAction handle_input(SDL_Event& event);
    Entity get_selected_item(World& world) const;

    void render(SDL_Renderer* renderer, TTF_Font* font,
                const SpriteManager& sprites, World& world,
                int screen_w, int screen_h) const;

private:
    bool open_ = false;
    int selected_ = 0;
    mutable int scroll_ = 0; // first visible item index
    Entity player_ = 0;
    mutable InvSortMode sort_mode_ = InvSortMode::DEFAULT;

    // Get sorted item indices for display
    std::vector<int> get_sorted_indices(World& world) const;

    // Cached layout rects for mouse interaction (mutable for const render)
    struct SlotRect { SDL_Rect rect; int slot_index; }; // equipment slot
    struct ItemRect { SDL_Rect rect; int item_index; }; // carried item
    mutable std::vector<SlotRect> slot_rects_;
    mutable std::vector<ItemRect> item_rects_;
    mutable SDL_Rect equip_btn_, use_btn_, drop_btn_; // action buttons

    int find_slot_at(int mx, int my) const;
    int find_item_at(int mx, int my) const;
    int find_button_at(int mx, int my) const; // 0=equip, 1=use, 2=drop, -1=none
};
