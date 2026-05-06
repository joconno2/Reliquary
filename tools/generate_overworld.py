#!/usr/bin/env python3
"""Generate the overworld map for Reliquary.

Run: python3 tools/generate_overworld.py
Output: data/maps/overworld.map
        data/dungeons.json

2000x1500 — dense, interesting, Daggerfall-style open world.
Named dungeons placed near quest towns + generic exploration dungeons.
"""

import random
import math
import os
import json

W = 1000
H = 750
random.seed(42)

def climate(y):
    if y < 125: return 'ice'
    if y < 200: return 'cold'
    if y < 550: return 'temperate'
    if y < 625: return 'warm'
    return 'desert'

def base_tile(y):
    c = climate(y)
    if c == 'ice': return 'I'       # snow ground
    if c == 'cold': return 'I' if random.random() < 0.7 else '.'
    if c == 'temperate': return '.'
    if c == 'warm': return '.' if random.random() < 0.5 else 's'
    return 's'                       # sand ground

print("Initializing map...", flush=True)
grid = [[base_tile(y) for x in range(W)] for y in range(H)]

def set_tile(x, y, ch):
    if 0 <= x < W and 0 <= y < H:
        grid[y][x] = ch

def get_tile(x, y):
    if 0 <= x < W and 0 <= y < H:
        return grid[y][x]
    return 'T'

def fill_rect(x1, y1, x2, y2, ch):
    for y in range(max(0, y1), min(H, y2)):
        for x in range(max(0, x1), min(W, x2)):
            grid[y][x] = ch

# === SPARSE TREES + BRUSH ===
# Brush types: 't' = small bush, 'b' = tall grass, 'c' = flowers (mapped in engine)
print("Placing vegetation...", flush=True)
brush_types = ['t', 'b', 'c']
for y in range(H):
    c = climate(y)
    for x in range(W):
        if grid[y][x] not in '.isI': continue
        r = random.random()
        brush = random.choice(brush_types)
        if c == 'temperate':
            if r < 0.05: grid[y][x] = 'T'
            elif r < 0.09: grid[y][x] = brush
        elif c == 'cold':
            if r < 0.03: grid[y][x] = 'T'
            elif r < 0.06: grid[y][x] = brush
        elif c == 'ice':
            if r < 0.01: grid[y][x] = 'T'
            elif r < 0.02: grid[y][x] = 'b'  # only tall grass in ice
        elif c == 'warm':
            if r < 0.03: grid[y][x] = 'T'
            elif r < 0.06: grid[y][x] = brush
        elif c == 'desert':
            if r < 0.005: grid[y][x] = 'R'
            elif r < 0.012: grid[y][x] = 'b'

# === FOREST PATCHES (navigable, not walls) ===
print("Creating forests...", flush=True)
for _ in range(250):
    fx = random.randint(30, W - 30)
    fy = random.randint(300, 1200)  # no forests in ice zone (y < 250)
    fr = random.randint(12, 45)
    # Smaller, sparser forests in cold zone
    c = climate(fy)
    if c == 'cold':
        fr = min(fr, 20)
        density = random.uniform(0.08, 0.2)
    else:
        density = random.uniform(0.15, 0.4)
    for dy in range(-fr, fr + 1):
        for dx in range(-fr, fr + 1):
            dist = math.sqrt(dx * dx + dy * dy)
            if dist > fr: continue
            nx, ny = fx + dx, fy + dy
            if not (0 <= nx < W and 0 <= ny < H): continue
            if climate(ny) == 'ice': continue  # no trees on ice
            falloff = 1.0 - dist / fr
            if random.random() < density * falloff:
                grid[ny][nx] = 'T' if random.random() < 0.35 else 't'

# === ROCKY AREAS ===
# Concentrated in mountain bands and desert, sparse elsewhere
print("Creating rocky areas...", flush=True)
# Mountain band at edges (avoid center where towns are)
for _ in range(20):
    rx = random.randint(30, W - 30)
    ry = random.randint(100, 250)  # northern mountains only
    # Skip near any town (within 60 tiles)
    near_town = False
    for t in [(500, 375)]:  # avoid center (Thornwall area)
        if abs(rx - t[0]) < 60 and abs(ry - t[1]) < 60:
            near_town = True; break
    if near_town: continue
    rr = random.randint(5, 15)
    for dy in range(-rr, rr + 1):
        for dx in range(-rr, rr + 1):
            if dx * dx + dy * dy > rr * rr: continue
            nx, ny = rx + dx, ry + dy
            if not (0 <= nx < W and 0 <= ny < H): continue
            r = random.random()
            if r < 0.25: set_tile(nx, ny, 'R')
            elif r < 0.4: set_tile(nx, ny, ',')
