// Includes
#define NOMINMAX
#include <windows.h>
#include "glew.h"
#include "wglew.h"
#include "sys_log.h"
#include "winfont.h"
#include "rawinput.h"
#include "fileio.h"
#include "iniFile.h"
#include <vector>
#include "mmtimer.h"
#include "mixer.h"
#include "path_helper.h"
#include <iostream>
#include <algorithm> // for std::min/std::max if you want, or use clamp elsewhere
#include <string>
#include <array>
#define SIMPLE_TEXLOADER_DEFINE_STB
#include "simple_texture_loader.h"
#include "color_defs.h"
#include "mixer/XAudio2Stream.h"
#include "FrameLimiter.h"
//Library Includes
#pragma comment(lib, "opengl32.lib")

//Globals
HWND hWnd;
int SCREEN_W = 1024;
int SCREEN_H = 768;
GLuint g_bgTex = 0;
static int g_winWidth = 0;
static int g_winHeight = 0;

static int TARGET_FRAMERATE = 30;
static int TARGET_FREQUENCY = 44100;

// Track current pan/vol for display (0..255)
static int  g_curPan[8] = {};  // init to center
static int  g_curVol[8] = {};  // init to full

// Selected Sample
static int  g_chanSampleId[8] = {};

// Track global audio pause state for UI + toggling
static bool g_audioPaused = false;

// Currently selected channel for pan/vol control
static int g_selectedCh = 0;

// Master volume percent we control from the UI (0..100)
static int g_masterVolPercent = 100;

// helper for selecting sample and starting mixer.
static inline void start_and_select_channel(int ch) {
	const int id = g_chanSampleId[ch];      // sample ID you stored at load time
	const int idx = snumlookup(id);          // ID -> lsamples index
	if (idx >= 0) {
		g_selectedCh = ch;
		sample_start_mixer(ch, idx, 0);
		g_curPan[ch] = 128;
		g_curVol[ch] = 255;
	}
	else {
		LOG_ERROR("start_and_select_channel: no index for sample ID %d on ch %d", id, ch);
	}
}
// Function Declarations
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
void EnableOpenGL(HWND hWnd, HDC* hDC, HGLRC* hRC);
void DisableOpenGL(HWND hWnd, HDC hDC, HGLRC hRC);


void ViewOrtho(int width, int height)
{
	glViewport(0, 0, width, height);             // Set Up An Ortho View
	glMatrixMode(GL_PROJECTION);			  // Select Projection
	glLoadIdentity();						  // Reset The Matrix
	glOrtho(0, width, 0, height, -1, 1);	  // Select Ortho 2D Mode DirectX style(640x480)
	glMatrixMode(GL_MODELVIEW);				  // Select Modelview Matrix
	glLoadIdentity();						  // Reset The Matrix
}

// -----------------------------------------------------------------------------
// DrawTextBackground
// Draws a semi-transparent quad with a dark border behind the given text area.
// Position is in screen coordinates, for an ortho 0..width, 0..height viewport.
//
// Parameters:
//   x, y     - top-left corner of the background
//   w, h     - width and height of the background
//   alpha    - 0..1 transparency for the fill (border is drawn slightly darker)
// -----------------------------------------------------------------------------
void DrawTextBackground(float x, float y, float w, float h, float alpha)
{
	glDisable(GL_TEXTURE_2D);

	// --- Fill ---
	glColor4f(0.0f, 0.0f, 0.0f, alpha); // black, semi-transparent
	glBegin(GL_QUADS);
	glVertex2f(x, y);
	glVertex2f(x + w, y);
	glVertex2f(x + w, y + h);
	glVertex2f(x, y + h);
	glEnd();

	// --- Border ---
	glColor4f(0.0f, 0.0f, 0.0f, alpha + 0.2f); // slightly less transparent
	glLineWidth(1.0f);
	glBegin(GL_LINE_LOOP);
	glVertex2f(x, y);
	glVertex2f(x + w, y);
	glVertex2f(x + w, y + h);
	glVertex2f(x, y + h);
	glEnd();

	glEnable(GL_TEXTURE_2D);
}

//========================================================================
// Return the Window Handle
//========================================================================
HWND win_get_window()
{
	return hWnd;
}

