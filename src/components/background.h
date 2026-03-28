#pragma once
#include <string>

enum class BackgroundId : int {
    GRAVEDIGGER = 0,
    ALCHEMISTS_APPRENTICE,
    DESERTER,
    NOBLE_EXILE,
    PIT_FIGHTER,
    HEDGE_WITCH,
    TOMB_ROBBER,
    HERETIC_PRIEST,
    SHIPWRECK_SURVIVOR,
    RATCATCHER,
    BLACKSMITHS_CHILD,
    DISGRACED_SCHOLAR,
    PLAGUE_DOCTOR,
    EXECUTIONER,
    FARMER,
    COUNT
};

constexpr int BACKGROUND_COUNT = static_cast<int>(BackgroundId::COUNT);

struct BackgroundInfo {
    const char* name;
    const char* description;
    const char* passive_name;
    const char* passive_desc;
    // Attribute bonuses
    int str_bonus, dex_bonus, con_bonus, int_bonus, wil_bonus, per_bonus, cha_bonus;
    // Vital bonuses
    int bonus_hp, bonus_damage;
};

inline const BackgroundInfo& get_background_info(BackgroundId id) {
    static const BackgroundInfo BACKGROUNDS[] = {
        // GRAVEDIGGER
        {"Gravedigger",
         "You buried the dead for coin. You know what lies beneath.",
         "Grave Sense",
         "You can sense undead creatures within a short range.",
         1, 0, 1, 0, 0, 0, -1,
         0, 0},
        // ALCHEMIST'S APPRENTICE
        {"Alchemist's Apprentice",
         "You mixed reagents in a cramped cellar until something exploded.",
         "Tincture Lore",
         "Potions are always identified on sight.",
         0, 0, 0, 2, 0, 0, 0,
         0, 0},
        // DESERTER
        {"Deserter",
         "You dropped your sword and ran. The army still wants you.",
         "Battlefield Training",
         "You start with knowledge of basic weapon techniques.",
         1, 1, 0, 0, 0, 0, -1,
         0, 1},
        // NOBLE EXILE
        {"Noble Exile",
         "Your family name is ash. Your debts remain.",
         "Silver Tongue",
         "Charisma checks occasionally yield extra gold from enemies.",
         0, 0, 0, 0, 0, 0, 2,
         0, 0},
        // PIT FIGHTER
        {"Pit Fighter",
         "You fought for coin and crowd. Scars tell the rest.",
         "Pit Hardened",
         "You take 1 less damage from unarmed and natural attacks.",
         2, 0, 1, 0, 0, 0, -1,
         2, 1},
        // HEDGE WITCH
        {"Hedge Witch",
         "Roots, bones, and whispered bargains. You made do.",
         "Hedge Craft",
         "You begin with one random potion already identified.",
         0, 0, 0, 1, 2, 0, 0,
         0, 0},
        // TOMB ROBBER
        {"Tomb Robber",
         "Ancient curses are an occupational hazard you've mostly survived.",
         "Trap Intuition",
         "You receive a warning when moving adjacent to a known trap type.",
         0, 2, 0, 0, 0, 1, 0,
         0, 0},
        // HERETIC PRIEST
        {"Heretic Priest",
         "You read the wrong prayers. The congregation noticed.",
         "Tainted Devotion",
         "Divine favor costs less to earn but is also lost faster.",
         0, 0, 0, 1, 1, 0, 0,
         0, 0},
        // SHIPWRECK SURVIVOR
        {"Shipwreck Survivor",
         "You held on while everyone else let go.",
         "Survivor's Grit",
         "When reduced below 25% HP, gain a small bonus to all saves.",
         0, 0, 2, 0, 1, 0, 0,
         2, 0},
        // RATCATCHER
        {"Ratcatcher",
         "Sewers, cellars, and dark places. You know the smell of danger.",
         "Vermin Sense",
         "You are never surprised by rats, insects, or small creatures.",
         0, 1, 0, 0, 0, 2, 0,
         0, 0},
        // BLACKSMITH'S CHILD
        {"Blacksmith's Child",
         "You grew up hauling iron and breathing smoke.",
         "Iron Knuckles",
         "Your unarmed attacks deal +1 damage.",
         2, 0, 1, 0, 0, 0, 0,
         0, 1},
        // DISGRACED SCHOLAR
        {"Disgraced Scholar",
         "You know things you weren't supposed to publish.",
         "Forbidden Index",
         "Scrolls are identified at half the normal cost.",
         0, 0, 0, 2, 0, 1, 0,
         0, 0},
        // PLAGUE DOCTOR
        {"Plague Doctor",
         "You treated the sick behind a beaked mask. Most survived.",
         "Miasma Resistance",
         "You are immune to disease and have resistance to poison.",
         0, 0, 1, 1, 0, 0, 0,
         0, 0},
        // EXECUTIONER
        {"Executioner",
         "Clean work. Necessary work. Nobody thanked you for it.",
         "Clean Strike",
         "Once per fight, a killing blow restores a small amount of HP.",
         2, 0, 0, 0, 1, 0, -2,
         0, 1},
        // FARMER
        {"Farmer",
         "You know how to work until dark and rise before dawn.",
         "Hardy Stock",
         "You regenerate 1 extra HP per rest and begin with +3 max HP.",
         1, 0, 2, 0, 0, 0, 0,
         3, 0},
    };
    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= BACKGROUND_COUNT) {
        static const BackgroundInfo NONE_BG = {
            "None", "No background.", "None", "No passive.", 0,0,0,0,0,0,0,0,0
        };
        return NONE_BG;
    }
    return BACKGROUNDS[idx];
}
