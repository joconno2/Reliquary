#include "core/audio.h"
#include <cstdio>
#include <cstdlib>

Mix_Chunk* Audio::load_chunk(const char* path) {
    Mix_Chunk* chunk = Mix_LoadWAV(path); // works for WAV, OGG, FLAC
    if (!chunk) {
        fprintf(stderr, "Audio: failed to load %s: %s\n", path, Mix_GetError());
    }
    return chunk;
}

Mix_Music* Audio::load_music(const char* path) {
    Mix_Music* mus = Mix_LoadMUS(path);
    if (!mus) {
        fprintf(stderr, "Audio: failed to load music %s: %s\n", path, Mix_GetError());
    }
    return mus;
}

bool Audio::init() {
    int mix_flags = MIX_INIT_MP3 | MIX_INIT_OGG;
    int mix_initted = Mix_Init(mix_flags);
    if ((mix_initted & mix_flags) != mix_flags) {
        fprintf(stderr, "Audio: Mix_Init incomplete (wanted 0x%x, got 0x%x): %s\n",
                mix_flags, mix_initted, Mix_GetError());
        // Continue anyway — WAV sfx may still work
    }
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        fprintf(stderr, "Audio: Mix_OpenAudio failed: %s\n", Mix_GetError());
        return false;
    }
    Mix_AllocateChannels(16);

    // Reserve channels 14-15 for ambient loops
    Mix_ReserveChannels(0); // unreserve all first
    // We'll just use channels 14-15 by explicit number

    // === Load SFX ===
    struct SfxDef { SfxId id; const char* file; };
    static const SfxDef DEFS[] = {
        // Combat — Fantasy SFX Pack (sword impacts, parries, bow)
        {SfxId::HIT1,         "assets/sfx/hit1.ogg"},
        {SfxId::HIT2,         "assets/sfx/hit2.ogg"},
        {SfxId::HIT3,         "assets/sfx/hit3.ogg"},
        {SfxId::CRIT,         "assets/sfx/crit.ogg"},
        {SfxId::MISS,         "assets/sfx/miss.ogg"},
        {SfxId::BLOCK1,       "assets/sfx/block1.ogg"},
        {SfxId::BLOCK2,       "assets/sfx/block2.ogg"},
        {SfxId::ARROW_FIRE,   "assets/sfx/bow_fire1.ogg"},
        {SfxId::ARROW_FIRE2,  "assets/sfx/bow_fire2.ogg"},
        {SfxId::ARROW_HIT,    "assets/sfx/bow_hit1.ogg"},
        {SfxId::ARROW_HIT2,   "assets/sfx/bow_hit2.ogg"},
        // Death (keep sox-generated — no pack equivalent)
        {SfxId::DEATH,        "assets/sfx/death.wav"},
        // Items
        {SfxId::PICKUP,       "assets/sfx/pickup.wav"},
        {SfxId::EQUIP,        "assets/sfx/equip.ogg"},
        {SfxId::POTION,       "assets/sfx/potion.wav"},
        {SfxId::GOLD,         "assets/sfx/gold.wav"},
        {SfxId::CHEST_OPEN,   "assets/sfx/chest_open.ogg"},
        {SfxId::CHEST_OPEN2,  "assets/sfx/chest_open2.ogg"},
        // Magic — per-school Fantasy SFX Pack + sox fallbacks
        {SfxId::SPELL,        "assets/sfx/spell.wav"},
        {SfxId::HEAL,         "assets/sfx/heal.wav"},
        {SfxId::PRAYER,       "assets/sfx/prayer.wav"},
        {SfxId::SPELL_FIRE,   "assets/sfx/spell_fire.ogg"},
        {SfxId::SPELL_ICE,    "assets/sfx/spell_ice.ogg"},
        {SfxId::SPELL_EARTH,  "assets/sfx/spell_earth.ogg"},
        {SfxId::SPELL_WATER,  "assets/sfx/spell_water.ogg"},
        {SfxId::SPELL_IMPACT, "assets/sfx/spell_impact.ogg"},
        {SfxId::SPELL_BUFF,   "assets/sfx/spell_buff.ogg"},
        {SfxId::SPELL_FREEZE, "assets/sfx/spell_freeze.ogg"},
        // Progression
        {SfxId::LEVELUP,      "assets/sfx/levelup.wav"},
        {SfxId::QUEST,        "assets/sfx/quest.wav"},
        // World interaction — Fantasy SFX Pack door
        {SfxId::DOOR,         "assets/sfx/door_open.ogg"},
        {SfxId::REST,         "assets/sfx/rest.wav"},
        {SfxId::STAIRS,       "assets/sfx/stairs.ogg"},
        // Status effects
        {SfxId::POISON,       "assets/sfx/poison.wav"},
        {SfxId::BURN,         "assets/sfx/burn.wav"},
        {SfxId::CURSE,        "assets/sfx/curse.wav"},
        // Terrain footsteps — Fantasy SFX Pack
        {SfxId::STEP_STONE1,  "assets/sfx/step_stone1.ogg"},
        {SfxId::STEP_STONE2,  "assets/sfx/step_stone2.ogg"},
        {SfxId::STEP_STONE3,  "assets/sfx/step_stone3.ogg"},
        {SfxId::STEP_DIRT1,   "assets/sfx/step_dirt1.ogg"},
        {SfxId::STEP_DIRT2,   "assets/sfx/step_dirt2.ogg"},
        {SfxId::STEP_DIRT3,   "assets/sfx/step_dirt3.ogg"},
        {SfxId::STEP_WATER1,  "assets/sfx/step_water1.ogg"},
        {SfxId::STEP_WATER2,  "assets/sfx/step_water2.ogg"},
        {SfxId::STEP_WATER3,  "assets/sfx/step_water3.ogg"},
        {SfxId::STEP_WOOD1,   "assets/sfx/step_wood1.ogg"},
        {SfxId::STEP_WOOD2,   "assets/sfx/step_wood2.ogg"},
        {SfxId::STEP_WOOD3,   "assets/sfx/step_wood3.ogg"},
        // UI
        {SfxId::SELECT,       "assets/sfx/select.wav"},
    };
    for (auto& def : DEFS) {
        chunks_[static_cast<int>(def.id)] = load_chunk(def.file);
    }

    // === Load Music ===
    struct MusDef { MusicId id; const char* file; };
    static const MusDef MUS_DEFS[] = {
        {MusicId::TITLE,         "assets/music/title.mp3"},
        {MusicId::OVERWORLD1,    "assets/music/overworld1.mp3"},
        {MusicId::OVERWORLD2,    "assets/music/overworld2.mp3"},
        {MusicId::OVERWORLD3,    "assets/music/overworld3.mp3"},
        {MusicId::TOWN1,         "assets/music/town1.mp3"},
        {MusicId::TOWN2,         "assets/music/town2.mp3"},
        {MusicId::DUNGEON1,      "assets/music/dungeon1.mp3"},
        {MusicId::DUNGEON2,      "assets/music/dungeon2.mp3"},
        {MusicId::DUNGEON3,      "assets/music/dungeon3.mp3"},
        {MusicId::DUNGEON_DEEP1, "assets/music/dungeon_deep1.mp3"},
        {MusicId::DUNGEON_DEEP2, "assets/music/dungeon_deep2.mp3"},
        {MusicId::DUNGEON_DEEP3, "assets/music/dungeon_deep3.mp3"},
        {MusicId::SEPULCHRE,     "assets/music/sepulchre.mp3"},
        {MusicId::BOSS,          "assets/music/boss.mp3"},
        {MusicId::BOSS2,         "assets/music/boss2.mp3"},
        {MusicId::DEATH,         "assets/music/death.mp3"},
        {MusicId::DEATH2,        "assets/music/death2.mp3"},
        {MusicId::VICTORY,       "assets/music/victory.mp3"},
        {MusicId::VICTORY2,      "assets/music/victory2.mp3"},
    };
    for (auto& def : MUS_DEFS) {
        music_tracks_[static_cast<int>(def.id)] = load_music(def.file);
    }

    // === Load Ambient Loops ===
    struct AmbDef { AmbientId id; const char* file; };
    static const AmbDef AMB_DEFS[] = {
        {AmbientId::CAVE,              "assets/ambient/cave.ogg"},
        {AmbientId::CAVE_RAIN,         "assets/ambient/cave_rain.ogg"},
        {AmbientId::FOREST_DAY,        "assets/ambient/forest_day.ogg"},
        {AmbientId::FOREST_DAY_RAIN,   "assets/ambient/forest_day_rain.ogg"},
        {AmbientId::FOREST_NIGHT,      "assets/ambient/forest_night.ogg"},
        {AmbientId::FOREST_NIGHT_RAIN, "assets/ambient/forest_night_rain.ogg"},
        {AmbientId::INTERIOR_DAY,      "assets/ambient/interior_day.ogg"},
        {AmbientId::INTERIOR_NIGHT,    "assets/ambient/interior_night.ogg"},
        {AmbientId::INTERIOR_NIGHT_RAIN,"assets/ambient/interior_night_rain.ogg"},
        {AmbientId::FIRE_CRACKLE,      "assets/ambient/fire_crackle.ogg"},
        {AmbientId::RIVER,             "assets/ambient/river.ogg"},
    };
    for (auto& def : AMB_DEFS) {
        ambient_chunks_[static_cast<int>(def.id)] = load_chunk(def.file);
    }

    set_volume(volume_);
    set_music_volume(music_volume_);
    return true;
}

