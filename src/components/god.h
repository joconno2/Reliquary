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
};

inline const GodInfo& get_god_info(GodId id) {
    static const GodInfo GODS[] = {
        // VETHRIK — Death, bone, endings
        {"Vethrik", "the Ossuary Lord", "Death, bone, endings",
         "God of death and burial. Hates undead. Keeps the dead in the ground.",
         "+15% damage to undead. Bone weapons +2 damage.",
         0, 0, 2, 0, 0, 0, 0,  5, 0, 0,
         {160, 160, 200}},

        // THESSARKA — Knowledge, secrets, madness
        {"Thessarka", "the Eyeless", "Knowledge, secrets, madness",
         "Goddess of forbidden knowledge. Blinded herself to see further.",
         "+3 FOV. Auto-identify one item per floor.",
         0, 0, 0, 2, 0, 2, 0,  0, 5, 3,
         {140, 140, 220}},

        // MORRETH — War, iron, honor
        {"Morreth", "the Iron Father", "War, iron, honor",
         "God of war and iron. Favors those who fight and endure.",
         "+2 damage with blunt and axe weapons.",
         2, 0, 2, 0, 0, 0, 0,  5, 0, 0,
         {200, 180, 140}},

        // YASHKHET — Blood, sacrifice, pain
        {"Yashkhet", "the Wound", "Blood, sacrifice, pain",
         "God of blood sacrifice. Power through suffering.",
         "Blood magic: spells cost HP, not MP. +15% damage below 50% HP. +1 with daggers. Healing -50%.",
         0, 0, 0, 0, 2, 0, 0,  0, 5, 0,
         {200, 60, 60}},

        // KHAEL — Nature, beasts, rot
        {"Khael", "the Green Watcher", "Nature, beasts, rot",
         "God of the wild. Nature is not kind.",
         "Animals never aggro first. 25% poison resistance.",
         0, 0, 0, 0, 0, 2, 0,  0, 0, 0,
         {80, 200, 80}},

        // SOLETH — Fire, purification, zealotry
        {"Soleth", "the Pale Flame", "Fire, purification, zealotry",
         "God of fire and purification. Burns away corruption.",
         "30% fire resistance. +2 damage vs undead. Undead take 1 dmg/turn adjacent to you.",
         0, 0, 0, 0, 2, 0, 0,  0, 0, 2,
         {255, 220, 100}},

        // IXUUL — Chaos, mutation, the void
        {"Ixuul", "the Formless", "Chaos, mutation, the void",
         "God of chaos and the void. Not worshipped — contracted.",
         "Slimes and aberrations ignore you. +3 INT, -2 CHA.",
         0, 0, 0, 3, 0, 0, -2, 0, 5, 0,
         {180, 100, 255}},

        // ZHAVEK — Shadow, silence, theft
        {"Zhavek", "the Unseen", "Shadow, silence, secrets",
         "God of shadow and silence. Patron of thieves and assassins.",
         "First strike from stealth 2x damage. Enemies forget you after 3 turns. Light -2.",
         0, 2, 0, 0, 0, 2, 0,  0, 0, -2,
         {60, 60, 100}},

        // THALARA — Sea, storms, drowning
        {"Thalara", "the Drowned Queen", "Sea, storms, drowning",
         "Goddess of the sea and storms. Drowned and returned.",
         "Poison immune. Fire weakness (-20%). Molten zones deal damage. +5 HP.",
         0, 0, 0, 0, 2, 0, 0,  5, 0, 0,
         {80, 180, 200}},

        // OSSREN — Craft, forge, permanence
        {"Ossren", "the Hammer Unworn", "Craft, forge, permanence",
         "God of craft and permanence. What he makes does not break.",
         "+1 damage with all weapons. Temper prayer upgrades items permanently.",
         0, 0, 2, 0, 0, 0, 0,  0, 0, 0,
         {220, 180, 80}},

        // LETHIS — Sleep, dreams, memory
        {"Lethis", "the Dreaming Wound", "Sleep, dreams, memory",
         "God of sleep and dreams. The boundary between living and dead.",
         "Rest heals 50% (not 25%). Once per floor, survive lethal damage at 1 HP.",
         0, 0, 0, 0, 2, -2, 0,  0, 5, 0,
         {160, 120, 200}},

        // GATHRUUN — Stone, earth, depth
        {"Gathruun", "the Root Below", "Stone, earth, depth",
         "God of stone and the deep earth. Strongest underground.",
         "+3 natural armor. +2 damage underground. Tremor scales with WIL and level.",
         2, 0, 2, 0, 0, 0, 0,  5, 0, 0,
         {160, 130, 90}},

        // SYTHARA — Plague, decay, entropy
        {"Sythara", "the Pallid Mother", "Plague, decay, entropy",
         "Goddess of plague and decay. Everything rots. She helps it along.",
         "Poison immune. 15% chance to poison enemies on hit. Healing -30%.",
         0, 0, 0, 2, 0, 0, -2, 0, 0, 0,
         {120, 180, 60}},
    };
    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= GOD_COUNT) {
        static const GodInfo NONE_GOD = {
            "None", "the Faithless", "Nothing",
            "You reject the gods. No divine aid. No tenets. No mercy.",
            "No passives. No prayers. No favor. Pure self-reliance.",
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
            {128, 128, 128}
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
