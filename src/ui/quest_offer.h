#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include "components/quest.h"
#include <string>

enum class QuestOfferChoice {
    NONE,
    ACCEPT,
    DECLINE,
};

class QuestOffer {
public:
    QuestOffer() = default;

    void show(QuestId id, const std::string& npc_name);
    void close() { open_ = false; }
    bool is_open() const { return open_; }

    QuestId get_quest_id() const { return quest_id_; }
    QuestOfferChoice handle_input(SDL_Event& event);

    void render(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                int w, int h) const;

private:
    bool open_ = false;
    int selected_ = 0; // 0 = accept, 1 = decline
    QuestId quest_id_ = QuestId::COUNT;
    std::string npc_name_;

    mutable SDL_Rect accept_btn_ = {};
    mutable SDL_Rect decline_btn_ = {};
};
