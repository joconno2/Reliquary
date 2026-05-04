#pragma once
#include <string>
#include <vector>

enum class QuestState : int {
    AVAILABLE,   // can be picked up
    ACTIVE,      // in progress
    COMPLETE,    // objectives met, turn in
    FINISHED,    // done and rewarded
};

enum class QuestId : int {
    // Main quest line (9 steps, tight loop)
    MQ_01_BARROW_WIGHT = 0,      // Kill the wight in the barrow (tutorial boss)
    MQ_02_SCHOLAR_CLUE,           // Talk to scholar, learn about fragments
    MQ_03_FIRST_FRAGMENT,         // Find first fragment in Stonekeep
    MQ_04_SAGE_COUNSEL,           // Talk to Frostmere sage about the Reliquary
    MQ_05_SECOND_FRAGMENT,        // Find second fragment in the Catacombs
    MQ_06_THIRD_FRAGMENT,         // Find third fragment in the Molten Depths
    MQ_07_BREAK_SEAL,             // Break the seal at Hollowgate
    MQ_08_ENTER_SEPULCHRE,        // Enter the Sepulchre
    MQ_09_CLAIM_RELIQUARY,        // Claim the Reliquary (final boss)

    // Side quests
    SQ_RAT_CELLAR,
    SQ_LOST_AMULET,
    SQ_UNDEAD_PATROL,
    SQ_KILL_BEAR,
    SQ_DELIVER_WEAPON,
    SQ_HERB_GATHERING,
    SQ_MISSING_PERSON,

    COUNT
};

constexpr int QUEST_COUNT = static_cast<int>(QuestId::COUNT);

struct QuestInfo {
    const char* name;
    const char* description;     // shown in quest log
    const char* objective;       // short current objective text
    const char* complete_text;   // shown when turning in
    bool is_main;                // main quest line vs side quest
    int xp_reward;
    int gold_reward;
};

inline const QuestInfo& get_quest_info(QuestId id) {
    static const QuestInfo QUESTS[] = {
        // MQ_01_BARROW_WIGHT
        {"The Barrow Wight",
         "The brand on your face burns when you face east. Something in the Barrow "
         "woke the same night you appeared. The elder says a wight walks there.",
         "Enter The Barrow east of Thornwall. Kill the wight on floor 3.",
         "The wight crumbles. Your brand flares. Something deeper answered.",
         true, 50, 30},

        // MQ_02_SCHOLAR_CLUE
        {"What Stirs Below",
         "When the wight died, your brand pulsed. Scholar Aldric in Thornwall "
         "studies old texts. He may know what the brand means.",
         "Speak to Scholar Aldric in Thornwall.",
         "Aldric went pale. 'The Reliquary. Three fragments, scattered in the deep places. "
         "Find them or the brand will consume you.'",
         true, 60, 0},

        // MQ_03_FIRST_FRAGMENT
        {"The First Fragment",
         "The first fragment lies in Stonekeep to the northeast. Your brand aches "
         "as you face that direction. The walls are groaning.",
         "Descend Stonekeep. Find the first fragment on the bottom floor.",
         "The fragment fused to your hand, then released. You can feel the others.",
         true, 120, 50},

        // MQ_04_SAGE_COUNSEL
        {"The Sage's Warning",
         "One fragment found. Sage Yeva in Frostmere to the north studies things "
         "older than the gods. She may know what the fragments do when assembled.",
         "Travel north to Frostmere. Speak to Sage Yeva.",
         "'Vehlkyr assembled it once. It killed him. The second fragment is in the "
         "Catacombs near Millhaven. The third burns in the Molten Depths.'",
         true, 140, 30},

        // MQ_05_SECOND_FRAGMENT
        {"The Second Fragment",
         "The Catacombs near Millhaven. Your brand burns brighter with each fragment. "
         "Something dead guards this one.",
         "Descend The Catacombs. Find the second fragment.",
         "Two of three. The fragments resonate. Your brand is changing.",
         true, 200, 60},

        // MQ_06_THIRD_FRAGMENT
        {"The Third Fragment",
         "The Molten Depths east of Ironhearth. Volcanic tunnels. "
         "Your brand glows hot enough to see by.",
         "Enter The Molten Depths. Find the third fragment.",
         "Three fragments. The Reliquary is whole. You feel it assembling inside you.",
         true, 280, 80},

        // MQ_07_BREAK_SEAL
        {"The Final Seal",
         "The Sepulchre is sealed beneath Hollowgate in the deep Greenwood. "
         "The assembled fragments will break it. Your brand is screaming.",
         "Travel to Hollowgate. Break the seal.",
         "The seal shattered. The descent is open.",
         true, 360, 0},

        // MQ_08_ENTER_SEPULCHRE
        {"The Sepulchre",
         "The oldest place in the world. Everything has led here. "
         "Your brand is a beacon now.",
         "Enter The Sepulchre.",
         "The air is wrong. Each floor is older than the last.",
         true, 400, 0},

        // MQ_09_CLAIM_RELIQUARY
        {"The Reliquary",
         "You see it. Not light. Not dark. Something from before the distinction. "
         "Your brand is burning. Your god is screaming. This is what you were made for.",
         "Claim the Reliquary on the bottom floor.",
         "You hold it. For a moment, you understand everything. Then you forget.",
         true, 500, 0},

        // SQ_RAT_CELLAR
        {"Rats in the Cellar",
         "The shopkeeper complains about rats in the cellar beneath the shop. "
         "Probably nothing dangerous.",
         "Kill 5 rats in the first dungeon level.",
         "The rats are dealt with. The shopkeeper is grateful.",
         false, 20, 15},

        // SQ_LOST_AMULET
        {"The Lost Amulet",
         "A farmer lost a family amulet somewhere in the Warrens. "
         "She'd like it back, if you happen to find it.",
         "Find the lost amulet in the Warrens.",
         "The farmer clutches the amulet. It's worthless, but she weeps anyway.",
         false, 25, 10},

        // SQ_UNDEAD_PATROL
        {"Undead Patrol",
         "The guard captain wants the undead numbers thinned in the Catacombs. "
         "Ten should make a difference. Maybe.",
         "Destroy 10 undead in the Catacombs.",
         "Ten fewer dead things walking. The captain nods.",
         false, 40, 25},

        // SQ_KILL_BEAR
        {"The Beast of the East",
         "A dangerous bear has been killing livestock east of Thornwall. "
         "The guard wants it dealt with before someone gets hurt.",
         "Kill the bear east of Thornwall.",
         "The bear is dead. The livestock are safe. For now.",
         false, 35, 20},

        // SQ_DELIVER_WEAPON
        {"Special Delivery",
         "The blacksmith has forged a sword for the guard captain in Greywatch. "
         "He needs someone to deliver it.",
         "Deliver the sword to the guard captain in Greywatch.",
         "The captain tests the blade and nods. Fine work.",
         false, 30, 25},

        // SQ_HERB_GATHERING
        {"Bitter Remedies",
         "The herbalist needs rare herbs that grow in the wilderness. "
         "Three bundles should be enough for a season's remedies.",
         "Gather 3 herb bundles from the wilderness.",
         "The herbalist smells each bundle carefully. These will do.",
         false, 30, 15},

        // SQ_MISSING_PERSON
        {"The Farmer's Son",
         "A farmer's son went into the Warrens looking for adventure. "
         "He hasn't come back. The farmer fears the worst.",
         "Find the farmer's son in the Warrens.",
         "The boy is shaken but alive. His father weeps with relief.",
         false, 50, 30},
    };
    return QUESTS[static_cast<int>(id)];
}

