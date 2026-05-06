#include <algorithm>
#include "systems/ai.h"
#include "components/ai.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/stats.h"
#include "components/energy.h"
#include "components/player.h"
#include "components/god.h"
#include "components/prayer.h"
#include "components/corpse.h"
#include "components/status_effect.h"
#include "components/skills.h"
#include "systems/combat.h"
#include <cmath>
#include <cstdlib>
#include <cstdio>

namespace ai {

// Check if entity at (x,y) can see target at (tx,ty) — simple LOS check
static bool has_los(const TileMap& map, int x, int y, int tx, int ty) {
    // Bresenham-style line of sight
    int dx = std::abs(tx - x);
    int dy = std::abs(ty - y);
    int sx = x < tx ? 1 : -1;
    int sy = y < ty ? 1 : -1;
    int err = dx - dy;

    int cx = x, cy = y;
    while (cx != tx || cy != ty) {
        int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; cx += sx; }
        if (e2 < dx)  { err += dx; cy += sy; }

        // Don't check the final tile (target)
        if (cx == tx && cy == ty) break;

        if (map.is_opaque(cx, cy)) return false;
    }
    return true;
}

// Distance (Chebyshev — diagonals count as 1)
static int distance(int x1, int y1, int x2, int y2) {
    return std::max(std::abs(x2 - x1), std::abs(y2 - y1));
}

// Check if a tile is occupied by a blocking entity
static bool tile_blocked_by_entity(World& world, int x, int y, Entity self) {
    auto& positions = world.pool<Position>();
    for (size_t i = 0; i < positions.size(); i++) {
        Entity e = positions.entity_at(i);
        if (e == self) continue;
        auto& p = positions.at_index(i);
        if (p.x == x && p.y == y) {
            // Blocked if it has stats (it's a creature)
            if (world.has<Stats>(e)) return true;
        }
    }
    return false;
}

static void update_facing(World& world, Entity e, int old_x, int new_x) {
    if (world.has<Renderable>(e) && old_x != new_x) {
        world.get<Renderable>(e).flip_h = (new_x > old_x);
    }
}

// Move toward a target position (cardinal only)
static void move_toward(World& world, TileMap& map, Entity e,
                         int tx, int ty, [[maybe_unused]] RNG& rng) {
    auto& pos = world.get<Position>(e);
    int dx = 0, dy = 0;

    // Pick the axis with the larger distance, or random if equal
    int adx = std::abs(tx - pos.x);
    int ady = std::abs(ty - pos.y);
    if (adx > ady || (adx == ady && rng.chance(50))) {
        dx = (tx > pos.x) ? 1 : (tx < pos.x) ? -1 : 0;
    } else {
        dy = (ty > pos.y) ? 1 : (ty < pos.y) ? -1 : 0;
    }

    int old_x = pos.x;
    int nx = pos.x + dx;
    int ny = pos.y + dy;

    if (map.is_walkable(nx, ny) && !tile_blocked_by_entity(world, nx, ny, e)) {
        pos.x = nx;
        pos.y = ny;
        update_facing(world, e, old_x, pos.x);
        return;
    }

    // Blocked on primary axis, try the other
    if (dx != 0) {
        dy = (ty > pos.y) ? 1 : (ty < pos.y) ? -1 : 0;
        if (dy != 0 && map.is_walkable(pos.x, pos.y + dy) &&
            !tile_blocked_by_entity(world, pos.x, pos.y + dy, e)) {
            pos.y += dy;
            return;
        }
    } else if (dy != 0) {
        dx = (tx > pos.x) ? 1 : (tx < pos.x) ? -1 : 0;
        if (dx != 0 && map.is_walkable(pos.x + dx, pos.y) &&
            !tile_blocked_by_entity(world, pos.x + dx, pos.y, e)) {
            pos.x += dx;
            update_facing(world, e, old_x, pos.x);
            return;
        }
    }
}

// Wander randomly (cardinal only)
static void wander(World& world, TileMap& map, Entity e, RNG& rng) {
    auto& pos = world.get<Position>(e);
    // 50% chance to just stand still
    if (rng.chance(50)) return;

    int dx = 0, dy = 0;
    switch (rng.range(0, 3)) {
        case 0: dx = -1; break;
        case 1: dx =  1; break;
        case 2: dy = -1; break;
        case 3: dy =  1; break;
    }
    if (dx == 0 && dy == 0) return;

    int old_x = pos.x;
    int nx = pos.x + dx;
    int ny = pos.y + dy;
    if (map.is_walkable(nx, ny) && !tile_blocked_by_entity(world, nx, ny, e)) {
        pos.x = nx;
        pos.y = ny;
        update_facing(world, e, old_x, pos.x);
    }
}

