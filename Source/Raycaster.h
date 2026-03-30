#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Raycaster.h  –  3-D ray casting and grid traversal.
//
// Two public concerns:
//
//  1. screenToRay()   – converts a 2-D screen pixel to a normalised world-space
//                       ray direction, using the current view / projection mats.
//
//  2. cast()          – walks the ray through the voxel grid with the Amanatides
//                       & Woo DDA algorithm, returning the first hit voxel and
//                       the outward face normal at the point of intersection.
//
// Placement helpers:
//  getPlacementPos()  – position adjacent to the hit face (for left-click add).
//  getAxisLockedPos() – axis-constrained version (Shift held).
// ─────────────────────────────────────────────────────────────────────────────

#include "MathUtils.h"
#include "VoxelGrid.h"

struct RaycastResult
{
    bool  hit      = false;
    Vec3i voxelPos;          ///< Grid position of the first voxel hit
    Vec3i normal;            ///< Outward face normal of the hit face (+/-X, Y or Z)
    float distance = 0.f;   ///< Approximate ray length to the hit face (world units)
};

class Raycaster
{
public:
    static constexpr int   MAX_STEPS = 150;
    static constexpr float MAX_DIST  = 120.f;

    // ── Primary API ───────────────────────────────────────────────────────────

    /// Cast a ray from `origin` along `dir` (must be normalised).
    /// Traverses the sparse voxel grid using DDA.
    static RaycastResult cast(const Vec3f&   origin,
                              const Vec3f&   dir,
                              const VoxelGrid& grid);

    /// Convert a screen-space pixel to a normalised world-space ray direction.
    /// mouseX, mouseY : pixel coordinates (top-left origin)
    /// screenW, screenH : viewport dimensions in pixels
    /// view, proj : current camera matrices
    static Vec3f screenToRay(float mouseX, float mouseY,
                             float screenW, float screenH,
                             const Mat4& view,
                             const Mat4& proj);

    // ── Placement helpers ─────────────────────────────────────────────────────

    /// Position directly adjacent to the hit face (for voxel placement).
    /// Returns a zero Vec3i if result.hit is false.
    static Vec3i getPlacementPos(const RaycastResult& result)
    {
        if (!result.hit) return {};
        return result.voxelPos + result.normal;
    }

    /// Axis-locked placement: constrains the candidate placement position so it
    /// only differs from `anchor` along the axis described by `axisNormal`.
    ///
    /// axisNormal : a unit-axis vector (e.g. the face normal of the last hit)
    /// anchor     : the reference voxel (typically the last placed voxel)
    /// candidate  : the unconstrained placement candidate
    static Vec3i getAxisLockedPos(const Vec3i& axisNormal,
                                  const Vec3i& anchor,
                                  const Vec3i& candidate)
    {
        int axis = axisNormal.dominantAxis();
        Vec3i result = anchor;
        if      (axis == 0) result.x = candidate.x;
        else if (axis == 1) result.y = candidate.y;
        else                result.z = candidate.z;
        return result;
    }

    /// Try to find an intersection with the y = 0 ground plane as a fallback
    /// when no voxel is hit.  Returns {0,0,0} (invalid) if the ray is parallel
    /// to or pointing away from the plane.
    static Vec3i groundPlaneHit(const Vec3f& origin, const Vec3f& dir)
    {
        if (dir.y >= -1e-4f) return {};            // ray not going downward
        float t = -origin.y / dir.y;
        if (t <= 0.f || t > MAX_DIST) return {};
        Vec3f pt = origin + dir * t;
        return pt.floor();
    }
};
