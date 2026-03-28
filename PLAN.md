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
- [x] 15 backgrounds with stat bonuses + unique passives (Gravedigger, Alchemist's Apprentice, Deserter, Noble Exile, Pit Fighter, Hedge Witch, Tomb Robber, Heretic Priest, Shipwreck Survivor, Ratcatcher, Blacksmith's Child, Disgraced Scholar, Plague Doctor, Executioner, Farmer)
- [x] 22 traits (12 positive, 10 negative) with stat modifiers + special flags
- [x] Background selection screen (browse + confirm)
- [x] Trait selection screen (pick 3 positive + 2 negative, toggle selection)
- [x] Full 4-screen creation flow: God → Class → Background → Traits → play
- [x] All bonuses (god + class + background + traits) applied to player stats
- [ ] Favor mechanics (gain/lose from actions)
- [ ] Prayer abilities
- [ ] God tenets
- [ ] NPC god affiliations

### Phase 7: Magic & Spells — IN PROGRESS
- [x] 15 spells across 6 schools (Conjuration, Transmutation, Divination, Healing, Nature, Dark Arts)
- [x] Spellbook component, spell learning
- [x] Spell casting system (MP cost, INT scaling, auto-targeting, area effects)
- [x] Spell screen UI (z key — browse, cast, MP display, descriptions)
- [x] MP bar in HUD
- [x] Class starting spells (Wizard: Spark/Force Bolt/Identify, Ranger: Detect Monsters, others: Minor Heal)
- [x] Implemented: Spark, Force Bolt, Fireball, Drain Life, Fear, Minor Heal, Major Heal, Harden Skin, Reveal Map, Detect Monsters, Entangle
- [ ] Spellbooks as findable items
- [ ] Spell failure rate (armor penalty)

## Next Up

Favor/prayer system, then look/examine mode, then save/load.

## Fonts

- **Press Start 2P** (PrStart.ttf, 8px) — all body text, UI, messages, stats
- **Jacquard 12** (Jacquard12-Regular.ttf, 24px) — title, god names, class names, death screen (mixed case only, not all caps)
