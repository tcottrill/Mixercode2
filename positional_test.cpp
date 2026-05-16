// =============================================================================
// positional_test.cpp
// Standalone interactive test for X3DAudio positional placement.
//
// Build: include this file plus mixer.cpp, xaudio2_backend.cpp, audio_3d.cpp,
// sys_log.cpp (and whatever sys_log links against). Console subsystem.
//
// Usage:
//   positional_test.exe <path-to-mp3-or-wav>
//
// Keys (must have console focus):
//   1 = source at front center      (0, +R)
//   2 = source at back center       (0, -R)
//   3 = source at back left         (-R, -R)
//   4 = source at back right        (+R, -R)
//   5 = toggle swoop (front <-> back, 3s period)
//   ESC = quit
//
// What to listen for:
//   - On 5.1: positions 2/3/4 should come clearly from the rear speakers.
//     Position 3 = surround-left only; position 4 = surround-right only.
//   - On Atmos (HDMI soundbar reporting 7.1.x): the OS spatializer renders
//     X3DAudio's 7.1 matrix into Atmos object metadata. Rear cues should
//     feel "behind you" even if the soundbar is in front of you - that's the
//     point of an Atmos render.
//   - On Windows Sonic for Headphones: same effect via HRTF in stereo
//     headphones. Position 2 should feel above/behind, not just quieter.
//
// Diagnostics: watch the log for "XAudio2Backend::Init: master N channels,
// mask=0x..." and "audio_3d_init: N output channels, mask=0x...". For Atmos
// you expect 8 channels (7.1) typically. Stereo endpoint without spatializer
// is 2 - in that case you won't hear front/back distinction, only L/R.
// =============================================================================
#include "mixer.h"
#include "audio_3d.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <thread>
#include <vector>

static std::vector<uint8_t> read_file_bytes(const char* path)
{
	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f) return {};
	const auto size = static_cast<std::streamsize>(f.tellg());
	if (size <= 0) return {};
	f.seekg(0);
	std::vector<uint8_t> bytes(static_cast<size_t>(size));
	if (!f.read(reinterpret_cast<char*>(bytes.data()), size)) return {};
	return bytes;
}

// Decode a SPEAKER_xxx bitfield (mmreg.h) to a human-readable list. Useful for
// telling at a glance whether the OS is exposing FL/FR/FC/LFE/BL/BR/SL/SR or
// some weird subset. Atmos endpoints usually report 7.1 (0x63F).
static void print_speaker_mask(uint32_t mask)
{
	struct Bit { uint32_t bit; const char* name; };
	static const Bit bits[] = {
		{ SPEAKER_FRONT_LEFT,            "FL"  },
		{ SPEAKER_FRONT_RIGHT,           "FR"  },
		{ SPEAKER_FRONT_CENTER,          "FC"  },
		{ SPEAKER_LOW_FREQUENCY,         "LFE" },
		{ SPEAKER_BACK_LEFT,             "BL"  },
		{ SPEAKER_BACK_RIGHT,            "BR"  },
		{ SPEAKER_FRONT_LEFT_OF_CENTER,  "FLC" },
		{ SPEAKER_FRONT_RIGHT_OF_CENTER, "FRC" },
		{ SPEAKER_BACK_CENTER,           "BC"  },
		{ SPEAKER_SIDE_LEFT,             "SL"  },
		{ SPEAKER_SIDE_RIGHT,            "SR"  },
		{ SPEAKER_TOP_CENTER,            "TC"  },
		{ SPEAKER_TOP_FRONT_LEFT,        "TFL" },
		{ SPEAKER_TOP_FRONT_CENTER,      "TFC" },
		{ SPEAKER_TOP_FRONT_RIGHT,       "TFR" },
		{ SPEAKER_TOP_BACK_LEFT,         "TBL" },
		{ SPEAKER_TOP_BACK_CENTER,       "TBC" },
		{ SPEAKER_TOP_BACK_RIGHT,        "TBR" },
	};
	bool first = true;
	for (const auto& b : bits) {
		if (mask & b.bit) {
			std::printf("%s%s", first ? "" : " ", b.name);
			first = false;
		}
	}
	if (first) std::printf("(none)");
}

