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
#include "components/renderable.h"
#include "components/energy.h"
#include "core/spritesheet.h"
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

    // Helper: single-target damage spell with optional status effect
    auto do_single_target_dmg = [&](StatusType status_type = StatusType::POISON,
                                     int status_dmg = 0, int status_turns = 0) -> bool {
        Entity target = nearest_enemy(world, caster, map, info.range);
        if (target == NULL_ENTITY) {
            if (is_player) log.add("No target in range.", {140, 130, 120, 255});
            if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost;
            result.consumed_turn = false;
            return false;
        }
        auto& tgt = world.get<Stats>(target);
        int dmg = power + rng.range(0, std::max(1, power / 3));
        tgt.hp -= dmg;
        if (is_player) {
            char buf[128];
            snprintf(buf, sizeof(buf), "%s hits the %s for %d.", info.name, tgt.name.c_str(), dmg);
            log.add(buf, {160, 140, 200, 255});
        }
        if (status_turns > 0 && world.has<StatusEffects>(target))
            world.get<StatusEffects>(target).add(status_type, status_dmg, status_turns);
        if (tgt.hp <= 0 && !world.has<Player>(target)) {
            int xp = combat::kill(world, target, log);
            if (is_player && xp > 0) stats.grant_xp(xp);
        }
        result.success = true;
        return true;
    };

    // Helper: AoE — apply to all visible enemies in range
    auto do_aoe = [&](auto per_enemy_fn) -> int {
        if (!world.has<Position>(caster)) return 0;
        auto& cpos = world.get<Position>(caster);
        int count = 0;
        auto& ai_pool = world.pool<AI>();
        for (size_t i = 0; i < ai_pool.size(); i++) {
            Entity e = ai_pool.entity_at(i);
            if (!world.has<Position>(e) || !world.has<Stats>(e)) continue;
            auto& epos = world.get<Position>(e);
            if (distance(cpos.x, cpos.y, epos.x, epos.y) <= info.range) {
                if (map.in_bounds(epos.x, epos.y) && map.at(epos.x, epos.y).visible) {
                    per_enemy_fn(e);
                    count++;
                }
            }
        }
        return count;
    };

    switch (spell) {
        // === CONJURATION ===
        case SpellId::SPARK:
        case SpellId::FORCE_BOLT:
            do_single_target_dmg();
            break;
        case SpellId::FIREBALL:
            do_single_target_dmg(StatusType::BURN, 3, 3);
            break;
        case SpellId::ICE_SHARD:
            do_single_target_dmg(StatusType::FROZEN, 0, 2);
            break;
        case SpellId::LIGHTNING:
        case SpellId::METEOR:
        case SpellId::DISINTEGRATE:
            do_single_target_dmg();
            break;
        case SpellId::ACID_SPLASH: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost;
                result.consumed_turn = false;
                break;
            }
            auto& tgt = world.get<Stats>(target);
            int dmg = power + rng.range(0, power / 3);
            tgt.hp -= dmg;
            tgt.natural_armor = std::max(0, tgt.natural_armor - 2);
            if (is_player) { char buf[128]; snprintf(buf, sizeof(buf), "Acid corrodes the %s. %d dmg, -2 armor.", tgt.name.c_str(), dmg); log.add(buf, {160, 200, 80, 255}); }
            if (tgt.hp <= 0 && !world.has<Player>(target)) combat::kill(world, target, log);
            result.success = true;
            break;
        }
        case SpellId::FROST_NOVA: {
            int count = do_aoe([&](Entity e) {
                auto& tgt = world.get<Stats>(e);
                int dmg = power + rng.range(0, power / 3);
                tgt.hp -= dmg;
                if (!world.has<StatusEffects>(e)) world.add<StatusEffects>(e, {});
                world.get<StatusEffects>(e).add(StatusType::FROZEN, 0, 1);
                if (tgt.hp <= 0 && !world.has<Player>(e)) combat::kill(world, e, log);
            });
            if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "Frost explodes outward. %d frozen.", count); log.add(buf, {140, 200, 255, 255}); }
            result.success = count > 0;
            break;
        }
        case SpellId::CHAIN_LIGHTNING: {
            // Hit up to 3 visible enemies
            if (!world.has<Position>(caster)) break;
            auto& cpos = world.get<Position>(caster);
            int hits = 0;
            auto& ai_pool = world.pool<AI>();
            for (size_t i = 0; i < ai_pool.size() && hits < 3; i++) {
                Entity e = ai_pool.entity_at(i);
                if (!world.has<Position>(e) || !world.has<Stats>(e)) continue;
                auto& epos = world.get<Position>(e);
                if (distance(cpos.x, cpos.y, epos.x, epos.y) <= info.range &&
                    map.in_bounds(epos.x, epos.y) && map.at(epos.x, epos.y).visible) {
                    int dmg = power + rng.range(0, power / 3);
                    auto& tgt = world.get<Stats>(e);
                    tgt.hp -= dmg;
                    if (tgt.hp <= 0 && !world.has<Player>(e)) combat::kill(world, e, log);
                    hits++;
                }
            }
            if (is_player) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Lightning chains through %d targets.", hits);
                log.add(buf, {180, 200, 255, 255});
            }
            result.success = hits > 0;
            break;
        }

        // === TRANSMUTATION ===
        case SpellId::HARDEN_SKIN:
            stats.natural_armor += 2;
            if (is_player) log.add("Your skin hardens. (+2 armor)", {160, 160, 140, 255});
            result.success = true;
            break;
        case SpellId::HASTEN:
            stats.base_speed += 30;
            if (is_player) log.add("+30 speed.", {200, 200, 140, 255});
            result.success = true;
            break;
        case SpellId::STONE_FIST:
            stats.base_damage += 3;
            if (is_player) log.add("+3 melee damage.", {160, 140, 120, 255});
            result.success = true;
            break;
        case SpellId::IRON_BODY:
            stats.natural_armor += 4;
            stats.base_speed -= 10;
            if (is_player) log.add("+4 armor, -10 speed.", {160, 160, 160, 255});
            result.success = true;
            break;
        case SpellId::SLOW: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost;
                result.consumed_turn = false; break;
            }
            world.get<Stats>(target).base_speed = std::max(30, world.get<Stats>(target).base_speed - 30);
            if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "The %s slows.", world.get<Stats>(target).name.c_str()); log.add(buf, {160, 160, 200, 255}); }
            result.success = true;
            break;
        }
        case SpellId::POLYMORPH: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost;
                result.consumed_turn = false; break;
            }
            auto& tgt = world.get<Stats>(target);
            tgt.name = "rat"; tgt.hp = 1; tgt.hp_max = 1; tgt.base_damage = 1; tgt.natural_armor = 0;
            if (world.has<Renderable>(target)) { auto& r = world.get<Renderable>(target); r.sprite_sheet = SHEET_ANIMALS; r.sprite_x = 0; r.sprite_y = 0; }
            if (is_player) log.add("The creature warps and shrinks into a rat.", {180, 160, 220, 255});
            result.success = true;
            break;
        }
        case SpellId::PHASE: {
            // Teleport to random walkable tile
            for (int a = 0; a < 100; a++) {
                int tx = rng.range(1, map.width() - 2);
                int ty = rng.range(1, map.height() - 2);
                if (map.is_walkable(tx, ty)) {
                    auto& cpos = world.get<Position>(caster);
                    cpos.x = tx; cpos.y = ty;
                    if (is_player) log.add("Reality shifts. You are elsewhere.", {180, 160, 220, 255});
                    result.success = true;
                    break;
                }
            }
            break;
        }

        // === DIVINATION ===
        case SpellId::REVEAL_MAP:
            for (int y = 0; y < map.height(); y++)
                for (int x = 0; x < map.width(); x++)
                    map.at(x, y).explored = true;
            if (is_player) log.add("The floor layout reveals itself.", {120, 120, 200, 255});
            result.success = true;
            break;
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
                char buf[64]; snprintf(buf, sizeof(buf), "You sense %d creatures.", count);
                log.add(buf, {120, 120, 200, 255});
            }
            result.success = true;
            break;
        }
        case SpellId::IDENTIFY: {
            if (!world.has<Inventory>(caster)) break;
            auto& inv = world.get<Inventory>(caster);
            bool found = false;
            for (Entity ie : inv.items) {
                if (!world.has<Item>(ie)) continue;
                auto& itm = world.get<Item>(ie);
                if (!itm.identified) {
                    itm.identified = true;
                    if (is_player) {
                        char buf[128]; snprintf(buf, sizeof(buf), "Identified: %s.", itm.name.c_str());
                        log.add(buf, {180, 200, 220, 255});
                    }
                    found = true; break;
                }
            }
            if (!found) {
                if (is_player) log.add("Nothing to identify.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost;
                result.consumed_turn = false;
            }
            result.success = found;
            break;
        }
        case SpellId::FORESIGHT:
            stats.dodge_value(); // just for reference — apply via dodge_bonus hack
            // Can't easily add temporary dodge. Add natural_armor as proxy.
            stats.natural_armor += 1; // minor defensive buff
            if (is_player) log.add("Your senses sharpen. You feel more evasive.", {120, 140, 200, 255});
            result.success = true;
            break;
        case SpellId::TRUESIGHT: {
            auto& cpos = world.get<Position>(caster);
            for (int dy = -6; dy <= 6; dy++)
                for (int dx = -6; dx <= 6; dx++) {
                    int tx = cpos.x + dx, ty = cpos.y + dy;
                    if (map.in_bounds(tx, ty)) { map.at(tx, ty).visible = true; map.at(tx, ty).explored = true; }
                }
            if (is_player) log.add("You see beyond the walls.", {120, 160, 220, 255});
            result.success = true;
            break;
        }
        case SpellId::SCRY: {
            // Reveal all item positions
            auto& item_pool = world.pool<Item>();
            int count = 0;
            for (size_t i = 0; i < item_pool.size(); i++) {
                Entity e = item_pool.entity_at(i);
                if (world.has<Position>(e)) {
                    auto& epos = world.get<Position>(e);
                    if (map.in_bounds(epos.x, epos.y)) { map.at(epos.x, epos.y).visible = true; map.at(epos.x, epos.y).explored = true; count++; }
                }
            }
            if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "You sense %d objects.", count); log.add(buf, {120, 120, 200, 255}); }
            result.success = true;
            break;
        }
        case SpellId::CLAIRVOYANCE:
            // Reveal map + detect monsters combined
            for (int y = 0; y < map.height(); y++)
                for (int x = 0; x < map.width(); x++)
                    map.at(x, y).explored = true;
            { auto& ai_pool = world.pool<AI>();
              for (size_t i = 0; i < ai_pool.size(); i++) {
                  Entity e = ai_pool.entity_at(i);
                  if (world.has<Position>(e)) { auto& epos = world.get<Position>(e);
                      if (map.in_bounds(epos.x, epos.y)) { map.at(epos.x, epos.y).visible = true; } }
              }
            }
            if (is_player) log.add("Everything on this floor is known to you.", {120, 140, 220, 255});
            result.success = true;
            break;

        // === HEALING ===
        case SpellId::MINOR_HEAL:
        case SpellId::MAJOR_HEAL: {
            int healed = std::min(power, stats.hp_max - stats.hp);
            stats.hp += healed;
            if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "Healed %d.", healed); log.add(buf, {100, 200, 100, 255}); }
            result.success = true;
            break;
        }
        case SpellId::CURE_POISON:
            if (world.has<StatusEffects>(caster)) {
                auto& fx = world.get<StatusEffects>(caster);
                fx.effects.erase(std::remove_if(fx.effects.begin(), fx.effects.end(),
                    [](const StatusEffect& e) { return e.type == StatusType::POISON; }), fx.effects.end());
            }
            if (is_player) log.add("The poison fades.", {100, 200, 100, 255});
            result.success = true;
            break;
        case SpellId::CLEANSE:
            if (world.has<StatusEffects>(caster))
                world.get<StatusEffects>(caster).effects.clear();
            if (is_player) log.add("All afflictions purged.", {100, 220, 100, 255});
            result.success = true;
            break;
        case SpellId::SHIELD_OF_FAITH:
            stats.natural_armor += 3;
            if (is_player) log.add("A ward settles over you. (+3 armor)", {200, 200, 140, 255});
            result.success = true;
            break;
        case SpellId::RESTORE: {
            int hp_heal = std::min(15, stats.hp_max - stats.hp);
            int mp_heal = std::min(10, stats.mp_max - stats.mp);
            stats.hp += hp_heal; stats.mp += mp_heal;
            if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "+%d HP, +%d MP.", hp_heal, mp_heal); log.add(buf, {100, 200, 160, 255}); }
            result.success = true;
            break;
        }
        case SpellId::SANCTUARY:
            stats.natural_armor += 5;
            // Make enemies lose track
            { auto& ai_pool = world.pool<AI>();
              for (size_t i = 0; i < ai_pool.size(); i++) {
                  ai_pool.at_index(i).state = AIState::IDLE;
              }
            }
            if (is_player) log.add("+5 armor. Enemies lose track of you.", {200, 220, 140, 255});
            result.success = true;
            break;
        case SpellId::RESURRECTION:
            // Set a revival flag — on death within 10 turns, revive at half HP
            stats.hp_max += 0; // placeholder: we'd need a revival counter on Stats
            // For now: just heal to full as a powerful heal
            { int h = stats.hp_max - stats.hp; stats.hp = stats.hp_max;
              if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "Full heal. (+%d HP)", h); log.add(buf, {255, 240, 200, 255}); } }
            result.success = true;
            break;

        // === NATURE ===
        case SpellId::ENTANGLE: {
            int count = do_aoe([&](Entity e) { world.get<Stats>(e).hp -= power; });
            if (is_player && count > 0) { char buf[64]; snprintf(buf, sizeof(buf), "Vines erupt. %d entangled.", count); log.add(buf, {80, 160, 80, 255}); }
            result.success = count > 0;
            break;
        }
        case SpellId::BEAST_CALL: {
            // Summon 2 friendly wolves near the caster
            if (!world.has<Position>(caster)) break;
            auto& cpos = world.get<Position>(caster);
            int spawned = 0;
            for (int a = 0; a < 40 && spawned < 2; a++) {
                int tx = cpos.x + rng.range(-3, 3);
                int ty = cpos.y + rng.range(-3, 3);
                if (!map.in_bounds(tx, ty) || !map.is_walkable(tx, ty)) continue;
                if (tx == cpos.x && ty == cpos.y) continue;
                Entity wolf = world.create();
                world.add<Position>(wolf, {tx, ty});
                world.add<Renderable>(wolf, {SHEET_ANIMALS, 4, 2, {255,255,255,255}, 5});
                Stats ws; ws.name = "summoned wolf"; ws.hp = 15 + power; ws.hp_max = ws.hp;
                ws.base_damage = 4 + power / 3; ws.base_speed = 110; ws.xp_value = 0;
                world.add<Stats>(wolf, std::move(ws));
                // Wolves hunt enemies near the caster — set to hunting with caster's position
                AI wai; wai.state = AIState::HUNTING; wai.last_seen_x = cpos.x; wai.last_seen_y = cpos.y;
                world.add<AI>(wolf, wai);
                world.add<Energy>(wolf, {0, 110});
                spawned++;
            }
            if (is_player) {
                char buf[64]; snprintf(buf, sizeof(buf), "%d wolves answer your call.", spawned);
                log.add(buf, {80, 160, 80, 255});
            }
            result.success = spawned > 0;
            break;
        }
        case SpellId::POISON_CLOUD: {
            int count = do_aoe([&](Entity e) {
                if (!world.has<StatusEffects>(e)) world.add<StatusEffects>(e, {});
                world.get<StatusEffects>(e).add(StatusType::POISON, 2, 6);
            });
            if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "Poison cloud. %d affected.", count); log.add(buf, {100, 200, 80, 255}); }
            result.success = count > 0;
            break;
        }
        case SpellId::THORNWALL: {
            int count = do_aoe([&](Entity e) {
                if (!world.has<StatusEffects>(e)) world.add<StatusEffects>(e, {});
                world.get<StatusEffects>(e).add(StatusType::BLEED, 3, 5);
            });
            if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "Thorns tear at %d creatures.", count); log.add(buf, {100, 160, 80, 255}); }
            result.success = count > 0;
            break;
        }
        case SpellId::REJUVENATE:
            { int h = std::min(power * 3, stats.hp_max - stats.hp); stats.hp += h;
              if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "+%d HP.", h); log.add(buf, {80, 200, 80, 255}); } }
            result.success = true;
            break;
        case SpellId::EARTHQUAKE: {
            // Damage ALL enemies on floor, stun adjacent
            auto& cpos = world.get<Position>(caster);
            int count = 0;
            auto& ai_pool = world.pool<AI>();
            for (size_t i = 0; i < ai_pool.size(); i++) {
                Entity e = ai_pool.entity_at(i);
                if (!world.has<Stats>(e)) continue;
                auto& es = world.get<Stats>(e);
                int dmg = power + rng.range(0, power / 3);
                es.hp -= dmg;
                if (world.has<Position>(e)) {
                    auto& ep = world.get<Position>(e);
                    if (distance(cpos.x, cpos.y, ep.x, ep.y) <= 2) {
                        if (!world.has<StatusEffects>(e)) world.add<StatusEffects>(e, {});
                        world.get<StatusEffects>(e).add(StatusType::STUNNED, 0, 2);
                    }
                }
                if (es.hp <= 0 && !world.has<Player>(e)) combat::kill(world, e, log);
                count++;
            }
            if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "The ground shakes. %d hit.", count); log.add(buf, {140, 120, 80, 255}); }
            result.success = count > 0;
            break;
        }
        case SpellId::LIGHTNING_STORM: {
            // Hit up to 5 random visible enemies
            if (!world.has<Position>(caster)) break;
            auto& cpos = world.get<Position>(caster);
            int hits = 0;
            auto& ai_pool = world.pool<AI>();
            for (size_t i = 0; i < ai_pool.size() && hits < 5; i++) {
                Entity e = ai_pool.entity_at(i);
                if (!world.has<Position>(e) || !world.has<Stats>(e)) continue;
                auto& epos = world.get<Position>(e);
                if (distance(cpos.x, cpos.y, epos.x, epos.y) <= info.range &&
                    map.in_bounds(epos.x, epos.y) && map.at(epos.x, epos.y).visible) {
                    int dmg = power + rng.range(0, power / 3);
                    auto& tgt = world.get<Stats>(e);
                    tgt.hp -= dmg;
                    if (tgt.hp <= 0 && !world.has<Player>(e)) combat::kill(world, e, log);
                    hits++;
                }
            }
            if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "Lightning storms through %d targets.", hits); log.add(buf, {180, 200, 255, 255}); }
            result.success = hits > 0;
            break;
        }
        case SpellId::BARKSKIN:
            stats.natural_armor += 3;
            stats.poison_resist += 15;
            if (is_player) log.add("+3 armor, +15% poison resist.", {80, 160, 80, 255});
            result.success = true;
            break;
        case SpellId::SWARM: {
            // Summon 4 rats
            if (!world.has<Position>(caster)) break;
            auto& cpos = world.get<Position>(caster);
            int spawned = 0;
            for (int a = 0; a < 40 && spawned < 4; a++) {
                int tx = cpos.x + rng.range(-2, 2);
                int ty = cpos.y + rng.range(-2, 2);
                if (!map.in_bounds(tx, ty) || !map.is_walkable(tx, ty)) continue;
                if (tx == cpos.x && ty == cpos.y) continue;
                Entity rat = world.create();
                world.add<Position>(rat, {tx, ty});
                world.add<Renderable>(rat, {SHEET_ANIMALS, 0, 0, {255,255,255,255}, 5});
                Stats rs; rs.name = "summoned rat"; rs.hp = 5 + power; rs.hp_max = rs.hp;
                rs.base_damage = 2; rs.base_speed = 120; rs.xp_value = 0;
                world.add<Stats>(rat, std::move(rs));
                AI rai; rai.state = AIState::HUNTING; rai.last_seen_x = cpos.x; rai.last_seen_y = cpos.y;
                world.add<AI>(rat, rai);
                world.add<Energy>(rat, {0, 120});
                spawned++;
            }
            if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "%d rats swarm from the ground.", spawned); log.add(buf, {80, 140, 80, 255}); }
            result.success = spawned > 0;
            break;
        }

        // === DARK ARTS ===
        case SpellId::DRAIN_LIFE: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost;
                result.consumed_turn = false;
                break;
            }
            auto& tgt = world.get<Stats>(target);
            int dmg = power + rng.range(0, power / 4);
            tgt.hp -= dmg;
            int healed = std::min(dmg / 2, stats.hp_max - stats.hp);
            stats.hp += healed;
            if (is_player) { char buf[128]; snprintf(buf, sizeof(buf), "Drain %s. %d dmg, +%d HP.", tgt.name.c_str(), dmg, healed); log.add(buf, {140, 80, 160, 255}); }
            if (tgt.hp <= 0 && !world.has<Player>(target)) { combat::kill(world, target, log); }
            result.success = true;
            break;
        }
        case SpellId::FEAR: {
            int count = do_aoe([&](Entity e) { world.get<AI>(e).state = AIState::FLEEING; });
            if (is_player) { if (count > 0) log.add("They flee in terror.", {140, 80, 160, 255}); else log.add("Nothing to frighten.", {140, 130, 120, 255}); }
            result.success = count > 0;
            break;
        }
        case SpellId::RAISE_DEAD: {
            // Find nearest corpse-like tile (FLOOR_BONE) and spawn a skeleton ally
            if (!world.has<Position>(caster)) break;
            auto& cpos = world.get<Position>(caster);
            bool raised = false;
            for (int dy = -3; dy <= 3 && !raised; dy++) {
                for (int dx = -3; dx <= 3 && !raised; dx++) {
                    int tx = cpos.x + dx, ty = cpos.y + dy;
                    if (!map.in_bounds(tx, ty) || !map.is_walkable(tx, ty)) continue;
                    Entity sk = world.create();
                    world.add<Position>(sk, {tx, ty});
                    world.add<Renderable>(sk, {SHEET_MONSTERS, 0, 3, {200,200,180,255}, 5});
                    Stats ss; ss.name = "raised skeleton"; ss.hp = 12 + power; ss.hp_max = ss.hp;
                    ss.base_damage = 3 + power / 3; ss.xp_value = 0;
                    world.add<Stats>(sk, std::move(ss));
                    AI sai; sai.state = AIState::HUNTING; sai.last_seen_x = cpos.x; sai.last_seen_y = cpos.y;
                    world.add<AI>(sk, sai);
                    world.add<Energy>(sk, {0, 90});
                    raised = true;
                }
            }
            if (is_player) { if (raised) log.add("Bones reassemble. Something gets up.", {140, 80, 160, 255}); else log.add("Nothing to raise.", {140, 130, 120, 255}); }
            result.success = raised;
            break;
        }
        case SpellId::HEX: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost;
                result.consumed_turn = false;
                break;
            }
            if (!world.has<StatusEffects>(target)) world.add<StatusEffects>(target, {});
            world.get<StatusEffects>(target).add(StatusType::CONFUSED, 0, 4);
            if (is_player) { auto& tgt = world.get<Stats>(target); char buf[128]; snprintf(buf, sizeof(buf), "The %s's mind fractures.", tgt.name.c_str()); log.add(buf, {140, 80, 160, 255}); }
            result.success = true;
            break;
        }
        case SpellId::SOUL_REND:
            do_single_target_dmg(StatusType::BLEED, 3, 4);
            break;
        case SpellId::DARKNESS: {
            int count = do_aoe([&](Entity e) {
                if (!world.has<StatusEffects>(e)) world.add<StatusEffects>(e, {});
                world.get<StatusEffects>(e).add(StatusType::BLIND, 0, 4);
            });
            if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "Darkness falls on %d creatures.", count); log.add(buf, {80, 60, 100, 255}); }
            result.success = count > 0;
            break;
        }
        case SpellId::WITHER: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost;
                result.consumed_turn = false; break;
            }
            auto& tgt = world.get<Stats>(target);
            int dmg = power + rng.range(0, power / 3);
            tgt.hp -= dmg;
            tgt.hp_max = std::max(1, tgt.hp_max - 5); // permanent max HP reduction
            if (is_player) { char buf[128]; snprintf(buf, sizeof(buf), "The %s withers. %d dmg, -5 max HP.", tgt.name.c_str(), dmg); log.add(buf, {140, 80, 160, 255}); }
            if (tgt.hp <= 0 && !world.has<Player>(target)) combat::kill(world, target, log);
            result.success = true;
            break;
        }
        case SpellId::BLOOD_PACT:
            // Sacrifice 20 HP for permanent bonuses
            if (stats.hp <= 20) {
                if (is_player) log.add("Not enough HP to sacrifice.", {200, 80, 80, 255});
                if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost;
                result.consumed_turn = false; break;
            }
            stats.hp -= 20;
            stats.base_damage += 5;
            stats.natural_armor += 2;
            if (is_player) log.add("Blood for power. -20 HP, +5 damage, +2 armor permanently.", {140, 40, 40, 255});
            result.success = true;
            break;
        case SpellId::DOOM: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += info.mp_cost; else stats.mp += info.mp_cost;
                result.consumed_turn = false; break;
            }
            // Instant kill if target HP < 50, otherwise massive damage
            auto& tgt = world.get<Stats>(target);
            if (tgt.hp < 50) {
                if (is_player) { char buf[128]; snprintf(buf, sizeof(buf), "The %s is marked for death. It falls.", tgt.name.c_str()); log.add(buf, {100, 40, 120, 255}); }
                tgt.hp = 0;
                combat::kill(world, target, log);
            } else {
                int dmg = tgt.hp / 2; // half current HP
                tgt.hp -= dmg;
                if (is_player) { char buf[128]; snprintf(buf, sizeof(buf), "Doom marks the %s. %d damage.", tgt.name.c_str(), dmg); log.add(buf, {100, 40, 120, 255}); }
            }
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
