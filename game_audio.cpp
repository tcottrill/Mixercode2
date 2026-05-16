#include "game_audio.h"
#include "mixer.h"
#include "sys_fileio.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>

namespace GameAudio {

    // -------------------------------------------------------------------------
    // Configuration
    // -------------------------------------------------------------------------
    static constexpr int   AUDIO_RATE_HZ   = 44100;
    static constexpr int   AUDIO_FPS       = 60;

    // Channel layout. Music gets its own dedicated channel so the SFX
    // round-robin pool can't clobber it.
    static constexpr int   FIRST_SFX_CH    = 0;
    static constexpr int   LAST_SFX_CH     = 18;   // 19 SFX channels (0..18)
    static constexpr int   MUSIC_CH        = 19;   // dedicated music channel

    // Mix levels (0.0 .. 1.0).
    static constexpr float MASTER_VOLUME   = 0.7f;
    static constexpr float SFX_VOLUME      = 1.0f;
    static constexpr float MUSIC_VOLUME    = 0.5f; // default; SetMusicVolume overrides

    // Pause/resume fade duration (milliseconds).
    static constexpr int   MUSIC_FADE_MS   = 50;

    // -------------------------------------------------------------------------
    // Sound table
    // -------------------------------------------------------------------------
    struct SoundEntry {
        int   sampleId  = -1;
        int   baseFreq  = AUDIO_RATE_HZ;
        float baseVol   = 1.0f;
    };

    static std::unordered_map<std::string, SoundEntry> g_sounds;
    static int  g_nextChannel = FIRST_SFX_CH;
    static bool g_initialized = false;

    // -------------------------------------------------------------------------
    // Music state
    // -------------------------------------------------------------------------
    enum class MusicState { Empty, Playing, Paused, Stopped };

    static int        g_musicSampleId = -1;
    static MusicState g_musicState    = MusicState::Empty;
    static float      g_musicVolume   = MUSIC_VOLUME; // 0..1, the "active" level

    // -------------------------------------------------------------------------
    // Helpers
    // -------------------------------------------------------------------------
    static int NextChannel() {
        int ch = g_nextChannel;
        g_nextChannel++;
        if (g_nextChannel > LAST_SFX_CH) g_nextChannel = FIRST_SFX_CH;
        return ch;
    }

    static float RandRange(float lo, float hi) {
        float t = (float)rand() / (float)RAND_MAX;
        return lo + t * (hi - lo);
    }

    static int VolFloatToByte(float v) {
        if (v < 0.0f) v = 0.0f;
        int b = (int)(v * 255.0f + 0.5f);
        if (b > 255) b = 255;
        return b;
    }

    static bool LoadSound(const char* id, const char* filepath, float baseVolume) {
        const int sid = loadSampleFromFileOrZip(nullptr, filepath, /*force_resample=*/true);
        if (sid < 0) {
            std::cout << "[GameAudio] Failed to load: " << filepath << std::endl;
            return false;
        }

        SoundEntry entry;
        entry.sampleId = sid;
        entry.baseVol  = baseVolume;
        entry.baseFreq = AUDIO_RATE_HZ;

        g_sounds[id] = entry;
        std::cout << "[GameAudio] Loaded '" << id << "' from " << filepath
                  << " (sample id " << sid << ")" << std::endl;
        return true;
    }

    static void PlayInternal(const char* id, float volume, float pitch) {
        auto it = g_sounds.find(id);
        if (it == g_sounds.end()) {
            std::cout << "[GameAudio] Sound '" << id << "' not found" << std::endl;
            return;
        }
        const SoundEntry& e = it->second;

        const int ch = NextChannel();
        sample_start(ch, e.sampleId, /*loop=*/0);

        const float finalVol = volume * e.baseVol * SFX_VOLUME;
        sample_set_volume(ch, VolFloatToByte(finalVol));

        if (pitch < 0.999f || pitch > 1.001f) {
            sample_set_freq(ch, (int)(e.baseFreq * pitch));
        }
    }

