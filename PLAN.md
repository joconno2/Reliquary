# Reliquary — Project Plan

## Current Status

**Phase 2: Combat & Creatures — IN PROGRESS**

### Phase 1: Core Engine — COMPLETE
- [x] CMake build system (SDL2, SDL2_image, SDL2_mixer, SDL2_ttf, nlohmann_json)
- [x] SDL2 window (1280x800), input handling, vsync game loop
- [x] Spritesheet loading (all 8 32rogues sheets)
- [x] ECS framework (World, ComponentPool, typed dense storage)
- [x] Tile map with tile types, walkability, opacity
- [x] BSP dungeon generation (rooms + corridors + doors)
- [x] Player entity with grid movement (arrows, vi keys, numpad, diagonals)
- [x] FOV (recursive shadowcasting, 8 octants)
- [x] Camera system (centered on player)
- [x] Message log (scrollable, color-coded, atmospheric)
- [x] Door interaction, stairs interaction, level generation

### Phase 2: Combat & Creatures — IN PROGRESS
- [x] Stats component (7 attributes, HP/MP, derived combat stats)
- [x] Energy/speed turn system (variable speed, energy accumulation)
- [x] Monster AI (idle/wander, hunting/chase with LOS, fleeing at low HP)
- [x] Melee combat (attack roll vs dodge, damage - protection, crits via PER)
- [x] Death → corpse conversion (sprite swap, component cleanup)
- [x] Monster spawning per room (monster table with stats, sprites, behaviors)
- [x] 11 monster types (goblin, orc, rat, spider, kobold, skeleton, zombie, troll, warg, orc warchief, goblin archer)
- [x] HUD (HP bar with color states, depth, turn counter)
- [x] Death screen overlay
- [x] Z-ordered entity rendering (corpses under living creatures)
- [ ] Ranged combat (bows, crossbows, throwing)
- [ ] Backstab/stealth attacks
- [ ] More monster variety + deeper scaling
- [ ] XP and leveling

## Architecture

```
src/
├── core/       — engine, ecs, tilemap, spritesheet, rng
├── components/ — position, renderable, player, blocker, stats, ai, energy, corpse
├── systems/    — render, fov, combat, ai
├── generation/ — dungeon, populate
└── ui/         — message_log
```

## Design Doc

Full game design: `~/Documents/Work/Games/Development/Roguelike Project.md`

## Next Up

Remaining Phase 2 items, then Phase 3: Items & Equipment.
