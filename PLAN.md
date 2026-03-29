# Reliquary — Project Plan

> Last updated: 2026-03-28

## Current Status: Playable Alpha

The game is playable from character creation through dungeon crawling. A 17-step main quest chain spans all regions. 20 towns, 30 dungeons, 5 climate zones.

---

## What's Built

### Core Engine
- [x] SDL2/C++20/CMake, ECS, tilemap, spritesheet loading (32rogues + pre-flipped copies)
- [x] BSP dungeon generation with 6 themed zones (3 levels each = 18 dungeon levels)
- [x] FOV (recursive shadowcasting), camera, energy/speed turn system
- [x] Message log (top-down, color-coded), dynamic resolution, fullscreen (F11)
- [x] Bundled fonts: Press Start (body 12px), Jacquard (title 32px, menu 96px)
- [x] Pixel-art panel borders (SNES-style beveled), UI scale system (1.0x-2.0x)
- [x] Tiles rendered at 48px (1.5x native), sprite mirroring on horizontal movement

### Combat & Creatures
- [x] Melee combat (d20 + attack vs 10 + dodge, crits, equipment bonuses, depth scaling)
- [x] 18+ monster types with HP/damage scaling per depth (+20%/+15% per level)
- [x] Monster AI: idle/wander, LOS-based hunting, flee at low HP
- [x] Death → corpse, XP system, level-up choice screen (attribute/HP/MP/speed/damage picks)
- [x] Rest system (r key — 25% HP/MP, 10 turns, 30% interruption chance in dungeons)

### Items & Equipment
- [x] 9+ weapon types, 11+ armor types, potions, food, gold
- [x] Paper doll inventory with mouse support, item sprites in equip slots
- [x] Item quality tiers: Fine (+1), Superior (+2), Masterwork (+3) at depth 5/10/15
- [x] Item stats visible on selection (damage, armor, dodge, heal, gold value)
- [x] Shops: buy (random stock, sprites, stats), sell (half price)
- [x] Identification system (potions have unid color names)

### Character System
- [x] 4 classes (Fighter, Rogue, Wizard, Ranger) with sprites, stats, starting spells
- [x] 7 gods with stat bonuses, domain descriptions, lore
- [x] 15 backgrounds with unique passives and stat bonuses
- [x] 22 traits (12 positive, 10 negative) with stat modifiers
- [x] Character creation: Class (big sprite) → Name (random + editable) → God → Background → Traits
- [x] Character sheet (c key) with 40+ derived stats in 3-column layout
- [x] Name entry with random fantasy name pool (32 names)

### Magic
- [x] 15 spells across 6 schools with atmospheric Dark Souls-style descriptions
- [x] Spell screen (z key), MP system, INT scaling, auto-targeting
- [x] Class starting spells (Wizard 3, Ranger 1, others Minor Heal)

### World & Content
- [x] 2000x1500 tile overworld (hand-editable text map format)
- [x] 20 towns with buildings, NPCs (shopkeeper, blacksmith, scholar, guard, farmer, herbalist, merchant, elder)
- [x] 27 dungeon entrances (9 named quest-linked + 18 generic), 5 climate zones
- [x] Named dungeons near quest towns: The Barrow, Ashford Ruins, Stonekeep, Frostmere Depths, The Catacombs, The Molten Depths, The Sunken Halls, The Hollowgate, The Sepulchre
- [x] Dungeon registry (data/dungeons.json) — zone themes and quest links loaded at runtime
- [x] Rivers, lakes, ruins, forests, rocky areas, roads connecting towns and quest dungeons
- [x] World map overview (M key) with labeled towns, dungeon markers, region names
- [x] 6 dungeon zones with depth limits (Warrens 1-3, Stonekeep 4-6, etc.)
- [x] Zone theming from dungeon registry (each named dungeon has its own tile theme)
- [x] Proper stairs navigation (up/down within dungeons, return to overworld at entrance point)

### Quests
- [x] 17-step main quest chain spanning all regions (Barrow Wight → Reliquary)
- [x] Quest chaining: each main quest requires the previous one to be FINISHED before offering
- [x] NPCs in 8 quest towns wired to main quests (MQ_01-MQ_14) by proximity + role
- [x] The Sepulchre (MQ_15-17): depth-triggered quests (entry, depth 4+, depth 6)
- [x] 7 static side quests (rat cellar, lost amulet, undead patrol, kill bear, deliver weapon, herbs, missing person)
- [x] Dynamic side quest generation (2-3 per town, based on NPC role)
- [x] Quest log (q key), quest offer modal (accept/decline), quest completion on NPC turn-in
- [x] Barrow Wight boss on depth 3 with quest target component