// Flee from a position
static void flee_from(World& world, TileMap& map, Entity e,
                       int fx, int fy, RNG& rng) {
    auto& pos = world.get<Position>(e);
    int dx = 0, dy = 0;
    // Move in opposite direction
    if (fx > pos.x) dx = -1;
    else if (fx < pos.x) dx = 1;
    if (fy > pos.y) dy = -1;
    else if (fy < pos.y) dy = 1;

    int old_x = pos.x;
    int nx = pos.x + dx;
    int ny = pos.y + dy;
    if (map.is_walkable(nx, ny) && !tile_blocked_by_entity(world, nx, ny, e)) {
        pos.x = nx;
        pos.y = ny;
        update_facing(world, e, old_x, pos.x);
    } else {
        wander(world, map, e, rng);
    }
}

void process(World& world, TileMap& map, Entity player, RNG& rng,
             MessageLog& log, bool player_sneaking) {
    if (!world.has<Position>(player)) return;
    auto& player_pos = world.get<Position>(player);

    auto& ai_pool = world.pool<AI>();
    for (size_t i = 0; i < ai_pool.size(); i++) {
        Entity e = ai_pool.entity_at(i);
        auto& ai_comp = ai_pool.at_index(i);

        if (!world.has<Position>(e) || !world.has<Energy>(e)) continue;
        auto& energy = world.get<Energy>(e);

        if (!energy.can_act()) continue;
        energy.spend();

        // Lethis: forget_player — permanently ignores player
        if (ai_comp.forget_player) {
            wander(world, map, e, rng);
            continue;
        }

        // Sleep: skip turn if sleeping. Wake if non-sneaking player is adjacent.
        if (world.has<Stats>(e) && world.get<Stats>(e).sleep_turns > 0) {
            auto& mpos = world.get<Position>(e);
            int wake_dist = distance(mpos.x, mpos.y, player_pos.x, player_pos.y);
            bool should_wake = (wake_dist <= 1 && !player_sneaking);
            // Stealth 25+: player can be adjacent without waking
            if (wake_dist <= 1 && player_sneaking) {
                int stealth_lv = 0;
                if (world.has<Skills>(player))
                    stealth_lv = world.get<Skills>(player).get_level(SkillId::STEALTH);
                if (stealth_lv < 25) should_wake = true; // not skilled enough
            }
            if (should_wake) {
                world.get<Stats>(e).sleep_turns = 0;
                // don't skip, let them act this turn
            } else {
                continue;
            }
        }

        auto& pos = world.get<Position>(e);
        int dist = distance(pos.x, pos.y, player_pos.x, player_pos.y);

        // Friendly summons: hunt nearest hostile AI instead of player
        if (ai_comp.friendly) {
            // Find nearest hostile entity
            Entity nearest_hostile = 0;
            int nearest_dist = 999;
            auto& all_ai = world.pool<AI>();
            for (size_t j = 0; j < all_ai.size(); j++) {
                Entity he = all_ai.entity_at(j);
                if (he == e || all_ai.at_index(j).friendly) continue;
                if (!world.has<Position>(he)) continue;
                auto& hp = world.get<Position>(he);
                int hd = distance(pos.x, pos.y, hp.x, hp.y);
                if (hd < nearest_dist) {
                    nearest_dist = hd;
                    nearest_hostile = he;
                }
            }
            if (nearest_hostile != 0 && world.has<Position>(nearest_hostile)) {
                auto& hp = world.get<Position>(nearest_hostile);
                if (nearest_dist <= 1) {
                    // Adjacent: simple melee damage
                    if (world.has<Stats>(nearest_hostile) && world.has<Stats>(e)) {
                        auto& ts = world.get<Stats>(nearest_hostile);
                        auto& ms = world.get<Stats>(e);
                        int dmg = std::max(1, ms.base_damage - ts.natural_armor);
                        ts.hp -= dmg;
                        char sbuf[128];
                        snprintf(sbuf, sizeof(sbuf), "Your %s strikes the %s. (%d)",
                                 ms.name.c_str(), ts.name.c_str(), dmg);
                        log.add(sbuf, {160, 160, 140, 255});
                        if (ts.hp <= 0) {
                            combat::kill(world, nearest_hostile, log);
                        }
                    }
                } else {
                    move_toward(world, map, e, hp.x, hp.y, rng);
                }
            } else {
                // No hostiles: follow player
                if (dist > 3) move_toward(world, map, e, player_pos.x, player_pos.y, rng);
                else wander(world, map, e, rng);
            }
            continue;
        }

        // Check if we can see the player
        // Zhavek invisible: enemies can't see you
        bool player_invisible = false;
        if (world.has<Stats>(player) && world.get<Stats>(player).invisible_turns > 0)
            player_invisible = true;
        // Sneaking reduces detection range (base 8 -> 3, modified by Stealth skill)
        int detect_range = 8;
        if (player_sneaking) {
            detect_range = 3;
            // Stealth skill further reduces detection
            if (world.has<Skills>(player)) {
                int stealth_lv = world.get<Skills>(player).get_level(SkillId::STEALTH);
                if (stealth_lv >= 50) detect_range = 1;
                else if (stealth_lv >= 25) detect_range = 2;
            }
        }
        bool can_see = dist <= detect_range && !player_invisible &&
                       has_los(map, pos.x, pos.y, player_pos.x, player_pos.y);

        // Zhavek passive: enemies lose track after 3 turns out of LOS
        if (world.has<GodAlignment>(player)) {
            auto& ga = world.get<GodAlignment>(player);
            if (ga.god == GodId::ZHAVEK && !can_see && ai_comp.state == AIState::HUNTING
                && ai_comp.alert_turns > 3) {
                ai_comp.state = AIState::IDLE;
                ai_comp.alert_turns = 0;
            }
        }

        // God passives: creature behavior overrides (re-evaluated each turn)
        if (world.has<GodAlignment>(player) && world.has<Stats>(e)) {
            auto& ga = world.get<GodAlignment>(player);
            const char* ename = world.get<Stats>(e).name.c_str();
            // Khael: ALL animals are friendly (fight for you) - only while Khael is active
            if (is_animal(ename)) {
                bool should_be_friendly = (ga.god == GodId::KHAEL);
                if (ai_comp.friendly != should_be_friendly) ai_comp.friendly = should_be_friendly;
                if (should_be_friendly) continue; // skip hostile AI
            }
            // Vethrik: undead completely ignore you
            if (ga.god == GodId::VETHRIK && is_undead(ename)) {
                can_see = false;
                if (ai_comp.state == AIState::HUNTING) {
                    ai_comp.state = AIState::IDLE;
                    ai_comp.alert_turns = 0;
                }
            }
            // Ixuul: slimes/aberrations neutral
            if (ga.god == GodId::IXUUL && is_slime(ename)) {
                can_see = false;
            }
        }

        // Check flee condition
        if (world.has<Stats>(e)) {
            auto& stats = world.get<Stats>(e);
            int hp_pct = (stats.hp * 100) / std::max(1, stats.hp_max);
            if (hp_pct <= ai_comp.flee_threshold && can_see) {
                ai_comp.state = AIState::FLEEING;
            }
        }

        // State transitions
        if (can_see && ai_comp.state != AIState::FLEEING) {
            ai_comp.state = AIState::HUNTING;
            ai_comp.last_seen_x = player_pos.x;
            ai_comp.last_seen_y = player_pos.y;
            ai_comp.alert_turns = 0;
        } else if (ai_comp.state == AIState::HUNTING) {
            ai_comp.alert_turns++;
            if (ai_comp.alert_turns > 10) {
                ai_comp.state = AIState::IDLE;
            }
        }

        // Tick ability cooldowns
        if (ai_comp.ability_cooldown > 0) ai_comp.ability_cooldown--;

        // Troll: regenerate every turn (blocked by fire/burn or acid)
        if (ai_comp.behavior == BehaviorType::TROLL && ai_comp.regen_per_turn > 0
            && world.has<Stats>(e)) {
            bool regen_blocked = false;
            if (world.has<StatusEffects>(e)) {
                for (auto& fx : world.get<StatusEffects>(e).effects) {
                    if (fx.type == StatusType::BURN) { regen_blocked = true; break; }
                }
            }
            if (!regen_blocked) {
                auto& mstats = world.get<Stats>(e);
                if (mstats.hp < mstats.hp_max) {
                    mstats.hp = std::min(mstats.hp + ai_comp.regen_per_turn, mstats.hp_max);
                }
            }
        }

        // Act based on state
        switch (ai_comp.state) {
            case AIState::IDLE:
                wander(world, map, e, rng);
                break;

            case AIState::HUNTING: {
                int tx = can_see ? player_pos.x : ai_comp.last_seen_x;
                int ty = can_see ? player_pos.y : ai_comp.last_seen_y;

                // Behavior-specific hunting
                switch (ai_comp.behavior) {
                    case BehaviorType::LICH: {
                        // Teleport away when below 50% HP
                        if (world.has<Stats>(e)) {
                            auto& ls = world.get<Stats>(e);
                            if (ls.hp <= ls.hp_max / 2 && ai_comp.ability_cooldown == 0) {
                                for (int tries = 0; tries < 20; tries++) {
                                    int rx = pos.x + rng.range(-8, 8);
                                    int ry = pos.y + rng.range(-8, 8);
                                    int td = distance(rx, ry, pos.x, pos.y);
                                    if (td >= 5 && td <= 8 && map.is_walkable(rx, ry)
                                        && !tile_blocked_by_entity(world, rx, ry, e)) {
                                        pos.x = rx;
                                        pos.y = ry;
                                        ai_comp.ability_cooldown = 6;
                                        break;
                                    }
                                }
                                break;
                            }
                        }
                        // Cast Drain Life at range (drain HP, heal self)
                        if (can_see && dist <= 6 && dist >= 2 && ai_comp.ability_cooldown == 0
                            && world.has<Stats>(e) && world.has<Stats>(player)) {
                            auto& ls = world.get<Stats>(e);
                            auto& ps = world.get<Stats>(player);
                            int drain = ai_comp.ranged_damage > 0 ? ai_comp.ranged_damage : 8;
                            ps.hp -= drain;
                            ls.hp = std::min(ls.hp + drain / 2, ls.hp_max);
                            ai_comp.ability_cooldown = 3;
                            break;
                        }
                        // Summon skeleton if no ability used and cooldown ready
                        if (can_see && dist <= 8 && ai_comp.ability_cooldown == 0 && rng.chance(25)) {
                            // Find empty adjacent tile to spawn a skeleton
                            static const int DX[] = {-1, 0, 1, -1, 1, -1, 0, 1};
                            static const int DY[] = {-1, -1, -1, 0, 0, 1, 1, 1};
                            for (int d = 0; d < 8; d++) {
                                int sx = pos.x + DX[d], sy = pos.y + DY[d];
                                if (map.is_walkable(sx, sy) && !tile_blocked_by_entity(world, sx, sy, e)) {
                                    Entity sk = world.create();
                                    world.add<Position>(sk, {sx, sy});
                                    world.add<Renderable>(sk, {1, 3, 4, {255, 255, 255, 255}, 5}); // skeleton sprite
                                    Stats ss;
                                    ss.name = "skeleton";
                                    ss.hp = 10; ss.hp_max = 10;
                                    ss.base_damage = 3; ss.xp_value = 15;
                                    world.add<Stats>(sk, std::move(ss));
                                    AI sk_ai; sk_ai.state = AIState::HUNTING;
                                    sk_ai.last_seen_x = player_pos.x; sk_ai.last_seen_y = player_pos.y;
                                    world.add<AI>(sk, sk_ai);
                                    world.add<Energy>(sk, {0, 90});
                                    world.add<StatusEffects>(sk);
                                    ai_comp.ability_cooldown = 10;
                                    break;
                                }
                            }
                            break;
                        }
                        // If nothing else, close distance
                        move_toward(world, map, e, tx, ty, rng);
                        break;
                    }

                    case BehaviorType::CHARGER:
                        // Charge when 2-4 tiles away in a cardinal line
                        if (can_see && ai_comp.ability_cooldown == 0 && dist >= 2 && dist <= 4) {
                            int cdx = player_pos.x - pos.x;
                            int cdy = player_pos.y - pos.y;
                            bool in_line = (cdx == 0 || cdy == 0);
                            if (in_line) {
                                int sdx = (cdx > 0) ? 1 : (cdx < 0) ? -1 : 0;
                                int sdy = (cdy > 0) ? 1 : (cdy < 0) ? -1 : 0;
                                // Move up to (dist-1) tiles toward player
                                for (int step = 0; step < dist - 1; step++) {
                                    int nx = pos.x + sdx;
                                    int ny = pos.y + sdy;
                                    if (map.is_walkable(nx, ny) && !tile_blocked_by_entity(world, nx, ny, e)) {
                                        pos.x = nx;
                                        pos.y = ny;
                                    } else break;
                                }
                                // Stun player if adjacent after charge
                                if (distance(pos.x, pos.y, player_pos.x, player_pos.y) <= 1
                                    && world.has<StatusEffects>(player)) {
                                    world.get<StatusEffects>(player).add(StatusType::STUNNED, 0, 1);
                                }
                                ai_comp.ability_cooldown = 6;
                                break;
                            }
                        }
                        move_toward(world, map, e, tx, ty, rng);
                        break;

                    case BehaviorType::DRAGON:
                        // Breath attack: fire cone when in range 2-4
                        if (can_see && dist >= 2 && dist <= 4 && ai_comp.ability_cooldown == 0
                            && world.has<Stats>(e)) {
                            auto& ds = world.get<Stats>(e);
                            int breath_dmg = ds.base_damage * 2 / 3;
                            // Damage player
                            if (world.has<Stats>(player)) {
                                world.get<Stats>(player).hp -= breath_dmg;
                            }
                            // Apply burn
                            if (world.has<StatusEffects>(player)) {
                                world.get<StatusEffects>(player).add(StatusType::BURN, 3, 4);
                            }
                            ai_comp.ability_cooldown = 3;
                            break;
                        }
                        // Dragons don't flee. They close and fight.
                        move_toward(world, map, e, tx, ty, rng);
                        break;

                    case BehaviorType::PACK: {
                        // Prefer flanking: move to tile opposite another pack member
                        bool flanked = false;
                        if (can_see && dist <= 2) {
                            // Find another pack member near the player
                            auto& all_ai = world.pool<AI>();
                            for (size_t j = 0; j < all_ai.size(); j++) {
                                Entity ally = all_ai.entity_at(j);
                                if (ally == e) continue;
                                if (all_ai.at_index(j).behavior != BehaviorType::PACK) continue;
                                if (!world.has<Position>(ally)) continue;
                                auto& apos = world.get<Position>(ally);
                                if (distance(apos.x, apos.y, player_pos.x, player_pos.y) <= 1) {
                                    // Try to move to opposite side of player
                                    int fx = player_pos.x - (apos.x - player_pos.x);
                                    int fy = player_pos.y - (apos.y - player_pos.y);
                                    if (map.is_walkable(fx, fy) && !tile_blocked_by_entity(world, fx, fy, e)) {
                                        int old_x = pos.x;
                                        move_toward(world, map, e, fx, fy, rng);
                                        flanked = true;
                                        break;
                                    }
                                }
                            }
                        }
                        if (!flanked) move_toward(world, map, e, tx, ty, rng);
                        break;
                    }

                    case BehaviorType::WRAITH:
                        // Phase through walls: move toward player ignoring walkability
                        if (can_see) {
                            int dx = 0, dy = 0;
                            if (player_pos.x > pos.x) dx = 1;
                            else if (player_pos.x < pos.x) dx = -1;
                            if (player_pos.y > pos.y) dy = 1;
                            else if (player_pos.y < pos.y) dy = -1;
                            int nx = pos.x + dx, ny = pos.y + dy;
                            // Can move through walls but not through other entities
                            if (!tile_blocked_by_entity(world, nx, ny, e)) {
                                int old_x = pos.x;
                                pos.x = nx; pos.y = ny;
                                update_facing(world, e, old_x, pos.x);
                            }
                        } else {
                            wander(world, map, e, rng);
                        }
                        break;

                    case BehaviorType::KEEPER: {
                        // Multi-phase final boss
                        auto& ks = world.get<Stats>(e);
                        int hp_pct = (ks.hp * 100) / std::max(1, ks.hp_max);
                        int tx = player_pos.x, ty = player_pos.y;

                        if (hp_pct > 50) {
                            // PHASE 1: Armored Guardian - charge behavior
                            if (can_see && dist >= 2 && dist <= 5 && ai_comp.ability_cooldown == 0) {
                                // Check cardinal line
                                bool line = (pos.x == tx || pos.y == ty);
                                if (line) {
                                    int dx = 0, dy = 0;
                                    if (tx > pos.x) dx = 1; else if (tx < pos.x) dx = -1;
                                    if (ty > pos.y) dy = 1; else if (ty < pos.y) dy = -1;
                                    for (int step = 0; step < dist - 1; step++) {
                                        int nx = pos.x + dx * (step + 1);
                                        int ny = pos.y + dy * (step + 1);
                                        if (map.is_walkable(nx, ny)) { pos.x = nx; pos.y = ny; }
                                        else break;
                                    }
                                    ai_comp.ability_cooldown = 5;
                                    // Shockwave stun if adjacent after charge
                                    if (distance(pos.x, pos.y, tx, ty) <= 1) {
                                        if (world.has<StatusEffects>(player))
                                            world.get<StatusEffects>(player).add(StatusType::STUNNED, 0, 1);
                                        log.add("The Keeper's charge shakes the ground.", {255, 200, 80, 255});
                                    }
                                    break;
                                }
                            }
                            move_toward(world, map, e, tx, ty, rng);
                        } else if (hp_pct > 25) {
                            // PHASE 2: The Unbound - teleport + ranged drain
                            if (dist <= 2 && ai_comp.ability_cooldown == 0) {
                                // Teleport away (5-7 tiles)
                                for (int a = 0; a < 30; a++) {
                                    int nx = pos.x + rng.range(-7, 7);
                                    int ny = pos.y + rng.range(-7, 7);
                                    int nd = distance(nx, ny, tx, ty);
                                    if (nd >= 4 && nd <= 8 && map.in_bounds(nx, ny) && map.is_walkable(nx, ny)) {
                                        pos.x = nx; pos.y = ny;
                                        log.add("The Keeper vanishes and reappears.", {200, 180, 255, 255});
                                        ai_comp.ability_cooldown = 4;
                                        break;
                                    }
                                }
                            } else if (can_see && dist >= 2 && dist <= ai_comp.ranged_range && ai_comp.ability_cooldown == 0) {
                                // Drain Life at range
                                if (world.has<Stats>(player)) {
                                    int drain = ai_comp.ranged_damage;
                                    world.get<Stats>(player).hp -= drain;
                                    ks.hp = std::min(ks.hp_max, ks.hp + drain / 2);
                                    char db[64]; snprintf(db, sizeof(db), "The Keeper drains your life. (%d)", drain);
                                    log.add(db, {200, 100, 255, 255});
                                    ai_comp.ability_cooldown = 3;
                                }
                            } else if (dist < 3) {
                                // Back away
                                int dx = pos.x - tx, dy = pos.y - ty;
                                int nx = pos.x + (dx > 0 ? 1 : dx < 0 ? -1 : 0);
                                int ny = pos.y + (dy > 0 ? 1 : dy < 0 ? -1 : 0);
                                if (map.in_bounds(nx, ny) && map.is_walkable(nx, ny)) { pos.x = nx; pos.y = ny; }
                            } else {
                                move_toward(world, map, e, tx, ty, rng);
                            }
                        } else {
                            // PHASE 3: The Vessel - dragon breath + close
                            if (can_see && dist >= 2 && dist <= 4 && ai_comp.ability_cooldown == 0) {
                                // Fire breath cone
                                int breath_dmg = 15;
                                if (world.has<Stats>(player)) {
                                    world.get<Stats>(player).hp -= breath_dmg;
                                    if (world.has<StatusEffects>(player))
                                        world.get<StatusEffects>(player).add(StatusType::BURN, 3, 3);
                                    char bb[64]; snprintf(bb, sizeof(bb), "The Keeper unleashes divine fire. (%d)", breath_dmg);
                                    log.add(bb, {255, 200, 100, 255});
                                }
                                ai_comp.ability_cooldown = 3;
                            } else {
                                // Close in aggressively
                                move_toward(world, map, e, tx, ty, rng);
                            }
                        }
                        break;
                    }

                    case BehaviorType::NECROMANCER: {
                        // Stay at range 3-5, cast drain at range, raise nearby corpses
                        // Raise corpse if one is nearby and cooldown ready
                        if (ai_comp.ability_cooldown == 0 && rng.chance(30)) {
                            auto& corpse_pool = world.pool<Corpse>();
                            for (size_t ci = 0; ci < corpse_pool.size(); ci++) {
                                Entity ce = corpse_pool.entity_at(ci);
                                if (!world.has<Position>(ce)) continue;
                                auto& cpos = world.get<Position>(ce);
                                if (distance(cpos.x, cpos.y, pos.x, pos.y) <= 3) {
                                    // Raise as zombie
                                    Entity zom = world.create();
                                    world.add<Position>(zom, {cpos.x, cpos.y});
                                    world.add<Renderable>(zom, {1, 5, 3, {180, 200, 160, 255}, 5});
                                    Stats zs;
                                    zs.name = "risen dead";
                                    zs.hp = 12; zs.hp_max = 12;
                                    zs.base_damage = 4; zs.xp_value = 10;
                                    world.add<Stats>(zom, std::move(zs));
                                    AI zai; zai.state = AIState::HUNTING;
                                    zai.last_seen_x = player_pos.x; zai.last_seen_y = player_pos.y;
                                    world.add<AI>(zom, zai);
                                    world.add<Energy>(zom, {0, 80});
                                    world.add<StatusEffects>(zom);
                                    // Remove the corpse
                                    world.destroy(ce);
                                    ai_comp.ability_cooldown = 8;
                                    break;
                                }
                            }
                        }
                        // Stay at range if close, back up
                        if (can_see && dist <= 2) {
                            flee_from(world, map, e, player_pos.x, player_pos.y, rng);
                        } else if (can_see && dist >= 3 && dist <= 6) {
                            // In sweet spot, cast drain if possible
                            if (ai_comp.ability_cooldown == 0 && world.has<Stats>(player) && world.has<Stats>(e)) {
                                auto& ns = world.get<Stats>(e);
                                int drain = ns.base_damage;
                                world.get<Stats>(player).hp -= drain;
                                ns.hp = std::min(ns.hp + drain / 2, ns.hp_max);
                                ai_comp.ability_cooldown = 3;
                            }
                        } else {
                            move_toward(world, map, e, tx, ty, rng);
                        }
                        break;
                    }

                    case BehaviorType::SHAMAN: {
                        // Heal wounded allies or buff damage, stay behind frontline
                        bool acted = false;
                        if (ai_comp.ability_cooldown == 0) {
                            auto& stat_pool = world.pool<Stats>();
                            for (size_t si = 0; si < stat_pool.size(); si++) {
                                Entity ally = stat_pool.entity_at(si);
                                if (ally == e || ally == player || !world.has<AI>(ally)) continue;
                                if (!world.has<Position>(ally)) continue;
                                auto& apos = world.get<Position>(ally);
                                auto& as = stat_pool.at_index(si);
                                if (distance(apos.x, apos.y, pos.x, pos.y) > 3) continue;

                                // Heal if wounded
                                if (as.hp < as.hp_max * 3 / 4) {
                                    int heal = 5 + as.hp_max / 10;
                                    as.hp = std::min(as.hp + heal, as.hp_max);
                                    ai_comp.ability_cooldown = 4;
                                    acted = true;
                                    break;
                                }
                                // Buff damage if at full health (+2 base damage, 8 turns)
                                // Simple: just boost damage directly. It won't stack
                                // because cooldown prevents re-buffing.
                                if (as.hp >= as.hp_max * 3 / 4 && rng.chance(30)) {
                                    as.base_damage += 2;
                                    ai_comp.ability_cooldown = 8;
                                    acted = true;
                                    break;
                                }
                            }
                        }
                        // Stay behind other monsters, don't rush in
                        if (!acted) {
                            if (can_see && dist <= 2) {
                                flee_from(world, map, e, player_pos.x, player_pos.y, rng);
                            } else {
                                move_toward(world, map, e, tx, ty, rng);
                            }
                        }
                        break;
                    }

                    case BehaviorType::THIEF:
                        // Rush in, if adjacent player has items, steal one and flee
                        if (dist <= 1 && can_see && ai_comp.ability_cooldown == 0) {
                            // Steal attempt handled in engine (monster hit callback)
                            // For now, just mark that the thief wants to flee after hitting
                            ai_comp.state = AIState::FLEEING;
                            ai_comp.ability_cooldown = 10;
                        }
                        move_toward(world, map, e, tx, ty, rng);
                        break;

                    default:
                        // BASIC, ARCHER: default chase
                        move_toward(world, map, e, tx, ty, rng);
                        break;
                }
                break;
            }

            case AIState::FLEEING:
                flee_from(world, map, e, player_pos.x, player_pos.y, rng);
                if (dist > 10 && !can_see) {
                    ai_comp.state = AIState::IDLE;
                }
                break;
        }
    }
}

} // namespace ai
