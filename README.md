C/C++ Audio Mixer code
# CPP-Mixercode "2" - The Revenge of the Sample!

This is the second iteration of this code and is a lot more complicated then the first one. If you are just starting out, please look at my mixercode project for an example of what not to do. 

Note: This is a work in progress and should be considered barely beta quality software. If you hold your mouth right and squint, it works great. 

What this code does:

This code loads and plays WAV and OGG and MP3 mono and stereo files in C/C++. It also mixes streaming audio at fixed frequencys and framerates. It does not use a callback, so you'll need to update the mixer in your main loop, or with a custom timed callback thread. 
This code was written primarily for emulator support and the streaming functions do not support dynamic framerates. You need to be within 30 to 60 fps (ish). I have had no issue with buffer exhaustion with this code (so far) on a properly configured system.
Dynamic resampling is built in, so if the samples are not at the target framerate, you can resample them if required.
This code was originally written to be compatible with Shawn Hargreaves Allegro Library, version 4, so all the main function names are named to match Allegro. 
This code has been tested to compile and run on Windows 7 - 11.
See the demo for how to use, or the AAE emulator in my Github for a more comprehensive example. 

This is all code, no third party libraries or DLL's used, except for Xaudio2. I am using Xaudio2 instead of WASPI just because I am familiar with it.  
OGG support is courtesy STB Vorbis - http://nothings.org/stb
MP3 support is courtesy MiniMP3 - https://github.com/lieff/minimp3

OGG and MP3 support are totally optional if you don't want to include the headers. See wav_file.cpp, and comment out:
#define OGG_DECODE
#define MP3_DECODE

What do you need to compile:

Requires Visual Studio 2022 and the Xaudio2, version 2.9 Microsoft.XAudio2.Redist.1.2.11 nuget package installed to build correctly. 

How to install the NuGet package:
https://learn.microsoft.com/en-us/windows/win32/xaudio2/xaudio2-redistributable

To use: Copy the files from the included demo and use mixer_init() with your target frequency and framerate. 
Make sure mixer_update is in your main loop, and mixer_end to cleanup. 

Notes and supported functions: 
- Currently WAV and OGG and MP3 mono and stereo files are supported. 
- Currently all samples that are mixed with the mixer must be the same frequency! You can enable resampling to automatically up/down scale loaded samples.  
- The mixercode currently is MONO ONLY. It will mix and play stereo samples, but only the left channel. 
- If you don't require OGG or MP3 support, you can comment the code out and delete the included headers.
- For non-mixed samples, dynamic volume, frequency and panning is supported. For mixed samples, only volume is supported and its from 0 to 100, not 0-255 like the samples are. It's confusing and I'll get it fixed in a future update.
- Currently Volume, Frequency and Panning are not supported as ramps from one value to another over a period of time as in allegro but I will add that as time permits.  
- The code permits samples to be played as standard files through xaudio2, or they can be mixed as streaming samples. This is great for looped samples that pop or crack no matter how much you tweak them!
- When building for x86 vs X64, you'll need to copy the correct xaudio2 dll to the release folder!
- Always, Always load/start streaming AFTER loading all samples!
I welcome anyone that wants to help make this code better, I am just an amature having fun. 


