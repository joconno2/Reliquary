#pragma once
#include <SDL2/SDL.h>
#include <string>
#include <cstdint>
#include <vector>

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

// ---------- Rarity & Affix System ----------

enum class Rarity : int {
    COMMON = 0,  // white  -- no affixes
    MAGIC,       // blue   -- 1 affix (prefix OR suffix)
    RARE,        // yellow -- 2 affixes (prefix AND suffix)
    LEGENDARY,   // orange -- hand-authored, no random affixes
    RELIC,       // god-colored -- god relics
    COUNT
};

inline SDL_Color rarity_color(Rarity r) {
    switch (r) {
        case Rarity::COMMON:    return {180, 175, 170, 255}; // grey-white
        case Rarity::MAGIC:     return {100, 160, 255, 255}; // blue
        case Rarity::RARE:      return {255, 255, 100, 255}; // yellow
        case Rarity::LEGENDARY: return {255, 180, 60,  255}; // orange
        case Rarity::RELIC:     return {220, 120, 255, 255}; // purple
        default:                return {180, 175, 170, 255};
    }
}

inline const char* rarity_name(Rarity r) {
    switch (r) {
        case Rarity::COMMON:    return "Common";
        case Rarity::MAGIC:     return "Magic";
        case Rarity::RARE:      return "Rare";
        case Rarity::LEGENDARY: return "Legendary";
        case Rarity::RELIC:     return "Relic";
        default:                return "";
    }
}

// Affix effect types. Each affix applies one effect.
enum class AffixEffect : int {
    NONE = 0,
    // Stat bonuses (stack with base item stats)
    BONUS_DAMAGE,       // +N damage
    BONUS_ARMOR,        // +N armor
    BONUS_ATTACK,       // +N attack (hit chance)
    BONUS_DODGE,        // +N dodge
    BONUS_STR,          // +N STR
    BONUS_DEX,          // +N DEX
    BONUS_CON,          // +N CON
    BONUS_HP,           // +N max HP
    BONUS_MP,           // +N max MP
    BONUS_SPEED,        // +N speed (energy gain per turn)
    // On-hit effects (weapons only, proc chance per hit)
    ONHIT_POISON,       // % chance to poison on hit
    ONHIT_BURN,         // % chance to burn on hit
    ONHIT_FREEZE,       // % chance to freeze on hit
    ONHIT_BLEED,        // % chance to bleed on hit
    ONHIT_LIFESTEAL,    // heal N per hit
    // On-kill effects
    ONKILL_HEAL,        // heal N HP on kill
    ONKILL_MANA,        // restore N MP on kill
    // Defensive
    RESIST_POISON,      // reduce poison tick damage by N
    RESIST_FIRE,        // reduce burn tick damage by N
    // God favor
    BONUS_FAVOR,        // +N favor gain per kill
    COUNT
};

struct Affix {
    std::string name;       // display name ("Blazing", "of the Serpent")
    AffixEffect effect = AffixEffect::NONE;
    int magnitude = 0;      // effect strength (damage bonus, proc %, heal amount, etc.)
    bool is_prefix = true;  // true = prefix, false = suffix
};

// Master affix table. Used by generation code.
struct AffixDef {
    const char* name;
    AffixEffect effect;
    int min_mag;         // minimum magnitude (rolled between min and max)
    int max_mag;
    bool is_prefix;
    int min_depth;       // earliest dungeon depth this can appear
    bool weapons_only;   // only rolls on weapons
    bool armor_only;     // only rolls on armor/shields/accessories
};

