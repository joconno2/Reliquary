#include "systems/npc_interaction.h"
#include "systems/god_system.h"
#include "core/ecs.h"
#include "core/rng.h"
#include "core/audio.h"
#include "ui/message_log.h"
#include "ui/shop_screen.h"
#include "ui/quest_offer.h"
#include "ui/levelup_screen.h"
#include "systems/particles.h"
#include "components/npc.h"
#include "components/god.h"
#include "components/stats.h"
#include "components/position.h"
#include "components/dynamic_quest.h"
#include "data/world_data.h"
#include "components/quest.h"
#include "save/meta.h"
#include <cstdio>
#include <cmath>
#include <algorithm>

namespace npc_interaction {

bool interact(Context& ctx, Entity target, int target_x, int target_y) {
    if (!ctx.world.has<NPC>(target)) return false;
    auto& npc = ctx.world.get<NPC>(target);

    // Check if bumping this NPC satisfies any delivery quest's target town
    {
        auto& dq_pool = ctx.world.pool<DynamicQuest>();
        for (size_t di = 0; di < dq_pool.size(); di++) {
            auto& dq = dq_pool.at_index(di);
            if (dq.accepted && !dq.completed && dq.target_town_x >= 0 && !dq.reached_target) {
                if (std::abs(target_x - dq.target_town_x) < 30 &&
                    std::abs(target_y - dq.target_town_y) < 30) {
                    dq.reached_target = true;
                }
            }
        }
    }

    // Shop difficulty from town distance to Thornwall
    auto calc_shop_diff = [&]() -> int {
        if (ctx.dungeon_level > 0) return 0;
        if (!ctx.world.has<Position>(target)) return 0;
        auto& sp = ctx.world.get<Position>(target);
        float d = std::sqrt(static_cast<float>((sp.x - 1000) * (sp.x - 1000) +
                                                (sp.y - 750) * (sp.y - 750)));
        return std::min(8, static_cast<int>(d / 80.0f));
    };

    // Province god for shop stock variety
    auto calc_province_god = [&]() -> GodId {
        if (!ctx.world.has<Position>(target)) return GodId::NONE;
        auto& sp = ctx.world.get<Position>(target);
        return get_town_god(sp.x, sp.y);
    };

    // Shop pricing: excommunication, god faction loyalty/rivalry
    auto calc_shop_price_mult = [&]() -> int {
        if (!ctx.world.has<GodAlignment>(ctx.player)) return 100;
        auto& a = ctx.world.get<GodAlignment>(ctx.player);
        if (a.god != GodId::NONE && a.favor <= -100) return 200;
        if (npc.god_affiliation != GodId::NONE && a.god != GodId::NONE) {
            if (a.god == npc.god_affiliation) return 85;
            return 125;
        }
        return 100;
    };

    // Shopkeeper — open shop screen (unless they have a dynamic quest to offer)
    if (npc.role == NPCRole::SHOPKEEPER && !ctx.world.has<DynamicQuest>(target)) {
        int pm = calc_shop_price_mult();
        if (pm >= 200)
            ctx.log.add("The merchant eyes you with suspicion and doubles the prices.", {200, 160, 80, 255});
        else if (pm > 100)
            ctx.log.add("The merchant seems wary of your faith. Prices are higher.", {200, 180, 120, 255});
        else if (pm < 100)
            ctx.log.add("The merchant gives you a knowing nod. A fellow believer.", {140, 200, 160, 255});
        ctx.shop_screen.open(ctx.player, ctx.world, ctx.rng, &ctx.gold, calc_shop_diff(), pm, calc_province_god());
        return true;
    }
    // Merchant with dynamic quest: show dialogue + quest, then can shop next time
    if (npc.role == NPCRole::SHOPKEEPER && ctx.world.has<DynamicQuest>(target)) {
        auto& dq = ctx.world.get<DynamicQuest>(target);
        if (dq.completed) {
            int pm = calc_shop_price_mult();
            if (pm >= 200)
                ctx.log.add("The merchant eyes you with suspicion and doubles the prices.", {200, 160, 80, 255});
            else if (pm > 100)
                ctx.log.add("The merchant seems wary of your faith. Prices are higher.", {200, 180, 120, 255});
            else if (pm < 100)
                ctx.log.add("The merchant gives you a knowing nod. A fellow believer.", {140, 200, 160, 255});
            ctx.shop_screen.open(ctx.player, ctx.world, ctx.rng, &ctx.gold, calc_shop_diff(), pm, calc_province_god());
            return true;
        }
    }

    // Show NPC dialogue
    char buf[256];
    snprintf(buf, sizeof(buf), "%s: \"%s\"", npc.name.c_str(), npc.dialogue.c_str());
    ctx.log.add(buf, {180, 180, 140, 255});

    // God-aware NPC reactions (priests/scholars react to player's god)
    if (npc.role == NPCRole::PRIEST && ctx.world.has<GodAlignment>(ctx.player)) {
        auto& align = ctx.world.get<GodAlignment>(ctx.player);
        if (align.favor <= -100) {
            auto& ginfo = get_god_info(align.god);
            char gbuf[128];
            snprintf(gbuf, sizeof(gbuf),
                "The priest recoils. \"Begone, excommunicate! %s has forsaken you!\"", ginfo.name);
            ctx.log.add(gbuf, {220, 60, 60, 255});
            return true;
        }
        // Hostile faction: Soleth priests attack Ixuul followers on sight
        if (npc.god_affiliation == GodId::SOLETH && align.god == GodId::IXUUL) {
            ctx.log.add("\"Servant of the Formless! The Pale Flame will purge you!\"",
                     {255, 220, 100, 255});
            return true;
        }
        // Same-god priest: free healing
        if (npc.god_affiliation != GodId::NONE && align.god == npc.god_affiliation) {
            auto& ginfo = get_god_info(align.god);
            auto& ps = ctx.world.get<Stats>(ctx.player);
            int heal = std::min(10, ps.hp_max - ps.hp);
            ps.hp += heal;
            char gbuf[128];
            if (heal > 0)
                snprintf(gbuf, sizeof(gbuf),
                    "\"Welcome, child of %s. Be restored.\" (+%d HP)", ginfo.name, heal);
            else
                snprintf(gbuf, sizeof(gbuf),
                    "\"Walk in %s's light, faithful one.\"", ginfo.name);
            ctx.log.add(gbuf, {ginfo.color.r, ginfo.color.g, ginfo.color.b, 255});
        }
        // Rival-god priest: wary
        else if (npc.god_affiliation != GodId::NONE && align.god != GodId::NONE
                 && align.god != npc.god_affiliation) {
            auto& ninfo = get_god_info(npc.god_affiliation);
            auto& pinfo = get_god_info(align.god);
            char gbuf[128];
            snprintf(gbuf, sizeof(gbuf),
                "\"This is %s's domain. Followers of %s are... tolerated.\"", ninfo.name, pinfo.name);
            ctx.log.add(gbuf, {180, 140, 140, 255});
        }
        // Specific god warnings (flavor)
        else if (align.god == GodId::IXUUL) {
            ctx.log.add("The priest eyes you warily. \"Your god is... unwelcome here.\"",
                     {180, 140, 140, 255});
        } else if (align.god == GodId::YASHKHET) {
            ctx.log.add("The priest flinches at your scars. \"The Wound's followers unsettle me.\"",
                     {180, 140, 140, 255});
        } else if (align.god == GodId::NONE) {
            ctx.log.add("The priest looks at you with pity. \"No god watches over you.\"",
                     {160, 150, 140, 255});
        } else if (align.favor > 50) {
            auto& ginfo = get_god_info(align.god);
            char gbuf[128];
            snprintf(gbuf, sizeof(gbuf),
                "The priest nods respectfully. \"%s's favor is strong in you.\"", ginfo.name);
            ctx.log.add(gbuf, {160, 180, 200, 255});
        }
    }

    // Show direction hint if quest is active
    if (npc.quest_id >= 0) {
        auto hint_qid = static_cast<QuestId>(npc.quest_id);
        if (ctx.journal.has_quest(hint_qid) &&
            ctx.journal.get_state(hint_qid) == QuestState::ACTIVE) {
            const char* hint = get_quest_hint(hint_qid);
            if (hint) {
                char hbuf[256];
                snprintf(hbuf, sizeof(hbuf), "%s: \"%s\"", npc.name.c_str(), hint);
                ctx.log.add(hbuf, {200, 190, 150, 255});
            }
        }
    }

    // Quest giving — static quests with prerequisite chaining
    if (npc.quest_id >= 0) {
        auto qid = static_cast<QuestId>(npc.quest_id);

        // Check prerequisite: for main quests, the previous quest must be FINISHED
        auto quest_prereq = [](QuestId id) -> QuestId {
            int idx = static_cast<int>(id);
            if (idx <= 0 || idx > static_cast<int>(QuestId::MQ_17_CLAIM_RELIQUARY))
                return QuestId::COUNT;
            return static_cast<QuestId>(idx - 1);
        };
        auto prereq = quest_prereq(qid);
        bool prereq_met = (prereq == QuestId::COUNT) ||
                          (ctx.journal.has_quest(prereq) &&
                           ctx.journal.get_state(prereq) == QuestState::FINISHED);

        if (prereq_met && !ctx.journal.has_quest(qid)) {
            // Show quest offer modal
            ctx.quest_offer.show(qid, npc.name);
            ctx.pending_quest_npc = target;
        } else if (ctx.journal.get_state(qid) == QuestState::COMPLETE) {
            ctx.journal.set_state(qid, QuestState::FINISHED);
            auto& qinfo = get_quest_info(qid);
            char qbuf[128];
            snprintf(qbuf, sizeof(qbuf), "Quest complete: %s (+%dxp, +%dgold)",
                     qinfo.name, qinfo.xp_reward, qinfo.gold_reward);
            ctx.log.add(qbuf, {120, 220, 120, 255});
            ctx.audio.play(SfxId::QUEST);
            ctx.gold += qinfo.gold_reward;
            god_system::adjust_favor(ctx.world, ctx.player, ctx.log, 5);
            if (ctx.world.has<Stats>(ctx.player) && qinfo.xp_reward > 0) {
                if (ctx.world.get<Stats>(ctx.player).grant_xp(qinfo.xp_reward)) {
                    ctx.pending_levelup = true;
                    ctx.levelup_screen.open(ctx.player, ctx.rng);
                    ctx.audio.play(SfxId::LEVELUP);
                    { auto& lp = ctx.world.get<Position>(ctx.player); ctx.particles.levelup_effect(lp.x, lp.y); }
                }
            }
        }
    }

    // Dynamic quest handling
    if (ctx.world.has<DynamicQuest>(target)) {
        auto& dq = ctx.world.get<DynamicQuest>(target);
        if (!dq.accepted) {
            // Offer the quest
            char qbuf[256];
            snprintf(qbuf, sizeof(qbuf), "[Quest] %s", dq.name.c_str());
            ctx.log.add(qbuf, {220, 200, 100, 255});
            ctx.log.add(dq.description.c_str(), {180, 170, 140, 255});
            snprintf(qbuf, sizeof(qbuf), "Objective: %s", dq.objective.c_str());
            ctx.log.add(qbuf, {160, 160, 130, 255});
            snprintf(qbuf, sizeof(qbuf), "Reward: %d XP, %d gold", dq.xp_reward, dq.gold_reward);
            ctx.log.add(qbuf, {160, 160, 130, 255});
            dq.accepted = true;
            dq.accepted_turn = ctx.game_turn;
            ctx.log.add("Quest accepted.", {120, 220, 120, 255});
        } else if (!dq.completed) {
            if (dq.conditions_met(ctx.game_turn)) {
                // Conditions satisfied — complete the quest
                dq.completed = true;
                ctx.meta.total_quests_completed++;
                char qbuf[256];
                snprintf(qbuf, sizeof(qbuf), "Quest complete: %s (+%dxp, +%dgold)",
                         dq.name.c_str(), dq.xp_reward, dq.gold_reward);
                ctx.log.add(qbuf, {120, 220, 120, 255});
                ctx.audio.play(SfxId::QUEST);
                ctx.log.add(dq.complete_text.c_str(), {180, 170, 140, 255});
                ctx.gold += dq.gold_reward;
                if (ctx.world.has<Stats>(ctx.player) && dq.xp_reward > 0) {
                    if (ctx.world.get<Stats>(ctx.player).grant_xp(dq.xp_reward)) {
                        ctx.pending_levelup = true;
                        ctx.levelup_screen.open(ctx.player, ctx.rng);
                        ctx.audio.play(SfxId::LEVELUP);
                    }
                }
            } else {
                // Tell the player what's still needed
                if (dq.requires_dungeon && !dq.visited_dungeon)
                    ctx.log.add("\"Come back when you've actually been down there.\"", {180, 170, 140, 255});
                else if (dq.target_town_x >= 0 && !dq.reached_target)
                    ctx.log.add("\"You haven't made the journey yet. Go.\"", {180, 170, 140, 255});
                else
                    ctx.log.add("\"It hasn't been long enough. These things take time.\"", {180, 170, 140, 255});
            }
        }
    }
    return true;
}

} // namespace npc_interaction
