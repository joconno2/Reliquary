#include "systems/status.h"
#include "components/position.h"
#include "components/stats.h"
#include "components/status_effect.h"
#include "components/buff.h"
#include "components/disease.h"
#include "components/god.h"
#include "components/ai.h"
#include "components/energy.h"
#include "components/renderable.h"
#include "components/prayer.h"
#include "core/spritesheet.h"
#include "core/tilemap.h"
#include "core/rng.h"
#include "core/audio.h"
#include "ui/message_log.h"
#include "systems/particles.h"
#include "systems/combat.h"
#include "components/inventory.h"
#include "components/item.h"
#include "components/skills.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace status {

EffectResult process(World& world, Entity player, TileMap& map, RNG& rng,
                     MessageLog& log, Audio& audio, ParticleSystem& particles,
                     int game_turn, int dungeon_level,
                     const std::string& dungeon_zone) {
    EffectResult result;

    if (!world.has<StatusEffects>(player) || !world.has<Stats>(player)) return result;
    auto& fx = world.get<StatusEffects>(player);
    auto& stats = world.get<Stats>(player);

    bool has_blackblood = world.has<Diseases>(player) &&
                          world.get<Diseases>(player).has(DiseaseId::BLACKBLOOD);

    auto& pp = world.get<Position>(player);
    for (auto& eff : fx.effects) {
        // Blackblood: immune to poison
        if (eff.type == StatusType::POISON && has_blackblood) {
            eff.turns_remaining = 0;
            log.add("Your blackened blood neutralizes the poison.", {80, 40, 80, 255});
            continue;
        }
        // Apply flat resistance from equipment affixes
        int dmg = eff.damage;
        if (eff.type == StatusType::POISON && world.has<Inventory>(player)) {
            auto& inv = world.get<Inventory>(player);
            for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
                Entity eq = inv.equipped[s];
                if (eq == NULL_ENTITY || !world.has<Item>(eq)) continue;
                dmg -= world.get<Item>(eq).get_resist(AffixEffect::RESIST_POISON);
            }
        }
        if (eff.type == StatusType::BURN && world.has<Inventory>(player)) {
            auto& inv = world.get<Inventory>(player);
            for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
                Entity eq = inv.equipped[s];
                if (eq == NULL_ENTITY || !world.has<Item>(eq)) continue;
                dmg -= world.get<Item>(eq).get_resist(AffixEffect::RESIST_FIRE);
            }
        }
        if (dmg < 0) dmg = 0;
        // Apply percentage resistance reduction
        if (eff.type == StatusType::POISON && stats.poison_resist > 0) {
            dmg = dmg * (100 - stats.poison_resist) / 100;
            if (stats.poison_resist >= 100) { eff.turns_remaining = 0; continue; } // immune
        }
        if (eff.type == StatusType::BURN && stats.fire_resist > 0) {
            dmg = dmg * (100 - stats.fire_resist) / 100;
            if (stats.fire_resist >= 100) { eff.turns_remaining = 0; continue; }
        }
        if (eff.type == StatusType::BLEED && stats.bleed_resist > 0) {
            dmg = dmg * (100 - stats.bleed_resist) / 100;
            if (stats.bleed_resist >= 100) { eff.turns_remaining = 0; continue; }
        }
        if (dmg < 0) dmg = 0;
        stats.hp -= dmg;
        if (dmg > 0 && stats.hp <= 0) {
            switch (eff.type) {
                case StatusType::POISON: result.death_cause = "poison"; break;
                case StatusType::BURN:   result.death_cause = "fire"; break;
                case StatusType::BLEED:  result.death_cause = "bleeding"; break;
                default: result.death_cause = "an affliction"; break;
            }
        }
        char buf[128];
        if (dmg > 0) {
            switch (eff.type) {
                case StatusType::POISON:
                    snprintf(buf, sizeof(buf), "Poison burns through your veins. (%d)", dmg);
                    log.add(buf, {100, 200, 100, 255});
                    audio.play(SfxId::POISON);
                    particles.poison_effect(pp.x, pp.y);
                    break;
                case StatusType::BURN:
                    snprintf(buf, sizeof(buf), "Fire sears your flesh. (%d)", dmg);
                    log.add(buf, {255, 160, 60, 255});
                    audio.play(SfxId::BURN);
                    particles.burn_effect(pp.x, pp.y);
                    break;
                case StatusType::BLEED:
                    snprintf(buf, sizeof(buf), "Blood seeps from your wounds. (%d)", dmg);
                    log.add(buf, {200, 80, 80, 255});
                    particles.bleed_effect(pp.x, pp.y);
                    break;
                default: break; // non-DOT statuses (frozen, stunned, etc.) don't tick damage
            }
        }
    }
    fx.tick();

    // Disease tick effects
    if (world.has<Diseases>(player)) {
        auto& diseases = world.get<Diseases>(player);

        // Sporebloom: regen 1 HP every 5 turns in dungeons
        if (diseases.has(DiseaseId::SPOREBLOOM) && dungeon_level > 0
            && game_turn % 5 == 0 && stats.hp < stats.hp_max) {
            stats.hp++;
        }

        // Vampirism: surface (overworld) hurts — 1 damage every 3 turns
        if (diseases.has(DiseaseId::VAMPIRISM) && dungeon_level <= 0
            && game_turn % 3 == 0) {
            stats.hp--;
            if (game_turn % 15 == 0) // don't spam
                log.add("The sunlight scalds your skin.", {200, 160, 100, 255});
        }
    }

    // Tick spell buffs and revert expired ones
    if (world.has<Buffs>(player)) {
        auto& buffs = world.get<Buffs>(player);
        buffs.tick();
        auto expired = buffs.collect_expired();
        for (auto& b : expired) {
            switch (b.type) {
                case BuffType::HARDEN_SKIN:
                case BuffType::FORESIGHT:
                case BuffType::SHIELD_OF_FAITH:
                case BuffType::SANCTUARY:
                    stats.natural_armor = std::max(0, stats.natural_armor - b.value);
                    break;
                case BuffType::HASTEN:
                    stats.base_speed -= b.value;
                    break;
                case BuffType::STONE_FIST:
                    stats.base_damage = std::max(1, stats.base_damage - b.value);
                    break;
                case BuffType::IRON_BODY:
                    stats.natural_armor = std::max(0, stats.natural_armor - b.value);
                    stats.base_speed += b.value2; // restore speed penalty
                    break;
                case BuffType::BARKSKIN:
                    stats.natural_armor = std::max(0, stats.natural_armor - b.value);
                    stats.poison_resist -= b.value2;
                    break;
            }
            log.add("A spell effect wears off.", {140, 130, 120, 255});
        }
    }

    // Tick god-specific status effects on player
    if (stats.invisible_turns > 0) {
        stats.invisible_turns--;
        // Stealth skill XP while invisible
        if (world.has<Skills>(player))
            world.get<Skills>(player).grant_xp(SkillId::STEALTH, 1);
    }
    if (stats.unyielding_turns > 0) stats.unyielding_turns--;
    if (stats.stone_skin_turns > 0) stats.stone_skin_turns--;

    // Tick drown, sleep, invisible on ALL entities (monsters)
    auto& all_stats_pool = world.pool<Stats>();
    for (size_t i = 0; i < all_stats_pool.size(); i++) {
        Entity e = all_stats_pool.entity_at(i);
        if (e == player) continue;
        auto& es = all_stats_pool.at_index(i);
        // Drown tick
        if (es.drown_turns > 0) {
            es.hp -= es.drown_damage;
            es.drown_turns--;
            if (es.hp <= 0) combat::kill(world, e, log);
        }
        // Sleep tick
        if (es.sleep_turns > 0) es.sleep_turns--;
    }

    // Soleth passive: undead adjacent to player take 1 damage/turn
    if (world.has<GodAlignment>(player)) {
        auto& ga = world.get<GodAlignment>(player);
        if (ga.god == GodId::SOLETH) {
            auto& ai_pool_s = world.pool<AI>();
            for (size_t i = 0; i < ai_pool_s.size(); i++) {
                Entity e = ai_pool_s.entity_at(i);
                if (!world.has<Position>(e) || !world.has<Stats>(e)) continue;
                auto& mp = world.get<Position>(e);
                int dx = std::abs(mp.x - pp.x);
                int dy = std::abs(mp.y - pp.y);
                if (dx <= 1 && dy <= 1) {
                    auto& es = world.get<Stats>(e);
                    if (is_undead(es.name.c_str())) {
                        es.hp -= 1;
                        if (es.hp <= 0) combat::kill(world, e, log);
                    }
                }
            }
        }

        // Sythara passive: 15% chance enemies you damaged get diseased
        // (handled in combat resolution, not here)

        // Thalara passive: fire zones hurt
        if (ga.god == GodId::THALARA && dungeon_level > 0 && game_turn % 3 == 0) {
            if (dungeon_zone == "molten" || dungeon_zone == "molten_depths") {
                stats.hp -= 1;
                if (game_turn % 15 == 0)
                    log.add("The heat sears you. Thalara's domain is water, not fire.", {80, 180, 200, 255});
            }
        }

        // Lethis passive: lethal save (once per floor)
        if (ga.god == GodId::LETHIS && stats.hp <= 0 && !ga.lethal_save_used) {
            ga.lethal_save_used = true;
            stats.hp = 1;
            log.add("You die. Then you wake up.", {160, 120, 200, 255});
            particles.prayer_effect(pp.x, pp.y, 160, 120, 200);
        }

        // === Negative favor punishments (god-specific, escalating) ===
        if (ga.god != GodId::NONE && ga.favor < 0) {
            auto& ginfo = get_god_info(ga.god);

            // Moderate: favor <= -30 — god-specific displeasure
            if (ga.favor <= -30 && game_turn % 40 == 0) {
                switch (ga.god) {
                    case GodId::VETHRIK:
                        // HP drain
                        stats.hp -= 2;
                        if (game_turn % 80 == 0) log.add("Death reaches for you.", {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        break;
                    case GodId::THESSARKA:
                        // Spells cost more (applied in magic system via favor check)
                        if (game_turn % 80 == 0) log.add("Knowledge slips from your mind.", {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        break;
                    case GodId::MORRETH:
                        // Weapon damage penalty (apply -3 to base_damage temporarily)
                        if (game_turn % 80 == 0) log.add("Your arms feel weak. The Iron Father turns away.", {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        break;
                    case GodId::YASHKHET:
                        // Bleed on every hit taken (applied in combat defender section)
                        if (game_turn % 80 == 0) log.add("Your blood turns against you.", {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        break;
                    case GodId::KHAEL:
                        // Poison ticks
                        stats.hp -= 1;
                        if (game_turn % 80 == 0) log.add("Nature rejects you. Thorns grow inward.", {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        break;
                    case GodId::SOLETH:
                        // Burn damage
                        stats.hp -= 2;
                        if (game_turn % 80 == 0) log.add("The Pale Flame sears you from within.", {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        break;
                    case GodId::IXUUL:
                        // Random stat shuffle
                        if (game_turn % 80 == 0) {
                            int a1 = rng.range(0, 5), a2 = rng.range(0, 5);
                            if (a1 != a2) std::swap(stats.attributes[a1], stats.attributes[a2]);
                            log.add("Your form shifts against your will.", {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        }
                        break;
                    case GodId::ZHAVEK:
                        // Gold drain
                        // (gold tracked in engine, not accessible here; just HP drain)
                        stats.hp -= 1;
                        if (game_turn % 80 == 0) log.add("Shadows claw at your life.", {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        break;
                    case GodId::THALARA:
                        // Freeze applied periodically
                        if (world.has<StatusEffects>(player))
                            world.get<StatusEffects>(player).add(StatusType::FROZEN, 0, 1);
                        if (game_turn % 80 == 0) log.add("Cold grips your bones.", {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        break;
                    case GodId::OSSREN:
                        // Armor degrades (stat drain on CON)
                        if (game_turn % 80 == 0 && stats.attributes[2] > 3) {
                            stats.attributes[2]--;
                            log.add("Your body weakens. The Hammer rejects you.", {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        }
                        break;
                    case GodId::LETHIS:
                        // Can't rest (enforced elsewhere); apply fatigue
                        if (game_turn % 80 == 0) log.add("Dreams turn to nightmares. Rest brings no peace.", {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        break;
                    case GodId::GATHRUUN:
                        // Extra damage on surface
                        if (game_turn % 80 == 0) log.add("The earth rejects you. Return below.", {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        break;
                    case GodId::SYTHARA:
                        // Random disease (simplified to poison)
                        if (world.has<StatusEffects>(player))
                            world.get<StatusEffects>(player).add(StatusType::POISON, 1, 5);
                        if (game_turn % 80 == 0) log.add("Your own plague consumes you.", {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                        break;
                    default: break;
                }
                if (stats.hp <= 0) result.death_cause = std::string(ginfo.name) + "'s wrath";
            }

            // Severe: favor <= -60 — escalated HP drain
            if (ga.favor <= -60 && game_turn % 15 == 0) {
                int dmg = 2 + (-ga.favor) / 20;
                stats.hp -= dmg;
                if (stats.hp <= 0) result.death_cause = std::string(ginfo.name) + "'s wrath";
            }

            // Excommunication: favor <= -100 — divine avenger
            if (ga.favor <= -100 && game_turn % 40 == 0) {
                for (int a = 0; a < 30; a++) {
                    int mx = pp.x + rng.range(-4, 4);
                    int my = pp.y + rng.range(-4, 4);
                    if (mx == pp.x && my == pp.y) continue;
                    if (!map.in_bounds(mx, my) || !map.is_walkable(mx, my)) continue;
                    if (combat::entity_at(world, mx, my, player) != NULL_ENTITY) continue;
                    Entity de = world.create();
                    world.add<Position>(de, {mx, my});
                    world.add<Renderable>(de, {SHEET_MONSTERS, 3, 4,
                                                 {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255}, 5});
                    Stats ds; ds.name = "divine avenger"; ds.hp = 30 + stats.level * 5;
                    ds.hp_max = ds.hp; ds.base_damage = 6 + stats.level; ds.base_speed = 110;
                    ds.xp_value = 25 + stats.level * 5;
                    world.add<Stats>(de, std::move(ds));
                    AI summon_ai; summon_ai.state = AIState::HUNTING;
                    summon_ai.last_seen_x = pp.x; summon_ai.last_seen_y = pp.y;
                    world.add<AI>(de, summon_ai);
                    world.add<Energy>(de, {0, 110});
                    if (game_turn % 120 == 0) {
                        char buf[128];
                        snprintf(buf, sizeof(buf), "%s sends an avenger.", ginfo.name);
                        log.add(buf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                    }
                    break;
                }
            }
        }
    }

    // Hazard terrain damage
    if (world.has<Position>(player)) {
        auto& pp = world.get<Position>(player);
        if (map.in_bounds(pp.x, pp.y)) {
            auto tile_type = map.at(pp.x, pp.y).type;
            if (tile_type == TileType::LAVA) {
                int lava_dmg = 5 + dungeon_level;
                stats.hp -= lava_dmg;
                if (game_turn % 3 == 0)
                    log.add("The magma sears your flesh!", {255, 100, 40, 255});
                // Also apply burn
                if (world.has<StatusEffects>(player))
                    world.get<StatusEffects>(player).add(StatusType::BURN, 3, 3);
            }
            if (tile_type == TileType::DEEP_WATER) {
                // Check heavy armor: armor_bonus >= 4 = drowning
                bool heavy = false;
                if (world.has<Inventory>(player)) {
                    Entity chest = world.get<Inventory>(player).get_equipped(EquipSlot::CHEST);
                    if (chest != NULL_ENTITY && world.has<Item>(chest) && world.get<Item>(chest).armor_bonus >= 4)
                        heavy = true;
                }
                if (heavy) {
                    stats.hp -= 3;
                    if (game_turn % 3 == 0)
                        log.add("Your heavy armor drags you under!", {80, 140, 220, 255});
                }
                // Slow regardless
                if (world.has<Energy>(player))
                    world.get<Energy>(player).current -= 30; // lose speed this turn
            }
        }
    }

    // Active curse effects from equipped cursed items
    if (world.has<Inventory>(player) && game_turn % 5 == 0) {
        auto& inv = world.get<Inventory>(player);
        for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
            Entity eq = inv.equipped[s];
            if (eq == NULL_ENTITY || !world.has<Item>(eq)) continue;
            auto& item = world.get<Item>(eq);
            if (item.curse_state != 1) continue;

            // Different curse effects based on slot
            EquipSlot slot = static_cast<EquipSlot>(s);
            if (slot == EquipSlot::MAIN_HAND && rng.chance(10)) {
                // Cursed weapon: occasional self-harm
                int self_dmg = 1 + stats.level / 5;
                stats.hp -= self_dmg;
                if (game_turn % 20 == 0)
                    log.add("Your cursed weapon bites into your hand.", {180, 100, 100, 255});
            } else if (slot == EquipSlot::HEAD && rng.chance(8)) {
                // Cursed helm: periodic confusion
                if (world.has<StatusEffects>(player))
                    world.get<StatusEffects>(player).add(StatusType::CONFUSED, 0, 2);
                if (game_turn % 25 == 0)
                    log.add("Your cursed helm clouds your mind.", {160, 100, 160, 255});
            } else if (slot == EquipSlot::RING_1 || slot == EquipSlot::RING_2) {
                // Cursed ring: slow HP drain
                stats.hp -= 1;
            } else if (slot == EquipSlot::FEET && rng.chance(8)) {
                // Cursed boots: slow you down
                if (world.has<StatusEffects>(player))
                    world.get<StatusEffects>(player).add(StatusType::STUNNED, 0, 1);
                if (game_turn % 25 == 0)
                    log.add("Your cursed boots drag at your feet.", {160, 120, 100, 255});
            }
        }
    }

    // Unique item effects (per-turn)
    if (world.has<Inventory>(player)) {
        auto& inv = world.get<Inventory>(player);
        for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
            Entity eq = inv.equipped[s];
            if (eq == NULL_ENTITY || !world.has<Item>(eq)) continue;
            auto& item = world.get<Item>(eq);
            // REGEN: heal 1 HP every 5 turns
            if (item.unique_effect == UniqueEffect::REGEN && game_turn % 5 == 0 &&
                stats.hp > 0 && stats.hp < stats.hp_max) {
                stats.hp = std::min(stats.hp + 1, stats.hp_max);
            }
            // DREAM_WALK: 10% chance enemies skip turns (applied in AI, but flag here)
            // (handled in AI processing)
        }
    }

    if (stats.hp <= 0) {
        result.player_died = true;
    }

    return result;
}

} // namespace status
