#pragma once
#include "components/god.h"
#include "components/spellbook.h"

enum class ChurchRank : int {
    OUTSIDER = 0,
    INITIATE,    // favor 15+ task complete
    ACOLYTE,     // favor 30+ task complete
    DEVOTED,     // favor 50+ task complete
    CHAMPION,    // favor 75+ task complete
    RANK_COUNT
};

constexpr int CHURCH_RANK_COUNT = static_cast<int>(ChurchRank::RANK_COUNT);

inline const char* church_rank_name(ChurchRank r) {
    static const char* NAMES[] = {"Outsider", "Initiate", "Acolyte", "Devoted", "Champion"};
    return NAMES[static_cast<int>(r)];
}

inline int church_rank_favor(ChurchRank r) {
    static const int THRESHOLDS[] = {0, 15, 30, 50, 75};
    return THRESHOLDS[static_cast<int>(r)];
}

// Rank is determined by completed tasks, not just favor
// Player must complete each rank's task AND have enough favor
inline ChurchRank church_rank_for_favor(int favor) {
    // This is now only used for display; actual rank is tracked on GodAlignment
    if (favor >= 75) return ChurchRank::CHAMPION;
    if (favor >= 50) return ChurchRank::DEVOTED;
    if (favor >= 30) return ChurchRank::ACOLYTE;
    if (favor >= 15) return ChurchRank::INITIATE;
    return ChurchRank::OUTSIDER;
}

// Church rank-up tasks: what you must do to advance
struct ChurchTask {
    const char* description;   // "Slay 5 undead"
    int kill_target;           // number of kills required (0 = not a kill task)
    int dungeon_clears;        // dungeons cleared requirement
    int items_donated;         // gold/items donated
    bool is_dungeon_quest;     // must clear a specific dungeon floor
};

