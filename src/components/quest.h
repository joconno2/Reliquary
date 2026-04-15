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
         "The brand on your face burns when you face east. Something in the Barrow "
         "woke the same night you appeared. The elder says a wight walks there.",
         "Go east from Thornwall. Enter The Barrow. Kill the wight on floor 3.",
         "The wight crumbles. Your brand flares. Something deeper answered.",
         true, 50, 30},

        // MQ_02_SCHOLAR_CLUE
        {"What Stirs Below",
         "When the wight died, your brand pulsed and something moved far below. "
         "Scholar Aldric in Thornwall studies old texts. He may know what the brand means.",
         "Speak to Scholar Aldric in Thornwall.",
         "Aldric went pale when he saw your face. 'That mark. There's a tablet in the "
         "ruins near Ashford. It describes what you are.'",
         true, 60, 0},

        // MQ_03_ASHFORD_TABLET
        {"The Ashford Tablet",
         "Aldric spoke of a stone tablet in the Ashford Ruins to the west. "
         "It predates the gods. It describes the brand.",
         "Travel west to Ashford. Enter the dungeon nearby. Find the tablet on floor 3.",
         "The tablet is cold. The words are in no language, but you can read them. "
         "'The Reliquary chooses. The branded are drawn. The seals will open.'",
         true, 80, 20},

        // MQ_04_GREYWATCH_WARNING
        {"The Captain's Warning",
         "The tablet says seals are failing beneath the land. Your brand is a key. "
         "Captain Voss in Greywatch commands the region's garrison.",
         "Travel northeast to Greywatch. Show the tablet to Captain Voss.",
         "Voss read the tablet twice. 'Stonekeep. Northeast. The walls have been "
         "groaning for weeks. Whatever is down there knows you're coming.'",
         true, 100, 40},

        // MQ_05_STONEKEEP_DEPTHS
        {"The First Inscription",
         "Stonekeep has been sealed for generations. Now the seals are failing. "
         "Your brand aches as you approach. Something is written on the deepest wall.",
         "Descend Stonekeep. Find the inscription on the bottom floor.",
         "The inscription burned itself into your mind before you finished reading. "
         "A name: Vehlkyr. The one who made the Reliquary.",
         true, 120, 50},

        // MQ_06_FROSTMERE_SAGE
        {"The Ice Sage",
         "Vehlkyr. The name means nothing to anyone alive. But in Frostmere, "
         "far north in the Frozen Marches, an old sage studies things older than the gods. "
         "Your brand pulls you north.",
         "Travel north to Frostmere. Speak to Sage Yeva.",
         "The sage said Vehlkyr was the first to find the Reliquary. "
         "He did not survive.",
         true, 140, 30},

        // MQ_07_FROZEN_KEY
        {"The Frozen Key",
         "The sage said Vehlkyr sealed the Reliquary behind locks of element and will. "
         "The first key is frozen in the ice dungeon near Frostmere. "
         "Your brand aches in the cold. It recognizes this place.",
         "Descend Frostmere Depths. Find the Frozen Key.",
         "The key burns cold in your hand. Your brand flares in response.",
         true, 160, 0},

        // MQ_08_CATACOMBS_GATE
        {"The Sealed Gate",
         "The Catacombs near Millhaven have been sealed since before the town existed. "
         "The Frozen Key fits the gate. Your brand is pulling you through.",
         "Travel southwest to The Catacombs near Millhaven. Use the key.",
         "The gate opened with a sound like a sigh. Something has been waiting for you.",
         true, 180, 0},

        // MQ_09_OSSUARY_FRAGMENT
        {"The First Fragment",
         "Deep in the Catacombs, past the dead, your brand burns brighter. "
         "A piece of the Reliquary is here. You can feel it in your skull.",
         "Descend The Catacombs. Find the first fragment.",
         "The fragment fused to your hand for a moment, then released. "
         "You can feel the other two. South and east.",
         true, 200, 60},

        // MQ_10_IRONHEARTH_FORGE
        {"The Master Smith",
         "The fragment is made of something that shouldn't exist. The master smith "
         "in Ironhearth on the Iron Coast works metals others refuse. "
         "He might know what you're carrying.",
         "Travel far east to Ironhearth. Show the fragment to Master Smith Brynn.",
         "Brynn held the fragment to the light and said nothing for a long time. "
         "'Two more pieces. Get them all or this one will eat you alive.'",
         true, 220, 50},

        // MQ_11_MOLTEN_TRIAL
        {"The Molten Trial",
         "The second fragment is in the Molten Depths east of Ironhearth. "
         "Volcanic tunnels. Your brand glows hotter as you approach.",
         "Enter The Molten Depths. Find the second fragment.",
         "Two of three. The fragments hum when brought together. "
         "Your brand is changing. Growing brighter.",
         true, 260, 80},

        // MQ_12_CANDLEMERE_RITUAL
        {"The Binding Ritual",
         "The fragments need to be bound or they'll tear apart. "
         "The Soleth priests in Candlemere preserve rituals from before the schism. "
         "They may know how to stabilize what you carry.",
         "Travel to Candlemere in the Pale Reach. Learn the binding ritual.",
         "The priests taught you the words. They looked at your brand and wept.",
         true, 280, 40},

        // MQ_13_SUNKEN_FRAGMENT
        {"The Drowned Shard",
         "The third fragment lies in the Sunken Halls, flooded ruins past Candlemere. "
         "Something guards it. Something old. Your brand knows it.",
         "Enter The Sunken Halls. Find the third fragment.",
         "Three fragments. The Reliquary is nearly whole. "
         "You feel it assembling itself inside you.",
         true, 320, 100},

        // MQ_14_HOLLOWGATE_SEAL
        {"The Final Seal",
         "The Sepulchre entrance is sealed beneath Hollowgate in the deep Greenwood. "
         "The assembled fragments will break it. Your brand is screaming.",
         "Travel far west to Hollowgate. Break the seal with the fragments.",
         "The seal shattered. The descent is open. You feel it below you, "
         "vast and patient and aware.",
         true, 360, 0},

        // MQ_15_THE_SEPULCHRE
        {"The Sepulchre",
         "The oldest place in the world. Beneath the Frozen Marches, far north. "
         "Everything has been leading here. Your brand is a beacon now.",
         "Enter The Sepulchre.",
         "The air is wrong. Each floor is older than the last. "
         "Your brand illuminates the way.",
         true, 400, 0},

        // MQ_16_THE_DESCENT
        {"The Descent",
         "Deeper. The walls are smooth, carved by something that wasn't human. "
         "The Reliquary is close. You can hear it.",
         "Descend past floor 4 of The Sepulchre.",
         "The vault is open. It was always open. It was waiting for you.",
         true, 450, 0},

        // MQ_17_CLAIM_RELIQUARY
        {"The Reliquary",
         "You see it. Not light. Not dark. Something from before the distinction. "
         "Your brand is burning. Your god is screaming. The paragons are close. "
         "This is what you were made for.",
         "Claim the Reliquary.",
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
