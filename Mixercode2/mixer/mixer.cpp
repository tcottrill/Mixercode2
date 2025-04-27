// Mixercode 2 "The 2nd Version", 2025 Tim Cottrill
// Free for any use. See Unlicense license
// Modify this code however you see fit, but if you make it better please send me the updates. :)
// This is revision 11. 4/13/25

// This code mixes one or more MONO 8bit or 16 bit WAV or OGG or MP3 samples and outputs it at the desired rate using Xaudio2 WITHOUT using a callback.
// See example for usage. Optionally it will allow you to change the sample rate of a loaded wav file to match the output sample rate.
// This code is not optimal, but it should compile for Windows 7 through 11 with no issues using Xaudio 2.9.

#define NOMINMAX
#include "mixer.h"
#include "framework.h"
#include "wav_file.h"
#include "fileio.h"
#include "XAudio2Stream.h"
#include "wav_resample.h"
#include "helper_functions.h"
#include "dbvolume.h"
#include "error_wav.h"
#include <cstdint>
#include <list>
#include "log.h"
//#include <thread>
//#include <mutex>
//#include <assert.h>
//#include <map>
#include <memory>

// Error handling macro
#define HR(hr) if (FAILED(hr)) { wrlog("Error at line %d: HRESULT = 0x%08X\n", __LINE__, hr);  }

extern IXAudio2* pXAudio2;

using namespace std;

static int SYS_FREQ = 44100;
static int BUFFER_SIZE = 0;
static int sound_paused = 0;
static int v_mute_audio = 0;
static int	sound_id = -1;      // id of sound to be loaded
static float last_master_vol;

CHANNEL channel[MAX_CHANNELS];

//List of actively playing samples
std::list<int> audio_list;
//List of loaded samples, so we can track, call by name, and delete when done;
std::vector<SAMPLE*> lsamples;
//std::vector< SAMPLE*>stream_samples; // Not Currently Being Used

static mutex audioMutex;

unsigned char Make8bit(int16_t sample)
{
	sample >>= 8;  // drop the low 8 bits
	sample ^= 0x80;  // toggle the sign bit
	return (sample & 0xFF);
}

short Make16bit(uint8_t sample)
{
	short sample16 = (int16_t)(sample - 0x80) << 8;
	return sample16;
}

void resample_wav_8(SAMPLE* sample, int new_freq)
{
	int input_size = sample->sampleCount;

	int output_size = (int)((float)input_size * new_freq / sample->fx.nSamplesPerSec);
	uint8_t* output_data = (uint8_t*)malloc(output_size);

	linear_interpolation_8(sample->data.u8, output_data, input_size, output_size);

	wrlog("Resampling 8 bit Sample #%d, %s", sample->num, sample->name.c_str());

	free(sample->data.buffer);
	sample->fx.nSamplesPerSec = new_freq;
	sample->dataSize = output_size;
	sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;

	sample->sampleCount = output_size;
	sample->data.buffer = output_data;

	wrlog("Resample: Samplerate #: %d", sample->fx.nSamplesPerSec);
	wrlog("Resample: Length #: %d", sample->dataSize);
	wrlog("Resample: Samplecount #: %d", sample->sampleCount);
}

void resample_wav_16(SAMPLE* sample, int new_freq)
{
	int16_t* output_data_16;
	int32_t output_samples;
	float resample_ratio = (float)new_freq / sample->fx.nSamplesPerSec;

	wrlog("Resampling 16 bit Sample #%d, %s", sample->num, sample->name.c_str());

	linear_interpolation_16(sample->data.u16, sample->sampleCount, &output_data_16, &output_samples, resample_ratio);

	free(sample->data.buffer);
	sample->fx.nSamplesPerSec = new_freq;
	sample->dataSize = output_samples * 2;
	sample->sampleCount = output_samples;
	sample->data.buffer = output_data_16;
	sample->fx.nAvgBytesPerSec = sample->fx.nSamplesPerSec * sample->fx.nBlockAlign;

	wrlog("Resample: Samplerate #: %d", sample->fx.nSamplesPerSec);
	wrlog("Resample: Length #: %d", sample->dataSize);
	wrlog("Resample: Samplecount #: %d", sample->sampleCount);
}