void Audio::shutdown() {
    stop_music(0);
    stop_all_ambient(0);

    for (auto& chunk : chunks_) {
        if (chunk) { Mix_FreeChunk(chunk); chunk = nullptr; }
    }
    for (auto& mus : music_tracks_) {
        if (mus) { Mix_FreeMusic(mus); mus = nullptr; }
    }
    for (auto& chunk : ambient_chunks_) {
        if (chunk) { Mix_FreeChunk(chunk); chunk = nullptr; }
    }
    Mix_CloseAudio();
    Mix_Quit();
}

void Audio::play(SfxId id) {
    int idx = static_cast<int>(id);
    if (idx >= 0 && idx < static_cast<int>(SfxId::COUNT) && chunks_[idx]) {
        Mix_PlayChannel(-1, chunks_[idx], 0);
    }
}

void Audio::play_hit() {
    SfxId hits[] = {SfxId::HIT1, SfxId::HIT2, SfxId::HIT3};
    play(hits[rand() % 3]);
}

void Audio::play_miss() {
    SfxId misses[] = {SfxId::MISS, SfxId::BLOCK1, SfxId::BLOCK2};
    play(misses[rand() % 3]);
}

void Audio::play_bow_fire() {
    play(rand() % 2 ? SfxId::ARROW_FIRE : SfxId::ARROW_FIRE2);
}

