# Reliquary -- Project Plan

> Last updated: 2026-05-07

## Current Status: v0.3.55+, Polish Pass Complete

All core systems built, wired, and functional. Major UI/UX polish pass, balance tuning, content additions, and bug fixes applied May 6-7. No TODOs, FIXMEs, or disabled code. ~41K lines of C++20.

**Repo:** https://github.com/joconno2/Reliquary.git
**Location:** ~/projects/Reliquary
**Build:** `cd ~/projects/Reliquary && cmake -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build`
**Run:** `cd ~/projects/Reliquary/build && ./reliquary`
**Design doc:** ~/Documents/Work/Games/Development/Roguelike Project.md
**Steam:** App 4627800, depots 4627801 (Linux) + 4627802 (Windows). Release via `./release.sh`. GitHub is backup only (no CI).

---

## The Game Right Now

Fully playable roguelike from character creation through a 9-step main quest chain ending with a 3-phase final boss in a 9-floor endgame dungeon.

### World
- **1000x750 tile overworld** (tightened from 2000x1500 in Apr 2026)
- **10 towns** (8 quest, 2 non-quest): Thornwall, Millhaven, Candlemere, Frostmere, Greywatch, Whitepeak, Bramblewood, Hollowgate, Ironhearth, Dustfall
- **24 dungeons** across 6 zones: warrens (4), stonekeep (4), catacombs (4), molten (4), deep halls (4), sunken (3), sepulchre (1)
- **6 provinces** with god affiliations, climate-tinted terrain, province-themed town visuals
- Overworld entities: wandering NPCs, merchants, wildlife, monster lairs, encampments, standing stones, POIs
- Signpost navigation, day/night cycle, overworld weather (snow/rain/dust by latitude)

### Characters
- **24 classes**: 4 base (Fighter, Rogue, Wizard, Ranger) + 20 unlockable (Barbarian, Knight, Monk, Templar, Druid, War Cleric, Warlock, Dwarf, Elf, Bandit, Necromancer, Schema Monk, Heretic, Wyrmkin, Revenant, Serpentine, Trollblood + 3 more)
- Every class has a **verb** (how you play), **gear interaction** (synergy weapon), **level 5 ability** (second major power), and **visual signature**
- **VERB-based design**: Fighter parries, Rogue ambushes, Barbarian rages, Monk flurries, Necromancer explodes corpses, Wyrmkin breathes fire, etc. Each class changes gameplay, not just stats.
- **Exponential scaling synergies**: class abilities grow stronger with the right gear/tree investment
- **Single-screen character builder**: class grid (left 2/3) with description panel (right 1/3), then god + traits + background on one build screen
- AD&D PHB-style class descriptions. Backgrounds rewritten mechanical-first.
- 15 backgrounds with unique passives, 22 traits (12 positive, 10 negative)
- Meta-save progression: class unlocks, persistent bestiary, potion ID carry-over, hardcore mode

### Gods (13)
- Vethrik (death), Thessarka (knowledge), Morreth (war), Yashkhet (blood), Khael (nature), Soleth (fire), Ixuul (chaos), Zhavek (shadow), Thalara (sea), Ossren (craft), Lethis (dreams), Gathruun (stone), Sythara (plague)
- Full tenet system (behavioral rules, favor +/-), sacred/profane items, god shrines, excommunication/conversion
- 26 prayers (2 per god) + god mastery ability at favor 75
- NPC god factions: province-based pricing, healing, hostility
- 13 god relics (bound legendary items), 7 wandering priests, per-god player aura particles

### Combat
- Melee (STR), ranged (DEX, f key), 56 spells across 6 schools (INT scaling)
- 8 status effects: poison, burn, bleed, frozen, stunned, confused, blind, feared
- Status visuals: enemy tint by active status
- 7 permanent diseases (Daggerfall-style)
- 12 monster AI behaviors: basic, archer, lich (teleport + drain + summon), troll (regen), charger (minotaur), dragon (breath AoE), pack (flanking), wraith (phase through walls), keeper (3-phase boss), necromancer (raise dead), shaman (heal + buff), thief (hit and run)
- All class abilities wired into combat (on-hit, on-kill, conditional triggers)
- Dual wield, riposte, counter-attacks, death mark, last stand

