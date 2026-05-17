// =============================================================================
// music.h
// Background music subsystem for the mixer.
//
//
// OVERVIEW
// --------
//   Decode-to-memory model: each track is decoded once via the mixer's
//   load_sample_from_buffer and held as PCM in RAM until unload(). For a 2D
//   game with a handful of tracks this is the right tradeoff: tens of MB
//   per 3-minute track, single-digit to low-triple-digit milliseconds of
//   decode cost on modern CPUs, zero per-frame decode work, and gapless
//   looping for free.
//
//   Two voice-path mixer channels are reserved (defaults: 18 and 19) so
//   that fade_to() can keep the outgoing and incoming tracks running
//   simultaneously during a crossfade. All fades use the mixer's existing
//   volume-sweep system; no new threads are started.
//
//
// LIFECYCLE
// ---------
//   1. mixer_init(rate, fps)
//   2. Music::init()
//   ... game runs ...
//   3. Music::shutdown()
//   4. mixer_end()
//
//   It is fine to call Music::shutdown() and Music::init() multiple times
//   inside a single mixer session (for example, when toggling music on
//   and off from a settings menu).
//
//
// QUICK START
// -----------
//   Music::init();                                  // after mixer_init
//
//   int title = Music::load_from_file("audio/title.ogg");
//   if (title < 0) { /* handle load failure */ }
//
//   Music::set_volume(70);                          // 0..100 music master
//   Music::play(title);                             // hard start, loops
//
//   // later, swap tracks with a 2 second crossfade:
//   int level1 = Music::load_from_file("audio/level1.ogg");
//   Music::fade_to(level1, 2000);
//
//   // pause/resume for a menu screen:
//   Music::pause();
//   Music::resume();
//
//   // fade out at game over:
//   Music::stop(500);
//
//   Music::shutdown();                              // before mixer_end
//
//
// LOADING TRACKS
// --------------
//   int load(data, size, name)        from a memory buffer (preferred for
//                                     pak / zip backed games).
//   int load_from_file(path, name)    convenience wrapper using std::ifstream.
//   void unload(music_id)             drop the registry slot; PCM frees
//                                     when no channel is pinning it.
//
//   Format is detected from the file header:
//     WAV    always supported
//     OGG    supported if OGG_DECODE is defined in the mixer translation unit
//     MP3    supported if MP3_DECODE is defined in the mixer translation unit
//
//   The decoded PCM is resampled to the system rate (SYS_FREQ) at load
//   time, so playback pays no per-frame resampling cost. load() blocks for
//   the decode duration; call it from a loading screen or a worker thread
//   if the wait would be visible.
//
//
// PLAYBACK
// --------
//   void play(id, loop)               hard cut. Replaces any active track.
//   void fade_to(id, ms, loop)        crossfade over ms (or fade-in from
//                                     silence if nothing is playing).
//   void stop(ms)                     stop the active track. ms == 0 is
//                                     immediate; ms > 0 fades to silence.
//   void force_stop()                 destroy both music voices and drop
//                                     sample references. Use to reclaim a
//                                     channel left silent by stop(ms>0).
//
//   Looping defaults to true on play() and fade_to(); pass loop=false for
//   a one-shot stinger or cutscene cue.
//
//
// VOLUME
// ------
//   void set_volume(percent)          0..100. Music master, applied
//                                     immediately to the active track.
//                                     Persists across play / fade_to /
//                                     pause / resume / shutdown.
//   int  get_volume()                 returns the current value.
//
//   Independent of mixer_set_master_volume(), which scales everything
//   including SFX. Typical pattern: keep mixer master near 100, and let
//   the player choose music and SFX volumes separately.
//
//
// PAUSE / RESUME
// --------------
//   void pause(ms)    fade the active track to silence over ms. The
//                     underlying voice keeps looping so resume() picks up
//                     at the same playback position.
//   void resume(ms)   fade back to the music master volume over ms.
//
//   Both are no-ops if not in the appropriate state (nothing playing for
//   pause; not paused for resume).
//
//
// THREADING
// ---------
//   All Music:: functions take an internal mutex and are safe to call
//   from any thread without external coordination. The functions do not
//   block on I/O except for load() and load_from_file(), which block for
//   the duration of the file read plus decode (typically tens to a few
//   hundred milliseconds for a multi-minute track).
//
//
// CHANNEL ALLOCATION
// ------------------
//   By default Music uses voice-path channels 18 (A) and 19 (B). If your
//   SFX pool uses channels 0..17 (or any subset that excludes 18 and 19)
//   nothing will collide.
//
//   To override, call set_channels(a, b) before init(), or while no music
//   is active. Both must differ and be in [0, MAX_CHANNELS).
//
//
// COMMON PATTERNS
// ---------------
//   Title screen, then level music:
//     Music::play(title);
//     ... when level loads ...
//     Music::fade_to(level1, 1500);
//
//   Boss intro and outro:
//     Music::fade_to(boss, 500);          // quick punch-in
//     ... boss defeated ...
//     Music::fade_to(level1, 2000);       // slower return to normal
//
//   Music ducking under dialog:
//     int prev = Music::get_volume();
//     Music::set_volume(prev / 3);        // duck to a third
//     ... play dialog ...
//     Music::set_volume(prev);            // restore
//
//   Toggle music from settings (without losing position):
//     Music::pause();                     // off
//     Music::resume();                    // on
//
//   Full unload between scenes:
//     Music::stop();
//     Music::unload(prev_track);          // optional, frees decoded PCM
//
//   Async load while menu music plays:
//     std::future<int> next = std::async(std::launch::async, []{
//         return Music::load_from_file("audio/level2.ogg");
//     });
//     ... show loading screen, run menu music ...
//     int id = next.get();
//     if (id >= 0) Music::fade_to(id, 1000);
//
//
// GOTCHAS
// -------
//   load() does NOT deduplicate. Each call decodes and registers a fresh
//   sample even if the same bytes were loaded before. Cache the returned
//   id in your asset system instead of reloading.
//
//   After stop(ms > 0) the music channel is silent but still parked in
//   the mix loop until the next play() / fade_to() / force_stop() reuses
//   or reaps it. CPU cost is trivial. If you want the channel truly idle
//   (for example, before a long pause or a save-and-quit), call force_stop().
//
//   is_playing() returns false the moment stop() is called, even during
//   the fade-out tail. The audible tail is driven independently by the
//   mixer's sweep worker.
//
//   set_channels() with music currently active is rejected (logs an
//   error). Call it before init() or after a stop() / shutdown().
//
//   load() blocks for the decode. For big tracks called outside a loading
//   screen, wrap in std::async (see COMMON PATTERNS above).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace Music {

	// One-time setup. Resets internal state and configures the two music
	// channels. Safe to call multiple times; subsequent calls are no-ops.
	// Must be called after mixer_init.
	void init();

	// Stop active music and release internal state. Safe to call when
	// not inited. Does not call mixer_end. Call before mixer_end at
	// shutdown so the music voices are torn down cleanly.
	void shutdown();

	// Override which voice-path channels music uses. Defaults: 18 (A)
	// and 19 (B). Must be called before init() or while no music is
	// active. channel_a and channel_b must differ and both be valid
	// indices in [0, MAX_CHANNELS). Rejected with a log message on
	// invalid input or active-music violation.
	void set_channels(int channel_a, int channel_b);

	// Decode a track from a memory buffer and register it with the
	// mixer. Format is auto-detected from the header (WAV always; OGG
	// requires OGG_DECODE; MP3 requires MP3_DECODE). The decoded PCM
	// is resampled to the system rate.
	//
	// Returns the sample id (>= 0) on success, -1 on read or decode
	// failure. Caller may free the input buffer after this returns.
	//
	// Blocks for the decode duration. For multi-MB tracks this is in
	// the tens to low hundreds of milliseconds; call from a loading
	// screen or worker thread if visible.
	int load(const uint8_t* data, size_t size, const char* name = nullptr);

	// Convenience wrapper that reads the file from disk via std::ifstream
	// and forwards to load(). Returns -1 on file-open, read, or decode
	// failure. If name is null, the file path is used as the registry
	// label.
	int load_from_file(const char* path, const char* name = nullptr);

	// Forget a previously-loaded track. If the id is currently playing
	// (active or in a crossfade tail), it is stopped first. The decoded
	// PCM is freed when no channel is pinning it any longer. The id is
	// invalid after this call; load again to get a fresh id.
	void unload(int music_id);

	// Start playing music_id from the beginning. Hard-cuts any active
	// track (both music channels). loop=true is the typical setting for
	// looping BGM; loop=false for one-shot cues.
	void play(int music_id, bool loop = true);

	// Crossfade from the current track to music_id over fade_ms
	// milliseconds. If nothing is playing, behaves as a fade-in from
	// silence. fade_ms <= 0 falls back to play() (hard cut).
	void fade_to(int music_id, int fade_ms = 1000, bool loop = true);

	// Stop the active track. fade_ms == 0 stops immediately and frees
	// the music channel; fade_ms > 0 ramps to silence and leaves the
	// (silent) voice running until the next play() / fade_to() /
	// force_stop() reclaims it.
	//
	// is_playing() returns false after this call regardless of the
	// fade-out tail.
	void stop(int fade_ms = 0);

	// Immediately destroy both music voices and drop the sample
	// references. Use this when you want the music channels truly idle
	// (long pause between scenes, save-and-quit, etc.) and don't want
	// any voice state lingering from a prior stop(fade_ms > 0).
	void force_stop();

	// Music master volume in 0..100. Applied immediately to the active
	// track, persisted across play / fade_to / pause / resume.
	// Independent of mixer_set_master_volume.
	void set_volume(int percent);

	// Returns the current music master volume in 0..100.
	int get_volume();

	// Fade the active track to silence over fade_ms milliseconds. The
	// underlying voice keeps looping, so resume() picks up at the same
	// position. No-op if there is no active track or if already paused.
	void pause(int fade_ms = 50);

	// Fade the active track from silence back to the music master
	// volume over fade_ms milliseconds. No-op if not currently paused.
	void resume(int fade_ms = 50);

	// True if a track is currently playing (started via play() or
	// fade_to(), not stopped or paused). Returns false during the
	// fade-out tail of stop(fade_ms > 0).
	bool is_playing();

	// True if the active track is in the paused state (pause() was
	// called and resume() has not yet been called).
	bool is_paused();

	// Sample id of the currently-active track, or -1 if nothing is
	// loaded / playing / paused.
	int current();

} // namespace Music
