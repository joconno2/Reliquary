#pragma once
#include "components/god.h"
#include <cstdint>

// ---- Player action flags (set during turn, cleared at start of next) ----

struct PlayerActions {
    bool killed_animal = false;
    bool killed_undead = false;
    bool killed_sleeping = false;
    bool used_dark_arts = false;
    bool used_fire_magic = false;
    bool used_healing_magic = false;
    bool used_poison = false;
    bool used_stealth_attack = false;  // backstab / attacked from invis
    bool fled_combat = false;          // moved away from adjacent enemy
    bool wore_heavy_armor = false;     // has plate equipped
    bool healed_above_75pct = false;   // used heal that put HP above 75%
    bool destroyed_book = false;       // failed spellbook study
    bool rested = false;
    bool rested_on_surface = false;    // rested while dungeon_level == 0
    bool descended = false;            // went down stairs
    bool dug_wall = false;
    bool killed_anything = false;
    bool picked_up_sacred = false;
    bool equipped_profane = false;
    bool carrying_cursed = false;      // has any cursed item in inventory
    bool used_light_source = false;    // carrying torch / light

    void clear() { *this = {}; }
};

// ---- Tenet definitions ----

// What kind of condition does this tenet check?
enum class TenetCheck : int {
    // Violations (doing something forbidden)
    NEVER_KILL_ANIMAL,        // Khael
    NEVER_USE_DARK_ARTS,      // Soleth, Vethrik
    NEVER_USE_FIRE_MAGIC,     // Khael, Thalara
    NEVER_USE_POISON,         // Morreth
    NEVER_BACKSTAB,           // Morreth
    NEVER_FLEE_COMBAT,        // Morreth, Yashkhet
    NEVER_USE_HEALING_MAGIC,  // Sythara
    NEVER_WEAR_HEAVY_ARMOR,   // Zhavek
    NEVER_CARRY_LIGHT,        // Zhavek
    NEVER_DESTROY_BOOK,       // Thessarka
    NEVER_KILL_SLEEPING,      // Lethis
    NEVER_DIG_WALLS,          // Khael
    NEVER_REST_ON_SURFACE,    // Gathruun, Thalara
    NEVER_HEAL_ABOVE_75,      // Yashkhet

    // Requirements (must do something)
    MUST_DESCEND,             // Gathruun (never backtrack without clearing)
    MUST_KILL_UNDEAD,         // Soleth (never spare undead in sight)
    MUST_REST_EACH_FLOOR,     // Lethis
};

struct TenetDef {
    TenetCheck check;
    const char* description;   // shown to player
    int violation_favor;       // favor change on violation (negative)
};

struct GodTenets {
    const TenetDef* tenets;
    int count;
};

// ---- Per-god tenet tables ----

// VETHRIK — Death, bone, endings
static const TenetDef TENETS_VETHRIK[] = {
    {TenetCheck::NEVER_USE_DARK_ARTS, "Never raise the dead or practice Dark Arts.", -5},
    {TenetCheck::NEVER_FLEE_COMBAT,   "Never flee from undead.", -3},
};

// THESSARKA — Knowledge, secrets, madness
static const TenetDef TENETS_THESSARKA[] = {
    {TenetCheck::NEVER_DESTROY_BOOK,  "Never destroy a book or scroll.", -5},
    {TenetCheck::MUST_DESCEND,        "Seek the deepest levels.", -2},
};

// MORRETH — War, iron, honor
static const TenetDef TENETS_MORRETH[] = {
    {TenetCheck::NEVER_USE_POISON,    "Never use poison.", -4},
    {TenetCheck::NEVER_BACKSTAB,      "Never strike from stealth.", -4},
    {TenetCheck::NEVER_FLEE_COMBAT,   "Never flee a fight you started.", -3},
    {TenetCheck::NEVER_USE_DARK_ARTS, "Never use offensive magic.", -3},
};

// YASHKHET — Blood, sacrifice, pain
static const TenetDef TENETS_YASHKHET[] = {
    {TenetCheck::NEVER_HEAL_ABOVE_75, "Never heal above 75% HP voluntarily.", -4},
    {TenetCheck::NEVER_FLEE_COMBAT,   "Never flee.", -3},
};

