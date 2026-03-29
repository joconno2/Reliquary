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
    PET,
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
    PET,
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

    // Quest
    int quest_id = -1; // if >= 0, picking this up completes this quest

    // Ranged
    int range = 0; // 0 = melee, >0 = ranged weapon with this range

    // Curse state: 0 = normal, 1 = cursed (can't unequip), 2 = blessed (+1 primary bonus)
    int curse_state = 0;

    // Spellbook — if >= 0, using this teaches this spell
    int teaches_spell = -1;

    // Pet type (-1 = not a pet, else PetId)
    int pet_id = -1;

    // Stacking
    int stack = 1;
    bool stackable = false;

    // Display name respecting identification
    const std::string& display_name() const {
        if (!identified && !unid_name.empty()) return unid_name;
        return name;
    }
};
