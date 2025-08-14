#pragma once
#include <cstdint>

// -----------------------------------------------------------------------------
// Color Macros and Predefined Colors
// -----------------------------------------------------------------------------

// Pack r,g,b,a (0-255 each) into a 32-bit integer as ARGB (alpha in highest byte)
#define MAKE_ARGB(a,r,g,b) ( \
    ( (uint32_t)((a) & 0xFF) << 24 ) | \
    ( (uint32_t)((r) & 0xFF) << 16 ) | \
    ( (uint32_t)((g) & 0xFF) << 8  ) | \
    ( (uint32_t)((b) & 0xFF) ) )

// Pack r,g,b with full alpha (0xFF)
#define MAKE_RGB(r,g,b) MAKE_ARGB(0xFF,(r),(g),(b))

// Extractors
#define GET_A(color) ( (uint8_t)((color) >> 24) )
#define GET_R(color) ( (uint8_t)((color) >> 16) )
#define GET_G(color) ( (uint8_t)((color) >> 8)  )
#define GET_B(color) ( (uint8_t)((color)      ) )

// -----------------------------------------------------------------------------
// Predefined Colors (opaque)
// -----------------------------------------------------------------------------
constexpr uint32_t COLOR_WHITE = MAKE_RGB(255, 255, 255);
constexpr uint32_t COLOR_BLACK = MAKE_RGB(0, 0, 0);
constexpr uint32_t COLOR_RED = MAKE_RGB(255, 0, 0);
constexpr uint32_t COLOR_GREEN = MAKE_RGB(0, 255, 0);
constexpr uint32_t COLOR_BLUE = MAKE_RGB(0, 0, 255);
constexpr uint32_t COLOR_YELLOW = MAKE_RGB(255, 255, 0);
constexpr uint32_t COLOR_CYAN = MAKE_RGB(0, 255, 255);
constexpr uint32_t COLOR_MAGENTA = MAKE_RGB(255, 0, 255);