### UI & Polish
- [x] Main menu (animated campfire, New Game/Continue/Load/Settings/Quit)
- [x] Pause menu (Esc — Continue/Save/Load/Settings/Exit to Menu)
- [x] Settings (resolution, volume, UI scale, keybinds)
- [x] Help screen (? key — all keybinds)
- [x] Save/load (JSON — full world state, player, inventory, quests, map)
- [x] Death returns to main menu with full state reset
- [x] NPC lore dialogue (randomized pools per role, position-deterministic)

---

## What's NOT Built (Priority Order)

### Tier 1 — Makes it a game
- [ ] God prayers + favor mechanics (tenets, gain/loss, prayer abilities)
- [ ] The Sepulchre mega-dungeon (6 levels, final zone — structure/content)
- [ ] Reliquary Vault endgame + god-flavored endings
- [ ] Atmospheric combat messages ("Your blade finds the orc's flank" not "You hit orc for 5")
- [ ] Examine/look mode (essential for lore delivery)
- [ ] Ranged combat (bows, crossbows, throwing)

### Tier 2 — Makes it deep
- [ ] Status effects (poison, burn, bleed, freeze ticking system)
- [ ] Cursed/blessed items
- [ ] Spell failure rate (armor penalty)
- [ ] Blood magic (Yashkhet HP-for-MP casting)
- [ ] Spellbooks as findable dungeon items
- [ ] God-aware NPC reactions (hostile to rival god followers)

### Tier 3 — Makes it polished
- [ ] Sound/music (dark ambient, environmental ambience)
- [ ] God-flavored death screens (per the design doc — Vethrik, Thessarka, etc.)
- [ ] 8 static text endings (Fear & Hunger style)
- [ ] Bestiary system (persistent across runs)
- [ ] Ground lore items (journals, letters, inscriptions)
- [ ] Ambient text cues in dungeons
- [ ] Particle effects (blood, sparks, spell impacts)

### Tier 4 — Makes it complete
- [ ] Permanent diseases (Daggerfall-style stat trade-offs)
- [ ] Pets (equip slot, invincible, small passive)
- [ ] Rival paragons (PC-like NPCs in late-game dungeons)
- [ ] Meta-progression (class unlocks, bestiary persistence)
- [ ] Hardcore/permadeath mode
- [ ] Unlockable classes (10+ from design doc)
- [ ] Heretic class (complete game with all 7 gods)
- [ ] Achievement system

### Tier 5 — Steam release
- [ ] Steamworks integration (achievements, cloud saves)
- [ ] Windows/Mac cross-compile
- [ ] Store page, capsule art, screenshots
- [ ] Balance pass
- [ ] Playtesting

---

## Removed from Scope
- ~~Custom class creation~~ (current 4 classes + god + background + traits provides enough depth)
- ~~Hunger clock~~ (rest system serves the resource pressure role better)
- ~~32-skill Morrowind-style system~~ (7 attributes + level-up choices is simpler and sufficient)

---

## Architecture

```
src/
├── core/          — engine, ecs, tilemap, spritesheet, rng
├── components/    — position, renderable, player, blocker, stats, ai, energy,
│                    corpse, item, inventory, god, class_def, background, traits,
│                    spellbook, npc, quest, quest_target, dynamic_quest
├── systems/       — render, fov, combat, ai, magic
├── generation/    — dungeon, populate, mapfile, village, quest_gen
├── ui/            — message_log, inventory_screen, character_sheet, spell_screen,
│                    creation_screen, background_select, trait_select,
│                    main_menu, pause_menu, settings_screen, help_screen,
│                    quest_log, quest_offer, levelup_screen, shop_screen, world_map,
│                    ui_draw (shared panel/text helpers)
├── save/          — save, load (JSON)
data/
├── maps/          — overworld.map (2000x1500), thornwall.map (legacy)
tools/
├── generate_overworld.py
assets/
├── 32rogues/      — all spritesheets
├── fonts/         — PrStart.ttf, Jacquard12-Regular.ttf
```

## Design Doc
Full game design: `~/Documents/Work/Games/Development/Roguelike Project.md`