int load_sample(const char* archname, const char* filename, bool force_resample)
{
	SAMPLE* mysample_temp = new SAMPLE();
	static int error_loading = 0;

	unsigned char* sample_data = 0;
	HRESULT result;
	//LOAD FILE - Please add some error handling here!!!!!!!!!
	//TODO: Fix this far more elegantly. Get rid of the error loading thing, fix the fileio return values, maybe with a struct or something?
	if (archname)
	{
		sample_data = load_generic_zip(archname, filename);
		if (!sample_data)
		{
			wrlog("Error File Not Found %s, loading alternate", filename);
			sample_data = error_wav;
			result = WavLoadFileInternal(sample_data, 10008, mysample_temp);
			error_loading = 1;
		}
		else
		{
			//Create Wav data
			result = WavLoadFileInternal(sample_data, (int)get_last_zip_file_size(), mysample_temp);
			if (result == 0)
			{
				wrlog("Error, check loaded file format.");
				if (sample_data) {
					free(sample_data);
				}
				return -1;
			}
		}
	}
	else
	{
		sample_data = load_file(filename);
		//Create Wav data

		if (!sample_data)
		{
			wrlog("Error File Not Found %s, loading alternate", filename);
			sample_data = error_wav;
			result = WavLoadFileInternal(sample_data, 10008, mysample_temp);
			error_loading = 1;
		}
		else
		{
			result = WavLoadFileInternal(sample_data, get_last_file_size(), mysample_temp);
			if (result == 0)
			{
				wrlog("Error, check loaded file format.");
				if (sample_data) {
					free(sample_data);
				}
				return -1;
			}
		}
	}

	mysample_temp->name = remove_extension(filename);
	mysample_temp->name = base_name(mysample_temp->name);
	sound_id++;
	mysample_temp->num = sound_id;
	mysample_temp->state = SOUND_LOADED;
	//If sample loaded successfully proceed!
	//

	//If we're at the wrong sample rate, resample to match the current rate if requested
	if (mysample_temp->fx.nSamplesPerSec != SYS_FREQ && force_resample)
	{
		// The code currently doesn't handle resampling of stereo wav files.
		if (mysample_temp->fx.nChannels == 1)
		{
			if (mysample_temp->fx.wBitsPerSample == 8)
			{
				resample_wav_8(mysample_temp, SYS_FREQ);
			}
			else { resample_wav_16(mysample_temp, SYS_FREQ); }
		}
		else ("Warning, sample needs to be interpolated, but it's the wrong number of channels, fix.!");
	}

	// Debug Out:
	wrlog("File %s loaded with sound id: %d and state is: %d", filename, mysample_temp->num, mysample_temp->state);
	wrlog("Loading WAV #: %d", mysample_temp->num);
	wrlog("Stored filename is %s", mysample_temp->name.c_str());
	wrlog("Channels #: %d", mysample_temp->fx.nChannels);
	wrlog("Samplerate #: %d", mysample_temp->fx.nSamplesPerSec);
	wrlog("Length #: %d", mysample_temp->dataSize);
	wrlog("BPS #: %d", mysample_temp->fx.wBitsPerSample);
	wrlog("Samplecount #: %d", mysample_temp->sampleCount);

	//Add this sample to the loaded samples list
	lsamples.push_back(mysample_temp);

	// Why Can't I Free the Sample Data?
	if (!error_loading)	free(sample_data);
	error_loading = 0; // TODO: Ugh, I don't like this, find a better way.
	//Return Sound ID
	wrlog("Loaded sound success");
	return(sound_id);
}

void mixer_init(int rate, int fps)
{
	int i = 0;
	BUFFER_SIZE = rate / fps;
	SYS_FREQ = rate;

	wrlog("Mixer init, BUFFER SIZE = %d, freq %d framerate %d", BUFFER_SIZE, rate, fps);

	// Initialize the xaudio2 backend at the correct rate.
	xaudio2_init(rate, fps);

	//Clear and init Sample Channels
	for (i = 0; i < MAX_CHANNELS; i++)
	{
		channel[i].loaded_sample_num = -1;
		channel[i].state = SOUND_STOPPED;
		channel[i].looping = 0;
		channel[i].pos = 0;
		channel[i].vol = 1.0;
	}

	sound_paused = 0;
	v_mute_audio = 0;
}

