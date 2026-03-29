# Reliquary — Project Plan

> Last updated: 2026-03-28

## Current Status: Playable Alpha — Full Quest Chain Implemented

The game is playable from character creation through a 17-step main quest chain ending with claiming The Reliquary in The Sepulchre. All quest items, targets, and NPCs are wired up. 20 towns, 27 dungeons (9 named quest-linked), 5 climate zones.

**Repo:** https://github.com/joconno2/Reliquary.git
**Location:** ~/Reliquary
**Build:** `cd ~/Reliquary && cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`
**Run:** `cd ~/Reliquary/build && ./reliquary`
**Design doc:** ~/Documents/Work/Games/Development/Roguelike Project.md

---

## What's Built (Complete)

### Core Engine
- SDL2/C++20/CMake, custom ECS, tilemap, spritesheet loading (32rogues + pre-flipped copies for sprite mirroring)
- BSP dungeon generation, 6 themed zones (Warrens, Stonekeep, Deep Halls, Catacombs, Molten Depths, Sunken Halls)
- FOV (recursive shadowcasting), camera (48px tiles), energy/speed turn system
- Message log (top-down, color-coded), dynamic resolution, fullscreen (F11)
- Bundled fonts: Press Start (body 12px), Jacquard (title 32px, menu 96px)
- Pixel-art panel borders (SNES-style), UI scale system (1.0x-2.0x)
- Save/load (JSON — full world state, player, inventory, quests, map, return position)

### Combat & Creatures
- Melee combat (d20 + attack vs dodge, crits, equipment bonuses, depth scaling)
- 18+ monster types, HP/damage scale with depth (+20%/+15% per level)
- Monster AI: idle/wander, LOS hunting, flee at low HP
- Death → corpse, XP, level-up choice screen (attribute/HP/MP/speed/damage picks)
- Rest system (r key — 25% HP/MP, 10 turns, 30% interruption in dungeons)
- Sprite mirroring on horizontal movement (pre-flipped spritesheets, no SDL_RenderCopyEx)

### Items & Equipment
- 9+ weapon types, 11+ armor types, potions, food, gold, quest items
- Paper doll inventory with mouse support, item sprites in equip slots
- Item quality tiers: Fine (+1), Superior (+2), Masterwork (+3) at depth 5/10/15
- Item stats visible on selection (damage, armor, dodge, heal, gold value)
- Shops: buy (random stock, sprites, stats), sell (half price)
- Identification system (potions have unid color names)
- Quest items with pickup-triggered quest completion

### Character System
- 4 classes (Fighter, Rogue, Wizard, Ranger) with sprites and starting spells
- 7 gods with stat bonuses and lore (favor component exists but mechanics not active)
- 15 backgrounds with unique passives, 22 traits (12 positive, 10 negative)
- Character creation: Class (128px sprite) → Name (random+editable) → God → Background → Traits
- Character sheet (c key) with 40+ derived stats, 3-column layout
- ~~Custom class creation~~ REMOVED from scope
- ~~Hunger clock~~ REMOVED from scope

### Magic
- 15 spells across 6 schools (Conjuration, Transmutation, Divination, Healing, Nature, Dark Arts)
- Spell screen (z key), MP system, INT scaling, auto-targeting
- Class starting spells (Wizard 3, Ranger 1, others Minor Heal)

### World
- 2000x1500 tile overworld (hand-editable text map: data/maps/overworld.map)
- Generator script: tools/generate_overworld.py
- 20 towns with buildings and NPCs (shopkeeper, blacksmith, scholar, guard, farmer, herbalist, merchant, elder, villager)
- 27 dungeons: 9 named quest-linked + 18 generic exploration
- Dungeon registry: data/dungeons.json (zone themes, quest links, loaded at runtime)
- 5 climate zones (ice/cold/temperate/warm/desert) with tinted floor tiles
- Rivers, lakes, ruins, forests, rocky areas, roads connecting towns to dungeons
- World map overview (M key) with labeled towns, dungeon markers, region names
- Proper stairs: up/down within dungeons, return to overworld at entrance point

### Quests — Fully Wired
- **17-step main quest chain** with chaining (each requires previous FINISHED):
  1. MQ_01 Barrow Wight — kill boss in The Barrow depth 3 (Thornwall)
  2. MQ_02 Scholar Clue — talk to Scholar Aldric (Thornwall)
  3. MQ_03 Ashford Tablet — find Stone Tablet in Ashford Ruins depth 3
  4. MQ_04 Greywatch Warning — talk to Captain Voss (Greywatch)
  5. MQ_05 First Inscription — find Ancient Inscription in Stonekeep depth 3
  6. MQ_06 Frostmere Sage — talk to Sage Yeva (Frostmere)
  7. MQ_07 Frozen Key — find Frozen Key in Frostmere Depths depth 3
  8. MQ_08 Catacombs Gate — talk to Scholar Maren (Millhaven)
  9. MQ_09 First Fragment — find Reliquary Fragment in The Catacombs depth 3
  10. MQ_10 Ironhearth Forge — talk to Master Smith Brynn (Ironhearth)
  11. MQ_11 Molten Trial — find Molten Fragment in The Molten Depths depth 3
  12. MQ_12 Candlemere Ritual — talk to Priest Solara (Candlemere)
  13. MQ_13 Sunken Fragment — find Sunken Fragment in The Sunken Halls depth 3
  14. MQ_14 Hollowgate Seal — find Seal Stone in The Hollowgate depth 3
  15. MQ_15 The Sepulchre — auto-trigger on entry (far north)
  16. MQ_16 The Descent — auto-trigger at depth 4
  17. MQ_17 Claim Reliquary — find The Reliquary (golden ankh) at Sepulchre depth 6, guarded by The Keeper (final boss, HP 150, STR 24)