// Per-god rank tasks (indexed by [god][rank-1], ranks 1-4)
inline const ChurchTask& get_church_task(GodId god, ChurchRank target_rank) {
    // 13 gods x 4 ranks = 52 tasks
    // Generic tasks per rank, flavored per god
    static const ChurchTask TASKS[13][4] = {
        // VETHRIK (death)
        {{"Destroy 5 undead.",             5, 0, 0, false},
         {"Clear a dungeon floor.",         0, 0, 0, true},
         {"Destroy 15 undead.",            15, 0, 0, false},
         {"Reach the bottom of any dungeon.", 0, 1, 0, false}},
        // THESSARKA (knowledge)
        {{"Identify 3 items at the church.", 0, 0, 3, false},
         {"Discover 2 dungeon floors.",     0, 0, 0, true},
         {"Identify 8 items.",              0, 0, 8, false},
         {"Find and read 3 lore items.",    0, 0, 0, false}},
        // MORRETH (war)
        {{"Kill 8 enemies in combat.",      8, 0, 0, false},
         {"Clear a dungeon floor.",         0, 0, 0, true},
         {"Kill 25 enemies.",              25, 0, 0, false},
         {"Defeat a dungeon boss.",         0, 1, 0, false}},
        // YASHKHET (blood)
        {{"Kill 8 enemies below 50% HP.",   8, 0, 0, false},
         {"Survive 3 near-death moments.",  0, 0, 0, true},
         {"Kill 20 enemies.",              20, 0, 0, false},
         {"Defeat a dungeon boss.",         0, 1, 0, false}},
        // KHAEL (nature)
        {{"Kill 5 enemies without harming animals.", 5, 0, 0, false},
         {"Clear a dungeon floor.",         0, 0, 0, true},
         {"Kill 15 enemies in dungeons.",  15, 0, 0, false},
         {"Defeat a dungeon boss.",         0, 1, 0, false}},
        // SOLETH (fire)
        {{"Destroy 5 undead.",              5, 0, 0, false},
         {"Burn 5 enemies.",                5, 0, 0, false},
         {"Destroy 15 undead or demons.",  15, 0, 0, false},
         {"Defeat a dungeon boss.",         0, 1, 0, false}},
        // IXUUL (chaos)
        {{"Kill 8 enemies.",                8, 0, 0, false},
         {"Clear a dungeon floor.",         0, 0, 0, true},
         {"Survive 50 mutation cycles.",    0, 0, 0, false},
         {"Defeat a dungeon boss.",         0, 1, 0, false}},
        // ZHAVEK (shadow)
        {{"Kill 5 enemies from stealth.",   5, 0, 0, false},
         {"Steal 50 gold total.",           0, 0, 50, false},
         {"Kill 15 enemies from stealth.", 15, 0, 0, false},
         {"Defeat a dungeon boss unseen.",  0, 1, 0, false}},
        // THALARA (sea)
        {{"Kill 8 enemies.",                8, 0, 0, false},
         {"Clear a dungeon floor.",         0, 0, 0, true},
         {"Kill 20 enemies.",              20, 0, 0, false},
         {"Defeat a dungeon boss.",         0, 1, 0, false}},
        // OSSREN (craft)
        {{"Kill 8 enemies with equipped gear.", 8, 0, 0, false},
         {"Clear a dungeon floor.",         0, 0, 0, true},
         {"Kill 20 enemies.",              20, 0, 0, false},
         {"Defeat a dungeon boss.",         0, 1, 0, false}},
        // LETHIS (dreams)
        {{"Rest 3 times in dungeons.",      0, 0, 3, false},
         {"Clear a dungeon floor.",         0, 0, 0, true},
         {"Rest 8 times.",                  0, 0, 8, false},
         {"Defeat a dungeon boss.",         0, 1, 0, false}},
        // GATHRUUN (stone)
        {{"Descend 3 dungeon floors.",      0, 0, 3, false},
         {"Clear a dungeon floor.",         0, 0, 0, true},
         {"Descend 8 floors total.",        0, 0, 8, false},
         {"Defeat a dungeon boss.",         0, 1, 0, false}},
        // SYTHARA (plague)
        {{"Poison 5 enemies.",              5, 0, 0, false},
         {"Clear a dungeon floor.",         0, 0, 0, true},
         {"Poison 20 enemies.",            20, 0, 0, false},
         {"Defeat a dungeon boss.",         0, 1, 0, false}},
    };
    int gi = static_cast<int>(god);
    int ri = static_cast<int>(target_rank) - 1; // rank 1-4 -> index 0-3
    if (gi < 0 || gi >= 13 || ri < 0 || ri >= 4)  {
        static const ChurchTask EMPTY = {"No task.", 0, 0, 0, false};
        return EMPTY;
    }
    return TASKS[gi][ri];
}

// What each god's church offers at each rank
struct ChurchRewards {
    // Initiate (10)
    bool free_rest;           // full heal, no exhaustion
    bool discount_shop;       // church supplies at 50% price

    // Acolyte (25)
    bool free_identify;       // ID all items on visit
    SpellId exclusive_spell;  // church-exclusive tome
    const char* enchant_name; // weapon enchant name
    int enchant_bonus;        // enchant damage bonus (50 turns)

    // Devoted (50)
    const char* exclusive_item_name;
    const char* exclusive_item_desc;
    int exclusive_item_damage;
    int exclusive_item_armor;

    // Champion (75)
    const char* blessing_name;
    const char* blessing_desc;
    // Blessing effect applied as permanent stat bonus
    int blessing_str, blessing_dex, blessing_con, blessing_int, blessing_wil, blessing_per;
    int blessing_hp, blessing_mp;
};

