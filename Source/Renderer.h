#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Renderer.h  –  OpenGL 3.3 batch renderer.
//
// Responsibilities:
//   • Compile and own all GLSL shaders.
//   • Maintain VAO/VBO objects for:
//       – voxels    (batched solid geometry, rebuilt on demand)
//       – ref grid  (static line mesh at y = 0)
//       – highlight (wireframe cube, rendered at arbitrary positions)
//   • Expose high-level draw calls that accept precomputed matrices.
//
// All GL calls are confined to init(), shutdown(), rebuildVoxelMesh(), and the
// render*() methods, which must be called from the OpenGL thread (inside
// OpenGLRenderer callbacks).
// ─────────────────────────────────────────────────────────────────────────────

#include "MathUtils.h"
#include "VoxelGrid.h"

// Forward declare GL types so the header stays independent of gl.h
using GLuint = unsigned int;
using GLint  = int;

class Renderer
{
public:
    Renderer()  = default;
    ~Renderer() = default;

    // ── Lifecycle (GL thread only) ────────────────────────────────────────────

    /// Create shaders, VAOs and VBOs; build the static grid mesh.
    void init();

    /// Release all OpenGL resources.
    void shutdown();

    // ── Mesh updates ─────────────────────────────────────────────────────────

    /// Rebuild the voxel VBO from scratch from the current VoxelGrid contents.
    /// Should be called when meshDirty is true, before rendering.
    void rebuildVoxelMesh(const VoxelGrid& grid);

    /// Set to true whenever the voxel set changes.
    /// Cleared automatically by rebuildVoxelMesh().
    bool meshDirty = true;

    // ── Render calls (GL thread only) ────────────────────────────────────────

    /// Draw all solid voxels with simple diffuse shading (single color batch).
    /// vp       : combined view × projection matrix
    /// lightDir : normalised world-space direction toward the light
    void render(const Mat4& vp, const Vec3f& lightDir);

    /// Draw a single solid cube at `pos` with the given base color.
    /// Used for per-block colored rendering.
    void renderSolidBlock(const Mat4& vp, const Vec3f& lightDir,
                          const Vec3i& pos, const Vec3f& color);

    /// Draw the horizontal XZ reference grid at y = 0 (the "floor").
    /// Kept for back-compat — equivalent to renderPlaneXZ.
    void renderGrid(const Mat4& vp);

    /// Draw the horizontal floor plane (lines along X and Z at y = 0).
    void renderPlaneXZ(const Mat4& vp);

    /// Draw the vertical "wall Z" plane (lines along X and Y at z = 0).
    void renderPlaneXY(const Mat4& vp);

    /// Draw the vertical "wall X" plane (lines along Y and Z at x = 0).
    void renderPlaneYZ(const Mat4& vp);

    /// Draw a wireframe cube at `pos` (integer grid coordinates).
    /// color : {r,g,b} in [0,1]
    void renderHighlight(const Mat4& vp, const Vec3i& pos,
                         const Vec3f& color);

    /// Draw the permanent red origin marker at (0,0,0).
    void renderOriginMarker(const Mat4& vp, const Vec3f& lightDir);

    /// Draw a Blender-style move arrow originating at @p origin (world space)
    /// pointing along @p axis (0=X, 1=Y, 2=Z) with the given length.
    /// `highlighted` brightens the color so the user knows it's hoverable.
    void renderArrow(const Mat4& vp, const Vec3f& origin,
                     int axis, float length,
                     const Vec3f& color, bool highlighted);

private:
    // ── VAO / VBO handles ─────────────────────────────────────────────────────

    GLuint vaoVoxels  = 0, vboVoxels  = 0;
    GLuint vaoPlaneXZ = 0, vboPlaneXZ = 0;
    GLuint vaoPlaneXY = 0, vboPlaneXY = 0;
    GLuint vaoPlaneYZ = 0, vboPlaneYZ = 0;
    GLuint vaoWire    = 0, vboWire    = 0;
    GLuint vaoCube    = 0, vboCube    = 0;   // origin marker

    // One arrow mesh per axis (CPU-rotated at build time so we don't need a
    // model matrix in the shader).  Each mesh is a thin cylinder shaft + a
    // cone tip, drawn as triangles.
    GLuint vaoArrow[3] {};
    GLuint vboArrow[3] {};
    int    arrowVertCount[3] {};

    int voxelVertCount   = 0;
    int planeXZVertCount = 0;
    int planeXYVertCount = 0;
    int planeYZVertCount = 0;
    int wireVertCount    = 0;
    int cubeVertCount    = 0;

    // ── Shader programs ───────────────────────────────────────────────────────

    GLuint progVoxels    = 0;
    GLuint progUnlit     = 0;   // used for grid and highlight

    // Uniform locations – voxel shader
    GLint uVP_vox         = -1;
    GLint uLight          = -1;
    GLint uColor_v        = -1;
    GLint uModelOffset_vox = -1;

    // Uniform locations – unlit shader
    GLint uVP_unlit    = -1;
    GLint uColor_unlit = -1;
    GLint uOffset      = -1;   // per-draw world translation

    // ── Internal helpers ──────────────────────────────────────────────────────

    void buildGridMesh(int halfSize = 40);   ///< Legacy single-plane builder
    void buildPlaneMeshes(int halfSize = 40);
    void buildArrowMeshes();
    void buildWireframeCube();
    void buildOriginCube();

    static GLuint compileShader(unsigned int type, const char* src);
    static GLuint linkProgram  (GLuint vert, GLuint frag);
};
