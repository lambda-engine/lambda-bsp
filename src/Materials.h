// What the compiler needs to know about a material: how big it is, how much light it bounces, and what the
// %compile vars say it is for.
//
// The size is the whole reason this program reads content at all. A face's texture axes are stored in texels
// per world unit, so the renderer divides them by the texture's dimensions - and it gets those dimensions from
// what the compiler wrote here. Guessing is not an option, and neither is zero.
#pragma once

#include "Math.h"

#include <string>
#include <unordered_map>

namespace lbsp
{

class FileSystem;

struct MaterialInfo
{
	bool bFound = false;			// false means no VMT: the caller must warn, not carry on quietly
	bool bTextureFound = false;		// VMT parsed but its $basetexture is missing
	int width = 0;
	int height = 0;
	Vec3 reflectivity;
	int surfaceFlags = 0;			// SURF_*
	int contents = 0;				// CONTENTS_*
	std::string shader;
	std::string baseTexture;
	std::string surfaceProp;
};

class MaterialCache
{
public:
	explicit MaterialCache(const FileSystem& fs) : Files(fs) {}

	/** Looks up "DEV/DEV_MEASUREWALL01A" (any case, with or without materials/ and .vmt). Cached. */
	const MaterialInfo& Get(const std::string& materialName);

	int GetMissingCount() const { return MissingCount; }

private:
	const FileSystem& Files;
	std::unordered_map<std::string, MaterialInfo> Cache;
	int MissingCount = 0;

	MaterialInfo Load(const std::string& normalizedName);
	/** Follows a "patch" VMT to the material it includes. */
	bool ResolveVmt(const std::string& normalizedName, class KeyValues*& outOwned, std::string& outShader, int depth);
};

/** Lower-case, forward slashes, no "materials/" prefix and no ".vmt" - how a texdata name is keyed. */
std::string NormalizeMaterialName(const std::string& name);

/** Reads a VTF header for its dimensions and reflectivity. Both live in the first 44 bytes of every version. */
bool ReadVtfHeader(const std::vector<uint8_t>& data, int& outWidth, int& outHeight, Vec3& outReflectivity);

}	// namespace lbsp
