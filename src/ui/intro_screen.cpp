#include "ui/intro_screen.h"
#include "ui/ui_draw.h"
#include <cstdio>
#include <algorithm>

void IntroScreen::start(GodId god) {
    done_ = false;
    skipping_ = false;
    start_time_ = SDL_GetTicks();
    lines_.clear();

    auto& ginfo = get_god_info(god);
    SDL_Color nar  = {180, 170, 150, 255};
    SDL_Color dark = {140, 130, 110, 255};
    SDL_Color warm = {200, 140, 100, 255};
    SDL_Color hint = {220, 200, 100, 255};
    SDL_Color god_col = (god != GodId::NONE)
        ? SDL_Color{ginfo.color.r, ginfo.color.g, ginfo.color.b, 255}
        : SDL_Color{160, 160, 160, 255};

    // Each line: text, color, delay after previous, fade-in duration
    lines_.push_back({"You wake face-down in the dirt outside Thornwall.",    nar,  800,  1200});
    lines_.push_back({"You don't remember how you got here.",                 nar,  600,  1000});

    if (god != GodId::NONE) {
        char buf[128];
        snprintf(buf, sizeof(buf), "There is a brand on your face. It glows %s, the color of %s.",
                 ginfo.color.r > 200 ? "warm" : ginfo.color.b > 150 ? "cold" : "faintly",
                 ginfo.name);
        lines_.push_back({buf, god_col, 1000, 1500});
    } else {
        lines_.push_back({"There is a brand on your face. It glows pale, bound to nothing.", dark, 1000, 1500});
    }

    lines_.push_back({"It was not there before. You are sure of that much.",  dark, 800,  1000});
    lines_.push_back({"",                                                      nar,  400,   200});
    lines_.push_back({"The ground trembled last night. Everyone felt it.",     nar,  600,  1000});
    lines_.push_back({"Something pulsed from deep underground, east of town.", nar,  400,  1000});
    lines_.push_back({"The Barrow has been restless since. The dead are walking.", warm, 600, 1200});
    lines_.push_back({"",                                                      nar,  400,   200});
    lines_.push_back({"An elder watches you from the road. He sees the brand.", {200, 180, 120, 255}, 800, 1200});
    lines_.push_back({"He knows what it means, or thinks he does.",            dark, 400,  1000});
    lines_.push_back({"",                                                      nar,  400,   200});
    lines_.push_back({"Find the elder. He may know what the brand means.",     hint, 800,  1200});
}

bool IntroScreen::handle_input(SDL_Event& event) {
    if (done_) return false;

    if (event.type == SDL_KEYDOWN || event.type == SDL_MOUSEBUTTONDOWN) {
        if (skipping_) {
            // Second press finishes immediately
            done_ = true;
            return true;
        }
        // First press: skip to all lines visible
        skipping_ = true;
        return true;
    }

    return true; // consume all input while intro is showing
}

void IntroScreen::render(SDL_Renderer* renderer, TTF_Font* font, [[maybe_unused]] TTF_Font* font_title,
                          int w, int h) const {
    if (done_ || !font) return;

    Uint32 now = SDL_GetTicks();
    Uint32 elapsed = now - start_time_;
    int line_h = TTF_FontLineSkip(font);

    // Black background
    SDL_SetRenderDrawColor(renderer, 8, 6, 10, 255);
    SDL_RenderClear(renderer);

    // Layout: center text vertically, 60% screen width
    int content_w = w * 3 / 5;
    int total_text_h = static_cast<int>(lines_.size()) * (line_h + 6);
    int start_y = (h - total_text_h) / 2;

    // Calculate the start time for each line
    Uint32 line_start = 0;
    bool all_visible = true;

    for (int i = 0; i < static_cast<int>(lines_.size()); i++) {
        auto& ln = lines_[i];
        line_start += (i == 0) ? 0 : static_cast<Uint32>(ln.delay_ms);

        // Skip empty spacer lines
        if (ln.text.empty()) {
            start_y += line_h / 2;
            line_start += static_cast<Uint32>(ln.fade_ms);
            continue;
        }

        float alpha;
        if (skipping_) {
            alpha = 1.0f;
        } else if (elapsed < line_start) {
            alpha = 0.0f;
            all_visible = false;
        } else {
            Uint32 line_elapsed = elapsed - line_start;
            alpha = std::min(1.0f, static_cast<float>(line_elapsed) / ln.fade_ms);
            if (alpha < 1.0f) all_visible = false;
        }

        line_start += static_cast<Uint32>(ln.fade_ms);

        if (alpha <= 0.0f) continue;

        SDL_Color col = {ln.color.r, ln.color.g, ln.color.b,
                         static_cast<Uint8>(alpha * 255)};

        // Render with alpha
        SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(font, ln.text.c_str(), col,
                                                            static_cast<Uint32>(content_w));
        if (surf) {
            SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
            SDL_SetTextureAlphaMod(tex, static_cast<Uint8>(alpha * 255));
            SDL_Rect dst = {(w - surf->w) / 2, start_y, surf->w, surf->h};
            SDL_RenderCopy(renderer, tex, nullptr, &dst);
            start_y += surf->h + 6;
            SDL_DestroyTexture(tex);
            SDL_FreeSurface(surf);
        } else {
            start_y += line_h + 6;
        }
    }

    // "Press any key" hint at bottom, fades in after all text is visible
    if (skipping_ || all_visible) {
        float hint_alpha = skipping_ ? 1.0f :
            std::min(1.0f, static_cast<float>(elapsed - line_start) / 1000.0f);
        if (hint_alpha > 0.0f) {
            SDL_Color hint_col = {100, 95, 85, static_cast<Uint8>(hint_alpha * 200)};
            ui::draw_text_centered(renderer, font, "Press any key to begin.",
                                    hint_col, w / 2, h - line_h * 2);
        }

        // Auto-finish after hint has been visible for 30 seconds (safety)
        if (!skipping_ && elapsed > line_start + 30000) {
            const_cast<IntroScreen*>(this)->done_ = true;
        }
    }
}
