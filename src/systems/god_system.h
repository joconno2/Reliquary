#pragma once
#include <SDL2/SDL.h>
#include "core/ecs.h"

// Forward declarations
class World;
class TileMap;
class RNG;
class MessageLog;
class Audio;
class ParticleSystem;
struct Camera;
struct PlayerActions;

namespace god_system {

// Adjust god favor by amount, log gain/loss message
void adjust_favor(World& world, Entity player, MessageLog& log, int amount);

// Check tenet violations for the current turn
void check_tenets(World& world, Entity player, const PlayerActions& actions,
                  int game_turn, MessageLog& log);

// Execute a prayer (index 0 or 1) for the player's god.
// Returns true if the prayer consumed the player's turn.
bool execute_prayer(World& world, Entity player, TileMap& map, RNG& rng,
                    MessageLog& log, Audio& audio, ParticleSystem& particles,
                    Camera& camera, int prayer_idx);

// Render per-god aura particles around the player
void render_god_visuals(World& world, Entity player, SDL_Renderer* renderer,
                        const Camera& cam, int y_offset);

} // namespace god_system
