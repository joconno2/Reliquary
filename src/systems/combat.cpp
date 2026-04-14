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

    // Passive tree bonuses
    auto atk_tree = get_tree_bonuses(world, attacker);
    auto def_tree = get_tree_bonuses(world, defender);

    // Ghost Blade: melee scales with INT instead of STR
    int atk_melee = atk_tree.ghost_blade
        ? (atk.attr(Attr::INT) + atk.level)
        : (atk.melee_attack());
    int atk_melee_dmg = atk_tree.ghost_blade
        ? (atk.base_damage + atk.attr(Attr::INT) / 3)
        : (atk.melee_damage());

    // Iron Reflexes: defender converts dodge to armor
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

    if (death_marked || (!tree_dodged && (attack_roll >= defense_roll || natural_20))) {
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
            // Unarmed with high WIL (monk-like) can hurt wraiths
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
        int effective_crit = atk.attr(Attr::PER) + atk_tree.crit_chance;
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

        dmg -= (def.protection() + def_eq_arm + def_tree.armor + def_armor_bonus);
        if (dmg < 1) dmg = 1;

        result.damage = dmg;
        def.hp -= dmg;

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

                // Check quest target before kill removes components
                if (world.has<QuestTarget>(defender)) {
                    result.quest_target_id = static_cast<int>(
                        world.get<QuestTarget>(defender).quest_id);
                }

                int xp = kill(world, defender, log);
                // Grant XP to attacker if they're the player
                if (attacker_is_player && world.has<Stats>(attacker) && xp > 0) {
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
                    result.killed = false; // attacker died, not defender
                    // Let the caller handle the kill on next check
                }
            }
        }
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
    int attack_roll = raw_roll + atk.attr(Attr::DEX) + atk.level;
    int defense_roll = 10 + def.dodge_value() + def_eq_dodge;

    bool natural_20 = (raw_roll == 20);

    if (attack_roll >= defense_roll || natural_20) {
        result.hit = true;

        // Wraith: immune to non-silver ranged attacks
        if (world.has<AI>(defender) && world.get<AI>(defender).behavior == BehaviorType::WRAITH) {
            // Ranged attacks can't hit wraiths (only magic/silver melee)
            result.hit = false;
            result.damage = 0;
            if (world.has<Player>(attacker))
                log.add("Your arrow passes through the wraith.", {140, 120, 160, 255});
            return result;
        }

        int dmg = weapon_damage + atk.attr(Attr::DEX) / 3;

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

        int ranged_crit = atk.attr(Attr::PER);
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

                if (world.has<QuestTarget>(defender)) {
                    result.quest_target_id = static_cast<int>(
                        world.get<QuestTarget>(defender).quest_id);
                }

                int xp = kill(world, defender, log);
                if (attacker_is_player && world.has<Stats>(attacker) && xp > 0) {
                    auto& atk_stats = world.get<Stats>(attacker);
                    if (atk_stats.grant_xp(xp)) {
                        char lvl_buf[64];
                        snprintf(lvl_buf, sizeof(lvl_buf),
                            "You reach level %d.", atk_stats.level);
                        log.add(lvl_buf, {255, 220, 100, 255});
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
