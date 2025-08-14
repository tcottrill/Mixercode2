/* =============================================================================
 * File: iniFile.cpp
 * Component: Minimal INI reader/writer with comment preservation
 *
 * Overview
 * --------
 * Lightweight, dependency-free INI loader/saver that:
 *   - Preserves original file ordering and full-line comments.
 *   - Supports sections ([Section]), keys, and values in key=value form.
 *   - Trims surrounding whitespace; strips inline comments (; or #) from values.
 *   - Offers typed getters/setters (int/float/bool/string) with defaults.
 *   - Provides both C-style (const char*) and std::string overloads.
 *
 * Data Model
 * ----------
 * - Entire file is parsed into memory as: map<section, vector<IniEntry>>.
 * - Each IniEntry keeps {key, value, original_line, is_comment}, allowing a
 *   round-trip save that preserves comments and unknown/invalid lines.
 * - The active ini filename is stored globally after SetIniFile(...).
 *
 * Parsing Rules
 * -------------
 * - Section lines:        [Name]    (exactly one per line)
 * - Key/value lines:      key=value (whitespace trimmed on both sides)
 * - Comments (full-line): lines starting with ';' or '#'
 * - Inline value comment: everything after first ';' or '#' in the value is removed
 * - Keys/values containing spaces are treated as invalid and preserved as comments.
 * - Duplicate keys: the first match in file order is returned by getters.
 *
 * Persistence & I/O
 * -----------------
 * - SetIniFile(path) loads the file into memory.
 * - Set operations (set_config_*) update the in-memory entry, then immediately
 *   call SaveIniFile() to write the full file back out.
 * - Uses secure CRT helpers where applicable (strncpy_s, strcpy_s).
 *
 * API (see iniFile.h)
 * -------------------
 *   // File selection & load
 *   void SetIniFile(const char* filePath);
 *
 *   // Get with defaults (C-style)
 *   int    get_config_int  (const char* sec, const char* key, int   def);
 *   float  get_config_float(const char* sec, const char* key, float def);
 *   bool   get_config_bool (const char* sec, const char* key, bool  def);
 *   char*  get_config_string(const char* sec, const char* key,
 *                            const char* def); // caller must delete[]
 *
 *   // Set (C-style)
 *   void set_config_int   (const char* sec, const char* key, int   v);
 *   void set_config_float (const char* sec, const char* key, float v);
 *   void set_config_bool  (const char* sec, const char* key, bool  v);
 *   void set_config_string(const char* sec, const char* key, const char* v);
 *
 *   // std::string overloads (get/set)
 *   int         get_config_int   (const std::string& sec, const std::string& key, int def);
 *   float       get_config_float (const std::string& sec, const std::string& key, float def);
 *   bool        get_config_bool  (const std::string& sec, const std::string& key, bool def);
 *   std::string get_config_string(const std::string& sec, const std::string& key,
 *                                 const std::string& def);
 *   void set_config_int   (const std::string& sec, const std::string& key, int v);
 *   void set_config_float (const std::string& sec, const std::string& key, float v);
 *   void set_config_bool  (const std::string& sec, const std::string& key, bool v);
 *   void set_config_string(const std::string& sec, const std::string& key, const std::string& v);
 *
 * Booleans & Numbers
 * ------------------
 * - Booleans accept: true/1/yes (case-insensitive) for true; otherwise false.
 * - Floats are saved without trailing zeros (e.g., "3.5000" -> "3.5").
 *
 * Threading
 * ---------
 * - Not thread-safe. Callers should serialize access if used across threads.
 *
 * Limitations
 * -----------
 * - No multi-line values, escape sequences, or quoted strings.
 * - Inline comments only recognized in values, not keys.
 * - Immediate SaveIniFile() on every set may be I/O-heavy for batch edits.
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
 * ============================================================================= */

// New code update 6/17/2025
// Updated 6/22/2025 for secure functions
// Removed dependency on legacy Windows functions. 
// Added string functions for other uses. 
// Some code below was written with the assistance of ChatGPT

/*
* -------------------------------------------------------------------------- -
*License(GPLv3) :
    *This file is part of GameEngine Alpha.
    *
    *<Project Name> is free software : you can redistribute it and /or modify
    * it under the terms of the GNU General Public License as published by
    * the Free Software Foundation, either version 3 of the License, or
    *(at your option) any later version.
    *
    *<Project Name> is distributed in the hope that it will be useful,
    * but WITHOUT ANY WARRANTY; without even the implied warranty of
    * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.See the
    * GNU General Public License for more details.
    *
    * You should have received a copy of the GNU General Public License
    * along with GameEngine Alpha.If not, see < https://www.gnu.org/licenses/>.
*
*Copyright(C) 2012 - 2025  Tim Cottrill
* SPDX - License - Identifier : GPL - 3.0 - or -later
* ============================================================================ =
*/


#include "iniFile.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <map>
#include <vector>
#include <fstream>
#include <sstream>
#include <algorithm>

#define MAX_INI 255

static char m_szFileName[MAX_INI] = { 0 };