void Audio::play_bow_hit() {
    play(rand() % 2 ? SfxId::ARROW_HIT : SfxId::ARROW_HIT2);
}

void Audio::play_chest_open() {
    play(rand() % 2 ? SfxId::CHEST_OPEN : SfxId::CHEST_OPEN2);
}

void Audio::play_music(MusicId id, int fade_ms) {
    if (id == current_music_) return; // already playing
    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= MUSIC_TRACK_COUNT || !music_tracks_[idx]) return;

    if (Mix_PlayingMusic()) {
        Mix_FadeOutMusic(fade_ms);
        // SDL_mixer will stop the old track before starting the new one
    }
    Mix_FadeInMusic(music_tracks_[idx], -1, fade_ms); // -1 = loop forever
    current_music_ = id;
}

void Audio::stop_music(int fade_ms) {
    if (Mix_PlayingMusic()) {
        if (fade_ms > 0) Mix_FadeOutMusic(fade_ms);
        else Mix_HaltMusic();
    }
    current_music_ = MusicId::NONE;
}

void Audio::play_ambient(AmbientId id, int fade_ms) {
    int idx = static_cast<int>(id);
    if (idx < 0 || idx >= AMBIENT_LAYER_COUNT || !ambient_chunks_[idx]) return;

    // Check if already playing on a channel
    for (int c = 0; c < AMBIENT_CHANNEL_COUNT; c++) {
        if (ambient_playing_[c] == id) return; // already playing
    }

    // Find a free ambient channel
    for (int c = 0; c < AMBIENT_CHANNEL_COUNT; c++) {
        int ch = AMBIENT_CHANNEL_BASE + c;
        if (ambient_playing_[c] == AmbientId::NONE || !Mix_Playing(ch)) {
            ambient_playing_[c] = id;
            Mix_Volume(ch, (volume_ * MIX_MAX_VOLUME) / 800); // ambient very subtle
            if (fade_ms > 0)
                Mix_FadeInChannel(ch, ambient_chunks_[idx], -1, fade_ms);
            else
                Mix_PlayChannel(ch, ambient_chunks_[idx], -1); // -1 = loop forever
            return;
        }
    }
    // No free channel — replace the first one
    int ch = AMBIENT_CHANNEL_BASE;
    if (fade_ms > 0) Mix_FadeOutChannel(ch, fade_ms / 2);
    else Mix_HaltChannel(ch);
    ambient_playing_[0] = id;
    Mix_Volume(ch, (volume_ * MIX_MAX_VOLUME) / 200);
    if (fade_ms > 0)
        Mix_FadeInChannel(ch, ambient_chunks_[idx], -1, fade_ms);
    else
        Mix_PlayChannel(ch, ambient_chunks_[idx], -1);
}

