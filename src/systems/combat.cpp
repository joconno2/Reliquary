#include "systems/combat.h"
#include "components/stats.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/ai.h"
#include "components/energy.h"
#include "components/player.h"
#include "components/corpse.h"
#include "components/death_anim.h"
#include "components/inventory.h"
#include "components/item.h"
#include "components/quest_target.h"
#include "components/status_effect.h"
#include "components/passive_tree.h"
#include "components/skills.h"
#include "components/tenet.h"
#include "components/prayer.h"  // is_undead, is_animal
#include "core/spritesheet.h"
#include <cstdio>
#include <algorithm>

// Get passive tree bonuses for an entity (zeroed if no tree)
static passive_tree::TreeBonuses get_tree_bonuses(World& world, Entity e) {
    if (world.has<PassiveTreeState>(e)) {
        return passive_tree::compute_bonuses(world.get<PassiveTreeState>(e));
    }
    return {};
}

// Calculate total equipment bonuses for an entity
static void get_equip_bonuses(World& world, Entity e,
                                int& dmg, int& armor, int& atk, int& dodge) {
    dmg = armor = atk = dodge = 0;
    if (!world.has<Inventory>(e)) return;
    auto& inv = world.get<Inventory>(e);
    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
        Entity eq = inv.equipped[s];
        if (eq == NULL_ENTITY || !world.has<Item>(eq)) continue;
        auto& item = world.get<Item>(eq);
        dmg += item.damage_bonus;
        armor += item.armor_bonus;
        atk += item.attack_bonus;
        dodge += item.dodge_bonus;
    }
}

// Calculate total equipment attribute bonuses (STR, DEX, CON, HP, MP, speed)
static void get_equip_attr_bonuses(World& world, Entity e,
                                     int& str, int& dex, int& con,
                                     int& hp, int& mp, int& speed) {
    str = dex = con = hp = mp = speed = 0;
    if (!world.has<Inventory>(e)) return;
    auto& inv = world.get<Inventory>(e);
    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
        Entity eq = inv.equipped[s];
        if (eq == NULL_ENTITY || !world.has<Item>(eq)) continue;
        auto& item = world.get<Item>(eq);
        str += item.str_bonus;
        dex += item.dex_bonus;
        con += item.con_bonus;
        hp += item.affix_hp;
        mp += item.affix_mp;
        speed += item.affix_speed;
    }
}


// (get_weapon_unique and apply_xp_bonus moved inside namespace below)

// Get equipped weapon name (for atmospheric messages)
static const char* get_weapon_name(World& world, Entity e) {
    if (!world.has<Inventory>(e)) return nullptr;
    auto& inv = world.get<Inventory>(e);
    Entity weapon = inv.get_equipped(EquipSlot::MAIN_HAND);
    if (weapon == NULL_ENTITY || !world.has<Item>(weapon)) return nullptr;
    return world.get<Item>(weapon).name.c_str();
}

static const char* random_body_part(RNG& rng) {
    static const char* parts[] = {
        "flank", "side", "chest", "shoulder", "arm", "leg", "midsection", "ribs"
    };
    return parts[rng.range(0, 7)];
}

namespace combat {

bool has_unique_effect(World& world, Entity e, UniqueEffect ue) {
    if (!world.has<Inventory>(e)) return false;
    auto& inv = world.get<Inventory>(e);
    for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
        Entity eq = inv.equipped[s];
        if (eq == NULL_ENTITY || !world.has<Item>(eq)) continue;
        if (world.get<Item>(eq).unique_effect == ue) return true;
    }
    return false;
}

static UniqueEffect get_weapon_unique(World& world, Entity e) {
    if (!world.has<Inventory>(e)) return UniqueEffect::NONE;
    Entity wpn = world.get<Inventory>(e).get_equipped(EquipSlot::MAIN_HAND);
    if (wpn == NULL_ENTITY || !world.has<Item>(wpn)) return UniqueEffect::NONE;
    return world.get<Item>(wpn).unique_effect;
}

static int apply_xp_bonus(World& world, Entity e, int xp) {
    if (has_unique_effect(world, e, UniqueEffect::XP_BONUS))
        return xp * 125 / 100;
    return xp;
}

Entity entity_at(World& world, int x, int y, Entity ignore) {
    auto& positions = world.pool<Position>();
    for (size_t i = 0; i < positions.size(); i++) {
        Entity e = positions.entity_at(i);
        if (e == ignore) continue;
        auto& pos = positions.at_index(i);
        if (pos.x == x && pos.y == y && world.has<Stats>(e)) {
            return e;
        }
    }
    return NULL_ENTITY;
}

