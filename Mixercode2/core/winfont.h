#pragma once

#pragma once

#include "framework.h"

int Font_Init(int sizept);
int GetCharFontWidth(const char cCharacter);
void StartTextMode(void);
void Font_Print(int x, int y, const char* string, ...);
void Font_Print(int x, int y, uint32_t argb, const char* string, ...);
void EndTextMode(void);
void KillFont(void);

