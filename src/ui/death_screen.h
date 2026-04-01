#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>

// Run summary stats shown on death/victory screens
struct RunSummary {
    int turns = 0;
    int kills = 0;
    int deepest_floor = 0;
    int gold_earned = 0;
    int quests_completed = 0;
    std::string class_name;
};

// Render the death overlay (fade-in text with god-flavored flavor line).
// elapsed_ms = SDL_GetTicks() - end_screen_time_
void render_death_screen(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                         int screen_w, int screen_h,
                         Uint32 elapsed_ms, const std::string& death_cause,
                         int god_id, const std::vector<std::string>& newly_unlocked,
                         const RunSummary& summary = {});

// Render the victory/ending overlay with god-specific ending text.
void render_victory_screen(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                           int screen_w, int screen_h,
                           int god_id, const std::vector<std::string>& newly_unlocked,
                           const RunSummary& summary = {});