// 20 prefixes
static const AffixDef PREFIX_TABLE[] = {
    // Elemental on-hit (weapons only)
    {"Blazing",     AffixEffect::ONHIT_BURN,     15, 30, true, 3, true,  false},
    {"Venomous",    AffixEffect::ONHIT_POISON,   15, 30, true, 2, true,  false},
    {"Frozen",      AffixEffect::ONHIT_FREEZE,   15, 25, true, 4, true,  false},
    {"Serrated",    AffixEffect::ONHIT_BLEED,    20, 35, true, 2, true,  false},
    {"Vampiric",    AffixEffect::ONHIT_LIFESTEAL, 2,  4, true, 5, true,  false},
    // Damage/offense (weapons only)
    {"Keen",        AffixEffect::BONUS_ATTACK,     1,  3, true, 1, true,  false},
    {"Brutal",      AffixEffect::BONUS_DAMAGE,     1,  3, true, 2, true,  false},
    {"Vicious",     AffixEffect::BONUS_DAMAGE,     2,  4, true, 5, true,  false},
    // Defense (armor only)
    {"Sturdy",      AffixEffect::BONUS_ARMOR,      1,  2, true, 1, false, true},
    {"Warded",      AffixEffect::BONUS_ARMOR,      2,  3, true, 4, false, true},
    {"Fortified",   AffixEffect::BONUS_ARMOR,      3,  4, true, 7, false, true},
    // Dodge (armor only)
    {"Nimble",      AffixEffect::BONUS_DODGE,      1,  2, true, 2, false, true},
    {"Elusive",     AffixEffect::BONUS_DODGE,      2,  3, true, 5, false, true},
    // Attribute bonuses (any equippable)
    {"Mighty",      AffixEffect::BONUS_STR,        1,  3, true, 3, false, false},
    {"Agile",       AffixEffect::BONUS_DEX,        1,  3, true, 3, false, false},
    {"Hardy",       AffixEffect::BONUS_CON,        1,  3, true, 3, false, false},
    // Speed (any equippable)
    {"Swift",       AffixEffect::BONUS_SPEED,      5, 10, true, 4, false, false},
    // HP/MP
    {"Vital",       AffixEffect::BONUS_HP,         5, 15, true, 2, false, false},
    {"Arcane",      AffixEffect::BONUS_MP,         5, 10, true, 3, false, false},
    // Resistance (armor only)
    {"Fireproof",   AffixEffect::RESIST_FIRE,      1,  2, true, 4, false, true},
};
static constexpr int PREFIX_COUNT = sizeof(PREFIX_TABLE) / sizeof(PREFIX_TABLE[0]);

// 20 suffixes
static const AffixDef SUFFIX_TABLE[] = {
    // On-kill (weapons only)
    {"of Slaughter",    AffixEffect::ONKILL_HEAL,    3,  8, false, 3, true,  false},
    {"of the Leech",    AffixEffect::ONKILL_MANA,    3,  6, false, 4, true,  false},
    // Stat bonuses (any equippable)
    {"of Strength",     AffixEffect::BONUS_STR,       1,  2, false, 2, false, false},
    {"of Dexterity",    AffixEffect::BONUS_DEX,       1,  2, false, 2, false, false},
    {"of Fortitude",    AffixEffect::BONUS_CON,       1,  2, false, 2, false, false},
    {"of the Ox",       AffixEffect::BONUS_STR,       2,  4, false, 5, false, false},
    {"of the Cat",      AffixEffect::BONUS_DEX,       2,  4, false, 5, false, false},
    {"of the Bear",     AffixEffect::BONUS_CON,       2,  4, false, 5, false, false},
    // HP/MP
    {"of Vitality",     AffixEffect::BONUS_HP,        5, 15, false, 2, false, false},
    {"of the Mind",     AffixEffect::BONUS_MP,        5, 12, false, 3, false, false},
    // Speed
    {"of Haste",        AffixEffect::BONUS_SPEED,     5, 10, false, 4, false, false},
    // Combat (weapons only)
    {"of Precision",    AffixEffect::BONUS_ATTACK,    1,  3, false, 2, true,  false},
    {"of Ruin",         AffixEffect::BONUS_DAMAGE,    2,  4, false, 5, true,  false},
    // Defense (armor only)
    {"of the Sentinel", AffixEffect::BONUS_ARMOR,     1,  3, false, 3, false, true},
    {"of Evasion",      AffixEffect::BONUS_DODGE,     1,  2, false, 3, false, true},
    {"of the Bulwark",  AffixEffect::BONUS_ARMOR,     2,  4, false, 6, false, true},
    // Elemental on-hit (weapons only)
    {"of Flame",        AffixEffect::ONHIT_BURN,     15, 25, false, 3, true,  false},
    {"of Venom",        AffixEffect::ONHIT_POISON,   15, 25, false, 2, true,  false},
    // Resistance (armor only)
    {"of Antivenom",    AffixEffect::RESIST_POISON,    1,  2, false, 3, false, true},
    // God favor
    {"of Devotion",     AffixEffect::BONUS_FAVOR,     1,  2, false, 4, false, false},
};
static constexpr int SUFFIX_COUNT = sizeof(SUFFIX_TABLE) / sizeof(SUFFIX_TABLE[0]);

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

