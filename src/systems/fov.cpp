#include "systems/fov.h"
#include <cmath>

namespace fov {

// Multipliers for the 8 octants
static const int mult[4][8] = {
    {1,  0,  0, -1, -1,  0,  0,  1},
    {0,  1, -1,  0,  0, -1,  1,  0},
    {0,  1,  1,  0,  0, -1, -1,  0},
    {1,  0,  0,  1, -1,  0,  0, -1},
};

static void cast_light(TileMap& map, int ox, int oy, int radius,
                        int row, float start, float end,
                        int xx, int xy, int yx, int yy) {
    if (start < end) return;

    float new_start = 0.0f;
    bool blocked = false;

    for (int j = row; j <= radius && !blocked; j++) {
        for (int dx = -j; dx <= 0; dx++) {
            int dy = -j;

            // Translate relative to octant
            int mx = ox + dx * xx + dy * xy;
            int my = oy + dx * yx + dy * yy;

            float l_slope = (dx - 0.5f) / (dy + 0.5f);
            float r_slope = (dx + 0.5f) / (dy - 0.5f);

            if (start < r_slope) continue;
            if (end > l_slope) break;

            // Within radius?
            float dist = std::sqrt(static_cast<float>(dx * dx + dy * dy));
            if (dist <= static_cast<float>(radius)) {
                if (map.in_bounds(mx, my)) {
                    map.at(mx, my).visible = true;
                    map.at(mx, my).explored = true;
                }
            }

            if (blocked) {
                if (map.in_bounds(mx, my) && map.is_opaque(mx, my)) {
                    new_start = r_slope;
                    continue;
                } else {
                    blocked = false;
                    start = new_start;
                }
            } else {
                if (map.in_bounds(mx, my) && map.is_opaque(mx, my) && j < radius) {
                    blocked = true;
                    cast_light(map, ox, oy, radius, j + 1, start, l_slope,
                               xx, xy, yx, yy);
                    new_start = r_slope;
                }
            }
        }
    }
}

void compute(TileMap& map, int ox, int oy, int radius) {
    // Clear visibility
    for (int y = 0; y < map.height(); y++) {
        for (int x = 0; x < map.width(); x++) {
            map.at(x, y).visible = false;
        }
    }

    // Origin is always visible
    if (map.in_bounds(ox, oy)) {
        map.at(ox, oy).visible = true;
        map.at(ox, oy).explored = true;
    }

    // Cast light in all 8 octants
    for (int oct = 0; oct < 8; oct++) {
        cast_light(map, ox, oy, radius, 1, 1.0f, 0.0f,
                   mult[0][oct], mult[1][oct],
                   mult[2][oct], mult[3][oct]);
    }
}

} // namespace fov
