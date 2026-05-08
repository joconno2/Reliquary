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
    // New spells
    GLACIAL_SPIKE,     // Conjuration: high single-target ice + stun
    MIRROR_IMAGE,      // Transmutation: dodge bonus for N turns
    FARSIGHT,          // Divination: reveal all enemies + items on floor
    MASS_HEAL,         // Healing: heal all friendlies in range
    VINE_PRISON,       // Nature: root + bleed target, can't move 3 turns
    SOUL_CAGE,         // Dark Arts: kill target below 15% HP, gain max HP
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
        // Clear tiers: Spark(cheap) < Force Bolt < Ice Shard < Fireball < Lightning < Glacial Spike < Meteor < Disintegrate
        // AoE: Acid Splash(armor shred) < Frost Nova(freeze all) < Chain Lightning(multi-hit)
        {"Spark",           "Dmg 8+INT/3. Range 5.",
         SpellSchool::CONJURATION, 3, 8, 5, true},
        {"Force Bolt",      "Dmg 15+INT/3. Range 6.",
         SpellSchool::CONJURATION, 6, 15, 6, true},
        {"Fireball",        "Dmg 25+INT/3. Burn 3 turns. Range 7.",
         SpellSchool::CONJURATION, 10, 25, 7, true},
        {"Ice Shard",       "Dmg 18+INT/3. Freeze 2 turns. Range 6.",
         SpellSchool::CONJURATION, 8, 18, 6, true},
        {"Lightning",       "Dmg 30+INT/3. Range 8.",
         SpellSchool::CONJURATION, 12, 30, 8, true},
        {"Chain Lightning", "Dmg 20+INT/3 to up to 3 enemies.",
         SpellSchool::CONJURATION, 16, 20, 7, true},
        {"Meteor",          "Dmg 45+INT/3. Range 6.",
         SpellSchool::CONJURATION, 20, 45, 6, true},
        {"Acid Splash",     "Dmg 12+INT/3. -3 armor. Range 5.",
         SpellSchool::CONJURATION, 6, 12, 5, true},
        {"Frost Nova",      "Dmg 15+INT/3 to ALL in range 4. Freeze 1 turn.",
         SpellSchool::CONJURATION, 14, 15, 4, true},
        {"Disintegrate",    "Dmg 60+INT/3. Range 5.",
         SpellSchool::CONJURATION, 28, 60, 5, true},

        // --- Transmutation (7) ---
        // Each buff does something different. No overlapping armor buffs.
        {"Harden Skin",     "+3 armor for this floor.",
         SpellSchool::TRANSMUTATION, 6, 4, 0, false},
        {"Hasten",          "+40 speed for this floor.",
         SpellSchool::TRANSMUTATION, 8, 40, 0, false},
        {"Stone Fist",      "+4 base melee damage for this floor.",
         SpellSchool::TRANSMUTATION, 8, 4, 0, false},
        {"Phase",           "Teleport to random location on this floor.",
         SpellSchool::TRANSMUTATION, 10, 0, 0, false},
        {"Iron Body",       "+5 armor, -20 speed for this floor.",
         SpellSchool::TRANSMUTATION, 14, 5, 0, false},
        {"Slow",            "-40 speed on nearest enemy. Range 6.",
         SpellSchool::TRANSMUTATION, 7, 40, 6, true},
        {"Polymorph",       "Turn nearest enemy into a rat (1 HP). Range 5.",
         SpellSchool::TRANSMUTATION, 18, 0, 5, true},

        // --- Divination (7) ---
        // Cut redundancy: Reveal Map does everything, cheaper spells do subsets.
        {"Reveal Map",      "Show all tiles on this floor.",
         SpellSchool::DIVINATION, 8, 0, -1, false},
        {"Detect Monsters", "Show all creatures on this floor.",
         SpellSchool::DIVINATION, 4, 0, -1, false},
        {"Identify",        "Identify one item in inventory.",
         SpellSchool::DIVINATION, 5, 0, 0, false},
        {"Foresight",       "+3 PER for this floor. Traps easier to spot.",
         SpellSchool::DIVINATION, 6, 3, 0, false},
        {"Truesight",       "See through walls (6 tile radius).",
         SpellSchool::DIVINATION, 10, 4, 0, false},
        {"Scry",            "Show all items on this floor.",
         SpellSchool::DIVINATION, 6, 0, -1, false},
        {"Clairvoyance",    "Reveal map + all creatures + all items.",
         SpellSchool::DIVINATION, 14, 0, -1, false},

        // --- Healing (8) ---
        // Clear tier: Minor < Major < Restore. Utility: Cure < Cleanse < Sanctuary < Resurrection.
        {"Minor Heal",      "Heal 12+INT/3 HP.",
         SpellSchool::HEALING, 4, 12, 0, false},
        {"Cure Poison",     "Remove poison.",
         SpellSchool::HEALING, 4, 0, 0, false},
        {"Major Heal",      "Heal 30+INT/3 HP.",
         SpellSchool::HEALING, 10, 30, 0, false},
        {"Cleanse",         "Remove all status effects.",
         SpellSchool::HEALING, 8, 0, 0, false},
        {"Shield of Faith", "+4 armor, +2 dodge for this floor.",
         SpellSchool::HEALING, 9, 4, 0, false},
        {"Restore",         "Heal 20 HP and 15 MP.",
         SpellSchool::HEALING, 12, 20, 0, false},
        {"Sanctuary",       "+6 armor. Enemies lose track of you.",
         SpellSchool::HEALING, 16, 6, 0, false},
        {"Resurrection",    "If HP hits 0 within 10 turns, revive at half HP.",
         SpellSchool::HEALING, 22, 0, 0, false},

        // --- Nature (9) ---
        // Summons + DoT + big AoE. Nature is the crowd control school.
        {"Entangle",        "Dmg 5+INT/3 to all in range 5. Slows.",
         SpellSchool::NATURE, 6, 5, 5, true},
        {"Beast Call",      "Summon 2 wolves (20 HP each).",
         SpellSchool::NATURE, 10, 0, -1, false},
        {"Poison Cloud",    "Poison all in range 6 (3 dmg/turn, 6 turns).",
         SpellSchool::NATURE, 10, 3, 6, true},
        {"Thornwall",       "Bleed all in range 4 (4 dmg/turn, 5 turns).",
         SpellSchool::NATURE, 8, 4, 4, true},
        {"Rejuvenate",      "Heal 15+INT/3 HP.",
         SpellSchool::NATURE, 9, 15, 0, false},
        {"Earthquake",      "Dmg 12+INT/3 to ALL on floor. Stun adjacent 2 turns.",
         SpellSchool::NATURE, 18, 12, -1, true},
        {"Lightning Storm",  "Dmg 18+INT/3 to up to 5 random enemies.",
         SpellSchool::NATURE, 20, 18, 7, true},
        {"Barkskin",        "+3 armor. Immune to poison for this floor.",
         SpellSchool::NATURE, 7, 3, 0, false},
        {"Swarm",           "Summon 4 rats (8 HP each).",
         SpellSchool::NATURE, 5, 0, -1, false},

        // --- Dark Arts (9) ---
        // Risk/reward. Drain heals. Fear controls. Doom kills anything. Blood Pact is permanent.
        {"Drain Life",      "Dmg 14+INT/3. Heal half dealt. Range 4.",
         SpellSchool::DARK_ARTS, 7, 14, 4, true},
        {"Fear",            "All enemies in range 5 flee.",
         SpellSchool::DARK_ARTS, 6, 0, 5, true},
        {"Raise Dead",      "Reanimate a skeleton ally (15 HP).",
         SpellSchool::DARK_ARTS, 12, 0, 3, false},
        {"Hex",             "Confuse nearest enemy 5 turns. Range 5.",
         SpellSchool::DARK_ARTS, 8, 0, 5, true},
        {"Soul Rend",       "Dmg 16+INT/3. Bleed 4 turns. Range 5.",
         SpellSchool::DARK_ARTS, 10, 16, 5, true},
        {"Darkness",        "Blind all in range 6 for 5 turns.",
         SpellSchool::DARK_ARTS, 12, 0, 6, true},
        {"Wither",          "Dmg 10+INT/3. -8 max HP permanently. Range 5.",
         SpellSchool::DARK_ARTS, 12, 10, 5, true},
        {"Blood Pact",      "Lose 25 HP. +6 damage, +3 armor permanently.",
         SpellSchool::DARK_ARTS, 0, 0, 0, false},
        {"Doom",            "Target dies in 5 turns. Range 4.",
         SpellSchool::DARK_ARTS, 24, 0, 4, true},
        // New spells
        {"Glacial Spike",   "Dmg 35+INT/3. Stun 2 turns. Range 6.",
         SpellSchool::CONJURATION, 16, 35, 6, true},
        {"Mirror Image",    "+30% dodge for 8 turns.",
         SpellSchool::TRANSMUTATION, 9, 0, 0, false},
        {"Farsight",        "Show all enemies and items on this floor.",
         SpellSchool::DIVINATION, 10, 0, 0, false},
        {"Mass Heal",       "Heal all friendly creatures in range 5 for 15 HP.",
         SpellSchool::HEALING, 14, 15, 5, false},
        {"Vine Prison",     "Root nearest enemy 3 turns. Apply bleed. Range 5.",
         SpellSchool::NATURE, 9, 0, 5, true},
        {"Soul Cage",       "Kill target below 20% HP. Gain +2 max HP. Range 4.",
         SpellSchool::DARK_ARTS, 18, 0, 4, true},
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
