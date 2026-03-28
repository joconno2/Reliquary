# Reliquary — Project Plan

## Current Status

**Phase 4/5: Character System & Gods — IN PROGRESS**

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

### Phase 4/5: Character System & Gods — IN PROGRESS
- [x] God definitions (7 gods with names, titles, domains, descriptions, stat bonuses)
- [x] GodAlignment component (god ID + favor tracking)
- [x] Class definitions (4 starting classes with stats, sprites, descriptions)
- [x] Character creation screen — God select → Class select, with stat preview
- [x] God + class stats combine at player creation
- [x] Player sprite matches chosen class
- [x] God name in HUD
- [x] 6 dungeon zone themes (Warrens, Stonekeep, Deep Halls, Catacombs, Molten Depths, Sunken Halls)
- [x] Atmospheric zone entry messages
- [x] Depth-scaled room count
- [ ] Backgrounds (15+ with starting gear + passives)
- [ ] Traits (positive/negative selection)
- [ ] Favor mechanics (gain/lose from actions)
- [ ] Prayer abilities
- [ ] God tenets
- [ ] NPC god affiliations

## Next Up

Backgrounds + traits, then favor/prayer mechanics.
