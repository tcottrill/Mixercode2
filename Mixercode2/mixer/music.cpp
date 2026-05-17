// =============================================================================
// music.cpp
// =============================================================================
#include "music.h"
#include "mixer.h"
#include "sys_log.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

namespace Music {

// -----------------------------------------------------------------------------
// State
// -----------------------------------------------------------------------------
namespace {

	constexpr int kDefaultChA = 18;
	constexpr int kDefaultChB = 19;

	// Track the active fader's intended target byte (0..255). Used so
	// set_volume / resume can pick up the current target without depending
	// on whatever the in-flight ramp is doing.
	enum class State { Empty, Playing, Paused, Stopped };

	std::mutex g_mutex;

	bool g_inited     = false;
	int  g_ch_a       = kDefaultChA;
	int  g_ch_b       = kDefaultChB;
	int  g_active_ch  = kDefaultChA;   // which of {a, b} currently holds the music
	int  g_active_id  = -1;            // sample id of active track, -1 if none
	int  g_music_pct  = 80;            // 0..100, music master volume
	State g_state     = State::Empty;

	// Helpers -----------------------------------------------------------------

	int pct_to_byte(int pct)
	{
		pct = std::clamp(pct, 0, 100);
		// Round-to-nearest 0..100 -> 0..255.
		return (pct * 255 + 50) / 100;
	}

	int other_channel(int ch)
	{
		return (ch == g_ch_a) ? g_ch_b : g_ch_a;
	}

	void stop_channel_hard(int ch)
	{
		// sample_stop on the voice path fully tears down: stops the voice,
		// destroys it, removes from audio_list, resets state, drops the
		// SAMPLE reference. Safe to call on a channel that isn't playing.
		sample_stop(ch);
	}

	// Start a track on the given channel at the given start volume (byte).
	// Caller is responsible for any subsequent fade.
	void start_channel(int ch, int music_id, bool loop, int start_vol_byte)
	{
		sample_start(ch, music_id, loop ? 1 : 0);
		sample_set_volume(ch, std::clamp(start_vol_byte, 0, 255));
	}

} // namespace

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

void init()
{
	std::scoped_lock lock(g_mutex);
	if (g_inited) return;

	g_active_ch = g_ch_a;
	g_active_id = -1;
	g_state     = State::Empty;
	g_inited    = true;

	LOG_INFO("Music::init: channels A=%d B=%d, master=%d%%", g_ch_a, g_ch_b, g_music_pct);
}

void shutdown()
{
	std::scoped_lock lock(g_mutex);
	if (!g_inited) return;

	// Stop both channels regardless of which is active. Either could be
	// holding a sample reference from a prior crossfade.
	stop_channel_hard(g_ch_a);
	stop_channel_hard(g_ch_b);

	g_active_id = -1;
	g_state     = State::Empty;
	g_inited    = false;

	LOG_INFO("Music::shutdown");
}

void set_channels(int channel_a, int channel_b)
{
	std::scoped_lock lock(g_mutex);

	if (channel_a == channel_b) {
		LOG_ERROR("Music::set_channels: channels must differ (got %d, %d)", channel_a, channel_b);
		return;
	}
	if (g_state == State::Playing || g_state == State::Paused) {
		LOG_ERROR("Music::set_channels: cannot change channels while music is active");
		return;
	}

	g_ch_a = channel_a;
	g_ch_b = channel_b;
	g_active_ch = g_ch_a;
}

int load(const uint8_t* data, size_t size, const char* name)
{
	if (!data || size == 0) {
		LOG_ERROR("Music::load: empty buffer");
		return -1;
	}
	const int sid = load_sample_from_buffer(data, size, name, /*force_resample=*/true);
	if (sid < 0) {
		LOG_ERROR("Music::load: decode failed (size=%zu, name=%s)", size, name ? name : "(null)");
	}
	return sid;
}

int load_from_file(const char* path, const char* name)
{
	if (!path) {
		LOG_ERROR("Music::load_from_file: null path");
		return -1;
	}

	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f) {
		LOG_ERROR("Music::load_from_file: could not open '%s'", path);
		return -1;
	}
	const auto size = static_cast<std::streamsize>(f.tellg());
	if (size <= 0) {
		LOG_ERROR("Music::load_from_file: '%s' is empty or unreadable", path);
		return -1;
	}
	f.seekg(0);

	std::vector<uint8_t> bytes(static_cast<size_t>(size));
	if (!f.read(reinterpret_cast<char*>(bytes.data()), size)) {
		LOG_ERROR("Music::load_from_file: read failed on '%s'", path);
		return -1;
	}

	return load(bytes.data(), bytes.size(), name ? name : path);
}

void unload(int music_id)
{
	if (music_id < 0) return;

	{
		std::scoped_lock lock(g_mutex);

		// If this track is currently playing, stop it first. Match either
		// channel since a crossfade could have it on the inactive side.
		if (g_active_id == music_id) {
			stop_channel_hard(g_ch_a);
			stop_channel_hard(g_ch_b);
			g_active_id = -1;
			g_state     = State::Empty;
		}
	}

	// sample_remove takes its own lock; call it outside ours.
	sample_remove(music_id);
}