### Passive Tree
- **260+ nodes** in 8 sectors (Might, Finesse, Arcane, Faith, Fortitude, Nature, Shadow, Venom) + center hub
- 8 keystones: Blood Magic, Ghost Blade, Zealot, Iron Reflexes, Chaos Inoculation, Point Blank, Avatar of Wild, Vampiric Pact
- 8 capstone active abilities: Whirlwind, Time Slip, Arcane Overload, Divine Intervention, Unbreakable, Aspect of Beast, Death Mark, Pandemic
- **Class verb amplifiers**: 8 tree nodes that boost specific class mechanics (status duration, shapeshift damage, fury chain, siphon, explode damage, counter damage, stealth opener, breath damage)
- 15 use-based skills (Blades, Axes, Blunt, Unarmed, Archery, 6 spell schools, Stealth, Heavy Armor, Dodge, Prayer)
- Skill requirements gate 6 notable nodes

### Items
- 80+ base items, 5 rarity tiers (Common/Magic/Rare/Legendary/Relic)
- 40 affixes (20 prefix + 20 suffix): on-hit procs, on-kill effects, stat bonuses, resistances
- 28 unique items with 18 UniqueEffect types, zone-specific drops
- 9 materials (Bone through Adamantine) with palette swap sprites and damage modifiers
- 13 god relics
- Legendaries overhauled: glowing icons, world secrets, guardians

### Quests
- **9-step main quest chain** (tightened from 17):
  1. MQ_01 Barrow Wight (kill boss, The Barrow)
  2. MQ_02 Scholar Clue (talk to Aldric, Thornwall)
  3. MQ_03 First Fragment (Stonekeep)
  4. MQ_04 Sage Counsel (talk to Yeva, Frostmere)
  5. MQ_05 Second Fragment (The Catacombs)
  6. MQ_06 Third Fragment (The Molten Depths)
  7. MQ_07 Break Seal (The Hollowgate)
  8. MQ_08 Enter Sepulchre
  9. MQ_09 Claim Reliquary (defeat The Keeper)
- 7 side quests + dynamic NPC quest generation (2-3 per town)

### The Sepulchre (Endgame)
- 9-floor final dungeon with atmospheric entry messages per depth
- **Floor 3: Bone Colossus** (120 HP, 18 STR, 6 armor, charger AI)
- **Floor 6: Ember Wyrm** (160 HP, 22 STR, 5 armor, dragon AI, ranged fire)
- **Floor 9: The Keeper** (250 HP, 28 STR, 8 armor, 3-phase boss)
  - Phase 1: full armor, standard attacks + ranged
  - Phase 2 (50% HP): sheds armor, faster, harder hits
  - Phase 3 (25% HP): adjacency aura (2 dmg/turn if next to player)
- Boss rooms with environmental hazards

### UI (20 screens)
- Main menu, creation screen (single-screen builder), build screen (god/traits/background), intro
- Inventory (paper doll, sort by type/rarity/value), character sheet, spellbook, passive tree (T key)
- Quest log, quest offer, shop, church (rank-up/prayers), dialogue
- World map, death screen (run stats), victory screen (god-flavored)
- Pause, settings, help, tutorial popups, floating text
- Dynamic panels with font-metric-based wrapping, no hardcoded caps
- Dungeon minimap (top-right), dynamic HUD, ability bar with cooldowns
- Gamepad support (Xbox/PS/Switch glyphs, full controller navigation)

### Audio
- 19 music tracks, 7 ambient loops, 24+ SFX
- Zone-specific ambient, weather-aware, day/night switching
- SFX on all class abilities, combat, spells, prayers, UI