void mixer_update()
{
	int32_t smix = 0;    //Sample mix buffer
	int32_t fmix = 0;   // Final sample mix buffer

	BYTE* soundbuffer = GetNextBuffer();

	for (int i = 0; i < BUFFER_SIZE; i++)
	{
		fmix = 0; //Set mix buffer to zero (silence for 16 bit audio)

		if (!sound_paused) // Other option, keep playing but set fmix to zero at end. Maybe better option to prevent buffer overflow?
		{
			for (std::list<int>::iterator it = audio_list.begin(); it != audio_list.end(); ++it)
			{
				SAMPLE* p = lsamples[channel[*it].loaded_sample_num]; //To shorten

				if (channel[*it].pos >= p->sampleCount) //Are we at the end?
				{
					if (channel[*it].looping == 0) {
						channel[*it].state = SOUND_STOPPED; audio_list.erase(it);
					} //If it's not looping, remove it.
					channel[*it].pos = 0;  //Otherwise, rewind to the beginning, or if it's a stream, ready to load more data;
				}
				// 16 bit mono
				if (p->fx.wBitsPerSample == 16)
				{
					smix = (short)p->data.u16[channel[*it].pos];
					smix = lround(smix = static_cast<int32_t> (smix * channel[*it].vol));
					channel[*it].pos += p->fx.nChannels;
				}
				// 8 bit mono
				else if (p->fx.wBitsPerSample == 8)
				{
					smix = (short)(((p->data.u8[channel[*it].pos] - 128) << 8));
					smix = lround(smix = static_cast<int32_t> (smix * channel[*it].vol));
					channel[*it].pos += p->fx.nChannels;
				}

				smix = static_cast<int32_t> (smix * .70); //Reduce volume to avoid clipping. This number can/should vary depending on the samples.
				fmix = fmix + smix;  //Mix here.
			}
		}

		if (v_mute_audio) fmix = 0; // Mute Volume

		if (fmix) //If the mix value is zero (nothing playing) , skip all this.
		{
			//Clip samples
			if (fmix > INT16_MAX) { fmix = INT16_MAX; }
			if (fmix < INT16_MIN) { fmix = INT16_MIN; }
		}
		soundbuffer[2 * i] = fmix & 0xff;
		soundbuffer[2 * i + 1] = (fmix >> 8) & 0xff;
	}

	xaudio2_update(soundbuffer, BUFFER_SIZE);
}

void mixer_end()
{
	xaudio2_stop();

	for (std::size_t i = 0; i < lsamples.size(); ++i)

	{
		if (lsamples[i]->data.buffer)
		{
			free(lsamples[i]->data.buffer);
			wrlog("Freeing sample #%d named %s", i, lsamples[i]->name.c_str());
			free(lsamples[i]);
		}
	}
}

void sample_stop(int chanid)
{
	if (channel[chanid].isPlaying)
	{
		//channel[chanid].voice->SetVolume(0);
		channel[chanid].voice->Stop();
		channel[chanid].voice->FlushSourceBuffers();
		channel[chanid].isPlaying = false;

		channel[chanid].state = SOUND_STOPPED;
		channel[chanid].looping = 0;
		channel[chanid].pos = 0;
	}
}

static void SetPan(IXAudio2SourceVoice* voice, float pan)
{
	float left = 0.5f - pan / 2;
	float right = 0.5f + pan / 2;

	float outputMatrix[8] = { 0 };
	int nChannels = 0;

	outputMatrix[0] = left;
	outputMatrix[1] = right;
	nChannels = 2;

	voice->SetOutputMatrix(nullptr, 1, nChannels, outputMatrix);
}