void Audio::stop_ambient(int fade_ms) {
    // Stop the most recently started ambient (last channel)
    for (int c = AMBIENT_CHANNEL_COUNT - 1; c >= 0; c--) {
        if (ambient_playing_[c] != AmbientId::NONE) {
            int ch = AMBIENT_CHANNEL_BASE + c;
            if (fade_ms > 0) Mix_FadeOutChannel(ch, fade_ms);
            else Mix_HaltChannel(ch);
            ambient_playing_[c] = AmbientId::NONE;
            return;
        }
    }
}

void Audio::stop_all_ambient(int fade_ms) {
    for (int c = 0; c < AMBIENT_CHANNEL_COUNT; c++) {
        if (ambient_playing_[c] != AmbientId::NONE) {
            int ch = AMBIENT_CHANNEL_BASE + c;
            if (fade_ms > 0) Mix_FadeOutChannel(ch, fade_ms);
            else Mix_HaltChannel(ch);
            ambient_playing_[c] = AmbientId::NONE;
        }
    }
}

void Audio::set_volume(int vol) {
    volume_ = vol;
    int mix_vol = (vol * MIX_MAX_VOLUME) / 100;
    // Set volume on non-ambient channels only (0-13)
    for (int c = 0; c < AMBIENT_CHANNEL_BASE; c++) {
        Mix_Volume(c, mix_vol);
    }
    // Update ambient channels — very subtle
    for (int c = 0; c < AMBIENT_CHANNEL_COUNT; c++) {
        Mix_Volume(AMBIENT_CHANNEL_BASE + c, (vol * MIX_MAX_VOLUME) / 800);
    }
}

void Audio::set_music_volume(int vol) {
    music_volume_ = vol;
    Mix_VolumeMusic((vol * MIX_MAX_VOLUME) / 100);
}