AttackResult melee_attack(World& world, Entity attacker, Entity defender,
                           RNG& rng, MessageLog& log) {
    AttackResult result;

    if (!world.has<Stats>(attacker) || !world.has<Stats>(defender)) return result;

    auto& atk = world.get<Stats>(attacker);
    auto& def = world.get<Stats>(defender);

    // Equipment bonuses
    int atk_eq_dmg, atk_eq_arm, atk_eq_atk, atk_eq_dodge;
    int def_eq_dmg, def_eq_arm, def_eq_atk, def_eq_dodge;
    get_equip_bonuses(world, attacker, atk_eq_dmg, atk_eq_arm, atk_eq_atk, atk_eq_dodge);
    get_equip_bonuses(world, defender, def_eq_dmg, def_eq_arm, def_eq_atk, def_eq_dodge);

    // Equipment attribute bonuses (from affixes and relics)
    int atk_eq_str, atk_eq_dex, atk_eq_con, atk_eq_hp, atk_eq_mp, atk_eq_spd;
    int def_eq_str, def_eq_dex, def_eq_con, def_eq_hp, def_eq_mp, def_eq_spd;
    get_equip_attr_bonuses(world, attacker, atk_eq_str, atk_eq_dex, atk_eq_con, atk_eq_hp, atk_eq_mp, atk_eq_spd);
    get_equip_attr_bonuses(world, defender, def_eq_str, def_eq_dex, def_eq_con, def_eq_hp, def_eq_mp, def_eq_spd);

    // Passive tree bonuses
    auto atk_tree = get_tree_bonuses(world, attacker);
    auto def_tree = get_tree_bonuses(world, defender);

    // Ghost Blade: melee scales with INT instead of STR
    // Note: melee_attack() and melee_damage() already include equip_str via eff_attr()
    int atk_melee = atk_tree.ghost_blade
        ? (atk.eff_attr(Attr::INT) + atk.level)
        : atk.melee_attack();
    int atk_melee_dmg = atk_tree.ghost_blade
        ? (atk.base_damage + atk.eff_attr(Attr::INT) / 3)
        : atk.melee_damage();

    // Iron Reflexes: defender converts dodge to armor
    // Note: dodge_value() already includes equip_dex via eff_attr()
    int def_dodge = def.dodge_value() + def_eq_dodge;
    // Dodge skill bonus
    if (world.has<Player>(defender) && world.has<Skills>(defender))
        def_dodge += skill_bonus::dodge_bonus(world.get<Skills>(defender).get_level(SkillId::DODGE));
    int def_armor_bonus = 0;
    if (def_tree.iron_reflexes) {
        def_armor_bonus = def_dodge + def_tree.dodge_chance;
        def_dodge = 0; // no dodging
    }

    // Attack roll
    int raw_roll = rng.range(1, 20);
    int attack_roll = raw_roll + atk_melee + atk_eq_atk;
    int defense_roll = 10 + def_dodge;

    // Tree dodge chance (disabled by Iron Reflexes)
    bool tree_dodged = false;
    if (!def_tree.iron_reflexes && def_tree.dodge_chance > 0 &&
        rng.range(1, 100) <= def_tree.dodge_chance) {
        tree_dodged = true;
    }

    // Death Mark: auto-hit, auto-crit, 3x damage
    bool death_marked = false;
    if (world.has<Player>(attacker) && world.has<PassiveTreeState>(attacker)) {
        auto& atk_state = world.get<PassiveTreeState>(attacker);
        int dm_idx = static_cast<int>(EffectType::CAP_DEATH_MARK) - static_cast<int>(EffectType::CAP_WHIRLWIND);
        if (dm_idx >= 0 && dm_idx < PassiveTreeState::MAX_CAPSTONES && atk_state.capstone_cooldowns[dm_idx] == -1) {
            death_marked = true;
            atk_state.capstone_cooldowns[dm_idx] = 20; // start real cooldown
        }
    }

    bool natural_20 = (raw_roll == 20);

    // Knight: Shield Wall (30% block if shield equipped)
    bool knight_blocked = false;
    if (world.has<Player>(defender) &&
        world.get<Player>(defender).class_id == ClassId::KNIGHT &&
        world.has<Inventory>(defender)) {
        Entity shield = world.get<Inventory>(defender).get_equipped(EquipSlot::OFF_HAND);
        if (shield != NULL_ENTITY && world.has<Item>(shield) &&
            world.get<Item>(shield).type == ItemType::SHIELD) {
            if (rng.range(1, 100) <= 30) {
                knight_blocked = true;
                log.add("Shield blocks!", {200, 200, 255, 255});
            }
        }
    }

    if (knight_blocked) {
        result.hit = false;
        result.damage = 0;
    } else if (death_marked || (!tree_dodged && (attack_roll >= defense_roll || natural_20))) {
        result.hit = true;

        // Wraith: immune to non-silver/non-magical weapons
        if (world.has<AI>(defender) && world.get<AI>(defender).behavior == BehaviorType::WRAITH) {
            bool can_harm = false;
            // Check weapon material
            if (world.has<Inventory>(attacker)) {
                Entity wpn = world.get<Inventory>(attacker).get_equipped(EquipSlot::MAIN_HAND);
                if (wpn != NULL_ENTITY && world.has<Item>(wpn)) {
                    auto mat = world.get<Item>(wpn).material;
                    if (mat == MaterialType::SILVER || mat == MaterialType::MITHRIL
                        || mat == MaterialType::ADAMANTINE) {
                        can_harm = true;
                    }
                }
            }
            // Ghost Blade keystone: attacks are magical
            if (atk_tree.ghost_blade) can_harm = true;
            // Schema Monk elemental strikes are magical
            if (!can_harm && world.has<Player>(attacker)) {
                Entity wpn = NULL_ENTITY;
                if (world.has<Inventory>(attacker))
                    wpn = world.get<Inventory>(attacker).get_equipped(EquipSlot::MAIN_HAND);
                if (wpn == NULL_ENTITY && world.get<Player>(attacker).class_id == ClassId::SCHEMA_MONK)
                    can_harm = true;
            }
            if (!can_harm) {
                result.hit = false;
                result.damage = 0;
                if (world.has<Player>(attacker)) {
                    log.add("Your weapon passes through the wraith.", {140, 120, 160, 255});
                }
                return result;
            }
        }

        // Damage
        int dmg = atk_melee_dmg + atk_eq_dmg + atk_tree.damage;

        // Crit: natural 20, PER, tree crit, skill crit, or Death Mark
        int effective_crit = atk.eff_attr(Attr::PER) + atk_tree.crit_chance;
        // Blades/Archery skill crit bonus
        if (world.has<Player>(attacker) && world.has<Skills>(attacker)) {
            auto& skills = world.get<Skills>(attacker);
            effective_crit += skill_bonus::blades_crit(skills.get_level(SkillId::BLADES));
        }
        if (death_marked || natural_20 || rng.range(1, 100) <= effective_crit) {
            dmg *= death_marked ? 3 : 2;
            dmg += atk_tree.on_crit_bonus_dmg;
            result.critical = true;
        }

        // Percent melee damage bonus from tree
        if (atk_tree.melee_dmg_pct > 0) {
            dmg = dmg * (100 + atk_tree.melee_dmg_pct) / 100;
        }

        // Skill bonuses (attacker only)
        if (world.has<Player>(attacker) && world.has<Skills>(attacker)) {
            auto& skills = world.get<Skills>(attacker);
            // Determine weapon type from equipped weapon
            uint32_t wtags = 0;
            if (world.has<Inventory>(attacker)) {
                Entity wpn = world.get<Inventory>(attacker).get_equipped(EquipSlot::MAIN_HAND);
                if (wpn != NULL_ENTITY && world.has<Item>(wpn))
                    wtags = world.get<Item>(wpn).tags;
            }
            if (wtags & TAG_AXE)
                dmg += skill_bonus::axes_damage(skills.get_level(SkillId::AXES));
            if (wtags & TAG_BLUNT) {
                int stun_chance = skill_bonus::blunt_stun(skills.get_level(SkillId::BLUNT));
                if (stun_chance > 0 && rng.range(1, 100) <= stun_chance
                    && world.has<StatusEffects>(defender)) {
                    world.get<StatusEffects>(defender).add(StatusType::STUNNED, 0, 1);
                }
            }
            if (wtags == 0)
                dmg += skill_bonus::unarmed_damage(skills.get_level(SkillId::UNARMED));
        }

        // Bonus damage vs low HP targets
        if (atk_tree.dmg_vs_low_hp > 0 && def.hp <= def.hp_max * 30 / 100) {
            dmg = dmg * (100 + atk_tree.dmg_vs_low_hp) / 100;
        }

        // Low HP attacker bonus
        if (atk_tree.low_hp_dmg_bonus > 0 && atk.hp <= atk.hp_max * 30 / 100) {
            dmg = dmg * (100 + atk_tree.low_hp_dmg_bonus) / 100;
        }

        // Unique item effects: damage modifiers
        if (world.has<Player>(attacker)) {
            UniqueEffect wpn_ue = get_weapon_unique(world, attacker);
            if (wpn_ue == UniqueEffect::UNDEAD_SLAYER && is_undead(def.name.c_str()))
                dmg = dmg * 150 / 100;
            if (wpn_ue == UniqueEffect::BEAST_SLAYER && is_animal(def.name.c_str()))
                dmg = dmg * 150 / 100;
            if (wpn_ue == UniqueEffect::BACKSTAB_BONUS &&
                world.has<Player>(attacker)) {
                // Check if attack was from stealth (stealth flag on player)
                // The stealth system already doubles damage; this stacks
                // We just add another 50% if player was sneaking
            }
        }

        // Thorns: reflect damage to melee attackers (defender)
        if (world.has<Player>(defender) && has_unique_effect(world, defender, UniqueEffect::THORNS)) {
            atk.hp -= 3;
            if (atk.hp > 0) {
                char tbuf[128];
                snprintf(tbuf, sizeof(tbuf), "Thorns pierce the %s. (3)", atk.name.c_str());
                log.add(tbuf, {180, 140, 100, 255});
            }
        }

        dmg -= (def.protection() + def_eq_arm + def_tree.armor + def_armor_bonus);
        if (dmg < 1) dmg = 1;

        result.damage = dmg;
        def.hp -= dmg;

        // Execute threshold: instant kill enemies below 15% HP
        if (world.has<Player>(attacker) && !world.has<Player>(defender) &&
            get_weapon_unique(world, attacker) == UniqueEffect::EXECUTE_THRESHOLD) {
            if (def.hp > 0 && def.hp <= def.hp_max * 15 / 100) {
                def.hp = 0;
                log.add("Your weapon finishes the job.", {255, 160, 80, 255});
            }
        }

        // Last Stand: survive lethal hit at 1 HP (defender, once per floor)
        if (def.hp <= 0 && world.has<Player>(defender) && def_tree.last_stand) {
            auto& tree_state = world.get<PassiveTreeState>(defender);
            // Use capstone_cooldowns[0] as last_stand_used flag (0 = available)
            if (tree_state.capstone_cooldowns[0] == 0) {
                def.hp = 1;
                tree_state.capstone_cooldowns[0] = 1; // mark used this floor
                log.add("Last Stand! You refuse to fall.", {255, 220, 100, 255});
            }
        }

        // Deathward (unique item): survive lethal hit if Last Stand didn't already save
        if (def.hp <= 0 && world.has<Player>(defender) &&
            has_unique_effect(world, defender, UniqueEffect::DEATHWARD)) {
            def.hp = 1;
            log.add("Your ward flares. Death denied.", {255, 200, 255, 255});
        }

        // Chain lightning on hit (unique weapon effect)
        if (world.has<Player>(attacker) &&
            get_weapon_unique(world, attacker) == UniqueEffect::CHAIN_LIGHTNING &&
            rng.range(1, 100) <= 20 && world.has<Position>(defender)) {
            auto& dpos = world.get<Position>(defender);
            int chains = 0;
            auto& positions = world.pool<Position>();
            for (size_t pi = 0; pi < positions.size() && chains < 2; pi++) {
                Entity ce = positions.entity_at(pi);
                if (ce == defender || ce == attacker) continue;
                if (!world.has<Stats>(ce) || !world.has<AI>(ce)) continue;
                auto& cp = positions.at_index(pi);
                int dx = std::abs(cp.x - dpos.x);
                int dy = std::abs(cp.y - dpos.y);
                if (dx <= 2 && dy <= 2) {
                    auto& cs = world.get<Stats>(ce);
                    cs.hp -= 4;
                    chains++;
                    if (cs.hp <= 0) {
                        int xp = kill(world, ce, log);
                        if (world.has<Stats>(attacker)) {
                            world.get<Stats>(attacker).grant_xp(xp);
                        }
                    }
                }
            }
            if (chains > 0) {
                log.add("Lightning arcs between enemies.", {140, 180, 255, 255});
            }
        }

        // Corpse explode on kill (unique effect)
        if (def.hp <= 0 && world.has<Player>(attacker) &&
            has_unique_effect(world, attacker, UniqueEffect::CORPSE_EXPLODE) &&
            world.has<Position>(defender)) {
            auto& dpos = world.get<Position>(defender);
            auto& positions = world.pool<Position>();
            bool exploded = false;
            for (size_t pi = 0; pi < positions.size(); pi++) {
                Entity ce = positions.entity_at(pi);
                if (ce == defender || ce == attacker) continue;
                if (!world.has<Stats>(ce) || !world.has<AI>(ce)) continue;
                auto& cp = positions.at_index(pi);
                int dx = std::abs(cp.x - dpos.x);
                int dy = std::abs(cp.y - dpos.y);
                if (dx <= 2 && dy <= 2) {
                    world.get<Stats>(ce).hp -= 3;
                    exploded = true;
                }
            }
            if (exploded) {
                log.add("The corpse detonates.", {200, 100, 80, 255});
            }
        }

        // Schema Monk elemental strikes: cycle fire/ice/lightning on unarmed hits
        if (world.has<Player>(attacker) && def.hp > 0 &&
            world.get<Player>(attacker).class_id == ClassId::SCHEMA_MONK) {
            // Only when unarmed
            bool unarmed = true;
            if (world.has<Inventory>(attacker)) {
                Entity wpn = world.get<Inventory>(attacker).get_equipped(EquipSlot::MAIN_HAND);
                if (wpn != NULL_ENTITY) unarmed = false;
            }
            if (unarmed && world.has<StatusEffects>(defender)) {
                // Cycle based on total kills (persistent counter)
                static int elem_cycle = 0;
                int elem = elem_cycle % 3;
                elem_cycle++;
                int bonus = 2 + atk.eff_attr(Attr::INT) / 5;
                def.hp -= bonus;
                switch (elem) {
                    case 0: // Fire
                        world.get<StatusEffects>(defender).add(StatusType::BURN, 2, 2);
                        { char eb[80]; snprintf(eb, sizeof(eb), "Your fist ignites! +%d fire.", bonus);
                          log.add(eb, {255, 140, 40, 255}); }
                        break;
                    case 1: // Ice
                        world.get<StatusEffects>(defender).add(StatusType::FROZEN, 0, 1);
                        { char eb[80]; snprintf(eb, sizeof(eb), "Your fist freezes! +%d cold.", bonus);
                          log.add(eb, {140, 200, 255, 255}); }
                        break;
                    case 2: // Lightning
                        world.get<StatusEffects>(defender).add(StatusType::STUNNED, 0, 1);
                        { char eb[80]; snprintf(eb, sizeof(eb), "Your fist crackles! +%d shock.", bonus);
                          log.add(eb, {200, 200, 255, 255}); }
                        break;
                }
            }
        }

        // === CLASS ABILITIES (attacker, on-hit) ===
        if (world.has<Player>(attacker) && def.hp > 0) {
            auto cid = world.get<Player>(attacker).class_id;

            // Barbarian: Rage (+50% damage below 50% HP)
            if (cid == ClassId::BARBARIAN && atk.hp * 2 < atk.hp_max) {
                int rage_bonus = dmg / 2;
                def.hp -= rage_bonus;
                result.damage += rage_bonus;
                log.add("Rage!", {255, 80, 80, 255});
            }

            // Monk: Flurry (40% chance bonus hit at half damage)
            if (cid == ClassId::MONK) {
                bool unarmed_m = true;
                if (world.has<Inventory>(attacker)) {
                    Entity w = world.get<Inventory>(attacker).get_equipped(EquipSlot::MAIN_HAND);
                    if (w != NULL_ENTITY) unarmed_m = false;
                }
                if (unarmed_m && rng.range(1, 100) <= 40 && def.hp > 0) {
                    int bonus_dmg = std::max(1, dmg / 2);
                    def.hp -= bonus_dmg;
                    result.damage += bonus_dmg;
                    log.add("Flurry!", {220, 200, 140, 255});
                }
            }

            // Templar: Holy Smite (+6 vs undead, execute below 20%)
            if (cid == ClassId::TEMPLAR && world.has<Stats>(defender)) {
                auto& ds = world.get<Stats>(defender);
                if (is_undead(ds.name.c_str())) {
                    def.hp -= 6;
                    result.damage += 6;
                    if (def.hp > 0 && def.hp * 5 < def.hp_max) {
                        def.hp = 0; // execute
                        log.add("Holy smite! Purified.", {255, 255, 200, 255});
                    } else {
                        log.add("Holy smite!", {255, 255, 200, 255});
                    }
                }
            }

            // Elf: Fey Precision (20% chance for +4 bonus damage)
            if (cid == ClassId::ELF && rng.range(1, 100) <= 20) {
                def.hp -= 4;
                result.damage += 4;
                log.add("You find a weakness!", {180, 255, 180, 255});
            }

            // Bandit: Ambush (+5 damage on first strike vs full HP enemy)
            if (cid == ClassId::BANDIT && def.hp == def.hp_max - result.damage) {
                // Target was at full HP before this hit
                def.hp -= 5;
                result.damage += 5;
                log.add("Ambush!", {200, 180, 100, 255});
            }

            // Serpentine: Venom Strike (poison on hit)
            if (cid == ClassId::SERPENTINE && world.has<StatusEffects>(defender)) {
                if (rng.range(1, 100) <= 30) {
                    world.get<StatusEffects>(defender).add(StatusType::POISON, 3, 3);
                } else {
                    world.get<StatusEffects>(defender).add(StatusType::POISON, 2, 2);
                }
            }

            // Wyrmkin: Dragon Breath (every 8th hit, fire AoE)
            if (cid == ClassId::WYRMKIN) {
                // Uses static counter (persists within session)
                static int wyrmkin_ctr = 0;
                wyrmkin_ctr++;
                if (wyrmkin_ctr >= 8) {
                    wyrmkin_ctr = 0;
                    int breath_dmg = 6 + atk.level;
                    // Damage target extra
                    def.hp -= breath_dmg;
                    result.damage += breath_dmg;
                    // Damage adjacent enemies
                    if (world.has<Position>(defender)) {
                        auto& dpos = world.get<Position>(defender);
                        auto& ai_pool = world.pool<AI>();
                        for (size_t ai = 0; ai < ai_pool.size(); ai++) {
                            Entity ae = ai_pool.entity_at(ai);
                            if (ae == defender || ai_pool.at_index(ai).friendly) continue;
                            if (!world.has<Position>(ae) || !world.has<Stats>(ae)) continue;
                            auto& ap = world.get<Position>(ae);
                            if (std::abs(ap.x - dpos.x) <= 1 && std::abs(ap.y - dpos.y) <= 1) {
                                world.get<Stats>(ae).hp -= breath_dmg;
                            }
                        }
                    }
                    log.add("Dragon breath!", {255, 140, 40, 255});
                    if (world.has<StatusEffects>(defender))
                        world.get<StatusEffects>(defender).add(StatusType::BURN, 3, 2);
                }
            }
        }

        // Crit bleed (unique ring): crits apply 3-turn bleed
        if (result.critical && world.has<Player>(attacker) &&
            has_unique_effect(world, attacker, UniqueEffect::CRIT_BLEED) &&
            world.has<StatusEffects>(defender) && def.hp > 0) {
            world.get<StatusEffects>(defender).add(StatusType::BLEED, 2, 3);
            log.add("The wound bleeds freely.", {200, 80, 80, 255});
        }

        // Teleport strike (unique ring): 15% chance to blink behind target
        if (world.has<Player>(attacker) &&
            has_unique_effect(world, attacker, UniqueEffect::TELEPORT_STRIKE) &&
            rng.range(1, 100) <= 15 && world.has<Position>(defender) && world.has<Position>(attacker)) {
            auto& dpos2 = world.get<Position>(defender);
            auto& apos = world.get<Position>(attacker);
            // Move attacker to opposite side of defender
            // Flag for engine.cpp to handle the actual position swap
            // (combat.cpp doesn't have map access for bounds checking)
            result.teleport_behind = true;
            log.add("You blink through your target.", {180, 140, 255, 255});
        }

        // Kill haste (unique ring): +50 speed for 3 turns on kill
        if (def.hp <= 0 && world.has<Player>(attacker) &&
            has_unique_effect(world, attacker, UniqueEffect::KILL_HASTE) &&
            world.has<Stats>(attacker)) {
            world.get<Stats>(attacker).haste_turns = 3;
            log.add("Adrenaline surges. You move faster.", {200, 220, 140, 255});
        }

        // MP shield (unique amulet): damage from MP first (50%)
        if (def.hp < 0 && world.has<Player>(defender) &&
            has_unique_effect(world, defender, UniqueEffect::MP_SHIELD) &&
            def.mp > 0) {
            // Recover some HP by spending MP instead
            int absorbed = std::min(result.damage / 2, def.mp);
            if (absorbed > 0) {
                def.hp += absorbed;
                def.mp -= absorbed;
                log.add("Your ward absorbs the blow.", {140, 140, 220, 255});
            }
        }

        // Tree on-hit effects (player melee attacks only)
        if (world.has<Player>(attacker)) {
            if (atk_tree.on_hit_bleed_chance > 0 &&
                rng.range(1, 100) <= atk_tree.on_hit_bleed_chance &&
                world.has<StatusEffects>(defender)) {
                world.get<StatusEffects>(defender).add(StatusType::BLEED, 1, 5);
            }
            if (atk_tree.on_hit_poison_chance > 0 &&
                rng.range(1, 100) <= atk_tree.on_hit_poison_chance &&
                world.has<StatusEffects>(defender)) {
                world.get<StatusEffects>(defender).add(StatusType::POISON, 2, 4);
            }
            // Vampiric Pact: heal from damage dealt
            if (atk_tree.vampiric_pact && world.has<Stats>(attacker)) {
                auto& atk_s = world.get<Stats>(attacker);
                int heal = dmg / 3; // heal 33% of damage dealt
                if (heal > 0) atk_s.hp = std::min(atk_s.hp + heal, atk_s.hp_max);
            }

            // Affix on-hit procs (from equipped weapon)
            if (world.has<Inventory>(attacker)) {
                Entity wpn_e = world.get<Inventory>(attacker).get_equipped(EquipSlot::MAIN_HAND);
                if (wpn_e != NULL_ENTITY && world.has<Item>(wpn_e)) {
                    auto& wpn_item = world.get<Item>(wpn_e);
                    if (world.has<StatusEffects>(defender)) {
                        auto& dse = world.get<StatusEffects>(defender);
                        int ch;
                        ch = wpn_item.get_onhit_chance(AffixEffect::ONHIT_POISON);
                        if (ch > 0 && rng.range(1, 100) <= ch)
                            dse.add(StatusType::POISON, 2, 4);
                        ch = wpn_item.get_onhit_chance(AffixEffect::ONHIT_BURN);
                        if (ch > 0 && rng.range(1, 100) <= ch)
                            dse.add(StatusType::BURN, 2, 3);
                        ch = wpn_item.get_onhit_chance(AffixEffect::ONHIT_FREEZE);
                        if (ch > 0 && rng.range(1, 100) <= ch)
                            dse.add(StatusType::FROZEN, 0, 2);
                        ch = wpn_item.get_onhit_chance(AffixEffect::ONHIT_BLEED);
                        if (ch > 0 && rng.range(1, 100) <= ch)
                            dse.add(StatusType::BLEED, 1, 5);
                    }
                    // Lifesteal
                    int ls = wpn_item.get_onhit_chance(AffixEffect::ONHIT_LIFESTEAL);
                    if (ls > 0 && world.has<Stats>(attacker)) {
                        auto& atk_s2 = world.get<Stats>(attacker);
                        atk_s2.hp = std::min(atk_s2.hp + ls, atk_s2.hp_max);
                    }
                }
            }
        }

        // Atmospheric combat message
        bool attacker_is_player = world.has<Player>(attacker);
        bool defender_is_player = world.has<Player>(defender);
        const char* bp = random_body_part(rng);
        const char* wpn = get_weapon_name(world, attacker);

        if (result.critical) {
            char buf[256];
            if (attacker_is_player) {
                if (wpn) {
                    const char* msgs[] = {
                        "Your %s cleaves through the %s's guard. Critical! (%d)",
                        "Your %s bites deep into the %s. Critical! (%d)",
                        "A devastating blow — your %s tears into the %s. Critical! (%d)",
                    };
                    snprintf(buf, sizeof(buf), msgs[rng.range(0, 2)], wpn, def.name.c_str(), dmg);
                } else {
                    snprintf(buf, sizeof(buf),
                        "Your fist crashes into the %s with bone-breaking force. Critical! (%d)",
                        def.name.c_str(), dmg);
                }
                log.add(buf, {255, 200, 100, 255});
            } else {
                const char* msgs[] = {
                    "The %s strikes you with terrible force. Critical! (%d)",
                    "The %s's blow nearly staggers you. Critical! (%d)",
                    "A vicious strike from the %s tears into you. Critical! (%d)",
                };
                snprintf(buf, sizeof(buf), msgs[rng.range(0, 2)], atk.name.c_str(), dmg);
                log.add(buf, {255, 80, 80, 255});
            }
        } else {
            char buf[256];
            if (attacker_is_player) {
                if (wpn) {
                    const char* msgs[] = {
                        "Your %s finds the %s's %s. (%d)",
                        "You drive your %s into the %s's %s. (%d)",
                        "Your %s catches the %s across the %s. (%d)",
                        "You land a solid hit on the %s with your %s. (%d)",
                        "Your %s strikes the %s's %s. (%d)",
                    };
                    int pick = rng.range(0, 4);
                    if (pick == 3)
                        snprintf(buf, sizeof(buf), msgs[3], def.name.c_str(), wpn, dmg);
                    else
                        snprintf(buf, sizeof(buf), msgs[pick], wpn, def.name.c_str(), bp, dmg);
                } else {
                    const char* msgs[] = {
                        "Your fist connects with the %s's %s. (%d)",
                        "You punch the %s in the %s. (%d)",
                    };
                    snprintf(buf, sizeof(buf), msgs[rng.range(0, 1)], def.name.c_str(), bp, dmg);
                }
                log.add(buf, {200, 180, 160, 255});
            } else if (defender_is_player) {
                const char* msgs[] = {
                    "The %s's blow catches your %s. (%d)",
                    "The %s rakes you across the %s. (%d)",
                    "The %s strikes your %s. (%d)",
                    "Pain flares as the %s connects. (%d)",
                    "The %s lands a hit on your %s. (%d)",
                };
                int pick = rng.range(0, 4);
                if (pick == 3)
                    snprintf(buf, sizeof(buf), msgs[3], atk.name.c_str(), dmg);
                else
                    snprintf(buf, sizeof(buf), msgs[pick], atk.name.c_str(), bp, dmg);
                log.add(buf, {255, 120, 120, 255});
            }
        }

        // Check death
        if (def.hp <= 0) {
            result.killed = true;
            if (defender_is_player) {
                log.add("You die.", {255, 50, 50, 255});
            } else {
                char buf[256];
                const char* death_msgs[] = {
                    "The %s crumples to the ground.",
                    "The %s collapses in a heap.",
                    "The %s falls and does not rise.",
                    "The %s staggers, then drops.",
                };
                snprintf(buf, sizeof(buf), death_msgs[rng.range(0, 3)], def.name.c_str());
                log.add(buf, {180, 160, 140, 255});

                int xp = kill(world, defender, log);
                // Grant XP to attacker if they're the player
                if (attacker_is_player && world.has<Stats>(attacker) && xp > 0) {
                    xp = apply_xp_bonus(world, attacker, xp);
                    auto& atk_stats = world.get<Stats>(attacker);
                    if (atk_stats.grant_xp(xp)) {
                        char lvl_buf[64];
                        snprintf(lvl_buf, sizeof(lvl_buf),
                            "You reach level %d.", atk_stats.level);
                        log.add(lvl_buf, {255, 220, 100, 255});
                    }
                }
                // Tree: on-kill heal
                if (attacker_is_player && atk_tree.on_kill_heal_pct > 0 &&
                    world.has<Stats>(attacker)) {
                    auto& atk_s = world.get<Stats>(attacker);
                    int heal = atk_s.hp_max * atk_tree.on_kill_heal_pct / 100;
                    if (heal > 0) {
                        atk_s.hp = std::min(atk_s.hp + heal, atk_s.hp_max);
                    }
                }
                // Tree: Mana Siphon (restore MP on kill)
                if (attacker_is_player && atk_tree.mana_siphon_pct > 0 &&
                    world.has<Stats>(attacker)) {
                    auto& atk_s = world.get<Stats>(attacker);
                    int mp_restore = atk_s.mp_max * atk_tree.mana_siphon_pct / 100;
                    if (mp_restore > 0) {
                        atk_s.mp = std::min(atk_s.mp + mp_restore, atk_s.mp_max);
                    }
                }

                // Affix on-kill effects (from equipped weapon)
                if (attacker_is_player && world.has<Inventory>(attacker)) {
                    Entity wpn_e = world.get<Inventory>(attacker).get_equipped(EquipSlot::MAIN_HAND);
                    if (wpn_e != NULL_ENTITY && world.has<Item>(wpn_e)) {
                        auto& wpn_item = world.get<Item>(wpn_e);
                        int ok_heal = wpn_item.get_onkill_mag(AffixEffect::ONKILL_HEAL);
                        if (ok_heal > 0 && world.has<Stats>(attacker)) {
                            auto& atk_s = world.get<Stats>(attacker);
                            atk_s.hp = std::min(atk_s.hp + ok_heal, atk_s.hp_max);
                        }
                        int ok_mana = wpn_item.get_onkill_mag(AffixEffect::ONKILL_MANA);
                        if (ok_mana > 0 && world.has<Stats>(attacker)) {
                            auto& atk_s = world.get<Stats>(attacker);
                            atk_s.mp = std::min(atk_s.mp + ok_mana, atk_s.mp_max);
                        }
                    }
                }
            }
        }
    } else {
        // Miss — atmospheric
        bool attacker_is_player = world.has<Player>(attacker);
        const char* wpn = get_weapon_name(world, attacker);
        char buf[256];
        if (attacker_is_player) {
            if (wpn) {
                const char* msgs[] = {
                    "You slash at the %s — it sidesteps.",
                    "Your %s whistles past the %s.",
                    "The %s dodges your swing.",
                    "You lunge at the %s, but your %s finds only air.",
                    "Your swing goes wide of the %s.",
                };
                int pick = rng.range(0, 4);
                switch (pick) {
                    case 0: snprintf(buf, sizeof(buf), msgs[0], def.name.c_str()); break;
                    case 1: snprintf(buf, sizeof(buf), msgs[1], wpn, def.name.c_str()); break;
                    case 2: snprintf(buf, sizeof(buf), msgs[2], def.name.c_str()); break;
                    case 3: snprintf(buf, sizeof(buf), msgs[3], def.name.c_str(), wpn); break;
                    case 4: snprintf(buf, sizeof(buf), msgs[4], def.name.c_str()); break;
                }
            } else {
                snprintf(buf, sizeof(buf), "You swing at the %s but miss.", def.name.c_str());
            }
            log.add(buf, {140, 130, 120, 255});
        } else if (world.has<Player>(defender)) {
            const char* msgs[] = {
                "The %s lunges at you, you twist away.",
                "The %s's attack goes wide.",
                "You dodge the %s's strike.",
                "The %s swings wildly and misses.",
                "The %s's blow glances off harmlessly.",
            };
            snprintf(buf, sizeof(buf), msgs[rng.range(0, 4)], atk.name.c_str());
            log.add(buf, {160, 150, 140, 255});

            // Riposte: counter-attack on dodge
            if (def_tree.riposte && world.has<Stats>(attacker)) {
                int riposte_dmg = def.melee_damage() + def_eq_dmg + def_tree.damage;
                riposte_dmg -= (atk.protection() + atk_eq_arm);
                if (riposte_dmg < 1) riposte_dmg = 1;
                atk.hp -= riposte_dmg;
                char rbuf[128];
                snprintf(rbuf, sizeof(rbuf), "You riposte the %s! (%d)", atk.name.c_str(), riposte_dmg);
                log.add(rbuf, {200, 220, 140, 255});
                if (atk.hp <= 0) {
                    int xp = kill(world, attacker, log);
                    if (world.has<Player>(defender) && xp > 0) {
                        xp = apply_xp_bonus(world, defender, xp);
                        def.grant_xp(xp);
                    }
                    result.attacker_killed = true;
                }
            }
        }
    }

    // Dual wield: off-hand weapon gets a bonus attack at reduced damage
    if (result.hit && !result.killed && world.has<Player>(attacker) &&
        world.has<Inventory>(attacker) && world.has<Stats>(defender)) {
        Entity oh = world.get<Inventory>(attacker).get_equipped(EquipSlot::OFF_HAND);
        if (oh != NULL_ENTITY && world.has<Item>(oh) &&
            world.get<Item>(oh).type == ItemType::WEAPON) {
            auto& oh_item = world.get<Item>(oh);
            auto& def2 = world.get<Stats>(defender);
            int oh_roll = rng.range(1, 20);
            int oh_atk_roll = oh_roll + atk.eff_attr(Attr::DEX) + atk.level;
            if (oh_atk_roll >= 10 + def2.dodge_value() + def_eq_dodge || oh_roll == 20) {
                int oh_dmg = oh_item.damage_bonus / 2 + atk.eff_attr(Attr::DEX) / 4;
                oh_dmg -= (def2.protection() + def_eq_arm);
                if (oh_dmg < 1) oh_dmg = 1;
                def2.hp -= oh_dmg;
                char ob[128];
                snprintf(ob, sizeof(ob), "Off-hand %s strikes the %s. (%d)",
                         oh_item.name.c_str(), def2.name.c_str(), oh_dmg);
                log.add(ob, {200, 190, 160, 255});
                result.damage += oh_dmg;
                if (def2.hp <= 0) {
                    result.killed = true;
                    int xp = kill(world, defender, log);
                    if (xp > 0) {
                        xp = apply_xp_bonus(world, attacker, xp);
                        world.get<Stats>(attacker).grant_xp(xp);
                    }
                }
            } else {
                char ob[64];
                snprintf(ob, sizeof(ob), "Off-hand swing misses the %s.", def.name.c_str());
                log.add(ob, {140, 130, 120, 255});
            }
        }
    }

    // === CLASS ABILITIES (defender, after hit) ===
    if (result.hit && world.has<Player>(defender)) {
        auto dcid = world.get<Player>(defender).class_id;

        // Druid: Thorns (reflect damage on attacker)
        if (dcid == ClassId::DRUID && world.has<Stats>(attacker)) {
            int thorn_dmg = 2 + world.get<Stats>(defender).level / 3;
            world.get<Stats>(attacker).hp -= thorn_dmg;
            char tb[64]; snprintf(tb, sizeof(tb), "Thorns lash back! (%d)", thorn_dmg);
            log.add(tb, {80, 200, 80, 255});
        }

        // Heretic: Godless Resolve (15% status resist)
        // (status resist handled where statuses are applied, not here)
    }

    return result;
}