// Unique item passive effects. Each unique has exactly one.
enum class UniqueEffect : int {
    NONE = 0,
    // Offensive
    UNDEAD_SLAYER,      // +50% damage vs undead
    BEAST_SLAYER,       // +50% damage vs animals/beasts
    BACKSTAB_BONUS,     // +100% damage from stealth (stacks with stealth bonus)
    EXECUTE_THRESHOLD,  // instant kill enemies below 15% HP
    CHAIN_LIGHTNING,    // 20% chance on hit: 4 damage to 2 nearby enemies
    // Defensive
    THORNS,             // reflect 3 damage to melee attackers
    DEATHWARD,          // survive one lethal hit per floor at 1 HP (like Last Stand)
    REGEN,              // regenerate 1 HP every 5 turns
    SPELL_ABSORB,       // 15% chance to absorb enemy spells as MP
    // Utility
    IDENTIFY_ON_PICKUP, // auto-identify items on pickup
    GOLD_FIND,          // +50% gold from all sources
    TRAP_IMMUNITY,      // immune to trap damage
    XP_BONUS,           // +25% XP from kills
    FREE_CAST,          // 20% chance spells cost no MP
    LIGHT_RADIUS,       // see 2 extra tiles in FOV
    // God-themed
    FAVOR_DOUBLED,      // double all favor gains
    CORPSE_EXPLODE,     // enemies explode on death, 3 damage in radius 2
    DREAM_WALK,         // 10% chance enemies skip their turn (Lethis)
    // Rings/amulets — gameplay-changing
    CRIT_BLEED,         // crits apply 3-turn bleed
    PRAYER_HEAL,        // heal 5 HP when you pray
    DODGE_COUNTER,      // 30% chance to counter-attack when dodging
    KILL_HASTE,         // +50 speed for 3 turns after a kill
    MP_SHIELD,          // damage taken from MP before HP (50% ratio)
    POISON_IMMUNE,      // immune to poison
    FIRE_IMMUNE,        // immune to burn
    TELEPORT_STRIKE,    // 15% chance to blink behind target on hit
    STEALTH_REGEN,      // regen 2 HP/turn while sneaking
    FEAR_AURA,          // 10% chance nearby enemies flee on your turn
    // Champion legendaries
    LIFESTEAL,          // heal 20% of melee damage dealt
    FREEZE_ON_HIT,      // melee hits apply FROZEN 1 turn
    POISON_BLEED_HIT,   // melee hits apply poison + bleed
    KILL_INVIS,         // 2 turns invisible after each kill
    FIRE_DAMAGE_BONUS,  // +5 fire damage on all melee hits
    ON_KILL_HEAL,       // kills heal 10% max HP
    COUNT
};

