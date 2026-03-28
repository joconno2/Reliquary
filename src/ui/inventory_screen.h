#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/ecs.h"
#include "core/spritesheet.h"
#include "ui/message_log.h"

enum class InvAction {
    NONE,
    CLOSE,
    EQUIP,    // equip/unequip selected item
    USE,      // use (drink/eat/read) selected item
    DROP,     // drop selected item
};

class InventoryScreen {
public:
    InventoryScreen() = default;

    void open(Entity player) { open_ = true; selected_ = 0; player_ = player; }
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    // Handle input, return action to perform
    InvAction handle_input(SDL_Event& event);

    // Get selected item entity
    Entity get_selected_item(World& world) const;

    void render(SDL_Renderer* renderer, TTF_Font* font,
                const SpriteManager& sprites, World& world,
                int screen_w, int screen_h) const;

private:
    bool open_ = false;
    int selected_ = 0;
    Entity player_ = 0;
};
