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
    // Main quest line (17 steps)
    MQ_01_BARROW_WIGHT = 0,      // Kill the wight in the barrow near Thornwall
    MQ_02_SCHOLAR_CLUE,           // Speak to the scholar in Thornwall about what stirred
    MQ_03_ASHFORD_TABLET,         // Travel to Ashford — find a stone tablet in the Warrens nearby
    MQ_04_GREYWATCH_WARNING,      // Deliver the tablet to the captain in Greywatch
    MQ_05_STONEKEEP_DEPTHS,       // Descend Stonekeep to find the First Inscription
    MQ_06_FROSTMERE_SAGE,         // Travel north to Frostmere — consult the ice sage
    MQ_07_FROZEN_KEY,             // Retrieve the Frozen Key from the ice dungeon near Frostmere
    MQ_08_CATACOMBS_GATE,         // Use the Frozen Key to open the Catacombs gate
    MQ_09_OSSUARY_FRAGMENT,       // Find the first Reliquary Fragment in the Catacombs
    MQ_10_IRONHEARTH_FORGE,       // Travel to Ironhearth — have the fragment analyzed
    MQ_11_MOLTEN_TRIAL,           // Descend the Molten Depths to find the second fragment
    MQ_12_CANDLEMERE_RITUAL,      // Travel to Candlemere — the Soleth priests know the binding ritual
    MQ_13_SUNKEN_FRAGMENT,        // Retrieve the third fragment from the Sunken Halls
    MQ_14_HOLLOWGATE_SEAL,        // Travel to Hollowgate — break the final seal
    MQ_15_THE_SEPULCHRE,           // Enter The Sepulchre — the deepest mega-dungeon (6 levels)
    MQ_16_THE_DESCENT,             // The Sepulchre goes deeper than it should
    MQ_17_CLAIM_RELIQUARY,        // Claim the Reliquary — the game's climax

    // Side quests
    SQ_RAT_CELLAR,         // Clear rats from the shop cellar
    SQ_LOST_AMULET,        // Find a lost amulet in the Warrens
    SQ_UNDEAD_PATROL,      // Destroy 10 undead in the Catacombs
    SQ_KILL_BEAR,          // Hunter asks to kill a dangerous bear in the wilderness
    SQ_DELIVER_WEAPON,     // Blacksmith asks to deliver a sword to the guard captain in Greywatch
    SQ_HERB_GATHERING,     // Herbalist needs 3 herbs from the wilderness
    SQ_MISSING_PERSON,     // Farmer's son went into the Warrens and hasn't returned

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
         "A wight has risen in the barrow just east of Thornwall. "
         "The elder wants it put down before more die.",
         "Go east from Thornwall. Enter The Barrow. Kill the wight on floor 3.",
         "The wight is dead. Something deeper stirred when it fell.",
         true, 50, 30},

        // MQ_02_SCHOLAR_CLUE
        {"What Stirs Below",
         "Something answered from the deep when the wight died. "
         "The scholar in Thornwall reads texts no one else can.",
         "Speak to Scholar Aldric in Thornwall. He is in one of the town buildings.",
         "The scholar spoke of a tablet in the ruins near Ashford, "
         "to the west. A warning older than the gods.",
         true, 60, 0},

        // MQ_03_ASHFORD_TABLET
        {"The Ashford Tablet",
         "A stone tablet in the Ashford Ruins, west of Thornwall. "
         "The ruins are near the town of Ashford in the western Heartlands.",
         "Travel west to Ashford. Enter the dungeon nearby. Find the tablet on floor 3.",
         "The tablet is heavy. The inscriptions are old.",
         true, 80, 20},

        // MQ_04_GREYWATCH_WARNING
        {"The Captain's Warning",
         "The tablet describes a seal failing beneath the hills. "
         "The guard captain in Greywatch has the largest garrison. "
         "Greywatch lies to the east-northeast of Thornwall, in the Pale Reach.",
         "Travel northeast to Greywatch. Show the tablet to Captain Voss.",
         "The captain read the tablet. 'Stonekeep,' he said. "
         "'Northeast of here. Something has been scratching at the walls.'",
         true, 100, 40},

        // MQ_05_STONEKEEP_DEPTHS
        {"The First Inscription",
         "Stonekeep is northeast of Greywatch. Sealed for generations, "
         "now unsealed by the captain's order.",
         "Go northeast to Stonekeep. Descend to the bottom. Find the inscription.",
         "The inscription is burned into the stone.",
         true, 120, 50},

        // MQ_06_FROSTMERE_SAGE
        {"The Ice Sage",
         "The inscription mentions the name Vehlkyr. Nobody in the Heartlands knows it. "
         "In Frostmere, due north of Thornwall in the Pale Reach, "
         "an old sage studies forbidden things.",
         "Travel north to Frostmere. Speak to Sage Yeva.",
         "The sage said Vehlkyr was the first to find the Reliquary. "
         "He did not survive.",
         true, 140, 30},

        // MQ_07_FROZEN_KEY
        {"The Frozen Key",
         "A key forged in cold lies in the ice dungeon near Frostmere, "
         "north-northeast of Thornwall in the Pale Reach.",
         "Go to Frostmere Depths, north of Frostmere. Find the Frozen Key.",
         "The key burns cold. It fits no lock you have seen.",
         true, 160, 0},

        // MQ_08_CATACOMBS_GATE
        {"The Sealed Gate",
         "The Catacombs are southwest of Thornwall, near Millhaven. "
         "The gate has been sealed since before the town was built. "
         "The Frozen Key opens it.",
         "Travel southwest to The Catacombs near Millhaven. Use the key.",
         "The gate opened. Something has been waiting.",
         true, 180, 0},

        // MQ_09_OSSUARY_FRAGMENT
        {"The First Fragment",
         "The Catacombs go deeper than the maps show. "
         "The first fragment of the Reliquary is on the lowest floor.",
         "Descend The Catacombs (southwest of Thornwall). Find the fragment.",
         "The fragment is warm. Two more remain.",
         true, 200, 60},

        // MQ_10_IRONHEARTH_FORGE
        {"The Master Smith",
         "The fragment is unknown material. The master smith in Ironhearth, "
         "far to the east on the Iron Coast, works metals others won't touch.",
         "Travel far east to Ironhearth on the Iron Coast. Show the fragment to the smith.",
         "The smith said there are two more pieces. Without all three, you hold nothing.",
         true, 220, 50},

        // MQ_11_MOLTEN_TRIAL
        {"The Molten Trial",
         "The second fragment is in the Molten Depths, volcanic tunnels "
         "east of Ironhearth on the Iron Coast.",
         "Enter The Molten Depths east of Ironhearth. Find the second fragment.",
         "The second fragment is cold even in the furnace. Two of three.",
         true, 260, 80},

        // MQ_12_CANDLEMERE_RITUAL
        {"The Binding Ritual",
         "Two fragments must be bound. The Soleth priests in Candlemere, "
         "far northeast in the Pale Reach, preserve the old rituals.",
         "Travel far northeast to Candlemere in the Pale Reach. Learn the ritual.",
         "The priests taught you the words and the price.",
         true, 280, 40},

        // MQ_13_SUNKEN_FRAGMENT
        {"The Drowned Shard",
         "The third fragment is in the Sunken Halls, flooded ruins "
         "far to the northeast, past Candlemere on the Iron Coast border.",
         "Enter The Sunken Halls, far northeast. Find the third fragment.",
         "Three fragments. The Reliquary is almost whole.",
         true, 320, 100},

        // MQ_14_HOLLOWGATE_SEAL
        {"The Final Seal",
         "Hollowgate is far to the west, deep in the Greenwood. "
         "A seal bars the entrance to the Sepulchre. "
         "The fragments will break it.",
         "Travel far west to Hollowgate in the Greenwood. Break the seal.",
         "The seal broke. The way down is open.",
         true, 360, 0},

        // MQ_15_THE_SEPULCHRE
        {"Enter The Sepulchre",
         "The Sepulchre lies beneath the far north, past the Pale Reach, "
         "in the Frozen Marches. The oldest place in the world.",
         "Travel far north. Enter The Sepulchre.",
         "Each floor is older than the last. You keep going down.",
         true, 400, 0},

        // MQ_16_THE_DESCENT
        {"The Descent",
         "The Sepulchre is 13 floors deep. What waits at the bottom "
         "has been there since before the gods.",
         "Descend past floor 4 of The Sepulchre.",
         "The Reliquary vault is open.",
         true, 450, 0},

        // MQ_17_CLAIM_RELIQUARY
        {"Claim the Reliquary",
         "You see it. A vessel of light that hurts to look at. Something that "
         "was here before the gods. Your god reaches through you. Other paragons "
         "are close. Take it.",
         "Claim the Reliquary.",
         "You hold the Reliquary. The world changes.",
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
        case QuestId::MQ_04_GREYWATCH_WARNING:
        case QuestId::MQ_06_FROSTMERE_SAGE:
        case QuestId::MQ_08_CATACOMBS_GATE:
        case QuestId::MQ_10_IRONHEARTH_FORGE:
        case QuestId::MQ_12_CANDLEMERE_RITUAL:
            return true;
        default:
            return false;
    }
}

