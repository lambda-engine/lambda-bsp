#include "Materials.h"

#include "BspFlags.h"
#include "FileSystem.h"
#include "KeyValues.h"

#include <cstring>
#include <vector>

namespace lbsp
{

std::string NormalizeMaterialName(const std::string& name)
{
	std::string out = NormalizePath(name);
	const std::string prefix = "materials/";
	if (out.rfind(prefix, 0) == 0)
	{
		out.erase(0, prefix.size());
	}
	if (out.size() > 4 && out.compare(out.size() - 4, 4, ".vmt") == 0)
	{
		out.resize(out.size() - 4);
	}
	return out;
}

bool ReadVtfHeader(const std::vector<uint8_t>& data, int& outWidth, int& outHeight, Vec3& outReflectivity)
{
	// signature[4], version[2], headerSize, width, height, flags, frames, firstFrame, pad[4], reflectivity[3]
	if (data.size() < 44 || std::memcmp(data.data(), "VTF\0", 4) != 0)
	{
		return false;
	}
	uint16_t w = 0, h = 0;
	std::memcpy(&w, data.data() + 16, 2);
	std::memcpy(&h, data.data() + 18, 2);
	float r[3] = { 0, 0, 0 };
	std::memcpy(r, data.data() + 32, 12);
	outWidth = w;
	outHeight = h;
	outReflectivity = Vec3(r[0], r[1], r[2]);
	return w > 0 && h > 0;
}

namespace
{
	// The %compile vars, in the order vbsp tests them (utils/vbsp/textures.cpp). Order matters: they are an
	// else-if chain, so a material that is both sky and nodraw is sky.
	void ApplyCompileFlags(const KeyValues& kv, int& flags, int& contents)
	{
		if (kv.GetBool("%noPortal"))
		{
			flags |= SURF_NOPORTAL;
		}

		if (kv.GetBool("%compileSky"))
		{
			flags |= SURF_SKY | SURF_NOLIGHT;
		}
		else if (kv.GetBool("%compile2DSky"))
		{
			flags |= SURF_SKY | SURF_SKY2D | SURF_NOLIGHT;
		}
		else if (kv.GetBool("%compileHint"))
		{
			flags |= SURF_NODRAW | SURF_NOLIGHT | SURF_HINT;
		}
		else if (kv.GetBool("%compileSkip"))
		{
			flags |= SURF_NODRAW | SURF_NOLIGHT | SURF_SKIP;
		}
		else if (kv.GetBool("%compileOrigin"))
		{
			contents |= CONTENTS_ORIGIN | CONTENTS_DETAIL;
			flags |= SURF_NODRAW | SURF_NOLIGHT;
		}
		else if (kv.GetBool("%compileClip"))
		{
			contents |= CONTENTS_PLAYERCLIP | CONTENTS_MONSTERCLIP;
			flags |= SURF_NODRAW | SURF_NOLIGHT;
		}
		else if (kv.GetBool("%playerClip"))
		{
			contents |= CONTENTS_PLAYERCLIP;
			flags |= SURF_NODRAW | SURF_NOLIGHT;
		}
		else if (kv.GetBool("%compileNpcClip"))
		{
			contents |= CONTENTS_MONSTERCLIP;
			flags |= SURF_NODRAW | SURF_NOLIGHT;
		}
		else if (kv.GetBool("%compileNoChop"))
		{
			flags |= SURF_NOCHOP;
		}
		else if (kv.GetBool("%compileTrigger"))
		{
			flags |= SURF_NOLIGHT | SURF_TRIGGER | SURF_NODRAW;
		}
		else if (kv.GetBool("%compileNoLight") && !kv.GetBool("%compileWater"))
		{
			flags |= SURF_NOLIGHT;
		}
		else
		{
			if (kv.GetBool("%compileLadder"))
			{
				contents |= CONTENTS_LADDER;
			}
			if (kv.GetBool("%compilePassBullets"))
			{
				contents &= ~CONTENTS_SOLID;
				contents |= CONTENTS_GRATE;
			}
			if (kv.GetBool("%compileNoDraw"))
			{
				flags |= SURF_NODRAW;
			}
		}

		if (kv.GetBool("%compileNonSolid"))
		{
			contents &= ~CONTENTS_SOLID;
		}
		if (kv.GetBool("%compileDetail"))
		{
			contents |= CONTENTS_DETAIL;
		}
		if (kv.GetBool("%compileWater"))
		{
			contents &= ~CONTENTS_SOLID;
			contents |= CONTENTS_WATER;
			flags |= SURF_WARP;
		}
		if (kv.GetBool("$translucent") || kv.GetBool("$alphatest"))
		{
			flags |= SURF_TRANS;
		}
	}
}

bool MaterialCache::ResolveVmt(const std::string& normalizedName, KeyValues*& outOwned, std::string& outShader, int depth)
{
	outOwned = nullptr;
	if (depth > 4)
	{
		return false;	// a patch cycle
	}

	std::string text;
	if (!Files.ReadFileToString("materials/" + normalizedName + ".vmt", text))
	{
		return false;
	}
	KeyValues* doc = KeyValues::ParseDocument(text);
	if (!doc)
	{
		return false;
	}
	// The root of a VMT is one block whose key is the shader name.
	const KeyValues::Entry* rootEntry = nullptr;
	for (const KeyValues::Entry& e : doc->GetEntries())
	{
		if (e.block)
		{
			rootEntry = &e;
			break;
		}
	}
	if (!rootEntry)
	{
		delete doc;
		return false;
	}

	if (EqualsNoCase(rootEntry->key, "patch"))
	{
		// A patch names the material it is based on and overrides some of its keys. The include is resolved
		// first, then the "replace"/"insert" blocks are laid over the top.
		const std::string include = NormalizeMaterialName(rootEntry->block->GetString("include"));
		KeyValues* base = nullptr;
		std::string baseShader;
		const bool ok = !include.empty() && ResolveVmt(include, base, baseShader, depth + 1);
		if (!ok)
		{
			delete doc;
			return false;
		}
		for (const char* section : { "replace", "insert" })
		{
			if (const KeyValues* over = rootEntry->block->GetBlock(section))
			{
				for (const KeyValues::Entry& e : over->GetEntries())
				{
					if (!e.block)
					{
						base->Add(e.key, e.value);	// added first, so it wins the first-match lookup
					}
				}
			}
		}
		delete doc;
		outOwned = base;
		outShader = baseShader;
		return true;
	}

	// The shader block's contents are what everything else reads; detach it from the document.
	KeyValues* result = new KeyValues();
	for (const KeyValues::Entry& e : rootEntry->block->GetEntries())
	{
		if (!e.block)
		{
			result->Add(e.key, e.value);
		}
	}
	outShader = rootEntry->key;
	delete doc;
	outOwned = result;
	return true;
}

MaterialInfo MaterialCache::Load(const std::string& normalizedName)
{
	MaterialInfo info;
	info.contents = CONTENTS_SOLID;

	KeyValues* kv = nullptr;
	if (!ResolveVmt(normalizedName, kv, info.shader, 0))
	{
		return info;	// bFound stays false
	}
	info.bFound = true;

	// A patch overriding a value adds it ahead of the original, so the first hit is the right one.
	info.baseTexture = NormalizePath(kv->GetString("$basetexture"));
	if (info.baseTexture.empty())
	{
		info.baseTexture = NormalizePath(kv->GetString("$basetexture2"));
	}
	info.surfaceProp = kv->GetString("$surfaceprop");
	ApplyCompileFlags(*kv, info.surfaceFlags, info.contents);
	delete kv;

	if (!info.baseTexture.empty())
	{
		std::string texPath = info.baseTexture;
		if (texPath.size() > 4 && texPath.compare(texPath.size() - 4, 4, ".vtf") == 0)
		{
			texPath.resize(texPath.size() - 4);
		}
		std::vector<uint8_t> data;
		if (Files.ReadFile("materials/" + texPath + ".vtf", data))
		{
			info.bTextureFound = ReadVtfHeader(data, info.width, info.height, info.reflectivity);
		}
	}
	return info;
}

const MaterialInfo& MaterialCache::Get(const std::string& materialName)
{
	const std::string key = NormalizeMaterialName(materialName);
	const auto it = Cache.find(key);
	if (it != Cache.end())
	{
		return it->second;
	}
	MaterialInfo info = Load(key);
	if (!info.bFound || !info.bTextureFound)
	{
		++MissingCount;
	}
	return Cache.emplace(key, std::move(info)).first->second;
}

}	// namespace lbsp
