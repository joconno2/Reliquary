# Reliquary — Project Plan

## Current Status

**Phase 1: Core Engine — IN PROGRESS**

### Completed
- [x] CMake build system with SDL2/SDL2_image/SDL2_mixer/SDL2_ttf/nlohmann_json
- [x] SDL2 window (1280x800), input handling, game loop
- [x] Spritesheet loading (all 8 32rogues sheets)
- [x] ECS framework (World, ComponentPool, Entity)
- [x] Tile map with tile types (walls, floors, doors, stairs)
- [x] BSP dungeon generation (rooms + corridors + doors)
- [x] Player entity with knight sprite, grid movement (arrows, vi keys, numpad)
- [x] Diagonal movement
- [x] FOV (recursive shadowcasting, 8 octants)
- [x] Camera system (centered on player)
- [x] Message log (scrollable, color-coded, atmospheric)
- [x] Door interaction (bump to open)
- [x] Stairs interaction (> to descend, generates new level)
- [x] Turn counter HUD
- [x] Wait action (./numpad 5)
- [x] F12 screenshot key

### Remaining Phase 1
- [ ] Tile variant randomization visible (floor variation within rooms)
- [ ] Explored-but-not-visible dimming looks correct
- [ ] Multiple dungeon zone themes (swap wall/floor types per zone)
- [ ] Basic ambient messages on movement ("You hear dripping...", etc.)

## Architecture

```
src/
├── core/       — engine.cpp, ecs.h, tilemap.cpp, spritesheet.cpp, rng.cpp
├── components/ — position.h, renderable.h, player.h, blocker.h
├── systems/    — render.cpp, fov.cpp
├── generation/ — dungeon.cpp
└── ui/         — message_log.cpp
```

## Design Doc

Full game design document at: `~/Documents/Work/Games/Development/Roguelike Project.md`

## Next Phase

**Phase 2: Combat & Creatures** — Monster entities, AI, energy/speed turn system, melee/ranged combat, stats.
