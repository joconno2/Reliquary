#!/usr/bin/env python3
"""
Build a clean 16x16 input icon atlas from irregular gamepad/keyboard sprite sheets.

Source sheets (560x640, from gamepad-db):
  /tmp/gamepad_sprites/gdb-xbox-2.png
  /tmp/gamepad_sprites/gdb-playstation-2.png
  /tmp/gamepad_sprites/gdb-switch-2.png
  /tmp/gamepad_sprites/gdb-keyboard-2.png

Output atlas: assets/32rogues/input_icons.png
  16 columns x 6 rows = 256x96 pixels
  Row 0: Xbox       (A B X Y LB RB LT RT Start Select L3 R3 DU DD DL DR)
  Row 1: PlayStation (Cross Circle Square Triangle L1 R1 L2 R2 Options Share L3 R3 DU DD DL DR)
  Row 2: Switch     (B A Y X L R ZL ZR + - LS RS DU DD DL DR)
  Rows 3-5: Keyboard keys

Each cell is 16x16. Icons are extracted from the source, tight-cropped,
then centered in the 16x16 cell.
"""

from PIL import Image
import os
import sys

CELL = 16
COLS = 16
ROWS = 6

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PROJECT_DIR = os.path.dirname(SCRIPT_DIR)
OUTPUT_PATH = os.path.join(PROJECT_DIR, "assets", "32rogues", "input_icons.png")
SPRITE_DIR = "/tmp/gamepad_sprites"


def extract_icon(img, bbox, cell_size=CELL):
    """Extract a sprite from bbox, crop to tight bounding box, center in cell."""
    x1, y1, x2, y2 = bbox
    crop = img.crop((x1, y1, x2, y2))

    pixels = crop.load()
    w, h = crop.size
    min_x, min_y = w, h
    max_x, max_y = 0, 0
    for py in range(h):
        for px in range(w):
            if crop.mode == 'RGBA' and pixels[px, py][3] > 32:
                min_x = min(min_x, px)
                min_y = min(min_y, py)
                max_x = max(max_x, px)
                max_y = max(max_y, py)

    if max_x < min_x:
        return Image.new('RGBA', (cell_size, cell_size), (0, 0, 0, 0))

    tight = crop.crop((min_x, min_y, max_x + 1, max_y + 1))
    tw, th = tight.size

    if tw > cell_size or th > cell_size:
        scale = min(cell_size / tw, cell_size / th)
        new_w = max(1, int(tw * scale))
        new_h = max(1, int(th * scale))
        tight = tight.resize((new_w, new_h), Image.NEAREST)
        tw, th = new_w, new_h

    cell = Image.new('RGBA', (cell_size, cell_size), (0, 0, 0, 0))
    ox = (cell_size - tw) // 2
    oy = (cell_size - th) // 2
    cell.paste(tight, (ox, oy), tight if tight.mode == 'RGBA' else None)
    return cell


def extract_dpad_directions(img, cross_bbox):
    """Split a cross-shaped d-pad into 4 direction icons: [up, down, left, right]."""
    x1, y1, x2, y2 = cross_bbox
    cross = img.crop((x1, y1, x2, y2))
    cw, ch = cross.size
    cx, cy = cw // 2, ch // 2

    def to_icon(crop):
        if crop.mode != 'RGBA':
            crop = crop.convert('RGBA')
        return extract_icon(crop, (0, 0, crop.width, crop.height))

    # Up: top half of center column
    up = cross.crop((cx - 5, 0, cx + 5, cy + 2))
    # Down: bottom half of center column
    down = cross.crop((cx - 5, cy - 2, cx + 5, ch))
    # Left: left half of center row
    left = cross.crop((0, cy - 5, cx + 2, cy + 5))
    # Right: right half of center row
    right = cross.crop((cx - 2, cy - 5, cw, cy + 5))

    return [to_icon(up), to_icon(down), to_icon(left), to_icon(right)]


