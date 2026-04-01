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

W = 2000
H = 1500
random.seed(42)

def climate(y):
    if y < 250: return 'ice'
    if y < 400: return 'cold'
    if y < 1100: return 'temperate'
    if y < 1250: return 'warm'
    return 'desert'

def base_tile(y):
    c = climate(y)
    if c == 'ice': return 'i'
    if c == 'cold': return '.' if random.random() < 0.6 else 'i'
    if c == 'temperate': return '.'
    if c == 'warm': return '.' if random.random() < 0.5 else 's'
    return 's'

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
        if grid[y][x] not in '.is': continue
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
    fy = random.randint(100, 1200)
    fr = random.randint(12, 45)
    density = random.uniform(0.15, 0.4)
    for dy in range(-fr, fr + 1):
        for dx in range(-fr, fr + 1):
            dist = math.sqrt(dx * dx + dy * dy)
            if dist > fr: continue
            nx, ny = fx + dx, fy + dy
            if not (0 <= nx < W and 0 <= ny < H): continue
            falloff = 1.0 - dist / fr
            if random.random() < density * falloff:
                grid[ny][nx] = 'T' if random.random() < 0.35 else 't'

# === ROCKY AREAS ===
print("Creating rocky areas...", flush=True)
for _ in range(100):
    rx, ry = random.randint(30, W - 30), random.randint(30, H - 30)
    rr = random.randint(6, 20)
    for dy in range(-rr, rr + 1):
        for dx in range(-rr, rr + 1):
            if dx * dx + dy * dy > rr * rr: continue
            nx, ny = rx + dx, ry + dy
            if not (0 <= nx < W and 0 <= ny < H): continue
            r = random.random()
            if r < 0.25: set_tile(nx, ny, 'R')
            elif r < 0.5: set_tile(nx, ny, ',')
            else: set_tile(nx, ny, '.')

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
for lx, ly, lrx, lry in [(500, 400, 30, 20), (1200, 350, 25, 18),
                            (900, 900, 35, 22), (1600, 600, 28, 18),
                            (350, 800, 22, 16), (1400, 1000, 30, 20),
                            (750, 1300, 25, 18)]:
    for dy in range(-lry, lry + 1):
        for dx in range(-lrx, lrx + 1):
            dist = (dx / lrx) ** 2 + (dy / lry) ** 2
            if dist < 0.85:
                set_tile(lx + dx, ly + dy, '~')

# === TOWNS (20) ===
print("Placing towns...", flush=True)
CX, CY = W // 2, H // 2  # 1000, 750

towns = [
    (CX, CY, "Thornwall", True),
    (CX - 250, CY - 100, "Ashford", False),
    (CX + 300, CY - 80, "Greywatch", False),
    (CX - 150, CY + 200, "Millhaven", False),
    (CX + 200, CY + 180, "Stonehollow", False),
    (CX + 50, CY - 300, "Frostmere", False),
    (CX - 350, CY + 50, "Bramblewood", False),
    (CX + 400, CY, "Ironhearth", False),
    (CX, CY + 350, "Dustfall", False),
    (CX - 200, CY - 350, "Whitepeak", False),
    (CX + 250, CY + 350, "Drywell", False),
    (CX - 450, CY - 200, "Hollowgate", False),
    (CX + 450, CY - 250, "Candlemere", False),
    (CX - 100, CY + 450, "Sandmoor", False),
    (CX + 100, CY - 450, "Glacierveil", False),
    (CX - 300, CY + 300, "Tanglewood", False),
    (CX + 350, CY + 250, "Redrock", False),
    (CX + 150, CY - 200, "Ravenshold", False),
    (CX - 400, CY - 50, "Fenwatch", False),
    (CX + 500, CY + 100, "Endgate", False),
]

