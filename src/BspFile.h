// The VBSP file, as it sits on disk, and the writer that produces one.
//
// Struct layouts are Valve's (public/bspfile.h) and every size is asserted, because a field out of place here
// does not fail loudly - it produces a file that loads and draws the wrong thing.
#pragma once

#include "Math.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lbsp
{

constexpr int32_t BSP_IDENT = ('P' << 24) | ('S' << 16) | ('B' << 8) | 'V';	// "VBSP"
constexpr int32_t BSP_VERSION = 20;
constexpr int32_t HEADER_LUMPS = 64;
constexpr int MAXLIGHTMAPS = 4;

enum ELump
{
	LUMP_ENTITIES = 0,
	LUMP_PLANES = 1,
	LUMP_TEXDATA = 2,
	LUMP_VERTEXES = 3,
	LUMP_VISIBILITY = 4,
	LUMP_NODES = 5,
	LUMP_TEXINFO = 6,
	LUMP_FACES = 7,
	LUMP_LIGHTING = 8,
	LUMP_LEAFS = 10,
	LUMP_EDGES = 12,
	LUMP_SURFEDGES = 13,
	LUMP_MODELS = 14,
	LUMP_LEAFFACES = 16,
	LUMP_LEAFBRUSHES = 17,
	LUMP_BRUSHES = 18,
	LUMP_BRUSHSIDES = 19,
	LUMP_ORIGINALFACES = 27,
	LUMP_GAME_LUMP = 35,
	LUMP_PAKFILE = 40,
	LUMP_TEXDATA_STRING_DATA = 43,
	LUMP_TEXDATA_STRING_TABLE = 44,
};

#pragma pack(push, 1)

struct dvec3_t
{
	float x = 0, y = 0, z = 0;
	dvec3_t() = default;
	dvec3_t(const Vec3& v) : x((float)v.x), y((float)v.y), z((float)v.z) {}
};
static_assert(sizeof(dvec3_t) == 12, "dvec3_t size");

struct lump_t
{
	int32_t fileofs = 0;
	int32_t filelen = 0;
	int32_t version = 0;
	char fourCC[4] = { 0, 0, 0, 0 };
};
static_assert(sizeof(lump_t) == 16, "lump_t size");

struct dheader_t
{
	int32_t ident = BSP_IDENT;
	int32_t version = BSP_VERSION;
	lump_t lumps[HEADER_LUMPS];
	int32_t mapRevision = 0;
};
static_assert(sizeof(dheader_t) == 1036, "dheader_t size");

struct dplane_t
{
	dvec3_t normal;
	float dist = 0;
	int32_t type = 0;
};
static_assert(sizeof(dplane_t) == 20, "dplane_t size");

struct dvertex_t
{
	dvec3_t point;
};
static_assert(sizeof(dvertex_t) == 12, "dvertex_t size");

struct dedge_t
{
	uint16_t v[2] = { 0, 0 };
};
static_assert(sizeof(dedge_t) == 4, "dedge_t size");

struct texinfo_t
{
	float textureVecsTexelsPerWorldUnits[2][4] = {};
	float lightmapVecsLuxelsPerWorldUnits[2][4] = {};
	int32_t flags = 0;
	int32_t texdata = 0;
};
static_assert(sizeof(texinfo_t) == 72, "texinfo_t size");

struct dtexdata_t
{
	dvec3_t reflectivity;
	int32_t nameStringTableID = 0;
	int32_t width = 0, height = 0;
	int32_t view_width = 0, view_height = 0;
};
static_assert(sizeof(dtexdata_t) == 32, "dtexdata_t size");

struct dface_t
{
	uint16_t planenum = 0;
	uint8_t side = 0;
	uint8_t onNode = 0;
	int32_t firstedge = 0;
	int16_t numedges = 0;
	int16_t texinfo = 0;
	int16_t dispinfo = -1;
	int16_t surfaceFogVolumeID = -1;
	uint8_t styles[MAXLIGHTMAPS] = { 255, 255, 255, 255 };
	int32_t lightofs = -1;
	float area = 0;
	int32_t m_LightmapTextureMinsInLuxels[2] = { 0, 0 };
	int32_t m_LightmapTextureSizeInLuxels[2] = { 0, 0 };
	int32_t origFace = -1;
	uint16_t m_NumPrims = 0;
	uint16_t firstPrimID = 0;
	uint32_t smoothingGroups = 0;
};
static_assert(sizeof(dface_t) == 56, "dface_t size");

struct dmodel_t
{
	dvec3_t mins, maxs;
	dvec3_t origin;
	int32_t headnode = -1;
	int32_t firstface = 0, numfaces = 0;
};
static_assert(sizeof(dmodel_t) == 48, "dmodel_t size");

// LUMP_LEAFS version 1: Episode One and newer, with the ambient light cube moved out to its own lump.
struct dleaf_t
{
	int32_t contents = 0;			// OR of all the brushes in the leaf
	int16_t cluster = -1;
	int16_t areaFlags = 0;			// area:9, flags:7
	int16_t mins[3] = { 0, 0, 0 };
	int16_t maxs[3] = { 0, 0, 0 };
	uint16_t firstleafface = 0;
	uint16_t numleaffaces = 0;
	uint16_t firstleafbrush = 0;
	uint16_t numleafbrushes = 0;
	int16_t leafWaterDataID = -1;
	int16_t pad = 0;
};
static_assert(sizeof(dleaf_t) == 32, "dleaf_t size");

struct dbrushside_t
{
	uint16_t planenum = 0;
	int16_t texinfo = 0;
	int16_t dispinfo = -1;
	int16_t bevel = 0;
};
static_assert(sizeof(dbrushside_t) == 8, "dbrushside_t size");

struct dbrush_t
{
	int32_t firstside = 0;
	int32_t numsides = 0;
	int32_t contents = 0;
};
static_assert(sizeof(dbrush_t) == 12, "dbrush_t size");

#pragma pack(pop)

/**
 * Collects lumps and writes the file.
 *
 * Lumps are written in the order they are added and each is padded to four bytes, which is what every Source
 * reader assumes even though nothing in the format says so.
 */
class BspWriter
{
public:
	template <typename T>
	void SetLumpArray(int lump, const std::vector<T>& items, int version = 0)
	{
		const uint8_t* bytes = reinterpret_cast<const uint8_t*>(items.data());
		SetLumpBytes(lump, bytes, items.size() * sizeof(T), version);
	}

	void SetLumpBytes(int lump, const uint8_t* data, size_t size, int version = 0);
	void SetLumpString(int lump, const std::string& text, int version = 0);
	void SetMapRevision(int revision) { MapRevision = revision; }

	bool Write(const std::string& path, std::string* outError) const;

private:
	struct LumpData
	{
		std::vector<uint8_t> bytes;
		int version = 0;
		bool bSet = false;
	};
	LumpData Lumps[HEADER_LUMPS];
	int MapRevision = 0;
};

}	// namespace lbsp
