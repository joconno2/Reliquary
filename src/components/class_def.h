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
        // === BASE CLASSES ===
        {"Fighter",     "PARRY: 25% counter-attack for 2x damage when hit. Punishes aggression.",
         0, 1, 1, 14, 12, 13, 8, 9, 10, 10, 35, 5, 4, nullptr},
        {"Rogue",       "SHADOW STEP: First strike vs full-HP targets teleports behind for 2x damage.",
         0, 3, 0, 9, 16, 10, 10, 8, 14, 9, 25, 5, 3, nullptr},
        {"Wizard",      "CAST: High INT/MP. Spells scale with intelligence. Fragile but devastating.",
         0, 1, 4, 7, 8, 8, 16, 14, 12, 9, 20, 25, 2, nullptr},
        {"Ranger",      "MARK: Auto-marks targets. Marked enemies take +50% damage from all attacks.",
         0, 2, 0, 11, 14, 11, 9, 10, 15, 8, 28, 8, 3, nullptr},
        // === UNLOCKABLE CLASSES ===
        {"Barbarian",   "RAGE: Below 50% HP, +50% melee damage. Get hurt to get dangerous.",
         0, 0, 3, 18, 10, 16, 6, 8, 9, 7, 40, 0, 5, "Slay 50 enemies across all runs."},
        {"Knight",      "SHIELD WALL: 30% block with shield. Riposte on block. Immovable.",
         0, 4, 1, 15, 8, 15, 9, 12, 10, 12, 38, 5, 4, "Reach dungeon depth 3."},
        {"Monk",        "FLURRY: 40% chance bonus hit when unarmed. Sustained pressure.",
         0, 0, 2, 10, 16, 12, 10, 14, 14, 8, 25, 10, 5, "Kill an enemy unarmed."},
        {"Templar",     "SMITE: +6 vs undead. Execute below 20% HP. Holy warrior.",
         0, 4, 2, 14, 10, 14, 8, 14, 10, 10, 35, 10, 4, "Destroy 30 undead."},
        {"Druid",       "SHAPESHIFT: 5 kills = beast form (+8 dmg, +30 speed). Kills extend it.",
         0, 2, 4, 8, 10, 12, 14, 12, 14, 10, 25, 20, 2, "Complete 10 side quests."},
        {"War Cleric",  "CHAIN FURY: Kills grant stacking holy damage. Chain kills amplify.",
         0, 2, 2, 13, 8, 14, 10, 16, 10, 12, 35, 15, 3, "Heal 300 HP total."},
        {"Warlock",     "SOUL SIPHON: Kills restore MP. Infinite casting if you keep killing.",
         0, 5, 4, 7, 10, 8, 18, 12, 10, 6, 18, 30, 2, "Die on depth 4+."},
        {"Dwarf",       "FORTIFY: Wait to enter stance. Next attack deals DOUBLE damage.",
         0, 0, 0, 14, 8, 18, 8, 12, 10, 8, 40, 5, 3, "Reach depth 4."},
        {"Elf",         "WEAVE: Every 3rd attack auto-casts a random known spell for free.",
         0, 1, 0, 8, 14, 8, 14, 12, 16, 12, 22, 18, 2, "Examine 15 creatures."},
        {"Bandit",      "EXPLOIT: Attacks vs enemies below 25% HP always crit. Execute window.",
         0, 4, 0, 12, 16, 10, 8, 8, 12, 10, 28, 0, 4, "Earn 500 gold in one run."},
        {"Necromancer", "EXPLODE: 25% on kill, corpse detonates for 4 AoE. Chain reactions.",
         0, 3, 4, 7, 8, 8, 18, 14, 10, 4, 18, 28, 2, "Cast 30 Dark Arts spells."},
        {"Schema Monk", "CYCLE: Unarmed hits cycle fire/ice/lightning. +3 dmg per status on target.",
         0, 5, 2, 12, 18, 14, 12, 16, 16, 8, 30, 15, 6, "Reach level 12 as Monk."},
        {"Heretic",     "DEVOUR: 20% on kill, learn a random spell. Build power from nothing.",
         0, 0, 6, 10, 10, 10, 10, 10, 10, 10, 25, 10, 3, "Win with all 13 gods."},
        // === MONSTER-THEMED CLASSES ===
        {"Wyrmkin",     "ERUPT: Every 8 hits, fire breath AoE (6+level). Burn everything.",
         1, 0, 8, 16, 8, 18, 10, 12, 8, 6, 45, 5, 7, "Kill a dragon."},
        {"Revenant",    "REFUSE: Heal on kill. Survive lethal 1/floor. Death can't hold you.",
         1, 3, 4, 10, 10, 14, 8, 16, 8, 4, 35, 10, 5, "Die 10 times."},
        {"Serpentine",  "INJECT: Poison stacks. At 5+, DETONATE for stacks-squared burst damage.",
         1, 4, 7, 8, 18, 10, 12, 10, 16, 6, 25, 10, 5, "Survive 3 diseases at once."},
        {"Trollblood",  "CONSUME: Wait near corpses to eat them. Heals 25% HP. No cooldown.",
         1, 2, 1, 18, 6, 20, 6, 10, 6, 4,
         55, 0, 8, "Reach dungeon depth 4."},
    };
    return CLASSES[static_cast<int>(id)];
}
