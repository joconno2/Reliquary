#include "ui/death_screen.h"
#include "ui/ui_draw.h"
#include "components/god.h"
#include <cstdio>
#include <algorithm>

void render_death_screen(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                         int screen_w, int screen_h,
                         Uint32 elapsed_ms, const std::string& death_cause,
                         int god_id, const std::vector<std::string>& newly_unlocked) {
    // Fade-in: 0->255 alpha over 2 seconds
    int fade_alpha = std::min(255, static_cast<int>(elapsed_ms * 255 / 2000));

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, screen_w, screen_h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(std::min(200, fade_alpha)));
    SDL_RenderFillRect(renderer, &overlay);

    // Text fades in after 500ms
    int text_alpha = std::max(0, std::min(255, static_cast<int>((elapsed_ms - 500) * 255 / 1500)));
    if (elapsed_ms >= 500) {
        // "You have died." -- large, red
        SDL_Color red = {200, 50, 50, static_cast<Uint8>(text_alpha)};
        TTF_Font* death_font = font_title ? font_title : font;
        SDL_Surface* surf = TTF_RenderText_Blended(death_font, "You have died.", red);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(text_alpha));
            SDL_Rect dst = {screen_w / 2 - surf->w / 2, screen_h / 3 - 20, surf->w, surf->h};
            SDL_RenderCopy(renderer, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        }
    }

    // Killed-by line -- fades in after 1 second
    int cause_alpha = std::max(0, std::min(255, static_cast<int>((elapsed_ms - 1000) * 255 / 1000)));
    if (elapsed_ms >= 1000 && !death_cause.empty()) {
        char cause_buf[128];
        snprintf(cause_buf, sizeof(cause_buf), "Killed by %s.", death_cause.c_str());
        SDL_Color cause_col = {180, 140, 140, static_cast<Uint8>(cause_alpha)};
        SDL_Surface* csurf = TTF_RenderText_Blended(font, cause_buf, cause_col);
        if (csurf) {
            SDL_Texture* ctex = SDL_CreateTextureFromSurface(renderer, csurf);
            SDL_SetTextureAlphaMod(ctex, static_cast<Uint8>(cause_alpha));
            SDL_Rect cdst = {screen_w / 2 - csurf->w / 2, screen_h / 3 + 30, csurf->w, csurf->h};
            SDL_RenderCopy(renderer, ctex, nullptr, &cdst);
            SDL_DestroyTexture(ctex);
            SDL_FreeSurface(csurf);
        }
    }

    // God-flavored death text -- fades in after 1.5 seconds
    int god_alpha = std::max(0, std::min(255, static_cast<int>((elapsed_ms - 1500) * 255 / 1000)));
    if (elapsed_ms >= 1500) {
        const char* death_line = nullptr;
        GodId dgod = static_cast<GodId>(god_id);
        switch (dgod) {
            case GodId::VETHRIK:   death_line = "Vethrik adds your bones to the collection."; break;
            case GodId::THESSARKA: death_line = "Thessarka records your death. She forgets nothing."; break;
            case GodId::MORRETH:   death_line = "Morreth found you wanting."; break;
            case GodId::YASHKHET:  death_line = "Yashkhet accepts the offering."; break;
            case GodId::KHAEL:     death_line = "Your body feeds the roots."; break;
            case GodId::SOLETH:    death_line = "The flame goes out."; break;
            case GodId::IXUUL:     death_line = "Ixuul is already making something new from the pieces."; break;
            case GodId::ZHAVEK:    death_line = "You vanish. No one notices."; break;
            case GodId::THALARA:   death_line = "The sea takes you back."; break;
            case GodId::OSSREN:    death_line = "You were not built to last."; break;
            case GodId::LETHIS:    death_line = "You fall asleep. You do not wake up."; break;
            case GodId::GATHRUUN:  death_line = "The stone closes over you."; break;
            case GodId::SYTHARA:   death_line = "Rot takes you before you hit the ground."; break;
            default:               death_line = "You die alone."; break;
        }
        SDL_Color dim = {160, 120, 120, static_cast<Uint8>(god_alpha)};
        SDL_Surface* dsurf = TTF_RenderText_Blended(font, death_line, dim);
        if (dsurf) {
            SDL_Texture* dtex = SDL_CreateTextureFromSurface(renderer, dsurf);
            SDL_SetTextureAlphaMod(dtex, static_cast<Uint8>(god_alpha));
            SDL_Rect ddst = {screen_w / 2 - dsurf->w / 2, screen_h / 3 + 60, dsurf->w, dsurf->h};
            SDL_RenderCopy(renderer, dtex, nullptr, &ddst);
            SDL_DestroyTexture(dtex);
            SDL_FreeSurface(dsurf);
        }
    }

    // Newly unlocked classes
    if (!newly_unlocked.empty()) {
        int uy = screen_h / 2 + 20;
        SDL_Color gold = {255, 220, 100, 255};
        ui::draw_text_centered(renderer, font, "Class unlocked:", gold, screen_w / 2, uy);
        uy += TTF_FontLineSkip(font) + 4;
        for (auto& name : newly_unlocked) {
            ui::draw_text_centered(renderer, font_title ? font_title : font,
                                    name.c_str(), gold, screen_w / 2, uy);
            uy += (font_title ? TTF_FontLineSkip(font_title) : TTF_FontLineSkip(font)) + 4;
        }
    }

    if (elapsed_ms >= 3000) {
        ui::draw_text_centered(renderer, font, "Press any key.",
                                {100, 95, 90, 255}, screen_w / 2, screen_h - 40);
    }
}

