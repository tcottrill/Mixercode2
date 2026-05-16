// =============================================================================
// music.h
// Background-music subsystem built on top of the mixer's voice path.
//
// Decode-to-memory model: a track is loaded once (WAV / OGG / MP3 via
// load_sample_from_buffer), held as PCM, and played from RAM. For a 2D Steam
// game with a handful of tracks this is the right tradeoff: ~30 MB per
// 3-minute 44.1k stereo track, single-digit milliseconds of decode cost on
// modern CPUs, zero per-frame decode work, and trivially gapless looping.
//
// Two mixer channels are reserved (defaults: 18 and 19) so crossfades have
// both the outgoing and incoming track running simultaneously during the
// transition.
//
// USAGE
//   Music::init();
//
//   // Load tracks (typically from inside a loading screen):
//   std::vector<uint8_t> bytes = read_file("audio/level1.ogg");
//   int track_lvl1 = Music::load(bytes.data(), bytes.size(), "level1");
//
//   // Play (hard cut), or fade in:
//   Music::play(track_lvl1);                     // hard start at master volume
//   Music::fade_to(track_lvl1, /*ms=*/500);      // fade-in if nothing playing
//
//   // Crossfade between tracks:
//   Music::fade_to(track_boss, /*ms=*/2000);
//
//   // Volume / pause / stop:
//   Music::set_volume(70);
//   Music::pause();                              // short fade to silence
//   Music::resume();
//   Music::stop(/*ms=*/500);                     // fade out and stop
//
//   Music::shutdown();   // at quit, before mixer_end
//
// THREADING
// Music functions take an internal mutex; they're safe to call from any
// thread. They do not block on file I/O. load() blocks for the decode
// duration (call it from a worker thread or a loading screen for big files).
// =============================================================================
#pragma once

#include <cstddef>
#include <cstdint>

namespace Music {

	// One-time setup. Resets internal state and configures the two music
	// channels. Safe to call multiple times (subsequent calls are no-ops).
	// Call after mixer_init.
	void init();

	// Stop active music and release internal state. Safe to call when not
	// inited. Does not call mixer_end. Call before mixer_end at shutdown.
	void shutdown();

	// Override which voice-path channels music uses. Defaults are 18 and 19.
	// Must be called before init(), or while no music is playing. The two
	// channels must be distinct and within [0, MAX_CHANNELS).
	void set_channels(int channel_a, int channel_b);

	// Load a track from a memory buffer. Format is detected from the header
	// (WAV always; OGG if OGG_DECODE is defined; MP3 if MP3_DECODE is defined).
	// The track is decoded to PCM at the system sample rate; the SAMPLE is
	// registered with the mixer and stays alive until unload() (or
	// mixer_end). Returns the sample id (>= 0) on success, -1 on failure.
	//
	// name is an optional label used in the mixer's sample registry; pass
	// nullptr to auto-generate.
	int load(const uint8_t* data, size_t size, const char* name = nullptr);

	// Convenience: read a file from disk into a buffer and call load().
	// Returns -1 on file-not-found / read failure or on decode failure.
	int load_from_file(const char* path, const char* name = nullptr);

	// Drop a previously-loaded track from the mixer's sample registry. If
	// the track is currently playing, it is stopped first. The decoded PCM
	// is freed when no playback channel is pinning it.
	void unload(int music_id);

	// Start playing music_id. Replaces any currently-playing track ABRUPTLY
	// (hard cut). Use fade_to() for a smooth transition. Track loops by
	// default; pass loop=false for one-shot playback.
	void play(int music_id, bool loop = true);

	// Crossfade from the current track to music_id over fade_ms ms. If
	// nothing is currently playing, this is equivalent to play() with a
	// fade-in over fade_ms.
	void fade_to(int music_id, int fade_ms = 1000, bool loop = true);

	// Stop the active track. fade_ms == 0 stops immediately and frees the
	// channel; fade_ms > 0 fades to silence and leaves the (silent) channel
	// running. Either way, is_playing() returns false afterwards. The next
	// play()/fade_to() reuses the channel.
	void stop(int fade_ms = 0);

	// Immediately destroy both music voices and drop sample references.
	// Use when you want the music channels truly idle (between scenes, on
	// a long pause, etc.). Cheaper than stop(0) only in that it cleans up
	// any post-fade-out silent voice that stop(fade_ms) left behind.
	void force_stop();

	// 0..100. Music master volume, applied to the active track immediately
	// and remembered across play / fade_to / pause / resume.
	void set_volume(int percent);
	int  get_volume();

	// Fade the active track to silence (pause) or back to the master volume
	// (resume). The underlying voice keeps running, so the loop position
	// stays in sync -- resume picks up exactly where pause left off.
	void pause(int fade_ms = 50);
	void resume(int fade_ms = 50);

	// True if a track is currently playing (not stopped, not paused).
	bool is_playing();

	// True if the active track is in the paused state.
	bool is_paused();

	// Sample id of the currently-active track, or -1 if nothing is loaded.
	int current();

} // namespace Music
