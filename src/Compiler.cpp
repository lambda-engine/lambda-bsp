#include "Compiler.h"

#include "BspFile.h"
#include "BspFlags.h"
#include "FileSystem.h"
#include "KeyValues.h"
#include "Materials.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <unordered_map>

namespace lbsp
{

namespace
{

// ---------------------------------------------------------------------------------------------------------
// The plane pool
// ---------------------------------------------------------------------------------------------------------

/**
 * Every plane in the map, stored so that index N and N^1 are the same plane facing opposite ways.
 *
 * That pairing is what makes the CSG pass cheap and, more importantly, exact: asking whether one brush's face
 * lies in the same surface as another's is an integer comparison, not a comparison of floats that were arrived
 * at by different routes. Two brushes that abut are the whole reason the question gets asked.
 */
class PlanePool
{
public:
	int Find(Vec3 normal, double dist)
	{
		Snap(normal, dist);
		for (size_t i = 0; i < Planes.size(); ++i)
		{
			if (std::fabs(Planes[i].normal.x - normal.x) < NORMAL_EPSILON
				&& std::fabs(Planes[i].normal.y - normal.y) < NORMAL_EPSILON
				&& std::fabs(Planes[i].normal.z - normal.z) < NORMAL_EPSILON
				&& std::fabs(Planes[i].dist - dist) < DIST_EPSILON)
			{
				return (int)i;
			}
		}
		// Added as a pair, so the opposite of any index is that index with its bottom bit flipped.
		Plane p;
		p.normal = normal;
		p.dist = dist;
		Plane flipped;
		flipped.normal = -normal;
		flipped.dist = -dist;
		Planes.push_back(p);
		Planes.push_back(flipped);
		return (int)Planes.size() - 2;
	}

	const Plane& operator[](int index) const { return Planes[(size_t)index]; }
	size_t Num() const { return Planes.size(); }

private:
	// A normal a hair off an axis makes every vertex on that face a hair off the grid, and two brushes meant to
	// be flush end up with a crack between them. Valve's SnapPlane, for the same reason.
	static void Snap(Vec3& normal, double& dist)
	{
		for (int i = 0; i < 3; ++i)
		{
			if (std::fabs(normal[i] - 1.0) < NORMAL_EPSILON)
			{
				normal = Vec3();
				normal[i] = 1.0;
				break;
			}
			if (std::fabs(normal[i] + 1.0) < NORMAL_EPSILON)
			{
				normal = Vec3();
				normal[i] = -1.0;
				break;
			}
		}
		const double rounded = std::floor(dist + 0.5);
		if (std::fabs(dist - rounded) < DIST_EPSILON)
		{
			dist = rounded;
		}
	}

	std::vector<Plane> Planes;
};

// ---------------------------------------------------------------------------------------------------------
// The map, as read out of the VMF
// ---------------------------------------------------------------------------------------------------------

struct BrushTexture
{
	std::string material;
	Vec3 uAxis, vAxis;
	double uShift = 0.0, vShift = 0.0;
	double uScale = 1.0, vScale = 1.0;
	double lightmapScale = 16.0;
};

struct Side
{
	int planenum = 0;
	BrushTexture tex;
	Winding winding;					// the face before CSG
	std::vector<Winding> fragments;		// what survives it
	int surfaceFlags = 0;
	int texinfo = -1;
	bool bHasDisplacement = false;
};

struct Brush
{
	std::vector<Side> sides;
	Bounds bounds;
	int contents = CONTENTS_SOLID;
	bool bIsOrigin = false;
	int index = 0;				// position within its model, for the coplanar tie-break
};

struct EntityDef
{
	std::vector<std::pair<std::string, std::string>> keys;
	std::vector<Brush> brushes;
	Vec3 origin;
	int modelIndex = -1;		// -1 for a point entity

	const std::string* Find(const std::string& key) const
	{
		for (const auto& kv : keys)
		{
			if (EqualsNoCase(kv.first, key))
			{
				return &kv.second;
			}
		}
		return nullptr;
	}

