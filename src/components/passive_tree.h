#pragma once
#include <vector>
#include <array>
#include <cstdint>
#include <cstring>
#include "components/class_def.h"
#include "components/stats.h"
#include "components/skills.h"

// ── Sectors ──────────────────────────────────────────────────────────
enum class Sector : int {
    MIGHT = 0, FINESSE, ARCANE, FAITH,
    FORTITUDE, NATURE, SHADOW, VENOM,
    CENTER,  // utility hub
    COUNT
};

inline const char* sector_name(Sector s) {
    static const char* NAMES[] = {
        "Might", "Finesse", "Arcane", "Faith",
        "Fortitude", "Nature", "Shadow", "Venom",
        "Center"
    };
    return NAMES[static_cast<int>(s)];
}

// ── Node visual types ────────────────────────────────────────────────
enum class NodeType : int {
    SMALL,      // +1 stat, connector
    NOTABLE,    // named passive
    KEYSTONE,   // build-defining trade-off
    CAPSTONE,   // deepest in sector, active ability
};

// ── Effect types ─────────────────────────────────────────────────────
// Each node applies one or more effects when allocated.
enum class EffectType : int {
    NONE = 0,

    // Flat stat bonuses
    BONUS_STR, BONUS_DEX, BONUS_CON, BONUS_INT, BONUS_WIL, BONUS_PER, BONUS_CHA,
    BONUS_HP, BONUS_MP, BONUS_SPEED, BONUS_DAMAGE, BONUS_ARMOR,

    // Percentage bonuses (stored as integer percent, e.g. 15 = 15%)
    BONUS_CRIT_CHANCE,        // +N% crit
    BONUS_DODGE_CHANCE,       // +N% dodge
    BONUS_MELEE_DAMAGE_PCT,   // +N% melee damage
    BONUS_SPELL_DAMAGE_PCT,   // +N% spell damage
    BONUS_FIRE_RESIST,        // +N% fire resist
    BONUS_POISON_RESIST,      // +N% poison resist
    BONUS_BLEED_RESIST,       // +N% bleed resist
    BONUS_ALL_RESIST,         // +N% all resists

    // Conditional passives (value = magnitude, trigger is implicit)
    ON_KILL_HEAL_PCT,         // heal N% max HP on kill
    ON_HIT_BLEED_CHANCE,      // N% chance to apply bleed on hit
    ON_HIT_POISON_CHANCE,     // N% chance to apply poison on hit
    ON_CRIT_BONUS_DAMAGE,     // +N flat damage on crit
    LOW_HP_DAMAGE_BONUS,      // +N% damage when below 30% HP
    DAMAGE_VS_LOW_HP,         // +N% damage vs enemies below 30% HP

    // Resource management
    SPELL_COST_REDUCE_PCT,    // -N% spell MP cost
    PRAYER_COST_REDUCE_PCT,   // -N% prayer favor cost
    REST_EFFICIENCY,          // +N% rest healing
    POTION_EFFECTIVENESS,     // +N% potion healing
    XP_GAIN_BONUS,            // +N% XP gained

    // Utility
    TRAP_DETECTION,           // +N to trap detection (PER-like)
    IDENTIFY_ON_PICKUP_PCT,   // N% chance to auto-identify items on pickup
    BONUS_FOV,                // +N FOV radius

    // Notable mechanics (flag, value = magnitude or 1 = enabled)
    RIPOSTE,                  // counter-attack on dodge (1 = enabled)
    LAST_STAND,               // survive lethal at 1 HP, once per floor (1 = enabled)
    MANA_SIPHON,              // killing blows restore N% max MP
    SPELL_PIERCE,             // spells ignore N armor
    PATIENT_HUNTER,           // +N% damage vs unaware enemies