// "Talk to" quests auto-complete on acceptance (the conversation IS the objective)
inline bool is_auto_complete_quest(QuestId id) {
    switch (id) {
        case QuestId::MQ_02_SCHOLAR_CLUE:
        case QuestId::MQ_04_SAGE_COUNSEL:
            return true;
        default:
            return false;
    }
}

// Direction hints shown when bumping a quest NPC while their quest is active
inline const char* get_quest_hint(QuestId id) {
    switch (id) {
        case QuestId::MQ_01_BARROW_WIGHT:
            return "The barrow is east of town. Look for the stairs down.";
        case QuestId::MQ_03_FIRST_FRAGMENT:
            return "Stonekeep is northeast. The fragment is on the bottom floor.";
        case QuestId::MQ_05_SECOND_FRAGMENT:
            return "The Catacombs are near Millhaven. The fragment lies deep.";
        case QuestId::MQ_06_THIRD_FRAGMENT:
            return "The Molten Depths are east of Ironhearth. Follow the heat.";
        case QuestId::MQ_07_BREAK_SEAL:
            return "Hollowgate is to the west. The fragments will break the seal.";

        // Side quests
        case QuestId::SQ_RAT_CELLAR:
            return "The dungeon entrance is nearby. The rats are on the first level.";
        case QuestId::SQ_LOST_AMULET:
            return "Try the Warrens — the nearest dungeon. My grandmother's amulet... she'd want it back.";
        case QuestId::SQ_UNDEAD_PATROL:
            return "The Catacombs, south of here. Ten undead should thin their numbers.";
        case QuestId::SQ_KILL_BEAR:
            return "The bear's been seen east of town. Watch yourself.";
        case QuestId::SQ_DELIVER_WEAPON:
            return "Captain Voss is in Greywatch, to the northeast. He's expecting it.";
        case QuestId::SQ_HERB_GATHERING:
            return "The herbs grow in the wilderness outside town. Look in the brush and grass.";
        case QuestId::SQ_MISSING_PERSON:
            return "He went into the Warrens nearby. Please, bring him back.";

        default:
            return nullptr;
    }
}

// Player's quest journal
struct QuestJournal {
    struct Entry {
        QuestId id;
        QuestState state;
        int progress;    // kill count, items found, etc.
        int target;      // target count (0 = not count-based)
    };

    std::vector<Entry> entries;

    bool has_quest(QuestId id) const {
        for (auto& e : entries) if (e.id == id) return true;
        return false;
    }

    QuestState get_state(QuestId id) const {
        for (auto& e : entries) if (e.id == id) return e.state;
        return QuestState::AVAILABLE;
    }

    void add_quest(QuestId id, int target = 0) {
        if (!has_quest(id)) {
            entries.push_back({id, QuestState::ACTIVE, 0, target});
        }
    }

    void set_state(QuestId id, QuestState state) {
        for (auto& e : entries) {
            if (e.id == id) { e.state = state; return; }
        }
    }

    void add_progress(QuestId id, int amount = 1) {
        for (auto& e : entries) {
            if (e.id == id && e.state == QuestState::ACTIVE) {
                e.progress += amount;
                if (e.target > 0 && e.progress >= e.target) {
                    e.state = QuestState::COMPLETE;
                }
                return;
            }
        }
    }

    int count_completed() const {
        int n = 0;
        for (auto& e : entries)
            if (e.state == QuestState::COMPLETE || e.state == QuestState::FINISHED)
                n++;
        return n;
    }
};
