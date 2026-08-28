// Vectors and planes, in Hammer's coordinate system.
//
// Deliberately its own small header rather than anything shared with the engine: LambdaBSP is a command-line
// tool that must build without Unreal, and a map compiler's arithmetic is its own concern. Everything here is
// in Source units and Source's right-handed Z-up space - no conversion happens anywhere in this program, because
// a BSP is a Source file from beginning to end.
#pragma once

#include <cmath>
#include <cstdint>

namespace lbsp
{

// Half the width of Source's world. Windings start out this large and get clipped down.
constexpr double MAX_COORD = 16384.0;

// How far off a plane a point may sit and still count as on it. Valve's ON_EPSILON, and the number that decides
// whether two brushes that touch produce one surface or two overlapping ones.
constexpr double ON_EPSILON = 0.1;
constexpr double NORMAL_EPSILON = 0.00001;
constexpr double DIST_EPSILON = 0.01;

struct Vec3
{
	double x = 0.0, y = 0.0, z = 0.0;

	Vec3() = default;
	Vec3(double InX, double InY, double InZ) : x(InX), y(InY), z(InZ) {}

	double operator[](int i) const { return (&x)[i]; }
	double& operator[](int i) { return (&x)[i]; }

	Vec3 operator+(const Vec3& o) const { return Vec3(x + o.x, y + o.y, z + o.z); }
	Vec3 operator-(const Vec3& o) const { return Vec3(x - o.x, y - o.y, z - o.z); }
	Vec3 operator*(double s) const { return Vec3(x * s, y * s, z * s); }
	Vec3 operator-() const { return Vec3(-x, -y, -z); }
	Vec3& operator+=(const Vec3& o) { x += o.x; y += o.y; z += o.z; return *this; }
};

inline double Dot(const Vec3& a, const Vec3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

inline Vec3 Cross(const Vec3& a, const Vec3& b)
{
	return Vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}

inline double Length(const Vec3& v) { return std::sqrt(Dot(v, v)); }

inline Vec3 Normalize(const Vec3& v)
{
	const double L = Length(v);
	return L > 0.0 ? Vec3(v.x / L, v.y / L, v.z / L) : Vec3();
}

/**
 * A plane, normal pointing out of the solid it bounds.
 *
 * Planes live in a shared pool and are always stored in pairs: index N and index N^1 are the same plane facing
 * opposite ways. Every brush side refers to the pool by index, which is what lets "is this side coplanar with
 * that one" be an integer comparison instead of a float comparison - and coplanar sides are exactly the case
 * that decides whether two touching brushes leave a seam.
 */
struct Plane
{
	Vec3 normal;
	double dist = 0.0;

	double Distance(const Vec3& p) const { return Dot(normal, p) - dist; }
};

/** dplane_t::type - which axis the plane is closest to, PLANE_X..PLANE_ANYZ. */
inline int PlaneTypeForNormal(const Vec3& n)
{
	const double ax = std::fabs(n.x), ay = std::fabs(n.y), az = std::fabs(n.z);
	if (ax == 1.0) return 0;		// PLANE_X
	if (ay == 1.0) return 1;		// PLANE_Y
	if (az == 1.0) return 2;		// PLANE_Z
	if (ax >= ay && ax >= az) return 3;	// PLANE_ANYX
	if (ay >= ax && ay >= az) return 4;	// PLANE_ANYY
	return 5;							// PLANE_ANYZ
}

struct Bounds
{
	Vec3 mins{ 1e30, 1e30, 1e30 };
	Vec3 maxs{ -1e30, -1e30, -1e30 };

	void Add(const Vec3& p)
	{
		for (int i = 0; i < 3; ++i)
		{
			if (p[i] < mins[i]) mins[i] = p[i];
			if (p[i] > maxs[i]) maxs[i] = p[i];
		}
	}

	bool Intersects(const Bounds& o, double Epsilon = 0.0) const
	{
		for (int i = 0; i < 3; ++i)
		{
			if (mins[i] > o.maxs[i] + Epsilon || maxs[i] < o.mins[i] - Epsilon)
			{
				return false;
			}
		}
		return true;
	}

	bool IsValid() const { return mins.x <= maxs.x; }
};

}	// namespace lbsp
