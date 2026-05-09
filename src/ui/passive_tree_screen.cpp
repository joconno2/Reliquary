#include "ui/passive_tree_screen.h"
#include "ui/ui_draw.h"
#include "core/input_glyphs.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

// Layout scale: 1 tree unit = this many pixels
static constexpr float SCALE = 90.0f;

// Node radii by type (screen pixels)
static constexpr int R_SMALL    = 10;
static constexpr int R_NOTABLE  = 16;
static constexpr int R_KEYSTONE = 20;
static constexpr int R_CAPSTONE = 24;

static int node_radius(NodeType t) {
    switch (t) {
        case NodeType::SMALL:    return R_SMALL;
        case NodeType::NOTABLE:  return R_NOTABLE;
        case NodeType::KEYSTONE: return R_KEYSTONE;
        case NodeType::CAPSTONE: return R_CAPSTONE;
    }
    return R_SMALL;
}

// Sector colors
static SDL_Color sector_color(Sector s) {
    switch (s) {
        case Sector::MIGHT:     return {220, 80,  60,  255}; // red
        case Sector::FINESSE:   return {60,  200, 80,  255}; // green
        case Sector::ARCANE:    return {100, 140, 255, 255}; // blue
        case Sector::FAITH:     return {255, 220, 100, 255}; // gold
        case Sector::FORTITUDE: return {180, 140, 100, 255}; // brown
        case Sector::NATURE:    return {80,  180, 80,  255}; // forest
        case Sector::SHADOW:    return {160, 100, 200, 255}; // purple
        case Sector::VENOM:     return {120, 200, 60,  255}; // lime
        case Sector::CENTER:    return {180, 175, 170, 255}; // grey
        default:                return {180, 175, 170, 255};
    }
}

void PassiveTreeScreen::open(Entity player, World* world, int screen_w, int screen_h) {
    open_ = true;
    player_ = player;
    world_ = world;
    hovered_node_ = -1;
    if (screen_w > 0) screen_w_ = screen_w;
    if (screen_h > 0) screen_h_ = screen_h;

    // Center camera on start node
    if (world_->has<PassiveTreeState>(player_)) {
        auto& state = world_->get<PassiveTreeState>(player_);
        auto* start = passive_tree::find_node(state.start_node);
        if (start) {
            cam_x_ = start->x;
            cam_y_ = start->y;
        }
    }
}

void PassiveTreeScreen::tree_to_screen(float tx, float ty,
                                        int sw, int sh,
                                        int& sx, int& sy) const {
    float s = SCALE * zoom_;
    sx = sw / 2 + static_cast<int>((tx - cam_x_) * s);
    sy = sh / 2 + static_cast<int>((ty - cam_y_) * s);
}

void PassiveTreeScreen::screen_to_tree(int sx, int sy,
                                        int sw, int sh,
                                        float& tx, float& ty) const {
    float s = SCALE * zoom_;
    tx = cam_x_ + (sx - sw / 2) / s;
    ty = cam_y_ + (sy - sh / 2) / s;
}

int PassiveTreeScreen::node_at_screen(int sx, int sy, int sw, int sh) const {
    int count = passive_tree::node_count();
    const auto* nodes = passive_tree::nodes();

    int best = -1;
    int best_dist_sq = 9999;

    for (int i = 0; i < count; i++) {
        int nx, ny;
        tree_to_screen(nodes[i].x, nodes[i].y, sw, sh, nx, ny);
        int dx = sx - nx;
        int dy = sy - ny;
        int dist_sq = dx * dx + dy * dy;
        int r = static_cast<int>(node_radius(nodes[i].type) * zoom_) + 8;
        if (dist_sq < r * r && dist_sq < best_dist_sq) {
            best = i;
            best_dist_sq = dist_sq;
        }
    }
    return best;
}