### Technical
- SDL2/C++20/CMake, custom ECS, BSP dungeon generation
- Full save/load (JSON): all components including passive tree, diseases, buffs, floor cache
- F5/F6 quicksave/quickload, dungeon floor persistence
- Resolution scaling (1080p baseline, ultrawide/4K)
- Local builds for Steam (Linux + Windows), no CI
- Data integrity test suite (CTest)

---

## Architecture

```
src/
  core/          -- engine (11,201 lines), ecs, tilemap, spritesheet, audio, gamepad, input_glyphs, rng
  components/    -- 32 component headers (item.h 483, passive_tree.h 283, quest.h 281, tenet.h 251, etc.)
  data/          -- world_data.h (10 towns, 6 provinces, quest structures)
  systems/       -- combat, magic, ai, render, particles, god_system, npc_interaction, status, passive_tree, fov
  generation/    -- dungeon, populate, overworld, village, quest_gen, player_setup, mapfile
  ui/            -- 20 screen files + message_log, floating_text, ui_draw, ui_layout
  save/          -- save.cpp, meta.cpp
data/
  maps/          -- overworld.map (1000x750), thornwall.map
  dungeons.json  -- 24 dungeon entries with zone/quest/province/patron_god links
assets/
  32rogues/      -- spritesheets (rogues, monsters, animals, items, tiles, animated, input icons)
  fonts/         -- PrStart.ttf, Jacquard12-Regular.ttf
  sfx/           -- 24 sound effects
  music/         -- 19 tracks
  ambient/       -- 7 ambient loops
```

### Key Files
- **engine.cpp** (11,201 lines, 48 methods) -- core game loop, input, rendering, class ability logic, Keeper phases, beast form, NPC spawn. Grew from ~4200 post-refactor; needs splitting.
- **combat.cpp** (1,304 lines) -- melee/ranged damage, class on-hit abilities, unique item procs, status application
- **magic.cpp** (1,407 lines) -- all 56 spell implementations
- **overworld.cpp** (1,995 lines) -- overworld entity population
- **populate.cpp** (1,964 lines) -- dungeon monster/item/doodad spawning, depth scaling
- **creation_screen.cpp** (1,373 lines) -- class grid, description panels, build flow
- **god_system.cpp** (949 lines) -- prayers, tenets, favor, god auras
- **passive_tree.cpp** (911 lines) -- tree structure, allocation, effect application, class amplifiers
- **ai.cpp** (746 lines) -- all 12 monster behavior types
- **save.cpp** (773 lines) -- full JSON serialization of all components
- **world_data.h** (175 lines) -- canonical town/province data, single source of truth

---

## Completed Tiers (collapsed)

All tiers 1-6 are complete. See git history for details.

- **Tier 1**: God prayers, ranged combat, atmospheric messages, examine mode, status effects, Sepulchre content, ending screens
- **Tier 2**: Cursed/blessed items, spell failure, blood magic, spellbooks, god-aware NPCs, monster abilities
- **Tier 3**: SFX, death screens, bestiary, lore items, ambient text, particles
- **Tier 4**: Diseases, pets, rival paragons, dynamic quests, unlockable classes, meta-progression, hardcore mode
- **Tier 5**: Tenet system, sacred/profane items, god shrines, excommunication, NPC god factions, god relics, material system, 56 spells, 8 status effects
- **Tier 6**: Passive tree (260+ nodes, keystones, capstones, class amplifiers), use-based skills, monster AI overhaul (12 types), traps/hazards, balance pass, spell acquisition rework

### Post-Tier Work (Apr 23 - May 6)

