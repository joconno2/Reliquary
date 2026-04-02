#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include "components/traits.h"

class TraitSelectScreen {
public:
    TraitSelectScreen() = default;

    void reset();
    bool is_confirmed() const { return confirmed_; }
    const std::vector<TraitId>& get_selected_traits() const { return selected_traits_; }

    // Returns true if input was consumed
    bool handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font, int w, int h) const;

private:
    int cursor_ = 0;             // index into the full trait list
    bool confirmed_ = false;
    std::vector<TraitId> selected_traits_;
    mutable int row_h_ = 0;      // cached for mouse hit-testing
    mutable int list_y_ = 0;     // cached for mouse hit-testing

    bool is_selected(TraitId id) const;
    int positive_selected_count() const;
    int negative_selected_count() const;
    bool can_confirm() const;
};
