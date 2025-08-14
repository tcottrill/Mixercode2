/* =============================================================================
 * File: iniFile.h
 * Component: INI configuration API (typed getters/setters)
 *
 * Overview
 * --------
 * Public interface for reading and writing INI settings with defaults. Offers
 * char* and std::string overloads for convenience and engine-wide consistency.
 *
 * Usage (minimal)
 * ---------------
 *   SetIniFile("cfg\\default.cfg");            // or "cfg\\mygame.cfg"
 *   int   w   = get_config_int("video", "width", 1280);
 *   bool  fs  = get_config_bool("video", "fullscreen", false);
 *   float vol = get_config_float("audio", "music_volume", 0.8f);
 *   set_config_bool("video", "fullscreen", true); // persists immediately
 *
 * API Summary
 * -----------
 *   void SetIniFile(const char* filePath);
 *
 *   // C-style getters (with defaults); string caller must delete[]
 *   int    get_config_int   (const char*, const char*, int);
 *   float  get_config_float (const char*, const char*, float);
 *   bool   get_config_bool  (const char*, const char*, bool);
 *   char*  get_config_string(const char*, const char*, const char*);
 *
 *   // C-style setters (persist immediately)
 *   void set_config_int   (const char*, const char*, int);
 *   void set_config_float (const char*, const char*, float);
 *   void set_config_bool  (const char*, const char*, bool);
 *   void set_config_string(const char*, const char*, const char*);
 *
 *   // std::string overloads
 *   int         get_config_int   (const std::string&, const std::string&, int);
 *   float       get_config_float (const std::string&, const std::string&, float);
 *   bool        get_config_bool  (const std::string&, const std::string&, bool);
 *   std::string get_config_string(const std::string&, const std::string&, const std::string&);
 *   void set_config_int   (const std::string&, const std::string&, int);
 *   void set_config_float (const std::string&, const std::string&, float);
 *   void set_config_bool  (const std::string&, const std::string&, bool);
 *   void set_config_string(const std::string&, const std::string&, const std::string&);
 *
 * Notes
 * -----
 * - The char* string getter allocates; caller must delete[] the returned buffer.
 * - Values are stored as strings in the file; typed getters perform conversions.
 * - Call SetIniFile(...) before any get/set operations.
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
 *   Copyright (C) 2012-2025  Tim Cottrill
 *   SPDX-License-Identifier: GPL-3.0-or-later
 * ============================================================================= */


#pragma once
#ifndef INI_H
#define INI_H

#include <string>

void SetIniFile(const char* szFileName);

int   get_config_int(const char* szSection, const char* szKey, int iDefaultValue);
float get_config_float(const char* szSection, const char* szKey, float fltDefaultValue);
bool  get_config_bool(const char* szSection, const char* szKey, bool bolDefaultValue);
char* get_config_string(const char* szSection, const char* szKey, const char* szDefaultValue); // Caller must free()

void set_config_int(const char* szSection, const char* szKey, int iValue);
void set_config_float(const char* szSection, const char* szKey, float fltValue);
void set_config_bool(const char* szSection, const char* szKey, bool bolValue);
void set_config_string(const char* szSection, const char* szKey, const char* szValue);

// std string overloads

int   get_config_int(const std::string& section, const std::string& key, int defaultValue);
float get_config_float(const std::string& section, const std::string& key, float defaultValue);
bool  get_config_bool(const std::string& section, const std::string& key, bool defaultValue);
std::string get_config_string(const std::string& section, const std::string& key, const std::string& defaultValue);

void set_config_int(const std::string& section, const std::string& key, int value);
void set_config_float(const std::string& section, const std::string& key, float value);
void set_config_bool(const std::string& section, const std::string& key, bool value);
void set_config_string(const std::string& section, const std::string& key, const std::string& value);
#endif // INI_H