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
        {"Fighter",     "A disciplined warrior trained in the art of the counterattack. When struck in melee, the Fighter has a 25% chance to parry and return the blow at double strength. Fighters excel with swords, which increase their parry rate.",
         0, 1, 1, 14, 12, 13, 8, 9, 10, 10, 35, 5, 4, nullptr},
        {"Rogue",       "A killer who strikes from shadow. The Rogue's first attack against an uninjured target teleports behind the victim and deals double damage. At level 5, killing a target returns the Rogue to stealth. Best with daggers.",
         0, 3, 0, 9, 16, 10, 10, 8, 14, 9, 25, 5, 3, nullptr},
        {"Wizard",      "A scholar of the arcane arts. The Wizard channels all power through spells, scaling damage with Intelligence. Physically fragile but capable of devastating area attacks. Staves reduce the Weave counter for free spell casts.",
         0, 1, 4, 7, 8, 8, 16, 14, 12, 9, 20, 25, 2, nullptr},
        {"Ranger",      "A patient hunter who designates prey. The Ranger automatically marks the first enemy struck; all subsequent attacks against that target deal 50% bonus damage. Bows increase this bonus to 75%. The mark transfers when the target dies.",
         0, 2, 0, 11, 14, 11, 9, 10, 15, 8, 28, 8, 3, nullptr},
        // === UNLOCKABLE CLASSES ===
        {"Barbarian",   "A berserker who grows stronger as wounds mount. Below half health, all melee attacks deal 50% additional damage. At level 5, kills during rage splash damage to adjacent enemies. Axes lower the rage threshold to 60% health.",
         0, 0, 3, 18, 10, 16, 6, 8, 9, 7, 40, 0, 5, "Slay 50 enemies across all runs."},
        {"Knight",      "A heavily armored defender built around the shield. With a shield equipped, the Knight blocks 30% of incoming melee attacks entirely. Blocked attacks trigger a riposte counter. At level 5, can activate Bulwark for 50% block rate.",
         0, 4, 1, 15, 8, 15, 9, 12, 10, 12, 38, 5, 4, "Reach dungeon depth 3."},
        {"Monk",        "An unarmed martial artist who fights without weapons or armor. Each unarmed strike has a 40% chance to land a bonus hit at half damage. At level 5, every fifth hit stuns the target for 2 turns. Cannot wield weapons effectively.",
         0, 0, 2, 10, 16, 12, 10, 14, 14, 8, 25, 10, 5, "Kill an enemy unarmed."},
        {"Templar",     "A holy warrior devoted to destroying the undead. Deals +6 bonus damage to undead creatures and instantly executes them below 20% health. At level 5, kills create consecrated ground that damages undead each turn. Best with maces.",
         0, 4, 2, 14, 10, 14, 8, 14, 10, 10, 35, 10, 4, "Destroy 30 undead."},
        {"Druid",       "A shapechanger who transforms through violence. After 5 kills, the Druid enters beast form: +8 damage, +30 speed for 10 turns. Each kill during beast form extends the duration by 3 turns. At level 5, transformation summons wolves. Spears reduce the kill requirement to 4.",
         0, 2, 4, 8, 10, 12, 14, 12, 14, 10, 25, 20, 2, "Complete 10 side quests."},
        {"War Cleric",  "A divine warrior whose power builds through consecutive kills. Each kill activates holy fury, adding scaling bonus damage to subsequent attacks. Kills during fury extend its duration and increase the damage bonus. At level 5, fury kills also heal. Maces extend fury duration further.",
         0, 2, 2, 13, 8, 14, 10, 16, 10, 12, 35, 15, 3, "Heal 300 HP total."},
        {"Warlock",     "A dark caster sustained by death. Each kill restores mana equal to 5 plus a quarter of Intelligence. This creates an infinite loop: kill to cast, cast to kill. At level 5, can spend health to cast when mana is empty. Daggers increase mana gained per kill.",
         0, 5, 4, 7, 10, 8, 18, 12, 10, 6, 18, 30, 2, "Die on depth 4+."},
        {"Dwarf",       "A stone-steady fighter who trades mobility for power. By waiting in place, the Dwarf enters a Fortified stance. The next attack from that stance deals double damage, then the stance breaks. Moving cancels the stance. Hammers make it triple damage instead.",
         0, 0, 0, 14, 8, 18, 8, 12, 10, 8, 40, 5, 3, "Reach depth 4."},
        {"Elf",         "An arcane warrior who weaves spells into combat. Every third melee or ranged attack automatically casts a random spell from the Elf's spellbook at no mana cost. Collecting spell tomes directly increases combat power. Bows trigger the weave every 2 hits instead of 3.",
         0, 1, 0, 8, 14, 8, 14, 12, 16, 12, 22, 18, 2, "Examine 15 creatures."},
        {"Bandit",      "An opportunist who finishes wounded prey. All attacks against enemies below 25% health automatically score critical hits, dealing double damage. At level 5, these critical kills grant 3 turns of invisibility. Daggers raise the threshold to 30%.",
         0, 4, 0, 12, 16, 10, 8, 8, 12, 10, 28, 0, 4, "Earn 500 gold in one run."},
        {"Necromancer", "A death mage whose kills create chain reactions. Each kill has a 25% chance to detonate the corpse, dealing 4 damage to all adjacent creatures. At level 5, can maintain 3 raised dead simultaneously. Staves increase the explosion radius.",
         0, 3, 4, 7, 8, 8, 18, 14, 10, 4, 18, 28, 2, "Cast 30 Dark Arts spells."},
        {"Schema Monk", "An elemental martial artist whose unarmed strikes cycle through fire, ice, and lightning. Each element inflicts its status effect, and the Monk deals +3 bonus damage for every active status on the target. The more ailments stacked, the harder each hit lands.",
         0, 5, 2, 12, 18, 14, 12, 16, 16, 8, 30, 15, 6, "Reach level 12 as Monk."},
        {"Heretic",     "A godless wanderer who steals power from the dead. Rejects all divine patronage in exchange for +1 to all attributes and a 20% chance per kill to permanently learn a random spell. Grows stronger with every floor cleared. Unique items increase the learning rate.",
         0, 0, 6, 10, 10, 10, 10, 10, 10, 10, 25, 10, 3, "Win with all 13 gods."},
        // === MONSTER-THEMED CLASSES ===
        {"Wyrmkin",     "A dragon-blooded warrior who builds toward eruption. Every 8 melee or ranged hits, the Wyrmkin unleashes a fire breath attack dealing 6 plus level damage to the target and all adjacent enemies, applying burn. Axes reduce the counter to 4 hits.",
         1, 0, 8, 16, 8, 18, 10, 12, 8, 6, 45, 5, 7, "Kill a dragon."},
        {"Revenant",    "An undead warrior who refuses to stay dead. Heals a portion of health on each kill, and survives one lethal blow per floor at 1 HP. At level 5, surviving death grants 5 turns of doubled damage. Heavy armor causes the death save to restore 25% HP instead of 1.",
         1, 3, 4, 10, 10, 14, 8, 16, 8, 4, 35, 10, 5, "Die 10 times."},
        {"Serpentine",  "A venom specialist who builds poison stacks on targets. Each hit applies a stack. At 5 or more stacks, the venom detonates for damage equal to stacks squared (5=25, 8=64, 10=100). At level 5, detonating at 8+ stacks also paralyzes. Daggers apply 2 stacks per hit.",
         1, 4, 7, 8, 18, 10, 12, 10, 16, 6, 25, 10, 5, "Survive 3 diseases at once."},
        {"Trollblood",  "A regenerating brute who sustains through consumption. Press interact near any corpse to devour it, restoring 25% of maximum health instantly with no cooldown. At level 5, eating 3 corpses on the same floor permanently increases max HP by 5. Blunt weapons heal 35% instead.",
         1, 2, 1, 18, 6, 20, 6, 10, 6, 4,
         55, 0, 8, "Reach dungeon depth 4."},
    };
    return CLASSES[static_cast<int>(id)];
}

