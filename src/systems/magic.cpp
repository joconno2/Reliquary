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
#include "components/buff.h"
#include "components/passive_tree.h"
#include "components/trap.h"
#include "components/skills.h"
#include "components/corpse.h"
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace magic {

static constexpr int MAX_SUMMONS = 3;

// Enforce summon cap: destroy oldest friendly summon if at limit.
// Returns number of current summons after cleanup.
static int enforce_summon_cap(World& world) {
    auto& ai_pool = world.pool<AI>();
    std::vector<Entity> friendlies;
    for (size_t i = 0; i < ai_pool.size(); i++) {
        Entity e = ai_pool.entity_at(i);
        if (ai_pool.at_index(i).friendly && world.has<Stats>(e))
            friendlies.push_back(e);
    }
    // Destroy oldest until under cap
    while (static_cast<int>(friendlies.size()) >= MAX_SUMMONS) {
        Entity oldest = friendlies.front();
        world.destroy(oldest);
        friendlies.erase(friendlies.begin());
    }
    return static_cast<int>(friendlies.size());
}

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

std::vector<Entity> all_visible_enemies(World& world, Entity caster, const TileMap& map, int range) {
    std::vector<Entity> result;
    if (!world.has<Position>(caster)) return result;
    auto& cpos = world.get<Position>(caster);

    struct DistEntity { int dist; Entity e; };
    std::vector<DistEntity> candidates;

    auto& positions = world.pool<Position>();
    for (size_t i = 0; i < positions.size(); i++) {
        Entity e = positions.entity_at(i);
        if (e == caster) continue;
        if (!world.has<Stats>(e) || !world.has<AI>(e)) continue;
        if (world.get<AI>(e).friendly) continue;

        auto& epos = positions.at_index(i);
        if (!map.in_bounds(epos.x, epos.y) || !map.at(epos.x, epos.y).visible) continue;

        int d = distance(cpos.x, cpos.y, epos.x, epos.y);
        if (d <= range) candidates.push_back({d, e});
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const DistEntity& a, const DistEntity& b) { return a.dist < b.dist; });

    for (auto& c : candidates) result.push_back(c.e);
    return result;
}

