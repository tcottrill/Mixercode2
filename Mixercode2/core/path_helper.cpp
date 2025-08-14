
// Copyright Tim Cottrill 2025
// Release notes:
// First revision 3/25/25
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
*Copyright(C) 2022 - 2025  Tim Cottrill
* SPDX - License - Identifier : GPL - 3.0 - or -later
* ============================================================================ =
*/

#include "path_helper.h"
#include <windows.h> // For MAX_PATH & GetModuleFileName
#include "utf8conv.h"
#include "sys_log.h"

// This is a helper function to return the fullpath of a file in Unicode

//Unicode Version
std::wstring getpathU(const char* dir, const char* file)
{
	std::wstring path;
	wchar_t temppath[MAX_PATH] = { 0 }; // Buffer to hold the path

	DWORD length = GetModuleFileNameW(NULL, temppath, MAX_PATH);

	if (length == 0)
	{
		// If the function fails, it returns 0
		LOG_INFO("Failed to get the file path. Error: %ld\n", GetLastError());
	}

	// Find the last backslash and terminate the string there
	wchar_t* lastBackslash = wcsrchr(temppath, '\\');
	if (lastBackslash != NULL) {
		*lastBackslash = '\0'; // End the string at the last backslash
	}

	path.assign(temppath);

	if (dir)
	{
		path.append(win32::Utf8ToUtf16("\\"));
		path.append(win32::Utf8ToUtf16(dir));
	}
	if (file)
	{
		path.append(win32::Utf8ToUtf16("\\"));
		path.append(win32::Utf8ToUtf16(file));
	}

	LOG_INFO("getpathU returning path: %s", path.c_str());
	return path;
}


// This is the non-wide version of this code. 
std::string getpathM(const char* dir, const char* file)
{
	std::string path;
	char temppath[MAX_PATH] = { 0 }; // Buffer to hold the path

	DWORD length = GetModuleFileNameA(NULL, temppath, MAX_PATH);

	if (length == 0)
	{
		// If the function fails, it returns 0
		LOG_INFO("Failed to get the file path. Error: %ld\n", GetLastError());
	}

	// Find the last backslash and terminate the string there
	char* lastBackslash = strrchr(temppath, '\\');
	if (lastBackslash != NULL) {
		*lastBackslash = '\0'; // End the string at the last backslash
	}

	path.assign(temppath);

	if (dir)
	{
		path.append("\\");
		path.append(dir);
	}
	if (file)
	{
		path.append("\\");
		path.append(file);
	}

	LOG_INFO("getpathM returning path: %s", path.c_str());
	return path;
}