void play(int music_id, bool loop)
{
	if (music_id < 0) {
		LOG_ERROR("Music::play: invalid music_id %d", music_id);
		return;
	}

	std::scoped_lock lock(g_mutex);
	if (!g_inited) {
		LOG_ERROR("Music::play: not initialized");
		return;
	}

	// Hard cut: stop BOTH channels so any silent-after-fade voice from a
	// prior fade_to/stop gets cleanly torn down too.
	stop_channel_hard(g_ch_a);
	stop_channel_hard(g_ch_b);

	g_active_ch = g_ch_a;
	g_active_id = music_id;
	g_state     = State::Playing;

	start_channel(g_active_ch, music_id, loop, pct_to_byte(g_music_pct));
}

void fade_to(int music_id, int fade_ms, bool loop)
{
	if (music_id < 0) {
		LOG_ERROR("Music::fade_to: invalid music_id %d", music_id);
		return;
	}
	if (fade_ms <= 0) {
		// No fade requested -- behave as a hard cut.
		play(music_id, loop);
		return;
	}

	std::scoped_lock lock(g_mutex);
	if (!g_inited) {
		LOG_ERROR("Music::fade_to: not initialized");
		return;
	}

	const int target_byte = pct_to_byte(g_music_pct);

	if (g_state != State::Playing && g_state != State::Paused) {
		// Nothing playing -- treat as fade-in on channel A.
		stop_channel_hard(g_ch_b);     // make sure B is clean too
		g_active_ch = g_ch_a;
		g_active_id = music_id;
		g_state     = State::Playing;

		start_channel(g_active_ch, music_id, loop, /*start_vol_byte=*/0);
		mixer_ramp_volume(g_active_ch, fade_ms, target_byte);
		return;
	}

	// Crossfade: start new on the OTHER channel at vol 0, ramp it up, ramp
	// the current down. After fade_ms, the old channel is silent (still
	// alive playing the old looped sample); the next fade_to / play / stop
	// will reuse it cleanly.
	const int old_ch = g_active_ch;
	const int new_ch = other_channel(old_ch);

	// Ensure the new-side channel is clean before reuse (could be holding
	// a prior fade-out remnant).
	stop_channel_hard(new_ch);

	start_channel(new_ch, music_id, loop, /*start_vol_byte=*/0);
	mixer_ramp_volume(new_ch, fade_ms, target_byte);
	mixer_ramp_volume(old_ch, fade_ms, 0);

	g_active_ch = new_ch;
	g_active_id = music_id;
	g_state     = State::Playing;
}

void stop(int fade_ms)
{
	std::scoped_lock lock(g_mutex);
	if (!g_inited) return;

	if (g_state == State::Empty || g_state == State::Stopped) {
		// Already idle. Make sure both channels are quiet anyway.
		stop_channel_hard(g_ch_a);
		stop_channel_hard(g_ch_b);
		g_active_id = -1;
		g_state     = State::Stopped;
		return;
	}

	if (fade_ms <= 0) {
		stop_channel_hard(g_active_ch);
	} else {
		mixer_ramp_volume(g_active_ch, fade_ms, 0);
		// Leave the voice running and silent; next play/fade_to/force_stop
		// will reap it. Channel keeps mixing at zero gain.
	}

	g_active_id = -1;
	g_state     = State::Stopped;
}

void force_stop()
{
	std::scoped_lock lock(g_mutex);
	if (!g_inited) return;

	stop_channel_hard(g_ch_a);
	stop_channel_hard(g_ch_b);
	g_active_id = -1;
	g_state     = State::Stopped;
}

void set_volume(int percent)
{
	percent = std::clamp(percent, 0, 100);
	std::scoped_lock lock(g_mutex);
	g_music_pct = percent;

	// Apply to active channel immediately. If paused, leave the channel at
	// 0 -- resume() will fade up to the new target.
	if (g_state == State::Playing) {
		sample_set_volume(g_active_ch, pct_to_byte(percent));
	}
}

int get_volume()
{
	std::scoped_lock lock(g_mutex);
	return g_music_pct;
}

void pause(int fade_ms)
{
	std::scoped_lock lock(g_mutex);
	if (!g_inited || g_state != State::Playing) return;

	if (fade_ms <= 0) {
		sample_set_volume(g_active_ch, 0);
	} else {
		mixer_ramp_volume(g_active_ch, fade_ms, 0);
	}
	g_state = State::Paused;
}

void resume(int fade_ms)
{
	std::scoped_lock lock(g_mutex);
	if (!g_inited || g_state != State::Paused) return;

	const int target_byte = pct_to_byte(g_music_pct);
	if (fade_ms <= 0) {
		sample_set_volume(g_active_ch, target_byte);
	} else {
		mixer_ramp_volume(g_active_ch, fade_ms, target_byte);
	}
	g_state = State::Playing;
}

bool is_playing()
{
	std::scoped_lock lock(g_mutex);
	return g_state == State::Playing;
}

bool is_paused()
{
	std::scoped_lock lock(g_mutex);
	return g_state == State::Paused;
}

int current()
{
	std::scoped_lock lock(g_mutex);
	return g_active_id;
}

} // namespace Music
