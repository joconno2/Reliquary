# Sprites Needed

## Completed (in tiles.png)

| Row | Col | Name | Status |
|-----|-----|------|--------|
| 18 | 8 | Bed (top half / pillow and headboard) | DONE |
| 19 | 8 | Bed (bottom half / blanket and footboard) | DONE |
| 19 | 2 | Anvil | DONE |

## Still Needed

| Name | Used For | Priority |
|------|----------|----------|
| Bookshelf | Scholar/temple interior | medium |
| Weapon rack | Guard post, blacksmith display | low |
| Cooking pot / cauldron | Inn kitchen, witch huts | low |
| Well | Town center focal point | medium |
| Fountain | Bigger towns (Thornwall, Candlemere) | low |
| Market stall / cart | Outdoor markets | low |
| Fence / gate | Farm boundaries | low |
| Grave marker | Overworld cemeteries | low |
| Lantern post | Town streets at night | medium |

Row 24 cols 0-6 have placeholders that can be repurposed.

## No Sprite Needed

- Moon/sun HUD indicator: drawn procedurally
- Lantern posts: reusing brazier animation (SHEET_ANIMATED row 1)
- Forge: reusing brazier animation
- Day/night tint: screen-space overlay

## Notes

- rogues.png is 7x7 (all character slots cols 0-4/5 in use). Expanding the sheet needed for more NPC variety.
- tiles.png is 17x26. Rows 25+ have trees (26) and some content at row 25 cols 0-3.
