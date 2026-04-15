#pragma once
#include "components/god.h"
#include "components/spellbook.h"

enum class ChurchRank : int {
    OUTSIDER = 0,
    INITIATE,    // favor 10+
    ACOLYTE,     // favor 25+
    DEVOTED,     // favor 50+
    CHAMPION,    // favor 75+
    RANK_COUNT
};

constexpr int CHURCH_RANK_COUNT = static_cast<int>(ChurchRank::RANK_COUNT);

inline const char* church_rank_name(ChurchRank r) {
    static const char* NAMES[] = {"Outsider", "Initiate", "Acolyte", "Devoted", "Champion"};
    return NAMES[static_cast<int>(r)];
}

inline int church_rank_favor(ChurchRank r) {
    static const int THRESHOLDS[] = {0, 10, 25, 50, 75};
    return THRESHOLDS[static_cast<int>(r)];
}

inline ChurchRank church_rank_for_favor(int favor) {
    if (favor >= 75) return ChurchRank::CHAMPION;
    if (favor >= 50) return ChurchRank::DEVOTED;
    if (favor >= 25) return ChurchRank::ACOLYTE;
    if (favor >= 10) return ChurchRank::INITIATE;
    return ChurchRank::OUTSIDER;
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
    bool blessing_given = false; // Champion blessing only given once
};