# Desert rocky outcrops
for _ in range(30):
    rx = random.randint(30, W - 30)
    ry = random.randint(int(H * 0.83), H - 30)
    rr = random.randint(5, 15)
    for dy in range(-rr, rr + 1):
        for dx in range(-rr, rr + 1):
            if dx * dx + dy * dy > rr * rr: continue
            nx, ny = rx + dx, ry + dy
            if not (0 <= nx < W and 0 <= ny < H): continue
            r = random.random()
            if r < 0.2: set_tile(nx, ny, 'R')
# Scattered small rocky patches in south (fewer, smaller)
for _ in range(10):
    rx = random.randint(30, W - 30)
    ry = random.randint(500, H - 30)
    near_town = False
    for t in [(500, 375)]:  # avoid center (Thornwall area)
        if abs(rx - t[0]) < 50 and abs(ry - t[1]) < 50:
            near_town = True; break
    if near_town: continue
    rr = random.randint(3, 7)
    for dy in range(-rr, rr + 1):
        for dx in range(-rr, rr + 1):
            if dx * dx + dy * dy > rr * rr: continue
            nx, ny = rx + dx, ry + dy
            if not (0 <= nx < W and 0 <= ny < H): continue
            if random.random() < 0.12: set_tile(nx, ny, 'R')

# === RIVERS ===
print("Drawing rivers...", flush=True)
for start_x in [400, 1000, 1600]:
    x = start_x
    for y in range(5, H - 5):
        x += random.choice([-1, -1, 0, 0, 0, 1, 1])
        x = max(10, min(W - 10, x))
        for dx in range(-2, 3):
            set_tile(x + dx, y, '~')
        for dx in [-3, 3]:
            if random.random() < 0.5:
                cur = get_tile(x + dx, y)
                if cur not in '~#:+':
                    set_tile(x + dx, y, '.')

# === LARGE LAKES ===
print("Creating lakes...", flush=True)
for lx, ly, lrx, lry in [(250, 200, 15, 10), (600, 175, 12, 9),
                            (450, 450, 18, 11), (800, 300, 14, 9),
                            (175, 400, 11, 8)]:
    for dy in range(-lry, lry + 1):
        for dx in range(-lrx, lrx + 1):
            dist = (dx / lrx) ** 2 + (dy / lry) ** 2
            if dist < 0.85:
                set_tile(lx + dx, ly + dy, '~')

# === PROVINCES & TOWNS ===
print("Placing towns...", flush=True)
CX, CY = W // 2, H // 2  # 1000, 750

# 6 provinces, each with a patron god and a capital city
# Province boundaries are approximate — based on position relative to center
#   god_idx: maps to GodId enum (0=Vethrik..12=Sythara). Used for wall tinting.
PROVINCES = [
    {"name": "The Pale Reach",   "god": "Soleth",   "god_idx": 5,  "region": "north"},      # north-central: fire/purification
    {"name": "The Frozen Marches","god": "Gathruun", "god_idx": 11, "region": "far_north"},   # far north: stone/earth
    {"name": "The Heartlands",   "god": "Morreth",  "god_idx": 2,  "region": "center"},      # center: war/iron (neutral)
    {"name": "The Greenwood",    "god": "Khael",    "god_idx": 4,  "region": "west"},         # west: nature/beasts
    {"name": "The Iron Coast",   "god": "Ossren",   "god_idx": 9,  "region": "east"},         # east: craft/forge
    {"name": "The Dust Provinces","god": "Sythara",  "god_idx": 12, "region": "south"},        # south: plague/decay
]

def get_province(x, y):
    """Return province index (0-5) based on world position."""
    dx, dy = x - CX, y - CY
    if dy < -350: return 1  # far north = Frozen Marches
    if dy < -100: return 0  # north = Pale Reach
    if dy > 250:  return 5  # south = Dust Provinces
    if dx < -200: return 3  # west = Greenwood
    if dx > 200:  return 4  # east = Iron Coast
    return 2                # center = Heartlands

# God colors for wall tinting (R, G, B) — matches god.h GodColor
GOD_COLORS = {
    0: (160,160,200), 1: (140,140,220), 2: (200,180,140), 3: (200,60,60),
    4: (80,200,80),   5: (255,220,100), 6: (180,100,255), 7: (60,60,100),
    8: (80,180,200),  9: (220,180,80), 10: (160,120,200), 11: (160,130,90),
    12: (120,180,60),
}

# Towns: (x, y, name, is_start, is_city, province_idx)
# Each province has 1 city (capital) + 2-3 towns
towns = [
    # === The Heartlands (province 2, Morreth) ===
    (CX,        CY,      "Thornwall",    True,  True, 2),   # Capital — player start
    (CX - 75,   CY + 100,"Millhaven",    False, False, 2),
    # === The Pale Reach (province 0, Soleth) ===
    (CX + 225,  CY - 125,"Candlemere",   False, True, 0),   # Capital
    (CX + 25,   CY - 150,"Frostmere",    False, False, 0),
    (CX + 150,  CY - 40, "Greywatch",    False, False, 0),
    # === The Frozen Marches (province 1, Gathruun) ===
    (CX - 100,  CY - 175,"Whitepeak",    False, True, 1),
    # === The Greenwood (province 3, Khael) ===
    (CX - 175,  CY + 25, "Bramblewood",  False, True, 3),   # Capital
    (CX - 225,  CY - 100,"Hollowgate",   False, False, 3),
    # === The Iron Coast (province 4, Ossren) ===
    (CX + 200,  CY,      "Ironhearth",   False, True, 4),   # Capital
    # === The Dust Provinces (province 5, Sythara) ===
    (CX,        CY + 175,"Dustfall",      False, True, 5),   # Capital
]

