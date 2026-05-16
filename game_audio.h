#pragma once
#include "game_types.h" // Assuming this contains your CollisionType enum

namespace GameAudio {
    // Loads sound effects and configures master volumes
    void Initialize();

    // Cleans up the audio engine
    void Shutdown();

    // Pumps the mixer once per frame. Call from Game::Update().
    void Tick();

    // Plays the appropriate sound for a collision event
    // We pass coinsCollected because the coin sound pitch changes based on it!
    void PlayCollisionSound(CollisionType type, int coinsCollected = 0);

    // ---- Background music --------------------------------------------------
    // Loads a music track from disk. Does not auto-play. Calling this while
    // another track is loaded stops and replaces the previous track.
    void LoadMusic(const char* filepath);

    // Starts (or restarts) the loaded track from the beginning. Loops forever.
    void PlayMusic();

    // Immediate stop. Position is reset; PlayMusic() restarts from frame 0.
    void StopMusic();

    // Fades to silence but keeps the track running underneath. Position
    // continues to advance while paused (fine for a looping bed).
    void PauseMusic();

    // Fades back to the configured music volume.
    void ResumeMusic();

    // 0.0 .. 1.0+. Applied immediately. Stored as the "active" volume that
    // ResumeMusic() will fade back to.
    void SetMusicVolume(float v);

    // True if music is loaded and not paused/stopped.
    bool IsMusicPlaying();
}
