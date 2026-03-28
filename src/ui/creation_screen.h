#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <vector>
#include <string>
#include "core/spritesheet.h"
#include "components/god.h"
#include "components/class_def.h"
#include "components/background.h"
#include "components/traits.h"
#include "ui/background_select.h"
#include "ui/trait_select.h"

enum class CreationPhase {
    CLASS_SELECT,
    NAME_ENTRY,
    GOD_SELECT,
    BACKGROUND_SELECT,
    TRAIT_SELECT,
    DONE
};

struct CharacterBuild {
    ClassId class_id = ClassId::FIGHTER;
    std::string name = "Aldric";
    GodId god = GodId::VETHRIK;
    BackgroundId background = BackgroundId::FARMER;
    std::vector<TraitId> traits;
};

class CreationScreen {
public:
    CreationScreen() = default;

    void reset();
    bool is_done() const { return phase_ == CreationPhase::DONE; }
    CharacterBuild get_build() const { return build_; }

    bool handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                const SpriteManager& sprites, int screen_w, int screen_h) const;

private:
    CreationPhase phase_ = CreationPhase::CLASS_SELECT;
    int selected_ = 0;
    CharacterBuild build_;
    bool cursor_blink_ = false;
    int blink_timer_ = 0;

    BackgroundSelectScreen bg_screen_;
    TraitSelectScreen trait_screen_;

    void randomize_name();

    void render_class_select(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                              const SpriteManager& sprites, int w, int h) const;
    void render_name_entry(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                            const SpriteManager& sprites, int w, int h) const;
    void render_god_select(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                            const SpriteManager& sprites, int w, int h) const;
};
