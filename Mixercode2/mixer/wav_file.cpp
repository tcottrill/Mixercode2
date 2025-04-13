// Mixercode 2 "The 2nd Version", 2025 Tim Cottrill
// Free for any use. See Unlicense license
// Modify this code however you see fit, but if you make it better please send me the updates. :)
// This is revision 11. 4/13/25

#define OGG_DECODE
#define MP3_DECODE

#include "wav_file.h"
#include <xaudio2redist.h>
#include "log.h"

#ifdef OGG_DECODE
#include "stb_vorbis.h"
#endif
#include <vcruntime_string.h>

#ifdef MP3_DECODE
#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"  // For MP3 decoding
#include "minimp3_ex.h"
#endif

#pragma warning (disable : 4018 ) //Signed Unsigned mismatch

// Function to process WAV data buffer
int processWaveDataBuffer(const unsigned char* buffer, size_t bufferSize, SAMPLE* audioFile) {
	if (bufferSize < 12) {
		wrlog("Invalid buffer size.\n");
		return -1;
	}

	if (strncmp((char*)buffer, "RIFF", 4) != 0 || strncmp((char*)(buffer + 8), "WAVE", 4) != 0) {
		wrlog("Invalid WAV file format.\n");
		return -1;
	}

	size_t pos = 12;
	while (pos < bufferSize) {
		char chunkID[4];
		memcpy(chunkID, buffer + pos, 4);
		pos += 4;

		uint32_t chunkSize;
		memcpy(&chunkSize, buffer + pos, sizeof(uint32_t));
		pos += sizeof(uint32_t);

		if (strncmp(chunkID, "fmt ", 4) == 0) {
			memcpy(&audioFile->fx.wFormatTag, buffer + pos, sizeof(uint16_t));
			memcpy(&audioFile->fx.nChannels, buffer + pos + 2, sizeof(uint16_t));
			memcpy(&audioFile->fx.nSamplesPerSec, buffer + pos + 4, sizeof(uint32_t));
			memcpy(&audioFile->fx.nAvgBytesPerSec, buffer + pos + 8, sizeof(uint32_t));
			memcpy(&audioFile->fx.nBlockAlign, buffer + pos + 12, sizeof(uint16_t));
			memcpy(&audioFile->fx.wBitsPerSample, buffer + pos + 14, sizeof(uint16_t));
			pos += chunkSize;
		}
		else if (strncmp(chunkID, "data", 4) == 0) {
			audioFile->dataSize = chunkSize;
			audioFile->data.buffer = (unsigned char*)malloc(chunkSize);
			if (!audioFile->data.buffer) {
				wrlog("Failed to allocate memory for WAV audio data.\n");
				return -1;
			}
			memcpy(audioFile->data.buffer, buffer + pos, chunkSize);
			pos += chunkSize;
		}
		else {
			pos += chunkSize;
		}
	}
	// Calculate the number of samples
	audioFile->sampleCount = audioFile->dataSize / (audioFile->fx.wBitsPerSample / 8);
	audioFile->fx.cbSize = 0;
	// free(buffer);
	return 0;
}

#ifdef MP3_DECODE
// Function to decode MP3 files (existing)
int processMp3DataBuffer(const unsigned char* buffer, size_t bufferSize, SAMPLE* audioFile) {
	mp3dec_t mp3d;
	mp3dec_file_info_t info;
	mp3dec_init(&mp3d);

	if (mp3dec_load_buf(&mp3d, buffer, bufferSize, &info, NULL, NULL) != 0) {
		wrlog("Failed to decode MP3 file.\n");
		return -1;
	}

	audioFile->fx.wFormatTag = WAVE_FORMAT_PCM;  // PCM format
	audioFile->fx.nChannels = info.channels;
	audioFile->fx.nSamplesPerSec = info.hz;
	audioFile->fx.wBitsPerSample = 16;  // Minimp3 outputs 16-bit PCM
	audioFile->fx.nBlockAlign = info.channels * 2;
	audioFile->fx.nAvgBytesPerSec = info.hz * audioFile->fx.nBlockAlign;
	audioFile->dataSize = info.samples * 2;  // 2 bytes per sample
	audioFile->data.buffer = (unsigned char*)malloc(audioFile->dataSize);
	if (!audioFile->data.buffer) {
		wrlog("Failed to allocate memory for MP3 audio data.\n");
		return -1;
	}
	// Calculate the number of samples
	audioFile->sampleCount = audioFile->dataSize / (audioFile->fx.wBitsPerSample / 8);
	memcpy(audioFile->data.buffer, info.buffer, audioFile->dataSize);
	free(info.buffer);  // Free buffer allocated by minimp3

	return 0;
}
#endif

#ifdef OGG_DECODE
// Function to decode OGG files
int processOggDataBuffer(const unsigned char* buffer, size_t bufferSize, SAMPLE* audioFile) {
	int error;
	stb_vorbis* vorbis = stb_vorbis_open_memory(buffer, bufferSize, &error, NULL);
	if (!vorbis || error) {
		wrlog("Failed to decode OGG file.\n");
		return -1;
	}

	stb_vorbis_info info = stb_vorbis_get_info(vorbis);
	audioFile->fx.wFormatTag = WAVE_FORMAT_PCM;  // PCM format
	audioFile->fx.nChannels = info.channels;
	audioFile->fx.nSamplesPerSec = info.sample_rate;
	audioFile->fx.wBitsPerSample = 16;  // stb_vorbis outputs 16-bit PCM
	audioFile->fx.nBlockAlign = info.channels * 2;
	audioFile->fx.nAvgBytesPerSec = info.sample_rate * audioFile->fx.nBlockAlign;

	// Decode the audio data
	audioFile->dataSize = stb_vorbis_stream_length_in_samples(vorbis) * info.channels * 2;
	audioFile->data.buffer = (unsigned char*)malloc(audioFile->dataSize);
	if (!audioFile->data.buffer) {
		wrlog("Failed to allocate memory for OGG audio data.\n");
		stb_vorbis_close(vorbis);
		return -1;
	}
	// Calculate the number of samples
	audioFile->sampleCount = audioFile->dataSize / (audioFile->fx.wBitsPerSample / 8);
	stb_vorbis_get_samples_short_interleaved(vorbis, info.channels,
		(short*)audioFile->data.buffer,
		audioFile->dataSize / 2);
	stb_vorbis_close(vorbis);

	return 0;
}
#endif

// Determine file format (WAV, MP3, or OGG) and load the appropriate type
int WavLoadFileInternal(unsigned char* buffer, int fileSize, SAMPLE* audioFile)
{
	if (fileSize >= 12 && strncmp((char*)buffer, "RIFF", 4) == 0 && strncmp((char*)(buffer + 8), "WAVE", 4) == 0) {
		wrlog("Processing as WAV file...");
		if (processWaveDataBuffer(buffer, fileSize, audioFile) != 0) {
			wrlog("Failed to process WAV file.");
			free(buffer);
			return 0;
		}
	}

#ifdef MP3_DECODE
	else if (fileSize >= 3 && strncmp((char*)buffer, "ID3", 3) == 0) {
		wrlog("Processing as MP3 file...");
		if (processMp3DataBuffer(buffer, fileSize, audioFile) != 0) {
			wrlog("Failed to process MP3 file.");
			free(buffer);
			return 0;
		}
	}
#endif

#ifdef OGG_DECODE
	else {
		wrlog("Processing as OGG file...");
		if (processOggDataBuffer(buffer, fileSize, audioFile) != 0) {
			wrlog("Failed to process OGG file.");
			free(buffer);
			return 0;
		}
	}

#endif
	return 1;
}