// Simple Key Handler
int KeyCheck(int keynum)
{
	static int keys[256];

	static int hasrun = 0;
	int i;

	if (hasrun == 0)
	{
		for (i = 0; i < 256; i++)
		{
			keys[i] = 0;
		}
		hasrun = 1;
	}
	if (!keys[keynum] && key[keynum]) //Return True if not in que
	{
		keys[keynum] = 1;
		return 1;
	}
	else if (keys[keynum] && !key[keynum]) //Return False if in que
		keys[keynum] = 0;
	return 0;
}

// -----------------------------------------------------------------------------
// KeyRepeat
// Tracks repeat state for a single key with an initial delay and repeat rate.
// Call KeyRepeat_Step() once per frame with the key's current "down" state.
//
// Parameters (to KeyRepeat_Step):
//   st            - per-key state (persist between frames)
//   isDown        - current down/up state for the key
//   initialDelay  - frames to wait after the first press before repeating
//   repeatRate    - frames between repeat triggers after the delay
//
// Returns:
//   true if the action should fire on this frame (first press OR repeat).
// -----------------------------------------------------------------------------
struct KeyRepeat {
	int  counter = 0;
	bool wasDown = false;
};

static inline bool KeyRepeat_Step(KeyRepeat& st, bool isDown,
	int initialDelay, int repeatRate)
{
	if (isDown) {
		if (!st.wasDown) {
			// First press: fire now; start negative counter to create the delay
			st.wasDown = true;
			st.counter = -initialDelay;
			return true;
		}
		// Held: count up; fire every repeatRate frames after the delay
		st.counter++;
		if (st.counter >= 0 && (st.counter % repeatRate) == 0)
			return true;
	}
	else {
		// Released: reset
		st.wasDown = false;
		st.counter = 0;
	}
	return false;
}

static KeyRepeat repLeft, repRight, repUp, repDown, repZ, repX;

