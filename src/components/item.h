#pragma once
#include <string>

enum class ItemType : int {
    WEAPON,
    ARMOR_HEAD,
    ARMOR_CHEST,
    ARMOR_HANDS,
    ARMOR_FEET,
    SHIELD,
    AMULET,
    RING,
    POTION,
    SCROLL,
    FOOD,
    KEY,
    GOLD,
    COUNT
};

enum class EquipSlot : int {
    NONE = -1,
    MAIN_HAND = 0,
    OFF_HAND,
    HEAD,
    CHEST,
    HANDS,
    FEET,
    AMULET,
    RING_1,
    RING_2,
    SLOT_COUNT
};

constexpr int EQUIP_SLOT_COUNT = static_cast<int>(EquipSlot::SLOT_COUNT);

struct Item {
    std::string name = "unknown item";
    std::string description = "";
    ItemType type = ItemType::WEAPON;
    EquipSlot slot = EquipSlot::NONE; // which slot it equips to

    // Combat bonuses (when equipped)
    int damage_bonus = 0;
    int armor_bonus = 0;
    int attack_bonus = 0;
    int dodge_bonus = 0;

    // Attribute bonuses
    int str_bonus = 0;
    int dex_bonus = 0;
    int con_bonus = 0;

    // Consumable effects
    int heal_amount = 0;
    int mp_restore = 0;

    // Identification
    bool identified = false;
    std::string unid_name = ""; // name when unidentified ("blue potion")

    // Value
    int gold_value = 0;

    // Stacking
    int stack = 1;
    bool stackable = false;

    // Display name respecting identification
    const std::string& display_name() const {
        if (!identified && !unid_name.empty()) return unid_name;
        return name;
    }
};
