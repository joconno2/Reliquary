#pragma once
#include "components/god.h"

struct PrayerInfo {
    const char* name;
    const char* description;
    int favor_cost; // 0 = free (may have other costs like HP)
};

// Two prayers per god. Returns nullptr for NONE/invalid.
inline const PrayerInfo* get_prayers(GodId god) {
    static const PrayerInfo PRAYERS[][2] = {
        // VETHRIK (Death) — healing from death, strike from beyond
        {{"Lay to Rest",    "Heal 5 + WIL/2 HP.",                             10},
         {"Death's Grasp",  "Deal 2 x WIL damage to nearest visible enemy.",  20}},
        // THESSARKA (Knowledge) — restore mind, reveal all
        {{"Clarity",        "Restore all MP.",                                 10},
         {"True Sight",     "Reveal the entire level.",                        20}},
        // MORRETH (War) — heal through grit, damage adjacent
        {{"Iron Resolve",   "Heal 10 + STR/2 HP.",                            10},
         {"War Cry",        "Deal STR damage to all adjacent enemies.",        20}},
        // YASHKHET (Blood) — sacrifice HP for MP, sacrifice HP for damage
        {{"Blood Offering", "Sacrifice 25% max HP, restore equal MP. Free, gain +5 favor.", 0},
         {"Sanguine Burst", "Sacrifice 10 HP, deal 10 + WIL damage to nearest enemy.",     15}},
        // KHAEL (Nature) — gentle heal, strike from nature
        {{"Regrowth",       "Heal 30% of max HP.",                             10},
         {"Nature's Wrath", "Deal WIL + 10 damage to nearest visible enemy.",  20}},
        // SOLETH (Fire) — burn adjacent, full heal
        {{"Purifying Flame","Deal WIL + 5 fire damage to all adjacent enemies.", 10},
         {"Holy Light",     "Fully restore HP.",                                 25}},
        // IXUUL (Chaos) — teleport, random damage
        {{"Warp",           "Teleport to a random location on this level.",     10},
         {"Chaos Surge",    "Deal 5-35 damage to a random visible enemy.",      20}},
        // ZHAVEK (Shadow) — vanish, silence
        {{"Vanish",         "Become invisible for 8 turns or until you attack.", 10},
         {"Silence",        "All enemies in FOV lose track of you.",             15}},
        // THALARA (Sea) — pull enemies, drown
        {{"Riptide",        "Pull all visible enemies 3 tiles toward you.",     10},
         {"Drown",          "Target nearest enemy, deal WIL damage/turn for 5 turns.", 20}},
        // OSSREN (Craft) — upgrade item, harden armor
        {{"Temper",         "Permanently upgrade one equipped item +1 quality.", 20},
         {"Unyielding",     "Double armor value for 15 turns.",                 10}},
        // LETHIS (Dreams) — put enemies to sleep, make enemy forget you
        {{"Sleepwalk",      "All visible enemies fall asleep for 5 turns.",     10},
         {"Forget",         "Target enemy permanently forgets you exist.",       15}},
        // GATHRUUN (Stone) — earthquake, stone skin
        {{"Tremor",         "Earthquake: WIL/2 + level damage to all enemies, stun adjacent.", 15},
         {"Stone Skin",     "+10 armor for 20 turns. Cannot move.",             10}},
        // SYTHARA (Plague) — poison cloud, corrode armor
        {{"Miasma",         "Poison cloud in FOV for 10 turns. Damages all non-you.", 10},
         {"Unravel",        "Target enemy loses 50% of current armor permanently.",    15}},
    };
    int idx = static_cast<int>(god);
    if (idx < 0 || idx >= GOD_COUNT) return nullptr;
    return PRAYERS[idx];
}

// Check if a monster name is undead (for Vethrik/Soleth favor bonus)
inline bool is_undead(const char* name) {
    const char* undead[] = {"skeleton", "zombie", "ghoul", "lich", "death knight",
                            "wight", "wraith", "revenant"};
    for (auto u : undead) {
        const char* p = name;
        while (*p) {
            const char* a = p;
            const char* b = u;
            while (*a && *b && (*a == *b || (*a >= 'A' && *a <= 'Z' && *a + 32 == *b))) {
                a++; b++;
            }
            if (!*b) return true;
            p++;
        }
    }
    return false;
}

// Check if a monster name is an animal (for Khael favor penalty)
inline bool is_animal(const char* name) {
    const char* animals[] = {"rat", "wolf", "boar", "spider", "bear", "warg", "bat",
                             "snake", "dire wolf"};
    for (auto a : animals) {
        const char* p = name;
        while (*p) {
            const char* x = p;
            const char* y = a;
            while (*x && *y && (*x == *y || (*x >= 'A' && *x <= 'Z' && *x + 32 == *y))) {
                x++; y++;
            }
            if (!*y) return true;
            p++;
        }
    }
    return false;
}

// Check if a monster is a slime/aberration (for Ixuul neutrality)
inline bool is_slime(const char* name) {
    const char* slimes[] = {"slime", "ooze", "writhing mass", "jelly", "aberration"};
    for (auto s : slimes) {
        const char* p = name;
        while (*p) {
            const char* a = p;
            const char* b = s;
            while (*a && *b && (*a == *b || (*a >= 'A' && *a <= 'Z' && *a + 32 == *b))) {
                a++; b++;
            }
            if (!*b) return true;
            p++;
        }
    }
    return false;
}
