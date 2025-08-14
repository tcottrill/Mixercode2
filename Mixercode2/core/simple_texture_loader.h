#pragma once
// ============================================================================
// simple_texture_loader.h
// Minimal OpenGL 2.1 texture loader using stb_image.h.
// - No sRGB, no glTexStorage2D, GL 2.1 friendly
// - Optional vertical flip
// - Optional mipmap generation using GL_GENERATE_MIPMAP (works on GL 2.1)
// - Defaults to forcing RGBA decode for predictable format
//
// Usage:
//   // In ONE .cpp before including this header:
//   #define SIMPLE_TEXLOADER_DEFINE_STB
//   #include "simple_texture_loader.h"
//
//   // In any other .cpp:
//   #include "simple_texture_loader.h"
//
// Dependencies:
//   - OpenGL 2.1 headers (GLEW or platform GL)
//   - stb_image.h (automatically included below)
//
// License: public domain / do what you want.
// ============================================================================

#ifndef SIMPLE_TEXTURE_LOADER_H
#define SIMPLE_TEXTURE_LOADER_H

#include "glew.h"
// --- OpenGL headers ----------------------------------------------------------
// If you use GLEW, include it before this header (recommended).
// Otherwise we include a basic GL header as a fallback.
#if !defined(__glew_h__) && !defined(GLEW_H)
#if defined(_WIN32)
#include <windows.h>
#endif
#include <GL/gl.h>
#endif

// --- stb_image setup ----------------------------------------------------------
// We include stb_image.h here. To compile its implementation in exactly ONE TU,
// define SIMPLE_TEXLOADER_DEFINE_STB before including this header in that TU.
#ifdef SIMPLE_TEXLOADER_DEFINE_STB
#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
// Prevent stb from redefining 'unused' static inline when included multiple times
#define STBI_NO_SIMD   // (optional) avoids some duplicate symbol edge cases on old compilers
#endif
#endif

// You may provide your own stb_image path in your build system.
// Here we assume it is on the include path.
#include "stb_image.h"

// --- C/C++ includes ----------------------------------------------------------
#include <cstddef>
#include <cstdint>

// -----------------------------------------------------------------------------
// LoadTexture2D_STB
// Loads an image from disk using stb_image and creates an OpenGL 2D texture.
// Targets OpenGL 2.1 only: uses glTexImage2D and (optionally) GL_GENERATE_MIPMAP.
//
// Parameters:
//   path            - UTF-8 file path to the image (must be non-null).
//   outW / outH     - Optional: return decoded width/height (can be nullptr).
//   generateMipmaps - If true, enables auto-mipmap generation (GL 2.1-friendly).
//   forceRGBA       - If true (default), force decode to 4 channels (RGBA).
//   flipY           - If true, flips the image vertically on load.
//
// Returns:
//   GLuint texture object name, or 0 on failure.
//
// Notes:
//   - We set UNPACK_ALIGNMENT = 1 to handle arbitrary widths, then restore it.
//   - Wrap is set to CLAMP_TO_EDGE; filters are set to LINEAR or MIPMAP_LINEAR.
//   - For GL 2.1 safety, we avoid modern APIs and sRGB formats.
// -----------------------------------------------------------------------------
static inline GLuint LoadTexture2D_STB(
    const char* path,
    int* outW = 0,
    int* outH = 0,
    bool generateMipmaps = true,
    bool forceRGBA = true,
    bool flipY = true)
{
    if (!path || !*path) {
        return 0;
    }

    // Configure vertical flip
    stbi_set_flip_vertically_on_load(flipY ? 1 : 0);

    int w = 0, h = 0, comp = 0;
    unsigned char* pixels = 0;

    if (forceRGBA) {
        pixels = stbi_load(path, &w, &h, &comp, 4);
        comp = pixels ? 4 : 0;
    }
    else {
        pixels = stbi_load(path, &w, &h, &comp, 0);
    }

    if (!pixels || w <= 0 || h <= 0 || comp <= 0) {
        if (pixels) stbi_image_free(pixels);
        return 0;
    }

    if (outW) *outW = w;
    if (outH) *outH = h;

    // Decide a safe GL 2.1 upload format.
    // For simplicity and widest compatibility, expand to RGBA if not already.
    GLenum dataFormat = GL_RGBA;
    GLenum internalFormat = GL_RGBA8; // available since early GL, OK on 2.1

    if (!forceRGBA) {
        if (comp == 3) {
            dataFormat = GL_RGB;
            internalFormat = GL_RGB8;
        }
        else if (comp == 4) {
            dataFormat = GL_RGBA;
            internalFormat = GL_RGBA8;
        }
        else {
            // Expand unusual channel counts to RGBA
            // (stb can do this by reloading, but to keep simple we just treat as RGBA here)
            // Re-decode to RGBA to guarantee format:
            stbi_image_free(pixels);
            pixels = stbi_load(path, &w, &h, &comp, 4);
            if (!pixels) return 0;
            comp = 4;
            dataFormat = GL_RGBA;
            internalFormat = GL_RGBA8;
        }
    }

    GLuint tex = 0;
    glGenTextures(1, &tex);
    if (!tex) {
        stbi_image_free(pixels);
        return 0;
    }

    glBindTexture(GL_TEXTURE_2D, tex);

    // Save & set UNPACK_ALIGNMENT for tight rows
    GLint prevUnpack = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpack);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    // For GL 2.1-compatible mipmap generation:
    if (generateMipmaps) {
        // GL_GENERATE_MIPMAP was the classic way prior to glGenerateMipmap.
        glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);
    }

    // Upload base level
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, dataFormat, GL_UNSIGNED_BYTE, pixels);

    // Restore unpack alignment
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpack);

    // We can free CPU memory now
    stbi_image_free(pixels);

    // Sampler state (safe defaults for GL 2.1)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    if (generateMipmaps) {
        // When GL_GENERATE_MIPMAP is TRUE, mipmaps are generated by the upload above.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    else {
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

#endif // SIMPLE_TEXTURE_LOADER_H
