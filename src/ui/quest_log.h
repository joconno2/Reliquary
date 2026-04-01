#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "components/quest.h"
#include "core/ecs.h"

class QuestLog {
public:
    QuestLog() = default;

    void open() { open_ = true; selected_ = 0; }
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    bool handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                const QuestJournal& journal, int w, int h,
                World* world = nullptr) const;

private:
    bool open_ = false;
    mutable int selected_ = 0;
};