AttackResult ranged_attack(World& world, Entity attacker, Entity defender,
                            int weapon_damage, RNG& rng, MessageLog& log) {
    AttackResult result;
    if (!world.has<Stats>(attacker) || !world.has<Stats>(defender)) return result;

    auto& atk = world.get<Stats>(attacker);
    auto& def = world.get<Stats>(defender);

    // Equipment bonuses (defender only — weapon damage passed in directly)
    int def_eq_dmg, def_eq_arm, def_eq_atk, def_eq_dodge;
    get_equip_bonuses(world, defender, def_eq_dmg, def_eq_arm, def_eq_atk, def_eq_dodge);

    // Attack roll: d20 + DEX + level vs 10 + dodge + equip
    int raw_roll = rng.range(1, 20);
    int attack_roll = raw_roll + atk.eff_attr(Attr::DEX) + atk.level;
    int defense_roll = 10 + def.dodge_value() + def_eq_dodge;

    bool natural_20 = (raw_roll == 20);

    if (attack_roll >= defense_roll || natural_20) {
        result.hit = true;

        // Wraith: immune to non-silver ranged attacks
        if (world.has<AI>(defender) && world.get<AI>(defender).behavior == BehaviorType::WRAITH) {
            bool can_harm = false;
            // Check ranged weapon material (silver/mithril/adamantine can harm)
            if (world.has<Inventory>(attacker)) {
                Entity wpn = world.get<Inventory>(attacker).get_equipped(EquipSlot::MAIN_HAND);
                if (wpn != NULL_ENTITY && world.has<Item>(wpn)) {
                    auto mat = world.get<Item>(wpn).material;
                    if (mat == MaterialType::SILVER || mat == MaterialType::MITHRIL
                        || mat == MaterialType::ADAMANTINE)
                        can_harm = true;
                }
            }
            if (!can_harm) {
                result.hit = false;
                result.damage = 0;
                if (world.has<Player>(attacker))
                    log.add("Your arrow passes through the wraith.", {140, 120, 160, 255});
                return result;
            }
        }

        int dmg = weapon_damage + atk.eff_attr(Attr::DEX) / 3;

        // Point Blank keystone: +50% at range 1, -50% at range 5+
        if (world.has<Player>(attacker) && world.has<PassiveTreeState>(attacker)) {
            auto tb = passive_tree::compute_bonuses(world.get<PassiveTreeState>(attacker));
            if (tb.point_blank && world.has<Position>(attacker) && world.has<Position>(defender)) {
                auto& ap = world.get<Position>(attacker);
                auto& dp = world.get<Position>(defender);
                int dist = std::max(std::abs(dp.x - ap.x), std::abs(dp.y - ap.y));
                if (dist <= 1) dmg = dmg * 150 / 100;
                else if (dist >= 5) dmg = dmg * 50 / 100;
            }
        }

        int ranged_crit = atk.eff_attr(Attr::PER);
        if (world.has<Player>(attacker) && world.has<Skills>(attacker))
            ranged_crit += skill_bonus::archery_crit(world.get<Skills>(attacker).get_level(SkillId::ARCHERY));
        if (natural_20 || rng.range(1, 100) <= ranged_crit) {
            dmg *= 2;
            result.critical = true;
        }

        dmg -= (def.protection() + def_eq_arm);
        if (dmg < 1) dmg = 1;
        result.damage = dmg;
        def.hp -= dmg;

        bool attacker_is_player = world.has<Player>(attacker);
        bool defender_is_player = world.has<Player>(defender);
        const char* bp = random_body_part(rng);

        if (result.critical) {
            char buf[256];
            if (attacker_is_player) {
                const char* msgs[] = {
                    "Your arrow strikes the %s dead center. Critical! (%d)",
                    "Your shot punches through the %s's guard. Critical! (%d)",
                    "A perfect shot — the arrow buries itself in the %s. Critical! (%d)",
                };
                snprintf(buf, sizeof(buf), msgs[rng.range(0, 2)], def.name.c_str(), dmg);
                log.add(buf, {255, 200, 100, 255});
            } else {
                const char* msgs[] = {
                    "The %s's arrow pierces your %s. Critical! (%d)",
                    "An arrow from the %s strikes you dead on. Critical! (%d)",
                };
                int pick = rng.range(0, 1);
                if (pick == 0)
                    snprintf(buf, sizeof(buf), msgs[0], atk.name.c_str(), bp, dmg);
                else
                    snprintf(buf, sizeof(buf), msgs[1], atk.name.c_str(), dmg);
                log.add(buf, {255, 80, 80, 255});
            }
        } else {
            char buf[256];
            if (attacker_is_player) {
                const char* msgs[] = {
                    "Your arrow strikes the %s in the %s. (%d)",
                    "Your shot finds the %s's %s. (%d)",
                    "The arrow catches the %s in the %s. (%d)",
                };
                snprintf(buf, sizeof(buf), msgs[rng.range(0, 2)], def.name.c_str(), bp, dmg);
                log.add(buf, {200, 180, 160, 255});
            } else if (defender_is_player) {
                const char* msgs[] = {
                    "The %s's arrow catches your %s. (%d)",
                    "An arrow from the %s strikes your %s. (%d)",
                    "The %s's shot hits you in the %s. (%d)",
                };
                snprintf(buf, sizeof(buf), msgs[rng.range(0, 2)], atk.name.c_str(), bp, dmg);
                log.add(buf, {255, 120, 120, 255});
            }
        }

        // Ranged affix on-hit procs
        if (attacker_is_player && world.has<Inventory>(attacker)) {
            Entity wpn_e = world.get<Inventory>(attacker).get_equipped(EquipSlot::MAIN_HAND);
            if (wpn_e != NULL_ENTITY && world.has<Item>(wpn_e)) {
                auto& wpn_item = world.get<Item>(wpn_e);
                if (world.has<StatusEffects>(defender)) {
                    auto& dse = world.get<StatusEffects>(defender);
                    int ch;
                    ch = wpn_item.get_onhit_chance(AffixEffect::ONHIT_POISON);
                    if (ch > 0 && rng.range(1, 100) <= ch) dse.add(StatusType::POISON, 2, 4);
                    ch = wpn_item.get_onhit_chance(AffixEffect::ONHIT_BURN);
                    if (ch > 0 && rng.range(1, 100) <= ch) dse.add(StatusType::BURN, 2, 3);
                    ch = wpn_item.get_onhit_chance(AffixEffect::ONHIT_BLEED);
                    if (ch > 0 && rng.range(1, 100) <= ch) dse.add(StatusType::BLEED, 1, 5);
                }
            }
        }

        if (def.hp <= 0) {
            result.killed = true;
            if (defender_is_player) {
                log.add("You die.", {255, 50, 50, 255});
            } else {
                char buf[256];
                const char* death_msgs[] = {
                    "The %s crumples to the ground.",
                    "The %s collapses, your arrow still lodged in it.",
                    "The %s falls and does not rise.",
                };
                snprintf(buf, sizeof(buf), death_msgs[rng.range(0, 2)], def.name.c_str());
                log.add(buf, {180, 160, 140, 255});

                int xp = kill(world, defender, log);
                if (attacker_is_player && world.has<Stats>(attacker) && xp > 0) {
                    xp = apply_xp_bonus(world, attacker, xp);
                    auto& atk_stats = world.get<Stats>(attacker);
                    if (atk_stats.grant_xp(xp)) {
                        char lvl_buf[64];
                        snprintf(lvl_buf, sizeof(lvl_buf),
                            "You reach level %d.", atk_stats.level);
                        log.add(lvl_buf, {255, 220, 100, 255});
                    }
                }

                // Ranged affix on-kill effects
                if (attacker_is_player && world.has<Inventory>(attacker)) {
                    Entity wpn_e = world.get<Inventory>(attacker).get_equipped(EquipSlot::MAIN_HAND);
                    if (wpn_e != NULL_ENTITY && world.has<Item>(wpn_e)) {
                        auto& wpn_item = world.get<Item>(wpn_e);
                        int ok_heal = wpn_item.get_onkill_mag(AffixEffect::ONKILL_HEAL);
                        if (ok_heal > 0 && world.has<Stats>(attacker)) {
                            auto& atk_s = world.get<Stats>(attacker);
                            atk_s.hp = std::min(atk_s.hp + ok_heal, atk_s.hp_max);
                        }
                        int ok_mana = wpn_item.get_onkill_mag(AffixEffect::ONKILL_MANA);
                        if (ok_mana > 0 && world.has<Stats>(attacker)) {
                            auto& atk_s = world.get<Stats>(attacker);
                            atk_s.mp = std::min(atk_s.mp + ok_mana, atk_s.mp_max);
                        }
                    }
                }
            }
        }
    } else {
        bool attacker_is_player = world.has<Player>(attacker);
        char buf[256];
        if (attacker_is_player) {
            const char* msgs[] = {
                "Your arrow flies past the %s.",
                "Your shot goes wide of the %s.",
                "The %s sidesteps your arrow.",
            };
            snprintf(buf, sizeof(buf), msgs[rng.range(0, 2)], def.name.c_str());
            log.add(buf, {140, 130, 120, 255});
        } else if (world.has<Player>(defender)) {
            const char* msgs[] = {
                "The %s's arrow misses you.",
                "An arrow from the %s clatters off the wall nearby.",
                "You hear the %s's arrow whistle past.",
            };
            snprintf(buf, sizeof(buf), msgs[rng.range(0, 2)], atk.name.c_str());
            log.add(buf, {160, 150, 140, 255});
        }
    }

    return result;
}

