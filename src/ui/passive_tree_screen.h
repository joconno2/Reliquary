#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/ecs.h"
#include "components/passive_tree.h"

class PassiveTreeScreen {
public:
    PassiveTreeScreen() = default;

    void open(Entity player, World* world, int screen_w = 0, int screen_h = 0);
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    // Returns true if a node was allocated this frame
    bool handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                int screen_w, int screen_h) const;

private:
    bool open_ = false;
    Entity player_ = 0;
    World* world_ = nullptr;

    // Camera offset (panning)
    float cam_x_ = 0.0f;
    float cam_y_ = 0.0f;

    // Currently hovered node (for tooltip)
    int hovered_node_ = -1; // index into node array, -1 = none

    // Mouse position (screen coords)
    int mouse_x_ = 0, mouse_y_ = 0;

    // Cached screen dimensions for input hit testing
    int screen_w_ = 1280, screen_h_ = 800;

    // Zoom level (1.0 = default)
    float zoom_ = 1.0f;

    // Convert tree coords to screen coords
    void tree_to_screen(float tx, float ty, int screen_w, int screen_h,
                        int& sx, int& sy) const;
    // Convert screen coords to tree coords
    void screen_to_tree(int sx, int sy, int screen_w, int screen_h,
                        float& tx, float& ty) const;

    // Find node index under screen position (or -1)
    int node_at_screen(int sx, int sy, int screen_w, int screen_h) const;

    // Render helpers
    void draw_connections(SDL_Renderer* renderer, const PassiveTreeState& state,
                         int screen_w, int screen_h) const;
    void draw_nodes(SDL_Renderer* renderer, TTF_Font* font,
                    const PassiveTreeState& state,
                    int screen_w, int screen_h) const;
    void draw_tooltip(SDL_Renderer* renderer, TTF_Font* font,
                      int screen_w, int screen_h) const;
    void draw_hud(SDL_Renderer* renderer, TTF_Font* font,
                  const PassiveTreeState& state,
                  int screen_w, int screen_h) const;
};