// KHAEL — Nature, beasts, rot
static const TenetDef TENETS_KHAEL[] = {
    {TenetCheck::NEVER_KILL_ANIMAL,   "Never kill an unprovoked animal.", -5},
    {TenetCheck::NEVER_DIG_WALLS,     "Never mine or dig walls.", -3},
    {TenetCheck::NEVER_USE_FIRE_MAGIC,"Never use fire magic.", -3},
};

// SOLETH — Fire, purification, zealotry
static const TenetDef TENETS_SOLETH[] = {
    {TenetCheck::NEVER_USE_DARK_ARTS, "Never use dark magic.", -5},
    {TenetCheck::MUST_KILL_UNDEAD,    "Never spare a demon or undead.", -3},
};

// IXUUL — Chaos, mutation, the void
static const TenetDef TENETS_IXUUL[] = {
    // Ixuul's tenets are strange — hard to track mechanically
    // "Never refuse a mutation" and "all 4 armor slots must be different material"
    // We'll implement what's trackable
    {TenetCheck::NEVER_USE_HEALING_MAGIC, "Reject order. Never use Healing magic.", -3},
};

// ZHAVEK — Shadow, silence, secrets
static const TenetDef TENETS_ZHAVEK[] = {
    {TenetCheck::NEVER_WEAR_HEAVY_ARMOR, "Never wear heavy armor.", -4},
    {TenetCheck::NEVER_CARRY_LIGHT,      "Never carry a torch or light source.", -3},
};

// THALARA — Sea, storms, drowning
static const TenetDef TENETS_THALARA[] = {
    {TenetCheck::NEVER_USE_FIRE_MAGIC,   "Never use fire magic.", -4},
    {TenetCheck::NEVER_REST_ON_SURFACE,  "Never rest on dry land.", -2},
};

// OSSREN — Craft, forge, permanence
static const TenetDef TENETS_OSSREN[] = {
    // "Never discard a weapon or armor" — hard to track
    // "Never use consumables in combat" — would need combat-state tracking
    // Simplified:
    {TenetCheck::NEVER_DESTROY_BOOK,     "Never destroy a crafted thing.", -3},
};

// LETHIS — Sleep, dreams, memory
static const TenetDef TENETS_LETHIS[] = {
    {TenetCheck::NEVER_KILL_SLEEPING,    "Never kill a sleeping creature.", -5},
    {TenetCheck::MUST_REST_EACH_FLOOR,   "Rest at least once per floor.", -2},
};

// GATHRUUN — Stone, earth, depth
static const TenetDef TENETS_GATHRUUN[] = {
    {TenetCheck::MUST_DESCEND,           "Always descend when stairs are available.", -3},
    {TenetCheck::NEVER_REST_ON_SURFACE,  "Never rest on the surface.", -2},
};

// SYTHARA — Plague, decay, entropy
static const TenetDef TENETS_SYTHARA[] = {
    {TenetCheck::NEVER_USE_HEALING_MAGIC,"Never use Healing magic.", -4},
};

// ---- Lookup function ----

inline GodTenets get_god_tenets(GodId god) {
    switch (god) {
        case GodId::VETHRIK:  return {TENETS_VETHRIK,  2};
        case GodId::THESSARKA:return {TENETS_THESSARKA, 2};
        case GodId::MORRETH:  return {TENETS_MORRETH,  4};
        case GodId::YASHKHET: return {TENETS_YASHKHET, 2};
        case GodId::KHAEL:    return {TENETS_KHAEL,    3};
        case GodId::SOLETH:   return {TENETS_SOLETH,   2};
        case GodId::IXUUL:    return {TENETS_IXUUL,    1};
        case GodId::ZHAVEK:   return {TENETS_ZHAVEK,   2};
        case GodId::THALARA:  return {TENETS_THALARA,  2};
        case GodId::OSSREN:   return {TENETS_OSSREN,   1};
        case GodId::LETHIS:   return {TENETS_LETHIS,   2};
        case GodId::GATHRUUN: return {TENETS_GATHRUUN, 2};
        case GodId::SYTHARA:  return {TENETS_SYTHARA,  1};
        default: return {nullptr, 0};
    }
}

