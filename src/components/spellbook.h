#pragma once
#include <string>
#include <vector>

enum class SpellId : int {
    // Conjuration (10)
    SPARK = 0,
    FORCE_BOLT,
    FIREBALL,
    ICE_SHARD,
    LIGHTNING,
    CHAIN_LIGHTNING,
    METEOR,
    ACID_SPLASH,
    FROST_NOVA,
    DISINTEGRATE,
    // Transmutation (7)
    HARDEN_SKIN,
    HASTEN,
    STONE_FIST,
    PHASE,
    IRON_BODY,
    SLOW,
    POLYMORPH,
    // Divination (7)
    REVEAL_MAP,
    DETECT_MONSTERS,
    IDENTIFY,
    FORESIGHT,
    TRUESIGHT,
    SCRY,
    CLAIRVOYANCE,
    // Healing (8)
    MINOR_HEAL,
    CURE_POISON,
    MAJOR_HEAL,
    CLEANSE,
    SHIELD_OF_FAITH,
    RESTORE,
    SANCTUARY,
    RESURRECTION,
    // Nature (9)
    ENTANGLE,
    BEAST_CALL,
    POISON_CLOUD,
    THORNWALL,
    REJUVENATE,
    EARTHQUAKE,
    LIGHTNING_STORM,
    BARKSKIN,
    SWARM,
    // Dark Arts (9)
    DRAIN_LIFE,
    FEAR,
    RAISE_DEAD,
    HEX,
    SOUL_REND,
    DARKNESS,
    WITHER,
    BLOOD_PACT,
    DOOM,
    COUNT
};

constexpr int SPELL_COUNT = static_cast<int>(SpellId::COUNT);

enum class SpellSchool : int {
    CONJURATION = 0,
    TRANSMUTATION,
    DIVINATION,
    HEALING,
    NATURE,
    DARK_ARTS,
    COUNT
};

struct SpellInfo {
    const char* name;
    const char* description;
    SpellSchool school;
    int mp_cost;
    int base_power;
    int range;         // 0 = self, -1 = all visible, >0 = tile range
    bool hostile;
};