void sample_start(int chanid, int samplenum, int loop)
{
	//First check that it's a valid sample!
	if (!lsamples[samplenum]->state == SOUND_LOADED)
	{
		wrlog("error, attempting to play invalid sample on channel %d state: %d", chanid, channel[chanid].state);
		return;
	}

	// Start the sample playing

	if (channel[chanid].voice)
	{
		// DEBUG: wrlog("Destroying Source Voice!, chan %d", chanid);
		channel[chanid].voice->Stop();
		channel[chanid].voice->FlushSourceBuffers();
		channel[chanid].voice->DestroyVoice();
		channel[chanid].voice = NULL;
	}

	if (!channel[chanid].voice)
	{
		if (FAILED(pXAudio2->CreateSourceVoice(&channel[chanid].voice, &lsamples[samplenum]->fx, 0, 16.0f)))
		{
			//CoUninitialize();
			wrlog("FAILED to create source voice %d for sample %d", lsamples[samplenum]->num);
			return;
		}
		else
			wrlog("Creating Source Voice!, chan %d", chanid);
	}

	channel[chanid].isAllocated = true;
	channel[chanid].isReleased = false;
	channel[chanid].isPlaying = true;
	channel[chanid].looping = loop;
	channel[chanid].volume = 255;
	channel[chanid].pan = 128;

	//samplenum
	CHANNEL& v = channel[chanid];

	channel[chanid].frequency = lsamples[samplenum]->fx.nSamplesPerSec;
	channel[chanid].buffer.AudioBytes = lsamples[samplenum]->dataSize;
	channel[chanid].buffer.pAudioData = (BYTE*)lsamples[samplenum]->data.buffer;
	channel[chanid].buffer.LoopCount = v.looping ? XAUDIO2_LOOP_INFINITE : 0;
	channel[chanid].voice->SubmitSourceBuffer(&channel[chanid].buffer);
	float frequencyRatio = static_cast<float>((float)channel[chanid].frequency / (float)lsamples[samplenum]->fx.nSamplesPerSec);
	channel[chanid].voice->SetFrequencyRatio(frequencyRatio);
	channel[chanid].voice->SetVolume((float)channel[chanid].volume / 255.0f);
	SetPan(channel[chanid].voice, (float)(channel[chanid].pan - 128) / 128.0f);

	HR(channel[chanid].voice->Start());

	channel[chanid].isPlaying = true;

	// DEBUG: wrlog("Playing Sample #%d :%s", samplenum, lsamples[samplenum]->name.c_str());
}

int sample_get_position(int chanid)
{
	return channel[chanid].pos;
}

// This goes from 0 to 100, with 100 being the original level.
void sample_set_volume(int chanid, int volume)
{
	channel[chanid].vol = db_volume[volume];
	//wrlog("Setting channel %i to with volume %i setting bvolume %f", chanid, volume, channel[chanid].vol);
};

int sample_get_volume(int chanid)
{
	return (int)(channel[chanid].vol * 100);
};

void sample_set_position(int chanid, int pos)
{
	// Not needed with this code. ?
};

void sample_set_freq(int chanid, int freq)
{
	if (channel[chanid].isPlaying)
	{
		float frequencyRatio = static_cast<float>((float)freq / (float)channel[chanid].frequency);
		channel[chanid].voice->SetFrequencyRatio(frequencyRatio);
	}
};

int sample_playing(int chanid)
{
	XAUDIO2_VOICE_STATE state;

	if (channel[chanid].voice)
	{
		channel[chanid].voice->GetState(&state);

		if (state.BuffersQueued == 0)
		{
			// DEBUG: wrlog("Check sample #%d playing, returning false", chanid);
			return 0;
		}
		else
		{
			// DEBUG: wrlog("Check sample #%d playing, returning true", chanid);
			return 1;
		}
	}
	// DEBUG: wrlog("Check sample #%d playing, returning false, not allocated", chanid);
	return 0;
}

void sample_end(int chanid)
{
	channel[chanid].looping = 0;
}
/////////////////////////////////// ************** STREAMING / MIXER SAMPLE CODE BELOW  ***************** ////////////////////////////////////////////
void sample_start_mixer(int chanid, int samplenum, int loop)
{
	//First check that it's a valid sample!
	if (!lsamples[samplenum]->state == SOUND_LOADED)
	{
		wrlog("error, attempting to play invalid sample on channel %d state: %d", chanid, channel[chanid].state);
		return;
	}

	if (channel[chanid].state == SOUND_PLAYING)
	{
		wrlog("error, sound already playing on this channel %d state: %d", chanid, channel[chanid].state);
		return;
	}

	channel[chanid].state = SOUND_PLAYING;
	channel[chanid].stream_type = SOUND_PCM;
	channel[chanid].loaded_sample_num = samplenum;
	channel[chanid].looping = loop;
	channel[chanid].pos = 0;
	//channel[chanid].vol = 1.0;
	audio_list.emplace_back(chanid);
	wrlog("Playing Sample #%d :%s", samplenum, lsamples[samplenum]->name.c_str());
}