def place_building(ax, ay, bw, bh, wall_ch, door_south=True, npc=None):
    """Place a single building with walls, floor, door, optional NPC."""
    if not (2 < ax < W - 2 and 2 < ay < H - 2): return
    fill_rect(ax, ay, ax + bw, ay + bh, wall_ch)
    fill_rect(ax + 1, ay + 1, ax + bw - 1, ay + bh - 1, ':')
    if door_south:
        set_tile(ax + bw // 2, ay + bh - 1, '+')
    else:
        set_tile(ax + bw // 2, ay, '+')
    if npc:
        set_tile(ax + bw // 2, ay + bh // 2, npc)

def place_town(tx, ty, is_start, town_rng, is_city=False, province_idx=2):
    """Place a town with province-specific layout and visual identity."""
    PROVINCE_WALLS = {
        0: '#',  # Pale Reach (Soleth) — clean white stone
        1: 'L',  # Frozen Marches (Gathruun) — rough stone blocks
        2: '#',  # Heartlands (Morreth) — iron-reinforced brick
        3: 'w',  # Greenwood (Khael) — wood walls
        4: 'L',  # Iron Coast (Ossren) — hewn stone
        5: 'n',  # Dust Provinces (Sythara) — sandstone
    }
    PROVINCE_GROUND = {
        0: ':',  # Pale Reach — clean stone
        1: ':',  # Frozen Marches — stone
        2: ':',  # Heartlands — cobblestone
        3: '.',  # Greenwood — dirt
        4: ':',  # Iron Coast — stone
        5: ',',  # Dust Provinces — sand
    }
    wall_ch = PROVINCE_WALLS.get(province_idx, '#')
    ground_ch = PROVINCE_GROUND.get(province_idx, ':')

    # Province-specific town layouts
    if province_idx == 2:  # Heartlands (Morreth) — military/organized
        half_w, half_h = (24, 18) if is_city else (16, 12)
        # Clear ground
        for dy in range(-half_h - 2, half_h + 3):
            for dx in range(-half_w - 2, half_w + 3):
                set_tile(tx + dx, ty + dy, ground_ch)
        # Outer wall for cities
        if is_city:
            for dx in range(-half_w - 1, half_w + 2):
                set_tile(tx + dx, ty - half_h - 1, wall_ch)
                set_tile(tx + dx, ty + half_h + 1, wall_ch)
            for dy in range(-half_h - 1, half_h + 2):
                set_tile(tx - half_w - 1, ty + dy, wall_ch)
                set_tile(tx + half_w + 1, ty + dy, wall_ch)
            set_tile(tx, ty - half_h - 1, '+')
            set_tile(tx, ty + half_h + 1, '+')
            set_tile(tx - half_w - 1, ty, '+')
            set_tile(tx + half_w + 1, ty, '+')
        # Central square (compact)
        for dy in range(-2, 3):
            for dx in range(-3, 4):
                set_tile(tx + dx, ty + dy, ',')
        # Main road through center
        for dx in range(-half_w - 4, half_w + 5):
            set_tile(tx + dx, ty, ',')
            set_tile(tx + dx, ty + 1, ',')
        # City: 12 buildings. Town: 7. Each NPC has a dedicated building.
        # M=Merchant, H=Herbalist, W=Villager/Inn spot
        if is_city:
            npcs_and_slots = [
                # North row
                ('G', -16, -half_h + 2, 8, 5, False),   # guard barracks
                ('S', -5, -half_h + 2, 8, 5, False),    # general store
                ('B', 6, -half_h + 2, 8, 5, False),     # smithy
                # South row
                ('M', -16, half_h - 6, 8, 5, True),     # merchant
                ('H', -5, half_h - 6, 8, 5, True),      # herbalist
                ('F', 6, half_h - 6, 8, 5, True),       # farmer house
                # West side
                ('P', -half_w + 2, -10, 7, 5, True),    # scholar
                ('N', -half_w + 2, 5, 7, 5, False),     # inn
                # East side
                ('G', half_w - 9, -10, 7, 5, True),     # guard post
                ('F', half_w - 9, 5, 7, 5, False),      # farmer
                # Inner (closer to center)
                ('W', 12, -6, 6, 5, True),               # villager
                ('S', -17, -6, 6, 5, True),              # second shop
            ]
        else:
            npcs_and_slots = [
                ('S', -13, -9, 7, 5, False),
                ('B', -3, -9, 7, 5, False),
                ('G', 7, -9, 6, 5, False),
                ('P', -13, 4, 7, 5, True),
                ('F', -3, 4, 7, 5, True),
                ('H', 7, 4, 6, 5, True),
                ('M', -8, -2, 6, 4, True),  # merchant near center
            ]
        for npc_ch, bx, by, bw, bh, ds in npcs_and_slots:
            place_building(tx + bx, ty + by, bw, bh, wall_ch, ds, npc_ch)

    elif province_idx == 0:  # Pale Reach (Soleth) — temple/symmetrical
        half_w, half_h = (22, 16) if is_city else (14, 11)
        for dy in range(-half_h - 2, half_h + 3):
            for dx in range(-half_w - 2, half_w + 3):
                set_tile(tx + dx, ty + dy, ground_ch)
        # Central temple (large, double-width building)
        temple_w, temple_h = 11, 7
        place_building(tx - temple_w // 2, ty - temple_h // 2, temple_w, temple_h, wall_ch, True, 'P')
        # Symmetrical approach road
        for dy in range(temple_h // 2, half_h + 4):
            set_tile(tx, ty + dy, ',')
            set_tile(tx + 1, ty + dy, ',')
        for dy in range(-half_h - 4, -temple_h // 2):
            set_tile(tx, ty + dy, ',')
            set_tile(tx + 1, ty + dy, ',')
        for dx in range(-half_w - 4, half_w + 5):
            set_tile(tx + dx, ty + half_h - 2, ',')
        # Flanking buildings (mirror left/right)
        npcs = ['S', 'G', 'B', 'F', 'S', 'G', 'F'] if is_city else ['S', 'G', 'B', 'F']
        town_rng.shuffle(npcs)
        left_slots = [(-half_w + 2, -8, 6, 5), (-half_w + 2, 4, 6, 5)]
        right_slots = [(half_w - 7, -8, 6, 5), (half_w - 7, 4, 6, 5)]
        if is_city:
            left_slots += [(-half_w + 10, -half_h + 2, 6, 5)]
            right_slots += [(half_w - 15, -half_h + 2, 6, 5)]
            left_slots += [(-half_w + 2, half_h - 6, 7, 5)]
        all_slots = []
        for s in left_slots: all_slots.append(s + (True,))
        for s in right_slots: all_slots.append(s + (True,))
        for i, (bx, by, bw, bh, ds) in enumerate(all_slots):
            npc = npcs[i] if i < len(npcs) else None
            place_building(tx + bx, ty + by, bw, bh, wall_ch, by >= 0, npc)

    elif province_idx == 1:  # Frozen Marches (Gathruun) — compact fortress
        half_w, half_h = (18, 14) if is_city else (12, 10)
        for dy in range(-half_h - 2, half_h + 3):
            for dx in range(-half_w - 2, half_w + 3):
                set_tile(tx + dx, ty + dy, ground_ch)
        # Thick double outer wall
        for dx in range(-half_w - 1, half_w + 2):
            set_tile(tx + dx, ty - half_h - 1, wall_ch)
            set_tile(tx + dx, ty - half_h, wall_ch)
            set_tile(tx + dx, ty + half_h, wall_ch)
            set_tile(tx + dx, ty + half_h + 1, wall_ch)
        for dy in range(-half_h - 1, half_h + 2):
            set_tile(tx - half_w - 1, ty + dy, wall_ch)
            set_tile(tx - half_w, ty + dy, wall_ch)
            set_tile(tx + half_w, ty + dy, wall_ch)
            set_tile(tx + half_w + 1, ty + dy, wall_ch)
        set_tile(tx, ty + half_h, '+')
        set_tile(tx, ty + half_h + 1, '+')
        # Single narrow road
        for dy in range(-half_h + 2, half_h + 4):
            set_tile(tx, ty + dy, ',')
        # Tight clustered buildings (small, packed)
        npcs = ['S', 'B', 'P', 'G', 'F'] if not is_city else ['S', 'B', 'P', 'G', 'F', 'S', 'G']
        town_rng.shuffle(npcs)
        slots = [
            (-half_w + 3, -half_h + 3, 6, 4, True),
            (half_w - 8, -half_h + 3, 6, 4, True),
            (-half_w + 3, 0, 6, 4, True),
            (half_w - 8, 0, 6, 4, True),
            (-half_w + 3, half_h - 6, 6, 4, False),
        ]
        if is_city:
            slots += [(half_w - 8, half_h - 6, 6, 4, False), (2, -half_h + 3, 6, 4, True)]
        for i, (bx, by, bw, bh, ds) in enumerate(slots):
            npc = npcs[i] if i < len(npcs) else None
            place_building(tx + bx, ty + by, bw, bh, wall_ch, ds, npc)

    elif province_idx == 3:  # Greenwood (Khael) — organic but structured
        half_w, half_h = (18, 14) if is_city else (14, 11)
        for dy in range(-half_h - 2, half_h + 3):
            for dx in range(-half_w - 2, half_w + 3):
                set_tile(tx + dx, ty + dy, '.')  # dirt everywhere
        # Gently curving main path
        for dx in range(-half_w - 3, half_w + 4):
            py_off = int(2.0 * math.sin(dx * 0.15))
            set_tile(tx + dx, ty + py_off, ',')
            set_tile(tx + dx, ty + py_off + 1, ',')
        # Small clearing at center
        for dy in range(-2, 3):
            for dx in range(-3, 4):
                set_tile(tx + dx, ty + dy, '.')
        # Buildings placed at fixed offsets (not random scatter)
        npcs = ['S', 'B', 'P', 'G', 'F'] if not is_city else ['S', 'B', 'P', 'G', 'F', 'S', 'F', 'G']
        town_rng.shuffle(npcs)
        slots = [
            (-half_w + 2, -half_h + 2, 6, 5, True),
            (half_w - 7, -half_h + 2, 6, 5, True),
            (-half_w + 2, 4, 6, 5, False),
            (half_w - 7, 4, 6, 5, False),
            (-4, -half_h + 2, 7, 5, True),
        ]
        if is_city:
            slots += [(4, 4, 6, 5, False), (-half_w + 10, -half_h + 2, 6, 5, True),
                       (-half_w + 2, half_h - 6, 6, 5, False)]
        for i, (bx, by, bw, bh, ds) in enumerate(slots):
            npc = npcs[i] if i < len(npcs) else None
            place_building(tx + bx, ty + by, bw, bh, wall_ch, ds, npc)

    elif province_idx == 4:  # Iron Coast (Ossren) — industrial
        half_w, half_h = (22, 16) if is_city else (15, 11)
        for dy in range(-half_h - 2, half_h + 3):
            for dx in range(-half_w - 2, half_w + 3):
                set_tile(tx + dx, ty + dy, ground_ch)
        # Wide main road
        for dx in range(-half_w - 4, half_w + 5):
            set_tile(tx + dx, ty, ',')
            set_tile(tx + dx, ty + 1, ',')
            set_tile(tx + dx, ty - 1, ',')
        # Large forge building (center-north, double size)
        forge_w, forge_h = 10, 6
        place_building(tx - forge_w // 2, ty - half_h + 2, forge_w, forge_h, wall_ch, True, 'B')
        # Warehouses (long, narrow)
        npcs = ['S', 'G', 'P', 'F', 'S', 'G', 'F'] if is_city else ['S', 'G', 'P', 'F']
        town_rng.shuffle(npcs)
        slots = [
            (-half_w + 2, 3, 9, 4, False),     # south warehouse
            (half_w - 10, 3, 9, 4, False),      # south warehouse
            (-half_w + 2, -half_h + 2, 7, 5, True),  # north left
            (half_w - 8, -half_h + 2, 7, 5, True),   # north right
        ]
        if is_city:
            slots += [(-half_w + 2, half_h - 6, 8, 5, False), (half_w - 9, half_h - 6, 8, 5, False)]
            slots += [(2, half_h - 6, 7, 5, False)]
        for i, (bx, by, bw, bh, ds) in enumerate(slots):
            npc = npcs[i] if i < len(npcs) else None
            place_building(tx + bx, ty + by, bw, bh, wall_ch, ds, npc)

    elif province_idx == 5:  # Dust Provinces (Sythara) — sparse, sand, intact buildings
        half_w, half_h = (16, 12) if is_city else (13, 10)
        for dy in range(-half_h - 2, half_h + 3):
            for dx in range(-half_w - 2, half_w + 3):
                set_tile(tx + dx, ty + dy, ',')  # sand everywhere
        # Straight road (sand-covered)
        for dx in range(-half_w - 3, half_w + 4):
            set_tile(tx + dx, ty, ',')
            set_tile(tx + dx, ty + 1, ',')
        # Buildings at fixed positions, wider spacing (sparse feel)
        npcs = ['S', 'P', 'G', 'F', 'B'] if not is_city else ['S', 'P', 'G', 'F', 'B', 'S', 'F']
        town_rng.shuffle(npcs)
        slots = [
            (-half_w + 2, -half_h + 2, 6, 5, True),
            (half_w - 7, -half_h + 2, 6, 5, True),
            (-half_w + 2, 4, 6, 5, False),
            (half_w - 7, 4, 6, 5, False),
            (-3, -half_h + 2, 6, 5, True),
        ]
        if is_city:
            slots += [(4, 4, 6, 5, False), (-half_w + 10, 4, 6, 5, False)]
        for i, (bx, by, bw, bh, ds) in enumerate(slots):
            npc = npcs[i] if i < len(npcs) else None
            place_building(tx + bx, ty + by, bw, bh, wall_ch, ds, npc)

    else:
        # Fallback: simple grid
        half_w, half_h = 14, 10
        for dy in range(-half_h - 2, half_h + 3):
            for dx in range(-half_w - 2, half_w + 3):
                set_tile(tx + dx, ty + dy, ground_ch)
        for dx in range(-half_w - 4, half_w + 5):
            set_tile(tx + dx, ty, ',')
            set_tile(tx + dx, ty + 1, ',')
        npcs = ['S', 'B', 'P', 'G', 'F']
        town_rng.shuffle(npcs)
        slots = [(-11, -7, 7, 5, False), (4, -7, 7, 5, False),
                 (-11, 3, 7, 5, True), (4, 3, 7, 5, True), (-3, -7, 6, 5, False)]
        for i, (bx, by, bw, bh, ds) in enumerate(slots):
            npc = npcs[i] if i < len(npcs) else None
            place_building(tx + bx, ty + by, bw, bh, wall_ch, ds, npc)

for i, (tx, ty, name, is_start, is_city, prov_idx) in enumerate(towns):
    if 25 < tx < W - 25 and 25 < ty < H - 25:
        place_town(tx, ty, is_start, random.Random(42 + i * 7), is_city, prov_idx)
        wall_ch = {0:'#', 1:'L', 2:'#', 3:'w', 4:'L', 5:'n'}.get(prov_idx, '#')
        if is_city:
            # Church building (prominent, east of center)
            place_building(tx + 8, ty - 6, 9, 6, wall_ch, True, 'C')
        # All towns get inn, merchant, herbalist buildings if not already placed by layout
        # Check which glyphs are already in the town area
        trng = random.Random(42 + i * 13)
        existing = set()
        for dy2 in range(-20, 21):
            for dx2 in range(-20, 21):
                gx, gy = tx + dx2, ty + dy2
                if 0 <= gx < W and 0 <= gy < H:
                    ch = grid[gy][gx]
                    if ch in 'NMBHC': existing.add(ch)
        # Add missing buildings along the south/east edges
        extra_y = 10 if is_city else 7
        extra_offset = 0
        for glyph, label_w, label_h in [('N', 7, 5), ('M', 7, 5), ('H', 6, 5)]:
            if glyph in existing: continue
            bx = -12 + extra_offset * 9
            by = extra_y
            place_building(tx + bx, ty + by, label_w, label_h, wall_ch, False, glyph)
            extra_offset += 1

# === NAMED QUEST DUNGEONS + GENERIC DUNGEONS ===
print("Placing dungeons...", flush=True)

# Named quest-linked dungeons: (x, y, name, zone, quest_id)
named_dungeons = [
    (CX + 60,   CY,      "The Barrow",        "warrens",      "MQ_01"),   # tutorial boss
    (CX + 150,  CY - 50, "Stonekeep",          "stonekeep",    "MQ_03"),   # first fragment
    (CX - 75,   CY + 100, "The Catacombs",     "catacombs",    "MQ_05"),   # second fragment
    (CX + 200,  CY,       "The Molten Depths",  "molten",      "MQ_06"),   # third fragment
    (CX - 225,  CY - 100, "The Hollowgate",    "deep_halls",   "MQ_07"),   # break seal
    (CX,        CY - 300, "The Sepulchre",     "sepulchre",    "MQ_09"),   # final dungeon
]

# Generic exploration dungeons spread across the map
generic_dungeons = []
generic_rng = random.Random(123)  # separate seed so named dungeon changes don't shift generics
placed_positions = [(d[0], d[1]) for d in named_dungeons]

def too_close(x, y, min_dist=80):
    for px, py in placed_positions:
        if abs(x - px) < min_dist and abs(y - py) < min_dist:
            return True
    # Also don't place on top of towns
    for tx, ty, _, _, _, _ in towns:
        if abs(x - tx) < 40 and abs(y - ty) < 40:
            return True
    return False

# Place 18 generic dungeons spread across the map
generic_zones = ["warrens", "stonekeep", "deep_halls", "catacombs", "molten", "sunken"]
for i in range(18):
    for _ in range(50):  # retry to find valid position
        gx = generic_rng.randint(60, W - 60)
        gy = generic_rng.randint(60, H - 60)
        if not too_close(gx, gy, 70):
            zone = generic_zones[i % len(generic_zones)]
            generic_dungeons.append((gx, gy, None, zone, None))
            placed_positions.append((gx, gy))
            break

all_dungeons = named_dungeons + generic_dungeons

def place_dungeon(dx_pos, dy_pos, is_sepulchre=False):
    dx_pos = max(15, min(W - 15, dx_pos))
    dy_pos = max(15, min(H - 15, dy_pos))
    if is_sepulchre:
        # Larger stone structure surrounded by ruins
        for dy in range(-8, 9):
            for dx in range(-8, 9):
                set_tile(dx_pos + dx, dy_pos + dy, '.')
        # Outer ruin ring
        for dy in range(-7, 8):
            for dx in range(-7, 8):
                dist = math.sqrt(dx * dx + dy * dy)
                if 5.5 < dist < 7.5 and random.random() < 0.5:
                    set_tile(dx_pos + dx, dy_pos + dy, '#')
        # Inner structure
        fill_rect(dx_pos - 4, dy_pos - 4, dx_pos + 5, dy_pos + 5, '#')
        fill_rect(dx_pos - 3, dy_pos - 3, dx_pos + 4, dy_pos + 4, ':')
        set_tile(dx_pos - 4, dy_pos, '+')
        set_tile(dx_pos, dy_pos, '>')
    else:
        for dy in range(-3, 4):
            for dx in range(-3, 4):
                set_tile(dx_pos + dx, dy_pos + dy, '.')
        fill_rect(dx_pos - 2, dy_pos - 2, dx_pos + 3, dy_pos + 3, '#')
        fill_rect(dx_pos - 1, dy_pos - 1, dx_pos + 2, dy_pos + 2, ':')
        set_tile(dx_pos - 2, dy_pos, '+')
        set_tile(dx_pos, dy_pos, '>')

for dx_pos, dy_pos, name, zone, quest in all_dungeons:
    is_sep = (name == "The Sepulchre")
    place_dungeon(dx_pos, dy_pos, is_sep)

# === ROADS ===
print("Drawing roads...", flush=True)
def draw_road(x1, y1, x2, y2):
    x, y = x1, y1
    while abs(x - x2) > 1 or abs(y - y2) > 1:
        dx = 1 if x2 > x else -1 if x2 < x else 0
        dy = 1 if y2 > y else -1 if y2 < y else 0
        if abs(x2 - x) > abs(y2 - y):
            x += dx if random.random() < 0.8 else 0
            if random.random() < 0.2: y += dy
        else:
            y += dy if random.random() < 0.8 else 0
            if random.random() < 0.2: x += dx
        for w in range(2):
            cur = get_tile(x, y + w)
            if cur not in '~#w:+>':
                set_tile(x, y + w, ',')

# Connect towns (MST)
town_coords = [(t[0], t[1]) for t in towns]
connected = {0}
for _ in range(len(town_coords)):
    best_d, best_f, best_t = float('inf'), -1, -1
    for i in connected:
        for j in range(len(town_coords)):
            if j in connected: continue
            d = math.hypot(town_coords[i][0] - town_coords[j][0],
                           town_coords[i][1] - town_coords[j][1])
            if d < best_d: best_d, best_f, best_t = d, i, j
    if best_t >= 0:
        draw_road(*town_coords[best_f], *town_coords[best_t])
        connected.add(best_t)

# Extra loop roads
for _ in range(8):
    a, b = random.sample(range(len(town_coords)), 2)
    draw_road(*town_coords[a], *town_coords[b])

# Roads from towns to their nearby quest dungeons
for dx_pos, dy_pos, name, zone, quest in named_dungeons:
    if name == "The Sepulchre":
        continue  # no road to the final dungeon — must find it
    # Find nearest town and draw road
    best_dist = float('inf')
    best_town = None
    for tx, ty, tname, *_ in towns:
        d = math.hypot(dx_pos - tx, dy_pos - ty)
        if d < best_dist:
            best_dist = d
            best_town = (tx, ty)
    if best_town:
        draw_road(best_town[0], best_town[1], dx_pos, dy_pos)

# === BRIDGES — where roads meet rivers ===
print("Building bridges...", flush=True)
# Scan for road tiles adjacent to water — replace the water crossing with stone floor (bridge)
for y in range(2, H - 2):
    for x in range(2, W - 2):
        if grid[y][x] != ',': continue  # only road tiles
        # Check all 4 cardinal directions for water
        for ddx, ddy in [(1, 0), (-1, 0), (0, 1), (0, -1)]:
            nx, ny = x + ddx, y + ddy
            if 0 <= nx < W and 0 <= ny < H and grid[ny][nx] == '~':
                # Found road adjacent to water — build a bridge across
                # Walk in that direction, replacing water with stone floor
                bx, by = nx, ny
                bridge_len = 0
                while 0 <= bx < W and 0 <= by < H and grid[by][bx] == '~' and bridge_len < 12:
                    grid[by][bx] = ':'  # stone floor = bridge
                    # Also widen the bridge by 1 tile perpendicular
                    if ddx != 0:  # horizontal crossing — widen vertically
                        if 0 <= by - 1 < H and grid[by - 1][bx] == '~': grid[by - 1][bx] = ':'
                    else:  # vertical crossing — widen horizontally
                        if 0 <= bx - 1 < W and grid[by][bx - 1] == '~': grid[by][bx - 1] = ':'
                    bx += ddx
                    by += ddy
                    bridge_len += 1

# === RUINS ===
print("Placing ruins...", flush=True)
# Don't place ruins near towns
town_positions = [(t[0], t[1]) for t in towns]
for _ in range(40):
    rx, ry = random.randint(50, W - 50), random.randint(50, H - 50)
    # Skip if near a town
    near_town = False
    for ttx, tty in town_positions:
        if abs(rx - ttx) < 40 and abs(ry - tty) < 40:
            near_town = True; break
    if near_town: continue
    sz = random.randint(4, 7)
    for dy in range(-sz - 1, sz + 2):
        for dx in range(-sz - 1, sz + 2):
            nx, ny = rx + dx, ry + dy
            if 0 <= nx < W and 0 <= ny < H and grid[ny][nx] in '.tbc':
                if random.random() < 0.25: set_tile(nx, ny, 't')
    for dy in range(sz):
        for dx in range(sz):
            nx, ny = rx + dx, ry + dy
            if not (0 <= nx < W and 0 <= ny < H): continue
            if grid[ny][nx] not in '.t,bc': continue  # don't overwrite walls/NPCs/water
            if dx == 0 or dx == sz - 1 or dy == 0 or dy == sz - 1:
                if random.random() < 0.6: set_tile(nx, ny, '#')
            else:
                set_tile(nx, ny, ':')

# === BORDER ===
for y in range(H):
    for x in range(2): grid[y][x] = 'T'; grid[y][W - 1 - x] = 'T'
for x in range(W):
    for y in range(2): grid[y][x] = 'T'; grid[H - 1 - y][x] = 'T'

# === PLAYER START + ELDER (last, can't be overwritten) ===
set_tile(CX, CY, '@')
set_tile(CX + 1, CY, 'E')  # Elder quest giver right next to spawn

# === OUTPUT MAP ===
print("Writing map...", flush=True)
base_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
out_path = os.path.join(base_dir, 'data', 'maps', 'overworld.map')
with open(out_path, 'w') as f:
    for row in grid:
        f.write(''.join(row) + '\n')

# === PROCEDURAL DUNGEON NAME GENERATOR ===
_dungeon_name_prefixes = {
    "warrens":    ["Rat", "Mud", "Root", "Burrow", "Worm", "Crawl"],
    "stonekeep":  ["Grey", "Iron", "Old", "Fallen", "Broken", "Silent"],
    "deep_halls": ["Deep", "Vast", "Sunless", "Echoing", "Ancient", "Hollow"],
    "catacombs":  ["Bone", "Dead", "Dust", "Grave", "Tomb", "Pale"],
    "molten":     ["Ember", "Slag", "Char", "Cinder", "Scorch", "Ash"],
    "sunken":     ["Drowned", "Tide", "Murk", "Flood", "Salt", "Damp"],
}
_dungeon_name_suffixes = {
    "warrens":    ["Warren", "Tunnels", "Burrows", "Holes", "Dens", "Crawlway"],
    "stonekeep":  ["Keep", "Hold", "Fortress", "Vault", "Bastion", "Citadel"],
    "deep_halls": ["Halls", "Galleries", "Chambers", "Expanse", "Underhall", "Caverns"],
    "catacombs":  ["Catacombs", "Ossuary", "Crypts", "Sepulcher", "Barrows", "Tombs"],
    "molten":     ["Forge", "Crucible", "Furnace", "Pit", "Core", "Depths"],
    "sunken":     ["Grotto", "Cistern", "Pools", "Reservoir", "Abyss", "Basin"],
}
_used_dungeon_names = set()

def generate_dungeon_name(zone, rng):
    """Generate a unique procedural name for a generic dungeon."""
    prefixes = _dungeon_name_prefixes.get(zone, ["Dark", "Lost", "Hidden"])
    suffixes = _dungeon_name_suffixes.get(zone, ["Dungeon", "Caves", "Ruins"])
    for _ in range(50):
        name = f"The {rng.choice(prefixes)} {rng.choice(suffixes)}"
        if name not in _used_dungeon_names:
            _used_dungeon_names.add(name)
            return name
    return f"The {prefixes[0]} {suffixes[0]}"  # fallback

# === OUTPUT DUNGEON REGISTRY ===
print("Writing dungeon registry...", flush=True)
name_rng = random.Random(42)  # deterministic naming
registry = []
for dx_pos, dy_pos, name, zone, quest in all_dungeons:
    dx_pos = max(15, min(W - 15, dx_pos))
    dy_pos = max(15, min(H - 15, dy_pos))
    prov = get_province(dx_pos, dy_pos)
    entry = {
        "name": name if name else generate_dungeon_name(zone, name_rng),
        "x": dx_pos,
        "y": dy_pos,
        "zone": zone,
        "quest": quest,
        "province": prov,
        "province_name": PROVINCES[prov]["name"],
        "patron_god": PROVINCES[prov]["god"],
        "patron_god_idx": PROVINCES[prov]["god_idx"],
    }
    registry.append(entry)

json_path = os.path.join(base_dir, 'data', 'dungeons.json')
with open(json_path, 'w') as f:
    json.dump(registry, f, indent=2)

print(f"Done: {W}x{H} = {W * H:,} tiles")
print(f"Towns: {len(towns)}, Named dungeons: {len(named_dungeons)}, "
      f"Generic dungeons: {len(generic_dungeons)}, Total dungeons: {len(all_dungeons)}")
print(f"Map: {out_path}")
print(f"Registry: {json_path}")