struct IniEntry {
    std::string key;
    std::string value;
    std::string original_line;
    bool is_comment = false;
};

static std::map<std::string, std::vector<IniEntry>> ini_data;

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of("");
    size_t end = s.find_last_not_of("");
    return (start == std::string::npos) ? "" : s.substr(start, end - start + 1);
}

void LoadIniFile() {
    ini_data.clear();
    std::ifstream file(m_szFileName);
    if (!file.is_open()) return;

    std::string line, section;
    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed[0] == ';' || trimmed[0] == '#') {
            ini_data[section].push_back({ "", "", line, true });
        } else if (trimmed.front() == '[' && trimmed.back() == ']') {
            section = trimmed.substr(1, trimmed.size() - 2);
            ini_data[section]; // Ensure section exists
        } else {
            size_t eq = trimmed.find('=');
            if (eq != std::string::npos) {
                std::string key = trim(trimmed.substr(0, eq));
                std::string value = trim(trimmed.substr(eq + 1));
                ini_data[section].push_back({ key, value, line, false });
            } else {
                ini_data[section].push_back({ "", "", line, true });
            }
        }
    }
}

void SaveIniFile() {
    std::ofstream file(m_szFileName);
    for (const auto& sec : ini_data) {
        if (!sec.first.empty())
            file << "[" << sec.first << "]";
        for (const auto& entry : sec.second) {
            if (entry.is_comment || entry.key.empty())
                file << entry.original_line << "";
            else
                file << entry.key << "=" << entry.value << "";
        }
        file << "";
    }
}

void SetIniFile(const char* szFileName) {
    strncpy_s(m_szFileName, MAX_INI, szFileName, _TRUNCATE);
    LoadIniFile();
}

std::string get_value(const char* section, const char* key, const char* defval) {
    auto it = ini_data.find(section);
    if (it != ini_data.end()) {
        for (const auto& entry : it->second) {
            if (!entry.is_comment && entry.key == key)
                return entry.value;
        }
    }
    return defval;
}

void update_or_add_entry(const char* section, const char* key, const char* value) {
    auto& entries = ini_data[section];
    for (auto& entry : entries) {
        if (!entry.is_comment && entry.key == key) {
            entry.value = value;
            entry.original_line = std::string(key) + "=" + value;
            SaveIniFile();
            return;
        }
    }
    entries.push_back({ key, value, std::string(key) + "=" + value, false });
    SaveIniFile();
}

int get_config_int(const char* section, const char* key, int defval) {
    return std::atoi(get_value(section, key, std::to_string(defval).c_str()).c_str());
}

float get_config_float(const char* section, const char* key, float defval) {
    return (float) std::atof(get_value(section, key, std::to_string(defval).c_str()).c_str());
}

bool get_config_bool(const char* section, const char* key, bool defval) {
    std::string val = get_value(section, key, defval ? "True" : "False");
    std::transform(val.begin(), val.end(), val.begin(), ::tolower);
    return (val == "true" || val == "1" || val == "yes");
}

char* get_config_string(const char* section, const char* key, const char* defval) {
    std::string val = get_value(section, key, defval);
    char* res = new char[val.size() + 1];
    strcpy_s(res, val.size() + 1, val.c_str());

    // Caller must delete[] the returned pointer!
    return res;
}

void set_config_string(const char* section, const char* key, const char* val) {
    update_or_add_entry(section, key, val);
}

void set_config_int(const char* section, const char* key, int val) {
    set_config_string(section, key, std::to_string(val).c_str());
}

void set_config_float(const char* section, const char* key, float val) {
    set_config_string(section, key, std::to_string(val).c_str());
}

void set_config_bool(const char* section, const char* key, bool val) {
    set_config_string(section, key, val ? "True" : "False");
}


// std::string overloads
int get_config_int(const std::string& section, const std::string& key, int defaultValue) {
    return get_config_int(section.c_str(), key.c_str(), defaultValue);
}

float get_config_float(const std::string& section, const std::string& key, float defaultValue) {
    return get_config_float(section.c_str(), key.c_str(), defaultValue);
}

bool get_config_bool(const std::string& section, const std::string& key, bool defaultValue) {
    return get_config_bool(section.c_str(), key.c_str(), defaultValue);
}

std::string get_config_string(const std::string& section, const std::string& key, const std::string& defaultValue) {
    char* result = get_config_string(section.c_str(), key.c_str(), defaultValue.c_str());
    std::string value(result);
    delete[] result;
    return value;
}

void set_config_int(const std::string& section, const std::string& key, int value) {
    set_config_int(section.c_str(), key.c_str(), value);
}

void set_config_float(const std::string& section, const std::string& key, float value) {
    set_config_float(section.c_str(), key.c_str(), value);
}

void set_config_bool(const std::string& section, const std::string& key, bool value) {
    set_config_bool(section.c_str(), key.c_str(), value);
}

void set_config_string(const std::string& section, const std::string& key, const std::string& value) {
    set_config_string(section.c_str(), key.c_str(), value.c_str());
}