- **Loop tightening**: quest chain 17 to 9, overworld 2000x1500 to 1000x750, 20 towns to 10, 27 dungeons to 24. Max dungeon depth 4 (except Sepulchre 9). Steeper scaling curves.
- **VERB-based class redesign**: every class now has a verb that changes gameplay. Gear interactions, level 5 abilities (all 10 remaining implemented), exponential scaling synergies.
- **Full class overhaul**: 24 total classes (was 17). Monster-themed classes added (Wyrmkin, Revenant, Serpentine, Trollblood). Gear interactions for all. AD&D PHB-style descriptions.
- **Single-screen character builder**: class grid + description panel, then build screen with god/traits/background together.
- **The Sepulchre**: 9-floor endgame dungeon. Bone Colossus (floor 3), Ember Wyrm (floor 6), The Keeper (floor 9, 3-phase boss).
- **Boss rooms**: environmental hazards in boss encounters.
- **Dense dungeon doodads**: guaranteed minimums per zone, zone-specific clutter.
- **Legendary item overhaul**: glowing icons, world secrets, guardians.
- **Status visuals**: enemy tint by active status effect.
- **Passive tree class amplifiers**: 8 verb-specific boost nodes.
- **UI overhaul**: dynamic panel sizing with font-metric wrapping, 3x bigger panels, description tracking cursor, paired build layout.
- **10+ systematic bug fix scans** plus tester-reported fixes.
- **Dialogue screen** refactored. **Church screen** added.
- **Overworld variety**: more wandering NPCs, merchants, world-building entities.
- **SFX added** to all class abilities that were silent.

### Polish Pass (May 6-7)

**UI/UX**
- **World map overhaul**: all 24 dungeon markers (was 6), legend bar (Town/Dungeon/You/Quest), brighter province labels, pulsing green quest objective marker. Zoom (mouse wheel, +/-, LB/RB) from 1x-4x, pan (arrows/WASD/stick), dungeon names appear at 2x+ zoom. Esc/M/Q to close.
- **Pause menu**: added "Save & Quit" option. "Exit to Menu" now requires Y/N confirmation.
- **Fade transitions**: fade-out entering character creation, fade-in after intro cinematic, black frame before dungeon floor generation.
- **Quest log direction hints**: active main quests show target location + compass direction from player (e.g. "Stonekeep (northeast)"). Uses overworld_return position when in a dungeon.
- **Examine popup panel**: hovering cursor over a monster in look mode shows info panel with name, HP/Dmg/Arm, STR/DEX/CON/WIL, active status effects, and behavior hint (e.g. "Breathes fire.", "Phases through walls."). Auto-positions to avoid screen edges.
- **HUD status abbreviation**: when 5+ status effects active, tags auto-shorten to 3-char codes (PSN/BRN/BLD/FRZ/STN/CNF/BLN/FER).
- **Shop owned count**: buy tab shows "own:N" next to items the player already carries.
- **Side panel text overlap fix**: build panel and god panel now use TTF_RenderText_Blended_Wrapped for actual pixel-height measurement instead of character-count estimate.

**Balance**
- **Rogue Vanish cooldown**: 3-turn cooldown after Vanish triggers. Prevents infinite kill-chain loop. `vanish_cooldown` field on Player component, ticked per turn.
- **Rest heals partial HP**: overworld 100%, depth 1: 80%, depth 2: 70%, depth 3: 60%, depth 4: 50%. Lethis passive still grants 100%. MP always restores fully.
- **Softer early depth scaling**: depths 1-2 use 0.35x HP / 0.25x damage per floor (was 0.5x/0.35x). Depths 3+ unchanged. Gentler on-ramp, same late difficulty.

**Content**
- **Fragment guardian bosses**: each fragment dungeon now has a boss on the bottom floor.
  - Stonekeep: The Warden (65 HP, basic AI). "A figure in corroded armor blocks the way."
  - Catacombs: The Revenant King (80 HP, necromancer AI). "The dead king rises from its throne."
  - Molten Depths: The Slag Mother (100 HP, dragon AI, fire breath). "Molten stone heaves upward."
