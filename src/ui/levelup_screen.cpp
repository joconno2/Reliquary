#include "ui/levelup_screen.h"
#include "ui/ui_draw.h"
#include "components/stats.h"
#include <cstdio>
#include <algorithm>

static const LevelUpChoice ALL_CHOICES[] = {
    {"+2 Strength",    static_cast<int>(Attr::STR), 2, 0, 0, 0, 0},
    {"+2 Dexterity",   static_cast<int>(Attr::DEX), 2, 0, 0, 0, 0},
    {"+2 Constitution",static_cast<int>(Attr::CON), 2, 0, 0, 0, 0},
    {"+2 Intelligence",static_cast<int>(Attr::INT), 2, 0, 0, 0, 0},
    {"+2 Willpower",   static_cast<int>(Attr::WIL), 2, 0, 0, 0, 0},
    {"+2 Perception",  static_cast<int>(Attr::PER), 2, 0, 0, 0, 0},
    {"Tough: +8 Max HP",  -1, 0, 8, 0, 0, 0},
    {"Arcane: +6 Max MP", -1, 0, 0, 6, 0, 0},
    {"Swift: +10 Speed",  -1, 0, 0, 0, 10, 0},
    {"Sharp: +1 Base Damage", -1, 0, 0, 0, 0, 1},
};
static constexpr int TOTAL_CHOICES = sizeof(ALL_CHOICES) / sizeof(ALL_CHOICES[0]);

void LevelUpScreen::open(Entity player, RNG& rng) {
    open_ = true;
    selected_ = 0;
    player_ = player;
    choices_.clear();

    // Pick 3 unique random choices
    std::vector<int> indices;
    for (int i = 0; i < TOTAL_CHOICES; i++) indices.push_back(i);
    rng.shuffle(indices);

    int count = std::min(3, TOTAL_CHOICES);
    for (int i = 0; i < count; i++) {
        choices_.push_back(ALL_CHOICES[indices[i]]);
    }
}

LevelUpAction LevelUpScreen::handle_input(SDL_Event& event) {
    if (!open_) return LevelUpAction::NONE;

    if (event.type == SDL_MOUSEBUTTONDOWN && event.button.button == SDL_BUTTON_LEFT) {
        // Simple: just confirm current selection
        return LevelUpAction::CHOSEN;
    }

    if (event.type != SDL_KEYDOWN) return LevelUpAction::NONE;

    int count = static_cast<int>(choices_.size());
    switch (event.key.keysym.sym) {
        case SDLK_UP:
        case SDLK_k:
            if (selected_ > 0) selected_--;
            return LevelUpAction::NONE;
        case SDLK_DOWN:
        case SDLK_j:
            if (selected_ < count - 1) selected_++;
            return LevelUpAction::NONE;
        case SDLK_RETURN:
        case SDLK_SPACE:
            return LevelUpAction::CHOSEN;
        case SDLK_1:
            if (count > 0) { selected_ = 0; return LevelUpAction::CHOSEN; }
            return LevelUpAction::NONE;
        case SDLK_2:
            if (count > 1) { selected_ = 1; return LevelUpAction::CHOSEN; }
            return LevelUpAction::NONE;
        case SDLK_3:
            if (count > 2) { selected_ = 2; return LevelUpAction::CHOSEN; }
            return LevelUpAction::NONE;
        default:
            return LevelUpAction::NONE;
    }
}

void LevelUpScreen::apply_choice(World& world) const {
    if (selected_ < 0 || selected_ >= static_cast<int>(choices_.size())) return;
    if (!world.has<Stats>(player_)) return;

    auto& stats = world.get<Stats>(player_);
    auto& choice = choices_[selected_];

    if (choice.attr_index >= 0 && choice.attr_index < ATTR_COUNT) {
        auto attr = static_cast<Attr>(choice.attr_index);
        stats.set_attr(attr, stats.attr(attr) + choice.attr_bonus);
    }
    if (choice.hp_bonus > 0) {
        stats.hp_max += choice.hp_bonus;
        stats.hp += choice.hp_bonus;
    }
    if (choice.mp_bonus > 0) {
        stats.mp_max += choice.mp_bonus;
        stats.mp += choice.mp_bonus;
    }
    if (choice.speed_bonus > 0) {
        stats.base_speed += choice.speed_bonus;
    }
    if (choice.damage_bonus > 0) {
        stats.base_damage += choice.damage_bonus;
    }
}

void LevelUpScreen::render(SDL_Renderer* renderer, TTF_Font* font,
                            int screen_w, int screen_h) const {
    if (!open_ || !font) return;

    int line_h = TTF_FontLineSkip(font);

    SDL_Color title_col = {255, 220, 100, 255};
    SDL_Color normal_col = {180, 175, 170, 255};
    SDL_Color sel_col = {255, 240, 180, 255};
    SDL_Color hint_col = {120, 110, 100, 255};

    // Darken background
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, screen_w, screen_h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer, &overlay);

    // Panel
    int panel_w = std::min(500, screen_w - 40);
    int panel_h = line_h * (6 + static_cast<int>(choices_.size()) * 2);
    int px = (screen_w - panel_w) / 2;
    int py = (screen_h - panel_h) / 2;
    ui::draw_panel(renderer, px, py, panel_w, panel_h);

    int y = py + 12;
    ui::draw_text_centered(renderer, font, "Level Up!", title_col, screen_w / 2, y);
    y += line_h + 8;
    ui::draw_text_centered(renderer, font, "Choose a bonus:", hint_col, screen_w / 2, y);
    y += line_h + 12;

    for (int i = 0; i < static_cast<int>(choices_.size()); i++) {
        bool is_sel = (i == selected_);
        SDL_Color col = is_sel ? sel_col : normal_col;

        char buf[128];
        snprintf(buf, sizeof(buf), "%s%d) %s", is_sel ? "> " : "  ", i + 1,
                 choices_[i].label.c_str());
        ui::draw_text(renderer, font, buf, col, px + 20, y);

        if (is_sel) {
            // Highlight bar
            SDL_Rect sel_rect = {px + 8, y - 2, panel_w - 16, line_h + 4};
            SDL_SetRenderDrawColor(renderer, 60, 50, 30, 100);
            SDL_RenderFillRect(renderer, &sel_rect);
        }

        y += line_h + 8;
    }

    y += 4;
    ui::draw_text_centered(renderer, font, "[1-3] or Up/Down + Enter",
                           hint_col, screen_w / 2, y);
}
