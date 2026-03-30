#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// MathUtils.h  –  Lightweight 3-D math primitives for the Voxel Builder.
//
// Provides:
//   Vec3i  – integer 3-vector (grid coordinates, face normals)
//   Vec3f  – float 3-vector  (world positions, ray directions)
//   Mat4   – column-major 4×4 matrix (OpenGL convention)
//
// No external math libraries required; all arithmetic is hand-rolled.
// ─────────────────────────────────────────────────────────────────────────────

#include <cmath>
#include <functional>
#include <algorithm>
#include <limits>

// ── Vec3i ────────────────────────────────────────────────────────────────────

struct Vec3i
{
    int x = 0, y = 0, z = 0;

    Vec3i() = default;
    Vec3i(int x_, int y_, int z_) : x(x_), y(y_), z(z_) {}

    bool operator==(const Vec3i& o) const noexcept
    { return x == o.x && y == o.y && z == o.z; }

    bool operator!=(const Vec3i& o) const noexcept { return !(*this == o); }

    Vec3i operator+(const Vec3i& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3i operator-(const Vec3i& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3i operator*(int s)          const { return {x*s,   y*s,   z*s};   }

    // Useful for checking axis-locked normals
    int dominantAxis() const
    {
        int ax = std::abs(x), ay = std::abs(y), az = std::abs(z);
        if (ax >= ay && ax >= az) return 0;
        if (ay >= az)             return 1;
        return 2;
    }
};

// Hash for use in std::unordered_set / std::unordered_map
struct Vec3iHash
{
    size_t operator()(const Vec3i& v) const noexcept
    {
        // FNV-inspired mix
        size_t h = 2166136261u;
        auto mix = [&](int val)
        {
            h ^= static_cast<size_t>(val + 0x9e3779b9) ;
            h *= 16777619u;
        };
        mix(v.x); mix(v.y); mix(v.z);
        return h;
    }
};

// ── Vec3f ────────────────────────────────────────────────────────────────────

struct Vec3f
{
    float x = 0.f, y = 0.f, z = 0.f;

    Vec3f() = default;
    Vec3f(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    explicit Vec3f(const Vec3i& v)
        : x(static_cast<float>(v.x))
        , y(static_cast<float>(v.y))
        , z(static_cast<float>(v.z)) {}

    Vec3f operator+(const Vec3f& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vec3f operator-(const Vec3f& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vec3f operator*(float s)        const { return {x*s,   y*s,   z*s};   }
    Vec3f operator/(float s)        const { return {x/s,   y/s,   z/s};   }
    Vec3f operator-()               const { return {-x, -y, -z};           }
    Vec3f& operator+=(const Vec3f& o)     { x+=o.x; y+=o.y; z+=o.z; return *this; }

    float dot  (const Vec3f& o) const { return x*o.x + y*o.y + z*o.z; }

    Vec3f cross(const Vec3f& o) const
    {
        return { y*o.z - z*o.y,
                 z*o.x - x*o.z,
                 x*o.y - y*o.x };
    }

    float lengthSq() const { return x*x + y*y + z*z; }
    float length()   const { return std::sqrt(lengthSq()); }

    Vec3f normalized() const
    {
        float l = length();
        return (l > 1e-8f) ? *this * (1.f / l) : Vec3f{0.f, 0.f, -1.f};
    }

    // Floor to integer grid coordinates
    Vec3i floor() const
    {
        return { static_cast<int>(std::floor(x)),
                 static_cast<int>(std::floor(y)),
                 static_cast<int>(std::floor(z)) };
    }
};

// ── Mat4 ─────────────────────────────────────────────────────────────────────
// Column-major storage: m[col * 4 + row], matching OpenGL convention.
//
//  m[ 0] m[ 4] m[ 8] m[12]     col 0   col 1   col 2   col 3
//  m[ 1] m[ 5] m[ 9] m[13]
//  m[ 2] m[ 6] m[10] m[14]
//  m[ 3] m[ 7] m[11] m[15]

struct Mat4
{
    float m[16] = {};

    // ── Factory methods ───────────────────────────────────────────────────────

    static Mat4 identity()
    {
        Mat4 r;
        r.m[0] = r.m[5] = r.m[10] = r.m[15] = 1.f;
        return r;
    }

    // Standard OpenGL perspective projection
    // fovYRad : full vertical FOV in radians
    static Mat4 perspective(float fovYRad, float aspect, float nearZ, float farZ)
    {
        float f = 1.f / std::tan(fovYRad * 0.5f);
        Mat4 r;
        r.m[0]  = f / aspect;
        r.m[5]  = f;
        r.m[10] = (farZ + nearZ) / (nearZ - farZ);
        r.m[11] = -1.f;
        r.m[14] = (2.f * farZ * nearZ) / (nearZ - farZ);
        return r;
    }

    // LookAt view matrix (right-handed, Y-up)
    static Mat4 lookAt(Vec3f eye, Vec3f center, Vec3f up)
    {
        Vec3f f = (center - eye).normalized();
        Vec3f s = f.cross(up).normalized();
        Vec3f u = s.cross(f);

        Mat4 r;
        // Rows of rotation matrix stored in column-major
        r.m[0] = s.x;   r.m[4] = s.y;   r.m[8]  =  s.z;  r.m[12] = -s.dot(eye);
        r.m[1] = u.x;   r.m[5] = u.y;   r.m[9]  =  u.z;  r.m[13] = -u.dot(eye);
        r.m[2] = -f.x;  r.m[6] = -f.y;  r.m[10] = -f.z;  r.m[14] =  f.dot(eye);
        r.m[3] = 0.f;   r.m[7] = 0.f;   r.m[11] =  0.f;  r.m[15] =  1.f;
        return r;
    }

    // Translation matrix
    static Mat4 translate(Vec3f t)
    {
        Mat4 r = identity();
        r.m[12] = t.x;
        r.m[13] = t.y;
        r.m[14] = t.z;
        return r;
    }

    // ── Operations ────────────────────────────────────────────────────────────

    // Matrix multiplication: (this) * b
    Mat4 operator*(const Mat4& b) const
    {
        Mat4 r;
        for (int col = 0; col < 4; ++col)
            for (int row = 0; row < 4; ++row)
            {
                float s = 0.f;
                for (int k = 0; k < 4; ++k)
                    s += m[k*4 + row] * b.m[col*4 + k];
                r.m[col*4 + row] = s;
            }
        return r;
    }

    // Element accessor (row, col)
    float  at(int row, int col) const { return m[col*4 + row]; }
    float& at(int row, int col)       { return m[col*4 + row]; }
};
