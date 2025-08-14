# C++ XAudio2 Mixer and Streaming System

This project provides a modern C++ audio mixer and sample playback system using **XAudio2**. It supports **WAV**, **OGG**, and **MP3** playback, stereo/mono streams, on-the-fly resampling, and safe memory management via smart pointers. It is written with high-performance game audio needs in mind.

WHATS NEW:
What are the latest core features?
Plays a wav, ogg or mp3 sample both through straight xaudio2, or through a mixer. Why two separate paths? If you just want to play audio samples, use straight xaudio2. If you are creating custom streams like in emulation, or have a sample that pops/cracks when looped, you use the mixer path. It's all your choice. The only drawback currently of using the mixer is that you can't change the sample frequency on the fly, it's not supported yet. 
You can adjust the pan (on stereo streams), frequency and volume for each sample, as well as a main volume control.
The mixer code runs in it's own thread, keeping the main thread free.
It now supports non integer audio playback speed, so you can use 29.97 for media player sound output.
You can load your samples from a zip file, or simply load them from a directory.
It's fairly fast, and at this point at least decently tested. 
By default the code will resample any loaded samples that don't match the selected audio frequency, eg 44100 to 48000. This can easily be overridden in the sample_load routine. But note, if you are mixing these, they will definitely play at the wrong rate, so anything your loading and using the mixer with needs to match the selected frequency!

ADDENDUM! if you don't need Windows 7 support, rename "xaudio2redist.h" to "xaudio2.h" and remove the nuget package. 


## ✅ Features

- ✅ **Sample Playback**
- ✅ **Streaming Support**
- ✅ **8-bit / 16-bit WAV**, **OGG**, and **MP3** loading
- ✅ **Mono and Stereo** mixing
- ✅ XAudio2 **non-callback-based buffer streaming**
- ✅ Smart pointer safety (`std::unique_ptr`)
- ✅ Resampling to match system output rate
- ✅ Thread-safe audio list and sample storage
- ✅ Allegro-style volume control (`0-255`)
- ✅ `save_sample()` to write audio data to `.wav`

---

## 🔧 Requirements

- Windows 7+ with **XAudio2 2.9** (use DirectX SDK or Windows Kits)
- C++17 or later
- Optional: stb_vorbis and minimp3 libraries included for OGG/MP3 decoding

---

## 🔌 Usage Overview

### Mixer Initialization

```cpp
mixer_init(44100, 60); // 44100 Hz, 60 FPS target buffer rate
mixer_init(48000, 29.97); // 48000 Hz, 29.97 FPS target buffer rate for video playback
```

### Loading a Sample

```cpp
int sample_id = load_sample(nullptr, "sfx/explosion.wav");
```

You can load from ZIPs too:

```cpp
int sample_id = load_sample("game.zip", "assets/sound.ogg");
```

### Playing a Sample

```cpp
sample_start(0, sample_id, 0); // channel 0, no loop
```

### Streaming Setup

```cpp
stream_start(1, 0, 16); // channel 1, 16-bit, 60 fps
```

Then regularly push PCM data to the buffer:

```cpp
stream_update(1, pcm_data_pointer);
```

### Mixer Update

Call every frame or tick:

```cpp
mixer_update();
```

---

## 🔊 Volume and Playback Controls

```cpp
sample_set_volume(0, 128); // Set channel 0 to 50%
sample_set_freq(0, 22050); // Pitch shift to 22.05 kHz
sample_stop(0);            // Stop channel 0
```

### Global Control

```cpp
mute_audio();
pause_audio();
resume_audio();
restore_audio();
```

---

## 🔁 Mixer Mode vs Source Voice Mode

- `sample_start()` creates and manages a `IXAudio2SourceVoice`
- `sample_start_mixer()` pushes raw PCM to the shared stream buffer

Use mixer mode for one-shot sounds or games that mix many small effects per frame.

---

## 📦 Sample Structure

```cpp
struct SAMPLE {
    WAVEFORMATEX fx;
    std::unique_ptr<uint8_t[]> data8;
    std::unique_ptr<int16_t[]> data16;
    void* buffer;
    uint32_t sampleCount;
    uint32_t dataSize;
    SoundState state;
    int num;
    std::string name;
};
```

---

## 💾 Saving Audio

```cpp
save_sample(sample_id); // Saves to 'name.wav'
```

---

## 🧠 Sample Lookup Helpers

```cpp
std::string name = numToName(sample_id);
int id = nameToNum("jump");
int index = snumlookup(sample_id); // index into internal vector
```

---

## 📚 File Layout

- `mixer.h / mixer.cpp` — Core mixer and playback logic
- `wav_file.h / wav_file.cpp` — WAV, OGG, MP3 decoding
- `dbvolume.h` — Volume lookup table (0-255)
- `XAudio2Stream.h` — Sound buffer and backend stream functions
- `framework.h` — Platform integration (e.g., logging, file loading)
- `error_wav.h` — Optional fallback buffer for error sounds

---

## 🔐 License

This project is provided under GPL3 now. 

If you like and improve it, please consider sharing your changes!

---

## 📞 Contact

Created by **Tim Cottrill** (2022–2025)


