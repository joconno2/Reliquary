#pragma once
#include <string>

enum class GodId : int {
    NONE = -1, // heretic / godless
    VETHRIK = 0,
    THESSARKA,
    MORRETH,
    YASHKHET,
    KHAEL,
    SOLETH,
    IXUUL,
    COUNT
};

constexpr int GOD_COUNT = static_cast<int>(GodId::COUNT);

struct GodInfo {
    const char* name;
    const char* title;
    const char* domain;
    const char* description;  // short flavor
    // Passive bonuses
    int str_bonus, dex_bonus, con_bonus, int_bonus, wil_bonus, per_bonus, cha_bonus;
    int bonus_hp, bonus_mp;
    int fov_bonus;
};

inline const GodInfo& get_god_info(GodId id) {
    static const GodInfo GODS[] = {
        // VETHRIK
        {"Vethrik", "the Ossuary Lord", "Death, bone, endings",
         "God of proper death. Despises undead. Collects bones because bones are honest.",
         0, 0, 2, 0, 0, 0, 0,  5, 0, 0},
        // THESSARKA
        {"Thessarka", "the Eyeless", "Knowledge, secrets, madness",
         "Tore out her own eyes to see truth. Her followers seek forbidden knowledge.",
         0, 0, 0, 2, 0, 2, 0,  0, 5, 3},
        // MORRETH
        {"Morreth", "the Iron Father", "War, iron, honor",
         "The old soldier-god. Respects strength tested, not displayed.",
         2, 0, 2, 0, 0, 0, 0,  5, 0, 0},
        // YASHKHET
        {"Yashkhet", "the Wound", "Blood, sacrifice, pain",
         "Pain is the only honest currency. Not evil, but ascetic, extreme, and unsettling.",
         0, 0, 0, 0, 2, 0, 0,  0, 5, 0},
        // KHAEL
        {"Khael", "the Green Watcher", "Nature, beasts, rot",
         "The wolf eating the deer, the fungus consuming the log.",
         0, 0, 0, 0, 0, 2, 0,  0, 0, 0},
        // SOLETH
        {"Soleth", "the Pale Flame", "Fire, purification, zealotry",
         "The burning light that scours corruption. Searing.",
         0, 0, 0, 0, 2, 0, 0,  0, 0, 2},
        // IXUUL
        {"Ixuul", "the Formless", "Chaos, mutation, the void",
         "Protean. What leaks through the cracks between things.",
         0, 0, 0, 3, 0, 0, -2, 0, 5, 0},
    };
    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= GOD_COUNT) {
        static const GodInfo NONE_GOD = {
            "None", "the Faithless", "Nothing",
            "You reject the gods. No divine aid. No tenets. No mercy.",
            0, 0, 0, 0, 0, 0, 0, 0, 0, 0
        };
        return NONE_GOD;
    }
    return GODS[idx];
}

// Player's divine state
struct GodAlignment {
    GodId god = GodId::NONE;
    int favor = 0; // -100 to +100
};