struct ClassDetails {
    const char* gear_synergy;  // what weapon type boosts this class
    const char* level5_ability; // second ability unlocked at level 5
    const char* scaling_tip;    // how to build around this class
};

inline ClassDetails get_class_details(ClassId id) {
    static const ClassDetails DETAILS[] = {
        // Fighter
        {"Swords: parry chance +15%", "Lv5: RIPOSTE STANCE (3 guaranteed parries)", "Stack armor. Let enemies hit you. Counter for 2x."},
        // Rogue
        {"Daggers: shadow step works vs any HP", "Lv5: VANISH (re-stealth after every kill)", "Open from stealth, kill, vanish, repeat. Chain assassinations."},
        // Wizard
        {"Staves: weave counter every 2 hits", "Lv5: OVERLOAD (2x MP cost, 3x spell damage)", "Stack INT. More spells = more power. Never melee."},
        // Ranger
        {"Bows: +25% extra mark damage (+75% total)", "Lv5: HUNTER'S FOCUS (marked can't regen)", "Mark the boss. Everything hits it harder. Bow amplifies."},
        // Barbarian
        {"Axes: rage threshold 60% (easier to trigger)", "Lv5: CLEAVE (rage kills splash adjacent)", "Get hurt on purpose. Axes make rage easier. Splash clears rooms."},
        // Knight
        {"Shields: block gives +2 armor 3 turns", "Lv5: BULWARK (50% block for 3 turns, CD 10)", "Full tank. Shield required. Block + riposte = free damage."},
        // Monk
        {"Unarmed: flurry chance 50% (not 40%)", "Lv5: PALM STRIKE (every 5th hit stuns 2 turns)", "Stay unarmed. Speed = more flurries = more stuns."},
        // Templar
        {"Maces: smite +4 vs undead", "Lv5: CONSECRATE (kills create holy ground, 3/turn to undead)", "Hunt undead. Mace + smite = instant kills. Holy ground controls space."},
        // Druid
        {"Spears: shapeshift at 4 kills (not 5)", "Lv5: NATURE'S CALL (beast form summons 2 wolves)", "Kill fast, transform, wolves + you = room clear. Kills extend form."},
        // War Cleric
        {"Maces: fury extends +4 per kill (not +3)", "Lv5: SERMON (fury kills heal 5 HP)", "Chain kills = stacking damage + healing. Mace accelerates the chain."},
        // Warlock
        {"Daggers: siphon +3 extra MP per kill", "Lv5: DARK PACT (spend HP to cast when no MP)", "Kill for MP, cast for kills. Infinite loop. Daggers sustain it."},
        // Dwarf
        {"Hammers: fortify = TRIPLE damage (not double)", "Lv5: EARTHQUAKE (fortify strike stuns adjacent)", "Wait, strike, stun room. Hammer triples the hit. Repeat."},
        // Elf
        {"Bows: weave triggers every 2 ranged hits", "Lv5: SPELL MASTERY (weave spells deal +50%)", "Collect spell tomes. More spells = stronger weave rotation."},
        // Bandit
        {"Daggers: exploit threshold 30% (not 25%)", "Lv5: CUTTHROAT (exploit kills = 3 turns invis)", "Poison/bleed enemies low, then execute. Daggers widen the window."},
        // Necromancer
        {"Staves: corpse explode radius +1", "Lv5: ARMY (raise dead cap = 3 summons)", "Kill groups. Explosions chain. Raise the dead. Staff widens AoE."},
        // Schema Monk
        {"Unarmed: status duration +1 turn per element", "Lv5: CONFLUENCE (every 9th hit = all 3 elements at once)", "Stack statuses. More ailments = more damage. Confluence triples it."},
        // Heretic
        {"Any unique item: +5% devour chance", "Lv5: MIMICRY (devoured spells deal +50% damage)", "Kill everything. Steal spells. Each unique item = faster learning."},
        // Wyrmkin
        {"Axes: breath counter 4 (not 8, half the hits)", "Lv5: INFERNO (breath leaves burning ground 3 turns)", "Axes halve the wait. Breath every 4 hits + burning ground."},
        // Revenant
        {"Heavy armor: death save heals to 25% (not 1 HP)", "Lv5: UNDYING FURY (+100% damage 5 turns after death save)", "Die on purpose. Armor keeps you at 25%. Fury doubles damage."},
        // Serpentine
        {"Daggers: +1 extra stack per hit (double speed)", "Lv5: NEUROTOXIN (8+ stacks = stun 3 turns on burst)", "Daggers double stacking. 8 stacks = 64 burst + paralysis."},
        // Trollblood
        {"Clubs/Blunt: consume heals 35% (not 25%)", "Lv5: GORGE (eat 3 corpses/floor = permanent +5 max HP)", "Position near corpses. Eat constantly. Clubs heal more. HP grows each floor."},
    };
    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= static_cast<int>(ClassId::COUNT)) {
        static const ClassDetails NONE = {"", "", ""};
        return NONE;
    }
    return DETAILS[idx];
}