bool PassiveTreeScreen::handle_input(SDL_Event& event) {
    if (!open_ || !world_ || !world_->has<PassiveTreeState>(player_)) return false;

    auto& state = world_->get<PassiveTreeState>(player_);

    if (event.type == SDL_MOUSEMOTION) {
        mouse_x_ = event.motion.x;
        mouse_y_ = event.motion.y;
        hovered_node_ = node_at_screen(mouse_x_, mouse_y_, screen_w_, screen_h_);
        return false;
    }

    if (event.type == SDL_KEYDOWN) {
        float pan_speed = 0.5f;
        switch (event.key.keysym.sym) {
            case SDLK_ESCAPE:
            case SDLK_t:
                close();
                return false;
            // Pan with arrow keys / WASD
            case SDLK_LEFT:  case SDLK_a: cam_x_ -= pan_speed; return false;
            case SDLK_RIGHT: case SDLK_d: cam_x_ += pan_speed; return false;
            case SDLK_UP:    case SDLK_w: cam_y_ -= pan_speed; return false;
            case SDLK_DOWN:  case SDLK_s: cam_y_ += pan_speed; return false;
            // Center on start node
            case SDLK_HOME: {
                auto* start = passive_tree::find_node(state.start_node);
                if (start) { cam_x_ = start->x; cam_y_ = start->y; }
                return false;
            }
            default: return false;
        }
    }

    // Left click to allocate
    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        mouse_x_ = event.button.x;
        mouse_y_ = event.button.y;
        hovered_node_ = node_at_screen(mouse_x_, mouse_y_, screen_w_, screen_h_);
        if (hovered_node_ >= 0) {
            const auto* nodes = passive_tree::nodes();
            uint16_t id = nodes[hovered_node_].id;
            if (passive_tree::can_allocate(state, id)) {
                // Check skill requirement
                if (world_->has<Skills>(player_) &&
                    !passive_tree::skill_requirement_met(id, world_->get<Skills>(player_))) {
                    // Can't allocate: skill too low
                    // (tooltip will show the requirement)
                } else {
                    state.allocate(id);
                    return true;
                }
            }
        }
    }

    // Right click to pan (drag)
    if (event.type == SDL_MOUSEMOTION && (event.motion.state & SDL_BUTTON_RMASK)) {
        cam_x_ -= event.motion.xrel / SCALE;
        cam_y_ -= event.motion.yrel / SCALE;
        return false;
    }

    // Scroll to zoom
    if (event.type == SDL_MOUSEWHEEL) {
        float old_zoom = zoom_;
        zoom_ += event.wheel.y * 0.1f;
        if (zoom_ < 0.4f) zoom_ = 0.4f;
        if (zoom_ > 3.0f) zoom_ = 3.0f;
        // Zoom toward mouse position
        if (std::abs(zoom_ - old_zoom) > 0.001f) {
            float mx_tree, my_tree;
            float s_old = SCALE * old_zoom;
            mx_tree = cam_x_ + (mouse_x_ - screen_w_ / 2) / s_old;
            my_tree = cam_y_ + (mouse_y_ - screen_h_ / 2) / s_old;
            float s_new = SCALE * zoom_;
            cam_x_ = mx_tree - (mouse_x_ - screen_w_ / 2) / s_new;
            cam_y_ = my_tree - (mouse_y_ - screen_h_ / 2) / s_new;
        }
        return false;
    }

    return false;
}