def build_xbox_row(img):
    """Extract 16 Xbox icons: A B X Y LB RB LT RT Start Select L3 R3 DU DD DL DR"""
    icons = []

    # Face buttons: small "flat" column (x=450-462, 12x12 colored circles)
    icons.append(extract_icon(img, (450, 50, 462, 62)))   # A (green)
    icons.append(extract_icon(img, (450, 82, 462, 94)))   # B (red)
    icons.append(extract_icon(img, (450, 34, 462, 46)))   # X (blue)
    icons.append(extract_icon(img, (450, 66, 462, 78)))   # Y (yellow)

    # LB/RB: u2 section flat column (x=113-127, 14x14 labeled circles)
    icons.append(extract_icon(img, (113, 530, 127, 544)))  # LB
    icons.append(extract_icon(img, (113, 546, 127, 560)))  # RB

    # LT/RT: u2 section trigger icons (x=226-240)
    icons.append(extract_icon(img, (226, 562, 240, 576)))  # LT
    icons.append(extract_icon(img, (226, 578, 240, 592)))  # RT

    # Start: u2 section hamburger (x=258-270, row 1 y=546)
    icons.append(extract_icon(img, (258, 546, 270, 560)))  # Start
    # Select: u2 section (x=258-270, row 0 y=530)
    icons.append(extract_icon(img, (258, 530, 270, 544)))  # Select

    # L3/R3: small grid (stick press, circle with dot)
    icons.append(extract_icon(img, (483, 100, 493, 112)))  # L3
    icons.append(extract_icon(img, (483, 116, 493, 128)))  # R3

    # D-pad: split cross from alt section (x=96-128, y=177-208)
    dpad = extract_dpad_directions(img, (96, 177, 128, 208))
    icons.extend(dpad)

    return icons


def build_playstation_row(img):
    """Extract 16 PS icons: Cross Circle Square Triangle L1 R1 L2 R2 Options Share L3 R3 DU DD DL DR"""
    icons = []

    # Face buttons: small flat column (x=450-462, 12x12)
    icons.append(extract_icon(img, (450, 66, 462, 78)))   # Cross (south, cyan)
    icons.append(extract_icon(img, (450, 49, 462, 61)))   # Circle (east, red)
    icons.append(extract_icon(img, (450, 82, 462, 94)))   # Square (west, pink)
    icons.append(extract_icon(img, (450, 34, 462, 46)))   # Triangle (north, green)

    # L1/R1: pressed area bumper shapes
    icons.append(extract_icon(img, (384, 117, 400, 128)))  # L1
    icons.append(extract_icon(img, (384, 133, 400, 144)))  # R1

    # L2/R2: pressed area trigger shapes
    icons.append(extract_icon(img, (384, 81, 400, 96)))    # L2
    icons.append(extract_icon(img, (384, 97, 400, 112)))   # R2

    # Options/Share: small icons from pressed area
    icons.append(extract_icon(img, (388, 52, 396, 64)))    # Options
    icons.append(extract_icon(img, (388, 68, 396, 80)))    # Share

    # L3/R3: stick icons
    icons.append(extract_icon(img, (388, 146, 396, 160)))  # L3
    icons.append(extract_icon(img, (420, 146, 428, 160)))  # R3

    # D-pad: split cross from alt section
    dpad = extract_dpad_directions(img, (96, 193, 128, 224))
    icons.extend(dpad)

    return icons


def build_switch_row(img):
    """Extract 16 Switch icons: B A Y X L R ZL ZR + - LS RS DU DD DL DR"""
    icons = []

    # Face buttons: alt section colored circles
    icons.append(extract_icon(img, (17, 210, 31, 223)))    # B (orange, south)
    icons.append(extract_icon(img, (17, 226, 31, 239)))    # A (red, east)
    icons.append(extract_icon(img, (17, 242, 31, 255)))    # Y (green, west)
    icons.append(extract_icon(img, (17, 258, 31, 271)))    # X (blue, north)

    # L/R: pressed area text labels
    icons.append(extract_icon(img, (386, 86, 398, 96)))    # L
    icons.append(extract_icon(img, (386, 102, 398, 112)))  # R

    # ZL/ZR: pressed area
    icons.append(extract_icon(img, (386, 50, 398, 64)))    # ZL
    icons.append(extract_icon(img, (386, 66, 398, 80)))    # ZR

    # +/-: small flat column
    icons.append(extract_icon(img, (450, 130, 462, 142)))  # +
    icons.append(extract_icon(img, (450, 146, 462, 158)))  # -

    # LS/RS: small flat column
    icons.append(extract_icon(img, (450, 114, 462, 126)))  # LS
    icons.append(extract_icon(img, (450, 98, 462, 110)))   # RS

    # D-pad: split cross from alt section
    dpad = extract_dpad_directions(img, (96, 210, 128, 240))
    icons.extend(dpad)

    return icons


