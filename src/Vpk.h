// Source 1 VPK archives, read-only.
//
// Half-Life's content lives in these, so a compiler that cannot open one cannot see the textures a map is built
// from. Only the directory is parsed up front; file data is pulled from the numbered chunk files on demand.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace lbsp
{

class Vpk
{
public:
	/** Opens a _dir.vpk. Returns false if it is not a VPK or the directory is malformed. */
	bool Open(const std::string& dirVpkPath, std::string* outError = nullptr);

	bool Contains(const std::string& relativePath) const;
	bool Read(const std::string& relativePath, std::vector<uint8_t>& outData) const;

	size_t GetFileCount() const { return Entries.size(); }
	const std::string& GetPath() const { return Path; }

private:
	struct Entry
	{
		uint16_t archiveIndex = 0;
		uint32_t entryOffset = 0;
		uint32_t entryLength = 0;
		std::vector<uint8_t> preload;	// small files live entirely in the directory
	};

	std::string Path;			// the _dir.vpk itself
	std::string ChunkPrefix;	// path minus "_dir.vpk", for building "_000.vpk" and friends
	uint32_t DataOffset = 0;	// where entryOffset 0 of archive 0x7FFF (the dir file itself) begins
	std::unordered_map<std::string, Entry> Entries;
};

}	// namespace lbsp
