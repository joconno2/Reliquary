#include "systems/ai.h"
#include "components/ai.h"
#include "components/position.h"
#include "components/renderable.h"
#include "components/stats.h"
#include "components/energy.h"
#include "components/player.h"
#include "components/god.h"
#include "components/prayer.h"
#include <cmath>
#include <cstdlib>

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

// Move toward a target position
static void move_toward(World& world, TileMap& map, Entity e,
                         int tx, int ty, [[maybe_unused]] RNG& rng) {
    auto& pos = world.get<Position>(e);
    int dx = 0, dy = 0;

    if (tx > pos.x) dx = 1;
    else if (tx < pos.x) dx = -1;
    if (ty > pos.y) dy = 1;
    else if (ty < pos.y) dy = -1;

    int nx = pos.x + dx;
    int ny = pos.y + dy;

    int old_x = pos.x;

    // Try direct path first
    if (map.is_walkable(nx, ny) && !tile_blocked_by_entity(world, nx, ny, e)) {
        pos.x = nx;
        pos.y = ny;
        update_facing(world, e, old_x, pos.x);
        return;
    }

    // Try cardinal alternatives if diagonal blocked
    if (dx != 0 && dy != 0) {
        if (map.is_walkable(pos.x + dx, pos.y) &&
            !tile_blocked_by_entity(world, pos.x + dx, pos.y, e)) {
            pos.x += dx;
            update_facing(world, e, old_x, pos.x);
            return;
        }
        if (map.is_walkable(pos.x, pos.y + dy) &&
            !tile_blocked_by_entity(world, pos.x, pos.y + dy, e)) {
            pos.y += dy;
            return;
        }
    }
}

// Wander randomly
static void wander(World& world, TileMap& map, Entity e, RNG& rng) {
    auto& pos = world.get<Position>(e);
    // 50% chance to just stand still
    if (rng.chance(50)) return;

    int dx = rng.range(-1, 1);
    int dy = rng.range(-1, 1);
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

void process(World& world, TileMap& map, Entity player, RNG& rng) {
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

        // Sleep: skip turn if sleeping
        if (world.has<Stats>(e) && world.get<Stats>(e).sleep_turns > 0) {
            continue; // sleeping — don't act
        }

        auto& pos = world.get<Position>(e);
        int dist = distance(pos.x, pos.y, player_pos.x, player_pos.y);

        // Check if we can see the player
        // Zhavek invisible: enemies can't see you
        bool player_invisible = false;
        if (world.has<Stats>(player) && world.get<Stats>(player).invisible_turns > 0)
            player_invisible = true;
        bool can_see = dist <= 8 && !player_invisible &&
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

        // Khael passive: animals never aggro first
        // Ixuul passive: slimes/aberrations neutral
        if (world.has<GodAlignment>(player) && world.has<Stats>(e)) {
            auto& ga = world.get<GodAlignment>(player);
            const char* ename = world.get<Stats>(e).name.c_str();
            if (ga.god == GodId::KHAEL && ai_comp.state == AIState::IDLE
                && is_animal(ename)) {
                can_see = false;
            }
            if (ga.god == GodId::IXUUL && ai_comp.state == AIState::IDLE
                && is_slime(ename)) {
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

        // Act based on state
        switch (ai_comp.state) {
            case AIState::IDLE:
                wander(world, map, e, rng);
                break;

            case AIState::HUNTING:
                // Move toward player (or last known position)
                if (can_see) {
                    move_toward(world, map, e, player_pos.x, player_pos.y, rng);
                } else {
                    move_toward(world, map, e, ai_comp.last_seen_x,
                                ai_comp.last_seen_y, rng);
                }
                break;

            case AIState::FLEEING:
                flee_from(world, map, e, player_pos.x, player_pos.y, rng);
                // Stop fleeing if far enough or can't see player
                if (dist > 10 && !can_see) {
                    ai_comp.state = AIState::IDLE;
                }
                break;
        }
    }
}

} // namespace ai