    static void Play(const char* id, float volume, float pitchVariance) {
        float pitch = 1.0f;
        if (pitchVariance > 0.0f) {
            pitch += RandRange(-pitchVariance, pitchVariance);
        }
        PlayInternal(id, volume, pitch);
    }

    static void PlayPitch(const char* id, float volume, float pitch) {
        PlayInternal(id, volume, pitch);
    }

    // -------------------------------------------------------------------------
    // Public API -- core
    // -------------------------------------------------------------------------
    void Initialize() {
        if (g_initialized) {
            std::cout << "[GameAudio] Already initialized" << std::endl;
            return;
        }

        if (!mixer_init(AUDIO_RATE_HZ, AUDIO_FPS)) {
            std::cout << "Warning: Audio system failed to initialize" << std::endl;
            return;
        }

        mixer_set_master_volume((int)(MASTER_VOLUME * 100.0f + 0.5f));

        g_initialized = true;

        // Basic movement sounds
        LoadSound("jump",          "sounds/jump.wav",          0.7f);
        LoadSound("land",          "sounds/land.wav",          0.6f);
        LoadSound("bump",          "sounds/bump.wav",          0.4f);
        LoadSound("dash",          "sounds/dash.wav",          1.0f);
        LoadSound("slide",         "sounds/slide.wav",         0.35f);

        // Combat / Hazards
        LoadSound("hit",           "sounds/hit.wav",           0.8f);
        LoadSound("stun",          "sounds/stun.wav",          0.8f);
        LoadSound("death",         "sounds/death.wav",         1.0f);
        LoadSound("spike",         "sounds/spike.wav",         0.9f);
        LoadSound("bullet_hit",    "sounds/bullet_hit.wav",    0.7f);
        LoadSound("break",         "sounds/break.wav",         0.9f);

        // Collectibles & Environment
        LoadSound("coin",          "sounds/coin.wav",          0.6f);
        LoadSound("spring",        "sounds/spring.wav",        0.8f);
        LoadSound("splash",        "sounds/splash.wav",        0.7f);
        LoadSound("trigger",       "sounds/trigger.wav",       0.5f);
        LoadSound("exit",          "sounds/exit.wav",          1.0f);
        LoadSound("checkpoint",    "sounds/checkpoint.wav",    0.9f);

        // Power-ups
        LoadSound("shield_pickup", "sounds/shield_pickup.wav", 0.9f);
        LoadSound("shield_break",  "sounds/shield_break.wav",  1.0f);

        std::cout << "[GameAudio] Assets loaded successfully" << std::endl;
    }

    void Tick() {
        if (!g_initialized) return;
        mixer_update();
    }

    void PlayCollisionSound(CollisionType type, int coinsCollected) {
        if (!g_initialized) return;

        switch (type) {
        case COL_WALL_BUMP:
            Play("bump", 0.3f, 0.15f);
            break;
        case COL_WALL_LAND:
            Play("land", 0.7f, 0.1f);
            break;
        case COL_ENEMY:
            Play("hit", 1.0f, 0.08f);
            break;
        case COL_SPIKE:
            Play("spike", 1.0f, 0.0f);
            break;
        case COL_WATER:
            Play("splash", 0.9f, 0.12f);
            break;
        case COL_COIN: {
            float pitch = 1.0f + (coinsCollected % 10) * 0.05f;
            PlayPitch("coin", 0.8f, pitch);
            break;
        }
        case COL_EXIT:
            Play("exit", 1.2f, 0.0f);
            break;
        case COL_JUMP:
            Play("jump", 0.7f, 0.08f);
            break;
        case COL_SPRING:
            PlayPitch("spring", 1.0f, 1.2f);
            break;
        case COL_TRIGGER:
            Play("trigger", 0.6f, 0.0f);
            break;
        case COL_BULLET_HIT_SOLID:
            Play("bullet_hit", 0.7f, 0.1f);
            break;
        case COL_BULLET_HIT_PLAYER:
        case COL_DEATH:
            Play("death", 1.0f, 0.0f);
            break;
        case COL_BREAK:
            Play("break", 1.0f, 0.1f);
            break;
        case COL_CHECKPOINT:
            Play("checkpoint", 0.9f, 0.0f);
            break;
        case COL_DASH_ATTACK:
            Play("dash", 1.0f, 0.05f);
            break;
        case COL_SHIELD_PICKUP:
            Play("shield_pickup", 1.0f, 0.0f);
            break;
        case COL_SHIELD_BREAK:
            Play("shield_break", 1.0f, 0.0f);
            break;
        case COL_DASH_KILL:
            Play("break", 1.2f, 0.05f);
            break;
        case COL_LAUNCHER:
            PlayPitch("spring", 1.0f, 1.5f);
            break;
        }
    }

