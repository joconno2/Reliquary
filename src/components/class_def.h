#pragma once

enum class ClassId : int {
    FIGHTER = 0,
    ROGUE,
    WIZARD,
    RANGER,
    COUNT
};

constexpr int CLASS_COUNT = static_cast<int>(ClassId::COUNT);

struct ClassInfo {
    const char* name;
    const char* description;
    int sprite_x, sprite_y; // in rogues.png
    // Starting attributes
    int str, dex, con, intel, wil, per, cha;
    int hp, mp;
    int base_damage;
};

inline const ClassInfo& get_class_info(ClassId id) {
    static const ClassInfo CLASSES[] = {
        // FIGHTER
        {"Fighter",
         "Balanced melee combatant. Strong, tough, adaptable.",
         1, 1, // male fighter sprite
         14, 12, 13, 8, 9, 10, 10,
         35, 5, 4},
        // ROGUE
        {"Rogue",
         "Quick and quiet. Daggers, stealth, precision.",
         3, 0, // rogue sprite
         9, 16, 10, 10, 8, 14, 9,
         25, 5, 3},
        // WIZARD
        {"Wizard",
         "Fragile scholar. Deep mana pool. Knows things.",
         1, 4, // male wizard sprite
         7, 8, 8, 16, 14, 12, 9,
         20, 25, 2},
        // RANGER
        {"Ranger",
         "Eyes like a hawk. Bow, blade, and the patience to use both.",
         2, 0, // ranger sprite
         11, 14, 11, 9, 10, 15, 8,
         28, 8, 3},
    };
    return CLASSES[static_cast<int>(id)];
}
