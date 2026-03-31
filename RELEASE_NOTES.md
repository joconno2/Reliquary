# Reliquary v0.1.0-alpha

Traditional turn-based roguelike. C++/SDL2. 32rogues tileset.

## Requirements

**Linux:** SDL2, SDL2_image, SDL2_mixer, SDL2_ttf (install via your package manager)
- Arch: `pacman -S sdl2 sdl2_image sdl2_mixer sdl2_ttf`
- Ubuntu/Debian: `apt install libsdl2-2.0-0 libsdl2-image-2.0-0 libsdl2-mixer-2.0-0 libsdl2-ttf-2.0-0`
- Fedora: `dnf install SDL2 SDL2_image SDL2_mixer SDL2_ttf`

**Windows:** All DLLs bundled. Run `reliquary.exe`.

## Running

**Linux:** Extract, then `./run.sh` or `./reliquary` from the extracted directory.

**Windows:** Extract zip, run `reliquary.exe`.

## Controls

| Key | Action |
|-----|--------|
| WASD / Arrows / hjkl / Numpad | Move |
| yubn / Numpad 7913 | Diagonal move |
| g or , | Pick up / Open container |
| i | Inventory |
| c | Character sheet |
| z | Spellbook |
| q | Quest log |
| p | Pray |
| f | Fire ranged weapon |
| r | Rest |
| x | Examine mode |
| M | World map |
| ? | Help |
| Esc | Pause menu |

## What's in this alpha

- 17 playable classes (4 base + 13 unlockable)
- 13 gods with unique passives, prayers, tenets, visual effects
- 50 spells across 6 schools
- 78 item types (weapons, armor, amulets, rings, staves, consumables)
- 8 material types with depth-scaled generation
- 8 legendary items in specific dungeon locations
- Material system (bone through adamantine)
- Tenet system (behavioral rules per god)
- Sacred/profane items
- God shrines in dungeons
- 8 status effects (poison, burn, bleed, frozen, stunned, confused, blind, feared)
- 22 traits with real gameplay effects
- Resistance system (fire, poison, bleed)
- Blood magic (Yashkhet)
- 27 dungeons with static world difficulty scaling
- Dungeon floor persistence
- 2000x1500 overworld with 20 towns, varied building materials
- World map rendered from terrain data
- Character creation preview with live stats
- Full audio (19 music tracks, 12 ambient loops, 24 SFX)

## Known Issues

- Summoned creatures (Beast Call, Raise Dead, Swarm) attack the nearest entity, which may include other summons
- Some buff spells (Harden Skin, Hasten, etc.) are permanent rather than temporary
- Save system does not persist spell buffs or summons
- Overworld is large — navigation takes time

## Reporting Bugs

Open an issue at https://github.com/joconno2/Reliquary/issues
