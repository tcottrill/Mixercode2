#pragma once

#ifndef SYS_DIR_H
#define SYS_DIR_H

#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// sys_dir.h
//
// Directory enumeration for UI file pickers and similar tools. Uses
// std::filesystem under the hood. All paths on the boundary are UTF-8 encoded
// std::string; conversion to/from the platform wide-path type happens inside
// the implementation via std::filesystem::u8path / path::u8string.
//
// Nothing here throws. Errors (missing path, permission denied, etc.) return
// false / empty values.
// -----------------------------------------------------------------------------


// -----------------------------------------------------------------------------
// DirEntry
// A single entry returned from EnumerateDrives or EnumerateDirectory. Holds
// the display name (not a full path) and whether it is a directory.
// -----------------------------------------------------------------------------
struct DirEntry
{
	std::string name;                // display name; e.g. "foo.txt", "Documents", "..", "C:\\"
	bool        isDirectory = false;
};


// -----------------------------------------------------------------------------
// EnumerateDrives
// Populate 'out' with available logical drives.
//
// On Windows: one entry per connected drive, with 'name' formatted as "C:\\",
// "D:\\" etc. and 'isDirectory' = true.
//
// On other platforms: a single entry { name = "/", isDirectory = true }.
//
// Returns false on failure. 'out' is cleared before any entries are added.
// -----------------------------------------------------------------------------
bool EnumerateDrives(std::vector<DirEntry>& out);


// -----------------------------------------------------------------------------
// EnumerateDirectory
// Populate 'out' with the contents of 'path'.
//
//   path   Absolute path to enumerate (e.g. "C:\\Users\\Colleen"). On Windows
//          this may also be a drive root like "C:\\".
//   filter Windows-style semicolon-separated pattern list: "*.png;*.jpg".
//          An empty string or "*" accepts all files. Filtering applies only
//          to files; directories are always included. Matching is
//          case-insensitive. Only '*.ext' style patterns are supported --
//          no '?' wildcard, no character classes.
//   out    Populated with entries. Cleared first. A ".." entry is inserted
//          as the first element if 'path' is not a drive root, so callers do
//          not have to synthesize one. Remaining entries are sorted with
//          directories first (alphabetical), then files (alphabetical).
//
// Returns false on failure (path does not exist, enumeration fails, etc.).
// On false, 'out' is left empty.
// -----------------------------------------------------------------------------
bool EnumerateDirectory(const std::string& path,
                        const std::string& filter,
                        std::vector<DirEntry>& out);


// -----------------------------------------------------------------------------
// GetParentDirectory
// Returns the parent of 'path'.
//
// If 'path' is a drive root (e.g. "C:\\") or otherwise has no parent, returns
// an empty string. Callers should treat the empty return as a signal to fall
// back to EnumerateDrives ("show the drive list").
// -----------------------------------------------------------------------------
std::string GetParentDirectory(const std::string& path);


// -----------------------------------------------------------------------------
// JoinPath
// Joins a directory path and an entry name into a full path, using the
// platform's preferred separator. Handles the trailing-slash question so
// callers do not have to. If 'name' is "..", resolves to the parent directory
// of 'dir' (which may return a drive root, or "" for already-at-root).
// -----------------------------------------------------------------------------
std::string JoinPath(const std::string& dir, const std::string& name);


// -----------------------------------------------------------------------------
// Debug_DumpDirectory
// Convenience: enumerate 'path' with 'filter' and emit LOG_INFO lines for each
// result. Intended for dev/debug sanity checks; remove calls before shipping.
// -----------------------------------------------------------------------------
void Debug_DumpDirectory(const std::string& path, const std::string& filter);


#endif // SYS_DIR_H
