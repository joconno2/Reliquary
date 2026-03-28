#!/usr/bin/env python3
"""Generate the overworld map for Reliquary.

Run: python3 tools/generate_overworld.py
Output: data/maps/overworld.map

2000x1500 — dense, interesting, Daggerfall-style open world.
"""

import random
import math
import os

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
print("Placing vegetation...", flush=True)
for y in range(H):
    c = climate(y)
    for x in range(W):
        if grid[y][x] not in '.is': continue
        r = random.random()
        if c == 'temperate':
            if r < 0.06: grid[y][x] = 'T'
            elif r < 0.16: grid[y][x] = 't'
        elif c == 'cold':
            if r < 0.04: grid[y][x] = 'T'
            elif r < 0.10: grid[y][x] = 't'
        elif c == 'ice':
            if r < 0.01: grid[y][x] = 'T'
            elif r < 0.03: grid[y][x] = 't'
        elif c == 'warm':
            if r < 0.03: grid[y][x] = 'T'
            elif r < 0.08: grid[y][x] = 't'
        elif c == 'desert':
            if r < 0.005: grid[y][x] = 'R'
            elif r < 0.015: grid[y][x] = 't'

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

def place_town(tx, ty, is_start):
    for dy in range(-20, 21):
        for dx in range(-28, 29):
            dist = math.sqrt(dx * dx * 0.6 + dy * dy)
            if dist < 16: set_tile(tx + dx, ty + dy, '.')
            elif dist < 20 and random.random() < 0.5: set_tile(tx + dx, ty + dy, '.')

    for dx in range(-25, 26):
        set_tile(tx + dx, ty, ',')
        set_tile(tx + dx, ty + 1, ',')
    for dy in range(-16, 17):
        set_tile(tx, ty + dy, ',')
        set_tile(tx + 1, ty + dy, ',')

    buildings = [(-12, -8, 8, 6), (-1, -8, 8, 6), (10, -8, 8, 6),
                 (-12, 4, 8, 6), (10, 4, 8, 6)]
    npcs = ['S', 'B', 'P', 'G', 'F']
    for idx, (bx, by, bw, bh) in enumerate(buildings):
        ax, ay = tx + bx, ty + by
        fill_rect(ax, ay, ax + bw, ay + bh, '#')
        fill_rect(ax + 1, ay + 1, ax + bw - 1, ay + bh - 1, ':')
        door_y = ay + bh - 1 if by < 0 else ay
        set_tile(ax + bw // 2, door_y, '+')
        if idx < len(npcs):
            set_tile(ax + bw // 2, ay + bh // 2, npcs[idx])

    if is_start:
        # Elder near the player start (quest giver)
        set_tile(tx + 3, ty + 2, 'E')

for tx, ty, name, is_start in towns:
    if 25 < tx < W - 25 and 25 < ty < H - 25:
        place_town(tx, ty, is_start)

# === DUNGEONS (30) ===
print("Placing dungeons...", flush=True)
dungeon_spots = []
# Barrow near Thornwall (first quest dungeon)
barrow_x, barrow_y = CX + 60, CY
dungeon_spots.append((barrow_x, barrow_y))

# Others spread around
for i in range(29):
    angle = (i / 29) * 2 * math.pi
    dist = 120 + (i % 5) * 120
    dx = int(math.cos(angle) * dist * 1.3)
    dy = int(math.sin(angle) * dist)
    dungeon_spots.append((CX + dx, CY + dy))

for dx_pos, dy_pos in dungeon_spots:
    dx_pos = max(15, min(W - 15, dx_pos))
    dy_pos = max(15, min(H - 15, dy_pos))
    for dy in range(-3, 4):
        for dx in range(-3, 4):
            set_tile(dx_pos + dx, dy_pos + dy, '.')
    fill_rect(dx_pos - 2, dy_pos - 2, dx_pos + 3, dy_pos + 3, '#')
    fill_rect(dx_pos - 1, dy_pos - 1, dx_pos + 2, dy_pos + 2, ':')
    set_tile(dx_pos - 2, dy_pos, '+')
    set_tile(dx_pos, dy_pos, '>')

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
            if cur not in '~#:+>':
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

# Road from Thornwall to the barrow
draw_road(CX, CY, barrow_x, barrow_y)

# === RUINS ===
print("Placing ruins...", flush=True)
for _ in range(40):
    rx, ry = random.randint(50, W - 50), random.randint(50, H - 50)
    sz = random.randint(4, 7)
    for dy in range(-sz - 1, sz + 2):
        for dx in range(-sz - 1, sz + 2):
            if random.random() < 0.25: set_tile(rx + dx, ry + dy, 't')
    for dy in range(sz):
        for dx in range(sz):
            if dx == 0 or dx == sz - 1 or dy == 0 or dy == sz - 1:
                if random.random() < 0.6: set_tile(rx + dx, ry + dy, '#')
            else:
                set_tile(rx + dx, ry + dy, ':')

# === BORDER ===
for y in range(H):
    for x in range(2): grid[y][x] = 'T'; grid[y][W - 1 - x] = 'T'
for x in range(W):
    for y in range(2): grid[y][x] = 'T'; grid[H - 1 - y][x] = 'T'

# === PLAYER START (last, can't be overwritten) ===
set_tile(CX, CY, '@')

# === OUTPUT ===
print("Writing map...", flush=True)
out_path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                         'data', 'maps', 'overworld.map')
with open(out_path, 'w') as f:
    for row in grid:
        f.write(''.join(row) + '\n')

print(f"Done: {W}x{H} = {W * H:,} tiles")
print(f"Towns: {len(towns)}, Dungeons: {len(dungeon_spots)}")
