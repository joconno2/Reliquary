#pragma once
#include <string>
#include <cstdint>

enum class GodId : int {
    NONE = -1, // heretic / godless
    VETHRIK = 0,
    THESSARKA,
    MORRETH,
    YASHKHET,
    KHAEL,
    SOLETH,
    IXUUL,
    ZHAVEK,
    THALARA,
    OSSREN,
    LETHIS,
    GATHRUUN,
    SYTHARA,
    COUNT
};

constexpr int GOD_COUNT = static_cast<int>(GodId::COUNT);

struct GodColor {
    uint8_t r, g, b;
};

struct GodInfo {
    const char* name;
    const char* title;
    const char* domain;
    const char* description;   // short flavor
    const char* passive_desc;  // what the passive actually does mechanically
    // Passive stat bonuses
    int str_bonus, dex_bonus, con_bonus, int_bonus, wil_bonus, per_bonus, cha_bonus;
    int bonus_hp, bonus_mp;
    int fov_bonus;
    // God color for particles, UI tint
    GodColor color;
    // Favor asymmetry
    int regen_rate;         // turns between +1 favor regen (higher = slower)
    int violation_mult_pct; // 100 = normal, 150 = 50% harsher violations
};

inline const GodInfo& get_god_info(GodId id) {
    static const GodInfo GODS[] = {
        // VETHRIK — Death, bone, endings
        {"Vethrik", "the Ossuary Lord", "Death, bone, endings",
         "God of death and burial. Hates undead. Keeps the dead in the ground.",
         "Undead ignore you. Bone weapons deal 2x. Living creatures deal +30% to you. No metal armor.",
         0, 0, 0, 0, 2, 0, 0,  5, 0, 0,
         {160, 160, 200}, 50, 100},

        // THESSARKA — Knowledge, secrets, madness
        {"Thessarka", "the Eyeless", "Knowledge, secrets, madness",
         "Goddess of forbidden knowledge. Blinded herself to see further.",
         "Auto-map floors. Identify on pickup. FOV 2 in combat. -4 STR.",
         0, 0, 0, 4, 0, 0, 0,  0, 10, 0,
         {140, 140, 220}, 40, 80},

        // MORRETH — War, iron, honor
        {"Morreth", "the Iron Father", "War, iron, honor",
         "God of war and iron. Favors those who fight and endure.",
         "First hit each fight = 2x. +3 armor. Speed 60 near enemies. No ranged.",
         2, 0, 2, 0, 0, 0, 0,  10, 0, 0,
         {200, 180, 140}, 50, 150},

        // YASHKHET — Blood, sacrifice, pain
        {"Yashkhet", "the Wound", "Blood, sacrifice, pain",
         "God of blood sacrifice. Power through suffering.",
         "Damage dealt heals you 15%. Below 25% HP: +50% damage. CANNOT heal from rest/potions/spells.",
         0, 0, 2, 0, 2, 0, 0,  0, 5, 0,
         {200, 60, 60}, 30, 120},

        // KHAEL — Nature, beasts, rot
        {"Khael", "the Green Watcher", "Nature, beasts, rot",
         "God of the wild. Nature is not kind.",
         "All animals fight for you. Regen 1 HP/5 turns on surface. -4 damage in dungeons. No metal weapons.",
         0, 0, 2, 0, 0, 2, 0,  0, 0, 0,
         {80, 200, 80}, 50, 100},

        // SOLETH — Fire, purification, zealotry
        {"Soleth", "the Pale Flame", "Fire, purification, zealotry",
         "God of fire and purification. Burns away corruption.",
         "All attacks +3 fire. Undead take 2x from you. Fire hurts you 2x. 2 dmg/turn in darkness.",
         0, 0, 0, 0, 2, 0, 0,  5, 0, 2,
         {255, 220, 100}, 60, 130},

        // IXUUL — Chaos, mutation, the void
        {"Ixuul", "the Formless", "Chaos, mutation, the void",
         "God of chaos and the void. Not worshipped, contracted.",
         "Immune to all status effects. +1 random stat per 50 turns. -1 random stat per 80 turns. No shrines. Shops 2x.",
         0, 0, 0, 3, 0, 0, 0, 0, 5, 0,
         {180, 100, 255}, 25, 70},

        // ZHAVEK — Shadow, silence, theft
        {"Zhavek", "the Unseen", "Shadow, silence, secrets",
         "God of shadow and silence. Patron of thieves and assassins.",
         "Invisible until you attack. Backstabs deal 3x. Heavy armor = excommunication. -30% max HP.",
         0, 3, 0, 0, 0, 2, 0,  -10, 0, 0,
         {60, 60, 100}, 40, 100},

        // THALARA — Sea, storms, drowning
        {"Thalara", "the Drowned Queen", "Sea, storms, drowning",
         "Goddess of the sea and storms. Drowned and returned.",
         "Immune to poison+freeze. +20 speed. Fire deals 2x. Molten zones 3/turn. Dry rest heals nothing.",
         0, 0, 0, 0, 2, 0, 0,  5, 0, 0,
         {80, 180, 200}, 50, 100},

        // OSSREN — Craft, forge, permanence
        {"Ossren", "the Hammer Unworn", "Craft, forge, permanence",
         "God of craft and permanence. What he makes does not break.",
         "All gear +1 damage +1 armor. Can never sell or drop equipment. -2 speed per equipped slot.",
         0, 0, 2, 0, 0, 0, 0,  0, 0, 0,
         {220, 180, 80}, 45, 80},

        // LETHIS — Sleep, dreams, memory
        {"Lethis", "the Dreaming Wound", "Sleep, dreams, memory",
         "God of sleep and dreams. The boundary between living and dead.",
         "Survive lethal 1/floor. Rest fully heals. PER halved. Unhit enemies forget you.",
         0, 0, 0, 0, 2, -4, 0,  0, 5, 0,
         {160, 120, 200}, 35, 100},

        // GATHRUUN — Stone, earth, depth
        {"Gathruun", "the Root Below", "Stone, earth, depth",
         "God of stone and the deep earth. Strongest underground.",
         "+5 armor. +4 damage underground. Crit = earthquake. Surface costs 2x energy. -3 dmg on surface.",
         2, 0, 3, 0, 0, 0, 0,  10, 0, 0,
         {160, 130, 90}, 60, 120},

        // SYTHARA — Plague, decay, entropy
        {"Sythara", "the Pallid Mother", "Plague, decay, entropy",
         "Goddess of plague and decay. Everything rots. She helps it along.",
         "All attacks poison. Immune to disease+poison. ALL healing halved. Towns charge 3x.",
         0, 0, 0, 2, 0, 0, 0, 0, 0, 0,
         {120, 180, 60}, 30, 80},
    };
    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= GOD_COUNT) {
        static const GodInfo NONE_GOD = {
            "None", "the Faithless", "Nothing",
            "You reject the gods. No divine aid. No tenets. No mercy.",
            "No passives. No prayers. No favor. Pure self-reliance.",
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            {128, 128, 128}, 0, 100
        };
        return NONE_GOD;
    }
    return GODS[idx];
}

// Player's divine state
struct GodAlignment {
    GodId god = GodId::NONE;
    int favor = 0; // -100 to +100
    bool lethal_save_used = false;  // Lethis once-per-floor lethal save
    int items_identified_floor = 0; // Thessarka auto-ID tracking
    bool dig_used_floor = false;    // Gathruun dig tracking
};