void render_victory_screen(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                           int screen_w, int screen_h,
                           int god_id, const std::vector<std::string>& newly_unlocked) {
    // Darken screen
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, screen_w, screen_h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer, &overlay);

    if (!font || !font_title) return;

    // God-specific ending text
    const char* title = "You hold the Reliquary.";
    const char* ending = nullptr;
    GodId god = static_cast<GodId>(god_id);
    switch (god) {
        case GodId::VETHRIK:
            ending = "Vethrik claims the Reliquary.\n"
                     "The dead lie still. The undead crumble to dust.\n"
                     "The graveyards are quiet again.\n"
                     "It is done.";
            break;
        case GodId::THESSARKA:
            ending = "Thessarka takes the Reliquary.\n"
                     "Every secret in the world is laid bare.\n"
                     "The price is madness. You pay it gladly.";
            break;
        case GodId::MORRETH:
            ending = "Morreth takes the Reliquary.\n"
                     "The wars end. The strong rule.\n"
                     "You are the strongest. That is enough.";
            break;
        case GodId::YASHKHET:
            ending = "Yashkhet takes the Reliquary.\n"
                     "The blood price is paid in full.\n"
                     "Your hands will never stop shaking.";
            break;
        case GodId::KHAEL:
            ending = "Khael takes the Reliquary.\n"
                     "The forest reclaims the cities.\n"
                     "Mankind is no longer the dominant species.";
            break;
        case GodId::SOLETH:
            ending = "Soleth takes the Reliquary.\n"
                     "The Sepulchre burns. The old places burn.\n"
                     "Everything unclean burns.\n"
                     "There is a lot of burning.";
            break;
        case GodId::IXUUL:
            ending = "Ixuul takes the Reliquary.\n"
                     "It becomes something else. So does everything.\n"
                     "The world is unrecognizable by morning.";
            break;
        case GodId::ZHAVEK:
            ending = "Zhavek takes the Reliquary.\n"
                     "It disappears. The gods cannot find it.\n"
                     "Neither can you.";
            break;
        case GodId::THALARA:
            ending = "Thalara takes the Reliquary.\n"
                     "The sea rises. The lowlands flood.\n"
                     "The age of land is over.";
            break;
        case GodId::OSSREN:
            ending = "Ossren takes the Reliquary.\n"
                     "It is sealed in iron and stone.\n"
                     "No one will ever open it again.";
            break;
        case GodId::LETHIS:
            ending = "Lethis takes the Reliquary.\n"
                     "The world falls asleep.\n"
                     "Some of it wakes up. Most does not.";
            break;
        case GodId::GATHRUUN:
            ending = "Gathruun takes the Reliquary.\n"
                     "It sinks into the earth.\n"
                     "The mountains grow taller. The tunnels go deeper.";
            break;
        case GodId::SYTHARA:
            ending = "Sythara takes the Reliquary.\n"
                     "It decays. So does everything else.\n"
                     "This was always going to happen.";
            break;
        default:
            ending = "No god claims the Reliquary.\n"
                     "You hold it in faithless hands.\n"
                     "It is yours. You are not sure what that means.";
            break;
    }

    // Render title
    SDL_Color gold = {255, 220, 100, 255};
    SDL_Surface* title_surf = TTF_RenderText_Blended(font_title, title, gold);
    if (title_surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, title_surf);
        SDL_Rect dst = {screen_w / 2 - title_surf->w / 2, screen_h / 4, title_surf->w, title_surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(title_surf);
    }

    // Render ending text line by line
    SDL_Color text_col = {200, 190, 170, 255};
    int y_pos = screen_h / 4 + 60;
    const char* p = ending;
    while (p && *p) {
        // Extract one line (up to \n)
        char line[256];
        int len = 0;
        while (*p && *p != '\n' && len < 255) {
            line[len++] = *p++;
        }
        line[len] = '\0';
        if (*p == '\n') p++;

        if (len > 0) {
            SDL_Surface* surf = TTF_RenderText_Blended(font, line, text_col);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_Rect dst = {screen_w / 2 - surf->w / 2, y_pos, surf->w, surf->h};
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }
        }
        y_pos += 24;
    }

    // Newly unlocked classes
    if (!newly_unlocked.empty()) {
        int uy = y_pos + 20;
        SDL_Color unlock_gold = {255, 220, 100, 255};
        ui::draw_text_centered(renderer, font, "Class unlocked:", unlock_gold, screen_w / 2, uy);
        uy += TTF_FontLineSkip(font) + 4;
        for (auto& name : newly_unlocked) {
            ui::draw_text_centered(renderer, font_title, name.c_str(), unlock_gold, screen_w / 2, uy);
            uy += TTF_FontLineSkip(font_title) + 4;
        }
    }

    // "Press any key"
    SDL_Color dim = {140, 130, 120, 255};
    SDL_Surface* prompt = TTF_RenderText_Blended(font, "Press any key.", dim);
    if (prompt) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, prompt);
        SDL_Rect dst = {screen_w / 2 - prompt->w / 2, screen_h - 40, prompt->w, prompt->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(prompt);
    }
}
