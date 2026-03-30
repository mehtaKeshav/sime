// ─────────────────────────────────────────────────────────────────────────────
// Raycaster.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "Raycaster.h"
#include <cmath>
#include <algorithm>
#include <limits>

// ─────────────────────────────────────────────────────────────────────────────
// screenToRay
//
// Converts a 2-D screen pixel to a normalised world-space ray direction.
//
// Math overview:
//   1. Pixel → NDC  (normalised device coordinates, [-1,+1] × [-1,+1])
//   2. NDC   → View-space direction via inverse perspective division
//   3. View  → World by transposing the upper-left 3×3 rotation block of the
//              view matrix (view = [R|t], so inv(view) rotation = R^T).
// ─────────────────────────────────────────────────────────────────────────────

Vec3f Raycaster::screenToRay(float mouseX, float mouseY,
                              float screenW, float screenH,
                              const Mat4& view,
                              const Mat4& proj)
{
    // ── Step 1: pixel → NDC ───────────────────────────────────────────────────
    float ndcX =  (2.f * mouseX / screenW) - 1.f;
    float ndcY = -(2.f * mouseY / screenH) + 1.f;   // Y flipped (screen vs NDC)

    // ── Step 2: NDC → view-space direction ────────────────────────────────────
    // For a standard perspective matrix:
    //   proj.m[0] = f / aspect   (element at row 0, col 0)
    //   proj.m[5] = f            (element at row 1, col 1)
    // So the un-projection is:
    //   x_view = ndcX / proj.m[0]
    //   y_view = ndcY / proj.m[5]
    //   z_view = -1  (pointing into the screen in view space)
    Vec3f viewDir
    {
        ndcX / proj.m[0],
        ndcY / proj.m[5],
        -1.f
    };

    // ── Step 3: view-space → world-space ─────────────────────────────────────
    // The view matrix V = [R | t] transforms world → view.
    // To transform a direction (w=0) from view → world we apply R^T.
    //
    // Column-major layout of lookAt (m[col*4+row]):
    //   m[0]=s.x  m[1]=u.x  m[2]=-f.x   (col 0, rows 0-2)
    //   m[4]=s.y  m[5]=u.y  m[6]=-f.y   (col 1, rows 0-2)
    //   m[8]=s.z  m[9]=u.z  m[10]=-f.z  (col 2, rows 0-2)
    //
    // (R^T * v)_i  = Σ_j  R[j][i] * v[j]
    //             = Σ_j  m[i*4+j] * v[j]     (reading the i-th "row of stored cols")
    //
    // So:
    //   world.x = m[0]*vx + m[1]*vy + m[2]*vz
    //   world.y = m[4]*vx + m[5]*vy + m[6]*vz
    //   world.z = m[8]*vx + m[9]*vy + m[10]*vz
    Vec3f world
    {
        view.m[0]*viewDir.x + view.m[1]*viewDir.y + view.m[2]*viewDir.z,
        view.m[4]*viewDir.x + view.m[5]*viewDir.y + view.m[6]*viewDir.z,
        view.m[8]*viewDir.x + view.m[9]*viewDir.y + view.m[10]*viewDir.z
    };

    return world.normalized();
}

// ─────────────────────────────────────────────────────────────────────────────
// cast  –  Amanatides & Woo DDA grid traversal
//
// For each axis, we track:
//   tMax   : the t-value (distance along the ray) at the *next* grid boundary
//   tDelta : how far we must travel between consecutive boundaries on that axis
//   step   : +1 or -1 (direction of traversal on that axis)
//
// At every iteration we advance along the axis whose tMax is smallest, then
// check whether the newly entered cell contains a voxel.
//
// The face normal of a hit is the negation of the step direction on the axis
// we just advanced (because we entered from the negative step side).
// ─────────────────────────────────────────────────────────────────────────────

RaycastResult Raycaster::cast(const Vec3f&   origin,
                               const Vec3f&   dir,
                               const VoxelGrid& grid)
{
    const float kInf = std::numeric_limits<float>::max();

    // ── Initialise per-axis DDA parameters ───────────────────────────────────

    auto initAxis = [&](float orig, float d, int& step,
                        float& tMax, float& tDelta)
    {
        if (std::abs(d) < 1e-8f)
        {
            step   = 0;
            tMax   = kInf;
            tDelta = kInf;
            return;
        }
        tDelta = 1.f / std::abs(d);
        if (d > 0.f)
        {
            step = +1;
            // distance to next integer above origin
            tMax = (std::floor(orig) + 1.f - orig) / d;
        }
        else
        {
            step = -1;
            // distance to next integer below origin
            tMax = (orig - std::floor(orig)) / (-d);
        }
    };

    int   stepX, stepY, stepZ;
    float tMaxX, tMaxY, tMaxZ;
    float tDeltaX, tDeltaY, tDeltaZ;

    initAxis(origin.x, dir.x, stepX, tMaxX, tDeltaX);
    initAxis(origin.y, dir.y, stepY, tMaxY, tDeltaY);
    initAxis(origin.z, dir.z, stepZ, tMaxZ, tDeltaZ);

    // Current grid cell (the cell containing the ray origin)
    Vec3i cell = origin.floor();

    // Face normal is updated as we step (stored from the previous step)
    Vec3i normal { 0, 0, 0 };
    float lastT = 0.f;   // t-value at which we entered the current cell

    // ── Traversal ─────────────────────────────────────────────────────────────

    for (int i = 0; i < MAX_STEPS; ++i)
    {
        // ── Hit test ──────────────────────────────────────────────────────────
        if (grid.contains(cell))
        {
            RaycastResult result;
            result.hit      = true;
            result.voxelPos = cell;
            result.normal   = normal;
            result.distance = lastT;
            return result;
        }

        // ── Distance guard ────────────────────────────────────────────────────
        float tMin = std::min({ tMaxX, tMaxY, tMaxZ });
        if (tMin > MAX_DIST)
            break;

        // ── Advance along the shortest axis ───────────────────────────────────
        if (tMaxX <= tMaxY && tMaxX <= tMaxZ)
        {
            lastT  = tMaxX;
            cell.x += stepX;
            normal = { -stepX, 0, 0 };
            tMaxX += tDeltaX;
        }
        else if (tMaxY <= tMaxZ)
        {
            lastT  = tMaxY;
            cell.y += stepY;
            normal = { 0, -stepY, 0 };
            tMaxY += tDeltaY;
        }
        else
        {
            lastT  = tMaxZ;
            cell.z += stepZ;
            normal = { 0, 0, -stepZ };
            tMaxZ += tDeltaZ;
        }
    }

    return {};   // No hit within MAX_STEPS / MAX_DIST
}
