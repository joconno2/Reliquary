#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <cstdint>

enum class MaterialType : int {
    NONE = 0,    // non-physical items (potions, scrolls, food)
    BONE,        // -1 dmg, very light, sacred to Vethrik
    WOOD,        // -2 dmg, light, sacred to Khael
    IRON,        // +0, standard
    STEEL,       // +1, improved
    SILVER,      // +0, +50% vs undead/lycanthropes
    OBSIDIAN,    // +2, shatters on crit fail, sacred to Yashkhet
    MITHRIL,     // +2, very light, reduced armor penalty
    ADAMANTINE,  // +4, heavy, ignores armor
    MAT_COUNT
};

inline const char* material_name(MaterialType m) {
    switch (m) {
        case MaterialType::BONE:       return "bone";
        case MaterialType::WOOD:       return "wooden";
        case MaterialType::IRON:       return "iron";
        case MaterialType::STEEL:      return "steel";
        case MaterialType::SILVER:     return "silver";
        case MaterialType::OBSIDIAN:   return "obsidian";
        case MaterialType::MITHRIL:    return "mithril";
        case MaterialType::ADAMANTINE: return "adamantine";
        default: return "";
    }
}

// Visual tint per material (applied to item sprite)
inline SDL_Color material_tint(MaterialType m) {
    switch (m) {
        case MaterialType::BONE:       return {220, 210, 190, 255}; // pale yellow
        case MaterialType::WOOD:       return {180, 140, 100, 255}; // brown
        case MaterialType::IRON:       return {200, 200, 210, 255}; // grey-blue
        case MaterialType::STEEL:      return {220, 225, 230, 255}; // bright silver
        case MaterialType::SILVER:     return {230, 230, 240, 255}; // bright white-blue
        case MaterialType::OBSIDIAN:   return {60, 50, 70, 255};    // dark purple-black
        case MaterialType::MITHRIL:    return {200, 220, 255, 255}; // bright blue-white
        case MaterialType::ADAMANTINE: return {180, 255, 180, 255}; // green-white
        default: return {255, 255, 255, 255};
    }
}

inline int material_damage_mod(MaterialType m) {
    switch (m) {
        case MaterialType::BONE:       return -1;
        case MaterialType::WOOD:       return -2;
        case MaterialType::IRON:       return 0;
        case MaterialType::STEEL:      return 1;
        case MaterialType::SILVER:     return 0;
        case MaterialType::OBSIDIAN:   return 2;
        case MaterialType::MITHRIL:    return 2;
        case MaterialType::ADAMANTINE: return 4;
        default: return 0;
    }
}

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

    // Material (weapons/armor)
    MaterialType material = MaterialType::NONE;

    // Item tags for sacred/profane system (bitmask from tenet.h ItemTag)
    uint32_t tags = 0;

    // Display name respecting identification and material
    std::string display_name() const {
        if (!identified && !unid_name.empty()) return unid_name;
        if (material != MaterialType::NONE && material != MaterialType::IRON) {
            return std::string(material_name(material)) + " " + name;
        }
        return name;
    }
};