def place_town(tx, ty, is_start, town_rng):
    """Place a town with structured grid layout and climate-appropriate materials."""
    lat = ty / H
    stone_prob = max(0, min(1, 1.0 - lat * 1.2))
    wall_ch = '#' if town_rng.random() < stone_prob else 'w'

    # Town size varies slightly
    size = town_rng.choice(['small', 'medium', 'large'])
    if size == 'small':
        half_w, half_h = 12, 10
        building_slots = [(-9, -7, 6, 5), (3, -7, 6, 5), (-9, 3, 6, 5), (3, 3, 6, 5)]
    elif size == 'medium':
        half_w, half_h = 16, 12
        building_slots = [(-13, -9, 7, 5), (-4, -9, 7, 5), (5, -9, 7, 5),
                          (-13, 4, 7, 5), (5, 4, 7, 5)]
    else:
        half_w, half_h = 20, 14
        building_slots = [(-17, -11, 7, 5), (-8, -11, 7, 5), (3, -11, 7, 5),
                          (-17, -4, 7, 5), (10, -4, 7, 5),
                          (-17, 5, 7, 5), (-4, 5, 7, 5), (10, 5, 7, 5)]

    # Clear rectangular ground
    for dy in range(-half_h - 2, half_h + 3):
        for dx in range(-half_w - 2, half_w + 3):
            set_tile(tx + dx, ty + dy, '.')

    # Main road — east-west through center, 2 tiles wide
    for dx in range(-half_w - 4, half_w + 5):
        set_tile(tx + dx, ty, ',')
        set_tile(tx + dx, ty + 1, ',')

    # Cross road — north-south, offset slightly based on seed
    cross_x = town_rng.choice([-2, 0, 2])
    for dy in range(-half_h - 4, half_h + 5):
        set_tile(tx + cross_x, ty + dy, ',')
        set_tile(tx + cross_x + 1, ty + dy, ',')

    # Place buildings on grid slots
    npcs = ['S', 'B', 'P', 'G', 'F']
    town_rng.shuffle(npcs)

    # Slight randomization of slot positions
    for idx, (bx, by, bw, bh) in enumerate(building_slots):
        bx += town_rng.randint(-1, 1)
        by += town_rng.randint(-1, 1)
        ax, ay = tx + bx, ty + by

        # Verify in bounds and not on road
        if not (2 < ax < W - 2 and 2 < ay < H - 2): continue

        # Walls
        fill_rect(ax, ay, ax + bw, ay + bh, wall_ch)
        # Floor
        fill_rect(ax + 1, ay + 1, ax + bw - 1, ay + bh - 1, ':')
        # Door — always face the nearest road
        if by < 0:  # building north of road
            set_tile(ax + bw // 2, ay + bh - 1, '+')  # door on south wall
        else:  # building south of road
            set_tile(ax + bw // 2, ay, '+')  # door on north wall

        # NPC inside
        if idx < len(npcs):
            set_tile(ax + bw // 2, ay + bh // 2, npcs[idx])

for i, (tx, ty, name, is_start) in enumerate(towns):
    if 25 < tx < W - 25 and 25 < ty < H - 25:
        place_town(tx, ty, is_start, random.Random(42 + i * 7))

# === NAMED QUEST DUNGEONS + GENERIC DUNGEONS ===
print("Placing dungeons...", flush=True)

# Named quest-linked dungeons: (x, y, name, zone, quest_id)
named_dungeons = [
    (CX + 60,   CY,      "The Barrow",        "warrens",      "MQ_01"),
    (CX - 200,  CY - 100, "Ashford Ruins",    "warrens",      "MQ_03"),  # near Ashford (CX-250, CY-100)
    (CX + 200,  CY - 110, "Stonekeep",        "stonekeep",    "MQ_05"),  # near Ravenshold/Greywatch area
    (CX + 100,  CY - 250, "Frostmere Depths", "deep_halls",   "MQ_07"),  # near Frostmere (CX+50, CY-300)
    (CX - 150,  CY + 150, "The Catacombs",    "catacombs",    "MQ_08"),  # near Millhaven/Bramblewood
    (CX + 450,  CY + 50,  "The Molten Depths", "molten",      "MQ_11"),  # near Ironhearth (CX+400, CY)
    (CX + 500,  CY - 200, "The Sunken Halls", "sunken",       "MQ_13"),  # near Candlemere (CX+450, CY-250)
    (CX - 400,  CY - 200, "The Hollowgate",   "deep_halls",   "MQ_14"),  # near Hollowgate (CX-450, CY-200)
    (CX,        CY - 600, "The Sepulchre",    "sepulchre",    "MQ_15"),  # far north, final mega-dungeon
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
    for tx, ty, _, _ in towns:
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
    for tx, ty, tname, _ in towns:
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

# === OUTPUT DUNGEON REGISTRY ===
print("Writing dungeon registry...", flush=True)
registry = []
for dx_pos, dy_pos, name, zone, quest in all_dungeons:
    dx_pos = max(15, min(W - 15, dx_pos))
    dy_pos = max(15, min(H - 15, dy_pos))
    entry = {
        "name": name if name else f"Dungeon ({dx_pos}, {dy_pos})",
        "x": dx_pos,
        "y": dy_pos,
        "zone": zone,
        "quest": quest,
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