static const char* layout_label(int channels, uint32_t mask)
{
	// KSAUDIO_SPEAKER_xxx equivalents, defined locally to avoid <ksmedia.h>.
	constexpr uint32_t MASK_5POINT1 =                                       // 0x3F
		SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
		SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT;
	constexpr uint32_t MASK_5POINT1_SURROUND =                              // 0x60F
		SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
		SPEAKER_LOW_FREQUENCY | SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;
	constexpr uint32_t MASK_7POINT1 =                                       // 0xFF
		SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
		SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT |
		SPEAKER_FRONT_LEFT_OF_CENTER | SPEAKER_FRONT_RIGHT_OF_CENTER;
	constexpr uint32_t MASK_7POINT1_SURROUND =                              // 0x63F
		SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT | SPEAKER_FRONT_CENTER |
		SPEAKER_LOW_FREQUENCY | SPEAKER_BACK_LEFT | SPEAKER_BACK_RIGHT |
		SPEAKER_SIDE_LEFT | SPEAKER_SIDE_RIGHT;

	switch (channels) {
	case 1: return "mono";
	case 2: return "stereo";
	case 4: return "quad";
	case 6: return (mask == MASK_5POINT1 || mask == MASK_5POINT1_SURROUND)
		? "5.1" : "5.1 (non-standard mask)";
	case 8: return (mask == MASK_7POINT1 || mask == MASK_7POINT1_SURROUND)
		? "7.1 / Windows Sonic / Atmos (7.1 surface)"
		: "8ch (non-standard mask)";
	default: return "(unknown layout)";
	}
}

