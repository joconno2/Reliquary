#include "systems/magic.h"
#include "components/position.h"
#include "components/stats.h"
#include "components/player.h"
#include "components/ai.h"
#include "components/inventory.h"
#include "components/item.h"
#include "systems/combat.h"
#include "components/status_effect.h"
#include "components/god.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace magic {

static int distance(int x1, int y1, int x2, int y2) {
    return std::max(std::abs(x2 - x1), std::abs(y2 - y1));
}

Entity nearest_enemy(World& world, Entity caster, const TileMap& map, int range) {
    if (!world.has<Position>(caster)) return NULL_ENTITY;
    auto& cpos = world.get<Position>(caster);
    bool caster_is_player = world.has<Player>(caster);

    Entity best = NULL_ENTITY;
    int best_dist = range + 1;

    auto& positions = world.pool<Position>();
    for (size_t i = 0; i < positions.size(); i++) {
        Entity e = positions.entity_at(i);
        if (e == caster) continue;
        if (!world.has<Stats>(e)) continue;

        // Player targets AI entities, AI targets player
        if (caster_is_player && !world.has<AI>(e)) continue;
        if (!caster_is_player && !world.has<Player>(e)) continue;

        auto& epos = positions.at_index(i);

        // Must be visible
        if (!map.in_bounds(epos.x, epos.y) || !map.at(epos.x, epos.y).visible) continue;

        int d = distance(cpos.x, cpos.y, epos.x, epos.y);
        if (d <= range && d < best_dist) {
            best = e;
            best_dist = d;
        }
    }
    return best;
}

