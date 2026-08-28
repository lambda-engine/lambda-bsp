// Convex polygons, and the plane arithmetic that turns a stack of planes into one.
//
// A brush is defined only by its planes; the polygon on each face is not stored anywhere and has to be derived.
// The construction is Valve's (polylib.cpp): start with a square on the plane far larger than the world, then
// clip it by every other plane of the brush until what is left is the face. Ported rather than invented, because
// the epsilon behaviour here is what decides whether two brushes that share a surface produce one polygon or two
// that fight over the same pixels.
#pragma once

#include "Math.h"

#include <vector>

namespace lbsp
{

struct Winding
{
	std::vector<Vec3> points;

	bool IsValid() const { return points.size() >= 3; }
	Bounds GetBounds() const;
	Vec3 Center() const;
	double Area() const;
	/** The plane the polygon lies in, derived from its own winding order (Newell). */
	Plane GetPlane() const;
	/** Drops points that lie on the line between their neighbours; studiomdl-style cleanup after clipping. */
	void RemoveColinearPoints();
};

/** A square on the plane, big enough to contain any face in a Source map. Valve's BaseWindingForPlane. */
Winding BaseWindingForPlane(const Vec3& normal, double dist);

/** Keeps the part of w on the front side of the plane. Returns false when nothing is left. */
bool ChopWindingInPlace(Winding& w, const Vec3& normal, double dist, double epsilon);

/**
 * Splits w by a plane. Either output may come back empty, which is how "entirely on one side" is reported.
 *
 * Used by the CSG pass, where the back half is the part swallowed by another brush and the front half is what
 * still gets drawn.
 */
void SplitWinding(const Winding& w, const Vec3& normal, double dist, double epsilon,
	Winding& outFront, Winding& outBack);

}	// namespace lbsp