inline const char* unique_effect_description(UniqueEffect ue) {
    switch (ue) {
        case UniqueEffect::UNDEAD_SLAYER:      return "+50% damage vs undead";
        case UniqueEffect::BEAST_SLAYER:       return "+50% damage vs beasts";
        case UniqueEffect::BACKSTAB_BONUS:     return "Double stealth attack bonus";
        case UniqueEffect::EXECUTE_THRESHOLD:  return "Kills enemies below 15% HP";
        case UniqueEffect::CHAIN_LIGHTNING:    return "20% chance: chain lightning on hit";
        case UniqueEffect::THORNS:             return "Reflect 3 damage to melee attackers";
        case UniqueEffect::DEATHWARD:          return "Survive one lethal hit per floor";
        case UniqueEffect::REGEN:              return "Regenerate 1 HP every 5 turns";
        case UniqueEffect::SPELL_ABSORB:       return "15% chance to absorb spells as MP";
        case UniqueEffect::IDENTIFY_ON_PICKUP: return "Auto-identify items on pickup";
        case UniqueEffect::GOLD_FIND:          return "+50% gold from all sources";
        case UniqueEffect::TRAP_IMMUNITY:      return "Immune to trap damage";
        case UniqueEffect::XP_BONUS:           return "+25% XP from kills";
        case UniqueEffect::FREE_CAST:          return "20% chance: spells cost no MP";
        case UniqueEffect::LIGHT_RADIUS:       return "+2 sight range";
        case UniqueEffect::FAVOR_DOUBLED:      return "Double all favor gains";
        case UniqueEffect::CORPSE_EXPLODE:     return "Enemies explode on death (3 dmg, r2)";
        case UniqueEffect::DREAM_WALK:         return "10% chance: enemies skip turns";
        case UniqueEffect::CRIT_BLEED:         return "Critical hits apply bleed (3 turns)";
        case UniqueEffect::PRAYER_HEAL:        return "Heal 5 HP when you pray";
        case UniqueEffect::DODGE_COUNTER:      return "30% chance: counter-attack on dodge";
        case UniqueEffect::KILL_HASTE:         return "+50 speed for 3 turns after a kill";
        case UniqueEffect::MP_SHIELD:          return "Damage taken from MP first (50%)";
        case UniqueEffect::POISON_IMMUNE:      return "Immune to poison";
        case UniqueEffect::FIRE_IMMUNE:        return "Immune to burn";
        case UniqueEffect::TELEPORT_STRIKE:    return "15% chance: blink behind target on hit";
        case UniqueEffect::STEALTH_REGEN:      return "Regenerate 2 HP/turn while sneaking";
        case UniqueEffect::FEAR_AURA:          return "10% chance: nearby enemies flee";
        case UniqueEffect::LIFESTEAL:          return "Heal 20% of melee damage dealt";
        case UniqueEffect::FREEZE_ON_HIT:      return "Melee hits freeze target 1 turn";
        case UniqueEffect::POISON_BLEED_HIT:   return "Melee hits apply poison + bleed";
        case UniqueEffect::KILL_INVIS:         return "2 turns invisible after each kill";
        case UniqueEffect::FIRE_DAMAGE_BONUS:  return "+5 fire damage on all melee hits";
        case UniqueEffect::ON_KILL_HEAL:       return "Kills heal 10% max HP";
        default: return "";
    }
}

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
    bool new_pickup = false; // set on pickup, cleared when viewed in inventory

    // God relic: -1 = not a relic, 0-12 = GodId of owning god
    int relic_god = -1;

    // Unique item passive effect
    UniqueEffect unique_effect = UniqueEffect::NONE;

    // Rarity & affixes
    Rarity rarity = Rarity::COMMON;
    std::vector<Affix> affixes; // 0 = common, 1 = magic, 2 = rare

    // Summed affix bonuses (cached on generation, rebuilt on load)
    int affix_damage = 0;
    int affix_armor = 0;
    int affix_attack = 0;
    int affix_dodge = 0;
    int affix_str = 0;
    int affix_dex = 0;
    int affix_con = 0;
    int affix_hp = 0;
    int affix_mp = 0;
    int affix_speed = 0;
    int affix_favor = 0;

    // Rebuild cached stat bonuses from affixes
    void rebuild_affix_cache() {
        affix_damage = affix_armor = affix_attack = affix_dodge = 0;
        affix_str = affix_dex = affix_con = 0;
        affix_hp = affix_mp = affix_speed = affix_favor = 0;
        for (auto& a : affixes) {
            switch (a.effect) {
                case AffixEffect::BONUS_DAMAGE:  affix_damage += a.magnitude; break;
                case AffixEffect::BONUS_ARMOR:   affix_armor  += a.magnitude; break;
                case AffixEffect::BONUS_ATTACK:  affix_attack += a.magnitude; break;
                case AffixEffect::BONUS_DODGE:   affix_dodge  += a.magnitude; break;
                case AffixEffect::BONUS_STR:     affix_str    += a.magnitude; break;
                case AffixEffect::BONUS_DEX:     affix_dex    += a.magnitude; break;
                case AffixEffect::BONUS_CON:     affix_con    += a.magnitude; break;
                case AffixEffect::BONUS_HP:      affix_hp     += a.magnitude; break;
                case AffixEffect::BONUS_MP:      affix_mp     += a.magnitude; break;
                case AffixEffect::BONUS_SPEED:   affix_speed  += a.magnitude; break;
                case AffixEffect::BONUS_FAVOR:   affix_favor  += a.magnitude; break;
                default: break;
            }
        }
    }

    // Get on-hit proc chance for a given effect type (0 if none)
    int get_onhit_chance(AffixEffect type) const {
        for (auto& a : affixes) {
            if (a.effect == type) return a.magnitude;
        }
        return 0;
    }

    // Get on-kill magnitude for a given effect type (0 if none)
    int get_onkill_mag(AffixEffect type) const {
        for (auto& a : affixes) {
            if (a.effect == type) return a.magnitude;
        }
        return 0;
    }

    // Get resistance magnitude for a given effect type (0 if none)
    int get_resist(AffixEffect type) const {
        for (auto& a : affixes) {
            if (a.effect == type) return a.magnitude;
        }
        return 0;
    }

    // Display name respecting identification, material, and affixes
    std::string display_name() const {
        if (!identified && !unid_name.empty()) return unid_name;

        std::string result;

        // Prefix affix
        for (auto& a : affixes) {
            if (a.is_prefix) { result = a.name + " "; break; }
        }

        // Material
        if (material != MaterialType::NONE && material != MaterialType::IRON) {
            result += std::string(material_name(material)) + " ";
        }

        result += name;

        // Suffix affix
        for (auto& a : affixes) {
            if (!a.is_prefix) { result += " " + a.name; break; }
        }

        return result;
    }
};
