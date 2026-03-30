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

    /// Draw all solid voxels with simple diffuse shading.
    /// vp       : combined view × projection matrix
    /// lightDir : normalised world-space direction toward the light
    void render(const Mat4& vp, const Vec3f& lightDir);

    /// Draw the reference grid at y = 0.
    void renderGrid(const Mat4& vp);

    /// Draw a wireframe cube at `pos` (integer grid coordinates).
    /// color : {r,g,b} in [0,1]
    void renderHighlight(const Mat4& vp, const Vec3i& pos,
                         const Vec3f& color);

    /// Draw the permanent red origin marker at (0,0,0).
    void renderOriginMarker(const Mat4& vp, const Vec3f& lightDir);

private:
    // ── VAO / VBO handles ─────────────────────────────────────────────────────

    GLuint vaoVoxels = 0,    vboVoxels    = 0;
    GLuint vaoGrid   = 0,    vboGrid      = 0;
    GLuint vaoWire   = 0,    vboWire      = 0;
    GLuint vaoCube   = 0,    vboCube      = 0;   // origin marker

    int voxelVertCount = 0;
    int gridVertCount  = 0;
    int wireVertCount  = 0;
    int cubeVertCount  = 0;

    // ── Shader programs ───────────────────────────────────────────────────────

    GLuint progVoxels    = 0;
    GLuint progUnlit     = 0;   // used for grid and highlight

    // Uniform locations – voxel shader
    GLint uVP_vox  = -1;
    GLint uLight   = -1;
    GLint uColor_v = -1;

    // Uniform locations – unlit shader
    GLint uVP_unlit    = -1;
    GLint uColor_unlit = -1;
    GLint uOffset      = -1;   // per-draw world translation

    // ── Internal helpers ──────────────────────────────────────────────────────

    void buildGridMesh(int halfSize = 40);
    void buildWireframeCube();
    void buildOriginCube();

    static GLuint compileShader(unsigned int type, const char* src);
    static GLuint linkProgram  (GLuint vert, GLuint frag);
};
