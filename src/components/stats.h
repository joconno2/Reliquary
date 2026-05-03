#pragma once
#include <string>
#include <array>

// Primary attributes
enum class Attr : int {
    STR = 0, DEX, CON, INT, WIL, PER, CHA,
    COUNT
};

constexpr int ATTR_COUNT = static_cast<int>(Attr::COUNT);

struct Stats {
    std::string name = "Unknown";

    // Primary attributes (base values, before modifiers)
    std::array<int, ATTR_COUNT> attributes = {10, 10, 10, 10, 10, 10, 10};

    // Vitals
    int hp = 20;
    int hp_max = 20;
    int base_hp_max = 20;    // hp_max before equipment bonuses
    int mp = 0;
    int mp_max = 0;
    int base_mp_max = 0;     // mp_max before equipment bonuses

    // Combat
    int base_damage = 1;     // unarmed / natural weapon damage
    int natural_armor = 0;   // innate protection (monsters, traits)
    int base_speed = 100;    // energy gained per tick (100 = normal)

    // Equipment attribute bonuses (recalculated each turn from equipped items)
    int equip_str = 0;
    int equip_dex = 0;
    int equip_con = 0;
    int equip_hp = 0;
    int equip_mp = 0;
    int equip_speed = 0;

    // Level
    int level = 1;
    int xp = 0;
    int xp_next = 100;

    // Convenience
    int attr(Attr a) const { return attributes[static_cast<int>(a)]; }
    void set_attr(Attr a, int val) { attributes[static_cast<int>(a)] = val; }

    // XP reward when killed (monsters only)
    int xp_value = 0;

    // Resistances (percentage reduction, 0-100)
    int fire_resist = 0;
    int poison_resist = 0;
    int bleed_resist = 0;

    // God prayer / passive status fields
    int invisible_turns = 0;   // Zhavek: invisible until attack or expiry
    int phase_turns = 0;       // Lethis: walk through walls
    int wyrmkin_breath_ctr = 0; // Wyrmkin: counts hits toward dragon breath
    int sleep_turns = 0;       // Lethis: sleeping, skip turns
    int drown_turns = 0;       // Thalara: taking drown damage over time
    int drown_damage = 0;      // Thalara: damage per drown tick
    int unyielding_turns = 0;  // Ossren: armor doubled
    int stone_skin_turns = 0;  // Gathruun: bonus armor, can't move
    int stone_skin_armor = 0;  // Gathruun: armor bonus while stone skin active
    int haste_turns = 0;       // kill haste unique: bonus speed

    // Effective attribute (base + equipment)
    int eff_attr(Attr a) const {
        int val = attr(a);
        switch (a) {
            case Attr::STR: val += equip_str; break;
            case Attr::DEX: val += equip_dex; break;
            case Attr::CON: val += equip_con; break;
            default: break;
        }
        return val;
    }

    // Derived combat stats (use effective attributes)
    int melee_attack() const { return eff_attr(Attr::STR) + level; }
    int melee_damage() const { return base_damage + eff_attr(Attr::STR) / 3; }
    int dodge_value() const { return eff_attr(Attr::DEX) / 2; }
    int protection() const { return natural_armor; }
    int fov_radius() const { return 8 + eff_attr(Attr::PER) / 3 + fov_bonus; }
    int effective_speed() const { return base_speed + equip_speed; }

    // Equipment FOV bonus (from unique items)
    int fov_bonus = 0;

    // Passive tree XP bonus (percent, e.g. 10 = +10%)
    int xp_bonus_pct = 0;

    // Grant XP — returns true if leveled up
    bool grant_xp(int amount) {
        if (xp_bonus_pct > 0) amount = amount * (100 + xp_bonus_pct) / 100;
        xp += amount;
        if (xp >= xp_next) {
            level++;
            xp -= xp_next;
            xp_next = level * level * 56 + 56;
            // No free HP/MP on level — real gains come from level-up choice screen only
            return true;
        }
        return false;
    }
};