// -----------------------------------------------------------------------------
// DrawVUMeters
// Draws two vertical bars (L/R) with a simple green->yellow->red gradient.
// Values are 0..1. Coordinates in current ortho (pixel) space.
// -----------------------------------------------------------------------------
static void DrawVUMeters(int x, int y, int w, int h, float vuL, float vuR)
{
	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);

	// Background panel
	glColor4f(0.f, 0.f, 0.f, 0.5f);
	glBegin(GL_QUADS);
	glVertex2f((GLfloat)x, (GLfloat)y);
	glVertex2f((GLfloat)(x + w), (GLfloat)y);
	glVertex2f((GLfloat)(x + w), (GLfloat)(y + h));
	glVertex2f((GLfloat)x, (GLfloat)(y + h));
	glEnd();

	auto drawBar = [&](int bx, float v)
		{
			v = std::clamp(v, 0.0f, 1.0f);
			int filled = (int)(v * (h - 6)); // padding

			// Color ramp: 0..0.7 = green->yellow, 0.7..1.0 = yellow->red
			auto setColorForT = [](float t)
				{
					if (t < 0.7f) {
						float u = t / 0.7f; // 0..1
						glColor4f(0.0f + u, 1.0f, 0.0f, 1.0f); // (0,1,0) -> (1,1,0)
					}
					else {
						float u = (t - 0.7f) / 0.3f; // 0..1
						glColor4f(1.0f, 1.0f - u, 0.0f, 1.0f); // (1,1,0) -> (1,0,0)
					}
				};

			// Draw filled bar in small horizontal slices to get the gradient
			const int slice = 4; // pixels
			int top = y + 3;
			for (int yy = 0; yy < filled; yy += slice) {
				float t = (float)yy / (float)(h - 6);
				setColorForT(t);
				int y0 = top + yy;
				int y1 = std::min(top + yy + slice, y + h - 3);
				glBegin(GL_QUADS);
				glVertex2f((GLfloat)(bx), (GLfloat)(y0));
				glVertex2f((GLfloat)(bx + (w / 2 - 4)), (GLfloat)(y0));
				glVertex2f((GLfloat)(bx + (w / 2 - 4)), (GLfloat)(y1));
				glVertex2f((GLfloat)(bx), (GLfloat)(y1));
				glEnd();
			}

			// Outline
			glColor4f(1.f, 1.f, 1.f, 0.9f);
			glBegin(GL_LINE_LOOP);
			glVertex2f((GLfloat)(bx), (GLfloat)(y + 3));
			glVertex2f((GLfloat)(bx + (w / 2 - 4)), (GLfloat)(y + 3));
			glVertex2f((GLfloat)(bx + (w / 2 - 4)), (GLfloat)(y + h - 3));
			glVertex2f((GLfloat)(bx), (GLfloat)(y + h - 3));
			glEnd();
		};

	// Fetch current VU levels from mixer
	float l = 0.f, r = 0.f;
	mixer_get_vu(&l, &r);

	// Left and right bars side-by-side
	drawBar(x + 3, l);
	drawBar(x + (w / 2) + 1, r);

	// Labels
	//StartTextMode();
	Font_Print(x-6, (y+30)+ h + 4, "VU METER");
	Font_Print(x + 6, y + h + 4, " L");
	Font_Print(x + (w / 2) + 6, y + h + 4, " R");
	//EndTextMode();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
	WNDCLASS wc = {};
	HWND hWnd;
	HDC hDC;
	HGLRC hRC;
	MSG msg = {};
	BOOL quit = FALSE;

	wc.style = CS_OWNDC;
	wc.lpfnWndProc = WndProc;
	wc.hInstance = hInstance;
	wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wc.hCursor = LoadCursor(NULL, IDC_ARROW);
	wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wc.lpszClassName = L"Mixercode Demo";
	RegisterClass(&wc);

	// Create main window (uses your SCREEN_W/SCREEN_H)
	hWnd = CreateWindow(L"Mixercode Demo", L"Mixercode Demo",
		WS_CAPTION | WS_POPUPWINDOW | WS_VISIBLE,
		0, 0, SCREEN_W, SCREEN_H, NULL, NULL, hInstance, NULL);

	// Set CWD to exe directory (your helper)
	std::wstring temppath = getpathU(0, 0);
	SetCurrentDirectoryW(temppath.c_str());

	// --- Init ---
	if (!Log::open("testlog.txt")) { return 1; }
	LOG_INFO("Opening Log");
	
	EnableOpenGL(hWnd, &hDC, &hRC);
	//if (wglSwapIntervalEXT) wglSwapIntervalEXT(0);
	Font_Init(20);
	RawInput_Initialize(hWnd);
	
	// Fix the Window size and center it onthe screen.
	RECT want{ 0,0,SCREEN_W,SCREEN_H };
	AdjustWindowRectEx(&want, (DWORD)GetWindowLongPtr(hWnd, GWL_STYLE), FALSE,
		(DWORD)GetWindowLongPtr(hWnd, GWL_EXSTYLE));
	int ww = want.right - want.left, wh = want.bottom - want.top;
	int sx = (GetSystemMetrics(SM_CXSCREEN) - ww) / 2;
	int sy = (GetSystemMetrics(SM_CYSCREEN) - wh) / 2;
	SetWindowPos(hWnd, nullptr, sx, sy, ww, wh, SWP_NOZORDER | SWP_NOACTIVATE);

	ViewOrtho(SCREEN_W, SCREEN_H);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	int bgW = 0, bgH = 0;
	g_bgTex = LoadTexture2D_STB("data/mixerbackground_named.png", &bgW, &bgH, false, true, true);
	if (!g_bgTex) {
		LOG_ERROR("Failed to load mixer background texture.");
	}
	else {
		LOG_INFO("Loaded background texture %dx%d", bgW, bgH);
	}

	//
	// Mixer: 44.1k/60 fps but can be set to anything 22050,30fps, etc...
	// IMPORTANT NOTE!
	// If the loaded samples do not match the set frequency, they are resampled in order
	// to play correctly with sample_start_mixer(). If you are not going to be using the mixer and are just playing
	// straight samples, be sure to to load them with load_sample(archive,sample,false);
	//
	mixer_init(TARGET_FREQUENCY, TARGET_FRAMERATE);
	FrameLimiter::Init((double) TARGET_FRAMERATE);

	// Load samples and remember IDs per channel row
	g_chanSampleId[0] = load_sample(0, "data\\musicstereo.wav");
	g_chanSampleId[1] = load_sample(0, "data\\InGame1Loop.wav");
	g_chanSampleId[2] = load_sample(0, "data\\sfx_zap.wav");
	g_chanSampleId[3] = load_sample(0, "data\\fire.wav");
	g_chanSampleId[4] = load_sample(0, "data\\Natural Vibes.mp3");
	g_chanSampleId[5] = load_sample(0, "data\\sfx_lose.ogg");
	g_chanSampleId[6] = load_sample(0, "data\\drone_22050.wav");
	g_chanSampleId[7] = load_sample(0, "data\\strings2mono.wav");

	// Optional save a copy of a sample after modifying or resampling.
	//save_sample(6);

	
	for (int i = 0; i < 8; ++i) {
		g_curPan[i] = 128;
		g_curVol[i] = 255;
	}

	// ---- Main loop ----
	while (!quit)
	{
		if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT) {
				quit = TRUE;
			}
			else {
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		else
		{
			if (IsIconic(hWnd) || GetForegroundWindow() != hWnd) {
				Sleep(10); // 
			}
			// ---------- INPUT ----------
		    
			// Panic: stop all
			if (KeyCheck(KEY_S)) {
				samples_stop_all();
			}

			// Toggle pause (P): mutes/unmutes and sets paused state
			if (KeyCheck(KEY_P)) {
				if (!g_audioPaused) { pause_audio();  g_audioPaused = true; }
				else { restore_audio(); g_audioPaused = false; }
			}

			// Number keys: select channel and (re)start its sample
			if (KeyCheck(KEY_0)) start_and_select_channel(0);
			if (KeyCheck(KEY_1)) start_and_select_channel(1);
			if (KeyCheck(KEY_2)) start_and_select_channel(2);
			if (KeyCheck(KEY_3)) start_and_select_channel(3);
			if (KeyCheck(KEY_4)) start_and_select_channel(4);
			if (KeyCheck(KEY_5)) start_and_select_channel(5);
			if (KeyCheck(KEY_6)) start_and_select_channel(6);
			if (KeyCheck(KEY_7)) start_and_select_channel(7);

			// Group-start (9): start all rows once; select last
			if (KeyCheck(KEY_9)) {
				for (int ch = 0; ch < 8; ++ch) {
					const int id = g_chanSampleId[ch];
					const int idx = snumlookup(id);
					if (idx >= 0) {
						sample_start_mixer(ch, idx, 0);
						g_curPan[ch] = 128;
						g_curVol[ch] = 255;
					}
					else {
						LOG_ERROR("start_all: no index for sample ID %d on ch %d", id, ch);
					}
				}
				g_selectedCh = 7;
			}
						
			// Tuning for the key repeat
			constexpr int kAdjustStepPan = 4;
			constexpr int kAdjustStepVol = 4;
			constexpr int kInitialDelay = 12; // frames before auto-repeat
			constexpr int kRepeatRate = 4;  // frames between repeats

			{
				const int ch = g_selectedCh;

				if (KeyRepeat_Step(repLeft, key[KEY_LEFT], kInitialDelay, kRepeatRate)) {
					g_curPan[ch] = std::max(0, g_curPan[ch] - kAdjustStepPan);
					sample_set_pan(ch, g_curPan[ch]);
				}
				if (KeyRepeat_Step(repRight, key[KEY_RIGHT], kInitialDelay, kRepeatRate)) {
					g_curPan[ch] = std::min(255, g_curPan[ch] + kAdjustStepPan);
					sample_set_pan(ch, g_curPan[ch]);
				}
				if (KeyRepeat_Step(repUp, key[KEY_UP], kInitialDelay, kRepeatRate)) {
					g_curVol[ch] = std::min(255, g_curVol[ch] + kAdjustStepVol);
					sample_set_volume(ch, g_curVol[ch]);
				}
				if (KeyRepeat_Step(repDown, key[KEY_DOWN], kInitialDelay, kRepeatRate)) {
					g_curVol[ch] = std::max(0, g_curVol[ch] - kAdjustStepVol);
					sample_set_volume(ch, g_curVol[ch]);
				}

				// Master volume Z/X 
				if (KeyRepeat_Step(repZ, key[KEY_Z], kInitialDelay, kRepeatRate)) {
					g_masterVolPercent = std::max(0, g_masterVolPercent - 5);
					mixer_set_master_volume(g_masterVolPercent);
				}
				if (KeyRepeat_Step(repX, key[KEY_X], kInitialDelay, kRepeatRate)) {
					g_masterVolPercent = std::min(100, g_masterVolPercent + 5);
					mixer_set_master_volume(g_masterVolPercent);
				}
			}

			if (KeyCheck(KEY_ESC)) { quit = TRUE; }

			// ---------- DRAW HUD ----------
			glClearColor(0.f, 0.f, 0.f, 0.f);
			glClear(GL_COLOR_BUFFER_BIT);
			glColor3f(1.f, 1.f, 1.f);
			
			if (g_bgTex) {
				glEnable(GL_TEXTURE_2D);
				glBindTexture(GL_TEXTURE_2D, g_bgTex);

				glColor4f(1.f, 1.f, 1.f, 1.f); // No tint, full opacity

				glBegin(GL_QUADS);
				glTexCoord2f(0.f, 0.f); glVertex2f(0.f, 0.f);
				glTexCoord2f(1.f, 0.f); glVertex2f((GLfloat)SCREEN_W, 0.f);
				glTexCoord2f(1.f, 1.f); glVertex2f((GLfloat)SCREEN_W, (GLfloat)SCREEN_H);
				glTexCoord2f(0.f, 1.f); glVertex2f(0.f, (GLfloat)SCREEN_H);
				glEnd();

				glBindTexture(GL_TEXTURE_2D, 0);
				glDisable(GL_TEXTURE_2D);
			}

			// Position
			float tx = 100.0f;
			float ty = 50.0f;

			// Draw background first
			DrawTextBackground(tx - 4, ty - 4, 800, 550, 0.5f);

			StartTextMode();

			// Layout tuned for 1024x768
			const int lineH = 32;                      //  line spacing
			const int blockW = 750;                     // target text width 
			const int colX = (SCREEN_W - blockW) / 2; // centered block
			int y = (int)(SCREEN_H * 0.70f); // sit lower on the screen
			int arrowX;
			float masterLin = mixer_get_master_volume(); // 0..1 linear
			uint32_t scolor = COLOR_WHITE;
			
			// Determine overall audio state
			bool anyPlaying = false;
			for (int ch = 0; ch < 8; ++ch) {
				if (sample_playing_mixer(ch)) {
					anyPlaying = true;
					break;
				}
			}

			const char* audioStatus = nullptr;
			if (g_audioPaused) {
				audioStatus = "PAUSED";
			}
			else if (anyPlaying) {
				audioStatus = "PLAYING";
			}
			else {
				audioStatus = "IDLE";
			}

			// Header split across multiple lines 
			Font_Print(colX, y,
				"Audio: %s   |   Selected: CH%d",
				audioStatus, g_selectedCh);
			y -= lineH;

			Font_Print(colX, y,
				"Master: %d%%  (lin %.2f)   |   Z/X = Master Volume (-/+)",
				g_masterVolPercent, masterLin);
			y -= lineH;

			Font_Print(colX, y,
				"Pause/Resume: P   |   Stop All: S");
			y -= (lineH + 8);

			// Controls 
			Font_Print(colX, y,
				"Arrow keys: Pan (Left/Right), Volume (Up/Down).");
			y -= lineH;

			Font_Print(colX, y,
				"0..7 = Start/Select   |   9 = Start All");
			y -= (lineH + 10);

			// Table header
			Font_Print(colX, y, "CH Play  Pan Vol    Name");
			y -= lineH;

			// Per-channel rows
			for (int ch = 0; ch < 8; ++ch) {
				const int playing = sample_playing_mixer(ch);
				const int pan = g_curPan[ch];
				const int vol = g_curVol[ch];
				const int sid = g_chanSampleId[ch];
				std::string nameStr = (sid >= 0) ? numToName(sid) : "";
				const char* name = nameStr.empty() ? "(none)" : nameStr.c_str();
				
				if (ch == g_selectedCh) {
					arrowX = colX - GetCharFontWidth('>');
					scolor = COLOR_YELLOW;
				}
				else {
					arrowX = colX;
					scolor = COLOR_WHITE;
				}
				
				Font_Print(arrowX, y, scolor, "%c%2d    %d    %3d  %3d     %s",
					(ch == g_selectedCh) ? '>' : ' ',
					ch, playing, pan, vol, name);
				y -= lineH;
			}

			// Draw compact VU meters near the bottom-left
			{
				const int vuX = 650;
				const int vuY = 160;
				const int vuW = 100;  // panel width covering both bars
				const int vuH = 180;  // bar height
				// Note: DrawVUMeters calls StartTextMode() for its labels, so just place it
				DrawVUMeters(vuX, vuY, vuW, vuH, 0.0f, 0.0f); // levels fetched internally
			}

			EndTextMode();

			// Mixer + throtte speed plus Ogl buffer swap
			mixer_update();
			FrameLimiter::Throttle();
			SwapBuffers(hDC);
		}
	}

	// ---- Shutdown ----
	DisableOpenGL(hWnd, hDC, hRC);
	KillFont();
	mixer_end();

	if (g_bgTex) {
		glDeleteTextures(1, &g_bgTex);
		g_bgTex = 0;
	}
	FrameLimiter::Shutdown();
	LOG_INFO("Closing Log");
	LogClose();
	DestroyWindow(hWnd);
	return (int)msg.wParam;
}