    // Class verb amplifiers (boost specific class mechanics)
    STATUS_DURATION_BONUS,    // +N turns to all applied status effects (Schema Monk, Serpentine)
    SHAPESHIFT_DAMAGE_BONUS,  // +N damage during beast/transform form (Druid)
    FURY_CHAIN_BONUS,         // +N fury turns per kill (War Cleric)
    SIPHON_BONUS,             // +N% MP restored on kill (Warlock)
    EXPLODE_DAMAGE_BONUS,     // +N corpse explode damage (Necromancer)
    COUNTER_DAMAGE_BONUS,     // +N% parry/riposte damage (Fighter, Knight)
    STEALTH_OPENER_BONUS,     // +N% shadow step/ambush damage (Rogue, Bandit)
    BREATH_DAMAGE_BONUS,      // +N breath/eruption damage (Wyrmkin)

    // Keystones (trade-off flags, value usually 1 = active)
    KS_BLOOD_MAGIC,           // spells cost HP, +30% spell power
    KS_GHOST_BLADE,           // attacks scale INT, deal magic damage
    KS_ZEALOT,                // prayer power 2x, favor decays 2x
    KS_IRON_REFLEXES,         // dodge -> armor conversion
    KS_CHAOS_INOCULATION,     // immune poison/disease, halve max HP
    KS_POINT_BLANK,           // +50% dmg at range 1, -50% at range 5+
    KS_AVATAR_OF_WILD,        // summons get your stats, can't wear armor
    KS_VAMPIRIC_PACT,         // all healing from damage dealt

    // Capstone active abilities (value = cooldown in turns)
    CAP_WHIRLWIND,            // attack all adjacent
    CAP_TIME_SLIP,            // 3 actions in 1 turn
    CAP_ARCANE_OVERLOAD,      // next spell free + 2x damage
    CAP_DIVINE_INTERVENTION,  // full heal + cleanse, once per floor
    CAP_UNBREAKABLE,          // halve damage for 8 turns
    CAP_ASPECT_OF_BEAST,      // transform: +5 all stats, natural attacks
    CAP_DEATH_MARK,           // mark target, next hit = guaranteed 3x crit
    CAP_PANDEMIC,             // all DoTs spread to nearby enemies

    EFFECT_COUNT
};

// Is this effect a keystone?
inline bool is_keystone_effect(EffectType e) {
    return e >= EffectType::KS_BLOOD_MAGIC && e <= EffectType::KS_VAMPIRIC_PACT;
}

// Is this effect a capstone active ability?
inline bool is_capstone_effect(EffectType e) {
    return e >= EffectType::CAP_WHIRLWIND && e <= EffectType::CAP_PANDEMIC;
}

// ── Single node effect ───────────────────────────────────────────────
struct NodeEffect {
    EffectType type = EffectType::NONE;
    int value = 0;  // magnitude (flat or percent depending on type)
};

// ── Passive node definition (static data) ────────────────────────────
// Max 4 effects per node, max 6 connections per node.
struct PassiveNode {
    uint16_t id;
    NodeType type;
    Sector sector;
    const char* name;           // nullptr for unnamed small nodes
    const char* description;    // tooltip text

    // Layout position (abstract 2D, scaled at render time)
    float x, y;

    // Effects applied when allocated
    NodeEffect effects[4];

    // Connections to other nodes (by id). 0xFFFF = unused.
    uint16_t connections[6];

    // Optional skill requirement (SKILL_COUNT = no requirement)
    int required_skill = SKILL_COUNT;  // SkillId as int, SKILL_COUNT = none
    int required_skill_level = 0;      // minimum level needed
};

constexpr uint16_t NO_CONN = 0xFFFF;

// ── Allocated tree state (per-character component) ───────────────────
struct PassiveTreeState {
    // Which nodes are allocated (bitfield, supports up to 512 nodes)
    static constexpr int MAX_NODES = 512;
    uint64_t allocated[MAX_NODES / 64] = {};

    int points_spent = 0;
    int points_available = 0;
    uint16_t start_node = 0;  // set from class

    // Capstone cooldowns (indexed by EffectType - CAP_WHIRLWIND)
    static constexpr int MAX_CAPSTONES = 8;
    int capstone_cooldowns[MAX_CAPSTONES] = {};

    bool is_allocated(uint16_t node_id) const {
        if (node_id >= MAX_NODES) return false;
        return (allocated[node_id / 64] >> (node_id % 64)) & 1;
    }

