# Reliquary — Project Plan

> Last updated: 2026-03-31

## Current Status: v0.2.0 Alpha Release

The game is fully playable from character creation through a 17-step main quest chain. 17 classes (4 base + 13 unlockable), **13 gods** (expanded from 7) with deep tenet system and sacred/profane items, 15 backgrounds, 22 traits, 7 permanent diseases, 8 pets, **13 rival paragons**, **50 spells** across 6 schools, **80+ items** with material palette swaps (bone/silver/mithril/adamantine). 8 status effects (poison/burn/bleed/frozen/stunned/confused/blind/feared). Full audio system with 19 music tracks, 12 ambient loops, 24 SFX. Animated torches/braziers/water with circular warm glow. 6 provinces with capital cities, 20 towns + 4 hamlets + 6 cabins + 3 outposts, 27 dungeons, wilderness POIs, wandering NPCs. **Signpost navigation** (~80+ signs with compass directions to nearby POIs). Meta-save progression, hardcore mode, persistent bestiary/potion IDs. Dungeon floor persistence across save/load. Resolution scaling for ultrawide/4K. **CI/CD pipeline** builds Linux + Windows. **God-specific player aura particles on every god.** NPC sprites properly assigned (rows 6-7 for NPCs, rows 0-5 for PCs/special named NPCs).

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
- FOV (recursive shadowcasting), camera (resolution-scaled tiles), energy/speed turn system
- Message log (top-down, color-coded), fullscreen by default, F11 toggle, resolution scaling (1080p baseline, auto-scales fonts/tiles/UI for ultrawide/4K)
- Bundled fonts: Press Start (body), Jacquard (titles)
- Pixel-art panel borders (SNES-style), proportional UI layouts
- Animated tiles: flickering torches/braziers with warm glow, animated water waves
- Save/load (JSON — full world state, player, inventory, quests, map, return position, buffs, traits)
- **Dungeon floor persistence** — `floor_cache_` with full entity serialization (Stats, AI, Energy, Items, Containers, StatusEffects, GodAlignment), separate `floors.dat` file
- **F5/F6 quicksave/quickload**
- Full audio system: 24 real SFX (pixel combat pack), 19 music tracks (Ifness — title/overworld/dungeon/boss/death/victory/sepulchre), 12 ambient loops (Fantasy SFX — cave/forest/interior/fire/rain/river). Title screen: Gethsemane + fire crackle + rain. Music/ambient crossfade on location change.

### Combat & Creatures
- Melee combat with atmospheric messages (weapon-aware, body parts, damage in parens)
- Ranged combat (f key — bows/crossbows, DEX-based, auto-target nearest visible enemy)
- **8 status effects**: poison (spider/naga), burn (dragon), bleed (ghoul), frozen, stunned, confused, blind, feared — tick damage, HUD indicators, monster abilities (troll regen, spider poison, wraith confuse, death knight fear, basilisk blind, orc warchief buff)
- Permanent diseases: 7 Daggerfall-style diseases (lycanthropy, vampirism, stonescale, mindfire, sporebloom, hollow bones, blackblood) — contracted from monster hits, CON resist, permanent stat trade-offs, HUD + character sheet display
- Rival paragons: 13 god-affiliated PC-like enemies (Osric/Mirael/Dain/Sera/Theron/Lucan/The Unnamed/Whisper/Nerissa/Varn/The Sleeper/Borek/Mother Rot), depth 4+ in named dungeons, 15% spawn rate, depth-scaled, class-based sprites with god-colored tints
- 18+ monster types, HP/damage scale with depth (+20%/+15% per level)
- 9 overworld enemy types: wolves, wild boars, highwaymen, giant spiders, bears, bandits, snakes, dire wolves, wandering skeletons
- Monster AI: idle/wander, LOS hunting, flee at low HP, ranged attacks (goblin archers)
- Death → corpse, XP, level-up choice screen (attribute/HP/MP/speed/damage picks)
- Rest system (r key — 25% HP/MP, 10 turns, 30% interruption in dungeons)
- Sprite mirroring on horizontal movement (pre-flipped spritesheets, no SDL_RenderCopyEx)
- Examine mode (x key) — cursor to inspect tiles, creatures (with HP), items, NPCs, corpses