void PassiveTreeScreen::draw_connections(SDL_Renderer* renderer,
                                          const PassiveTreeState& state,
                                          int sw, int sh) const {
    const auto* nodes = passive_tree::nodes();
    int count = passive_tree::node_count();

    for (int i = 0; i < count; i++) {
        int ax, ay;
        tree_to_screen(nodes[i].x, nodes[i].y, sw, sh, ax, ay);

        for (int c = 0; c < 6; c++) {
            uint16_t conn = nodes[i].connections[c];
            if (conn == NO_CONN) continue;
            // Only draw each connection once (lower id draws)
            if (conn < nodes[i].id) continue;

            const auto* other = passive_tree::find_node(conn);
            if (!other) continue;

            int bx, by;
            tree_to_screen(other->x, other->y, sw, sh, bx, by);

            bool a_alloc = state.is_allocated(nodes[i].id);
            bool b_alloc = state.is_allocated(other->id);

            if (a_alloc && b_alloc) {
                // Both allocated: bright glowing line
                // Outer glow
                SDL_SetRenderDrawColor(renderer, 180, 160, 100, 60);
                SDL_RenderDrawLine(renderer, ax-2, ay-2, bx-2, by-2);
                SDL_RenderDrawLine(renderer, ax+2, ay+2, bx+2, by+2);
                // Core bright line
                SDL_SetRenderDrawColor(renderer, 240, 220, 160, 255);
                SDL_RenderDrawLine(renderer, ax, ay, bx, by);
                SDL_RenderDrawLine(renderer, ax+1, ay, bx+1, by);
                SDL_RenderDrawLine(renderer, ax, ay+1, bx, by+1);
                SDL_RenderDrawLine(renderer, ax-1, ay, bx-1, by);
                SDL_RenderDrawLine(renderer, ax, ay-1, bx, by-1);
            } else if (a_alloc || b_alloc) {
                // One allocated: medium line (available path)
                SDL_SetRenderDrawColor(renderer, 130, 120, 90, 200);
                SDL_RenderDrawLine(renderer, ax, ay, bx, by);
                SDL_RenderDrawLine(renderer, ax+1, ay, bx+1, by);
                SDL_RenderDrawLine(renderer, ax, ay+1, bx, by+1);
            } else {
                // Neither: thin dim line
                SDL_SetRenderDrawColor(renderer, 50, 45, 40, 120);
                SDL_RenderDrawLine(renderer, ax, ay, bx, by);
            }
        }
    }
}