- 7 static side quests + dynamic side quest generation (2-3 per town)
- Quest log (q key), quest offer modal (accept/decline with mouse/keyboard)

### UI Screens
- Main menu: animated campfire, huge Jacquard title, New Game/Continue/Load/Settings/Quit
- Pause menu (Esc): Continue/Save/Load/Settings/Exit to Menu
- Settings: resolution presets, volume, UI scale (1.0-2.0x), keybinds reference
- Help screen (? key): all keybinds in 2-column layout
- Character sheet (c key): 40+ derived stats
- Inventory (i key): paper doll + item list + mouse support
- Spellbook (z key): spell list with MP costs
- Quest log (q key): main/side quests with progress
- World map (M key): terrain overview, town/dungeon markers
- Shop screen: buy/sell tabs with item sprites and stats
- Level-up choice screen: 3 random options per level
- Quest offer modal: accept/decline with rewards shown

### Keybinds
Movement: arrows/hjkl/numpad (+ diagonals yubn) | Wait: ./numpad5
Actions: g/, pickup | Enter/>/< stairs | r rest | ? help
Screens: i inventory | c character | z spells | q quests | M world map | Esc pause
F11 fullscreen | F12 screenshot

---

## What's NOT Built (Priority Order)

### Tier 1 — Core gameplay gaps
- [ ] God prayers + favor mechanics (tenets, gain/loss, prayer abilities, sacred/profane)
- [ ] Atmospheric combat messages ("Your blade finds the orc's flank" not "You hit orc for 5")
- [ ] Examine/look mode (x key — describe tiles, items, creatures)
- [ ] Ranged combat (bows, crossbows, throwing — Rangers need this)
- [ ] Status effects (poison, burn, bleed, freeze tick system)
- [ ] The Sepulchre needs unique content/feel (currently just another themed dungeon)
- [ ] Game ending screen when claiming The Reliquary (god-flavored text)

### Tier 2 — Depth
- [ ] Cursed/blessed items
- [ ] Spell failure rate (armor penalty)
- [ ] Blood magic (Yashkhet HP-for-MP)
- [ ] Spellbooks as findable dungeon items
- [ ] God-aware NPC reactions (hostile to rival followers)
- [ ] Monster spells/abilities (liches should cast, etc.)

### Tier 3 — Polish
- [ ] Sound/music (dark ambient, environmental ambience)
- [ ] God-flavored death screens
- [ ] 8 static text endings (one per god + heretic)
- [ ] Bestiary system
- [ ] Ground lore items (journals, inscriptions)
- [ ] Ambient text cues in dungeons
- [ ] Particle effects

### Tier 4 — Complete
- [ ] Permanent diseases (Daggerfall-style)
- [ ] Pets (equip slot, passive bonus)
- [ ] Rival paragons (PC-like enemies in late dungeons)
- [ ] Meta-progression (class unlocks, bestiary persistence)
- [ ] Hardcore/permadeath mode
- [ ] Unlockable classes (10+ from design doc)

### Tier 5 — Ship
- [ ] Steamworks integration
- [ ] Windows/Mac builds
- [ ] Store page, art
- [ ] Balance pass
- [ ] Playtesting

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
│                    quest_log, quest_offer, levelup_screen, shop_screen,
│                    world_map, ui_draw
├── save/          — save.cpp/.h (JSON serialization)
data/
├── maps/          — overworld.map (2000x1500)
├── dungeons.json  — dungeon registry (27 entries with zone/quest links)
tools/
├── generate_overworld.py — regenerate overworld + dungeons.json
assets/
├── 32rogues/      — all spritesheets
├── fonts/         — PrStart.ttf, Jacquard12-Regular.ttf
```

## Key Files to Know
- **engine.cpp** — ~1400 lines, the main game loop. Most gameplay logic lives here.
- **quest.h** — all 17 main quest + 7 side quest definitions
- **populate.cpp** — monster/item spawning with depth scaling
- **generate_overworld.py** — generates overworld.map + dungeons.json
- **dungeons.json** — runtime dungeon registry linking map positions to zone themes and quests
