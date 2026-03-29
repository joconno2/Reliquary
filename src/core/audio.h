#pragma once
#include <SDL2/SDL_mixer.h>
#include <string>
#include <unordered_map>

// One-shot sound effects
enum class SfxId {
    HIT1, HIT2, HIT3, CRIT, MISS,
    ARROW_FIRE, ARROW_HIT,
    DEATH,
    PICKUP, EQUIP, POTION, GOLD,
    SPELL, HEAL, PRAYER,
    LEVELUP, QUEST,
    DOOR, REST, STAIRS,
    POISON, BURN, CURSE,
    SELECT,
    COUNT
};

// Music tracks (played via Mix_Music, one at a time)
enum class MusicId {
    NONE = -1,
    TITLE,
    OVERWORLD1, OVERWORLD2, OVERWORLD3,
    TOWN1, TOWN2,
    DUNGEON1, DUNGEON2, DUNGEON3,
    DUNGEON_DEEP1, DUNGEON_DEEP2, DUNGEON_DEEP3,
    SEPULCHRE,
    BOSS, BOSS2,
    DEATH, DEATH2,
    VICTORY, VICTORY2,
    MUSIC_COUNT
};

constexpr int MUSIC_TRACK_COUNT = static_cast<int>(MusicId::MUSIC_COUNT);

// Looping ambient layers (played on dedicated channels, can overlap)
enum class AmbientId {
    NONE = -1,
    CAVE,
    CAVE_RAIN,
    FOREST_DAY,
    FOREST_DAY_RAIN,
    FOREST_NIGHT,
    FOREST_NIGHT_RAIN,
    INTERIOR_DAY,
    INTERIOR_NIGHT,
    INTERIOR_NIGHT_RAIN,
    FIRE_CRACKLE,
    RIVER,
    AMBIENT_COUNT
};

constexpr int AMBIENT_LAYER_COUNT = static_cast<int>(AmbientId::AMBIENT_COUNT);

class Audio {
public:
    bool init();
    void shutdown();

    // SFX (one-shot, any available channel)
    void play(SfxId id);
    void play_hit(); // random hit variant

    // Music (one track at a time, loops)
    void play_music(MusicId id, int fade_ms = 1000);
    void stop_music(int fade_ms = 1000);
    MusicId current_music() const { return current_music_; }

    // Ambient layers (up to 2 simultaneous, looping)
    void play_ambient(AmbientId id, int fade_ms = 500);
    void stop_ambient(int fade_ms = 500);
    void stop_all_ambient(int fade_ms = 500);

    // Volume control
    void set_volume(int vol); // 0-100 (affects SFX)
    int get_volume() const { return volume_; }
    void set_music_volume(int vol); // 0-100
    int get_music_volume() const { return music_volume_; }

private:
    // SFX
    Mix_Chunk* chunks_[static_cast<int>(SfxId::COUNT)] = {};

    // Music
    Mix_Music* music_tracks_[MUSIC_TRACK_COUNT] = {};
    MusicId current_music_ = MusicId::NONE;

    // Ambient layers (loaded as chunks, played on reserved channels)
    Mix_Chunk* ambient_chunks_[AMBIENT_LAYER_COUNT] = {};
    static constexpr int AMBIENT_CHANNEL_BASE = 14; // channels 14-15 reserved for ambient
    static constexpr int AMBIENT_CHANNEL_COUNT = 2;
    AmbientId ambient_playing_[AMBIENT_CHANNEL_COUNT] = {AmbientId::NONE, AmbientId::NONE};

    int volume_ = 45;
    int music_volume_ = 35;

    Mix_Chunk* load_chunk(const char* path);
    Mix_Music* load_music(const char* path);
};
