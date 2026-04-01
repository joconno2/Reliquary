#include "systems/god_system.h"
#include "core/ecs.h"
#include "core/tilemap.h"
#include "core/rng.h"
#include "core/audio.h"
#include "ui/message_log.h"
#include "systems/particles.h"
#include "systems/render.h"
#include "systems/combat.h"
#include "systems/magic.h"
#include "systems/fov.h"
#include "components/god.h"
#include "components/prayer.h"
#include "components/tenet.h"
#include "components/stats.h"
#include "components/position.h"
#include "components/ai.h"
#include "components/inventory.h"
#include "components/item.h"
#include "components/status_effect.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace god_system {

void adjust_favor(World& world, Entity player, MessageLog& log, int amount) {
    if (!world.has<GodAlignment>(player)) return;
    auto& align = world.get<GodAlignment>(player);
    int old_favor = align.favor;
    align.favor = std::max(-100, std::min(100, align.favor + amount));
    if (align.favor != old_favor && amount > 0) {
        char buf[64];
        auto& ginfo = get_god_info(align.god);
        snprintf(buf, sizeof(buf), "%s is pleased. (+%d favor)", ginfo.name, amount);
        log.add(buf, {180, 200, 255, 255});
    } else if (align.favor != old_favor && amount < 0) {
        char buf[64];
        auto& ginfo = get_god_info(align.god);
        snprintf(buf, sizeof(buf), "%s is displeased. (%d favor)", ginfo.name, amount);
        log.add(buf, {255, 160, 120, 255});
    }
}

