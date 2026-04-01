#pragma once

struct DeathAnim {
    float timer = 0.0f;          // elapsed time in seconds
    float duration = 0.4f;       // total dissolve duration
    int original_sheet;          // preserve original sprite during anim
    int original_sx;
    int original_sy;
    bool original_flip_h = false;
};
