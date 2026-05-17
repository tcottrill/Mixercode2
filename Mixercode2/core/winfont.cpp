#include "winfont.h"
#include <array>
#include <cstdarg>
#include <cstdio>
#include "color_defs.h"
#include <cstring>

namespace {
	constexpr int kDisplayListCount = 96;
	constexpr int kFirstChar = 32;
	constexpr int kMaxPrintBuffer = 256;

	GLuint textBase = 0;
	GLuint startTextModeList = 0;

	static void Font_PrintV(int x, int y, const char* fmt, va_list args) {
		if (!fmt) return;
		std::array<char, kMaxPrintBuffer> buffer;
		vsnprintf(buffer.data(), buffer.size(), fmt, args);
		glRasterPos2i(x, y);
		glCallLists(static_cast<GLsizei>(strlen(buffer.data())), GL_UNSIGNED_BYTE, buffer.data());
	}
}

int GetCharFontWidth(const char cCharacter)
{
	HWND hWnd = win_get_window();
	HDC hDC = GetDC(hWnd);
	SIZE kSize{};
	GetTextExtentPoint32A(hDC, &cCharacter, 1, &kSize);
	ReleaseDC(hWnd, hDC);
	return static_cast<int>(kSize.cx);
}

int Font_Init(int sizept)
{
	HWND hwnd = win_get_window();
	HDC hdc = GetDC(hwnd);
	if (!hdc) return 0;

	textBase = glGenLists(kDisplayListCount);
	if (textBase == 0)
	{
		LOG_INFO("Unable to create display lists for font");
		ReleaseDC(hwnd, hdc);
		return 0;
	}

	const long lfHeight = -MulDiv(sizept, GetDeviceCaps(hdc, LOGPIXELSY), 72);

	HFONT font = CreateFontW(
		lfHeight, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
		ANSI_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
		ANTIALIASED_QUALITY, FF_DONTCARE | DEFAULT_PITCH,
		L"Arial");

	if (!font) {
		ReleaseDC(hwnd, hdc);
		return 0;
	}

	HGDIOBJ oldfont = SelectObject(hdc, font);
	wglUseFontBitmapsA(hdc, kFirstChar, kDisplayListCount, textBase);
	SelectObject(hdc, oldfont);
	DeleteObject(font);
	ReleaseDC(hwnd, hdc);

	LOG_INFO("Font created successfully");
	return 1;
}

void StartTextMode()
{
	if (startTextModeList == 0)
	{
		startTextModeList = glGenLists(1);
		glNewList(startTextModeList, GL_COMPILE);
		glListBase(textBase - kFirstChar);
		ViewOrtho(SCREEN_W, SCREEN_H);
		glDisable(GL_DEPTH_TEST);
		glEndList();
	}

	glCallList(startTextModeList);
}

void Font_Print(int x, int y, const char* string, ...)
{
	if (!string) return;
	glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
	va_list args; va_start(args, string);
	Font_PrintV(x, y, string, args);
	va_end(args);
}

// NEW: RGBA
void Font_Print(int x, int y, uint32_t argb, const char* string, ...)
{
	if (!string) return;

	// Save current color
	GLfloat prev[4];
	glGetFloatv(GL_CURRENT_COLOR, prev);

	// Extract components and set color in one call
	GLubyte a = (GLubyte)((argb >> 24) & 0xFF);
	GLubyte r = (GLubyte)((argb >> 16) & 0xFF);
	GLubyte g = (GLubyte)((argb >> 8) & 0xFF);
	GLubyte b = (GLubyte)((argb >> 0) & 0xFF);

	glColor4ub(r, g, b, a);

	va_list args; va_start(args, string);
	Font_PrintV(x, y, string, args);
	va_end(args);

	// Restore color
	glColor4fv(prev);
}

void EndTextMode()
{
	glMatrixMode(GL_PROJECTION);
	glPopMatrix();
	glMatrixMode(GL_MODELVIEW);
	glPopMatrix();
	glListBase(0);
}

void KillFont()
{
	if (textBase) {
		glDeleteLists(textBase, kDisplayListCount);
		textBase = 0;
	}

	if (startTextModeList) {
		glDeleteLists(startTextModeList, 1);
		startTextModeList = 0;
	}
}