// Helper: draw a filled circle using horizontal lines
static void fill_circle(SDL_Renderer* r, int cx, int cy, int rad) {
    for (int dy = -rad; dy <= rad; dy++) {
        int dx = static_cast<int>(std::sqrt(rad * rad - dy * dy));
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

// Helper: draw a circle outline
static void draw_circle(SDL_Renderer* r, int cx, int cy, int rad) {
    int x = rad, y = 0, err = 1 - rad;
    while (x >= y) {
        SDL_RenderDrawPoint(r, cx + x, cy + y); SDL_RenderDrawPoint(r, cx - x, cy + y);
        SDL_RenderDrawPoint(r, cx + x, cy - y); SDL_RenderDrawPoint(r, cx - x, cy - y);
        SDL_RenderDrawPoint(r, cx + y, cy + x); SDL_RenderDrawPoint(r, cx - y, cy + x);
        SDL_RenderDrawPoint(r, cx + y, cy - x); SDL_RenderDrawPoint(r, cx - y, cy - x);
        y++;
        if (err < 0) { err += 2 * y + 1; }
        else { x--; err += 2 * (y - x) + 1; }
    }
}

// Helper: draw a filled diamond
static void fill_diamond(SDL_Renderer* r, int cx, int cy, int rad) {
    for (int dy = -rad; dy <= rad; dy++) {
        int dx = rad - std::abs(dy);
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

// Helper: draw a diamond outline
static void draw_diamond(SDL_Renderer* r, int cx, int cy, int rad) {
    SDL_RenderDrawLine(r, cx, cy - rad, cx + rad, cy);
    SDL_RenderDrawLine(r, cx + rad, cy, cx, cy + rad);
    SDL_RenderDrawLine(r, cx, cy + rad, cx - rad, cy);
    SDL_RenderDrawLine(r, cx - rad, cy, cx, cy - rad);
}

// Helper: fill a hexagon
static void fill_hexagon(SDL_Renderer* r, int cx, int cy, int rad) {
    // Flat-top hexagon: 6 vertices
    for (int dy = -rad; dy <= rad; dy++) {
        float t = static_cast<float>(std::abs(dy)) / rad;
        int dx;
        if (t <= 0.5f) dx = rad;          // top/bottom half: full width
        else dx = static_cast<int>(rad * (1.0f - (t - 0.5f) * 2.0f)); // taper
        SDL_RenderDrawLine(r, cx - dx, cy + dy, cx + dx, cy + dy);
    }
}

// Helper: draw a hexagon outline
static void draw_hexagon(SDL_Renderer* r, int cx, int cy, int rad) {
    int hr = rad / 2; // half-height of flat section
    // 6 vertices (flat-top hex)
    SDL_RenderDrawLine(r, cx - rad, cy - hr, cx, cy - rad);      // top-left
    SDL_RenderDrawLine(r, cx, cy - rad, cx + rad, cy - hr);      // top-right
    SDL_RenderDrawLine(r, cx + rad, cy - hr, cx + rad, cy + hr);  // right
    SDL_RenderDrawLine(r, cx + rad, cy + hr, cx, cy + rad);      // bottom-right
    SDL_RenderDrawLine(r, cx, cy + rad, cx - rad, cy + hr);      // bottom-left
    SDL_RenderDrawLine(r, cx - rad, cy + hr, cx - rad, cy - hr);  // left
}

void PassiveTreeScreen::draw_nodes(SDL_Renderer* renderer, TTF_Font* font,
                                    const PassiveTreeState& state,
                                    int sw, int sh) const {
    const auto* nodes = passive_tree::nodes();
    int count = passive_tree::node_count();

    for (int i = 0; i < count; i++) {
        int cx, cy;
        tree_to_screen(nodes[i].x, nodes[i].y, sw, sh, cx, cy);

        if (cx < -50 || cx > sw + 50 || cy < -50 || cy > sh + 50) continue;

        int r = std::max(4, static_cast<int>(node_radius(nodes[i].type) * zoom_));
        bool allocated = state.is_allocated(nodes[i].id);
        bool available = !allocated && passive_tree::can_allocate(state, nodes[i].id);
        bool hovered = (i == hovered_node_);

        SDL_Color col = sector_color(nodes[i].sector);
        uint8_t cr, cg, cb, ca;

        if (allocated) {
            cr = col.r; cg = col.g; cb = col.b; ca = 255;
        } else if (available) {
            cr = col.r * 6 / 10; cg = col.g * 6 / 10; cb = col.b * 6 / 10; ca = 220;
        } else {
            cr = 45; cg = 42; cb = 40; ca = 160;
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        switch (nodes[i].type) {
            case NodeType::SMALL:
                SDL_SetRenderDrawColor(renderer, cr, cg, cb, ca);
                fill_circle(renderer, cx, cy, r);
                if (hovered || allocated) {
                    SDL_SetRenderDrawColor(renderer, 200, 195, 180, allocated ? 200 : 120);
                    draw_circle(renderer, cx, cy, r);
                }
                break;

            case NodeType::NOTABLE:
                SDL_SetRenderDrawColor(renderer, cr, cg, cb, ca);
                fill_diamond(renderer, cx, cy, r);
                SDL_SetRenderDrawColor(renderer, allocated ? 255 : 100,
                                        allocated ? 240 : 95,
                                        allocated ? 200 : 80, 220);
                draw_diamond(renderer, cx, cy, r);
                if (allocated) draw_diamond(renderer, cx, cy, r + 2);
                break;

            case NodeType::KEYSTONE:
                SDL_SetRenderDrawColor(renderer, cr, cg, cb, ca);
                fill_hexagon(renderer, cx, cy, r);
                // Double border in gold
                SDL_SetRenderDrawColor(renderer, 255, 200, 60, 255);
                draw_hexagon(renderer, cx, cy, r);
                draw_hexagon(renderer, cx, cy, r + 3);
                break;

            case NodeType::CAPSTONE:
                SDL_SetRenderDrawColor(renderer, cr, cg, cb, ca);
                fill_circle(renderer, cx, cy, r);
                // Triple border
                SDL_SetRenderDrawColor(renderer, 255, 220, 100, 255);
                draw_circle(renderer, cx, cy, r);
                draw_circle(renderer, cx, cy, r + 2);
                draw_circle(renderer, cx, cy, r + 4);
                break;
        }

        // Start node: special golden ring
        if (nodes[i].id == state.start_node) {
            SDL_SetRenderDrawColor(renderer, 255, 220, 100, 180);
            draw_circle(renderer, cx, cy, r + 5);
            draw_circle(renderer, cx, cy, r + 6);
        }

        // Hover glow
        if (hovered) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 160);
            draw_circle(renderer, cx, cy, r + 6);
            draw_circle(renderer, cx, cy, r + 7);
            // Bright inner highlight
            SDL_SetRenderDrawColor(renderer, 255, 255, 255, 60);
            fill_circle(renderer, cx, cy, r / 2);
        }

        // Available pulse
        if (available && !hovered) {
            int pulse_phase = (SDL_GetTicks() / 40 + i * 17) % 60;
            int pulse_alpha = (pulse_phase < 30) ? (40 + pulse_phase * 2) : (100 - (pulse_phase - 30) * 2);
            SDL_SetRenderDrawColor(renderer, col.r, col.g, col.b, static_cast<uint8_t>(pulse_alpha));
            draw_circle(renderer, cx, cy, r + 3);
            draw_circle(renderer, cx, cy, r + 4);
        }

        // Allocated inner glow
        if (allocated && !hovered) {
            SDL_SetRenderDrawColor(renderer, 255, 255, 240, 40);
            fill_circle(renderer, cx, cy, r * 2 / 3);
        }

        // Node name label for notables, keystones, capstones (when zoomed in enough)
        if (nodes[i].name && zoom_ >= 0.7f && font &&
            (nodes[i].type != NodeType::SMALL)) {
            SDL_Color label_col;
            if (allocated) label_col = {col.r, col.g, col.b, 220};
            else if (available) label_col = {static_cast<Uint8>(col.r * 7 / 10), static_cast<Uint8>(col.g * 7 / 10), static_cast<Uint8>(col.b * 7 / 10), 180};
            else label_col = {70, 65, 60, 140};

            ui::draw_text_centered(renderer, font, nodes[i].name, label_col,
                                    cx, cy + r + static_cast<int>(8 * zoom_));
        }
    }
}

void PassiveTreeScreen::draw_tooltip(SDL_Renderer* renderer, TTF_Font* font,
                                      int sw, int sh) const {
    if (hovered_node_ < 0 || !font) return;

    const auto* nodes = passive_tree::nodes();
    const auto& node = nodes[hovered_node_];

    const char* name = node.name ? node.name : sector_name(node.sector);
    const char* desc = node.description ? node.description : "";

    // Node type label
    const char* type_label = "";
    switch (node.type) {
        case NodeType::SMALL:    type_label = "Minor"; break;
        case NodeType::NOTABLE:  type_label = "Notable"; break;
        case NodeType::KEYSTONE: type_label = "Keystone"; break;
        case NodeType::CAPSTONE: type_label = "Capstone"; break;
    }

    // Sector name
    const char* sec_name = sector_name(node.sector);

    int line_h = TTF_FontLineSkip(font);
    int tooltip_w = 320;
    int desc_h = ui::text_wrapped_height(font, desc, tooltip_w - 20);
    int tooltip_h = line_h * 2 + desc_h + line_h + 24; // name + tag line, desc, status, padding
    int tx = mouse_x_ + 20;
    int ty = mouse_y_ - tooltip_h / 2;

    // Keep on screen
    if (tx + tooltip_w > sw) tx = mouse_x_ - tooltip_w - 12;
    if (ty + tooltip_h > sh) ty = sh - tooltip_h - 4;
    if (ty < 4) ty = 4;
    if (tx < 4) tx = 4;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    // Drop shadow
    SDL_Rect shadow = {tx + 3, ty + 3, tooltip_w, tooltip_h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 120);
    SDL_RenderFillRect(renderer, &shadow);

    // Background
    SDL_Rect bg = {tx, ty, tooltip_w, tooltip_h};
    SDL_SetRenderDrawColor(renderer, 18, 16, 12, 240);
    SDL_RenderFillRect(renderer, &bg);

    // Border (sector-colored top edge, dim sides/bottom)
    SDL_Color sec_col = sector_color(node.sector);
    SDL_SetRenderDrawColor(renderer, sec_col.r, sec_col.g, sec_col.b, 200);
    SDL_RenderDrawLine(renderer, tx, ty, tx + tooltip_w - 1, ty); // top
    SDL_RenderDrawLine(renderer, tx, ty + 1, tx + tooltip_w - 1, ty + 1); // top 2px
    SDL_SetRenderDrawColor(renderer, 80, 70, 55, 180);
    SDL_RenderDrawLine(renderer, tx, ty + 2, tx, ty + tooltip_h - 1); // left
    SDL_RenderDrawLine(renderer, tx + tooltip_w - 1, ty + 2, tx + tooltip_w - 1, ty + tooltip_h - 1); // right
    SDL_RenderDrawLine(renderer, tx, ty + tooltip_h - 1, tx + tooltip_w - 1, ty + tooltip_h - 1); // bottom

    int cy = ty + 8;

    // Name
    SDL_Color name_col = sec_col;
    if (node.type == NodeType::KEYSTONE) name_col = {255, 180, 60, 255};
    if (node.type == NodeType::CAPSTONE) name_col = {255, 220, 100, 255};
    ui::draw_text(renderer, font, name, name_col, tx + 10, cy);

    // Type + Sector (right-aligned on same line)
    {
        char tag[48];
        snprintf(tag, sizeof(tag), "%s / %s", type_label, sec_name);
        SDL_Color tag_col = {100, 95, 85, 200};
        // Right-align: estimate width
        int est_w = ui::text_width(font, tag);
        ui::draw_text(renderer, font, tag, tag_col, tx + tooltip_w - est_w - 10, cy);
    }
    cy += line_h + 4;

    // Description
    SDL_Color desc_col = {190, 185, 175, 255};
    ui::draw_text_wrapped(renderer, font, desc, desc_col, tx + 10, cy, tooltip_w - 20);
    cy += ui::text_wrapped_height(font, desc, tooltip_w - 20) + 4;

    // Status
    if (world_ && world_->has<PassiveTreeState>(player_)) {
        auto& state = world_->get<PassiveTreeState>(player_);
        const char* status_text = "";
        SDL_Color status_col = {120, 115, 110, 255};

        if (state.is_allocated(node.id)) {
            status_text = "Allocated";
            status_col = {100, 200, 100, 255};
        } else if (passive_tree::can_allocate(state, node.id)) {
            // Check skill requirement
            bool skill_ok = true;
            if (world_->has<Skills>(player_))
                skill_ok = passive_tree::skill_requirement_met(node.id, world_->get<Skills>(player_));
            if (!skill_ok) {
                static char req_buf[64];
                snprintf(req_buf, sizeof(req_buf), "Requires %s %d",
                         skill_name(static_cast<SkillId>(node.required_skill)),
                         node.required_skill_level);
                status_text = req_buf;
                status_col = {200, 100, 80, 255};
            } else if (state.points_available > 0) {
                status_text = "Click to allocate (1 point)";
                status_col = {220, 200, 80, 255};
            } else {
                status_text = "No points available";
                status_col = {140, 100, 80, 255};
            }
        } else {
            status_text = "Not connected";
            status_col = {100, 80, 70, 200};
        }
        ui::draw_text(renderer, font, status_text, status_col, tx + 10, cy);
    }
}

void PassiveTreeScreen::draw_hud(SDL_Renderer* renderer, TTF_Font* font,
                                  const PassiveTreeState& state,
                                  [[maybe_unused]] int sw, int sh) const {
    if (!font) return;
    int line_h = TTF_FontLineSkip(font);

    // Top-left: points available / spent
    char buf[64];
    snprintf(buf, sizeof(buf), "Points: %d available  (%d spent)",
             state.points_available, state.points_spent);
    SDL_Color pts_col = state.points_available > 0
        ? SDL_Color{255, 220, 100, 255}
        : SDL_Color{180, 175, 170, 255};
    ui::draw_text(renderer, font, buf, pts_col, 12, 12);

    // Bottom: controls hint
    SDL_Color hint_col = {120, 115, 110, 255};
    { auto* ig = InputGlyphs::get();
      char hbuf[256];
      if (ig && ig->using_gamepad())
          snprintf(hbuf, sizeof(hbuf), "L-Stick: pan  |  %s allocate  |  %s close",
                   ig->confirm().c_str(), ig->cancel().c_str());
      else
          snprintf(hbuf, sizeof(hbuf), "WASD/Arrows: pan  |  Click: allocate  |  T/Esc: close");
      ui::draw_text(renderer, font, hbuf, hint_col, 12, sh - line_h - 8); }
}

void PassiveTreeScreen::render(SDL_Renderer* renderer, TTF_Font* font,
                                TTF_Font* font_title,
                                int sw, int sh) const {
    if (!open_ || !world_ || !world_->has<PassiveTreeState>(player_)) return;

    auto& state = world_->get<PassiveTreeState>(player_);

    // Darken background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, sw, sh};
    SDL_SetRenderDrawColor(renderer, 10, 8, 6, 240);
    SDL_RenderFillRect(renderer, &overlay);

    // Cache screen dimensions for input hit testing
    auto* self = const_cast<PassiveTreeScreen*>(this);
    self->screen_w_ = sw;
    self->screen_h_ = sh;

    // Draw background radial lines from center to sector positions (subtle orientation)
    {
        int center_sx, center_sy;
        tree_to_screen(0.0f, 0.0f, sw, sh, center_sx, center_sy);
        SDL_SetRenderDrawColor(renderer, 30, 28, 25, 80);
        struct SectorPos { float x, y; };
        static const SectorPos SECTOR_POS[] = {
            {0.0f, 7.5f}, {-7.5f, 0.0f}, {0.0f, -7.0f}, {3.5f, -5.2f},
            {6.8f, 0.0f}, {3.0f, 5.5f}, {-3.0f, -5.8f}, {-4.5f, 4.5f}
        };
        for (auto& sp : SECTOR_POS) {
            int ex, ey;
            tree_to_screen(sp.x, sp.y, sw, sh, ex, ey);
            SDL_RenderDrawLine(renderer, center_sx, center_sy, ex, ey);
        }
    }

    // Draw sector labels (use title font if available, with sector tint)
    {
        struct SectorLabel { Sector sector; float x, y; };
        static const SectorLabel LABELS[] = {
            {Sector::MIGHT,     0.0f,  2.0f},
            {Sector::FINESSE,  -2.0f,  0.0f},
            {Sector::ARCANE,    0.0f, -2.0f},
            {Sector::FAITH,     2.0f, -1.5f},
            {Sector::FORTITUDE, 2.5f,  0.0f},
            {Sector::NATURE,    1.5f,  2.0f},
            {Sector::SHADOW,   -2.0f, -1.5f},
            {Sector::VENOM,    -1.5f,  2.0f},
        };
        TTF_Font* label_font = font_title ? font_title : font;
        for (auto& lbl : LABELS) {
            int sx, sy;
            tree_to_screen(lbl.x, lbl.y, sw, sh, sx, sy);
            SDL_Color col = sector_color(lbl.sector);
            col.a = static_cast<uint8_t>(std::min(255, static_cast<int>(60 + zoom_ * 40)));
            if (label_font)
                ui::draw_text_centered(renderer, label_font, sector_name(lbl.sector), col, sx, sy);
        }
        // Center label
        if (label_font) {
            int cx, cy;
            tree_to_screen(0.0f, 0.0f, sw, sh, cx, cy);
            SDL_Color center_col = {120, 115, 110, static_cast<uint8_t>(40 + zoom_ * 20)};
            ui::draw_text_centered(renderer, label_font, "Core", center_col, cx, cy);
        }
    }

    draw_connections(renderer, state, sw, sh);
    draw_nodes(renderer, font, state, sw, sh);
    draw_tooltip(renderer, font, sw, sh);
    draw_hud(renderer, font, state, sw, sh);
}
