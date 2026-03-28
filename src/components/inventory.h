#pragma once
#include "core/ecs.h"
#include "components/item.h"
#include <vector>
#include <array>

struct Inventory {
    static constexpr int MAX_ITEMS = 26; // a-z

    std::vector<Entity> items; // carried items (entity IDs with Item component)
    std::array<Entity, EQUIP_SLOT_COUNT> equipped = {}; // equipped items per slot

    Inventory() { equipped.fill(NULL_ENTITY); }

    bool is_full() const { return static_cast<int>(items.size()) >= MAX_ITEMS; }

    // Add item to inventory. Returns false if full.
    bool add(Entity item) {
        if (is_full()) return false;
        items.push_back(item);
        return true;
    }

    // Remove item from inventory (does NOT unequip)
    void remove(Entity item) {
        for (auto it = items.begin(); it != items.end(); ++it) {
            if (*it == item) {
                items.erase(it);
                return;
            }
        }
    }

    bool has(Entity item) const {
        for (auto e : items) {
            if (e == item) return true;
        }
        return false;
    }

    // Get item in slot, or NULL_ENTITY
    Entity get_equipped(EquipSlot slot) const {
        int idx = static_cast<int>(slot);
        if (idx < 0 || idx >= EQUIP_SLOT_COUNT) return NULL_ENTITY;
        return equipped[idx];
    }

    void equip(EquipSlot slot, Entity item) {
        equipped[static_cast<int>(slot)] = item;
    }

    Entity unequip(EquipSlot slot) {
        int idx = static_cast<int>(slot);
        Entity prev = equipped[idx];
        equipped[idx] = NULL_ENTITY;
        return prev;
    }

    bool is_equipped(Entity item) const {
        for (auto e : equipped) {
            if (e == item) return true;
        }
        return false;
    }
};