### Items & Equipment
- **80+ items**: 28 melee weapons (tiered), 5 ranged, 6 staves, 23 armor pieces, 6 amulets, 7 rings, 9 consumables, 9 legendaries
- Cursed items (can't unequip, bait stats), blessed items (+1 bonus), revealed on equip/identify
- Pet system: 8 pets (rat/dog/cat/owl/snake/bat/imp/crow), PET equip slot, invincible sprite follows behind player, passive bonuses (crow scavenges gold)
- Paper doll inventory with mouse support, item sprites in equip slots
- Item quality tiers: Fine (+1), Superior (+2), Masterwork (+3) at depth 5/10/15
- **Material system**: 5 materials (Iron default, Bone, Silver, Mithril, Adamantine) with palette swap sprites, depth-scaled distribution, damage modifiers (bone -1, silver +1 & +50% vs undead, mithril +2, adamantine +4)
- Weighted item distribution (`weighted_item_pick`) spreading gear across difficulty zones
- Material weapons in shops at difficulty 5+
- Item stats visible on selection + effective damage display ("Effective: X dmg (STR/DEX)")
- 9 consumables: healing/strong healing/mana potions, antidote, speed draught, strength elixir, bread, cheese, dried meat
- Shops: buy (random stock, sprites, stats), sell (half price)
- Identification system (potions have unid color names)
- Quest items with pickup-triggered quest completion

### Character System
- 17 classes: 4 base (Fighter, Rogue, Wizard, Ranger) + 13 unlockable with meta-save progression
- **13 gods** with stat bonuses, lore, favor system, 2 prayers each (p key), unique passives, and player aura particles
  - Original 7: Vethrik (death), Thessarka (knowledge), Morreth (war), Yashkhet (blood), Khael (nature), Soleth (fire), Ixuul (chaos)
  - New 6: Zhavek (shadow/stealth), Thalara (sea/storms), Ossren (craft/forge), Lethis (sleep/dreams), Gathruun (stone/earth), Sythara (plague/decay)
- God favor: +1 per kill, +5 per quest, god-specific bonuses/penalties (undead, animals, exploration, rest, stealth kills, depth, poison, sleeping enemies)
- Prayers: 26 unique prayers (2 per god) — heals, damage, teleport, MP restore, map reveal, AoE, blood sacrifice, invisibility, silence, riptide, drown, temper items, unyielding armor, sleep enemies, forget, earthquake, stone skin, miasma, armor corrode
- God passives with real gameplay impact: Vethrik +15% undead damage, Zhavek 2x stealth damage + enemy memory loss, Lethis lethal save once/floor + 50% rest healing, Gathruun +1 armor/depth + earthquake scaling, Sythara disease spread + 2x poison duration, etc.
- Per-god player aura particles: bone motes, orbiting runes, iron sparks, blood drips, leaf drift, flame halo, void glitch, shadow trail, water ripples, forge sparks, purple mist, stone orbit, green spores
- 15 backgrounds with unique passives, 22 traits (12 positive, 10 negative)
- Character creation: Class grid (all 17 visible, SNES-bordered selection) → Name → God → Background → Traits → Hardcore toggle
- Pet naming dialog on pickup
- All classes start with appropriate gear (weapon + armor per archetype)
- Character sheet (c key) with 40+ derived stats, 3-column layout
- ~~Custom class creation~~ REMOVED from scope
- ~~Hunger clock~~ REMOVED from scope

### Magic
- **50 spells** across 6 schools (Conjuration, Transmutation, Divination, Healing, Nature, Dark Arts)
- Includes: Chain Lightning, Frost Nova, Polymorph, Phase (teleport), Beast Call/Swarm (summon allies), Raise Dead, Poison Cloud, Thornwall, Earthquake, Blood Pact, Doom, Wither, and more
- Spell screen (z key) with spell description panel, MP system, INT scaling, auto-targeting
- Spell failure from heavy armor (chain 15%, plate 25% per piece)
- Spellbooks drop in dungeons — "Tome of X" teaches spell on use
- Class starting spells (Wizard 3, Ranger 1, others Minor Heal)
- Blood magic: Yashkhet spells cost HP instead of MP

### World
- 2000x1500 tile overworld (hand-editable text map + programmatic decoration pass)
- 20 towns with buildings and wandering NPCs + herbalists/merchants at every town
- 4 hamlets (Thornbrook, Icewind Post, Dry Creek, Mosshaven) — wood cabin clusters with villagers
- 6 isolated cabins with hermit NPCs (Woodsman, Recluse, Swamp Hermit, Retired Soldier, Old Hunter, Cartographer)
- 3 road outposts with guards at crossroads
- 18+ wandering wilderness NPCs (travelers, pilgrims, hunters, hermits, refugees, sellswords, field scholars)
- Points of interest: 3 standing stones with ancient lore, graveyard, old battlefield, ancient shrine, watchtower ruins, witch's hut
- 5 small lakes, 3 river segments, animated water with wave overlay
- Town doodads (barrels, log piles against walls), overworld vegetation by climate zone, mushrooms near forests
- Dungeon doodads: lootable chests/jars, mushrooms, blood splatters, coffins, barrels, animated torches/braziers with warm glow
- Wood wall tile type for cabins and hamlets
- 27 dungeons: 9 named quest-linked + 18 generic exploration
- **6 provinces** (Pale Reach, Frozen Marches, Heartlands, Greenwood, Iron Coast, Dust Provinces) with god affiliations and province-tinted walls
- 5 climate zones (ice/cold/temperate/warm/desert) with tinted floor tiles and region-appropriate plants
- **Signpost system** — ~80+ signs at town outskirts, dungeon entrances, and road crossings with auto-generated compass directions to nearest POIs
- Wall rendering: correct top/side view based on wall depth (bottom row = side face, upper rows = top view)
- Town/boss music triggers based on proximity and enemy presence
- The Sepulchre: atmospheric entry messages per depth, ambient text every ~18 turns

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
- 7 static side quests (all wired: rat cellar, lost amulet, undead patrol, kill bear, deliver weapon, herb gathering, missing person)
- Dynamic side quest generation (2-3 per town, requires actual travel/dungeon entry/minimum turns)
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
- Victory screen: god-flavored ending text (8 variants + heretic), any key to return to menu

### Keybinds
Movement: arrows/hjkl/numpad (+ diagonals yubn) | Wait: ./numpad5
Actions: g/, pickup | Enter/>/< stairs | r rest | f fire ranged | p pray | x examine | ? help
Screens: i inventory | c character | z spells | q quests | M world map | Tab bestiary | Esc pause
F11 fullscreen | F12 screenshot

---

## What's NOT Built (Priority Order)

### Tier 1 — COMPLETE
- [x] God prayers + favor mechanics — 2 prayers per god (p key), favor from kills/quests/tenets
- [x] Ranged combat — bows/crossbows drop in dungeons, f key to fire, goblin archers shoot back
- [x] Atmospheric combat messages — weapon-aware hit/miss/crit/death with damage in parens
- [x] Examine/look mode (x key) — cursor movement, describes tiles/creatures/items/NPCs with HP
- [x] Status effects — poison (spider/naga), burn (dragon), bleed (ghoul), tick damage, HUD indicators
- [x] Sepulchre unique content — entry messages per depth, ambient text every ~18 turns
- [x] Game ending screen — 8 god-flavored victory texts + heretic ending on claiming The Reliquary

### Tier 2 — COMPLETE
- [x] Cursed/blessed items — 10% cursed (can't unequip, revealed on equip), 8% blessed (+1 bonus), depth 3+
- [x] Spell failure rate — heavy armor (chain 15%, plate 25%) causes spell fizzle, still costs MP
- [x] Blood magic (Yashkhet HP-for-MP) — implemented as Yashkhet prayer "Blood Offering"
- [x] Spellbooks as findable dungeon items — "Tome of X" teaches spells on use, ~5% of loot, depth-scaled
- [x] God-aware NPC reactions — priests react to Ixuul/Yashkhet/faithless negatively, high favor positively
- [x] Monster spells/abilities — lich drains life at range, dragon breathes fire (+ burn), death knight fear aura

### Tier 3 — Polish
- [x] Sound effects (24 sfx via sox synthesis + SDL_mixer) — combat, spells, items, prayer, UI, status ticks
- [x] God-flavored death screens — unique death text per god + heretic
- [x] 8 static text endings (one per god + heretic) — victory screen from Tier 1
- [x] Bestiary system (Tab key) — tracks kills with HP/dmg/armor/speed stats
- [x] Ground lore items — 10 journals/inscriptions/notes with world-building text, ~4% of loot
- [x] Ambient text cues in dungeons — zone-specific messages (warrens/stonekeep/catacombs/molten/sunken/deep halls/sepulchre)
- [x] Particle effects — blood splatter, crit flash, death burst, arrow trails, spell bursts, heal/poison/burn/bleed effects, god-colored prayers, gold sparkle, level-up glow

### Tier 4 — COMPLETE
- [x] Permanent diseases (Daggerfall-style) — 7 diseases (lycanthropy/vampirism/stonescale/mindfire/sporebloom/hollow bones/blackblood), contracted from monster hits with CON resist check, permanent stat modifiers, HUD indicators, character sheet display, save/load. Vampirism blocks rest HP regen + surface damage, blackblood poison immunity + retaliation, sporebloom dungeon regen.
- [x] Pets (equip slot, passive bonus) — 8 pets (rat/dog/cat/owl/snake/bat/imp/crow), PET equip slot, invincible visual entity follows player 1 tile behind, sprites from animals.png/monsters.png, crow scavenges gold on kills. Found in dungeons depth 2+ (~3% drop). Save/load recreates visual on load.
- [x] Rival paragons — 7 god-affiliated PC-like enemies (one per god, never player's own god). Spawn depth 4+ in named dungeons, 15% chance. Class-based stats/sprites, god-colored tints, depth scaling. +10 favor on kill, unique death message. Examine shows "Paragon of [god]".
- [x] Dynamic quest improvements — quests now require actual gameplay: delivery quests need travel to target town (bump NPC there), dungeon quests need dungeon entry, wilderness quests need minimum turns. Herbalist + Merchant NPCs added to all 20 towns.
- [x] Unlockable classes — 13 classes (Barbarian, Knight, Monk, Templar, Druid, War Cleric, Warlock, Dwarf, Elf, Bandit, Necromancer, Schema Monk, Heretic) with unique stats/sprites/starting spells. Meta-save persists across runs (save/meta.json). Tracks kills, undead kills, HP healed, Dark Arts casts, quests, depth, gold, god completions. Locked classes shown grayed with unlock hints in creation screen.
- [x] Meta-progression polish — persistent bestiary (monster stats + kill counts survive across runs, pre-populated on new game) and potion identification (consumed potions auto-identified in future runs on pickup)
- [x] Hardcore/permadeath mode — toggle in character creation after traits. Save deleted on death + one-shot load (save removed after loading). Red "HC" indicator on HUD. Meta-save still persists.

### Polish (post-Tier 4)
- [x] All 7 static side quests now reachable — SQ_RAT_CELLAR (Ashford shopkeeper, kill 5 rats), SQ_LOST_AMULET (Millhaven farmer, find amulet in dungeon), SQ_UNDEAD_PATROL (Greywatch guard, kill 10 undead)
- [x] Town + boss music — overworld switches to town tracks near settlements, boss/paragon levels get combat music, periodic proximity check
- [x] 5 new consumables — mana potion (restores 15 MP), antidote (clears poison), speed draught (3 bonus actions), strength elixir (+4 STR), dried meat. All potions have unid color names.
- [x] Identify spell implemented — identifies first unidentified item in inventory. Cure Poison spell also implemented.
- [x] Monster ranged attacks now show arrow trail + sound (was silent). Goblin archer nerfed (range 6->5, damage 3->2).
- [x] All 17 classes start with appropriate gear (weapons + armor per class archetype)
- [x] Weapon efficacy display — inventory shows "Effective: X dmg (STR/DEX)" for at-a-glance comparison
- [x] Overworld world-building — 8 wandering travelers, 3 pilgrims, 3 hunters, 4 hermits on roads and wilderness. 4 encampments (deserter camp, mercenary camp, scholar camp, refugee camp). Points of interest: 3 standing stones with pre-god lore, graveyard, old battlefield, ancient shrine ruins, watchtower ruins, witch's hut. 5 small lakes, 3 river segments. 9 overworld enemy types (up from 4: added bear, bandit, snake, dire wolf, wandering skeleton).
- [x] Class unlock notifications on death/victory + progress display in creation screen

### Tier 5 — God System Deep + Systems (in progress)

**5A. Tenet System** — behavioral rules that auto-adjust favor every turn ✅
- [x] Define 3-4 tenets per god as data (tenet.h) — each tenet is a condition + favor delta
- [x] Tenet checker runs end of each turn in process_turn() — evaluates conditions against recent player actions
- [x] Track "recent actions" flags per turn (PlayerActions struct): killed_animal, killed_sleeping, used_dark_arts, used_fire_magic, used_poison, used_stealth_attack, fled_combat, wore_heavy_armor, healed_above_75pct, destroyed_book, rested_on_surface, etc.
- [x] Tenet violations: immediate -3 to -5 favor + god-colored warning message
- [x] Tenet compliance: passive +1 favor every 20 turns when no violations
- [x] Display tenets in god detail panel (creation screen)
- [x] Lethis floor-rest tenet checked on descent

**5B. Sacred/Profane Items** — god-specific item affinities ✅
- [x] Add `material` field to Item (MaterialType enum: NONE, BONE, WOOD, IRON, STEEL, SILVER, OBSIDIAN, MITHRIL, ADAMANTINE)
- [x] Add `item_tags` bitfield to Item (24 tag types: TAG_DAGGER, TAG_BLUNT, TAG_AXE, TAG_HEAVY_ARMOR, TAG_BOOK, TAG_HERB, TAG_TORCH, etc.)
- [x] Define sacred/profane tag sets per god in tenet.h (SacredProfane struct + get_sacred_profane())
- [x] Sacred items: +1 favor on pickup + god-colored approval message
- [x] Profane items: -2 favor on equip + god-colored warning message
- [x] Material system: depth-scaled material assignment (bone/wood/iron → steel/silver → obsidian/mithril → adamantine)
- [x] Material name in item display ("steel long sword", "mithril chainmail")
- [x] Material damage modifiers applied (bone -1, wood -2, steel +1, obsidian/mithril +2, adamantine +4)
- [x] Save/load for material and tags fields

**5C. God Shrines** — interactive dungeon objects ✅
- [x] Shrine tile type (TileType::SHRINE) with altar sprite
- [x] ~20% chance per floor, placed in mid-room center
- [x] Shrines now spawn with dungeon's patron god (from dungeons.json patron_god_idx) instead of random
- [x] Same-god shrine: +5 favor, small heal, identify all equipped items
- [x] Rival-god shrine: +2 own favor, message
- [x] Godless player: "It means nothing to you"
- [x] Excommunicated conversion at rival shrine (see 5D)

**5D. Excommunication & Conversion** ✅
- [x] Prayers always fail when excommunicated (favor <= -100)
- [x] Periodic divine damage (~60% every 15 turns, 2-6 dmg) with god-colored message + crit flash
- [x] Periodic stat drain (~40% every 40 turns, -1 random attribute)
- [x] Conversion at rival god's shrine: reset to 0 favor, -2 all attributes, sprite tint update
- [x] Priests refuse interaction with excommunicated players
- [x] Merchants charge double (200% price multiplier) with warning message

**5E. NPC God Factions** ✅
- [x] Province→god lookup: 6 provinces map to 6 patron gods (Heartlands=Morreth, Pale Reach=Soleth, Frozen Marches=Gathruun, Greenwood=Khael, Iron Coast=Ossren, Dust Provinces=Sythara)
- [x] god_affiliation field on NPC component (GodId)
- [x] All 20 town NPCs auto-assigned god affiliation based on province position
- [x] Extra NPCs (herbalists, merchants) also get town god affiliation
- [x] Same-god shopkeepers: -15% prices + "fellow believer" message
- [x] Rival-god shopkeepers: +25% prices + "wary of your faith" message
- [x] Same-god priests: free healing (+10 HP) + god-colored welcome
- [x] Rival-god priests: territorial warning dialogue
- [x] Hostile faction: Soleth priests refuse Ixuul followers
- [x] Excommunicated: priests refuse, merchants charge double (from 5D)
- [x] 7 wandering priests for non-provincial gods (Vethrik, Thessarka, Yashkhet, Ixuul, Zhavek, Thalara, Lethis) on wilderness roads with god affiliations
- [ ] Town visual identity (god-themed decorations in generate_overworld.py) — deferred to polish

**5F. God Relics** — 13 unique legendary items ✅
- [x] One relic per god (13 total): Skull of the Ossuary (Vethrik), Eye of the Eyeless (Thessarka), Fist of the Iron Father (Morreth), Heartseeker (Yashkhet), Antler Crown (Khael), Ember of the Pale Flame (Soleth), Void Shard (Ixuul), Shroud of the Unseen (Zhavek), Tide of the Drowned (Thalara), Hammer Unworn (Ossren), Dream Veil (Lethis), Heart of the Mountain (Gathruun), Rot Blossom (Sythara)
- [x] Spawn on bottom floor of late-game dungeons (zone_difficulty >= 6) with patron god, ~30% chance
- [x] God-colored tint, high z-order rendering, always identified, priceless
- [x] Can't be unequipped (bound, not cursed — distinct messaging)
- [x] Each has powerful combat/stat bonuses + a stat penalty trade-off
- [x] Equipping own god's relic: +20 favor
- [x] Equipping rival god's relic: -50 favor (potential excommunication trigger)
- [x] relic_god field on Item, fully serialized in save/load

**5G. Material System** — weapon/armor materials ✅
- [x] MaterialType enum: NONE, BONE, SILVER, MITHRIL, ADAMANTINE (Iron is default/untagged)
- [x] Material affects: damage_mod, special properties (silver +50% vs undead)
- [x] Palette swap sprite selection based on material (items-palette-swaps.png)
- [x] Material determines sacred/profane status per god
- [x] Integrate into item generation (populate.cpp) — deeper = better materials
- [x] Material weapons in shops at difficulty 5+

**5H. Expanded Spells** — 15 → 50 spells ✅
- [x] 50 spells across 6 schools (Conjuration, Transmutation, Divination, Healing, Nature, Dark Arts)
- [x] Spellbook UI shows spell descriptions on highlight
- [x] Spellbook drops scale with dungeon depth and school affinity of zone
- [x] God-profane spells: casting Dark Arts as Soleth/Vethrik = tenet violation
- [x] Blood magic: Yashkhet spells cost HP instead of MP

**5I. More Status Effects** ✅
- [x] 8 status effects: poison, burn, bleed, frozen, stunned, confused, blind, feared
- [x] Monster abilities: troll regen, spider poison, wraith confuse, death knight fear, basilisk blind, orc warchief buff
- [x] Each status has visual indicator on HUD

### Tier 6 — Ship (deferred)
Steamworks integration, Windows builds, store page, balance pass, playtesting — not in scope yet.

---

## Architecture

```
src/
├── core/          — engine, ecs, tilemap, spritesheet, rng
├── components/    — position, renderable, player, blocker, stats, ai, energy,
│                    corpse, item, inventory, god, class_def, background, traits,
│                    spellbook, npc, quest, quest_target, dynamic_quest, tenet,
│                    prayer, buff, status_effect, container, sign, disease, pet
├── data/          — world_data.h (canonical town/province data, shared constants)
├── systems/       — render, fov, combat, ai, magic, god_system, npc_interaction, status
├── generation/    — dungeon, populate, mapfile, village, quest_gen, overworld, player_setup
├── ui/            — message_log, inventory_screen, character_sheet, spell_screen,
│                    creation_screen, background_select, trait_select,
│                    main_menu, pause_menu, settings_screen, help_screen,
│                    quest_log, quest_offer, levelup_screen, shop_screen,
│                    world_map, ui_draw, death_screen
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
- **engine.cpp** — ~4200 lines, core game loop: movement, combat, input, rendering, level generation. Refactored 2026-04-01 from ~6900 lines.
- **world_data.h** — canonical town coordinates, province→god mapping, compass helpers. Single source of truth for all 20 towns.
- **god_system.cpp** — prayers (26 total), tenet checks, favor adjustments, god aura rendering. All god logic consolidated here.
- **overworld.cpp** — overworld population (wilderness NPCs, cabins, hamlets, vegetation, signs, water, wandering priests)
- **npc_interaction.cpp** — NPC bump handling: shops, quests, priest reactions, god-faction pricing
- **status.cpp** — status effect ticking, disease effects, buff expiration, god passives, negative favor punishments
- **player_setup.cpp** — player entity creation (class/god/trait/background setup, starting gear/spells)
- **quest_gen.cpp** — quest boss/item spawning, dynamic quest generation
- **death_screen.cpp** — death and victory screen rendering
- **quest.h** — all 17 main quest + 7 side quest definitions with compass direction text
- **god.h** — 13 gods: GodInfo, GodColor, GodAlignment, favor system
- **tenet.h** — tenet checks, PlayerActions, sacred/profane items per god
- **magic.cpp** — all 50 spell implementations (AoE, summons, status effects, blood magic)
- **populate.cpp** — monster/item/legendary/relic spawning with depth + zone difficulty scaling
- **generate_overworld.py** — province system, capital cities, structured town generation
- **dungeons.json** — runtime dungeon registry with zone/quest/province links