	void Set(const std::string& key, const std::string& value)
	{
		for (auto& kv : keys)
		{
			if (EqualsNoCase(kv.first, key))
			{
				kv.second = value;
				return;
			}
		}
		keys.emplace_back(key, value);
	}
};

// ---------------------------------------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------------------------------------

bool ParseSide(const KeyValues& kv, PlanePool& planes, MaterialCache& materials, Side& outSide, std::string& outError)
{
	double pts[3][3];
	if (!ParsePlanePoints(kv.GetString("plane"), pts))
	{
		outError = "a side has no readable \"plane\"";
		return false;
	}
	const Vec3 p0(pts[0][0], pts[0][1], pts[0][2]);
	const Vec3 p1(pts[1][0], pts[1][1], pts[1][2]);
	const Vec3 p2(pts[2][0], pts[2][1], pts[2][2]);

	// Valve's PlaneFromPoints: normal = normalize(cross(p0 - p1, p2 - p1)), pointing out of the brush.
	const Vec3 normal = Normalize(Cross(p0 - p1, p2 - p1));
	if (Length(normal) < 0.5)
	{
		outError = "a side's three plane points are collinear";
		return false;
	}
	outSide.planenum = planes.Find(normal, Dot(p0, normal));

	outSide.tex.material = kv.GetString("material");
	double axis[3] = { 1, 0, 0 };
	double shift = 0.0, scale = 1.0;
	if (ParseTextureAxis(kv.GetString("uaxis"), axis, shift, scale))
	{
		outSide.tex.uAxis = Vec3(axis[0], axis[1], axis[2]);
		outSide.tex.uShift = shift;
		outSide.tex.uScale = scale;
	}
	if (ParseTextureAxis(kv.GetString("vaxis"), axis, shift, scale))
	{
		outSide.tex.vAxis = Vec3(axis[0], axis[1], axis[2]);
		outSide.tex.vShift = shift;
		outSide.tex.vScale = scale;
	}
	outSide.tex.lightmapScale = kv.GetNumber("lightmapscale", 16.0);
	if (outSide.tex.lightmapScale <= 0.0)
	{
		outSide.tex.lightmapScale = 16.0;
	}

	// A displacement replaces the face it sits on with a subdivided mesh. LambdaBSP does not generate that mesh
	// yet, and emitting the flat face in its place would be a lie the renderer cannot tell from real geometry -
	// so the side is marked and skipped, and the compile says how many were dropped.
	outSide.bHasDisplacement = kv.GetBlock("dispinfo") != nullptr;

	const MaterialInfo& mat = materials.Get(outSide.tex.material);
	outSide.surfaceFlags = mat.surfaceFlags;
	return true;
}

bool ParseSolid(const KeyValues& kv, PlanePool& planes, MaterialCache& materials, Brush& outBrush, std::string& outError)
{
	const std::vector<const KeyValues*> sides = kv.GetBlocks("side");
	if (sides.size() < 4)
	{
		outError = "a solid has fewer than four sides";
		return false;
	}
	outBrush.sides.reserve(sides.size());
	for (const KeyValues* s : sides)
	{
		Side side;
		if (!ParseSide(*s, planes, materials, side, outError))
		{
			return false;
		}
		outBrush.sides.push_back(std::move(side));
	}

	// vbsp takes the brush's contents from its sides' materials. An origin brush is not geometry at all - it
	// tells a brush entity where to pivot - so it is recognised here and dropped after the entity is placed.
	int contents = 0;
	bool bAny = false;
	for (const Side& s : outBrush.sides)
	{
		const MaterialInfo& mat = materials.Get(s.tex.material);
		if (mat.contents & CONTENTS_ORIGIN)
		{
			outBrush.bIsOrigin = true;
		}
		if (!bAny)
		{
			contents = mat.contents;
			bAny = true;
		}
	}
	outBrush.contents = bAny ? contents : CONTENTS_SOLID;
	return true;
}

/** Derives each side's polygon by clipping a huge square on its plane by all the brush's other planes. */
void BuildBrushWindings(Brush& brush, const PlanePool& planes)
{
	// The clip is done about a point inside the brush and moved back afterwards. The average of points taken
	// from the boundary of a convex solid is inside it, and clipping near the origin rather than out at the
	// edge of the world is worth several digits of precision on a big map.
	Vec3 center;
	int count = 0;
	for (const Side& s : brush.sides)
	{
		center += planes[s.planenum].normal * planes[s.planenum].dist;
		++count;
	}
	if (count > 0)
	{
		center = center * (1.0 / count);
	}
	const Vec3 offset = -center;

	brush.bounds = Bounds();
	for (size_t i = 0; i < brush.sides.size(); ++i)
	{
		const Plane& p = planes[brush.sides[i].planenum];
		Winding w = BaseWindingForPlane(p.normal, p.dist + Dot(p.normal, offset));
		for (size_t j = 0; j < brush.sides.size() && w.IsValid(); ++j)
		{
			if (i == j)
			{
				continue;
			}
			// The opposite of the other side's plane: keep what is on the inside of that face.
			const Plane& other = planes[brush.sides[j].planenum ^ 1];
			ChopWindingInPlace(w, other.normal, other.dist + Dot(other.normal, offset), 0.0);
		}
		for (Vec3& pt : w.points)
		{
			pt = pt - offset;
		}
		w.RemoveColinearPoints();
		brush.sides[i].winding = w;
		for (const Vec3& pt : w.points)
		{
			brush.bounds.Add(pt);
		}
	}
}

// ---------------------------------------------------------------------------------------------------------
// CSG
// ---------------------------------------------------------------------------------------------------------

/**
 * Removes from `w` whatever brush B swallows, returning the fragments still worth drawing.
 *
 * Two cases the generic clip cannot handle on its own, and both come up in every map ever built:
 *
 *  - the face lies exactly in one of B's own faces, pointing the same way. Two brushes overlap and each
 *    claims the shared surface; if both keep it they flicker against each other, so the lower-numbered brush
 *    wins and the other gives it up.
 *  - the face lies exactly in one of B's faces pointing the *opposite* way. That is two brushes stacked
 *    against each other, and the surface between them is interior - neither should draw it.
 *
 * In both, the winding sits precisely on a clipping plane, where a split returns nothing at all rather than a
 * front and a back. Skipping that plane and testing the rest gives the footprint of the overlap.
 */
std::vector<Winding> SubtractBrush(const Winding& w, int sidePlane, int ownerIndex,
	const Brush& b, const PlanePool& planes)
{
	std::vector<Winding> kept;

	bool bCoincidentSame = false;
	bool bCoincidentOpposite = false;
	for (const Side& s : b.sides)
	{
		bCoincidentSame = bCoincidentSame || s.planenum == sidePlane;
		bCoincidentOpposite = bCoincidentOpposite || s.planenum == (sidePlane ^ 1);
	}
	if (bCoincidentSame && ownerIndex < b.index)
	{
		kept.push_back(w);		// this brush owns the shared surface
		return kept;
	}

	Winding inside = w;
	for (const Side& s : b.sides)
	{
		if (s.planenum == sidePlane || s.planenum == (sidePlane ^ 1))
		{
			continue;
		}
		const Plane& p = planes[s.planenum];
		Winding front, back;
		SplitWinding(inside, p.normal, p.dist, ON_EPSILON, front, back);
		if (front.IsValid())
		{
			kept.push_back(front);
		}
		inside = back;
		if (!inside.IsValid())
		{
			break;
		}
	}
	// Whatever is left in `inside` lies within B and is not drawn.
	return kept;
}

void ChopBrushes(std::vector<Brush>& brushes, const PlanePool& planes, bool bNoCsg, CompileStats& stats)
{
	for (size_t i = 0; i < brushes.size(); ++i)
	{
		Brush& a = brushes[i];
		for (Side& s : a.sides)
		{
			if (!s.winding.IsValid())
			{
				continue;
			}
			++stats.facesBuilt;
			s.fragments.clear();
			s.fragments.push_back(s.winding);
			if (bNoCsg)
			{
				continue;
			}

			for (size_t j = 0; j < brushes.size() && !s.fragments.empty(); ++j)
			{
				const Brush& b = brushes[j];
				if (i == j || !(b.contents & CONTENTS_SOLID) || b.bIsOrigin)
				{
					continue;
				}
				if (!a.bounds.Intersects(b.bounds, ON_EPSILON))
				{
					continue;
				}
				std::vector<Winding> next;
				for (const Winding& frag : s.fragments)
				{
					std::vector<Winding> pieces = SubtractBrush(frag, s.planenum, a.index, b, planes);
					for (Winding& piece : pieces)
					{
						next.push_back(std::move(piece));
					}
				}
				s.fragments.swap(next);
			}
			if (s.fragments.empty())
			{
				++stats.facesCulledCsg;
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------------------
// Emitting
// ---------------------------------------------------------------------------------------------------------

/** Accumulates the lumps as faces are added, and hands out the deduplicated indices they refer to. */
struct LumpBuilder
{
	std::vector<dplane_t> planes;
	std::vector<dvertex_t> vertexes;
	std::vector<dedge_t> edges;
	std::vector<int32_t> surfedges;
	std::vector<dface_t> faces;
	std::vector<texinfo_t> texinfos;
	std::vector<dtexdata_t> texdatas;
	std::vector<int32_t> texdataStringTable;
	std::string texdataStringData;
	std::vector<dmodel_t> models;

	std::unordered_map<uint64_t, int> vertexLookup;
	std::map<std::pair<int, int>, int> edgeLookup;
	std::unordered_map<std::string, int> texdataLookup;

	LumpBuilder()
	{
		// Edge 0 is never used: a face refers to an edge by a signed index, and there is no negative zero to
		// spell "this edge, backwards".
		edges.push_back(dedge_t());
	}

	static Vec3 SnapVertex(Vec3 v)
	{
		// A vertex within a hundredth of a whole unit is put on it. Brushes are built on a grid, so this is
		// almost always restoring a number that only drifted through the arithmetic - and two faces that
		// should share an edge then share it exactly, instead of leaving a seam to see the void through.
		for (int i = 0; i < 3; ++i)
		{
			const double rounded = std::floor(v[i] + 0.5);
			if (std::fabs(v[i] - rounded) < 0.01)
			{
				v[i] = rounded;
			}
		}
		return v;
	}

	int AddVertex(const Vec3& raw)
	{
		const Vec3 v = SnapVertex(raw);
		// Quantised to a sixteenth of a unit for the lookup only; what gets stored is the snapped value.
		const int64_t qx = (int64_t)std::floor(v.x * 16.0 + 0.5);
		const int64_t qy = (int64_t)std::floor(v.y * 16.0 + 0.5);
		const int64_t qz = (int64_t)std::floor(v.z * 16.0 + 0.5);
		const uint64_t key = (uint64_t)((qx * 73856093) ^ (qy * 19349663) ^ (qz * 83492791));
		auto range = vertexLookup.equal_range(key);
		for (auto it = range.first; it != range.second; ++it)
		{
			const dvertex_t& e = vertexes[(size_t)it->second];
			if (std::fabs(e.point.x - v.x) < 0.03 && std::fabs(e.point.y - v.y) < 0.03
				&& std::fabs(e.point.z - v.z) < 0.03)
			{
				return it->second;
			}
		}
		dvertex_t dv;
		dv.point = dvec3_t(v);
		vertexes.push_back(dv);
		const int index = (int)vertexes.size() - 1;
		vertexLookup.emplace(key, index);
		return index;
	}

	/** Positive for an edge used forwards, negative for one an earlier face already used the other way. */
	int32_t AddSurfEdge(int v0, int v1)
	{
		const auto reversed = edgeLookup.find({ v1, v0 });
		if (reversed != edgeLookup.end())
		{
			return -reversed->second;
		}
		const auto forward = edgeLookup.find({ v0, v1 });
		if (forward != edgeLookup.end())
		{
			return forward->second;
		}
		dedge_t e;
		e.v[0] = (uint16_t)v0;
		e.v[1] = (uint16_t)v1;
		edges.push_back(e);
		const int index = (int)edges.size() - 1;
		edgeLookup[{ v0, v1 }] = index;
		return index;
	}

	int AddTexData(const std::string& materialName, const MaterialInfo& mat)
	{
		const std::string key = NormalizeMaterialName(materialName);
		const auto it = texdataLookup.find(key);
		if (it != texdataLookup.end())
		{
			return it->second;
		}

		// The name is stored as the mapper wrote it, which is how Valve's own maps read.
		const int32_t stringOffset = (int32_t)texdataStringData.size();
		texdataStringData += materialName;
		texdataStringData.push_back('\0');
		texdataStringTable.push_back(stringOffset);

		dtexdata_t td;
		td.nameStringTableID = (int32_t)texdataStringTable.size() - 1;
		td.reflectivity = dvec3_t(mat.reflectivity);
		td.width = mat.width;
		td.height = mat.height;
		td.view_width = mat.width;
		td.view_height = mat.height;
		texdatas.push_back(td);
		const int index = (int)texdatas.size() - 1;
		texdataLookup.emplace(key, index);
		return index;
	}
};

int EmitTexInfo(LumpBuilder& out, const BrushTexture& tex, int surfaceFlags, int texdata, const Vec3& modelOffset)
{
	texinfo_t ti;
	// The axes carry the scale; the shift does not. That asymmetry is Valve's (TexinfoForBrushTexture) and
	// getting it backwards slides every texture in the map by a factor of its own scale.
	for (int i = 0; i < 3; ++i)
	{
		ti.textureVecsTexelsPerWorldUnits[0][i] = (float)(tex.uAxis[i] / tex.uScale);
		ti.textureVecsTexelsPerWorldUnits[1][i] = (float)(tex.vAxis[i] / tex.vScale);
		ti.lightmapVecsLuxelsPerWorldUnits[0][i] = (float)(tex.uAxis[i] / tex.lightmapScale);
		ti.lightmapVecsLuxelsPerWorldUnits[1][i] = (float)(tex.vAxis[i] / tex.lightmapScale);
	}
	// Brush-entity geometry is moved to sit around its own origin, so the texture has to move with it or the
	// pattern would slide across the surface as soon as the door it is on was compiled.
	const Vec3 u(ti.textureVecsTexelsPerWorldUnits[0][0], ti.textureVecsTexelsPerWorldUnits[0][1],
		ti.textureVecsTexelsPerWorldUnits[0][2]);
	const Vec3 v(ti.textureVecsTexelsPerWorldUnits[1][0], ti.textureVecsTexelsPerWorldUnits[1][1],
		ti.textureVecsTexelsPerWorldUnits[1][2]);
	ti.textureVecsTexelsPerWorldUnits[0][3] = (float)(tex.uShift + Dot(modelOffset, u));
	ti.textureVecsTexelsPerWorldUnits[1][3] = (float)(tex.vShift + Dot(modelOffset, v));

	const double shiftScaleU = tex.uScale / tex.lightmapScale;
	const double shiftScaleV = tex.vScale / tex.lightmapScale;
	const Vec3 lu(ti.lightmapVecsLuxelsPerWorldUnits[0][0], ti.lightmapVecsLuxelsPerWorldUnits[0][1],
		ti.lightmapVecsLuxelsPerWorldUnits[0][2]);
	const Vec3 lv(ti.lightmapVecsLuxelsPerWorldUnits[1][0], ti.lightmapVecsLuxelsPerWorldUnits[1][1],
		ti.lightmapVecsLuxelsPerWorldUnits[1][2]);
	ti.lightmapVecsLuxelsPerWorldUnits[0][3] = (float)(shiftScaleU * tex.uShift + Dot(modelOffset, lu));
	ti.lightmapVecsLuxelsPerWorldUnits[1][3] = (float)(shiftScaleV * tex.vShift + Dot(modelOffset, lv));

	ti.flags = surfaceFlags;
	ti.texdata = texdata;

	for (size_t i = 0; i < out.texinfos.size(); ++i)
	{
		if (std::memcmp(&out.texinfos[i], &ti, sizeof(texinfo_t)) == 0)
		{
			return (int)i;
		}
	}
	out.texinfos.push_back(ti);
	return (int)out.texinfos.size() - 1;
}

/** Turns one surviving polygon into a dface_t and everything it points at. */
void EmitFace(LumpBuilder& out, const Winding& w, int planenum, int texinfo)
{
	if (w.points.size() < 3 || w.points.size() > 255)
	{
		return;
	}

	std::vector<int> indices;
	indices.reserve(w.points.size());
	for (const Vec3& p : w.points)
	{
		indices.push_back(out.AddVertex(p));
	}
	// Snapping can weld two corners of a sliver into one; what is left is not a polygon.
	for (size_t i = 0; i < indices.size(); ++i)
	{
		if (indices[i] == indices[(i + 1) % indices.size()])
		{
			return;
		}
	}

	dface_t f;
	f.planenum = (uint16_t)(planenum & ~1);
	f.side = (uint8_t)(planenum & 1);
	f.onNode = 0;
	f.firstedge = (int32_t)out.surfedges.size();
	f.numedges = (int16_t)indices.size();
	f.texinfo = (int16_t)texinfo;
	f.dispinfo = -1;
	f.surfaceFogVolumeID = -1;
	f.lightofs = -1;
	f.area = (float)w.Area();
	f.origFace = -1;

	for (size_t i = 0; i < indices.size(); ++i)
	{
		out.surfedges.push_back(out.AddSurfEdge(indices[i], indices[(i + 1) % indices.size()]));
	}
	out.faces.push_back(f);
}

/**
 * Emits one model - the world, or one brush entity - and returns its dmodel_t.
 *
 * Brush-entity geometry is written relative to the entity's own origin, because that is the pivot the engine
 * rotates and moves it about. A door compiled in world coordinates swings around a point somewhere else in
 * the level.
 */
dmodel_t EmitModel(LumpBuilder& out, std::vector<Brush>& brushes, const PlanePool& planes,
	MaterialCache& materials, const Vec3& origin, CompileStats& stats)
{
	dmodel_t model;
	model.firstface = (int32_t)out.faces.size();

	Bounds bounds;
	for (Brush& brush : brushes)
	{
		if (brush.bIsOrigin)
		{
			continue;	// not geometry; it only said where the entity pivots
		}
		for (Side& side : brush.sides)
		{
			if (side.fragments.empty() || side.bHasDisplacement)
			{
				continue;
			}
			// Faces that exist only to instruct the compiler never reach the renderer. NODRAW and SKY do,
			// because the engine still wants them for collision and for the sky.
			// SURF_TRIGGER is deliberately not in this list. Hint and skip describe nothing in the world, but
			// a trigger volume's faces are the only shape the engine has to test against - Source can fall
			// back on the brush model, this cannot.
			if (side.surfaceFlags & (SURF_SKIP | SURF_HINT))
			{
				stats.facesCulledNoDraw += (int)side.fragments.size();
				continue;
			}

			const MaterialInfo& mat = materials.Get(side.tex.material);
			const int texdata = out.AddTexData(side.tex.material, mat);
			const int texinfo = EmitTexInfo(out, side.tex, side.surfaceFlags, texdata, -origin);

			for (const Winding& frag : side.fragments)
			{
				Winding moved = frag;
				for (Vec3& p : moved.points)
				{
					p = p - origin;
					bounds.Add(p);
				}
				const size_t before = out.faces.size();
				EmitFace(out, moved, side.planenum, texinfo);
				stats.facesEmitted += (int)(out.faces.size() - before);
			}
		}
	}

	model.numfaces = (int32_t)out.faces.size() - model.firstface;
	if (bounds.IsValid())
	{
		model.mins = dvec3_t(bounds.mins);
		model.maxs = dvec3_t(bounds.maxs);
	}
	// headnode is -1 rather than a lie: LambdaBSP writes no BSP tree, and a reader that walks one must be told
	// there is nothing to walk rather than being sent to node zero.
	model.headnode = -1;
	return model;
}

std::string BuildEntityLump(const std::vector<EntityDef>& entities)
{
	std::string text;
	for (const EntityDef& e : entities)
	{
		text += "{\n";
		for (const auto& kv : e.keys)
		{
			text += "\"" + kv.first + "\" \"" + kv.second + "\"\n";
		}
		text += "}\n";
	}
	return text;
}

/**
 * Copies an entity's keyvalues out of the VMF, leaving behind what only Hammer cares about.
 *
 * "id" survives as "hammerid", which is what vbsp does and worth keeping: it is the only thread back from an
 * entity misbehaving in game to the object the mapper can select in the editor.
 */
void CopyEntityKeys(const KeyValues& from, EntityDef& to)
{
	for (const KeyValues::Entry& e : from.GetEntries())
	{
		if (e.block)
		{
			// "connections" is where Hammer keeps an entity's outputs, and they are keyvalues after all - vbsp
			// copies each one into the entity verbatim, key ("OnPressed") and comma-delimited value alike
			// (CMapFile::LoadConnectionsKeyCallback). Dropping the block along with "editor" and "solid" costs
			// the map every wire the mapper drew, and costs it silently: the entities all arrive, they simply
			// never talk to each other.
			if (EqualsNoCase(e.key, "connections"))
			{
				for (const KeyValues::Entry& c : e.block->GetEntries())
				{
					if (!c.block)
					{
						// Appended, not Set: one output fires as many connections as the mapper drew from it,
						// and they are all spelled with the same key.
						to.keys.emplace_back(c.key, c.value);
					}
				}
			}
			continue;		// "editor" and "solid" really are not keyvalues
		}
		if (EqualsNoCase(e.key, "id"))
		{
			to.Set("hammerid", e.value);
			continue;
		}
		to.Set(e.key, e.value);
	}
}

}	// anonymous namespace

// ---------------------------------------------------------------------------------------------------------

bool CompileMap(const CompileOptions& options, CompileStats& stats, std::string& outError)
{
	const auto started = std::chrono::steady_clock::now();

	std::vector<uint8_t> vmfBytes;
	if (!ReadWholeFile(options.vmfPath, vmfBytes))
	{
		outError = "cannot read " + options.vmfPath;
		return false;
	}
	const std::string vmfText((const char*)vmfBytes.data(), vmfBytes.size());
	std::string parseError;
	KeyValues* vmf = KeyValues::ParseDocument(vmfText, &parseError);
	if (!vmf)
	{
		outError = options.vmfPath + ": " + parseError;
		return false;
	}

	FileSystem files;
	std::vector<std::string> mountLog;
	if (!options.modDir.empty())
	{
		files.MountGameInfo(options.modDir, mountLog);
	}
	if (options.bVerbose)
	{
		for (const std::string& line : files.GetMountDescriptions())
		{
			std::printf("  mount %s\n", line.c_str());
		}
	}
	for (const std::string& line : mountLog)
	{
		std::printf("  %s\n", line.c_str());
	}

	MaterialCache materials(files);
	PlanePool planes;

	// ------------------------------------------------------------------ the world
	const KeyValues* world = vmf->GetBlock("world");
	if (!world)
	{
		outError = "the VMF has no \"world\" block";
		delete vmf;
		return false;
	}

	std::vector<Brush> worldBrushes;
	for (const KeyValues* solid : world->GetBlocks("solid"))
	{
		Brush brush;
		std::string err;
		if (!ParseSolid(*solid, planes, materials, brush, err))
		{
			std::printf("  warning: %s (solid skipped)\n", err.c_str());
			continue;
		}
		worldBrushes.push_back(std::move(brush));
	}

	// ------------------------------------------------------------------ the entities
	std::vector<EntityDef> entities;
	EntityDef worldspawn;
	CopyEntityKeys(*world, worldspawn);
	worldspawn.Set("classname", "worldspawn");
	// Deliberately no "model" key: model 0 is the world and the engine builds it directly. Writing "*0" here
	// asks it to also spawn the world as a brush entity, which it rightly refuses.
	entities.push_back(worldspawn);
	const size_t worldspawnIndex = entities.size() - 1;

	std::vector<EntityDef> brushEntities;
	for (const KeyValues* ent : vmf->GetBlocks("entity"))
	{
		EntityDef def;
		CopyEntityKeys(*ent, def);

		const std::vector<const KeyValues*> solids = ent->GetBlocks("solid");
		for (const KeyValues* solid : solids)
		{
			Brush brush;
			std::string err;
			if (!ParseSolid(*solid, planes, materials, brush, err))
			{
				std::printf("  warning: %s (solid skipped)\n", err.c_str());
				continue;
			}
			def.brushes.push_back(std::move(brush));
		}

		const std::string* classname = def.Find("classname");
		const bool bIsDetail = classname && EqualsNoCase(*classname, "func_detail");
		if (!def.brushes.empty() && bIsDetail)
		{
			// func_detail is a compiler instruction, not an entity: its brushes are part of the world and it
			// leaves nothing behind in the entity lump.
			for (Brush& b : def.brushes)
			{
				worldBrushes.push_back(std::move(b));
			}
			continue;
		}
		if (def.brushes.empty())
		{
			entities.push_back(std::move(def));		// a point entity
			continue;
		}
		brushEntities.push_back(std::move(def));
	}

	// ------------------------------------------------------------------ geometry
	auto prepare = [&](std::vector<Brush>& brushes)
	{
		for (size_t i = 0; i < brushes.size(); ++i)
		{
			brushes[i].index = (int)i;
			BuildBrushWindings(brushes[i], planes);
		}
		ChopBrushes(brushes, planes, options.bNoCsg, stats);
	};

	prepare(worldBrushes);
	for (EntityDef& def : brushEntities)
	{
		prepare(def.brushes);

		// Where the entity pivots: an origin brush if it has one, otherwise whatever it says in "origin",
		// otherwise the world origin.
		bool bFoundOrigin = false;
		for (const Brush& b : def.brushes)
		{
			if (b.bIsOrigin && b.bounds.IsValid())
			{
				def.origin = (b.bounds.mins + b.bounds.maxs) * 0.5;
				bFoundOrigin = true;
				break;
			}
		}
		if (!bFoundOrigin)
		{
			if (const std::string* originKey = def.Find("origin"))
			{
				double v[3] = { 0, 0, 0 };
				if (ParseNumbers(*originKey, v, 3) == 3)
				{
					def.origin = Vec3(v[0], v[1], v[2]);
				}
			}
		}
	}

	// ------------------------------------------------------------------ lumps
	LumpBuilder out;
	for (size_t i = 0; i < planes.Num(); ++i)
	{
		dplane_t p;
		p.normal = dvec3_t(planes[(int)i].normal);
		p.dist = (float)planes[(int)i].dist;
		p.type = PlaneTypeForNormal(planes[(int)i].normal);
		out.planes.push_back(p);
	}

	out.models.push_back(EmitModel(out, worldBrushes, planes, materials, Vec3(), stats));

	// worldspawn advertises the extent of the level, the way vbsp writes it.
	{
		const dmodel_t& world0 = out.models[0];
		char text[96];
		std::snprintf(text, sizeof(text), "%d %d %d",
			(int)world0.mins.x, (int)world0.mins.y, (int)world0.mins.z);
		entities[worldspawnIndex].Set("world_mins", text);
		std::snprintf(text, sizeof(text), "%d %d %d",
			(int)world0.maxs.x, (int)world0.maxs.y, (int)world0.maxs.z);
		entities[worldspawnIndex].Set("world_maxs", text);
	}
	for (EntityDef& def : brushEntities)
	{
		const int modelIndex = (int)out.models.size();
		out.models.push_back(EmitModel(out, def.brushes, planes, materials, def.origin, stats));

		def.Set("model", "*" + std::to_string(modelIndex));
		char originText[96];
		std::snprintf(originText, sizeof(originText), "%g %g %g", def.origin.x, def.origin.y, def.origin.z);
		def.Set("origin", originText);
		entities.push_back(def);
	}

	stats.brushes = (int)worldBrushes.size();
	for (const EntityDef& def : brushEntities)
	{
		stats.brushes += (int)def.brushes.size();
	}
	stats.entities = (int)entities.size();
	stats.brushEntities = (int)brushEntities.size();
	stats.materials = (int)out.texdatas.size();
	stats.missingMaterials = materials.GetMissingCount();

	if (out.vertexes.size() > 65535)
	{
		outError = "too many vertices for the format (" + std::to_string(out.vertexes.size())
			+ "); a BSP addresses them with 16 bits";
		delete vmf;
		return false;
	}

	BspWriter writer;
	writer.SetMapRevision(world->GetInt("mapversion", 0));
	writer.SetLumpString(LUMP_ENTITIES, BuildEntityLump(entities));
	writer.SetLumpArray(LUMP_PLANES, out.planes);
	writer.SetLumpArray(LUMP_TEXDATA, out.texdatas);
	writer.SetLumpArray(LUMP_VERTEXES, out.vertexes);
	writer.SetLumpArray(LUMP_TEXINFO, out.texinfos);
	writer.SetLumpArray(LUMP_FACES, out.faces);
	writer.SetLumpArray(LUMP_EDGES, out.edges);
	writer.SetLumpArray(LUMP_SURFEDGES, out.surfedges);
	writer.SetLumpArray(LUMP_MODELS, out.models);
	writer.SetLumpArray(LUMP_TEXDATA_STRING_TABLE, out.texdataStringTable);
	writer.SetLumpBytes(LUMP_TEXDATA_STRING_DATA,
		(const uint8_t*)out.texdataStringData.data(), out.texdataStringData.size());
	// v20 leaves carry no ambient lighting of their own; the version says so even for an empty lump.
	writer.SetLumpBytes(LUMP_LEAFS, nullptr, 0, 1);

	std::string writeError;
	if (!writer.Write(options.bspPath, &writeError))
	{
		outError = writeError;
		delete vmf;
		return false;
	}

	delete vmf;
	stats.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
	return true;
}

}	// namespace lbsp
