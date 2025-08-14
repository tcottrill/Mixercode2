/* =============================================================================
 * File: XAudio2Stream.cpp
 * Overview:
 *   Implementation of a lightweight XAudio2 streaming backend using a small
 *   ring of interleaved stereo S16 buffers. Handles COM initialization (when
 *   needed), XAudio2 device/voice creation, buffer submission, and teardown.
 *
 * Implementation Details:
 *   - Creates an IXAudio2 engine, a mastering voice (device-selected channel
 *     count), and a source voice configured for stereo 16-bit PCM at the
 *     requested sample rate.
 *   - Allocates N ring buffers (capacity derived from sampleRate / fpsExact)
 *     and advances one slot per update.
 *   - xaudio2_update(ptr, bytes) submits exactly the caller-specified byte
 *     length (must be a multiple of 4 bytes: 2 channels * 16 bits).
 *   - Clean shutdown destroys voices, releases IXAudio2, frees buffers, and
 *     calls CoUninitialize() if this module called CoInitializeEx() earlier.
 *
 * Error Handling:
 *   - Uses an HR(...) macro to log and return on failures.
 *   - Validates buffer byte counts and reports issues via the logging utility.
 *
 * Build Notes:
 *   - Links against XAudio2 (e.g., via pragma or build system).
 *   - Requires the XAudio2 redistributable headers/libraries and Windows SDK.
 *
 * Typical Usage:
 *   // during startup:
 *   hr = xaudio2_init(sampleRate, fpsExact);
 *
 *   // per audio frame:
 *   BYTE* dst = GetNextBuffer();
 *   // mix 'frames' stereo S16 samples into 'dst' ...
 *   xaudio2_update(dst, frames * 4);
 *
 *   // shutdown:
 *   xaudio2_stop();
 *
 * Limitations:
 *   - Fixed to stereo S16 PCM by default; adjust the source voice format here
 *     if you need a different channel layout or bit depth.
 *
 * ---------------------------------------------------------------------------
  * License (GPLv3):
 *   This file is part of GameEngine Alpha.
 *
 *   <Project Name> is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   <Project Name> is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with GameEngine Alpha.  If not, see <https://www.gnu.org/licenses/>.
 *
 *   Copyright (C) 2022-2025  Tim Cottrill
 *   SPDX-License-Identifier: GPL-3.0-or-later
 * =============================================================================
 */

#include "XAudio2Stream.h"
#include "mixer.h"
#include "sys_log.h"
#include "mixer_volume.h"  
#include <cstring>

// Error handling macro
#define HR(hr) if (FAILED(hr)) { LOG_ERROR("Error at line %d: HRESULT = 0x%08X\n", __LINE__, hr); return hr; }

#pragma comment(lib, "xaudio2.lib")

// Global variables
const int NUM_BUFFERS = 5;
IXAudio2* pXAudio2 = nullptr;
IXAudio2MasteringVoice* pMasterVoice = nullptr;
IXAudio2SourceVoice* pSourceVoice = nullptr;
BYTE* audioBuffers[NUM_BUFFERS];
DWORD bufferSize;
// CoInitalize tracker
static bool g_comInitLocal = false;

// Number of audio updates per second
int UpdatesPerSecond = 60;
int SamplesPerSecond = 22050;
int BufferDurationMs = 1000 / UpdatesPerSecond;
int SamplesPerBuffer = SamplesPerSecond / UpdatesPerSecond;

int currentBufferIndex = 0;

void mixer_set_master_volume(int volumePercent)
{
    volumePercent = std::clamp(volumePercent, 0, 100);
    const float amplitude = VolumePercentToLinear(volumePercent);

    if (pMasterVoice)
        pMasterVoice->SetVolume(amplitude);

    // Optional: log also the dB we effectively used
    float dB = (amplitude > 0.0f) ? 20.0f * log10f(amplitude) : -1000.0f;
    LOG_INFO("Master volume: %d%% -> %.2f dB -> %.6f linear", volumePercent, dB, amplitude);
}

float mixer_get_master_volume()
{
    float vol = 1.0f;
    if (pMasterVoice) {
        pMasterVoice->GetVolume(&vol);
    }
   // LOG_INFO("Mixer get master volume %f", vol);
    return vol;
}

BYTE* GetNextBuffer()
{
    return audioBuffers[currentBufferIndex];
}