// ---- Sacred/Profane item tags (bitmask) ----

enum ItemTag : uint32_t {
    TAG_NONE         = 0,
    TAG_DAGGER       = 1 << 0,
    TAG_BLUNT        = 1 << 1,   // hammers, maces, clubs
    TAG_AXE          = 1 << 2,
    TAG_SWORD        = 1 << 3,
    TAG_BOW          = 1 << 4,
    TAG_HEAVY_ARMOR  = 1 << 5,   // plate
    TAG_MEDIUM_ARMOR = 1 << 6,   // chain, scale
    TAG_LIGHT_ARMOR  = 1 << 7,   // leather, cloth
    TAG_SHIELD       = 1 << 8,
    TAG_BOOK         = 1 << 9,   // spellbooks, scrolls, tomes
    TAG_HERB         = 1 << 10,  // herbs, mushrooms, plants
    TAG_TORCH        = 1 << 11,  // torches, light sources
    TAG_HOLY         = 1 << 12,  // holy symbols, blessed items
    TAG_DARK         = 1 << 13,  // dark arts items, necromantic
    TAG_POTION       = 1 << 14,
    TAG_FOOD_COOKED  = 1 << 15,
    TAG_FOOD_RAW     = 1 << 16,
    TAG_BONE_ITEM    = 1 << 17,  // bone weapons/items
    TAG_FIRE_ITEM    = 1 << 18,  // fire weapons, braziers
    TAG_WATER_ITEM   = 1 << 19,  // trident, net, coral
    TAG_CRAFT_ITEM   = 1 << 20,  // hammers as tools, anvils
    TAG_POISON_ITEM  = 1 << 21,  // poison vials, toxic items
    TAG_GEM          = 1 << 22,
    TAG_MUSHROOM     = 1 << 23,
};

struct SacredProfane {
    uint32_t sacred;   // item tags considered sacred
    uint32_t profane;  // item tags considered profane
};

inline SacredProfane get_sacred_profane(GodId god) {
    switch (god) {
        case GodId::VETHRIK:
            return {TAG_BONE_ITEM,
                    TAG_DARK};
        case GodId::THESSARKA:
            return {TAG_BOOK,
                    TAG_NONE}; // profane is willful ignorance, not items
        case GodId::MORRETH:
            return {TAG_BLUNT | TAG_AXE | TAG_HEAVY_ARMOR | TAG_SHIELD,
                    TAG_DAGGER | TAG_POISON_ITEM | TAG_DARK};
        case GodId::YASHKHET:
            return {TAG_DAGGER,
                    TAG_NONE}; // profane is fleeing/healing, not items
        case GodId::KHAEL:
            return {TAG_HERB | TAG_MUSHROOM | TAG_FOOD_RAW,
                    TAG_FOOD_COOKED | TAG_HEAVY_ARMOR | TAG_FIRE_ITEM};
        case GodId::SOLETH:
            return {TAG_TORCH | TAG_HOLY | TAG_FIRE_ITEM,
                    TAG_DARK};
        case GodId::IXUUL:
            return {TAG_NONE, // only change matters
                    TAG_HOLY};
        case GodId::ZHAVEK:
            return {TAG_DAGGER | TAG_LIGHT_ARMOR,
                    TAG_TORCH | TAG_HEAVY_ARMOR | TAG_BLUNT};
        case GodId::THALARA:
            return {TAG_WATER_ITEM,
                    TAG_FIRE_ITEM | TAG_TORCH};
        case GodId::OSSREN:
            return {TAG_BLUNT | TAG_CRAFT_ITEM | TAG_HEAVY_ARMOR,
                    TAG_NONE}; // profane is broken/crude items, not tagged
        case GodId::LETHIS:
            return {TAG_NONE,
                    TAG_NONE}; // Lethis doesn't care about items
        case GodId::GATHRUUN:
            return {TAG_BLUNT | TAG_GEM,
                    TAG_NONE};
        case GodId::SYTHARA:
            return {TAG_MUSHROOM | TAG_POISON_ITEM | TAG_HERB,
                    TAG_HOLY | TAG_POTION}; // healing potions are profane
        default:
            return {TAG_NONE, TAG_NONE};
    }
}