CastResult cast(World& world, Entity caster, SpellId spell,
                 TileMap& map, RNG& rng, MessageLog& log,
                 [[maybe_unused]] int target_x, [[maybe_unused]] int target_y) {
    CastResult result;

    if (!world.has<Stats>(caster)) return result;
    auto& stats = world.get<Stats>(caster);
    auto& info = get_spell_info(spell);

    // Yashkhet blood magic: use HP instead of MP
    bool blood_magic = false;
    if (world.has<Player>(caster) && world.has<GodAlignment>(caster)) {
        auto& ga = world.get<GodAlignment>(caster);
        if (ga.god == GodId::YASHKHET) blood_magic = true;
    }

    if (blood_magic) {
        // Blood magic: HP cost instead of MP (costs HP equal to MP cost)
        if (stats.hp <= info.mp_cost) {
            log.add("Not enough blood to give.", {200, 60, 60, 255});
            result.consumed_turn = false;
            return result;
        }
    } else {
        // Normal MP check
        if (stats.mp < info.mp_cost) {
            if (world.has<Player>(caster)) {
                log.add("Not enough mana.", {150, 120, 150, 255});
            }
            result.consumed_turn = false;
            return result;
        }
    }

    // Spell failure from heavy armor (player only)
    if (world.has<Player>(caster) && world.has<Inventory>(caster)) {
        auto& inv = world.get<Inventory>(caster);
        int fail_chance = 0;
        // Check chest and head armor for failure penalty
        for (auto slot : {EquipSlot::CHEST, EquipSlot::HEAD, EquipSlot::FEET}) {
            Entity eq = inv.get_equipped(slot);
            if (eq == NULL_ENTITY || !world.has<Item>(eq)) continue;
            auto& eq_item = world.get<Item>(eq);
            // Heavy armor: armor_bonus >= 4 adds failure chance
            if (eq_item.armor_bonus >= 6) fail_chance += 25;      // plate
            else if (eq_item.armor_bonus >= 4) fail_chance += 15;  // chain
            else if (eq_item.armor_bonus >= 3) fail_chance += 8;   // medium
        }
        if (fail_chance > 0 && rng.chance(fail_chance)) {
            if (blood_magic) stats.hp -= info.mp_cost;
            else stats.mp -= info.mp_cost;
            log.add("Your armor interferes. The spell fizzles.", {180, 130, 130, 255});
            result.consumed_turn = true;
            result.success = false;
            return result;
        }
    }

    // Deduct cost (blood or mana)
    if (blood_magic) {
        stats.hp -= info.mp_cost;
        log.add("Blood for power.", {200, 60, 60, 255});
    } else {
        stats.mp -= info.mp_cost;
    }

    // Spell power scales with INT
    int power = info.base_power + stats.attr(Attr::INT) / 3;
    bool is_player = world.has<Player>(caster);

    switch (spell) {
        case SpellId::SPARK:
        case SpellId::FORCE_BOLT:
        case SpellId::FIREBALL: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target in range.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost; // refund
                result.consumed_turn = false;
                return result;
            }
            auto& tgt = world.get<Stats>(target);

            // Spell damage — no dodge, reduced by magic resistance (future)
            int dmg = power + rng.range(0, power / 3);
            tgt.hp -= dmg;

            if (is_player) {
                char buf[128];
                snprintf(buf, sizeof(buf), "%s strikes the %s for %d.",
                         info.name, tgt.name.c_str(), dmg);
                log.add(buf, {160, 140, 200, 255});
            }

            if (tgt.hp <= 0) {
                if (!world.has<Player>(target)) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "The %s is destroyed.", tgt.name.c_str());
                    log.add(buf, {180, 160, 140, 255});
                    int xp = combat::kill(world, target, log);
                    if (is_player && xp > 0) {
                        if (stats.grant_xp(xp)) {
                            char lvl[64];
                            snprintf(lvl, sizeof(lvl), "You reach level %d.", stats.level);
                            log.add(lvl, {255, 220, 100, 255});
                        }
                    }
                }
            }
            result.success = true;
            break;
        }

        case SpellId::DRAIN_LIFE: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target in range.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost;
                result.consumed_turn = false;
                return result;
            }
            auto& tgt = world.get<Stats>(target);
            int dmg = power + rng.range(0, power / 4);
            tgt.hp -= dmg;
            int healed = std::min(dmg / 2, stats.hp_max - stats.hp);
            stats.hp += healed;

            if (is_player) {
                char buf[128];
                snprintf(buf, sizeof(buf), "You drain the %s. %d damage, %d healed.",
                         tgt.name.c_str(), dmg, healed);
                log.add(buf, {140, 80, 160, 255});
            }

            if (tgt.hp <= 0 && !world.has<Player>(target)) {
                char buf[128];
                snprintf(buf, sizeof(buf), "The %s withers and collapses.", tgt.name.c_str());
                log.add(buf, {180, 160, 140, 255});
                int xp = combat::kill(world, target, log);
                if (is_player && xp > 0) stats.grant_xp(xp);
            }
            result.success = true;
            break;
        }

        case SpellId::FEAR: {
            // All visible enemies in range flee
            if (!world.has<Position>(caster)) break;
            auto& cpos = world.get<Position>(caster);
            int count = 0;

            auto& ai_pool = world.pool<AI>();
            for (size_t i = 0; i < ai_pool.size(); i++) {
                Entity e = ai_pool.entity_at(i);
                if (!world.has<Position>(e)) continue;
                auto& epos = world.get<Position>(e);
                if (distance(cpos.x, cpos.y, epos.x, epos.y) <= info.range) {
                    if (map.in_bounds(epos.x, epos.y) && map.at(epos.x, epos.y).visible) {
                        ai_pool.at_index(i).state = AIState::FLEEING;
                        count++;
                    }
                }
            }

            if (is_player) {
                if (count > 0) {
                    log.add("They see what you've seen. They run.", {140, 80, 160, 255});
                } else {
                    log.add("Nothing nearby to frighten.", {140, 130, 120, 255});
                }
            }
            result.success = count > 0;
            break;
        }

        case SpellId::MINOR_HEAL:
        case SpellId::MAJOR_HEAL: {
            int healed = std::min(power, stats.hp_max - stats.hp);
            stats.hp += healed;
            if (is_player) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Healed %d.", healed);
                log.add(buf, {100, 200, 100, 255});
            }
            result.success = true;
            break;
        }

        case SpellId::HARDEN_SKIN: {
            // Temporary armor boost — for now just flat bonus
            stats.natural_armor += power / 2;
            if (is_player) {
                log.add("Your skin hardens.", {160, 160, 140, 255});
            }
            result.success = true;
            break;
        }

        case SpellId::REVEAL_MAP: {
            for (int y = 0; y < map.height(); y++) {
                for (int x = 0; x < map.width(); x++) {
                    map.at(x, y).explored = true;
                }
            }
            if (is_player) {
                log.add("The walls tell you their secrets.", {120, 120, 200, 255});
            }
            result.success = true;
            break;
        }

        case SpellId::DETECT_MONSTERS: {
            int count = 0;
            auto& ai_pool = world.pool<AI>();
            for (size_t i = 0; i < ai_pool.size(); i++) {
                Entity e = ai_pool.entity_at(i);
                if (world.has<Position>(e)) {
                    auto& epos = world.get<Position>(e);
                    if (map.in_bounds(epos.x, epos.y)) {
                        map.at(epos.x, epos.y).visible = true;
                        map.at(epos.x, epos.y).explored = true;
                        count++;
                    }
                }
            }
            if (is_player) {
                char buf[64];
                snprintf(buf, sizeof(buf), "You sense %d creatures.", count);
                log.add(buf, {120, 120, 200, 255});
            }
            result.success = true;
            break;
        }

        case SpellId::ENTANGLE: {
            // Damage + slow nearby enemies (just damage for now)
            if (!world.has<Position>(caster)) break;
            [[maybe_unused]] auto& cpos = world.get<Position>(caster);
            int count = 0;
            auto& positions = world.pool<Position>();
            for (size_t i = 0; i < positions.size(); i++) {
                Entity e = positions.entity_at(i);
                if (e == caster || !world.has<AI>(e) || !world.has<Stats>(e)) continue;
                auto& epos = positions.at_index(i);
                if (distance(cpos.x, cpos.y, epos.x, epos.y) <= info.range) {
                    world.get<Stats>(e).hp -= power;
                    count++;
                }
            }
            if (is_player && count > 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Vines erupt. %d creatures entangled.", count);
                log.add(buf, {80, 160, 80, 255});
            }
            result.success = count > 0;
            break;
        }

        case SpellId::IDENTIFY: {
            // Identify the first unidentified item in inventory
            if (!world.has<Inventory>(caster)) break;
            auto& inv = world.get<Inventory>(caster);
            bool found = false;
            for (Entity ie : inv.items) {
                if (!world.has<Item>(ie)) continue;
                auto& itm = world.get<Item>(ie);
                if (!itm.identified) {
                    itm.identified = true;
                    if (is_player) {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "You identify the %s.", itm.name.c_str());
                        log.add(buf, {180, 200, 220, 255});
                    }
                    found = true;
                    break;
                }
            }
            if (is_player && !found) {
                log.add("Everything you carry is already identified.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost; // refund
                result.consumed_turn = false;
                break;
            }
            result.success = found;
            break;
        }

        case SpellId::CURE_POISON: {
            // Cure all poison effects
            if (world.has<StatusEffects>(caster)) {
                auto& fx = world.get<StatusEffects>(caster);
                fx.effects.erase(
                    std::remove_if(fx.effects.begin(), fx.effects.end(),
                        [](const StatusEffect& e) { return e.type == StatusType::POISON; }),
                    fx.effects.end());
            }
            if (is_player) log.add("The poison fades from your blood.", {100, 200, 100, 255});
            result.success = true;
            break;
        }

        default:
            if (is_player) {
                log.add("That spell does nothing yet.", {140, 130, 120, 255});
            }
            result.consumed_turn = false;
            if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost; // refund
            break;
    }

    return result;
}

} // namespace magic