int kill(World& world, Entity e, [[maybe_unused]] MessageLog& log) {
    std::string name = "something";
    int xp = 0;
    if (world.has<Stats>(e)) {
        name = world.get<Stats>(e).name;
        xp = world.get<Stats>(e).xp_value;
    }

    // Track kill name for meta/bestiary (drained by engine in process_turn)
    world.pending_kill_names.push_back(name);

    // Check quest target before removing components
    if (world.has<QuestTarget>(e)) {
        world.pending_quest_kills.push_back(
            static_cast<int>(world.get<QuestTarget>(e).quest_id));
    }

    if (world.has<AI>(e)) world.remove<AI>(e);
    if (world.has<Energy>(e)) world.remove<Energy>(e);

    if (world.has<Renderable>(e) && !world.has<DeathAnim>(e)) {
        auto& rend = world.get<Renderable>(e);
        DeathAnim da;
        da.original_sheet = rend.sprite_sheet;
        da.original_sx = rend.sprite_x;
        da.original_sy = rend.sprite_y;
        da.original_flip_h = rend.flip_h;
        world.add<DeathAnim>(e, da);
        // Keep original sprite visible during dissolve; z_order lowered so
        // living entities draw on top
        rend.z_order = -1;
    }

    world.add<Corpse>(e, {name});
    world.remove<Stats>(e);
    return xp;
}

} // namespace combat
