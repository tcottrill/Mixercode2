#include "sys_dir.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

#include "sys_log.h"

#ifdef _WIN32
  #include <Windows.h>
#endif

namespace fs = std::filesystem;


// -----------------------------------------------------------------------------
// Internal helpers
// -----------------------------------------------------------------------------
namespace
{
	// Lowercase an ASCII string in place. Used for case-insensitive filter
	// matching. Non-ASCII bytes are left alone; filesystem filters are almost
	// always ASCII extensions like ".png" or ".txt".
	std::string ToLowerAscii(const std::string& s)
	{
		std::string out = s;
		for (char& c : out)
		{
			if ((unsigned char)c < 128)
				c = (char)std::tolower((unsigned char)c);
		}
		return out;
	}

	// Split a Windows-style filter "*.png;*.jpg" into individual patterns
	// like ["*.png", "*.jpg"]. Empty segments are dropped. Leading/trailing
	// whitespace on each segment is trimmed. All segments are lowercased.
	//
	// A single "*" or an empty filter returns an empty vector, which the
	// caller treats as "accept everything".
	std::vector<std::string> ParseFilter(const std::string& filter)
	{
		std::vector<std::string> patterns;
		if (filter.empty() || filter == "*")
			return patterns;

		std::string current;
		for (char c : filter)
		{
			if (c == ';')
			{
				if (!current.empty())
					patterns.push_back(ToLowerAscii(current));
				current.clear();
			}
			else
			{
				current.push_back(c);
			}
		}
		if (!current.empty())
			patterns.push_back(ToLowerAscii(current));

		// Trim whitespace on each pattern.
		for (std::string& p : patterns)
		{
			while (!p.empty() && (p.front() == ' ' || p.front() == '\t'))
				p.erase(p.begin());
			while (!p.empty() && (p.back() == ' ' || p.back() == '\t'))
				p.pop_back();
		}

		// Drop any empties produced by trimming.
		patterns.erase(
			std::remove_if(patterns.begin(), patterns.end(),
				[](const std::string& s) { return s.empty(); }),
			patterns.end());

		// If the only pattern is "*", that also means accept-all.
		if (patterns.size() == 1 && patterns[0] == "*")
			patterns.clear();

		return patterns;
	}

	// True if 'filename' (lowercased) matches any of the '*.ext' patterns.
	// An empty pattern list accepts everything.
	bool FileMatchesFilter(const std::string& filenameLower,
	                       const std::vector<std::string>& patternsLower)
	{
		if (patternsLower.empty())
			return true;

		for (const std::string& pat : patternsLower)
		{
			// We only support "*.ext" style. Extract the suffix after the
			// leading '*' (if any) and compare as a literal suffix.
			if (pat.size() >= 2 && pat[0] == '*')
			{
				const std::string suffix = pat.substr(1); // e.g. ".png"
				if (filenameLower.size() >= suffix.size())
				{
					if (filenameLower.compare(
					        filenameLower.size() - suffix.size(),
					        suffix.size(),
					        suffix) == 0)
					{
						return true;
					}
				}
			}
			else
			{
				// Not a "*.ext" pattern; treat as exact-name match (rare).
				if (filenameLower == pat)
					return true;
			}
		}
		return false;
	}

	// Case-insensitive less-than for ASCII sort. Filesystem names with
	// non-ASCII bytes will sort by their raw byte order after the ASCII
	// portion, which is fine for display purposes.
	bool NameLessCaseInsensitive(const std::string& a, const std::string& b)
	{
		const size_t n = (std::min)(a.size(), b.size());
		for (size_t i = 0; i < n; ++i)
		{
			unsigned char ca = (unsigned char)a[i];
			unsigned char cb = (unsigned char)b[i];
			if (ca < 128) ca = (unsigned char)std::tolower(ca);
			if (cb < 128) cb = (unsigned char)std::tolower(cb);
			if (ca != cb) return ca < cb;
		}
		return a.size() < b.size();
	}

	// True if 'path' represents a drive root on this platform. On Windows
	// this is a path whose parent equals itself (e.g. "C:\\"). On non-Windows
	// this is "/" .
	bool IsDriveRoot(const fs::path& path)
	{
		std::error_code ec;
		fs::path parent = path.parent_path();
		// parent_path() of "C:\\" returns "C:\\" on Windows.
		if (parent == path)
			return true;
		// Also catch the empty case to be safe.
		if (parent.empty() && !path.empty())
			return true;
		(void)ec;
		return false;
	}
}


// -----------------------------------------------------------------------------
// EnumerateDrives
// -----------------------------------------------------------------------------
bool EnumerateDrives(std::vector<DirEntry>& out)
{
	out.clear();

#ifdef _WIN32
	// GetLogicalDriveStringsW fills a double-null-terminated buffer of
	// wide-char drive strings like L"C:\\\0D:\\\0\0".
	DWORD needed = GetLogicalDriveStringsW(0, nullptr);
	if (needed == 0)
	{
		LOG_ERROR("EnumerateDrives: GetLogicalDriveStringsW size probe failed.");
		return false;
	}

	std::vector<wchar_t> buf((size_t)needed + 1, L'\0');
	DWORD written = GetLogicalDriveStringsW(needed, buf.data());
	if (written == 0)
	{
		LOG_ERROR("EnumerateDrives: GetLogicalDriveStringsW fetch failed.");
		return false;
	}

	const wchar_t* p = buf.data();
	while (*p)
	{
		fs::path drive(p);
		DirEntry e;
		e.name = drive.u8string();   // e.g. "C:\\"
		e.isDirectory = true;
		out.push_back(e);
		p += wcslen(p) + 1;
	}

	// Drives returned by the OS are already in A-Z order; no sort needed.
	return true;

#else
	DirEntry e;
	e.name = "/";
	e.isDirectory = true;
	out.push_back(e);
	return true;
#endif
}


