#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "core/spritesheet.h"
#include "components/god.h"
#include "components/class_def.h"

enum class CreationPhase {
    GOD_SELECT,
    CLASS_SELECT,
    DONE
};

struct CharacterBuild {
    GodId god = GodId::VETHRIK;
    ClassId class_id = ClassId::FIGHTER;
};

class CreationScreen {
public:
    CreationScreen() = default;

    void reset();
    bool is_done() const { return phase_ == CreationPhase::DONE; }
    CharacterBuild get_build() const { return build_; }

    // Returns true if input was consumed
    bool handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font,
                const SpriteManager& sprites, int screen_w, int screen_h) const;

private:
    CreationPhase phase_ = CreationPhase::GOD_SELECT;
    int selected_ = 0;
    CharacterBuild build_;

    void render_god_select(SDL_Renderer* renderer, TTF_Font* font,
                            const SpriteManager& sprites, int w, int h) const;
    void render_class_select(SDL_Renderer* renderer, TTF_Font* font,
                              const SpriteManager& sprites, int w, int h) const;
};
