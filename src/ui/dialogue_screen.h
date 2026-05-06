#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>
#include <functional>

// A dialogue option the player can select
struct DialogueOption {
    std::string label;    // "Shop", "Rest (10g)", "Ask about the dungeon"
    int action_id = 0;    // arbitrary ID, caller interprets
    bool enabled = true;  // greyed out if false
};

// Dialogue screen: shows NPC name, text, and selectable options
class DialogueScreen {
public:
    DialogueScreen() = default;

    void open(const std::string& npc_name, const std::string& text,
              const std::vector<DialogueOption>& options);
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    // Returns selected action_id, or -1 if no action yet, -2 if closed
    int handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                int screen_w, int screen_h) const;

    const std::string& get_npc_name() const { return npc_name_; }

private:
    bool open_ = false;
    std::string npc_name_;
    std::string text_;
    std::vector<DialogueOption> options_;
    int selected_ = 0;
};
