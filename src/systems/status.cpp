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
        // Apply resistance reduction
        int dmg = eff.damage;
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
    if (stats.invisible_turns > 0) stats.invisible_turns--;
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

        // === Negative favor punishments (escalating) ===
        if (ga.god != GodId::NONE && ga.favor < 0) {
            auto& ginfo = get_god_info(ga.god);

            // Mild: favor -1 to -30 — prayer costs doubled (handled in execute_prayer)
            // Moderate: favor -31 to -60 — random stat drain every 50 turns
            if (ga.favor <= -30 && game_turn % 50 == 0) {
                int attr_idx = rng.range(0, 6);
                int cur = stats.attributes[attr_idx];
                if (cur > 3) {
                    stats.attributes[attr_idx] = cur - 1;
                    static const char* ATTR_NAMES[] = {"STR","DEX","CON","INT","WIL","PER","CHA"};
                    char buf[128];
                    snprintf(buf, sizeof(buf), "%s's displeasure weakens you. (-1 %s)", ginfo.name, ATTR_NAMES[attr_idx]);
                    log.add(buf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                }
            }

            // Severe: favor -61 to -99 — periodic HP damage
            if (ga.favor <= -60 && game_turn % 20 == 0) {
                int dmg = 1 + (-ga.favor) / 30;
                stats.hp -= dmg;
                if (stats.hp <= 0) result.death_cause = std::string(ginfo.name) + "'s wrath";
                if (game_turn % 60 == 0) {
                    char buf[128];
                    snprintf(buf, sizeof(buf), "%s punishes your faithlessness. (%d)", ginfo.name, dmg);
                    log.add(buf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
                }
            }

            // Excommunication: favor == -100
            if (ga.favor <= -100 && game_turn % 40 == 0) {
                // Spawn a divine enemy near the player
                for (int a = 0; a < 30; a++) {
                    int mx = pp.x + rng.range(-4, 4);
                    int my = pp.y + rng.range(-4, 4);
                    if (mx == pp.x && my == pp.y) continue;
                    if (!map.in_bounds(mx, my) || !map.is_walkable(mx, my)) continue;
                    if (combat::entity_at(world, mx, my, player) != NULL_ENTITY) continue;
                    Entity de = world.create();
                    world.add<Position>(de, {mx, my});
                    world.add<Renderable>(de, {SHEET_MONSTERS, 3, 4, // death knight sprite
                                                 {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255}, 5});
                    Stats ds; ds.name = "divine avenger"; ds.hp = 30 + stats.level * 5;
                    ds.hp_max = ds.hp; ds.base_damage = 6 + stats.level; ds.base_speed = 110;
                    ds.xp_value = 25 + stats.level * 5;
                    world.add<Stats>(de, std::move(ds));
                    world.add<AI>(de, {AIState::HUNTING, pp.x, pp.y, 0, 0}); // never flees
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

    if (stats.hp <= 0) {
        result.player_died = true;
    }

    return result;
}

} // namespace status