    // -------------------------------------------------------------------------
    // Public API -- music
    // -------------------------------------------------------------------------
    void LoadMusic(const char* filepath) {
        if (!g_initialized) return;

        // Replace any currently-loaded track. The mixer's sample_remove is a
        // stub, so the previous sample's bytes will leak until mixer_end()
        // -- acceptable for a handful of tracks across a session.
        if (g_musicState != MusicState::Empty) {
            sample_stop(MUSIC_CH);
        }

        const int sid = loadSampleFromFileOrZip(nullptr, filepath, /*force_resample=*/true);
        if (sid < 0) {
            std::cout << "[GameAudio] Failed to load music: " << filepath << std::endl;
            g_musicSampleId = -1;
            g_musicState = MusicState::Empty;
            return;
        }

        g_musicSampleId = sid;
        g_musicState    = MusicState::Stopped;
        std::cout << "[GameAudio] Music loaded: " << filepath
                  << " (sample id " << sid << ")" << std::endl;
    }

    void PlayMusic() {
        if (!g_initialized || g_musicSampleId < 0) return;

        // sample_start with loop=1 -> XAUDIO2_LOOP_INFINITE
        sample_start(MUSIC_CH, g_musicSampleId, /*loop=*/1);
        sample_set_volume(MUSIC_CH, VolFloatToByte(g_musicVolume));
        g_musicState = MusicState::Playing;
    }

    void StopMusic() {
        if (!g_initialized || g_musicSampleId < 0) return;
        sample_stop(MUSIC_CH);
        g_musicState = MusicState::Stopped;
    }

    void PauseMusic() {
        if (!g_initialized || g_musicState != MusicState::Playing) return;
        // Fade to silence; the loop keeps running underneath so position
        // stays in sync. Cheaper than tearing down + rebuilding the voice.
        mixer_ramp_volume(MUSIC_CH, MUSIC_FADE_MS, 0);
        g_musicState = MusicState::Paused;
    }

    void ResumeMusic() {
        if (!g_initialized) return;
        if (g_musicState != MusicState::Paused) return;
        mixer_ramp_volume(MUSIC_CH, MUSIC_FADE_MS, VolFloatToByte(g_musicVolume));
        g_musicState = MusicState::Playing;
    }

    void SetMusicVolume(float v) {
        if (v < 0.0f) v = 0.0f;
        g_musicVolume = v;
        // Only push to the channel if music is actively playing -- if paused,
        // ResumeMusic() will pick up the new value when it ramps back up.
        if (g_initialized && g_musicState == MusicState::Playing) {
            sample_set_volume(MUSIC_CH, VolFloatToByte(v));
        }
    }

    bool IsMusicPlaying() {
        return g_initialized && g_musicState == MusicState::Playing;
    }

    // -------------------------------------------------------------------------
    // Shutdown
    // -------------------------------------------------------------------------
    void Shutdown() {
        if (!g_initialized) return;
        samples_stop_all();
        g_sounds.clear();
        g_musicSampleId = -1;
        g_musicState = MusicState::Empty;
        mixer_end();
        g_initialized = false;
        std::cout << "[GameAudio] System shut down" << std::endl;
    }

} // namespace GameAudio
