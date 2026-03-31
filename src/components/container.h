#pragma once
#include "components/item.h"

// A container (chest, jar, coffin) that can be opened to reveal loot.
// The entity has Renderable + Position + Container. No Item component.
// When opened: sprite changes to open variant, loot item spawned on top.
struct Container {
    int open_sprite_x;   // sprite col when opened
    int open_sprite_y;   // sprite row when opened
    bool opened = false;
    Item contents;       // what's inside (spawned as new entity when opened)
};