// Window Callback Procedure
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_CREATE:
		return 0;

	case WM_CLOSE:
		// Gracefully destroy; WM_DESTROY will post quit.
		DestroyWindow(hWnd);
		return 0;

	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	
	case WM_SIZE: {
		g_winWidth = LOWORD(lParam);
		g_winHeight = HIWORD(lParam);
		// Option A: fixed logical 1024x768 with stretch
		glViewport(0, 0, g_winWidth, g_winHeight);
		// Option B: strict logical ortho (no stretch)
		//ViewOrtho(SCREEN_W, SCREEN_H);
		return 0;
	}
	case WM_INPUT:
		// Let your raw input handler decide the LRESULT (common to return 0 if handled).
		return RawInput_ProcessInput(hWnd, wParam, lParam);

	case WM_SYSCOMMAND:
	{
		switch (wParam & 0xFFF0)
		{
		case SC_SCREENSAVE:
		case SC_MONITORPOWER:
			// Prevent screen saver/monitor power while app is active.
			return 0;

		case SC_CLOSE:
			DestroyWindow(hWnd);
			return 0;

		case SC_KEYMENU:
			// Block ALT activation of the system menu (and beep).
			return 0;

		default:
			break;
		}
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	case WM_KEYDOWN:
		if (wParam == VK_ESCAPE)
		{
			PostQuitMessage(0);
			return 0;
		}
		// Not handled: fall through to default processing.
		return DefWindowProc(hWnd, message, wParam, lParam);

	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
}
// Enable OpenGL

void EnableOpenGL(HWND hWnd, HDC* hDC, HGLRC* hRC)
{
	PIXELFORMATDESCRIPTOR pfd;
	int format;

	// get the device context (DC)
	*hDC = GetDC(hWnd);

	// set the pixel format for the DC
	ZeroMemory(&pfd, sizeof(pfd));
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	pfd.iLayerType = PFD_MAIN_PLANE;
	format = ChoosePixelFormat(*hDC, &pfd);
	SetPixelFormat(*hDC, format, &pfd);

	// create and enable the render context (RC)
	*hRC = wglCreateContext(*hDC);
	wglMakeCurrent(*hDC, *hRC);

	GLenum err = glewInit();
	if (GLEW_OK != err) {
		LOG_INFO("Error: %s", glewGetErrorString(err));
	}
	LOG_INFO("Status: Using GLEW %s", glewGetString(GLEW_VERSION));
}

// Disable OpenGL

void DisableOpenGL(HWND hWnd, HDC hDC, HGLRC hRC)
{
	wglMakeCurrent(NULL, NULL);
	wglDeleteContext(hRC);
	ReleaseDC(hWnd, hDC);
}