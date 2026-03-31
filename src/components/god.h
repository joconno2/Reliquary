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
         "God of proper death. Despises undead. Collects bones because bones are honest.",
         "+15% damage to undead. Bone weapons +2 damage.",
         0, 0, 2, 0, 0, 0, 0,  5, 0, 0,
         {160, 160, 200}},

        // THESSARKA — Knowledge, secrets, madness
        {"Thessarka", "the Eyeless", "Knowledge, secrets, madness",
         "Tore out her own eyes to see truth. Her followers seek forbidden knowledge.",
         "+3 FOV. Auto-identify one item per floor.",
         0, 0, 0, 2, 0, 2, 0,  0, 5, 3,
         {140, 140, 220}},

        // MORRETH — War, iron, honor
        {"Morreth", "the Iron Father", "War, iron, honor",
         "The old soldier-god. Respects strength tested, not displayed.",
         "+2 damage with blunt and axe weapons.",
         2, 0, 2, 0, 0, 0, 0,  5, 0, 0,
         {200, 180, 140}},

        // YASHKHET — Blood, sacrifice, pain
        {"Yashkhet", "the Wound", "Blood, sacrifice, pain",
         "Pain is the only honest currency. Not evil, but ascetic, extreme, and unsettling.",
         "Blood magic: spells cost HP, not MP. +15% damage below 50% HP. +1 with daggers. Healing -50%.",
         0, 0, 0, 0, 2, 0, 0,  0, 5, 0,
         {200, 60, 60}},

        // KHAEL — Nature, beasts, rot
        {"Khael", "the Green Watcher", "Nature, beasts, rot",
         "The wolf eating the deer, the fungus consuming the log.",
         "Animals never aggro first. 25% poison resistance.",
         0, 0, 0, 0, 0, 2, 0,  0, 0, 0,
         {80, 200, 80}},

        // SOLETH — Fire, purification, zealotry
        {"Soleth", "the Pale Flame", "Fire, purification, zealotry",
         "The burning light that scours corruption. Searing.",
         "30% fire resistance. +2 damage vs undead. Undead take 1 dmg/turn adjacent to you.",
         0, 0, 0, 0, 2, 0, 0,  0, 0, 2,
         {255, 220, 100}},

        // IXUUL — Chaos, mutation, the void
        {"Ixuul", "the Formless", "Chaos, mutation, the void",
         "Protean. What leaks through the cracks between things.",
         "Slimes and aberrations ignore you. +3 INT, -2 CHA.",
         0, 0, 0, 3, 0, 0, -2, 0, 5, 0,
         {180, 100, 255}},

        // ZHAVEK — Shadow, silence, theft
        {"Zhavek", "the Unseen", "Shadow, silence, secrets",
         "Not darkness — absence. The space between a candle and its shadow.",
         "First strike from stealth 2x damage. Enemies forget you after 3 turns. Light -2.",
         0, 2, 0, 0, 0, 2, 0,  0, 0, -2,
         {60, 60, 100}},

        // THALARA — Sea, storms, drowning
        {"Thalara", "the Drowned Queen", "Sea, storms, drowning",
         "She walked into the sea and something walked back out.",
         "Poison immune. Fire weakness (-20%). Molten zones deal damage. +5 HP.",
         0, 0, 0, 0, 2, 0, 0,  5, 0, 0,
         {80, 180, 200}},

        // OSSREN — Craft, forge, permanence
        {"Ossren", "the Hammer Unworn", "Craft, forge, permanence",
         "Not a smith-god — a god of things that endure.",
         "+1 damage with all weapons. Temper prayer upgrades items permanently.",
         0, 0, 2, 0, 0, 0, 0,  0, 0, 0,
         {220, 180, 80}},

        // LETHIS — Sleep, dreams, memory
        {"Lethis", "the Dreaming Wound", "Sleep, dreams, memory",
         "What you see when you close your eyes and something sees you back.",
         "Rest heals 50% (not 25%). Once per floor, survive lethal damage at 1 HP.",
         0, 0, 0, 0, 2, -2, 0,  0, 5, 0,
         {160, 120, 200}},

        // GATHRUUN — Stone, earth, depth
        {"Gathruun", "the Root Below", "Stone, earth, depth",
         "The pressure that makes diamonds. The deeper you go, the closer you are.",
         "+3 natural armor. +2 damage underground. Tremor scales with WIL and level.",
         2, 0, 2, 0, 0, 0, 0,  5, 0, 0,
         {160, 130, 90}},

        // SYTHARA — Plague, decay, entropy
        {"Sythara", "the Pallid Mother", "Plague, decay, entropy",
         "Her love is mold on bread, moss on stone, the slow softening of all hard things.",
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