int sample_playing_mixer(int chanid)
{
	if (channel[chanid].state == SOUND_PLAYING)
		return 1;
	else return 0;
}

void sample_end_mixer(int chanid)
{
	channel[chanid].looping = 0;
}

void sample_stop_mixer(int chanid)
{
	channel[chanid].state = SOUND_STOPPED;
	channel[chanid].looping = 0;
	channel[chanid].pos = 0;
	audio_list.remove(chanid);
}

// This goes from 0 to 100, with 100 being the original level.
void sample_set_volume_mixer(int chanid, int volume)
{
	channel[chanid].vol = db_volume[volume];
	//wrlog("Setting channel %i to with volume %i setting bvolume %f", chanid, volume, channel[chanid].vol);
};

int sample_get_volume_mixer(int chanid)
{
	return (int)(channel[chanid].vol * 100);
};

void stream_start(int chanid, int stream, int bits, int frame_rate)
{
	int stream_sample = create_sample(bits, 0, SYS_FREQ, (int)SYS_FREQ / frame_rate, "STREAM");

	if (channel[chanid].state == SOUND_PLAYING)
	{
		wrlog("error, sound already playing on this channel %d state: %d", chanid, channel[chanid].state);
		return;
	}
	//wrlog("Playing Sample :%s", sound[samplenum].name.c_str());
	channel[chanid].state = SOUND_PLAYING;
	channel[chanid].loaded_sample_num = stream_sample;
	channel[chanid].looping = 1;
	channel[chanid].pos = 0;
	channel[chanid].stream_type = SOUND_STREAM;
	// Add to the list of playing streaming samples
	audio_list.emplace_back(chanid);
}

void stream_stop(int chanid, int stream)
{
	channel[stream].state = SOUND_STOPPED;
	channel[stream].loaded_sample_num = 0;
	channel[stream].looping = 0;
	channel[stream].pos = 0;
	audio_list.remove(chanid);
	//Warning, This doesn't delete the created sample/stream
}

void stream_update(int chanid, short* data)
{
	if (channel[chanid].state == SOUND_PLAYING)
	{
		SAMPLE* p = lsamples[channel[chanid].loaded_sample_num];
		memcpy(p->data.buffer, data, p->dataSize);
	}
}

void stream_update(int chanid, unsigned char* data)
{
	if (channel[chanid].state == SOUND_PLAYING)
	{
		SAMPLE* p = lsamples[channel[chanid].loaded_sample_num];
		memcpy(p->data.buffer, data, p->dataSize);
	}
}

void sample_remove(int samplenum)
{
}
// create_sample:
// *  Constructs a new sample structure of the specified type.
int create_sample(int bits, bool is_stereo, int freq, int len, const std::string& name)
{
	wrlog("Creating sample, Buffer size here is %d", len);
	
	std::string sample_name = "";
	SAMPLE* mysample_temp = new SAMPLE();
	sound_id++;
	mysample_temp->num = sound_id;

	if (name == "STREAM")	{
		// Since it's const I have to work around here. 
		 sample_name = name + std::to_string(sound_id);
	}
	else { sample_name = name; }

	mysample_temp->name = sample_name;
	wrlog("Creating Audio Sample with name %s and sound id %d", mysample_temp->name.c_str(), sound_id);

	// set rate and size in data structure
	mysample_temp->fx.wFormatTag = WAVE_FORMAT_PCM;
	mysample_temp->fx.nChannels = ((is_stereo) ? 2 : 1);
	mysample_temp->fx.nSamplesPerSec = freq;
	mysample_temp->fx.nBlockAlign = mysample_temp->fx.nChannels * bits / 8;
	mysample_temp->fx.wBitsPerSample = bits;
	mysample_temp->fx.nAvgBytesPerSec = mysample_temp->fx.nSamplesPerSec * mysample_temp->fx.nBlockAlign;
	mysample_temp->state = SOUND_LOADED;
	mysample_temp->data.buffer = (unsigned char*)malloc(len * bits / 8);
	mysample_temp->dataSize = len * bits / 8;
	mysample_temp->sampleCount = len;
	memset(mysample_temp->data.buffer, 0, len * bits / 8);

	//DEBUG: wrlog("Real buffer size %d", BUFFER_SIZE * 2);
	//DEBUG: wrlog("Buffer size created here %d", (len * ((bits == 8) ? 1 : sizeof(short)) * ((is_stereo) ? 2 : 1)));
	// Add to the list of samples
	lsamples.push_back(mysample_temp);
	return sound_id;
}