inline const SpellInfo& get_spell_info(SpellId id) {
    static const SpellInfo SPELLS[] = {
        // --- Conjuration (10) ---
        {"Spark",           "Dmg 6+INT/3 to nearest enemy. Range 5.",
         SpellSchool::CONJURATION, 3, 6, 5, true},
        {"Force Bolt",      "Dmg 12+INT/3 to nearest enemy. Range 6.",
         SpellSchool::CONJURATION, 5, 12, 6, true},
        {"Fireball",        "Dmg 20+INT/3, applies burn 3 turns. Range 7.",
         SpellSchool::CONJURATION, 10, 20, 7, true},
        {"Ice Shard",       "Dmg 14+INT/3, freezes target 2 turns. Range 6.",
         SpellSchool::CONJURATION, 7, 14, 6, true},
        {"Lightning",       "Dmg 25+INT/3. Range 8.",
         SpellSchool::CONJURATION, 12, 25, 8, true},
        {"Chain Lightning", "Dmg 15+INT/3 to up to 3 visible enemies.",
         SpellSchool::CONJURATION, 14, 15, 7, true},
        {"Meteor",          "Dmg 35+INT/3 to nearest enemy. Range 6.",
         SpellSchool::CONJURATION, 20, 35, 6, true},
        {"Acid Splash",     "Dmg 8+INT/3, reduces target armor by 2. Range 5.",
         SpellSchool::CONJURATION, 5, 8, 5, true},
        {"Frost Nova",      "Dmg 10+INT/3 to ALL visible in range 4. Freezes 1 turn.",
         SpellSchool::CONJURATION, 12, 10, 4, true},
        {"Disintegrate",    "Dmg 45+INT/3 to nearest enemy. Range 5.",
         SpellSchool::CONJURATION, 25, 45, 5, true},

        // --- Transmutation (7) ---
        {"Harden Skin",     "+2 natural armor.",
         SpellSchool::TRANSMUTATION, 6, 4, 0, false},
        {"Hasten",          "+30 movement speed.",
         SpellSchool::TRANSMUTATION, 8, 30, 0, false},
        {"Stone Fist",      "+3 base melee damage.",
         SpellSchool::TRANSMUTATION, 7, 3, 0, false},
        {"Phase",           "Teleport to random location on this floor.",
         SpellSchool::TRANSMUTATION, 10, 0, 0, false},
        {"Iron Body",       "+4 natural armor, -10 speed.",
         SpellSchool::TRANSMUTATION, 12, 4, 0, false},
        {"Slow",            "Reduce nearest enemy speed by 30. Range 6.",
         SpellSchool::TRANSMUTATION, 6, 30, 6, true},
        {"Polymorph",       "Turn nearest enemy into a rat (1 HP). Range 5.",
         SpellSchool::TRANSMUTATION, 15, 0, 5, true},

        // --- Divination (7) ---
        {"Reveal Map",      "Mark all tiles on this floor as explored.",
         SpellSchool::DIVINATION, 8, 0, -1, false},
        {"Detect Monsters", "Reveal positions of all creatures on this floor.",
         SpellSchool::DIVINATION, 4, 0, -1, false},
        {"Identify",        "Identify one unidentified item in your inventory.",
         SpellSchool::DIVINATION, 5, 0, 0, false},
        {"Foresight",       "+1 armor (heightened awareness).",
         SpellSchool::DIVINATION, 6, 3, 0, false},
        {"Truesight",       "Reveal 6-tile radius around you.",
         SpellSchool::DIVINATION, 9, 4, 0, false},
        {"Scry",            "Reveal all items on this floor.",
         SpellSchool::DIVINATION, 7, 0, -1, false},
        {"Clairvoyance",    "Reveal map + detect monsters combined.",
         SpellSchool::DIVINATION, 14, 0, -1, false},

        // --- Healing (8) ---
        {"Minor Heal",      "Restore 10+INT/3 HP.",
         SpellSchool::HEALING, 4, 10, 0, false},
        {"Cure Poison",     "Remove all poison status effects.",
         SpellSchool::HEALING, 5, 0, 0, false},
        {"Major Heal",      "Restore 25+INT/3 HP.",
         SpellSchool::HEALING, 10, 25, 0, false},
        {"Cleanse",         "Remove ALL status effects (poison, burn, bleed, etc).",
         SpellSchool::HEALING, 8, 0, 0, false},
        {"Shield of Faith", "+3 natural armor.",
         SpellSchool::HEALING, 7, 3, 0, false},
        {"Restore",         "Restore 15 HP and 10 MP simultaneously.",
         SpellSchool::HEALING, 12, 15, 0, false},
        {"Sanctuary",       "+5 natural armor. Enemies lose track of you.",
         SpellSchool::HEALING, 15, 5, 0, false},
        {"Resurrection",    "If HP <= 0 within 10 turns, revive at half HP instead.",
         SpellSchool::HEALING, 20, 0, 0, false},

        // --- Nature (9) ---
        {"Entangle",        "Dmg 3+INT/3 to all visible enemies in range 5.",
         SpellSchool::NATURE, 6, 3, 5, true},
        {"Beast Call",      "Summon 2 wolves (15+power HP) that attack nearby enemies.",
         SpellSchool::NATURE, 8, 0, -1, false},
        {"Poison Cloud",    "Apply poison (2 dmg/turn, 6 turns) to all visible in range 6.",
         SpellSchool::NATURE, 8, 2, 6, true},
        {"Thornwall",       "Apply bleed (3 dmg/turn, 5 turns) to all visible in range 4.",
         SpellSchool::NATURE, 7, 3, 4, true},
        {"Rejuvenate",      "Heal power x3 HP immediately.",
         SpellSchool::NATURE, 9, 3, 0, false},
        {"Earthquake",      "Dmg 8+INT/3 to ALL enemies on floor. Stuns adjacent 2 turns.",
         SpellSchool::NATURE, 16, 8, -1, true},
        {"Lightning Storm",  "Dmg 12+INT/3 to up to 5 random visible enemies.",
         SpellSchool::NATURE, 18, 12, 7, true},
        {"Barkskin",        "+3 natural armor, +15% poison resist.",
         SpellSchool::NATURE, 7, 3, 0, false},
        {"Swarm",           "Summon 4 rats that attack nearby enemies.",
         SpellSchool::NATURE, 6, 0, -1, false},

        // --- Dark Arts (9) ---
        {"Drain Life",      "Dmg 10+INT/3, range 4. Heal for half damage dealt.",
         SpellSchool::DARK_ARTS, 6, 10, 4, true},
        {"Fear",            "All visible enemies in range 5 enter fleeing state.",
         SpellSchool::DARK_ARTS, 5, 0, 5, true},
        {"Raise Dead",      "Reanimate a skeleton ally (12+power HP) near you.",
         SpellSchool::DARK_ARTS, 12, 0, 3, false},
        {"Hex",             "Confuse nearest enemy for 4 turns. Range 5.",
         SpellSchool::DARK_ARTS, 7, 0, 5, true},
        {"Soul Rend",       "Dmg 12+INT/3, applies bleed 4 turns. Range 5.",
         SpellSchool::DARK_ARTS, 9, 12, 5, true},
        {"Darkness",        "Blind all visible enemies for 4 turns. Range 6.",
         SpellSchool::DARK_ARTS, 10, 0, 6, true},
        {"Wither",          "Dmg 8+INT/3, permanently reduce target max HP by 5. Range 5.",
         SpellSchool::DARK_ARTS, 11, 8, 5, true},
        {"Blood Pact",      "Sacrifice 20 HP. Gain +5 damage, +2 armor permanently.",
         SpellSchool::DARK_ARTS, 0, 0, 0, false},
        {"Doom",            "Mark nearest enemy. It dies in 5 turns. Range 4.",
         SpellSchool::DARK_ARTS, 22, 0, 4, true},
    };
    return SPELLS[static_cast<int>(id)];
}

struct Spellbook {
    std::vector<SpellId> known_spells;

    bool knows(SpellId id) const {
        for (auto s : known_spells) {
            if (s == id) return true;
        }
        return false;
    }

    void learn(SpellId id) {
        if (!knows(id)) known_spells.push_back(id);
    }
};
