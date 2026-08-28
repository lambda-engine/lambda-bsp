#include "BspFile.h"

#include "FileSystem.h"

#include <cstring>

namespace lbsp
{

void BspWriter::SetLumpBytes(int lump, const uint8_t* data, size_t size, int version)
{
	if (lump < 0 || lump >= HEADER_LUMPS)
	{
		return;
	}
	Lumps[lump].bytes.assign(data, data + size);
	Lumps[lump].version = version;
	Lumps[lump].bSet = true;
}

void BspWriter::SetLumpString(int lump, const std::string& text, int version)
{
	// Entity text is null-terminated; the terminator is part of the lump, not padding.
	std::vector<uint8_t> bytes(text.begin(), text.end());
	bytes.push_back(0);
	SetLumpBytes(lump, bytes.data(), bytes.size(), version);
}

bool BspWriter::Write(const std::string& path, std::string* outError) const
{
	std::vector<uint8_t> file;
	file.resize(sizeof(dheader_t));

	dheader_t header;
	header.ident = BSP_IDENT;
	header.version = BSP_VERSION;
	header.mapRevision = MapRevision;

	for (int i = 0; i < HEADER_LUMPS; ++i)
	{
		const LumpData& lump = Lumps[i];
		header.lumps[i].version = lump.version;
		if (!lump.bSet || lump.bytes.empty())
		{
			// An empty lump still needs a plausible offset: some readers seek to it before checking the length.
			header.lumps[i].fileofs = (int32_t)file.size();
			header.lumps[i].filelen = 0;
			continue;
		}
		header.lumps[i].fileofs = (int32_t)file.size();
		header.lumps[i].filelen = (int32_t)lump.bytes.size();
		file.insert(file.end(), lump.bytes.begin(), lump.bytes.end());
		while ((file.size() & 3) != 0)
		{
			file.push_back(0);
		}
	}

	std::memcpy(file.data(), &header, sizeof(header));
	if (!WriteWholeFile(path, file.data(), file.size()))
	{
		if (outError)
		{
			*outError = "could not write " + path;
		}
		return false;
	}
	return true;
}

}	// namespace lbsp