int main(int argc, char** argv)
{
	if (argc < 2) {
		std::printf("Usage: %s <path-to-mp3-or-wav>\n", argv[0]);
		return 1;
	}

	const char* audio_path = argv[1];
	auto bytes = read_file_bytes(audio_path);
	if (bytes.empty()) {
		std::printf("Failed to read '%s'\n", audio_path);
		return 1;
	}
	std::printf("Loaded %zu bytes from %s\n", bytes.size(), audio_path);

	if (!mixer_init(44100, 60)) {
		std::printf("mixer_init failed\n");
		return 1;
	}

	// ---- output device diagnostics ----
	// Report the mask audio_3d is actually rendering against (audio_3d coerces
	// the OS-reported 0x60F to 0x3F to match real BL/BR hardware). Also note
	// what the OS reported, in case that ever disagrees with reality.
	const int      out_channels = mixer_get_output_channels();
	const uint32_t os_mask      = mixer_get_output_channel_mask();
	const uint32_t out_mask     = audio_3d_get_channel_mask();
	std::printf("\n");
	std::printf("Output endpoint: %d channels (%s)\n",
		out_channels, layout_label(out_channels, out_mask));
	std::printf("  Speaker mask:  0x%X = ", out_mask);
	print_speaker_mask(out_mask);
	std::printf("\n");
	if (os_mask != out_mask) {
		std::printf("  (OS reported 0x%X = ", os_mask);
		print_speaker_mask(os_mask);
		std::printf("; corrected above)\n");
	}
	if (out_channels < 4) {
		std::printf("  WARNING: only %d channels - front/back distinction won't be audible.\n",
			out_channels);
		std::printf("           For surround, set the OS Sound endpoint to 5.1 or 7.1.\n");
		std::printf("           For headphones, enable Windows Sonic for Headphones.\n");
	}

	const int snd = load_sample_from_buffer(bytes.data(), bytes.size(), "test");
	if (snd < 0) {
		std::printf("load_sample_from_buffer failed - is the file a valid WAV/MP3/OGG?\n");
		mixer_end();
		return 1;
	}

	// Radius small enough that distance attenuation doesn't dominate over
	// directional cues. ~2 "meters" in X3DAudio default units.
	constexpr float R = 2.0f;
	constexpr int   CH = 0;

	// X3DAudio is initialized with mask 0x3F (BL/BR at -/+110 deg). Aiming the
	// test source at the speaker angle gives a clean hard placement.
	const float rear_angle_deg = 110.0f;
	const char* rear_label = "BL/BR (-/+110 deg)";
	const float rear_rad = rear_angle_deg * 3.14159265f / 180.0f;
	const float BL_X = -R * std::sin(rear_rad);
	const float BL_Y =  R * std::cos(rear_rad);
	const float BR_X =  R * std::sin(rear_rad);
	const float BR_Y =  R * std::cos(rear_rad);

	std::printf("Rear placement: %s\n", rear_label);

	// Listener at origin, facing +Y (screen-up).
	mixer_set_listener_2d(0.0f, 0.0f);

	// Start looping playback so you can move the source around and hear the
	// difference in real time.
	sample_start(CH, snd, /*loop=*/1);
	sample_set_world_position(CH, 0.0f, +R); // start at front

	std::printf("\n");
	std::printf("Positional audio test\n");
	std::printf("---------------------\n");
	std::printf("Listener at (0,0), facing +Y (screen-up).\n");
	std::printf("  1 = front center                    (angle    0 deg)\n");
	std::printf("  2 = directly behind                 (angle  180 deg, phantoms both rears)\n");
	std::printf("  3 = rear-LEFT speaker angle         (angle -%-3.0f deg, left rear dominant)\n", rear_angle_deg);
	std::printf("  4 = rear-RIGHT speaker angle        (angle +%-3.0f deg, right rear dominant)\n", rear_angle_deg);
	std::printf("  5 = toggle swoop front<->back (3s period)\n");
	std::printf("  ESC = quit\n");
	std::printf("\n");
	std::printf("NOTE: X3DAudio uses phantom imaging - sources between speakers play\n");
	std::printf("through both speakers proportionally to angle, not from one speaker.\n");
	std::printf("Position 2 will sound from BOTH rears equally (no behind speaker exists\n");
	std::printf("in 5.1). Positions 3/4 are angled at the rear speaker positions X3DAudio\n");
	std::printf("derived from your channel mask, so they should localize hard in that one\n");
	std::printf("rear (slight bleed is normal X3DAudio smoothing - the Windows speaker\n");
	std::printf("test sounds more 'one-speaker-only' because it bypasses spatializers).\n");
	std::printf("\n");
	std::printf("Now: front center\n");
	std::fflush(stdout);

	bool prev[6] = { false }; // index by digit 1..5
	bool swoop_active = false;
	float swoop_time = 0.0f;

	auto frame_dur = std::chrono::milliseconds(16);
	auto next_tick = std::chrono::steady_clock::now() + frame_dur;

	const auto pressed = [](int vk) {
		return (GetAsyncKeyState(vk) & 0x8000) != 0;
	};

	while (true) {
		if (pressed(VK_ESCAPE)) break;

		bool now[6] = {
			false,
			pressed('1'), pressed('2'), pressed('3'), pressed('4'), pressed('5')
		};

		if (now[1] && !prev[1]) {
			swoop_active = false;
			audio_3d_debug_print_next_matrix();
			sample_set_world_position(CH, 0.0f, +R);
			std::printf("Now: front center             (0.00, +%.2f) = 0 deg\n", R);
			std::fflush(stdout);
		}
		if (now[2] && !prev[2]) {
			swoop_active = false;
			audio_3d_debug_print_next_matrix();
			sample_set_world_position(CH, 0.0f, -R);
			std::printf("Now: directly behind          (0.00, -%.2f) = 180 deg\n", R);
			std::fflush(stdout);
		}
		if (now[3] && !prev[3]) {
			swoop_active = false;
			audio_3d_debug_print_next_matrix();
			sample_set_world_position(CH, BL_X, BL_Y);
			std::printf("Now: rear-LEFT speaker angle  (%.2f, %.2f) = -%.0f deg\n",
				BL_X, BL_Y, rear_angle_deg);
			std::fflush(stdout);
		}
		if (now[4] && !prev[4]) {
			swoop_active = false;
			audio_3d_debug_print_next_matrix();
			sample_set_world_position(CH, BR_X, BR_Y);
			std::printf("Now: rear-RIGHT speaker angle (%.2f, %.2f) = +%.0f deg\n",
				BR_X, BR_Y, rear_angle_deg);
			std::fflush(stdout);
		}
		if (now[5] && !prev[5]) {
			swoop_active = !swoop_active;
			std::printf("Swoop: %s\n", swoop_active ? "ON" : "OFF");
			std::fflush(stdout);
		}
		for (int i = 1; i <= 5; ++i) prev[i] = now[i];

		// Animate swoop: cosine sweep on Y between +R (front) and -R (back),
		// 3-second period. X stays at 0.
		if (swoop_active) {
			swoop_time += 0.016f;
			const float w = swoop_time * (2.0f * 3.14159265f / 3.0f);
			const float y = R * std::cos(w);
			sample_set_world_position(CH, 0.0f, y);
		}

		mixer_update();

		std::this_thread::sleep_until(next_tick);
		next_tick += frame_dur;
	}

	std::printf("\nShutting down...\n");
	sample_stop(CH);
	mixer_end();
	return 0;
}
