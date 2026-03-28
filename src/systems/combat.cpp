#include "systems/combat.h"
#include "components/stats.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/ai.h"
#include "components/energy.h"
#include "components/player.h"
#include "components/corpse.h"
#include "core/spritesheet.h"
#include <cstdio>

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

    // Attack roll: attacker's melee_attack vs defender's dodge_value
    int attack_roll = rng.range(1, 20) + atk.melee_attack();
    int defense_roll = 10 + def.dodge_value();

    // Critical: natural 20 on the d20
    bool natural_20 = (attack_roll - atk.melee_attack()) == 20;

    if (attack_roll >= defense_roll || natural_20) {
        result.hit = true;

        // Damage: base melee damage - defender protection, minimum 1
        int dmg = atk.melee_damage();

        // Critical hit: double damage
        if (natural_20 || rng.range(1, 100) <= atk.attr(Attr::PER)) {
            dmg *= 2;
            result.critical = true;
        }

        dmg -= def.protection();
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
            kill(world, defender, log);
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

void kill(World& world, Entity e, [[maybe_unused]] MessageLog& log) {
    std::string name = "something";
    if (world.has<Stats>(e)) {
        name = world.get<Stats>(e).name;
    }

    // Turn into a corpse: remove AI, energy, stats; add corpse component
    // Keep position and change renderable to corpse sprite
    if (world.has<AI>(e)) world.remove<AI>(e);
    if (world.has<Energy>(e)) world.remove<Energy>(e);

    // Change sprite to bones (tiles.png row 21, col 0-1)
    if (world.has<Renderable>(e)) {
        auto& rend = world.get<Renderable>(e);
        rend.sprite_sheet = SHEET_TILES;
        rend.sprite_x = 0; // corpse bones 1
        rend.sprite_y = 21;
        rend.z_order = -1; // render under living entities
    }

    world.add<Corpse>(e, {name});

    // Remove stats last (corpse doesn't have stats)
    world.remove<Stats>(e);
}

} // namespace combat