// -----------------------------------------------------------------------------
// EnumerateDirectory
// -----------------------------------------------------------------------------
bool EnumerateDirectory(const std::string& path,
                        const std::string& filter,
                        std::vector<DirEntry>& out)
{
	out.clear();

	if (path.empty())
	{
		LOG_ERROR("EnumerateDirectory: empty path.");
		return false;
	}

	std::error_code ec;
	fs::path dir = fs::u8path(path);

	if (!fs::exists(dir, ec) || ec)
	{
		LOG_ERROR("EnumerateDirectory: path does not exist: %s", path.c_str());
		return false;
	}
	if (!fs::is_directory(dir, ec) || ec)
	{
		LOG_ERROR("EnumerateDirectory: path is not a directory: %s", path.c_str());
		return false;
	}

	const std::vector<std::string> patternsLower = ParseFilter(filter);

	std::vector<DirEntry> dirs;
	std::vector<DirEntry> files;

	fs::directory_iterator it(dir, ec);
	if (ec)
	{
		LOG_ERROR("EnumerateDirectory: could not open directory: %s (%s)",
			path.c_str(), ec.message().c_str());
		return false;
	}

	// Use the error_code iteration form to avoid exceptions mid-loop.
	for (; it != fs::directory_iterator(); it.increment(ec))
	{
		if (ec)
		{
			LOG_ERROR("EnumerateDirectory: iteration error in %s (%s)",
				path.c_str(), ec.message().c_str());
			break;
		}

		std::error_code statEc;
		const fs::directory_entry& entry = *it;
		const bool isDir = entry.is_directory(statEc);
		if (statEc)
		{
			// Could not stat this entry (permission denied, broken junction,
			// etc.). Skip it and keep going.
			continue;
		}

		DirEntry e;
		e.name = entry.path().filename().u8string();
		e.isDirectory = isDir;

		if (e.name.empty())
			continue;

		if (isDir)
		{
			dirs.push_back(std::move(e));
		}
		else
		{
			const std::string nameLower = ToLowerAscii(e.name);
			if (FileMatchesFilter(nameLower, patternsLower))
				files.push_back(std::move(e));
		}
	}

	auto cmp = [](const DirEntry& a, const DirEntry& b) {
		return NameLessCaseInsensitive(a.name, b.name);
	};
	std::sort(dirs.begin(), dirs.end(), cmp);
	std::sort(files.begin(), files.end(), cmp);

	// Prepend ".." if we are not at a drive root.
	if (!IsDriveRoot(dir))
	{
		DirEntry up;
		up.name = "..";
		up.isDirectory = true;
		out.push_back(up);
	}

	out.insert(out.end(), dirs.begin(), dirs.end());
	out.insert(out.end(), files.begin(), files.end());

	return true;
}


// -----------------------------------------------------------------------------
// GetParentDirectory
// -----------------------------------------------------------------------------
std::string GetParentDirectory(const std::string& path)
{
	if (path.empty())
		return "";

	fs::path p = fs::u8path(path);
	fs::path parent = p.parent_path();

	// A drive root's parent is itself (e.g. "C:\\".parent_path() == "C:\\").
	if (parent == p || parent.empty())
		return "";

	return parent.u8string();
}


// -----------------------------------------------------------------------------
// JoinPath
// -----------------------------------------------------------------------------
std::string JoinPath(const std::string& dir, const std::string& name)
{
	if (name == "..")
		return GetParentDirectory(dir);

	if (dir.empty())
		return name;

	fs::path joined = fs::u8path(dir) / fs::u8path(name);
	return joined.u8string();
}


// -----------------------------------------------------------------------------
// Debug_DumpDirectory
// -----------------------------------------------------------------------------
void Debug_DumpDirectory(const std::string& path, const std::string& filter)
{
	LOG_INFO("Debug_DumpDirectory: path='%s' filter='%s'",
		path.c_str(), filter.c_str());

	std::vector<DirEntry> entries;

	bool ok;
	if (path.empty())
	{
		ok = EnumerateDrives(entries);
		if (!ok)
		{
			LOG_INFO("  (drive enumeration failed)");
			return;
		}
	}
	else
	{
		ok = EnumerateDirectory(path, filter, entries);
		if (!ok)
		{
			LOG_INFO("  (enumeration failed)");
			return;
		}
	}

	if (entries.empty())
	{
		LOG_INFO("  (empty)");
		return;
	}

	for (const DirEntry& e : entries)
	{
		if (e.isDirectory)
			LOG_INFO("  [DIR] %s", e.name.c_str());
		else
			LOG_INFO("        %s", e.name.c_str());
	}
}
