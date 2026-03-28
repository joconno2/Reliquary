# Reliquary — Project Plan

## Current Status

**Phase 3: Items & Inventory — IN PROGRESS**

### Phase 1: Core Engine — COMPLETE
- [x] CMake build, SDL2, spritesheet loading, ECS, tilemap, dungeon gen, FOV, camera, message log

### Phase 2: Combat & Creatures — COMPLETE
- [x] Stats (7 attributes, HP/MP, derived stats), energy/speed turns, monster AI (idle/hunt/flee)
- [x] Melee combat (d20 + attack vs 10 + dodge, crits, damage - armor, equipment bonuses)
- [x] Death → corpse, 18 monster types with depth scaling, HUD, death screen, z-ordered rendering
- [x] XP system with scaling curve, level-up HP/MP gains

### Phase 3: Items & Inventory — IN PROGRESS
- [x] Item component (weapons, armor, shields, potions, food, gold, rings, amulets)
- [x] Inventory component (26 slots, 9 equip slots)
- [x] Item spawning in dungeons (weapons, armor, consumables, gold — depth-scaled)
- [x] Pickup system (g/comma key, auto-collect gold)
- [x] Inventory UI screen (i key — list items, equipment summary, select/scroll, descriptions)
- [x] Equip/unequip (e key in inventory)
- [x] Use consumables (u key — potions heal, food heals)
- [x] Drop items (d key — returns to ground)
- [x] Equipment affects combat (damage bonus, armor bonus, attack bonus, dodge bonus)
- [x] Item identification (equipping/using identifies, unid potions show color names)
- [x] 6 weapon types, 7 armor types, 4 consumable types, gold piles
- [ ] Shops
- [ ] Cursed/blessed items
- [ ] More item variety and special items

## Architecture

```
src/
├── core/       — engine, ecs, tilemap, spritesheet, rng
├── components/ — position, renderable, player, blocker, stats, ai, energy, corpse, item, inventory
├── systems/    — render, fov, combat, ai
├── generation/ — dungeon, populate
└── ui/         — message_log, inventory_screen
```

## Design Doc

Full game design: `~/Documents/Work/Games/Development/Roguelike Project.md`

## Next Up

Remaining Phase 3 (shops, cursed/blessed, more items), then Phase 4: Character System.
