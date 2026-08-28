#include "Vpk.h"

#include "FileSystem.h"

#include <cstdio>
#include <cstring>

namespace lbsp
{

namespace
{
	constexpr uint32_t VPK_SIGNATURE = 0x55AA1234;

	struct Reader
	{
		const std::vector<uint8_t>& data;
		size_t pos = 0;
		bool bBad = false;

		explicit Reader(const std::vector<uint8_t>& d) : data(d) {}

		template <typename T> T Read()
		{
			T v{};
			if (pos + sizeof(T) > data.size())
			{
				bBad = true;
				return v;
			}
			std::memcpy(&v, data.data() + pos, sizeof(T));
			pos += sizeof(T);
			return v;
		}

		std::string ReadCString()
		{
			std::string s;
			while (pos < data.size() && data[pos] != 0)
			{
				s.push_back((char)data[pos++]);
			}
			if (pos < data.size())
			{
				++pos;
			}
			else
			{
				bBad = true;
			}
			return s;
		}
	};
}

bool Vpk::Open(const std::string& dirVpkPath, std::string* outError)
{
	std::vector<uint8_t> data;
	if (!ReadWholeFile(dirVpkPath, data))
	{
		if (outError) *outError = "cannot read " + dirVpkPath;
		return false;
	}

	Reader r(data);
	if (r.Read<uint32_t>() != VPK_SIGNATURE)
	{
		if (outError) *outError = "not a VPK: " + dirVpkPath;
		return false;
	}
	const uint32_t version = r.Read<uint32_t>();
	const uint32_t treeSize = r.Read<uint32_t>();
	if (version == 2)
	{
		// v2 adds checksum and signature sections after the data; none of it matters for reading files.
		r.pos += 16;
	}
	else if (version != 1)
	{
		if (outError) *outError = "unsupported VPK version " + std::to_string(version);
		return false;
	}

	const size_t treeStart = r.pos;
	DataOffset = (uint32_t)(treeStart + treeSize);

	// extension \0 { path \0 { filename \0 { entry } } }, each level terminated by an empty string.
	for (;;)
	{
		const std::string ext = r.ReadCString();
		if (r.bBad) break;
		if (ext.empty()) break;
		for (;;)
		{
			const std::string dir = r.ReadCString();
			if (r.bBad || dir.empty()) break;
			for (;;)
			{
				const std::string name = r.ReadCString();
				if (r.bBad || name.empty()) break;

				Entry e;
				r.Read<uint32_t>();							// crc
				const uint16_t preloadBytes = r.Read<uint16_t>();
				e.archiveIndex = r.Read<uint16_t>();
				e.entryOffset = r.Read<uint32_t>();
				e.entryLength = r.Read<uint32_t>();
				const uint16_t terminator = r.Read<uint16_t>();
				if (r.bBad || terminator != 0xFFFF)
				{
					if (outError) *outError = "malformed VPK directory in " + dirVpkPath;
					return false;
				}
				if (preloadBytes > 0)
				{
					if (r.pos + preloadBytes > data.size())
					{
						if (outError) *outError = "truncated VPK preload in " + dirVpkPath;
						return false;
					}
					e.preload.assign(data.begin() + r.pos, data.begin() + r.pos + preloadBytes);
					r.pos += preloadBytes;
				}

				// " " is how a VPK spells "no directory" and "no extension".
				std::string full;
				if (dir != " ")
				{
					full = dir + "/";
				}
				full += name;
				if (ext != " ")
				{
					full += "." + ext;
				}
				Entries.emplace(NormalizePath(full), std::move(e));
			}
		}
	}

	Path = dirVpkPath;
	ChunkPrefix = dirVpkPath;
	// "hl2_misc_dir.vpk" -> "hl2_misc", so the numbered chunks beside it can be named.
	const std::string suffix = "_dir.vpk";
	if (ChunkPrefix.size() > suffix.size()
		&& NormalizePath(ChunkPrefix).compare(ChunkPrefix.size() - suffix.size(), suffix.size(), suffix) == 0)
	{
		ChunkPrefix.resize(ChunkPrefix.size() - suffix.size());
	}
	return true;
}

bool Vpk::Contains(const std::string& relativePath) const
{
	return Entries.find(NormalizePath(relativePath)) != Entries.end();
}

bool Vpk::Read(const std::string& relativePath, std::vector<uint8_t>& outData) const
{
	const auto it = Entries.find(NormalizePath(relativePath));
	if (it == Entries.end())
	{
		return false;
	}
	const Entry& e = it->second;

	outData = e.preload;
	if (e.entryLength == 0)
	{
		return true;	// wholly preloaded
	}

	// 0x7FFF means the bytes are in the _dir.vpk itself, after the tree; anything else names a chunk file.
	std::string archivePath;
	uint32_t offset = e.entryOffset;
	if (e.archiveIndex == 0x7FFF)
	{
		archivePath = Path;
		offset += DataOffset;
	}
	else
	{
		char suffix[32];
		std::snprintf(suffix, sizeof(suffix), "_%03u.vpk", (unsigned)e.archiveIndex);
		archivePath = ChunkPrefix + suffix;
	}

	std::FILE* f = std::fopen(archivePath.c_str(), "rb");
	if (!f)
	{
		return false;
	}
	if (std::fseek(f, (long)offset, SEEK_SET) != 0)
	{
		std::fclose(f);
		return false;
	}
	const size_t base = outData.size();
	outData.resize(base + e.entryLength);
	const size_t got = std::fread(outData.data() + base, 1, e.entryLength, f);
	std::fclose(f);
	if (got != e.entryLength)
	{
		outData.resize(base + got);
		return false;
	}
	return true;
}

}	// namespace lbsp
