#include "ui/death_screen.h"
#include "ui/ui_draw.h"
#include "components/god.h"
#include <cstdio>
#include <algorithm>

static void render_run_summary(SDL_Renderer* renderer, TTF_Font* font,
                                const ui::Rect& area, const RunSummary& s) {
    if (s.turns == 0 && s.kills == 0) return;
    SDL_Color label = {120, 115, 110, 255};
    SDL_Color value = {180, 175, 165, 255};
    int line_h = TTF_FontLineSkip(font);
    auto layout = ui::Layout::from_rect(area, line_h);

    auto hdr = layout.row(line_h + 4);
    ui::draw_text_in(renderer, font, "-- Run Summary --", {140, 130, 120, 255},
                     hdr, ui::Align::CENTER);

    char buf[128];
    auto draw_stat = [&](const char* lbl, const char* val) {
        if (!layout.fits_row()) return;
        auto row = layout.row(line_h + 2);
        SDL_Surface* ls = TTF_RenderText_Blended(font, lbl, label);
        SDL_Surface* vs = TTF_RenderText_Blended(font, val, value);
        if (ls && vs) {
            int total_w = ls->w + 8 + vs->w;
            SDL_Texture* lt = SDL_CreateTextureFromSurface(renderer, ls);
            SDL_Texture* vt = SDL_CreateTextureFromSurface(renderer, vs);
            SDL_Rect ld = {row.cx() - total_w / 2, row.y, ls->w, ls->h};
            SDL_Rect vd = {row.cx() - total_w / 2 + ls->w + 8, row.y, vs->w, vs->h};
            SDL_RenderCopy(renderer, lt, nullptr, &ld);
            SDL_RenderCopy(renderer, vt, nullptr, &vd);
            SDL_DestroyTexture(lt); SDL_DestroyTexture(vt);
        }
        if (ls) SDL_FreeSurface(ls);
        if (vs) SDL_FreeSurface(vs);
    };

    if (!s.class_name.empty()) {
        snprintf(buf, sizeof(buf), "%s (Lv %d)", s.class_name.c_str(), s.level);
        draw_stat("Class:", buf);
    }
    if (!s.god_name.empty()) draw_stat("God:", s.god_name.c_str());
    if (!s.death_location.empty()) draw_stat("Fell in:", s.death_location.c_str());
    snprintf(buf, sizeof(buf), "%d", s.turns); draw_stat("Turns:", buf);
    snprintf(buf, sizeof(buf), "%d", s.kills); draw_stat("Kills:", buf);
    snprintf(buf, sizeof(buf), "%d", s.deepest_floor); draw_stat("Deepest floor:", buf);
    snprintf(buf, sizeof(buf), "%d", s.gold_earned); draw_stat("Gold earned:", buf);
    snprintf(buf, sizeof(buf), "%d", s.quests_completed); draw_stat("Quests:", buf);
    snprintf(buf, sizeof(buf), "%d", s.items_carried); draw_stat("Items carried:", buf);
}

