#include "systems/combat.h"
#include "components/stats.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/ai.h"
#include "components/energy.h"
#include "components/player.h"
#include "components/corpse.h"
#include "components/inventory.h"
#include "components/item.h"
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

        // Message
        bool attacker_is_player = world.has<Player>(attacker);
        bool defender_is_player = world.has<Player>(defender);

        if (result.critical) {
            if (attacker_is_player) {
                char buf[128];
                snprintf(buf, sizeof(buf),
                    "Your blade bites deep into the %s. Critical.", def.name.c_str());
                log.add(buf, {255, 200, 100, 255});
            } else {
                char buf[128];
                snprintf(buf, sizeof(buf),
                    "The %s strikes you with terrible force.", atk.name.c_str());
                log.add(buf, {255, 80, 80, 255});
            }
        } else {
            if (attacker_is_player) {
                char buf[128];
                snprintf(buf, sizeof(buf),
                    "You hit the %s for %d.", def.name.c_str(), dmg);
                log.add(buf, {200, 180, 160, 255});
            } else if (defender_is_player) {
                char buf[128];
                snprintf(buf, sizeof(buf),
                    "The %s hits you for %d.", atk.name.c_str(), dmg);
                log.add(buf, {255, 120, 120, 255});
            }
        }

        // Check death
        if (def.hp <= 0) {
            result.killed = true;
            if (defender_is_player) {
                log.add("You die.", {255, 50, 50, 255});
            } else {
                char buf[128];
                snprintf(buf, sizeof(buf),
                    "The %s crumples to the ground.", def.name.c_str());
                log.add(buf, {180, 160, 140, 255});
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
    } else {
        // Miss
        bool attacker_is_player = world.has<Player>(attacker);
        if (attacker_is_player) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                "You swing at the %s. Miss.", def.name.c_str());
            log.add(buf, {140, 130, 120, 255});
        } else if (world.has<Player>(defender)) {
            char buf[128];
            snprintf(buf, sizeof(buf),
                "The %s swings at you. Miss.", atk.name.c_str());
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

    if (world.has<Renderable>(e)) {
        auto& rend = world.get<Renderable>(e);
        rend.sprite_sheet = SHEET_TILES;
        rend.sprite_x = 0;
        rend.sprite_y = 21;
        rend.z_order = -1;
    }

    world.add<Corpse>(e, {name});
    world.remove<Stats>(e);
    return xp;
}

} // namespace combat
