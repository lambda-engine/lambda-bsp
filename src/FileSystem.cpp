#include "FileSystem.h"

#include "KeyValues.h"
#include "Vpk.h"

#include <algorithm>
#include <cstdio>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace lbsp
{

std::string NormalizePath(const std::string& path)
{
	std::string out;
	out.reserve(path.size());
	for (char c : path)
	{
		out.push_back(c == '\\' ? '/' : (char)std::tolower((unsigned char)c));
	}
	while (out.size() > 2 && out[0] == '.' && out[1] == '/')
	{
		out.erase(0, 2);
	}
	return out;
}

std::string JoinPath(const std::string& a, const std::string& b)
{
	if (a.empty()) return b;
	if (b.empty()) return a;
	const char last = a[a.size() - 1];
	if (last == '/' || last == '\\')
	{
		return a + b;
	}
	return a + "/" + b;
}

std::string GetDirectoryName(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? std::string() : path.substr(0, slash);
}

std::string GetFileName(const std::string& path)
{
	const size_t slash = path.find_last_of("/\\");
	return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string GetFileNameWithoutExtension(const std::string& path)
{
	std::string name = GetFileName(path);
	const size_t dot = name.find_last_of('.');
	return dot == std::string::npos ? name : name.substr(0, dot);
}

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& outData)
{
	std::FILE* f = std::fopen(path.c_str(), "rb");
	if (!f)
	{
		return false;
	}
	std::fseek(f, 0, SEEK_END);
	const long size = std::ftell(f);
	std::fseek(f, 0, SEEK_SET);
	if (size < 0)
	{
		std::fclose(f);
		return false;
	}
	outData.resize((size_t)size);
	const size_t got = size > 0 ? std::fread(outData.data(), 1, (size_t)size, f) : 0;
	std::fclose(f);
	outData.resize(got);
	return true;
}

bool WriteWholeFile(const std::string& path, const void* data, size_t size)
{
	std::FILE* f = std::fopen(path.c_str(), "wb");
	if (!f)
	{
		return false;
	}
	const size_t wrote = size > 0 ? std::fwrite(data, 1, size, f) : 0;
	std::fclose(f);
	return wrote == size;
}

bool DirectoryExists(const std::string& path)
{
	const DWORD attr = GetFileAttributesA(path.c_str());
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool FileExistsOnDisk(const std::string& path)
{
	const DWORD attr = GetFileAttributesA(path.c_str());
	return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY) == 0;
}

std::vector<std::string> ListDirectory(const std::string& path, bool bWantDirectories)
{
	std::vector<std::string> out;
	WIN32_FIND_DATAA fd;
	const HANDLE h = FindFirstFileA(JoinPath(path, "*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE)
	{
		return out;
	}
	do
	{
		const std::string name = fd.cFileName;
		if (name == "." || name == "..")
		{
			continue;
		}
		const bool bIsDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
		if (bIsDir == bWantDirectories)
		{
			out.push_back(name);
		}
	} while (FindNextFileA(h, &fd));
	FindClose(h);
	std::sort(out.begin(), out.end());
	return out;
}

std::vector<std::string> FindSteamLibraries()
{
	// Steam's own folder first, then every library the user added on another drive. libraryfolders.vdf is
	// KeyValues like everything else Valve writes.
	std::vector<std::string> roots;
	const char* candidates[] = {
		"C:/Program Files (x86)/Steam",
		"C:/Program Files/Steam",
	};
	for (const char* c : candidates)
	{
		if (DirectoryExists(JoinPath(c, "steamapps")))
		{
			roots.push_back(c);
		}
	}

	std::vector<std::string> libraries;
	for (const std::string& root : roots)
	{
		libraries.push_back(JoinPath(root, "steamapps/common"));

		std::vector<uint8_t> data;
		if (!ReadWholeFile(JoinPath(root, "steamapps/libraryfolders.vdf"), data))
		{
			continue;
		}
		const std::string text((const char*)data.data(), data.size());
		KeyValues* kv = KeyValues::ParseDocument(text);
		if (!kv)
		{
			continue;
		}
		if (const KeyValues* folders = kv->GetBlock("libraryfolders"))
		{
			for (const KeyValues::Entry& e : folders->GetEntries())
			{
				// Newer Steam nests each library in a block with a "path"; older versions used "1" "D:\\Games".
				const std::string path = e.block ? e.block->GetString("path") : e.value;
				if (!path.empty())
				{
					const std::string common = JoinPath(path, "steamapps/common");
					if (DirectoryExists(common))
					{
						libraries.push_back(common);
					}
				}
			}
		}
		delete kv;
	}

	// A library can be listed twice - Steam's own folder appears in its libraryfolders.vdf as well.
	std::vector<std::string> unique;
	for (const std::string& lib : libraries)
	{
		const std::string key = NormalizePath(lib);
		bool bSeen = false;
		for (const std::string& u : unique)
		{
			bSeen = bSeen || NormalizePath(u) == key;
		}
		if (!bSeen && DirectoryExists(lib))
		{
			unique.push_back(lib);
		}
	}
	return unique;
}

FileSystem::FileSystem() = default;
FileSystem::~FileSystem() = default;

void FileSystem::MountDirectory(const std::string& dir)
{
	if (!DirectoryExists(dir))
	{
		return;
	}
	Directories.push_back(dir);
	Mounts.push_back({ false, Directories.size() - 1 });
	MountDescriptions.push_back("[dir] " + dir);
}

bool FileSystem::MountVpk(const std::string& vpkPath)
{
	if (!FileExistsOnDisk(vpkPath))
	{
		return false;
	}
	auto vpk = std::make_unique<Vpk>();
	std::string error;
	if (!vpk->Open(vpkPath, &error))
	{
		return false;
	}
	MountDescriptions.push_back("[vpk] " + vpkPath + " (" + std::to_string(vpk->GetFileCount()) + " files)");
	Vpks.push_back(std::move(vpk));
	Mounts.push_back({ true, Vpks.size() - 1 });
	return true;
}

void FileSystem::MountPluginsFolder(const std::string& pluginsDir, std::vector<std::string>& outLog)
{
	// Every folder and VPK inside plugins/, alphabetically - a content pack is installed by dropping it in.
	if (!DirectoryExists(pluginsDir))
	{
		return;
	}
	for (const std::string& name : ListDirectory(pluginsDir, true))
	{
		MountDirectory(JoinPath(pluginsDir, name));
	}
	for (const std::string& name : ListDirectory(pluginsDir, false))
	{
		if (NormalizePath(name).size() > 4 && NormalizePath(name).substr(NormalizePath(name).size() - 4) == ".vpk")
		{
			if (!MountVpk(JoinPath(pluginsDir, name)))
			{
				outLog.push_back("warning: could not mount pack " + name);
			}
		}
	}
}

void FileSystem::MountSteamPath(const std::string& tail, std::vector<std::string>& outLog)
{
	// |steamlibrary_path|Half-Life 2 Deathmatch/hl2mp/hl2mp_pak_dir.vpk - the first library that has it wins,
	// and one nothing answers to is skipped with a warning rather than stopping the compile.
	static const std::vector<std::string> libraries = FindSteamLibraries();
	for (const std::string& lib : libraries)
	{
		const std::string full = JoinPath(lib, tail);
		if (FileExistsOnDisk(full))
		{
			if (MountVpk(full))
			{
				return;
			}
		}
		if (DirectoryExists(full))
		{
			MountDirectory(full);
			return;
		}
	}
	outLog.push_back("warning: search path not found in any Steam library: " + tail);
}

bool FileSystem::MountGameInfo(const std::string& modDir, std::vector<std::string>& outLog)
{
	std::vector<uint8_t> data;
	if (!ReadWholeFile(JoinPath(modDir, "gameinfo.txt"), data))
	{
		outLog.push_back("no gameinfo.txt in " + modDir);
		return false;
	}

	const std::string text((const char*)data.data(), data.size());
	std::string error;
	KeyValues* kv = KeyValues::ParseDocument(text, &error);
	if (!kv)
	{
		outLog.push_back("gameinfo.txt: " + error);
		return false;
	}

	const KeyValues* gameInfo = kv->GetBlock("GameInfo");
	const KeyValues* fileSystem = gameInfo ? gameInfo->GetBlock("FileSystem") : nullptr;
	const KeyValues* searchPaths = fileSystem ? fileSystem->GetBlock("SearchPaths") : nullptr;
	if (!searchPaths)
	{
		outLog.push_back("gameinfo.txt has no FileSystem/SearchPaths block");
		delete kv;
		return false;
	}

	for (const KeyValues::Entry& e : searchPaths->GetEntries())
	{
		if (e.block || e.value.empty())
		{
			continue;
		}
		std::string path = e.value;
		const std::string gameInfoToken = "|gameinfo_path|";
		const std::string steamToken = "|steamlibrary_path|";
		const std::string allSourceToken = "|all_source_engine_paths|";

		if (path.rfind(gameInfoToken, 0) == 0)
		{
			std::string tail = path.substr(gameInfoToken.size());
			if (tail == "." || tail.empty())
			{
				MountDirectory(modDir);
			}
			else if (tail == "plugins/*" || tail == "plugins\\*")
			{
				MountPluginsFolder(JoinPath(modDir, "plugins"), outLog);
			}
			else
			{
				MountDirectory(JoinPath(modDir, tail));
			}
			continue;
		}
		if (path.rfind(steamToken, 0) == 0)
		{
			MountSteamPath(path.substr(steamToken.size()), outLog);
			continue;
		}
		if (path.rfind(allSourceToken, 0) == 0)
		{
			path = path.substr(allSourceToken.size());
		}
		// A bare relative path is resolved against the mod folder, an absolute one taken as it stands.
		const bool bAbsolute = path.size() > 1 && (path[1] == ':' || path[0] == '/' || path[0] == '\\');
		const std::string full = bAbsolute ? path : JoinPath(modDir, path);
		if (NormalizePath(full).size() > 4
			&& NormalizePath(full).substr(NormalizePath(full).size() - 4) == ".vpk")
		{
			if (!MountVpk(full))
			{
				outLog.push_back("warning: could not mount " + full);
			}
		}
		else
		{
			MountDirectory(full);
		}
	}

	delete kv;
	return true;
}

bool FileSystem::ReadFile(const std::string& relativePath, std::vector<uint8_t>& outData) const
{
	const std::string key = NormalizePath(relativePath);
	for (const Mount& m : Mounts)
	{
		if (m.bIsVpk)
		{
			if (Vpks[m.index]->Read(key, outData))
			{
				return true;
			}
		}
		else
		{
			// Windows matches case-insensitively already, which is what a Source path expects.
			if (ReadWholeFile(JoinPath(Directories[m.index], key), outData))
			{
				return true;
			}
		}
	}
	return false;
}

bool FileSystem::ReadFileToString(const std::string& relativePath, std::string& outText) const
{
	std::vector<uint8_t> data;
	if (!ReadFile(relativePath, data))
	{
		return false;
	}
	outText.assign((const char*)data.data(), data.size());
	return true;
}

bool FileSystem::FileExists(const std::string& relativePath) const
{
	const std::string key = NormalizePath(relativePath);
	for (const Mount& m : Mounts)
	{
		if (m.bIsVpk)
		{
			if (Vpks[m.index]->Contains(key))
			{
				return true;
			}
		}
		else if (FileExistsOnDisk(JoinPath(Directories[m.index], key)))
		{
			return true;
		}
	}
	return false;
}

}	// namespace lbsp