inline const ChurchRewards& get_church_rewards(GodId god) {
    static const ChurchRewards REWARDS[] = {
        // VETHRIK (death)
        {true, true, true, SpellId::RAISE_DEAD, "Deathtouch", 3,
         "Ossuary Blade", "A sword that drinks life.", 6, 0,
         "Death's Embrace", "Kills heal 10% max HP permanently.", 0, 0, 2, 0, 3, 0, 10, 5},
        // THESSARKA (knowledge)
        {true, true, true, SpellId::CLAIRVOYANCE, "Insight", 2,
         "Seer's Circlet", "A helm that reveals truth.", 0, 3,
         "Omniscience", "All traps revealed. +3 PER.", 0, 0, 0, 3, 0, 3, 0, 15},
        // MORRETH (war)
        {true, true, true, SpellId::EARTHQUAKE, "Warbrand", 4,
         "Warlord's Plate", "Armor forged in battle.", 0, 5,
         "Fury Unbound", "+5 base damage permanently.", 3, 0, 2, 0, 0, 0, 15, 0},
        // YASHKHET (blood)
        {true, true, true, SpellId::BLOOD_PACT, "Bloodthirst", 3,
         "Hungering Dagger", "It drinks deep.", 5, 0,
         "Crimson Pact", "Damage heals you for 15%.", 2, 2, 0, 0, 2, 0, 0, 10},
        // KHAEL (nature)
        {true, true, true, SpellId::BEAST_CALL, "Thornbind", 2,
         "Verdant Shield", "Living wood that grows.", 0, 4,
         "One With Nature", "Regen 1 HP/turn. Immune to poison.", 0, 0, 3, 0, 2, 0, 10, 5},
        // SOLETH (fire/order)
        {true, true, true, SpellId::SANCTUARY, "Holyfire", 3,
         "Sunforged Mace", "Burns the unholy.", 5, 0,
         "Solar Radiance", "+3 damage vs undead. Immune to blind.", 2, 0, 0, 0, 3, 0, 10, 5},
        // IXUUL (chaos)
        {true, true, true, SpellId::POLYMORPH, "Chaostouch", 2,
         "Entropic Ring", "Reality bends around it.", 0, 2,
         "Chaos Ascendant", "10% chance to dodge any attack.", 0, 3, 0, 2, 0, 0, 5, 10},
        // ZHAVEK (shadow)
        {true, true, true, SpellId::DARKNESS, "Shadowstrike", 3,
         "Nightcloak", "Woven from darkness.", 0, 3,
         "Living Shadow", "Permanent invisibility for 2 turns after each kill.", 0, 3, 0, 0, 0, 3, 5, 5},
        // THALARA (sea)
        {true, true, true, SpellId::FROST_NOVA, "Tidalforce", 2,
         "Coral Trident", "Cold as the deep.", 4, 1,
         "Ocean's Blessing", "Immune to freeze. +20 speed.", 0, 2, 2, 0, 0, 0, 10, 10},
        // OSSREN (craft)
        {true, true, true, SpellId::IRON_BODY, "Tempered Edge", 4,
         "Masterwork Gauntlets", "Perfect craftsmanship.", 2, 3,
         "Forgemaster", "All equipped items +1 damage, +1 armor.", 2, 0, 2, 0, 0, 0, 10, 0},
        // LETHIS (sleep/dreams)
        {true, true, true, SpellId::PHASE, "Dreamtouch", 2,
         "Dreamer's Cowl", "The boundary thins.", 0, 2,
         "Lucid Dream", "Rest always succeeds, no exhaustion, double healing.", 0, 0, 0, 2, 3, 0, 5, 15},
        // GATHRUUN (stone)
        {true, true, true, SpellId::EARTHQUAKE, "Stonefist", 3,
         "Granite Aegis", "Mountain-born defense.", 0, 6,
         "Heart of Stone", "+5 armor permanently. Immune to stun.", 3, 0, 3, 0, 0, 0, 20, 0},
        // SYTHARA (plague)
        {true, true, true, SpellId::POISON_CLOUD, "Plaguebite", 3,
         "Blighted Blade", "Dripping with decay.", 4, 0,
         "Plague Lord", "All attacks poison. Immune to disease.", 0, 2, 0, 0, 0, 2, 5, 5},
    };
    int idx = static_cast<int>(god);
    if (idx < 0 || idx >= GOD_COUNT) idx = 0;
    return REWARDS[idx];
}

// Church entity component
struct Church {
    GodId god = GodId::NONE;
    bool item_given = false;     // Devoted item only given once
    bool blessing_given = false; // Champion blessing only given once
};