// Direction hints shown when bumping a quest NPC while their quest is active
inline const char* get_quest_hint(QuestId id) {
    switch (id) {
        // Main quests — dungeon directions
        case QuestId::MQ_01_BARROW_WIGHT:
            return "The barrow is east of town. Look for the stairs down among the old stones.";
        case QuestId::MQ_03_ASHFORD_TABLET:
            return "The Warrens lie just east of here, in the old ruins. The tablet should be on the lowest level.";
        case QuestId::MQ_05_STONEKEEP_DEPTHS:
            return "Stonekeep is west of Greywatch. An old fortress in the hills — you can't miss it.";
        case QuestId::MQ_07_FROZEN_KEY:
            return "The ice dungeon is southeast of Frostmere. The cold gets worse the deeper you go.";
        case QuestId::MQ_09_OSSUARY_FRAGMENT:
            return "The Catacombs are north of Millhaven. The fragment lies deep — search the lowest level.";
        case QuestId::MQ_11_MOLTEN_TRIAL:
            return "The Molten Depths are southeast of Ironhearth. Follow the heat.";
        case QuestId::MQ_13_SUNKEN_FRAGMENT:
            return "The Sunken Halls are southeast of Candlemere. The water there doesn't behave naturally.";
        case QuestId::MQ_14_HOLLOWGATE_SEAL:
            return "The seal is east of town. The fragments will guide you.";

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