def build_keyboard_rows(img):
    """Extract 48 keyboard key icons (3 rows of 16).

    Uses keyboard layout 3 (y=242-336).

    Verified grid from pixel analysis:
    - All standard keys are on a 16px stride
    - Key cap fills 14x14 within the 16px cell (with shared 2px borders)
    - Extract each key as 16x16 cell directly (it already IS the right size)

    Row layout (y coordinates, all 14px high):
      func:   y=242-256  ESC at x=32, F1-F12 at x=64 stride=16
      num:    y=258-272  ~` at x=48, 1-0 at x=64 stride=16
      qwerty: y=274-288  Tab at x=36, Q at x=64 stride=16
      home:   y=290-304  Caps at x=36, A at x=64 stride=16
      bottom: y=306-320  Shift at x=36, Z at x=64 stride=16
      mod:    y=322-336  Ctrl at x=36, Alt at x=68

    Letter key x positions (all at stride 16 from x=64):
      QWERTY: Q=64, W=80, E=96, R=112, T=128, Y=144, U=160, I=176, O=192, P=208
      HOME:   A=64, S=80, D=96, F=112, G=128, H=144, J=160, K=176, L=192
      BOTTOM: Z=64, X=80, C=96, V=112, B=128, N=144, M=160, ,=176, .=192, /=208
    """
    all_icons = []

    # Row y coordinates
    func_y = 242
    num_y = 258
    qwerty_y = 274
    home_y = 290
    bottom_y = 306
    mod_y = 322
    rh = 14  # row height

    # Key stride
    stride = 16
    # Standard key starts (first letter key in row)
    letter_x0 = 64

    def key_cell(x, y):
        """Extract a 16x14 key cell and center in 16x16."""
        return extract_icon(img, (x, y, x + stride, y + rh))

    def letter_key(row_y, col):
        """Get a letter key at position col (0-based from x=64)."""
        return key_cell(letter_x0 + col * stride, row_y)

    # QWERTY mapping: letter -> (row_y, col)
    key_map = {
        # QWERTY row
        'Q': (qwerty_y, 0), 'W': (qwerty_y, 1), 'E': (qwerty_y, 2),
        'R': (qwerty_y, 3), 'T': (qwerty_y, 4), 'Y': (qwerty_y, 5),
        'U': (qwerty_y, 6), 'I': (qwerty_y, 7), 'O': (qwerty_y, 8),
        'P': (qwerty_y, 9),
        # Home row
        'A': (home_y, 0), 'S': (home_y, 1), 'D': (home_y, 2),
        'F': (home_y, 3), 'G': (home_y, 4), 'H': (home_y, 5),
        'J': (home_y, 6), 'K': (home_y, 7), 'L': (home_y, 8),
        # Bottom row
        'Z': (bottom_y, 0), 'X': (bottom_y, 1), 'C': (bottom_y, 2),
        'V': (bottom_y, 3), 'B': (bottom_y, 4), 'N': (bottom_y, 5),
        'M': (bottom_y, 6),
        # Number row (starts at x=64 for '1')
        '1': (num_y, 0), '2': (num_y, 1), '3': (num_y, 2), '4': (num_y, 3),
        '5': (num_y, 4), '6': (num_y, 5), '7': (num_y, 6), '8': (num_y, 7),
        '9': (num_y, 8), '0': (num_y, 9),
    }

    def get_key(name):
        if name in key_map:
            row_y, col = key_map[name]
            return letter_key(row_y, col)
        elif name == 'ESC':
            return key_cell(32, func_y)
        elif name.startswith('F') and name[1:].isdigit():
            fnum = int(name[1:])
            # F1 at x=64, F2 at x=80... same stride
            return key_cell(64 + (fnum - 1) * stride, func_y)
        elif name == 'Tab':
            return extract_icon(img, (36, qwerty_y, 60, qwerty_y + rh))
        elif name == 'Enter':
            return extract_icon(img, (244, home_y, 268, home_y + rh))
        elif name == 'Space':
            return extract_icon(img, (100, mod_y, 204, mod_y + rh))
        elif name == 'Shift':
            return extract_icon(img, (36, bottom_y, 60, bottom_y + rh))
        elif name == 'Ctrl':
            return extract_icon(img, (36, mod_y, 60, mod_y + rh))
        elif name == '.':
            return letter_key(bottom_y, 8)  # period after comma
        elif name == '/':
            return letter_key(bottom_y, 9)  # slash
        elif name == 'Up':
            return key_cell(320, bottom_y)      # up arrow at x=320, bottom row
        elif name == 'Down':
            return key_cell(320, mod_y)          # down arrow at x=320, mod row
        elif name == 'Left':
            return key_cell(304, mod_y)          # left arrow at x=304, mod row
        elif name == 'Right':
            return key_cell(336, mod_y)          # right arrow at x=336, mod row
        else:
            return Image.new('RGBA', (CELL, CELL), (0, 0, 0, 0))

    # Row 0: A B C D E F G H I J K L M N O P
    for ch in 'ABCDEFGHIJKLMNOP':
        all_icons.append(get_key(ch))

    # Row 1: Q R S T U V W X Y Z 1 2 3 4 Tab Enter
    for ch in 'QRSTUVWXYZ':
        all_icons.append(get_key(ch))
    for ch in '1234':
        all_icons.append(get_key(ch))
    all_icons.append(get_key('Tab'))
    all_icons.append(get_key('Enter'))

    # Row 2: Esc Space Up Down Left Right . / F5 F6 F11 F12 Shift Ctrl (pad pad)
    for name in ['ESC', 'Space', 'Up', 'Down', 'Left', 'Right',
                 '.', '/', 'F5', 'F6', 'F11', 'F12', 'Shift', 'Ctrl']:
        all_icons.append(get_key(name))

    # Pad to 48
    while len(all_icons) < 48:
        all_icons.append(Image.new('RGBA', (CELL, CELL), (0, 0, 0, 0)))

    return all_icons[:48]