CastResult cast(World& world, Entity caster, SpellId spell,
                 TileMap& map, RNG& rng, MessageLog& log,
                 [[maybe_unused]] int target_x, [[maybe_unused]] int target_y) {
    CastResult result;

    if (!world.has<Stats>(caster)) return result;
    auto& stats = world.get<Stats>(caster);
    auto& info = get_spell_info(spell);

    // Blood magic: Yashkhet god OR Blood Magic keystone
    bool blood_magic = false;
    if (world.has<Player>(caster) && world.has<GodAlignment>(caster)) {
        auto& ga = world.get<GodAlignment>(caster);
        if (ga.god == GodId::YASHKHET) blood_magic = true;
    }
    // Keystone: Blood Magic
    passive_tree::TreeBonuses tree_bonuses{};
    bool arcane_overload = false;
    if (world.has<Player>(caster) && world.has<PassiveTreeState>(caster)) {
        tree_bonuses = passive_tree::compute_bonuses(world.get<PassiveTreeState>(caster));
        if (tree_bonuses.blood_magic) blood_magic = true;
        // Check Arcane Overload flag
        auto& tree_state = world.get<PassiveTreeState>(caster);
        int ao_idx = static_cast<int>(EffectType::CAP_ARCANE_OVERLOAD) - static_cast<int>(EffectType::CAP_WHIRLWIND);
        if (ao_idx >= 0 && ao_idx < PassiveTreeState::MAX_CAPSTONES && tree_state.capstone_cooldowns[ao_idx] == -1) {
            arcane_overload = true;
        }
    }

    int actual_cost = arcane_overload ? 0 : info.mp_cost;
    // Spell Glutton trait: spells cost half MP
    if (!arcane_overload && world.has<Player>(caster)) {
        auto& player = world.get<Player>(caster);
        for (auto tid : player.traits) {
            if (tid == TraitId::SPELL_GLUTTON) { actual_cost = actual_cost / 2; break; }
        }
    }
    // Skill cost reduction from spell school proficiency
    if (!arcane_overload && world.has<Player>(caster) && world.has<Skills>(caster)) {
        auto& skills = world.get<Skills>(caster);
        SkillId school_skill = SkillId::CONJURATION;
        switch (info.school) {
            case SpellSchool::CONJURATION:   school_skill = SkillId::CONJURATION; break;
            case SpellSchool::TRANSMUTATION: school_skill = SkillId::TRANSMUTATION; break;
            case SpellSchool::DIVINATION:    school_skill = SkillId::DIVINATION; break;
            case SpellSchool::HEALING:       school_skill = SkillId::HEALING; break;
            case SpellSchool::NATURE:        school_skill = SkillId::NATURE_MAGIC; break;
            case SpellSchool::DARK_ARTS:     school_skill = SkillId::DARK_ARTS; break;
        }
        int reduce_pct = skill_bonus::spell_cost_reduce(skills.get_level(school_skill));
        // Also tree bonus
        if (tree_bonuses.spell_cost_reduce > 0) reduce_pct += tree_bonuses.spell_cost_reduce;
        if (reduce_pct > 0) actual_cost = actual_cost * (100 - reduce_pct) / 100;
        if (actual_cost < 1 && info.mp_cost > 0) actual_cost = 1;
    }

    // Unique effect: FREE_CAST (20% chance spells cost no MP)
    if (!arcane_overload && actual_cost > 0 && world.has<Inventory>(caster)) {
        auto& inv = world.get<Inventory>(caster);
        for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
            Entity eq = inv.equipped[s];
            if (eq != NULL_ENTITY && world.has<Item>(eq) &&
                world.get<Item>(eq).unique_effect == UniqueEffect::FREE_CAST) {
                RNG& local_rng = rng; // use the rng passed in
                if (local_rng.range(1, 100) <= 20) {
                    actual_cost = 0;
                    log.add("The spell flows freely.", {180, 200, 255, 255});
                }
                break;
            }
        }
    }

    if (!arcane_overload) {
        if (blood_magic) {
            if (stats.hp <= actual_cost) {
                log.add("Not enough blood to give.", {200, 60, 60, 255});
                result.consumed_turn = false;
                return result;
            }
        } else {
            if (stats.mp < actual_cost) {
                if (world.has<Player>(caster))
                    log.add("Not enough mana.", {150, 120, 150, 255});
                result.consumed_turn = false;
                return result;
            }
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
        // Heavy Armor skill reduces spell failure
        if (world.has<Skills>(caster))
            fail_chance -= skill_bonus::armor_spell_penalty_reduce(
                world.get<Skills>(caster).get_level(SkillId::HEAVY_ARMOR));
        if (fail_chance > 0 && rng.chance(fail_chance)) {
            if (blood_magic) stats.hp -= actual_cost;
            else stats.mp -= actual_cost;
            log.add("Your armor interferes. The spell fizzles.", {180, 130, 130, 255});
            result.consumed_turn = true;
            result.success = false;
            return result;
        }
    }

    // Deduct cost (blood or mana)
    if (!arcane_overload) {
        if (blood_magic) {
            stats.hp -= actual_cost;
            log.add("Blood for power.", {200, 60, 60, 255});
        } else {
            stats.mp -= actual_cost;
        }
    }

    // Spell power scales with INT + tree bonuses
    int power = info.base_power + stats.attr(Attr::INT) / 3;
    // Blood Magic keystone: +30% spell power
    if (tree_bonuses.blood_magic) power = power * 130 / 100;
    // Tree spell damage percent bonus
    if (tree_bonuses.spell_dmg_pct > 0) power = power * (100 + tree_bonuses.spell_dmg_pct) / 100;
    // Spell cost reduction from tree
    // (already handled via actual_cost for Arcane Overload; general reduction TODO)
    // Arcane Overload: 2x damage
    if (arcane_overload) {
        power *= 2;
        // Consume the overload, start real cooldown
        if (world.has<PassiveTreeState>(caster)) {
            auto& ts = world.get<PassiveTreeState>(caster);
            int ao_idx = static_cast<int>(EffectType::CAP_ARCANE_OVERLOAD) - static_cast<int>(EffectType::CAP_WHIRLWIND);
            ts.capstone_cooldowns[ao_idx] = 20;
        }
        log.add("Arcane Overload!", {100, 160, 255, 255});
    }
    bool is_player = world.has<Player>(caster);

    // Helper: handle spell kill XP grant
    auto spell_kill_xp = [&](Entity target) {
        int xp = combat::kill(world, target, log);
        if (is_player && xp > 0) stats.grant_xp(xp);
    };

    // Helper: single-target damage spell with optional status effect
    auto do_single_target_dmg = [&](StatusType status_type = StatusType::POISON,
                                     int status_dmg = 0, int status_turns = 0) -> bool {
        Entity target = nearest_enemy(world, caster, map, info.range);
        if (target == NULL_ENTITY) {
            if (is_player) log.add("No target in range.", {140, 130, 120, 255});
            if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
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
        case SpellId::FIREBALL: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
                result.consumed_turn = false; break;
            }
            auto& tgt = world.get<Stats>(target);
            int dmg = power + rng.range(0, power / 3);
            // Shatter combo: frozen + fire = bonus damage
            bool shattered = false;
            if (world.has<StatusEffects>(target)) {
                auto& se = world.get<StatusEffects>(target);
                for (auto& fx : se.effects) {
                    if (fx.type == StatusType::FROZEN) {
                        dmg *= 2;
                        fx.turns_remaining = 0; // remove frozen
                        shattered = true;
                        break;
                    }
                }
            }
            tgt.hp -= dmg;
            if (!world.has<StatusEffects>(target)) world.add<StatusEffects>(target, {});
            world.get<StatusEffects>(target).add(StatusType::BURN, 3, 3);
            if (is_player) {
                char buf[128];
                if (shattered)
                    snprintf(buf, sizeof(buf), "Fireball shatters the frozen %s! %d damage!", tgt.name.c_str(), dmg);
                else
                    snprintf(buf, sizeof(buf), "Fireball hits the %s. %d dmg, burning.", tgt.name.c_str(), dmg);
                log.add(buf, {255, 160, 60, 255});
            }
            // Leave burning ground on target tile
            if (world.has<Position>(target)) {
                auto& tp = world.get<Position>(target);
                if (map.in_bounds(tp.x, tp.y) && map.is_walkable(tp.x, tp.y))
                    map.at(tp.x, tp.y).type = TileType::LAVA; // burning ground
            }
            if (tgt.hp <= 0 && !world.has<Player>(target)) spell_kill_xp(target);
            result.success = true;
            break;
        }
        case SpellId::ICE_SHARD: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
                result.consumed_turn = false; break;
            }
            auto& tgt = world.get<Stats>(target);
            int dmg = power + rng.range(0, power / 3);
            // Steam combo: burning + ice = blind AoE
            bool steamed = false;
            if (world.has<StatusEffects>(target)) {
                auto& se = world.get<StatusEffects>(target);
                for (auto& fx : se.effects) {
                    if (fx.type == StatusType::BURN) {
                        fx.turns_remaining = 0; // remove burn
                        steamed = true;
                        break;
                    }
                }
            }
            tgt.hp -= dmg;
            if (!world.has<StatusEffects>(target)) world.add<StatusEffects>(target, {});
            world.get<StatusEffects>(target).add(StatusType::FROZEN, 0, 2);
            if (steamed) {
                // Steam cloud: blind all visible enemies
                auto& ai_pool2 = world.pool<AI>();
                for (size_t j = 0; j < ai_pool2.size(); j++) {
                    Entity ae = ai_pool2.entity_at(j);
                    if (world.has<StatusEffects>(ae))
                        world.get<StatusEffects>(ae).add(StatusType::BLIND, 0, 3);
                }
                if (is_player) log.add("Steam erupts! All enemies blinded.", {180, 200, 255, 255});
            }
            if (is_player && !steamed) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Ice pierces the %s. %d dmg, frozen.", tgt.name.c_str(), dmg);
                log.add(buf, {140, 200, 255, 255});
            }
            if (tgt.hp <= 0 && !world.has<Player>(target)) spell_kill_xp(target);
            result.success = true;
            break;
        }
        case SpellId::LIGHTNING: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
                result.consumed_turn = false; break;
            }
            auto& tgt = world.get<Stats>(target);
            int dmg = power + rng.range(0, power / 3);
            // Water combo: if target is on deep water, chain to all adjacent
            bool water_chain = false;
            if (world.has<Position>(target)) {
                auto& tp = world.get<Position>(target);
                if (map.in_bounds(tp.x, tp.y) && map.at(tp.x, tp.y).type == TileType::DEEP_WATER) {
                    water_chain = true;
                    // Damage all entities adjacent to the water tile
                    static const int DX[] = {-1, 0, 1, -1, 1, -1, 0, 1};
                    static const int DY[] = {-1, -1, -1, 0, 0, 1, 1, 1};
                    for (int d = 0; d < 8; d++) {
                        Entity adj = combat::entity_at(world, tp.x + DX[d], tp.y + DY[d], caster);
                        if (adj != NULL_ENTITY && world.has<Stats>(adj) && !world.has<Player>(adj)) {
                            world.get<Stats>(adj).hp -= dmg / 2;
                            if (world.has<StatusEffects>(adj))
                                world.get<StatusEffects>(adj).add(StatusType::STUNNED, 0, 1);
                        }
                    }
                }
            }
            tgt.hp -= dmg;
            // Lightning stuns
            if (world.has<StatusEffects>(target))
                world.get<StatusEffects>(target).add(StatusType::STUNNED, 0, 1);
            if (is_player) {
                char buf[128];
                if (water_chain)
                    snprintf(buf, sizeof(buf), "Lightning arcs through the water! %d dmg + chain stun!", dmg);
                else
                    snprintf(buf, sizeof(buf), "Lightning strikes the %s. %d dmg, stunned.", tgt.name.c_str(), dmg);
                log.add(buf, {200, 200, 255, 255});
            }
            if (tgt.hp <= 0 && !world.has<Player>(target)) spell_kill_xp(target);
            result.success = true;
            break;
        }
        case SpellId::METEOR: {
            // AoE: damage target + all adjacent
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
                result.consumed_turn = false; break;
            }
            auto& tgt = world.get<Stats>(target);
            int dmg = power + rng.range(0, power / 3);
            tgt.hp -= dmg;
            if (!world.has<StatusEffects>(target)) world.add<StatusEffects>(target, {});
            world.get<StatusEffects>(target).add(StatusType::BURN, 3, 3);
            world.get<StatusEffects>(target).add(StatusType::STUNNED, 0, 1);
            // Splash damage to adjacent
            int splash = 0;
            if (world.has<Position>(target)) {
                auto& tp = world.get<Position>(target);
                static const int DX[] = {-1, 0, 1, -1, 1, -1, 0, 1};
                static const int DY[] = {-1, -1, -1, 0, 0, 1, 1, 1};
                for (int d = 0; d < 8; d++) {
                    Entity adj = combat::entity_at(world, tp.x + DX[d], tp.y + DY[d], caster);
                    if (adj != NULL_ENTITY && world.has<Stats>(adj) && !world.has<Player>(adj)) {
                        world.get<Stats>(adj).hp -= dmg / 2;
                        if (world.has<StatusEffects>(adj))
                            world.get<StatusEffects>(adj).add(StatusType::BURN, 2, 2);
                        splash++;
                    }
                }
            }
            if (is_player) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Meteor crashes down! %d dmg%s.", dmg,
                         splash > 0 ? ", splash burns nearby" : "");
                log.add(buf, {255, 200, 60, 255});
            }
            if (tgt.hp <= 0 && !world.has<Player>(target)) spell_kill_xp(target);
            result.success = true;
            break;
        }
        case SpellId::DISINTEGRATE: {
            // Massive single target, destroys corpse (no raise dead possible)
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
                result.consumed_turn = false; break;
            }
            auto& tgt = world.get<Stats>(target);
            int dmg = power * 2 + rng.range(0, power);
            tgt.hp -= dmg;
            if (is_player) {
                char buf[128];
                snprintf(buf, sizeof(buf), "A beam of annihilation hits the %s. %d damage.", tgt.name.c_str(), dmg);
                log.add(buf, {255, 255, 200, 255});
            }
            if (tgt.hp <= 0 && !world.has<Player>(target)) {
                // Grant XP before destroying
                if (is_player && tgt.xp_value > 0) stats.grant_xp(tgt.xp_value);
                // Destroy completely, no corpse
                world.destroy(target);
                if (is_player) log.add("Nothing remains.", {200, 200, 180, 255});
            }
            result.success = true;
            break;
        }
        case SpellId::ACID_SPLASH: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
                result.consumed_turn = false;
                break;
            }
            auto& tgt = world.get<Stats>(target);
            int dmg = power + rng.range(0, power / 3);
            tgt.hp -= dmg;
            tgt.natural_armor = std::max(0, tgt.natural_armor - 2);
            if (is_player) { char buf[128]; snprintf(buf, sizeof(buf), "Acid corrodes the %s. %d dmg, -2 armor.", tgt.name.c_str(), dmg); log.add(buf, {160, 200, 80, 255}); }
            if (tgt.hp <= 0 && !world.has<Player>(target)) spell_kill_xp(target);
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
                if (tgt.hp <= 0 && !world.has<Player>(e)) spell_kill_xp(e);
            });
            // Freeze nearby water tiles into walkable ice (ice bridge)
            if (world.has<Position>(caster)) {
                auto& cp = world.get<Position>(caster);
                int frozen_tiles = 0;
                for (int dy = -3; dy <= 3; dy++)
                    for (int dx = -3; dx <= 3; dx++) {
                        int fx = cp.x + dx, fy = cp.y + dy;
                        if (map.in_bounds(fx, fy) &&
                            (map.at(fx, fy).type == TileType::WATER || map.at(fx, fy).type == TileType::DEEP_WATER)) {
                            map.at(fx, fy).type = TileType::FLOOR_ICE;
                            frozen_tiles++;
                        }
                    }
                if (frozen_tiles > 0 && is_player) {
                    char fb[48]; snprintf(fb, sizeof(fb), "Water freezes solid. (%d tiles)", frozen_tiles);
                    log.add(fb, {140, 200, 255, 255});
                }
            }
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
                    if (tgt.hp <= 0 && !world.has<Player>(e)) spell_kill_xp(e);
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
            if (world.has<Buffs>(caster) && !world.get<Buffs>(caster).has(BuffType::HARDEN_SKIN)) {
                stats.natural_armor += 2;
                world.get<Buffs>(caster).add(BuffType::HARDEN_SKIN, 20, 2);
                if (is_player) log.add("+2 armor, 20 turns.", {160, 160, 140, 255});
            } else if (is_player) log.add("Already active.", {140, 130, 120, 255});
            result.success = true;
            break;
        case SpellId::HASTEN:
            if (world.has<Buffs>(caster) && !world.get<Buffs>(caster).has(BuffType::HASTEN)) {
                stats.base_speed += 30;
                world.get<Buffs>(caster).add(BuffType::HASTEN, 15, 30);
                if (is_player) log.add("+30 speed, 15 turns.", {200, 200, 140, 255});
            } else if (is_player) log.add("Already active.", {140, 130, 120, 255});
            result.success = true;
            break;
        case SpellId::STONE_FIST:
            if (world.has<Buffs>(caster) && !world.get<Buffs>(caster).has(BuffType::STONE_FIST)) {
                stats.base_damage += 3;
                world.get<Buffs>(caster).add(BuffType::STONE_FIST, 15, 3);
                if (is_player) log.add("+3 damage, 15 turns.", {160, 140, 120, 255});
            } else if (is_player) log.add("Already active.", {140, 130, 120, 255});
            result.success = true;
            break;
        case SpellId::IRON_BODY:
            if (world.has<Buffs>(caster) && !world.get<Buffs>(caster).has(BuffType::IRON_BODY)) {
                stats.natural_armor += 4;
                stats.base_speed -= 10;
                world.get<Buffs>(caster).add(BuffType::IRON_BODY, 20, 4, 10);
                if (is_player) log.add("+4 armor, -10 speed, 20 turns.", {160, 160, 160, 255});
            } else if (is_player) log.add("Already active.", {140, 130, 120, 255});
            result.success = true;
            break;
        case SpellId::SLOW: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
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
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
                result.consumed_turn = false; break;
            }
            auto& tgt = world.get<Stats>(target);
            // Bosses, paragons, and high-level creatures resist
            bool immune = false;
            if (tgt.xp_value >= 100) immune = true; // bosses/paragons/dragons
            if (!immune) {
                // WIL save: d20 + target WIL/2 vs caster INT
                int save_roll = rng.range(1, 20) + tgt.attr(Attr::WIL) / 2;
                int dc = stats.attr(Attr::INT);
                if (save_roll >= dc) {
                    if (is_player) {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "The %s resists the polymorph.", tgt.name.c_str());
                        log.add(buf, {180, 140, 160, 255});
                    }
                    result.success = true;
                    break;
                }
            } else {
                if (is_player) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "The %s is immune to polymorph.", tgt.name.c_str());
                    log.add(buf, {180, 140, 160, 255});
                }
                result.success = true;
                break;
            }
            // Polymorph succeeds: temporary (5 turns), store original stats
            // For simplicity: just set to rat stats. After 5 turns the creature dies
            // (rats with 1 HP don't survive long). Not a permanent delete button.
            tgt.name = "rat"; tgt.hp = 1; tgt.hp_max = 1; tgt.base_damage = 1; tgt.natural_armor = 0;
            tgt.base_speed = 130;
            if (world.has<Renderable>(target)) {
                auto& r = world.get<Renderable>(target);
                r.sprite_sheet = SHEET_ANIMALS; r.sprite_x = 0; r.sprite_y = 0;
            }
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
            // Also reveal all traps
            {
                auto& trap_pool = world.pool<Trap>();
                for (size_t ti = 0; ti < trap_pool.size(); ti++) {
                    auto& trap = trap_pool.at_index(ti);
                    if (!trap.revealed) {
                        trap.revealed = true;
                        Entity te = trap_pool.entity_at(ti);
                        if (!world.has<Renderable>(te)) {
                            world.add<Renderable>(te, {SHEET_TILES, trap.sprite_x, trap.sprite_y,
                                                       {255, 200, 100, 200}, -2});
                        }
                    }
                }
            }
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
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
                result.consumed_turn = false;
            }
            result.success = found;
            break;
        }
        case SpellId::FORESIGHT:
            if (world.has<Buffs>(caster) && !world.get<Buffs>(caster).has(BuffType::FORESIGHT)) {
                stats.natural_armor += 1;
                world.get<Buffs>(caster).add(BuffType::FORESIGHT, 15, 1);
                if (is_player) log.add("+1 armor, 15 turns.", {120, 140, 200, 255});
            } else if (is_player) log.add("Already active.", {140, 130, 120, 255});
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
            if (world.has<Buffs>(caster) && !world.get<Buffs>(caster).has(BuffType::SHIELD_OF_FAITH)) {
                stats.natural_armor += 3;
                world.get<Buffs>(caster).add(BuffType::SHIELD_OF_FAITH, 20, 3);
                if (is_player) log.add("+3 armor, 20 turns.", {200, 200, 140, 255});
            } else if (is_player) log.add("Already active.", {140, 130, 120, 255});
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
            if (world.has<Buffs>(caster) && !world.get<Buffs>(caster).has(BuffType::SANCTUARY)) {
                stats.natural_armor += 5;
                world.get<Buffs>(caster).add(BuffType::SANCTUARY, 12, 5);
                // Also make enemies lose track
                auto& ai_pool = world.pool<AI>();
                for (size_t i = 0; i < ai_pool.size(); i++)
                    ai_pool.at_index(i).state = AIState::IDLE;
                if (is_player) log.add("+5 armor, 12 turns. Enemies lose track.", {200, 220, 140, 255});
            } else if (is_player) log.add("Already active.", {140, 130, 120, 255});
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
            // Root + damage all visible enemies (stun 2 turns + minor damage)
            int count = do_aoe([&](Entity e) {
                world.get<Stats>(e).hp -= power / 2;
                if (!world.has<StatusEffects>(e)) world.add<StatusEffects>(e, {});
                world.get<StatusEffects>(e).add(StatusType::STUNNED, 0, 2);
            });
            if (is_player && count > 0) {
                char buf[64];
                snprintf(buf, sizeof(buf), "Vines erupt from the ground! %d rooted for 2 turns.", count);
                log.add(buf, {80, 180, 80, 255});
            }
            result.success = count > 0;
            break;
        }
        case SpellId::BEAST_CALL: {
            // Summon 2 friendly wolves near the caster
            enforce_summon_cap(world);
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
                AI wai; wai.state = AIState::HUNTING; wai.friendly = true;
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
            // Damage ALL enemies on floor, stun adjacent, create rubble
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
                if (es.hp <= 0 && !world.has<Player>(e)) spell_kill_xp(e);
                count++;
            }
            // Collapse nearby walls (opens paths): 1-3 wall tiles become floor
            int collapsed = 0;
            for (int a = 0; a < 20 && collapsed < rng.range(1, 3); a++) {
                int wx = cpos.x + rng.range(-3, 3);
                int wy = cpos.y + rng.range(-3, 3);
                if (!map.in_bounds(wx, wy)) continue;
                auto wt = map.at(wx, wy).type;
                if (wt == TileType::WALL_STONE_BRICK || wt == TileType::WALL_STONE_ROUGH ||
                    wt == TileType::WALL_CATACOMB || wt == TileType::WALL_IGNEOUS) {
                    map.at(wx, wy).type = TileType::FLOOR_STONE;
                    collapsed++;
                }
            }
            // Create rubble on open tiles
            int rubble = 0;
            for (int a = 0; a < 30 && rubble < rng.range(2, 4); a++) {
                int rx = cpos.x + rng.range(-3, 3);
                int ry = cpos.y + rng.range(-3, 3);
                if (rx == cpos.x && ry == cpos.y) continue;
                if (!map.in_bounds(rx, ry) || !map.is_walkable(rx, ry)) continue;
                if (combat::entity_at(world, rx, ry, caster) != NULL_ENTITY) continue;
                map.at(rx, ry).type = TileType::ROCK;
                rubble++;
            }
            if (is_player) {
                char buf[128];
                snprintf(buf, sizeof(buf), "The earth splits! %d hit, %d walls collapsed, %d rubble.", count, collapsed, rubble);
                log.add(buf, {180, 140, 80, 255});
            }
            result.success = count > 0 || rubble > 0;
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
                    if (tgt.hp <= 0 && !world.has<Player>(e)) spell_kill_xp(e);
                    hits++;
                }
            }
            if (is_player) { char buf[64]; snprintf(buf, sizeof(buf), "Lightning storms through %d targets.", hits); log.add(buf, {180, 200, 255, 255}); }
            result.success = hits > 0;
            break;
        }
        case SpellId::BARKSKIN:
            if (world.has<Buffs>(caster) && !world.get<Buffs>(caster).has(BuffType::BARKSKIN)) {
                stats.natural_armor += 3;
                stats.poison_resist += 15;
                world.get<Buffs>(caster).add(BuffType::BARKSKIN, 25, 3, 15);
                if (is_player) log.add("+3 armor, +15% poison resist, 25 turns.", {80, 160, 80, 255});
            } else if (is_player) log.add("Already active.", {140, 130, 120, 255});
            result.success = true;
            break;
        case SpellId::SWARM: {
            enforce_summon_cap(world);
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
                world.add<StatusEffects>(rat);
                AI rai; rai.state = AIState::HUNTING; rai.friendly = true;
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
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
                result.consumed_turn = false;
                break;
            }
            auto& tgt = world.get<Stats>(target);
            int dmg = power + rng.range(0, power / 4);
            tgt.hp -= dmg;
            int healed = std::min(dmg / 2, stats.hp_max - stats.hp);
            stats.hp += healed;
            if (is_player) { char buf[128]; snprintf(buf, sizeof(buf), "Drain %s. %d dmg, +%d HP.", tgt.name.c_str(), dmg, healed); log.add(buf, {140, 80, 160, 255}); }
            if (tgt.hp <= 0 && !world.has<Player>(target)) { spell_kill_xp(target); }
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
            enforce_summon_cap(world);
            // Find nearest corpse entity and raise it as a friendly skeleton
            if (!world.has<Position>(caster)) break;
            auto& cpos = world.get<Position>(caster);
            bool raised = false;
            auto& corpse_pool = world.pool<Corpse>();
            Entity best_corpse = 0;
            int best_dist = 999;
            for (size_t ci = 0; ci < corpse_pool.size(); ci++) {
                Entity ce = corpse_pool.entity_at(ci);
                if (!world.has<Position>(ce)) continue;
                auto& cp = world.get<Position>(ce);
                int cd = std::max(std::abs(cp.x - cpos.x), std::abs(cp.y - cpos.y));
                if (cd <= 4 && cd < best_dist) {
                    best_dist = cd;
                    best_corpse = ce;
                }
            }
            if (best_corpse != 0 && world.has<Position>(best_corpse)) {
                auto& cp = world.get<Position>(best_corpse);
                std::string corpse_name = world.has<Corpse>(best_corpse)
                    ? world.get<Corpse>(best_corpse).name : "skeleton";
                Entity sk = world.create();
                world.add<Position>(sk, {cp.x, cp.y});
                world.add<Renderable>(sk, {SHEET_MONSTERS, 0, 4, {200, 200, 180, 255}, 5});
                Stats ss;
                ss.name = "risen " + corpse_name;
                ss.hp = 10 + power; ss.hp_max = ss.hp;
                ss.base_damage = 3 + power / 4; ss.xp_value = 0;
                world.add<Stats>(sk, std::move(ss));
                world.add<StatusEffects>(sk);
                AI sai; sai.state = AIState::HUNTING; sai.friendly = true;
                world.add<AI>(sk, sai);
                world.add<Energy>(sk, {0, 90});
                // Remove the corpse
                world.destroy(best_corpse);
                raised = true;
            }
            if (is_player) {
                if (raised) log.add("The dead rise to serve you.", {140, 80, 160, 255});
                else log.add("No corpses nearby.", {140, 130, 120, 255});
            }
            result.success = raised;
            if (!raised) { if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost; }
            break;
        }
        case SpellId::HEX: {
            // Curse: confuse + weaken (reduce damage and speed permanently)
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
                result.consumed_turn = false;
                break;
            }
            auto& tgt = world.get<Stats>(target);
            if (!world.has<StatusEffects>(target)) world.add<StatusEffects>(target, {});
            world.get<StatusEffects>(target).add(StatusType::CONFUSED, 0, 5);
            tgt.base_damage = std::max(1, tgt.base_damage - 2);
            tgt.base_speed = std::max(50, tgt.base_speed - 20);
            if (is_player) {
                char buf[128];
                snprintf(buf, sizeof(buf), "The %s is hexed. Confused, -2 damage, -20 speed.", tgt.name.c_str());
                log.add(buf, {160, 80, 180, 255});
            }
            result.success = true;
            break;
        }
        case SpellId::SOUL_REND:
            do_single_target_dmg(StatusType::BLEED, 3, 4);
            break;
        case SpellId::DARKNESS: {
            // Blind all visible enemies AND grant caster invisibility
            int count = do_aoe([&](Entity e) {
                if (!world.has<StatusEffects>(e)) world.add<StatusEffects>(e, {});
                world.get<StatusEffects>(e).add(StatusType::BLIND, 0, 5);
            });
            // Caster gets invisible turns (like Zhavek stealth)
            stats.invisible_turns = std::max(stats.invisible_turns, 4);
            if (is_player) {
                if (count > 0) log.add("Darkness swallows the room. You vanish into shadow.", {80, 60, 120, 255});
                else log.add("Darkness wraps around you. You are hidden.", {80, 60, 120, 255});
            }
            result.success = true;
            break;
        }
        case SpellId::WITHER: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
                result.consumed_turn = false; break;
            }
            auto& tgt = world.get<Stats>(target);
            int dmg = power + rng.range(0, power / 3);
            tgt.hp -= dmg;
            tgt.hp_max = std::max(1, tgt.hp_max - 5); // permanent max HP reduction
            if (is_player) { char buf[128]; snprintf(buf, sizeof(buf), "The %s withers. %d dmg, -5 max HP.", tgt.name.c_str(), dmg); log.add(buf, {140, 80, 160, 255}); }
            if (tgt.hp <= 0 && !world.has<Player>(target)) spell_kill_xp(target);
            result.success = true;
            break;
        }
        case SpellId::BLOOD_PACT: {
            // Sacrifice MAX HP (permanent) for a random powerful bonus
            if (stats.hp_max <= 15) {
                if (is_player) log.add("Your body can't survive another pact.", {200, 80, 80, 255});
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
                result.consumed_turn = false; break;
            }
            int sacrifice = 10 + stats.level;
            stats.hp_max -= sacrifice;
            if (stats.hp > stats.hp_max) stats.hp = stats.hp_max;
            // Random powerful bonus
            int roll = rng.range(1, 6);
            const char* bonus_desc = "";
            switch (roll) {
                case 1: stats.base_damage += 4; bonus_desc = "+4 permanent damage"; break;
                case 2: stats.natural_armor += 3; bonus_desc = "+3 permanent armor"; break;
                case 3: stats.mp_max += 15; stats.mp += 15; bonus_desc = "+15 permanent MP"; break;
                case 4: stats.base_speed += 15; bonus_desc = "+15 permanent speed"; break;
                case 5:
                    stats.set_attr(Attr::STR, stats.attr(Attr::STR) + 3);
                    stats.set_attr(Attr::INT, stats.attr(Attr::INT) + 3);
                    bonus_desc = "+3 STR, +3 INT"; break;
                case 6: stats.base_damage += 2; stats.natural_armor += 2; stats.base_speed += 10;
                    bonus_desc = "+2 damage, +2 armor, +10 speed"; break;
            }
            if (is_player) {
                char buf[128];
                snprintf(buf, sizeof(buf), "Blood for power. -%d max HP. Gained: %s.", sacrifice, bonus_desc);
                log.add(buf, {180, 40, 40, 255});
            }
            result.success = true;
            break;
        }
        case SpellId::DOOM: {
            Entity target = nearest_enemy(world, caster, map, info.range);
            if (target == NULL_ENTITY) {
                if (is_player) log.add("No target.", {140, 130, 120, 255});
                if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost;
                result.consumed_turn = false; break;
            }
            // Instant kill if target HP < 50, otherwise massive damage
            auto& tgt = world.get<Stats>(target);
            if (tgt.hp < 50) {
                if (is_player) { char buf[128]; snprintf(buf, sizeof(buf), "The %s is marked for death. It falls.", tgt.name.c_str()); log.add(buf, {100, 40, 120, 255}); }
                tgt.hp = 0;
                spell_kill_xp(target);
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
            if (blood_magic) stats.hp += actual_cost; else stats.mp += actual_cost; // refund
            break;
    }

    // Grant spell school skill XP on successful cast
    if (result.success && is_player && world.has<Skills>(caster)) {
        auto& skills = world.get<Skills>(caster);
        SkillId school_skill = SkillId::CONJURATION; // default
        switch (info.school) {
            case SpellSchool::CONJURATION:   school_skill = SkillId::CONJURATION; break;
            case SpellSchool::TRANSMUTATION: school_skill = SkillId::TRANSMUTATION; break;
            case SpellSchool::DIVINATION:    school_skill = SkillId::DIVINATION; break;
            case SpellSchool::HEALING:       school_skill = SkillId::HEALING; break;
            case SpellSchool::NATURE:        school_skill = SkillId::NATURE_MAGIC; break;
            case SpellSchool::DARK_ARTS:     school_skill = SkillId::DARK_ARTS; break;
        }
        skills.grant_xp(school_skill, 3);
    }

    return result;
}

} // namespace magic
