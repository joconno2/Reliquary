#pragma once
#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <string>
#include <vector>

// Render the death overlay (fade-in text with god-flavored flavor line).
// elapsed_ms = SDL_GetTicks() - end_screen_time_
void render_death_screen(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                         int screen_w, int screen_h,
                         Uint32 elapsed_ms, const std::string& death_cause,
                         int god_id, const std::vector<std::string>& newly_unlocked);

// Render the victory/ending overlay with god-specific ending text.
void render_victory_screen(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                           int screen_w, int screen_h,
                           int god_id, const std::vector<std::string>& newly_unlocked);