HRESULT xaudio2_init(int rate, double fpsExact)
{
    HRESULT hr;

    UpdatesPerSecond = (int)std::lround(fpsExact);   // for logs only
    SamplesPerSecond = rate;
    const int framesCapacity = (int)std::ceil((double)SamplesPerSecond / fpsExact);
    SamplesPerBuffer = framesCapacity;               // capacity, not nominal
    BufferDurationMs = (int)std::lround(1000.0 / fpsExact);

    HRESULT hrCI = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hrCI == S_OK || hrCI == S_FALSE) g_comInitLocal = true;

    // Init XAudio2
    HR(XAudio2Create(&pXAudio2, 0, XAUDIO2_DEFAULT_PROCESSOR));

    // Mastering voice (let XAudio2 choose device channel count)
    HR(pXAudio2->CreateMasteringVoice(&pMasterVoice, XAUDIO2_DEFAULT_CHANNELS, SamplesPerSecond, 0, 0));
    //pMasterVoice->SetVolume(0.9f);
    pMasterVoice->SetVolume(1.0f);

    // We want a STEREO 16-bit PCM streaming voice
    WAVEFORMATEX wf = {};
    wf.wFormatTag = WAVE_FORMAT_PCM;
    wf.nChannels = 2;                      // *** stereo ***
    wf.nSamplesPerSec = SamplesPerSecond;
    wf.wBitsPerSample = 16;
    wf.nBlockAlign = wf.nChannels * wf.wBitsPerSample / 8; // 4 bytes per frame
    wf.nAvgBytesPerSec = wf.nSamplesPerSec * wf.nBlockAlign;
    wf.cbSize = 0;

    hr = pXAudio2->CreateSourceVoice(&pSourceVoice, &wf, XAUDIO2_VOICE_NOPITCH,
        XAUDIO2_DEFAULT_FREQ_RATIO, NULL, NULL, NULL);
    if (FAILED(hr))
    {
        LOG_INFO("Failed to create source voice: %#X\n", hr);
        return hr;
    }

    LOG_INFO("Source Voice Created Successfully. SamplesPerBuffer=%d (stereo frames)", SamplesPerBuffer);

    // Allocate interleaved stereo buffers: frames * 4 bytes
    bufferSize = SamplesPerBuffer * wf.nBlockAlign;  // alloc big enough for N+1 frames
    for (int i = 0; i < NUM_BUFFERS; ++i) {
        audioBuffers[i] = new BYTE[bufferSize];
        std::memset(audioBuffers[i], 0, bufferSize);
    }

    HR(pSourceVoice->Start());
    currentBufferIndex = 0;

    return S_OK;
}

// -----------------------------------------------------------------------------
// xaudio2_update
// Submit one mixed audio frame to XAudio2. Supports variable-length (fractional)
// frames by honoring the caller-provided byte length.
//
// The caller typically does:
//   BYTE* dst = GetNextBuffer();          // pointer to ring buffer slot
//   /* mix samples into dst ... */
//   xaudio2_update(dst, frames * 4);      // 16-bit stereo => 4 bytes per frame
//
// Notes:
// - If 'buffera' is nullptr, this function will submit the current ring slot.
// - 'bufferLength' is in BYTES and is used exactly as provided.
// - Advances the ring buffer index after a successful submit.
// -----------------------------------------------------------------------------
HRESULT xaudio2_update(BYTE* buffera, DWORD bufferLength)
{
    if (!pSourceVoice) {
        LOG_ERROR("xaudio2_update: source voice is null");
        return E_FAIL;
    }

    if (bufferLength == 0) {
        // Nothing to submit; not an error.
        return S_OK;
    }

    if (bufferLength % 4 != 0) {
        LOG_ERROR("xaudio2_update: bufferLength (%u) not multiple of 4", (unsigned)bufferLength);
        return E_INVALIDARG;
    }

    // Choose the payload: caller-provided pointer or the current ring slot.
    BYTE* payload = buffera ? buffera : audioBuffers[currentBufferIndex];

    // If we are using the internal ring slot, ensure we never submit beyond its capacity.
    if (!buffera && bufferLength > bufferSize) {
        LOG_DEBUG("xaudio2_update: bufferLength (%u) > ring buffer capacity (%u); clamping",
            (unsigned)bufferLength, (unsigned)bufferSize);
        bufferLength = (DWORD)bufferSize;
    }

    XAUDIO2_BUFFER xb = {};
    xb.AudioBytes = bufferLength;   // USE CALLER-PROVIDED LENGTH (BYTES)
    xb.pAudioData = payload;        // Interleaved PCM data

    HRESULT hr = pSourceVoice->SubmitSourceBuffer(&xb);
    if (FAILED(hr)) {
        LOG_ERROR("xaudio2_update: SubmitSourceBuffer failed, hr=0x%08X", (unsigned)hr);
        return hr;
    }

    // Advance to next ring buffer slot for the following frame.
    currentBufferIndex = (currentBufferIndex + 1) % NUM_BUFFERS;
    return hr;
}

void xaudio2_stop()
{
    if (pSourceVoice) { pSourceVoice->DestroyVoice(); pSourceVoice = nullptr; }
    if (pMasterVoice) { pMasterVoice->DestroyVoice(); pMasterVoice = nullptr; }
    if (pXAudio2) { pXAudio2->Release(); pXAudio2 = nullptr; }

    for (int i = 0; i < NUM_BUFFERS; ++i)  
    {
        delete[] audioBuffers[i];
        audioBuffers[i] = nullptr;
    }
    if (g_comInitLocal) { CoUninitialize(); g_comInitLocal = false; }
}