- **Sepulchre floor 7-8 narrative**: floor 7 ("The walls are breathing."), floor 8 ("You can hear it. Below you. Waiting." + "One floor remains."). Joins mini-boss beats at floors 3/6 for full 9-floor arc.
- **35 zone-specific dungeon room templates** (5 per zone, 7 zones) replacing 6 generic clusters. Named room concepts: Nest, Den, Fungal Grove (warrens); Armory, Guard Post, Mess Hall (stonekeep); Crypt, Ossuary, Embalming Room (catacombs); Forge, Mine Cart, Slag Heap (molten); Tide Pool, Coral Cluster, Ruin (sunken); Collapsed Pillar, Boulder Field, Crystal Cave (deep halls); Ancient Tomb, Bone Throne, Sacrificial Altar (sepulchre). 50% chance per room >= 7x7.

**SFX**
- Monster special abilities now have audio: death knight fear (CURSE), naga stun (SPELL_IMPACT), wraith confusion (CURSE), ice breath (SPELL_FREEZE), basilisk blind (CURSE), dragon fire (SPELL_FIRE), lich drain (SPELL_IMPACT).

**Onboarding (16 tutorial popups)**
- New: "Welcome" (after intro, shows movement/inventory/quest/map/help keybinds), "Resting" (first rest, explains limited rests and depth scaling), "Consumables" (first potion pickup, explains use and identification), "Danger" (first time HP drops below 30%, suggests rest/potions/retreat).
- Updated dungeon entry tip text to reflect partial rest healing.
- Pre-existing: first combat, level up, spell, dungeon entry, trap, sneak, shrine, skill level, NPC, prayer, shop, church.

**Bug Fixes**
- Fixed duplicate AI component crash on fragment guardian bosses (spawn_boss already adds AI; was calling world.add<AI> again which asserts in debug).
- Added StatusEffects component to all three fragment guardians (without it, they couldn't be poisoned/burned/etc).
- Fixed quest text: "The Molten Depths east of Ironhearth" -> "beneath Ironhearth" (both at 700,375).
- Fixed stale town names in overworld.cpp lair comments (Tanglewood, Ashford, Sandmoor -> actual nearby locations).
- Fixed rest message "Fully restored" -> shows actual heal amount since rest is now partial.
- Fixed quest log compass direction using dungeon-local coordinates when underground (now uses overworld_return_x/y).

---

## What's Not Built / Known Issues

### Open Issues
- **engine.cpp is 11,201 lines.** Keeper phase logic, class ability processing, beast form state, NPC spawning all live in there. Should be split back out.
- **Steam SetLive broken.** Deleted macOS depot 4627803 still referenced by packages. Remove from packages on Steamworks to fix auto-set-live.
- **9 sprites still needed** (bookshelf, weapon rack, well, fountain, market stall, fence, grave marker, lantern post, cooking pot). See SPRITES_NEEDED.md.
- **test_data segfault.** Pre-existing. Data integrity test crashes on startup. Not related to any recent changes.

### Unbuilt Features (priority order)

**Content**
- [ ] God-specific church questlines (6 provinces, unique quest chains)
- [ ] More side quest variety (escort, defend, timed, multi-step, bounty board)
- [ ] Living world systems (NPC schedules, town events, merchant caravans)
- [ ] Fast travel (between visited towns)

**Polish**
- [ ] Colorblind mode (letter/symbol inside HUD status tags)
- [ ] More UI scale options (currently 100%/125%/150%)

**Technical**
- [ ] engine.cpp split (extract Keeper logic, class abilities, beast form, NPC spawn into system files)

### Removed from Scope
- ~~Custom class creation~~
- ~~Hunger clock~~
- ~~Thief steal-item-on-hit~~ (bandit is hit-and-run only)
- ~~Crumbling floor, spreading fire~~
- ~~CI/GitHub Actions~~ (removed Apr 19)
- ~~More passive tree nodes~~ (260+ is enough)

---

## Keybinds

Movement: arrows/WASD/hjkl/numpad (cardinal only) | Wait: ./numpad5
Actions: g/, pickup | Enter/>/< stairs | r rest | f fire ranged | p pray | x examine | ? help
Screens: i inventory | c character | z spells | q quests | M world map | T passive tree | Tab bestiary | Esc pause
Abilities: 1-4 (capstone actives)
F5 quicksave | F6 quickload | F11 fullscreen | F12 screenshot
