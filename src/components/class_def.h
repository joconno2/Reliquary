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
    // Monster-themed unlockable classes
    WYRMKIN,      // dragon-inspired: fire resist, high CON
    REVENANT,     // undead-inspired: self-heal on kill, high WIL
    SERPENTINE,   // naga-inspired: poison attacks, high DEX
    TROLLBLOOD,   // troll-inspired: regeneration, massive HP, slow
    COUNT
};

constexpr int CLASS_COUNT = static_cast<int>(ClassId::COUNT);
constexpr int BASE_CLASS_COUNT = 4; // first 4 are always unlocked

struct ClassInfo {
    const char* name;
    const char* description;
    int sprite_sheet; // SHEET_ROGUES or SHEET_MONSTERS
    int sprite_x, sprite_y;
    // Starting attributes
    int str, dex, con, intel, wil, per, cha;
    int hp, mp;
    int base_damage;
    // Unlock hint (nullptr = always available)
    const char* unlock_hint;
};

inline const ClassInfo& get_class_info(ClassId id) {
    static const ClassInfo CLASSES[] = {
        // === BASE CLASSES === (sheet, sx, sy, stats..., hp, mp, dmg, hint)
        {"Fighter",     "Balanced melee combatant. Strong, tough, adaptable.",
         0, 1, 1, 14, 12, 13, 8, 9, 10, 10, 35, 5, 4, nullptr},
        {"Rogue",       "Quick and quiet. Daggers, stealth, precision.",
         0, 3, 0, 9, 16, 10, 10, 8, 14, 9, 25, 5, 3, nullptr},
        {"Wizard",      "Fragile scholar. Deep mana pool. Knows things.",
         0, 1, 4, 7, 8, 8, 16, 14, 12, 9, 20, 25, 2, nullptr},
        {"Ranger",      "Eyes like a hawk. Bow, blade, and the patience to use both.",
         0, 2, 0, 11, 14, 11, 9, 10, 15, 8, 28, 8, 3, nullptr},
        // === UNLOCKABLE CLASSES ===
        {"Barbarian",   "Berserker. Rage fuels the blade. Light armor, heavy damage.",
         0, 0, 3, 18, 10, 16, 6, 8, 9, 7, 40, 0, 5, "Slay 50 enemies across all runs."},
        {"Knight",      "Heavy armor, shield, honor. The old way of war.",
         0, 0, 1, 15, 8, 15, 9, 12, 10, 12, 38, 5, 4, "Reach dungeon depth 5."},
        {"Monk",        "Empty hands, clear mind. Unarmed, unarmored, unstoppable.",
         0, 0, 2, 10, 16, 12, 10, 14, 14, 8, 25, 10, 5, "Kill an enemy unarmed."},
        {"Templar",     "Anti-undead specialist. Divine smite. Unwavering.",
         0, 4, 2, 14, 10, 14, 8, 14, 10, 10, 35, 10, 4, "Destroy 30 undead."},
        {"Druid",       "Nature magic, animal kinship. The forest remembers.",
         0, 2, 4, 8, 10, 12, 14, 12, 14, 10, 25, 20, 2, "Complete 10 side quests."},
        {"War Cleric",  "Divine healing and a heavy mace. Faith tested in blood.",
         0, 2, 2, 13, 8, 14, 10, 16, 10, 12, 35, 15, 3, "Heal 300 HP total."},
        {"Warlock",     "Dark pacts. Glass cannon. Something watches over your shoulder.",
         0, 5, 5, 7, 10, 8, 18, 12, 10, 6, 18, 30, 2, "Die on depth 4+."},
        {"Dwarf",       "Tough as stone. Poison resist. Sees in the dark.",
         0, 0, 0, 14, 8, 18, 8, 12, 10, 8, 40, 5, 3, "Reach depth 6."},
        {"Elf",         "Magic affinity, keen perception. Agile and ancient.",
         0, 1, 0, 8, 14, 8, 14, 12, 16, 12, 22, 18, 2, "Examine 50 creatures."},
        {"Bandit",      "Dual wield, intimidation, theft. The road provides.",
         0, 4, 0, 12, 16, 10, 8, 8, 12, 10, 28, 0, 4, "Earn 500 gold in one run."},
        {"Necromancer", "The dead serve willingly. Feared by all. Corpse manipulation.",
         0, 3, 4, 7, 8, 8, 18, 14, 10, 4, 18, 28, 2, "Cast 30 Dark Arts spells."},
        {"Schema Monk", "Elemental strikes. Ki mastery. The perfected form.",
         0, 5, 2, 12, 18, 14, 12, 16, 16, 8, 30, 15, 6, "Reach level 12 as Monk."},
        {"Heretic",     "Godless. No divine aid. No tenets. No mercy.",
         0, 0, 6, 10, 10, 10, 10, 10, 10, 10, 25, 10, 3, "Win with all 7 gods."},
        // === MONSTER-THEMED CLASSES === (use SHEET_MONSTERS = 1)
        {"Wyrmkin",     "Dragon-touched. Fire runs in your veins.",
         1, 0, 8, 16, 8, 18, 10, 12, 8, 6, 45, 5, 7, "Kill a dragon."},
        {"Revenant",    "Death refused you. You came back wrong.",
         1, 3, 4, 10, 10, 14, 8, 16, 8, 4, 35, 10, 5, "Die 10 times."},
        {"Serpentine",  "Cold blood. Quick hands. Venom in every strike.",
         1, 4, 7, 8, 18, 10, 12, 10, 16, 6, 25, 10, 5, "Survive 3 diseases at once."},
        {"Trollblood",  "Something in your ancestry wasn't human. You heal. You endure.",
         1, 2, 1, 18, 6, 20, 6, 10, 6, 4,
         55, 0, 8, "Reach dungeon depth 8."},
    };
    return CLASSES[static_cast<int>(id)];
}