void render_death_screen(SDL_Renderer* renderer, TTF_Font* font, TTF_Font* font_title,
                         int screen_w, int screen_h,
                         Uint32 elapsed_ms, const std::string& death_cause,
                         int god_id, const std::vector<std::string>& newly_unlocked,
                         const RunSummary& summary) {
    int line_h = font ? TTF_FontLineSkip(font) : 20;
    int title_h = font_title ? TTF_FontLineSkip(font_title) : line_h;
    auto screen = ui::Layout::from_screen(screen_w, screen_h, line_h);

    // Fade-in overlay
    int fade_alpha = std::min(255, static_cast<int>(elapsed_ms * 255 / 2000));
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_Rect overlay = {0, 0, screen_w, screen_h};
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, static_cast<Uint8>(std::min(200, fade_alpha)));
    SDL_RenderFillRect(renderer, &overlay);

    // All text is centered. Use the top 1/3 for death messages, middle for summary.
    auto text_area = screen.cursor.inset(screen.cursor.w / 6, 0);
    auto layout = ui::Layout::from_rect(text_area, line_h);
    layout.skip(screen_h / 4);

    // "You have died." -- fades in after 500ms
    int text_alpha = std::max(0, std::min(255, static_cast<int>((elapsed_ms - 500) * 255 / 1500)));
    if (elapsed_ms >= 500) {
        SDL_Color red = {200, 50, 50, static_cast<Uint8>(text_alpha)};
        auto row = layout.row(title_h + 8);
        TTF_Font* death_font = font_title ? font_title : font;
        SDL_Surface* surf = TTF_RenderText_Blended(death_font, "You have died.", red);
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(text_alpha));
            SDL_Rect dst = {row.cx() - surf->w / 2, row.y, surf->w, surf->h};
            SDL_RenderCopy(renderer, tex, nullptr, &dst);
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        }
    }

    // Killed-by line -- fades in after 1s
    int cause_alpha = std::max(0, std::min(255, static_cast<int>((elapsed_ms - 1000) * 255 / 1000)));
    if (elapsed_ms >= 1000 && !death_cause.empty()) {
        auto row = layout.row(line_h + 8);
        char cause_buf[128];
        snprintf(cause_buf, sizeof(cause_buf), "Killed by %s.", death_cause.c_str());
        SDL_Color cause_col = {180, 140, 140, static_cast<Uint8>(cause_alpha)};
        SDL_Surface* csurf = TTF_RenderText_Blended(font, cause_buf, cause_col);
        if (csurf) {
            SDL_Texture* ctex = SDL_CreateTextureFromSurface(renderer, csurf);
            SDL_SetTextureAlphaMod(ctex, static_cast<Uint8>(cause_alpha));
            SDL_Rect cdst = {row.cx() - csurf->w / 2, row.y, csurf->w, csurf->h};
            SDL_RenderCopy(renderer, ctex, nullptr, &cdst);
            SDL_DestroyTexture(ctex);
            SDL_FreeSurface(csurf);
        }
    }

    // God-flavored death text -- fades in after 1.5s
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
        auto row = layout.row(line_h + 8);
        SDL_Color dim = {160, 120, 120, static_cast<Uint8>(god_alpha)};
        SDL_Surface* dsurf = TTF_RenderText_Blended(font, death_line, dim);
        if (dsurf) {
            SDL_Texture* dtex = SDL_CreateTextureFromSurface(renderer, dsurf);
            SDL_SetTextureAlphaMod(dtex, static_cast<Uint8>(god_alpha));
            SDL_Rect ddst = {row.cx() - dsurf->w / 2, row.y, dsurf->w, dsurf->h};
            SDL_RenderCopy(renderer, dtex, nullptr, &ddst);
            SDL_DestroyTexture(dtex);
            SDL_FreeSurface(dsurf);
        }
    }

    // Brand fading line -- fades in after 2s
    int brand_alpha = std::max(0, std::min(255, static_cast<int>((elapsed_ms - 2000) * 255 / 1500)));
    if (elapsed_ms >= 2000) {
        const char* brand_line = "The brand fades from your face. The Reliquary will find another.";
        auto row = layout.row(line_h + 16);
        SDL_Color brand_col = {180, 160, 100, static_cast<Uint8>(brand_alpha)};
        SDL_Surface* bsurf = TTF_RenderText_Blended(font, brand_line, brand_col);
        if (bsurf) {
            SDL_Texture* btex = SDL_CreateTextureFromSurface(renderer, bsurf);
            SDL_SetTextureAlphaMod(btex, static_cast<Uint8>(brand_alpha));
            SDL_Rect bdst = {row.cx() - bsurf->w / 2, row.y, bsurf->w, bsurf->h};
            SDL_RenderCopy(renderer, btex, nullptr, &bdst);
            SDL_DestroyTexture(btex);
            SDL_FreeSurface(bsurf);
        }
    }

    // Run summary -- fades in after 2.5s
    if (elapsed_ms >= 2500) {
        render_run_summary(renderer, font, layout.cursor, summary);
    }

    // Newly unlocked classes
    if (!newly_unlocked.empty()) {
        int uy = screen_h * 3 / 4;
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
                           int god_id, const std::vector<std::string>& newly_unlocked,
                           const RunSummary& summary) {
    int line_h = font ? TTF_FontLineSkip(font) : 20;
    auto screen = ui::Layout::from_screen(screen_w, screen_h, line_h);

    ui::draw_overlay(renderer, screen_w, screen_h);

    if (!font || !font_title) return;

    auto text_area = screen.cursor.inset(screen.cursor.w / 6, 0);
    auto layout = ui::Layout::from_rect(text_area, line_h);
    layout.skip(screen_h / 4);

    // God-specific ending
    const char* title = "You hold the Reliquary.";
    const char* ending = nullptr;
    GodId god = static_cast<GodId>(god_id);
    switch (god) {
        case GodId::VETHRIK:   ending = "Vethrik claims the Reliquary.\nThe dead lie still. The undead crumble to dust.\nThe graveyards are quiet again.\nIt is done."; break;
        case GodId::THESSARKA: ending = "Thessarka takes the Reliquary.\nEvery secret in the world is laid bare.\nThe price is madness. You pay it gladly."; break;
        case GodId::MORRETH:   ending = "Morreth takes the Reliquary.\nThe wars end. The strong rule.\nYou are the strongest. That is enough."; break;
        case GodId::YASHKHET:  ending = "Yashkhet takes the Reliquary.\nThe blood price is paid in full.\nYour hands will never stop shaking."; break;
        case GodId::KHAEL:     ending = "Khael takes the Reliquary.\nThe forest reclaims the cities.\nMankind is no longer the dominant species."; break;
        case GodId::SOLETH:    ending = "Soleth takes the Reliquary.\nThe Sepulchre burns. The old places burn.\nEverything unclean burns.\nThere is a lot of burning."; break;
        case GodId::IXUUL:     ending = "Ixuul takes the Reliquary.\nIt becomes something else. So does everything.\nThe world is unrecognizable by morning."; break;
        case GodId::ZHAVEK:    ending = "Zhavek takes the Reliquary.\nIt disappears. The gods cannot find it.\nNeither can you."; break;
        case GodId::THALARA:   ending = "Thalara takes the Reliquary.\nThe sea rises. The lowlands flood.\nThe age of land is over."; break;
        case GodId::OSSREN:    ending = "Ossren takes the Reliquary.\nIt is sealed in iron and stone.\nNo one will ever open it again."; break;
        case GodId::LETHIS:    ending = "Lethis takes the Reliquary.\nThe world falls asleep.\nSome of it wakes up. Most does not."; break;
        case GodId::GATHRUUN:  ending = "Gathruun takes the Reliquary.\nIt sinks into the earth.\nThe mountains grow taller. The tunnels go deeper."; break;
        case GodId::SYTHARA:   ending = "Sythara takes the Reliquary.\nIt decays. So does everything else.\nThis was always going to happen."; break;
        default:               ending = "No god claims the Reliquary.\nYou hold it in faithless hands.\nIt is yours. You are not sure what that means."; break;
    }

    const char* brand_conclusion = "The brand burns white. Then it goes out forever.";

    // Render title
    SDL_Color gold = {255, 220, 100, 255};
    auto title_row = layout.row(TTF_FontLineSkip(font_title) + 8);
    SDL_Surface* title_surf = TTF_RenderText_Blended(font_title, title, gold);
    if (title_surf) {
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, title_surf);
        SDL_Rect dst = {title_row.cx() - title_surf->w / 2, title_row.y, title_surf->w, title_surf->h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        SDL_DestroyTexture(tex);
        SDL_FreeSurface(title_surf);
    }

    // Brand conclusion
    SDL_Color brand_col = {200, 180, 120, 255};
    auto brand_row = layout.row(line_h + 8);
    SDL_Surface* bsurf = TTF_RenderText_Blended(font, brand_conclusion, brand_col);
    if (bsurf) {
        SDL_Texture* btex = SDL_CreateTextureFromSurface(renderer, bsurf);
        SDL_Rect bdst = {brand_row.cx() - bsurf->w / 2, brand_row.y, bsurf->w, bsurf->h};
        SDL_RenderCopy(renderer, btex, nullptr, &bdst);
        SDL_DestroyTexture(btex);
        SDL_FreeSurface(bsurf);
    }

    // Ending text line by line
    SDL_Color text_col = {200, 190, 170, 255};
    const char* p = ending;
    while (p && *p) {
        char line_buf[256];
        int len = 0;
        while (*p && *p != '\n' && len < 255) {
            line_buf[len++] = *p++;
        }
        line_buf[len] = '\0';
        if (*p == '\n') p++;

        if (len > 0) {
            auto row = layout.row(24);
            SDL_Surface* surf = TTF_RenderText_Blended(font, line_buf, text_col);
            if (surf) {
                SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
                SDL_Rect dst = {row.cx() - surf->w / 2, row.y, surf->w, surf->h};
                SDL_RenderCopy(renderer, tex, nullptr, &dst);
                SDL_DestroyTexture(tex);
                SDL_FreeSurface(surf);
            }
        }
    }

    layout.skip(10);

    // Run summary
    render_run_summary(renderer, font, layout.cursor, summary);

    // Newly unlocked classes
    if (!newly_unlocked.empty()) {
        int uy = screen_h * 3 / 4 + 30;
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
    ui::draw_text_centered(renderer, font, "Press any key.", dim, screen_w / 2, screen_h - 40);
}