void check_tenets(World& world, Entity player, const PlayerActions& actions,
                  int game_turn, MessageLog& log) {
    if (!world.has<GodAlignment>(player)) return;
    auto& align = world.get<GodAlignment>(player);
    if (align.god == GodId::NONE) return;

    auto tenets = get_god_tenets(align.god);
    if (!tenets.tenets || tenets.count == 0) return;

    // We need a mutable copy of actions for equipment scanning
    PlayerActions turn_actions = actions;

    for (int i = 0; i < tenets.count; i++) {
        auto& t = tenets.tenets[i];
        bool violated = false;

        switch (t.check) {
            case TenetCheck::NEVER_KILL_ANIMAL:
                violated = turn_actions.killed_animal;
                break;
            case TenetCheck::NEVER_USE_DARK_ARTS:
                violated = turn_actions.used_dark_arts;
                break;
            case TenetCheck::NEVER_USE_FIRE_MAGIC:
                violated = turn_actions.used_fire_magic;
                break;
            case TenetCheck::NEVER_USE_POISON:
                violated = turn_actions.used_poison;
                break;
            case TenetCheck::NEVER_BACKSTAB:
                violated = turn_actions.used_stealth_attack;
                break;
            case TenetCheck::NEVER_FLEE_COMBAT:
                violated = turn_actions.fled_combat;
                break;
            case TenetCheck::NEVER_USE_HEALING_MAGIC:
                violated = turn_actions.used_healing_magic;
                break;
            case TenetCheck::NEVER_WEAR_HEAVY_ARMOR:
                violated = turn_actions.wore_heavy_armor;
                break;
            case TenetCheck::NEVER_CARRY_LIGHT:
                violated = turn_actions.used_light_source;
                break;
            case TenetCheck::NEVER_DESTROY_BOOK:
                violated = turn_actions.destroyed_book;
                break;
            case TenetCheck::NEVER_KILL_SLEEPING:
                violated = turn_actions.killed_sleeping;
                break;
            case TenetCheck::NEVER_DIG_WALLS:
                violated = turn_actions.dug_wall;
                break;
            case TenetCheck::NEVER_REST_ON_SURFACE:
                violated = turn_actions.rested_on_surface;
                break;
            case TenetCheck::NEVER_HEAL_ABOVE_75:
                violated = turn_actions.healed_above_75pct;
                break;
            case TenetCheck::MUST_DESCEND:
                // Checked on floor transition, not per-turn
                break;
            case TenetCheck::MUST_KILL_UNDEAD:
                // Soleth: if undead is visible and you leave the floor without killing it
                // Too complex for per-turn — skip for now
                break;
            case TenetCheck::MUST_REST_EACH_FLOOR:
                // Checked on floor transition
                break;
        }

        if (violated) {
            adjust_favor(world, player, log, t.violation_favor);
            auto& ginfo = get_god_info(align.god);
            char buf[192];
            snprintf(buf, sizeof(buf), "Tenet broken: %s", t.description);
            log.add(buf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
        }
    }

    // Check equipment-based tenet flags (per-turn scan of equipped items)
    if (world.has<Inventory>(player)) {
        auto& inv = world.get<Inventory>(player);
        for (int s = 0; s < EQUIP_SLOT_COUNT; s++) {
            Entity eq = inv.equipped[s];
            if (eq != NULL_ENTITY && world.has<Item>(eq)) {
                auto& item = world.get<Item>(eq);
                if (item.tags & TAG_HEAVY_ARMOR) turn_actions.wore_heavy_armor = true;
                if (item.tags & TAG_TORCH) turn_actions.used_light_source = true;
                if (item.curse_state == 1) turn_actions.carrying_cursed = true;
            }
        }
        // Also check carried (not just equipped) items for torch/cursed
        for (auto ie : inv.items) {
            if (ie != NULL_ENTITY && world.has<Item>(ie)) {
                auto& item = world.get<Item>(ie);
                if (item.tags & TAG_TORCH) turn_actions.used_light_source = true;
                if (item.curse_state == 1) turn_actions.carrying_cursed = true;
            }
        }
    }

    // Soleth: carrying cursed items drains favor slowly
    if (align.god == GodId::SOLETH && turn_actions.carrying_cursed && game_turn % 10 == 0) {
        adjust_favor(world, player, log, -1);
        log.add("The cursed item in your possession offends Soleth.", {255, 220, 100, 255});
    }

    // Passive favor gain: +1 favor every 20 turns with no violations this cycle
    if (game_turn % 20 == 0 && align.favor < 100) {
        // Only gain passive favor if no violation flags were set
        bool clean = !turn_actions.killed_animal && !turn_actions.used_dark_arts
            && !turn_actions.fled_combat && !turn_actions.used_stealth_attack;
        if (clean) {
            align.favor = std::min(100, align.favor + 1);
        }
    }
}

bool execute_prayer(World& world, Entity player, TileMap& map, RNG& rng,
                    MessageLog& log, Audio& audio, ParticleSystem& particles,
                    Camera& camera, int prayer_idx) {
    if (!world.has<GodAlignment>(player) || !world.has<Stats>(player)) return false;
    auto& align = world.get<GodAlignment>(player);
    auto prayers = get_prayers(align.god);
    if (!prayers || prayer_idx < 0 || prayer_idx > 1) return false;

    auto& prayer = prayers[prayer_idx];
    auto& stats = world.get<Stats>(player);

    // Excommunicated — prayers always fail
    if (align.favor <= -100) {
        log.add("You are excommunicated. No god answers.", {180, 80, 80, 255});
        return false;
    }
    // Negative favor — prayers fail below -50
    if (align.favor < -50) {
        auto& ginfo = get_god_info(align.god);
        char buf[128];
        snprintf(buf, sizeof(buf), "%s does not answer.", ginfo.name);
        log.add(buf, {180, 120, 120, 255});
        return false;
    }
    // Negative favor — prayer costs doubled
    int cost = prayer.favor_cost;
    if (align.favor < 0) cost *= 2;
    if (align.favor < cost) {
        log.add("Not enough favor.", {180, 120, 120, 255});
        return false;
    }

    align.favor -= cost;
    bool acted = true;
    audio.play(SfxId::PRAYER);

    auto& ppos = world.get<Position>(player);

    // God-colored prayer particles
    auto& ginfo = get_god_info(align.god);
    uint8_t pr = ginfo.color.r, pg = ginfo.color.g, pb = ginfo.color.b;
    particles.prayer_effect(ppos.x, ppos.y, pr, pg, pb);

    // Execute prayer effect based on god and index
    switch (align.god) {
        case GodId::VETHRIK:
            if (prayer_idx == 0) {
                // Lay to Rest — heal
                int heal = 5 + stats.attr(Attr::WIL) / 2;
                stats.hp = std::min(stats.hp + heal, stats.hp_max);
                char buf[128];
                snprintf(buf, sizeof(buf), "The dead grant you respite. (+%d HP)", heal);
                log.add(buf, {160, 200, 180, 255});
                particles.heal_effect(ppos.x, ppos.y);
            } else {
                // Death's Grasp — damage nearest
                Entity target = magic::nearest_enemy(world, player, map, 8);
                if (target != NULL_ENTITY && world.has<Stats>(target)) {
                    int dmg = stats.attr(Attr::WIL) * 2;
                    auto& tgt = world.get<Stats>(target);
                    tgt.hp -= dmg;
                    char buf[128];
                    snprintf(buf, sizeof(buf), "A skeletal hand tears at the %s. (%d damage)",
                             tgt.name.c_str(), dmg);
                    log.add(buf, {200, 180, 255, 255});
                    if (tgt.hp <= 0) {
                        combat::kill(world, target, log);
                    }
                } else {
                    log.add("There is nothing nearby to grasp.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost; // refund
                    acted = false;
                }
            }
            break;

        case GodId::THESSARKA:
            if (prayer_idx == 0) {
                // Clarity — restore MP
                int restored = stats.mp_max - stats.mp;
                stats.mp = stats.mp_max;
                char buf[128];
                snprintf(buf, sizeof(buf), "Your mind clears. (+%d MP)", restored);
                log.add(buf, {160, 200, 255, 255});
            } else {
                // True Sight — reveal level
                for (int y = 0; y < map.height(); y++) {
                    for (int x = 0; x < map.width(); x++) {
                        map.at(x, y).explored = true;
                    }
                }
                log.add("The veil lifts. You see everything.", {200, 200, 255, 255});
            }
            break;

        case GodId::MORRETH:
            if (prayer_idx == 0) {
                // Iron Resolve — heal
                int heal = 10 + stats.attr(Attr::STR) / 2;
                stats.hp = std::min(stats.hp + heal, stats.hp_max);
                char buf[128];
                snprintf(buf, sizeof(buf), "Iron will sustains you. (+%d HP)", heal);
                log.add(buf, {200, 200, 160, 255});
                particles.heal_effect(ppos.x, ppos.y);
            } else {
                // War Cry — damage all adjacent
                int dmg = stats.attr(Attr::STR);
                int hit_count = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        Entity target = combat::entity_at(world, ppos.x + dx, ppos.y + dy, player);
                        if (target != NULL_ENTITY && world.has<Stats>(target) &&
                            world.has<AI>(target)) {
                            world.get<Stats>(target).hp -= dmg;
                            hit_count++;
                            if (world.get<Stats>(target).hp <= 0) {
                                combat::kill(world, target, log);
                            }
                        }
                    }
                }
                if (hit_count > 0) {
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                        "Your war cry shakes the air. (%d enemies struck for %d)", hit_count, dmg);
                    log.add(buf, {255, 200, 140, 255});
                } else {
                    log.add("Your cry echoes unanswered.", {150, 140, 130, 255});
                }
            }
            break;

        case GodId::YASHKHET:
            if (prayer_idx == 0) {
                // Blood Offering — sacrifice HP for MP, gain favor
                int sacrifice = stats.hp_max / 4;
                if (stats.hp <= sacrifice + 1) {
                    log.add("You don't have enough blood to offer.", {180, 120, 120, 255});
                    acted = false;
                    break;
                }
                stats.hp -= sacrifice;
                int restore = std::min(sacrifice, stats.mp_max - stats.mp);
                stats.mp += restore;
                adjust_favor(world, player, log, 5);
                char buf[128];
                snprintf(buf, sizeof(buf), "Your blood sings. (-%d HP, +%d MP)", sacrifice, restore);
                log.add(buf, {200, 100, 100, 255});
            } else {
                // Sanguine Burst — sacrifice HP, damage nearest
                if (stats.hp <= 11) {
                    log.add("You don't have enough blood.", {180, 120, 120, 255});
                    align.favor += prayer.favor_cost; // refund
                    acted = false;
                    break;
                }
                Entity target = magic::nearest_enemy(world, player, map, 8);
                if (target != NULL_ENTITY && world.has<Stats>(target)) {
                    stats.hp -= 10;
                    int dmg = 10 + stats.attr(Attr::WIL);
                    auto& tgt = world.get<Stats>(target);
                    tgt.hp -= dmg;
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                        "Your blood erupts outward, striking the %s. (%d damage)", tgt.name.c_str(), dmg);
                    log.add(buf, {200, 80, 80, 255});
                    if (tgt.hp <= 0) {
                        combat::kill(world, target, log);
                    }
                } else {
                    log.add("There is nothing nearby to strike.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost; // refund
                    acted = false;
                }
            }
            break;

        case GodId::KHAEL:
            if (prayer_idx == 0) {
                // Regrowth — heal 30%
                int heal = stats.hp_max * 3 / 10;
                stats.hp = std::min(stats.hp + heal, stats.hp_max);
                char buf[128];
                snprintf(buf, sizeof(buf), "Green light mends your wounds. (+%d HP)", heal);
                log.add(buf, {120, 220, 120, 255});
                particles.heal_effect(ppos.x, ppos.y);
            } else {
                // Nature's Wrath — damage nearest
                Entity target = magic::nearest_enemy(world, player, map, 8);
                if (target != NULL_ENTITY && world.has<Stats>(target)) {
                    int dmg = stats.attr(Attr::WIL) + 10;
                    auto& tgt = world.get<Stats>(target);
                    tgt.hp -= dmg;
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                        "Thorns erupt from the ground beneath the %s. (%d damage)", tgt.name.c_str(), dmg);
                    log.add(buf, {100, 200, 100, 255});
                    if (tgt.hp <= 0) {
                        combat::kill(world, target, log);
                    }
                } else {
                    log.add("The wilderness is quiet.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost; // refund
                    acted = false;
                }
            }
            break;

        case GodId::SOLETH:
            if (prayer_idx == 0) {
                // Purifying Flame — damage all adjacent
                int dmg = stats.attr(Attr::WIL) + 5;
                int hit_count = 0;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        if (dx == 0 && dy == 0) continue;
                        Entity target = combat::entity_at(world, ppos.x + dx, ppos.y + dy, player);
                        if (target != NULL_ENTITY && world.has<Stats>(target) &&
                            world.has<AI>(target)) {
                            world.get<Stats>(target).hp -= dmg;
                            hit_count++;
                            if (world.get<Stats>(target).hp <= 0) {
                                combat::kill(world, target, log);
                            }
                        }
                    }
                }
                if (hit_count > 0) {
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                        "Holy fire erupts around you. (%d enemies burned for %d)", hit_count, dmg);
                    log.add(buf, {255, 220, 100, 255});
                } else {
                    log.add("The flame finds nothing to purify.", {150, 140, 130, 255});
                }
            } else {
                // Holy Light — full heal
                int heal = stats.hp_max - stats.hp;
                stats.hp = stats.hp_max;
                char buf[128];
                snprintf(buf, sizeof(buf), "Blinding light restores you. (+%d HP)", heal);
                log.add(buf, {255, 255, 200, 255});
                particles.heal_effect(ppos.x, ppos.y);
            }
            break;

        case GodId::IXUUL:
            if (prayer_idx == 0) {
                // Warp — teleport to random walkable tile
                for (int attempt = 0; attempt < 100; attempt++) {
                    int tx = rng.range(1, map.width() - 2);
                    int ty = rng.range(1, map.height() - 2);
                    if (map.is_walkable(tx, ty)) {
                        ppos.x = tx;
                        ppos.y = ty;
                        fov::compute(map, ppos.x, ppos.y, stats.fov_radius());
                        camera.center_on(ppos.x, ppos.y);
                        log.add("Reality blinks. You are elsewhere.", {180, 120, 255, 255});
                        break;
                    }
                }
            } else {
                // Chaos Surge — random damage to random visible enemy
                std::vector<Entity> visible_enemies;
                auto& ai_pool = world.pool<AI>();
                for (size_t i = 0; i < ai_pool.size(); i++) {
                    Entity e = ai_pool.entity_at(i);
                    if (!world.has<Position>(e) || !world.has<Stats>(e)) continue;
                    auto& mp = world.get<Position>(e);
                    if (map.in_bounds(mp.x, mp.y) && map.at(mp.x, mp.y).visible) {
                        visible_enemies.push_back(e);
                    }
                }
                if (!visible_enemies.empty()) {
                    Entity target = visible_enemies[rng.range(0, static_cast<int>(visible_enemies.size()) - 1)];
                    int dmg = rng.range(5, 35);
                    auto& tgt = world.get<Stats>(target);
                    tgt.hp -= dmg;
                    char buf[128];
                    snprintf(buf, sizeof(buf),
                        "Chaos lashes at the %s. (%d damage)", tgt.name.c_str(), dmg);
                    log.add(buf, {200, 100, 255, 255});
                    if (tgt.hp <= 0) {
                        combat::kill(world, target, log);
                    }
                } else {
                    log.add("The void has nothing to consume.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost; // refund
                    acted = false;
                }
            }
            break;

        case GodId::ZHAVEK:
            if (prayer_idx == 0) {
                // Vanish — invisible for 8 turns or until attack
                stats.invisible_turns = 8;
                log.add("You slip between the spaces. The world forgets you.", {80, 80, 120, 255});
            } else {
                // Silence — all enemies in FOV lose track
                auto& ai_pool2 = world.pool<AI>();
                int silenced = 0;
                for (size_t i = 0; i < ai_pool2.size(); i++) {
                    Entity e = ai_pool2.entity_at(i);
                    if (!world.has<Position>(e)) continue;
                    auto& mp = world.get<Position>(e);
                    if (map.in_bounds(mp.x, mp.y) && map.at(mp.x, mp.y).visible) {
                        auto& ai = world.get<AI>(e);
                        ai.state = AIState::IDLE;
                        ai.target = NULL_ENTITY;
                        silenced++;
                    }
                }
                char buf2[128];
                snprintf(buf2, sizeof(buf2),
                    "Silence falls. %d creatures lose your trail.", silenced);
                log.add(buf2, {80, 80, 120, 255});
            }
            break;

        case GodId::THALARA:
            if (prayer_idx == 0) {
                // Riptide — pull all visible enemies 3 tiles toward player
                auto& ai_pool3 = world.pool<AI>();
                int pulled = 0;
                for (size_t i = 0; i < ai_pool3.size(); i++) {
                    Entity e = ai_pool3.entity_at(i);
                    if (!world.has<Position>(e) || !world.has<Stats>(e)) continue;
                    auto& mp = world.get<Position>(e);
                    if (!map.in_bounds(mp.x, mp.y) || !map.at(mp.x, mp.y).visible) continue;
                    // Pull toward player
                    for (int step = 0; step < 3; step++) {
                        int dx = (ppos.x > mp.x) ? 1 : (ppos.x < mp.x) ? -1 : 0;
                        int dy = (ppos.y > mp.y) ? 1 : (ppos.y < mp.y) ? -1 : 0;
                        int nx = mp.x + dx;
                        int ny = mp.y + dy;
                        if (map.is_walkable(nx, ny) && !(nx == ppos.x && ny == ppos.y)) {
                            mp.x = nx;
                            mp.y = ny;
                        } else break;
                    }
                    pulled++;
                }
                char buf3[128];
                snprintf(buf3, sizeof(buf3),
                    "The tide pulls. %d creatures dragged toward you.", pulled);
                log.add(buf3, {80, 180, 200, 255});
            } else {
                // Drown — deal WIL damage/turn for 5 turns to nearest enemy
                Entity target = magic::nearest_enemy(world, player, map, 8);
                if (target != NULL_ENTITY) {
                    auto& tgt = world.get<Stats>(target);
                    tgt.drown_turns = 5;
                    tgt.drown_damage = stats.attr(Attr::WIL);
                    char buf4[128];
                    snprintf(buf4, sizeof(buf4),
                        "Water fills the %s's lungs.", tgt.name.c_str());
                    log.add(buf4, {80, 180, 200, 255});
                } else {
                    log.add("No one to drown.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost;
                    acted = false;
                }
            }
            break;

        case GodId::OSSREN:
            if (prayer_idx == 0) {
                // Temper — upgrade one equipped item +1 quality
                Entity weapon_e = world.get<Inventory>(player).get_equipped(EquipSlot::MAIN_HAND);
                if (weapon_e != NULL_ENTITY && world.has<Item>(weapon_e)) {
                    auto& item = world.get<Item>(weapon_e);
                    item.damage_bonus += 1;
                    char buf5[128];
                    snprintf(buf5, sizeof(buf5),
                        "Ossren's hand steadies the forge. Your %s is tempered.", item.name.c_str());
                    log.add(buf5, {220, 180, 80, 255});
                    particles.hit_spark(ppos.x, ppos.y);
                } else {
                    log.add("You have nothing to temper.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost;
                    acted = false;
                }
            } else {
                // Unyielding — double armor for 15 turns
                stats.unyielding_turns = 15;
                log.add("Your armor hardens beyond what metal should allow.", {220, 180, 80, 255});
            }
            break;

        case GodId::LETHIS:
            if (prayer_idx == 0) {
                // Sleepwalk — all visible enemies fall asleep for 5 turns
                auto& ai_pool4 = world.pool<AI>();
                int slept = 0;
                for (size_t i = 0; i < ai_pool4.size(); i++) {
                    Entity e = ai_pool4.entity_at(i);
                    if (!world.has<Position>(e) || !world.has<Stats>(e)) continue;
                    auto& mp = world.get<Position>(e);
                    if (map.in_bounds(mp.x, mp.y) && map.at(mp.x, mp.y).visible) {
                        world.get<Stats>(e).sleep_turns = 5;
                        auto& ai = world.get<AI>(e);
                        ai.state = AIState::IDLE;
                        ai.target = NULL_ENTITY;
                        slept++;
                    }
                }
                char buf6[128];
                snprintf(buf6, sizeof(buf6),
                    "A dream passes through. %d creatures slumber.", slept);
                log.add(buf6, {160, 120, 200, 255});
            } else {
                // Forget — target enemy permanently forgets you
                Entity target = magic::nearest_enemy(world, player, map, 8);
                if (target != NULL_ENTITY) {
                    auto& ai = world.get<AI>(target);
                    ai.state = AIState::IDLE;
                    ai.target = NULL_ENTITY;
                    ai.forget_player = true;
                    auto& tgt = world.get<Stats>(target);
                    char buf7[128];
                    snprintf(buf7, sizeof(buf7),
                        "The %s blinks. You were never here.", tgt.name.c_str());
                    log.add(buf7, {160, 120, 200, 255});
                } else {
                    log.add("No one to forget.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost;
                    acted = false;
                }
            }
            break;

        case GodId::GATHRUUN:
            if (prayer_idx == 0) {
                // Tremor — earthquake damage to all enemies on floor (WIL + level based)
                int depth_dmg = stats.attr(Attr::WIL) / 2 + stats.level;
                auto& ai_pool5 = world.pool<AI>();
                int hit_count = 0;
                for (size_t i = 0; i < ai_pool5.size(); i++) {
                    Entity e = ai_pool5.entity_at(i);
                    if (!world.has<Stats>(e)) continue;
                    auto& es = world.get<Stats>(e);
                    es.hp -= depth_dmg;
                    hit_count++;
                    // Stun adjacent enemies (sleep as stun substitute)
                    if (world.has<Position>(e)) {
                        auto& mp = world.get<Position>(e);
                        int dx = std::abs(mp.x - ppos.x);
                        int dy = std::abs(mp.y - ppos.y);
                        if (dx <= 1 && dy <= 1) {
                            es.sleep_turns = 2;
                        }
                    }
                    if (es.hp <= 0) combat::kill(world, e, log);
                }
                char buf8[128];
                snprintf(buf8, sizeof(buf8),
                    "The earth convulses. %d creatures take %d damage.", hit_count, depth_dmg);
                log.add(buf8, {160, 130, 90, 255});
                particles.burst(ppos.x, ppos.y, 20, 160, 130, 90, 0.12f, 0.8f, 6);
            } else {
                // Stone Skin — +10 armor, can't move, 20 turns
                stats.stone_skin_turns = 20;
                stats.stone_skin_armor = 10;
                log.add("Your flesh becomes stone. You cannot move, but nothing can reach you.", {160, 130, 90, 255});
            }
            break;

        case GodId::SYTHARA:
            if (prayer_idx == 0) {
                // Miasma — poison all visible enemies for 10 turns
                auto& ai_pool6 = world.pool<AI>();
                int poisoned = 0;
                for (size_t i = 0; i < ai_pool6.size(); i++) {
                    Entity e = ai_pool6.entity_at(i);
                    if (!world.has<Position>(e) || !world.has<Stats>(e)) continue;
                    auto& mp = world.get<Position>(e);
                    if (map.in_bounds(mp.x, mp.y) && map.at(mp.x, mp.y).visible) {
                        if (!world.has<StatusEffects>(e))
                            world.add<StatusEffects>(e, {});
                        world.get<StatusEffects>(e).add(StatusType::POISON, 2, 10);
                        poisoned++;
                    }
                }
                char buf9[128];
                snprintf(buf9, sizeof(buf9),
                    "A sickly cloud spreads. %d creatures choke.", poisoned);
                log.add(buf9, {120, 180, 60, 255});
                particles.drift(ppos.x, ppos.y, 15, 120, 180, 60, 1.5f, 5);
            } else {
                // Unravel — target loses 50% natural armor
                Entity target = magic::nearest_enemy(world, player, map, 8);
                if (target != NULL_ENTITY) {
                    auto& tgt = world.get<Stats>(target);
                    int lost = tgt.natural_armor / 2;
                    if (lost < 1) lost = 1;
                    tgt.natural_armor = std::max(0, tgt.natural_armor - lost);
                    char buf10[128];
                    snprintf(buf10, sizeof(buf10),
                        "The %s's armor corrodes and flakes away. (-%d armor)", tgt.name.c_str(), lost);
                    log.add(buf10, {120, 180, 60, 255});
                } else {
                    log.add("Nothing to corrode.", {150, 140, 130, 255});
                    align.favor += prayer.favor_cost;
                    acted = false;
                }
            }
            break;

        default: break;
    }

    return acted;
}

void render_god_visuals(World& world, Entity player, SDL_Renderer* renderer,
                        const Camera& cam, int y_offset) {
    if (!world.has<GodAlignment>(player) || !world.has<Position>(player)) return;
    auto& ga = world.get<GodAlignment>(player);
    if (ga.god == GodId::NONE) return;
    auto& pos = world.get<Position>(player);
    auto& ginfo = get_god_info(ga.god);

    int TS = cam.tile_size;
    int px = (pos.x - cam.x) * TS;
    int py = (pos.y - cam.y) * TS + y_offset;
    int cx = px + TS / 2;
    int cy = py + TS / 2;

    Uint32 ticks = SDL_GetTicks();
    float t = ticks / 1000.0f; // time in seconds
    uint8_t r = ginfo.color.r, g = ginfo.color.g, b = ginfo.color.b;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    int ds = std::max(3, TS / 12); // dot/drip size scales with tile


    switch (ga.god) {
    case GodId::VETHRIK: {
        // Rising bone motes (matches creation screen)
        for (int i = 0; i < 4; i++) {
            float phase = std::fmod(t * 0.8f + i * 0.25f, 1.0f);
            int mx = cx + static_cast<int>(std::sin(t * 0.5f + i * 1.7f) * TS * 0.4f);
            int my = py + TS - static_cast<int>(phase * TS * 1.2f);
            int ma = static_cast<int>((1.0f - phase) * 180);
            SDL_SetRenderDrawColor(renderer, 200, 200, 220, static_cast<Uint8>(ma));
            SDL_Rect mote = {mx - ds, my - ds, ds * 2, ds * 2};
            SDL_RenderFillRect(renderer, &mote);
        }
        break;
    }
    case GodId::THESSARKA: {
        for (int i = 0; i < 4; i++) {
            float angle = t * 1.8f + i * 1.5708f;
            int ox = cx + static_cast<int>(std::cos(angle) * TS * 0.6f);
            int oy = cy + static_cast<int>(std::sin(angle) * TS * 0.4f);
            SDL_SetRenderDrawColor(renderer, r, g, b, 170);
            SDL_Rect dot = {ox - ds * 2, oy - ds * 2, ds * 4, ds * 4};
            SDL_RenderFillRect(renderer, &dot);
        }
        break;
    }
    case GodId::MORRETH: {
        for (int i = 0; i < 3; i++) {
            float phase = std::fmod(t * 2.0f + i * 0.33f, 1.0f);
            if (phase < 0.3f) {
                int spx = cx + static_cast<int>((phase - 0.15f) * TS * 3.0f * (i % 2 ? 1 : -1));
                int spy = py + TS - static_cast<int>(phase * TS * 0.5f);
                int sa = static_cast<int>((0.3f - phase) * 600);
                SDL_SetRenderDrawColor(renderer, 220, 180, 120, static_cast<Uint8>(std::min(sa, 200)));
                SDL_Rect spark = {spx - ds, spy - ds, ds * 2, ds * 2};
                SDL_RenderFillRect(renderer, &spark);
            }
        }
        break;
    }
    case GodId::YASHKHET: {
        for (int i = 0; i < 5; i++) {
            float phase = std::fmod(t * 0.6f + i * 0.2f, 1.0f);
            int dx2 = cx + static_cast<int>(std::sin(i * 2.3f) * TS * 0.4f);
            int dy2 = py + static_cast<int>(phase * TS * 1.4f);
            int da = static_cast<int>((1.0f - phase) * 180);
            SDL_SetRenderDrawColor(renderer, 200, 40, 40, static_cast<Uint8>(da));
            SDL_Rect drip = {dx2 - ds / 2, dy2, ds, ds * 3};
            SDL_RenderFillRect(renderer, &drip);
        }
        break;
    }
    case GodId::KHAEL: {
        for (int i = 0; i < 5; i++) {
            float phase = std::fmod(t * 0.4f + i * 0.2f, 1.0f);
            float angle = i * 1.257f + t * 0.3f;
            int lx = cx + static_cast<int>(std::cos(angle) * phase * TS * 0.8f);
            int ly = cy + static_cast<int>(std::sin(angle) * phase * TS * 0.5f);
            int la = static_cast<int>((1.0f - phase) * 150);
            SDL_SetRenderDrawColor(renderer, 80, 180, 60, static_cast<Uint8>(la));
            SDL_Rect leaf = {lx - ds, ly - ds / 2, ds * 2, ds};
            SDL_RenderFillRect(renderer, &leaf);
        }
        break;
    }
    case GodId::SOLETH: {
        int fl = static_cast<int>(45 + 25 * std::sin(t * 6.0f));
        SDL_SetRenderDrawColor(renderer, 255, 220, 100, static_cast<Uint8>(fl));
        SDL_Rect halo = {cx - TS / 2, py - TS / 5, TS, TS / 5};
        SDL_RenderFillRect(renderer, &halo);
        for (int i = 0; i < 4; i++) {
            float phase = std::fmod(t * 1.0f + i * 0.25f, 1.0f);
            int ex = cx + static_cast<int>(std::sin(t * 0.7f + i * 2.1f) * TS * 0.4f);
            int ey = py + TS - static_cast<int>(phase * TS * 1.3f);
            int ea = static_cast<int>((1.0f - phase) * 170);
            SDL_SetRenderDrawColor(renderer, 255, 180, 60, static_cast<Uint8>(ea));
            SDL_Rect ember = {ex - ds, ey - ds, ds * 2, ds * 2};
            SDL_RenderFillRect(renderer, &ember);
        }
        break;
    }
    case GodId::IXUUL: {
        if ((ticks / 50) % 4 == 0) {
            int off = (ticks / 25) % 9 - 4;
            SDL_SetRenderDrawColor(renderer, r, g, b, 90);
            SDL_Rect tear = {px + off * 3, py + TS / 3, TS + 4, ds * 2};
            SDL_RenderFillRect(renderer, &tear);
            SDL_Rect tear2 = {px - off * 4, py + TS * 2 / 3, TS + 6, ds + 1};
            SDL_RenderFillRect(renderer, &tear2);
            SDL_Rect tear3 = {px + off * 2, py + TS / 6, TS / 2, ds};
            SDL_RenderFillRect(renderer, &tear3);
        }
        break;
    }
    case GodId::ZHAVEK: {
        for (int i = 1; i <= 3; i++) {
            int off = i * TS / 8;
            int alpha = 45 - i * 12;
            SDL_SetRenderDrawColor(renderer, 15, 15, 30, static_cast<Uint8>(alpha));
            SDL_Rect trail = {px + off, py + off, TS - off / 2, TS - off / 2};
            SDL_RenderFillRect(renderer, &trail);
        }
        break;
    }
    case GodId::THALARA: {
        for (int i = 0; i < 3; i++) {
            float rp = std::fmod(t * 1.2f + i * 0.33f, 1.0f);
            int rs = static_cast<int>(rp * TS * 1.2f);
            int ra = static_cast<int>((1.0f - rp) * 60);
            SDL_SetRenderDrawColor(renderer, 80, 180, 200, static_cast<Uint8>(ra));
            SDL_Rect ring = {cx - rs / 2, py + TS - rs / 3, rs, rs * 2 / 3};
            SDL_RenderDrawRect(renderer, &ring);
            SDL_Rect ring2 = {cx - rs / 2 + 1, py + TS - rs / 3 + 1, rs - 2, rs * 2 / 3 - 2};
            SDL_RenderDrawRect(renderer, &ring2);
        }
        break;
    }
    case GodId::OSSREN: {
        for (int i = 0; i < 3; i++) {
            float phase = std::fmod(t * 1.5f + i * 0.33f, 1.0f);
            if (phase < 0.4f) {
                int spx = cx + static_cast<int>(std::sin(i * 3.1f) * TS * 0.4f);
                int spy = py + TS - static_cast<int>(phase * TS * 0.7f);
                int sa = static_cast<int>((0.4f - phase) * 500);
                SDL_SetRenderDrawColor(renderer, 255, 180, 60, static_cast<Uint8>(std::min(sa, 200)));
                SDL_Rect spark = {spx - ds, spy - ds, ds * 2, ds * 2};
                SDL_RenderFillRect(renderer, &spark);
            }
        }
        break;
    }
    case GodId::LETHIS: {
        for (int ei = 0; ei < 2; ei++) {
            float ep = std::fmod(t * 0.6f + ei * 1.5f, 3.0f);
            if (ep < 0.6f) {
                int ea = static_cast<int>((0.6f - std::abs(ep - 0.3f)) * 500);
                SDL_SetRenderDrawColor(renderer, 200, 160, 240, static_cast<Uint8>(std::min(ea, 220)));
                float eangle = t * 0.3f + ei * 3.14f;
                int ex2 = cx + static_cast<int>(std::cos(eangle) * TS * 0.5f);
                int ey2 = cy - TS / 6 + static_cast<int>(std::sin(eangle * 0.7f) * TS * 0.1f);
                SDL_Rect eye = {ex2 - ds * 2, ey2 - ds, ds * 4, ds * 2};
                SDL_RenderFillRect(renderer, &eye);
            }
        }
        break;
    }
    case GodId::GATHRUUN: {
        for (int i = 0; i < 4; i++) {
            float angle = t * 1.2f + i * 1.5708f;
            int ox = cx + static_cast<int>(std::cos(angle) * TS * 0.55f);
            int oy = py + TS - ds * 2 + static_cast<int>(std::sin(angle) * TS * 0.08f);
            SDL_SetRenderDrawColor(renderer, 160, 130, 90, 190);
            SDL_Rect peb = {ox - ds * 2, oy - ds, ds * 3, ds * 2};
            SDL_RenderFillRect(renderer, &peb);
        }
        break;
    }
    case GodId::SYTHARA: {
        for (int i = 0; i < 6; i++) {
            float phase = std::fmod(t * 0.5f + i * 0.167f, 1.0f);
            float angle = i * 1.047f + t * 0.2f;
            int spx = cx + static_cast<int>(std::cos(angle) * phase * TS * 0.7f);
            int spy = cy + static_cast<int>(std::sin(angle) * phase * TS * 0.5f);
            int sa = static_cast<int>((1.0f - phase) * 150);
            SDL_SetRenderDrawColor(renderer, 100, 160, 40, static_cast<Uint8>(sa));
            SDL_Rect spore = {spx - ds, spy - ds, ds * 2, ds * 2};
            SDL_RenderFillRect(renderer, &spore);
        }
        break;
    }
    default: break;
    }
}

} // namespace god_system
