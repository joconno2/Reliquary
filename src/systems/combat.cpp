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
#include "core/spritesheet.h"
#include <cstdio>

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

    // Attack roll: d20 + melee_attack + equip bonus vs 10 + dodge + equip bonus
    int raw_roll = rng.range(1, 20);
    int attack_roll = raw_roll + atk.melee_attack() + atk_eq_atk;
    int defense_roll = 10 + def.dodge_value() + def_eq_dodge;

    bool natural_20 = (raw_roll == 20);

    if (attack_roll >= defense_roll || natural_20) {
        result.hit = true;

        // Damage: base + equip - defender protection - equip armor, minimum 1
        int dmg = atk.melee_damage() + atk_eq_dmg;

        if (natural_20 || rng.range(1, 100) <= atk.attr(Attr::PER)) {
            dmg *= 2;
            result.critical = true;
        }

        dmg -= (def.protection() + def_eq_arm);
        if (dmg < 1) dmg = 1;

        result.damage = dmg;
        def.hp -= dmg;

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
                "The %s lunges at you — you twist away.",
                "The %s's attack goes wide.",
                "You dodge the %s's strike.",
                "The %s swings wildly and misses.",
                "The %s's blow glances off harmlessly.",
            };
            snprintf(buf, sizeof(buf), msgs[rng.range(0, 4)], atk.name.c_str());
            log.add(buf, {160, 150, 140, 255});
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

        int dmg = weapon_damage + atk.attr(Attr::DEX) / 3;

        if (natural_20 || rng.range(1, 100) <= atk.attr(Attr::PER)) {
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