void mute_audio()
{
	last_master_vol = mixer_get_master_volume();
	v_mute_audio = 1;
	mixer_set_master_volume(0);
}

void restore_audio()
{
	mixer_set_master_volume(static_cast<int>(last_master_vol * 100));
	v_mute_audio = 0;
}

void pause_audio()
{
	last_master_vol = mixer_get_master_volume();
	mixer_set_master_volume(0);
	sound_paused = 1;
}

void resume_audio()
{
	mixer_set_master_volume(static_cast<int>(last_master_vol * 100));
	sound_paused = 0;
}

std::string numToName(int num)
{
	for (const auto& item : lsamples) {
		if (item->num == num) {
			return item->name;
		}
	}
	string retval = "";
	if (retval.empty()) { wrlog("Name not found looking up Sample #%d!", num); }
	return retval;
}

int nameToNum(std::string name)
{
	for (const auto& item : lsamples) {
		if (item->name == name) {
			return item->num;
		}
	}
	return -1; // Return -1 if the target name is not found
}

int snumlookup(int snum)
{
	// Search for the target number in the local vector
	for (size_t i = 0; i < lsamples.size(); ++i) {
		if (lsamples[i]->num == snum) {
			return static_cast<int>(i); // Return the index if found
		}
	}
	wrlog("Attempted lookup of sample number, it was not found in loaded samples?");
	return -1;
}

// Save a loaded sample, may be useful for saving modified or created samples. 
// TODO: Add some error checking to this!!
void save_sample(int samplenum)
{
	FILE* file;
	errno_t err;

	SAMPLE* p = lsamples[samplenum]; // Get a pointer to the required sample
	// Verify we have something real
	if (!p) { wrlog("Error writing sample #%d, could not get a pointer to that sample, does it exist?", samplenum); return; }
	
	string n_temp = p->name + ".wav";
		
	err = fopen_s(&file, n_temp.c_str(), "wb");
	if (err != 0) {
		wrlog("Error %d: Failed to open file: %s", stderr, n_temp.c_str());
		return;
	}
		
	// WAV file format sizes
	DWORD subchunk1Size = 16; // PCM
	DWORD subchunk2Size = (DWORD)p->dataSize;
	DWORD chunkSize = 4 + (8 + subchunk1Size) + (8 + subchunk2Size);

	// Write "RIFF" chunk descriptor
	fwrite("RIFF", 1, 4, file);
	fwrite(&chunkSize, 4, 1, file);
	fwrite("WAVE", 1, 4, file);

	// Write "fmt " subchunk
	fwrite("fmt ", 1, 4, file);
	fwrite(&subchunk1Size, 4, 1, file);
	fwrite(&p->fx.wFormatTag, 2, 1, file);
	fwrite(&p->fx.nChannels, 2, 1, file);
	fwrite(&p->fx.nSamplesPerSec, 4, 1, file);
	fwrite(&p->fx.nAvgBytesPerSec, 4, 1, file);
	fwrite(&p->fx.nBlockAlign, 2, 1, file);
	fwrite(&p->fx.wBitsPerSample, 2, 1, file);

	// Write "data" subchunk
	fwrite("data", 1, 4, file);
	fwrite(&subchunk2Size, 4, 1, file);
	fwrite((unsigned char*)p->data.buffer, 1, p->dataSize, file);

	fclose(file);
	wrlog("WAV file written to %s", n_temp.c_str());
}