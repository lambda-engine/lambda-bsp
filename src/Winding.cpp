#include "Winding.h"

#include <algorithm>

namespace lbsp
{

Bounds Winding::GetBounds() const
{
	Bounds b;
	for (const Vec3& p : points)
	{
		b.Add(p);
	}
	return b;
}

Vec3 Winding::Center() const
{
	Vec3 c;
	for (const Vec3& p : points)
	{
		c += p;
	}
	return points.empty() ? c : c * (1.0 / (double)points.size());
}

double Winding::Area() const
{
	double total = 0.0;
	for (size_t i = 2; i < points.size(); ++i)
	{
		total += 0.5 * Length(Cross(points[i - 1] - points[0], points[i] - points[0]));
	}
	return total;
}

Plane Winding::GetPlane() const
{
	// Newell's method rather than one cross product from three points: a clipped polygon can have very short
	// edges, and picking an arbitrary corner off one of those gives a normal that is mostly rounding error.
	Vec3 n;
	for (size_t i = 0; i < points.size(); ++i)
	{
		const Vec3& a = points[i];
		const Vec3& b = points[(i + 1) % points.size()];
		n.x += (a.y - b.y) * (a.z + b.z);
		n.y += (a.z - b.z) * (a.x + b.x);
		n.z += (a.x - b.x) * (a.y + b.y);
	}
	Plane p;
	p.normal = Normalize(n);
	p.dist = Dot(p.normal, Center());
	return p;
}

void Winding::RemoveColinearPoints()
{
	if (points.size() < 3)
	{
		return;
	}
	std::vector<Vec3> kept;
	kept.reserve(points.size());
	for (size_t i = 0; i < points.size(); ++i)
	{
		const Vec3& prev = points[(i + points.size() - 1) % points.size()];
		const Vec3& cur = points[i];
		const Vec3& next = points[(i + 1) % points.size()];
		const Vec3 v1 = Normalize(cur - prev);
		const Vec3 v2 = Normalize(next - cur);
		if (Dot(v1, v2) < 0.999)
		{
			kept.push_back(cur);
		}
	}
	points.swap(kept);
}

Winding BaseWindingForPlane(const Vec3& normal, double dist)
{
	// Pick whichever axis the plane is least aligned with, so the "up" vector projected onto the plane cannot
	// come out near zero.
	int major = 0;
	double best = -1.0;
	for (int i = 0; i < 3; ++i)
	{
		const double v = std::fabs(normal[i]);
		if (v > best)
		{
			best = v;
			major = i;
		}
	}

	Vec3 up;
	if (major == 2)
	{
		up.x = 1.0;
	}
	else
	{
		up.z = 1.0;
	}

	up = Normalize(up - normal * Dot(up, normal));
	const Vec3 org = normal * dist;
	const Vec3 right = Cross(up, normal);

	const Vec3 u = up * (MAX_COORD * 4.0);
	const Vec3 r = right * (MAX_COORD * 4.0);

	Winding w;
	w.points.resize(4);
	w.points[0] = org - r + u;
	w.points[1] = org + r + u;
	w.points[2] = org + r - u;
	w.points[3] = org - r - u;
	return w;
}

namespace
{
	enum ESide { SIDE_FRONT = 0, SIDE_BACK = 1, SIDE_ON = 2 };

	// Classifies every point once, so the two passes below agree about which side each one is on. Doing it
	// twice with the same comparison is not the same thing: a point exactly at the epsilon would be free to
	// land differently each time, and the polygon would come apart.
	void ClassifyPoints(const Winding& w, const Vec3& normal, double dist, double epsilon,
		std::vector<int>& outSides, std::vector<double>& outDists, int outCounts[3])
	{
		const size_t n = w.points.size();
		outSides.resize(n + 1);
		outDists.resize(n + 1);
		outCounts[0] = outCounts[1] = outCounts[2] = 0;

		for (size_t i = 0; i < n; ++i)
		{
			const double d = Dot(w.points[i], normal) - dist;
			outDists[i] = d;
			const int side = (d > epsilon) ? SIDE_FRONT : ((d < -epsilon) ? SIDE_BACK : SIDE_ON);
			outSides[i] = side;
			++outCounts[side];
		}
		// The wrap-around entry, so the edge loop below can read i+1 without a modulo.
		outSides[n] = outSides[0];
		outDists[n] = outDists[0];
	}

	Vec3 IntersectEdge(const Vec3& p1, const Vec3& p2, double d1, double d2, const Vec3& normal, double dist)
	{
		const double frac = d1 / (d1 - d2);
		Vec3 mid;
		for (int j = 0; j < 3; ++j)
		{
			// On an axial plane the crossing coordinate is known exactly, so it is written rather than
			// interpolated. Valve does this and it matters: a wall at x=256 whose vertices come out at
			// 255.99997 leaves a hairline crack against the brush next to it.
			if (normal[j] == 1.0)
			{
				mid[j] = dist;
			}
			else if (normal[j] == -1.0)
			{
				mid[j] = -dist;
			}
			else
			{
				mid[j] = p1[j] + frac * (p2[j] - p1[j]);
			}
		}
		return mid;
	}
}

bool ChopWindingInPlace(Winding& w, const Vec3& normal, double dist, double epsilon)
{
	Winding front, back;
	SplitWinding(w, normal, dist, epsilon, front, back);
	w = front;
	return w.IsValid();
}

void SplitWinding(const Winding& w, const Vec3& normal, double dist, double epsilon,
	Winding& outFront, Winding& outBack)
{
	outFront.points.clear();
	outBack.points.clear();
	if (!w.IsValid())
	{
		return;
	}

	std::vector<int> sides;
	std::vector<double> dists;
	int counts[3];
	ClassifyPoints(w, normal, dist, epsilon, sides, dists, counts);

	if (counts[SIDE_BACK] == 0 && counts[SIDE_FRONT] == 0)
	{
		// Every point is on the plane: the polygon is coplanar and belongs to neither half. The caller decides
		// what a coplanar face means - for CSG it is the case that needs a tie-break, not a split.
		return;
	}
	if (counts[SIDE_BACK] == 0)
	{
		outFront = w;
		return;
	}
	if (counts[SIDE_FRONT] == 0)
	{
		outBack = w;
		return;
	}

	const size_t n = w.points.size();
	outFront.points.reserve(n + 4);
	outBack.points.reserve(n + 4);
	for (size_t i = 0; i < n; ++i)
	{
		const Vec3& p1 = w.points[i];
		if (sides[i] == SIDE_ON)
		{
			// A point on the plane is a corner of both halves.
			outFront.points.push_back(p1);
			outBack.points.push_back(p1);
			continue;
		}
		(sides[i] == SIDE_FRONT ? outFront : outBack).points.push_back(p1);

		if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
		{
			continue;
		}

		const Vec3& p2 = w.points[(i + 1) % n];
		const Vec3 mid = IntersectEdge(p1, p2, dists[i], dists[i + 1], normal, dist);
		outFront.points.push_back(mid);
		outBack.points.push_back(mid);
	}

	if (!outFront.IsValid()) outFront.points.clear();
	if (!outBack.IsValid()) outBack.points.clear();
}

}	// namespace lbsp
