// The search path, loose directories and VPKs together.
//
// This exists because of a bug it is meant to prevent. vbsp records each material's pixel dimensions into the
// BSP, and when it cannot find the material it silently writes zero - and a renderer that divides texture
// coordinates by zero draws a wall tiled several hundred times over. The map looks perfect in Hammer, which
// reads the VTF itself and never consults the compiler's record.
//
// The cure is for the compiler to see exactly the content the game will see, so LambdaBSP reads the same
// gameinfo.txt the engine does and mounts the same directories and VPKs in the same order. Anything it cannot
// find, it says so loudly rather than writing a zero.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lbsp
{

class Vpk;

class FileSystem
{
public:
	FileSystem();
	~FileSystem();

	/**
	 * Mounts everything gameinfo.txt in modDir asks for: its own folder, every pack under plugins/, and the
	 * Steam-installed games it names. Returns false only if gameinfo.txt is missing or unreadable - a single
	 * search path that resolves to nothing is a warning, exactly as it is in the engine.
	 */
	bool MountGameInfo(const std::string& modDir, std::vector<std::string>& outLog);

	/** Mounts one loose directory at the bottom of the search order. */
	void MountDirectory(const std::string& dir);
	/** Mounts one VPK (point at the _dir.vpk of a multi-chunk set). */
	bool MountVpk(const std::string& vpkPath);

	/** Reads a file by its game-relative path ("materials/dev/x.vmt"). Case-insensitive, "/" separated. */
	bool ReadFile(const std::string& relativePath, std::vector<uint8_t>& outData) const;
	bool ReadFileToString(const std::string& relativePath, std::string& outText) const;
	bool FileExists(const std::string& relativePath) const;

	const std::vector<std::string>& GetMountDescriptions() const { return MountDescriptions; }

private:
	std::vector<std::string> Directories;
	std::vector<std::unique_ptr<Vpk>> Vpks;
	// Parallel to the mount order, so the two lists can be walked together when resolving.
	struct Mount { bool bIsVpk; size_t index; };
	std::vector<Mount> Mounts;
	std::vector<std::string> MountDescriptions;

	void MountPluginsFolder(const std::string& pluginsDir, std::vector<std::string>& outLog);
	void MountSteamPath(const std::string& tail, std::vector<std::string>& outLog);
};

/** Every Steam library folder on this machine, read out of libraryfolders.vdf. */
std::vector<std::string> FindSteamLibraries();

/** Lower-case, forward slashes, no leading "./" - how every path is keyed inside this program. */
std::string NormalizePath(const std::string& path);

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>& outData);
bool WriteWholeFile(const std::string& path, const void* data, size_t size);
bool DirectoryExists(const std::string& path);
bool FileExistsOnDisk(const std::string& path);
std::vector<std::string> ListDirectory(const std::string& path, bool bWantDirectories);
std::string GetDirectoryName(const std::string& path);
std::string GetFileName(const std::string& path);
std::string GetFileNameWithoutExtension(const std::string& path);
std::string JoinPath(const std::string& a, const std::string& b);

}	// namespace lbsp