def main():
    sheets = {
        'xbox': os.path.join(SPRITE_DIR, 'gdb-xbox-2.png'),
        'playstation': os.path.join(SPRITE_DIR, 'gdb-playstation-2.png'),
        'switch': os.path.join(SPRITE_DIR, 'gdb-switch-2.png'),
        'keyboard': os.path.join(SPRITE_DIR, 'gdb-keyboard-2.png'),
    }

    for name, path in sheets.items():
        if not os.path.exists(path):
            print(f"ERROR: Missing source sheet: {path}")
            sys.exit(1)

    atlas = Image.new('RGBA', (COLS * CELL, ROWS * CELL), (0, 0, 0, 0))

    # Row 0: Xbox
    print("Extracting Xbox icons...")
    xbox_img = Image.open(sheets['xbox'])
    xbox_icons = build_xbox_row(xbox_img)
    for col, icon in enumerate(xbox_icons):
        atlas.paste(icon, (col * CELL, 0 * CELL), icon)

    # Row 1: PlayStation
    print("Extracting PlayStation icons...")
    ps_img = Image.open(sheets['playstation'])
    ps_icons = build_playstation_row(ps_img)
    for col, icon in enumerate(ps_icons):
        atlas.paste(icon, (col * CELL, 1 * CELL), icon)

    # Row 2: Switch
    print("Extracting Switch icons...")
    switch_img = Image.open(sheets['switch'])
    switch_icons = build_switch_row(switch_img)
    for col, icon in enumerate(switch_icons):
        atlas.paste(icon, (col * CELL, 2 * CELL), icon)

    # Rows 3-5: Keyboard
    print("Extracting keyboard icons...")
    kb_img = Image.open(sheets['keyboard'])
    kb_icons = build_keyboard_rows(kb_img)
    for i, icon in enumerate(kb_icons):
        row = 3 + i // COLS
        col = i % COLS
        atlas.paste(icon, (col * CELL, row * CELL), icon)

    # Save
    os.makedirs(os.path.dirname(OUTPUT_PATH), exist_ok=True)
    atlas.save(OUTPUT_PATH)
    print(f"Saved atlas: {OUTPUT_PATH}")
    print(f"Size: {atlas.size[0]}x{atlas.size[1]}")


if __name__ == '__main__':
    main()