    void allocate(uint16_t node_id) {
        if (node_id >= MAX_NODES) return;
        allocated[node_id / 64] |= (1ULL << (node_id % 64));
        points_spent++;
        points_available--;
    }

    void deallocate(uint16_t node_id) {
        if (node_id >= MAX_NODES) return;
        allocated[node_id / 64] &= ~(1ULL << (node_id % 64));
        points_spent--;
        points_available++;
    }

    void grant_point() { points_available++; }

    void clear() {
        std::memset(allocated, 0, sizeof(allocated));
        points_spent = 0;
        points_available = 0;
        std::memset(capstone_cooldowns, 0, sizeof(capstone_cooldowns));
    }
};

// ── Tree queries (operate on static node data + state) ───────────────
// Defined in passive_tree.cpp
namespace passive_tree {

// Get full static node array and count
const PassiveNode* nodes();
int node_count();

// Find node by id (returns nullptr if invalid)
const PassiveNode* find_node(uint16_t id);

// Can a node be allocated right now? (checks points, connectivity, not already allocated)
bool can_allocate(const PassiveTreeState& state, uint16_t node_id);

// Check if a node's skill requirement is met. Returns true if no requirement or met.
bool skill_requirement_met(uint16_t node_id, const Skills& skills);

// Is a node reachable (connected to an allocated node or is start)?
bool is_connected(const PassiveTreeState& state, uint16_t node_id);

// Get the start node for a class
uint16_t start_node_for_class(ClassId cls);

// Compute aggregate stat bonuses from all allocated nodes.
// Called once on load and after each allocation to cache derived stats.
struct TreeBonuses {
    int str = 0, dex = 0, con = 0, intel = 0, wil = 0, per = 0, cha = 0;
    int hp = 0, mp = 0, speed = 0, damage = 0, armor = 0;
    int crit_chance = 0;      // percent
    int dodge_chance = 0;     // percent
    int melee_dmg_pct = 0;    // percent
    int spell_dmg_pct = 0;    // percent
    int fire_resist = 0, poison_resist = 0, bleed_resist = 0;
    int spell_cost_reduce = 0;
    int on_kill_heal_pct = 0;
    int on_hit_bleed_chance = 0;
    int on_hit_poison_chance = 0;
    int on_crit_bonus_dmg = 0;
    int low_hp_dmg_bonus = 0;
    int dmg_vs_low_hp = 0;
    int rest_efficiency = 0;
    int potion_effectiveness = 0;
    int xp_gain_bonus = 0;
    int trap_detection = 0;
    int identify_on_pickup = 0;
    int fov_bonus = 0;
    // Class verb amplifiers
    int status_duration_bonus = 0;
    int shapeshift_dmg_bonus = 0;
    int fury_chain_bonus = 0;
    int siphon_bonus_pct = 0;
    int explode_dmg_bonus = 0;
    int counter_dmg_bonus_pct = 0;
    int stealth_opener_bonus_pct = 0;
    int breath_dmg_bonus = 0;
    // Notable mechanics
    bool riposte = false;
    bool last_stand = false;
    bool last_stand_used = false; // per-floor tracking (reset on floor change)
    int mana_siphon_pct = 0;
    int spell_pierce = 0;
    int patient_hunter_pct = 0;
    // Keystone flags
    bool blood_magic = false;
    bool ghost_blade = false;
    bool zealot = false;
    bool iron_reflexes = false;
    bool chaos_inoculation = false;
    bool point_blank = false;
    bool avatar_of_wild = false;
    bool vampiric_pact = false;
    // Capstone active abilities (cooldown value, 0 = not owned)
    int cap_whirlwind_cd = 0;
    int cap_time_slip_cd = 0;
    int cap_arcane_overload_cd = 0;
    int cap_divine_intervention_cd = 0;
    int cap_unbreakable_cd = 0;
    int cap_aspect_of_beast_cd = 0;
    int cap_death_mark_cd = 0;
    int cap_pandemic_cd = 0;
};

TreeBonuses compute_bonuses(const PassiveTreeState& state);

} // namespace passive_tree
