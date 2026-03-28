#pragma once
#include <string>
#include <vector>

enum class SpellId : int {
    // Conjuration
    SPARK = 0,
    FORCE_BOLT,
    FIREBALL,
    // Transmutation
    HARDEN_SKIN,
    // Divination
    REVEAL_MAP,
    DETECT_MONSTERS,
    IDENTIFY,
    // Healing
    MINOR_HEAL,
    CURE_POISON,
    MAJOR_HEAL,
    // Nature
    ENTANGLE,
    BEAST_CALL,
    // Dark Arts
    DRAIN_LIFE,
    FEAR,
    RAISE_DEAD,
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
    const char* description; // vague, Dark Souls style
    SpellSchool school;
    int mp_cost;
    int base_power;    // damage, heal amount, or effect strength
    int range;         // 0 = self, -1 = all visible, >0 = tile range
    bool hostile;      // targets enemies?
};

inline const SpellInfo& get_spell_info(SpellId id) {
    static const SpellInfo SPELLS[] = {
        // Conjuration
        {"Spark",          "A jolt of something not quite lightning.",
         SpellSchool::CONJURATION, 3, 6, 5, true},
        {"Force Bolt",     "Raw force, compressed and hurled.",
         SpellSchool::CONJURATION, 5, 12, 6, true},
        {"Fireball",       "A pale fire that remembers what it burns.",
         SpellSchool::CONJURATION, 10, 20, 7, true},
        // Transmutation
        {"Harden Skin",    "Your flesh forgets that it is soft.",
         SpellSchool::TRANSMUTATION, 6, 4, 0, false},
        // Divination
        {"Reveal Map",     "The walls tell you their secrets.",
         SpellSchool::DIVINATION, 8, 0, -1, false},
        {"Detect Monsters","You feel the weight of nearby things.",
         SpellSchool::DIVINATION, 4, 0, -1, false},
        {"Identify",       "You see what a thing truly is.",
         SpellSchool::DIVINATION, 5, 0, 0, false},
        // Healing
        {"Minor Heal",     "Flesh knits. Not well, but enough.",
         SpellSchool::HEALING, 4, 10, 0, false},
        {"Cure Poison",    "The venom loses its way.",
         SpellSchool::HEALING, 5, 0, 0, false},
        {"Major Heal",     "Something old remembers how you were before the wound.",
         SpellSchool::HEALING, 10, 25, 0, false},
        // Nature
        {"Entangle",       "The ground remembers being a forest.",
         SpellSchool::NATURE, 6, 3, 5, true},
        {"Beast Call",     "Something answers. It may not be friendly.",
         SpellSchool::NATURE, 8, 0, -1, false},
        // Dark Arts
        {"Drain Life",     "You take what isn't yours. It sustains.",
         SpellSchool::DARK_ARTS, 6, 10, 4, true},
        {"Fear",           "They see what you've already seen.",
         SpellSchool::DARK_ARTS, 5, 0, 5, true},
        {"Raise Dead",     "It gets up. It shouldn't, but it does.",
         SpellSchool::DARK_ARTS, 12, 0, 3, false},
    };
    return SPELLS[static_cast<int>(id)];
}

// Component: tracks which spells an entity knows
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
