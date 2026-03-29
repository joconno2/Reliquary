#pragma once

enum class ClassId : int {
    // Base classes (always available)
    FIGHTER = 0,
    ROGUE,
    WIZARD,
    RANGER,
    // Unlockable classes
    BARBARIAN,
    KNIGHT,
    MONK,
    TEMPLAR,
    DRUID,
    WAR_CLERIC,
    WARLOCK,
    DWARF,
    ELF,
    BANDIT,
    NECROMANCER,
    SCHEMA_MONK,
    HERETIC,
    COUNT
};

constexpr int CLASS_COUNT = static_cast<int>(ClassId::COUNT);
constexpr int BASE_CLASS_COUNT = 4; // first 4 are always unlocked

struct ClassInfo {
    const char* name;
    const char* description;
    int sprite_x, sprite_y; // in rogues.png
    // Starting attributes
    int str, dex, con, intel, wil, per, cha;
    int hp, mp;
    int base_damage;
    // Unlock hint (nullptr = always available)
    const char* unlock_hint;
};

inline const ClassInfo& get_class_info(ClassId id) {
    static const ClassInfo CLASSES[] = {
        // === BASE CLASSES ===
        // FIGHTER
        {"Fighter",
         "Balanced melee combatant. Strong, tough, adaptable.",
         1, 1,
         14, 12, 13, 8, 9, 10, 10,
         35, 5, 4, nullptr},
        // ROGUE
        {"Rogue",
         "Quick and quiet. Daggers, stealth, precision.",
         3, 0,
         9, 16, 10, 10, 8, 14, 9,
         25, 5, 3, nullptr},
        // WIZARD
        {"Wizard",
         "Fragile scholar. Deep mana pool. Knows things.",
         1, 4,
         7, 8, 8, 16, 14, 12, 9,
         20, 25, 2, nullptr},
        // RANGER
        {"Ranger",
         "Eyes like a hawk. Bow, blade, and the patience to use both.",
         2, 0,
         11, 14, 11, 9, 10, 15, 8,
         28, 8, 3, nullptr},

        // === UNLOCKABLE CLASSES ===
        // BARBARIAN — kill 50 enemies total
        {"Barbarian",
         "Berserker. Rage fuels the blade. Light armor, heavy damage.",
         0, 3,  // male barbarian (row 3, col 0)
         18, 10, 16, 6, 8, 9, 7,
         40, 0, 5, "Slay 50 enemies across all runs."},
        // KNIGHT — reach depth 5 with shield
        {"Knight",
         "Heavy armor, shield, honor. The old way of war.",
         0, 1,  // knight (row 1, col 0)
         15, 8, 15, 9, 12, 10, 12,
         38, 5, 4, "Reach dungeon depth 5 with a shield equipped."},
        // MONK — kill an enemy unarmed
        {"Monk",
         "Empty hands, clear mind. Unarmed, unarmored, unstoppable.",
         0, 2,  // monk (row 2, col 0)
         10, 16, 12, 10, 14, 14, 8,
         25, 10, 5, "Kill an enemy with no weapon equipped."},
        // TEMPLAR — kill 30 undead total
        {"Templar",
         "Anti-undead specialist. Divine smite. Unwavering.",
         4, 2,  // templar (row 2, col 4)
         14, 10, 14, 8, 14, 10, 10,
         35, 10, 4, "Destroy 30 undead across all runs."},
        // DRUID — complete 10 dynamic quests
        {"Druid",
         "Nature magic, animal kinship. The forest remembers.",
         2, 4,  // druid (row 4, col 2)
         8, 10, 12, 14, 12, 14, 10,
         25, 20, 2, "Complete 10 side quests across all runs."},
        // WAR CLERIC — heal 300 HP total
        {"War Cleric",
         "Divine healing and a heavy mace. Faith tested in blood.",
         2, 2,  // female war cleric (row 2, col 2)
         13, 8, 14, 10, 16, 10, 12,
         35, 15, 3, "Heal 300 HP across all runs."},
        // WARLOCK — die deep
        {"Warlock",
         "Dark pacts. Glass cannon. Something watches over your shoulder.",
         5, 5,  // warlock (row 5, col 5)
         7, 10, 8, 18, 12, 10, 6,
         18, 30, 2, "Die on dungeon level 4 or deeper."},
        // DWARF — reach depth 6
        {"Dwarf",
         "Tough as stone. Poison resist. Sees in the dark.",
         0, 0,  // dwarf (row 0, col 0)
         14, 8, 18, 8, 12, 10, 8,
         40, 5, 3, "Descend to dungeon depth 6 or deeper."},
        // ELF — examine 50 creatures
        {"Elf",
         "Magic affinity, keen perception. Agile and ancient.",
         1, 0,  // elf (row 0, col 1)
         8, 14, 8, 14, 12, 16, 12,
         22, 18, 2, "Examine 50 creatures across all runs."},
        // BANDIT — accumulate 500 gold
        {"Bandit",
         "Dual wield, intimidation, theft. The road provides.",
         4, 0,  // bandit (row 0, col 4)
         12, 16, 10, 8, 8, 12, 10,
         28, 0, 4, "Accumulate 500 gold in a single run."},
        // NECROMANCER — cast 30 Dark Arts spells
        {"Necromancer",
         "The dead serve willingly. Feared by all. Corpse manipulation.",
         3, 4,  // desert sage (row 4, col 3)
         7, 8, 8, 18, 14, 10, 4,
         18, 28, 2, "Cast 30 Dark Arts spells across all runs."},
        // SCHEMA MONK — reach level 12 as Monk
        {"Schema Monk",
         "Elemental strikes. Ki mastery. The perfected form.",
         5, 2,  // schema monk (row 2, col 5)
         12, 18, 14, 12, 16, 16, 8,
         30, 15, 6, "Reach level 12 as Monk."},
        // HERETIC — win with all 7 gods
        {"Heretic",
         "Godless. No divine aid. No tenets. No mercy. Nobody notices.",
         0, 7,  // peasant/coalburner (row 7, col 0)
         10, 10, 10, 10, 10, 10, 10,
         25, 10, 3, "Complete the game with all 7 gods."},
    };
    return CLASSES[static_cast<int>(id)];
}
