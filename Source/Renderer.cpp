// ─────────────────────────────────────────────────────────────────────────────
// Renderer.cpp  –  OpenGL 3.3 batch renderer implementation.
// ─────────────────────────────────────────────────────────────────────────────

#include "Renderer.h"

#include <juce_core/juce_core.h>
#include <juce_opengl/juce_opengl.h>
using namespace juce::gl;

#include <vector>
#include <cstring>
#include <stdexcept>
#include <array>
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// Embedded GLSL shaders  (GLSL 330 core, compatible with GL 3.3+)
// ─────────────────────────────────────────────────────────────────────────────

static const char* kVoxelVert = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;

uniform mat4 uVP;
uniform vec3 uModelOffset;

out vec3 vNormal;
out vec3 vWorldPos;

void main()
{
    vec3 worldPos = aPosition + uModelOffset;
    vNormal   = aNormal;
    vWorldPos = worldPos;
    gl_Position = uVP * vec4(worldPos, 1.0);
}
)glsl";

static const char* kVoxelFrag = R"glsl(
#version 330 core

in  vec3 vNormal;
in  vec3 vWorldPos;
out vec4 fragColor;

uniform vec3 uLightDir;  // normalised, toward the light
uniform vec3 uBaseColor;

void main()
{
    vec3 n = normalize(vNormal);

    // Simple hemisphere lighting: ambient + diffuse
    float diffuse = max(dot(n, normalize(uLightDir)), 0.0);
    float ambient = 0.30;
    float light   = ambient + (1.0 - ambient) * diffuse;

    // Subtle face-tinting to reinforce 3-D shape even without shadows
    float tint = 0.0;
    if (n.y >  0.5) tint =  0.12;   // top  face  – slightly brighter
    if (n.y < -0.5) tint = -0.08;   // bottom face – slightly darker

    vec3 color = clamp(uBaseColor + tint, 0.0, 1.0) * light;
    fragColor = vec4(color, 1.0);
}
)glsl";

// ── Unlit shader (grid lines and highlight wireframe) ─────────────────────────
static const char* kUnlitVert = R"glsl(
#version 330 core

layout(location = 0) in vec3 aPosition;

uniform mat4 uVP;
uniform vec3 uOffset;   // world-space translation per draw call

void main()
{
    gl_Position = uVP * vec4(aPosition + uOffset, 1.0);
}
)glsl";

static const char* kUnlitFrag = R"glsl(
#version 330 core

out vec4 fragColor;
uniform vec3 uColor;

void main()
{
    fragColor = vec4(uColor, 1.0);
}
)glsl";

// ─────────────────────────────────────────────────────────────────────────────
// Cube face geometry (for batch voxel mesh)
// Each face defined by 4 vertices in CCW order (viewed from outside).
// Vertex layout: px py pz  nx ny nz
// ─────────────────────────────────────────────────────────────────────────────

struct FaceVert { float px,py,pz, nx,ny,nz; };

static const FaceVert kFaces[6][4] =
{
    // FRONT  (z = 1)  normal  (0, 0, +1)
    { {0,0,1, 0,0,1}, {1,0,1, 0,0,1}, {1,1,1, 0,0,1}, {0,1,1, 0,0,1} },
    // BACK   (z = 0)  normal  (0, 0, -1)
    { {1,0,0, 0,0,-1}, {0,0,0, 0,0,-1}, {0,1,0, 0,0,-1}, {1,1,0, 0,0,-1} },
    // LEFT   (x = 0)  normal  (-1, 0, 0)
    { {0,0,0, -1,0,0}, {0,0,1, -1,0,0}, {0,1,1, -1,0,0}, {0,1,0, -1,0,0} },
    // RIGHT  (x = 1)  normal  (+1, 0, 0)
    { {1,0,1, 1,0,0}, {1,0,0, 1,0,0}, {1,1,0, 1,0,0}, {1,1,1, 1,0,0} },
    // BOTTOM (y = 0)  normal  (0, -1, 0)
    { {0,0,0, 0,-1,0}, {1,0,0, 0,-1,0}, {1,0,1, 0,-1,0}, {0,0,1, 0,-1,0} },
    // TOP    (y = 1)  normal  (0, +1, 0)
    { {0,1,1, 0,1,0}, {1,1,1, 0,1,0}, {1,1,0, 0,1,0}, {0,1,0, 0,1,0} },
};

// Quad → two triangles (CCW): indices 0,1,2 and 0,2,3
static const int kQuadIdx[6] = { 0, 1, 2,  0, 2, 3 };

// ─────────────────────────────────────────────────────────────────────────────
// 12 edges of a unit wireframe cube  (24 endpoint positions, 3 floats each)
// ─────────────────────────────────────────────────────────────────────────────

static const float kWireVerts[24 * 3] =
{
    // Bottom face
    0,0,0, 1,0,0,   1,0,0, 1,0,1,   1,0,1, 0,0,1,   0,0,1, 0,0,0,
    // Top face
    0,1,0, 1,1,0,   1,1,0, 1,1,1,   1,1,1, 0,1,1,   0,1,1, 0,1,0,
    // Verticals
    0,0,0, 0,1,0,   1,0,0, 1,1,0,   1,0,1, 1,1,1,   0,0,1, 0,1,1,
};

// ─────────────────────────────────────────────────────────────────────────────
// Shader helpers
// ─────────────────────────────────────────────────────────────────────────────

GLuint Renderer::compileShader(unsigned int type, const char* src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);

    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);

    if (!ok)
    {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        // Silently delete; caller gets 0 from linkProgram
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint Renderer::linkProgram(GLuint vert, GLuint frag)
{
    if (!vert || !frag) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vert);
    glAttachShader(prog, frag);
    glLinkProgram(prog);

    glDeleteShader(vert);
    glDeleteShader(frag);

    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// ─────────────────────────────────────────────────────────────────────────────
// init / shutdown
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::init()
{
    // ── Compile shaders ───────────────────────────────────────────────────────

    progVoxels = linkProgram(
        compileShader(GL_VERTEX_SHADER,   kVoxelVert),
        compileShader(GL_FRAGMENT_SHADER, kVoxelFrag));

    progUnlit = linkProgram(
        compileShader(GL_VERTEX_SHADER,   kUnlitVert),
        compileShader(GL_FRAGMENT_SHADER, kUnlitFrag));

    if (progVoxels)
    {
        uVP_vox         = glGetUniformLocation(progVoxels, "uVP");
        uLight          = glGetUniformLocation(progVoxels, "uLightDir");
        uColor_v        = glGetUniformLocation(progVoxels, "uBaseColor");
        uModelOffset_vox = glGetUniformLocation(progVoxels, "uModelOffset");
    }

    if (progUnlit)
    {
        uVP_unlit    = glGetUniformLocation(progUnlit, "uVP");
        uColor_unlit = glGetUniformLocation(progUnlit, "uColor");
        uOffset      = glGetUniformLocation(progUnlit, "uOffset");
    }

    // ── Allocate VAO/VBO for voxels (dynamic, rebuilt on demand) ─────────────

    glGenVertexArrays(1, &vaoVoxels);
    glGenBuffers     (1, &vboVoxels);

    glBindVertexArray(vaoVoxels);
    glBindBuffer(GL_ARRAY_BUFFER, vboVoxels);
    // Vertex layout: position (3f) + normal (3f) = 24 bytes stride
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // ── Build static meshes ───────────────────────────────────────────────────

    buildPlaneMeshes(40);
    buildArrowMeshes();
    buildWireframeCube();
    buildOriginCube();
}

void Renderer::shutdown()
{
    auto del = [](GLuint& h) { if (h) { glDeleteBuffers(1, &h); h = 0; } };
    auto delV = [](GLuint& h) { if (h) { glDeleteVertexArrays(1, &h); h = 0; } };
    auto delP = [](GLuint& h) { if (h) { glDeleteProgram(h); h = 0; } };

    delV(vaoVoxels);   del(vboVoxels);
    delV(vaoPlaneXZ);  del(vboPlaneXZ);
    delV(vaoPlaneXY);  del(vboPlaneXY);
    delV(vaoPlaneYZ);  del(vboPlaneYZ);
    delV(vaoWire);     del(vboWire);
    delV(vaoCube);     del(vboCube);
    for (int i = 0; i < 3; ++i) { delV(vaoArrow[i]); del(vboArrow[i]); }
    delP(progVoxels);
    delP(progUnlit);
}

// ─────────────────────────────────────────────────────────────────────────────
// buildGridMesh  –  horizontal line grid at y = 0
// ─────────────────────────────────────────────────────────────────────────────

// Legacy single-plane builder.  Kept as a thin wrapper so existing callers
// (renderGrid) still resolve; it just builds the XZ floor mesh.
void Renderer::buildGridMesh(int halfSize)
{
    juce::ignoreUnused(halfSize);
    // No-op: superseded by buildPlaneMeshes().
}

// ─────────────────────────────────────────────────────────────────────────────
// buildPlaneMeshes  –  three line grids, one per axis-aligned plane
// ─────────────────────────────────────────────────────────────────────────────

namespace
{
    // Upload @p verts (xyz triples) as a static line VBO.  Returns vert count.
    int uploadLineMesh(GLuint& vao, GLuint& vbo,
                       const std::vector<float>& verts)
    {
        glGenVertexArrays(1, &vao);
        glGenBuffers     (1, &vbo);

        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                     verts.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);

        return static_cast<int>(verts.size() / 3);
    }
}

void Renderer::buildPlaneMeshes(int halfSize)
{
    const float fH = static_cast<float>(halfSize);

    // ── XZ floor (y = 0) ─────────────────────────────────────────────────────
    {
        std::vector<float> verts;
        verts.reserve((halfSize * 2 + 1) * 4 * 3);

        for (int i = -halfSize; i <= halfSize; ++i)
        {
            const float fi = static_cast<float>(i);
            verts.insert(verts.end(), { -fH, 0.f, fi });
            verts.insert(verts.end(), {  fH, 0.f, fi });
            verts.insert(verts.end(), { fi, 0.f, -fH });
            verts.insert(verts.end(), { fi, 0.f,  fH });
        }
        planeXZVertCount = uploadLineMesh(vaoPlaneXZ, vboPlaneXZ, verts);
    }

    // ── XY wall (z = 0, vertical) ────────────────────────────────────────────
    // Y starts at 0 (the floor) and goes UP only — no negative Y lines.
    {
        std::vector<float> verts;
        verts.reserve((halfSize + 1) * 4 * 3);

        for (int i = -halfSize; i <= halfSize; ++i)
        {
            const float fi = static_cast<float>(i);
            // X-parallel (horizontal) lines: only y >= 0
            verts.insert(verts.end(), { -fH, fi, 0.f });
            verts.insert(verts.end(), {  fH, fi, 0.f });
            // Y-parallel (vertical) lines
            verts.insert(verts.end(), { fi, 0.f, 0.f });
            verts.insert(verts.end(), { fi, fH,  0.f });
        }
        planeXYVertCount = uploadLineMesh(vaoPlaneXY, vboPlaneXY, verts);
    }

    // ── YZ wall (x = 0, vertical) ────────────────────────────────────────────
    {
        std::vector<float> verts;
        verts.reserve((halfSize + 1) * 4 * 3);

        for (int i = -halfSize; i <= halfSize; ++i)
        {
            const float fi = static_cast<float>(i);
            // Z-parallel (horizontal) lines
            verts.insert(verts.end(), { 0.f, fi, -fH });
            verts.insert(verts.end(), { 0.f, fi,  fH });
            // Y-parallel (vertical) lines
            verts.insert(verts.end(), { 0.f, 0.f, fi });
            verts.insert(verts.end(), { 0.f, fH,  fi });
        }
        planeYZVertCount = uploadLineMesh(vaoPlaneYZ, vboPlaneYZ, verts);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// buildArrowMeshes  –  one solid arrow (shaft + cone tip) per axis
// ─────────────────────────────────────────────────────────────────────────────
//
// Geometry is generated CPU-side so we can keep the cheap unlit shader (no
// model matrix uniform needed).  Each axis gets its own VBO rotated so that
// the shaft points along the desired axis, starting at the local origin.
//
// Total triangles per arrow ≈ 8 shaft sides * 2 + 8 cone base + 1 cap = ~25.
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::buildArrowMeshes()
{
    constexpr int   kSegments  = 10;
    constexpr float kLength    = 1.0f;   // total length along the axis
    constexpr float kShaftLen  = 0.70f;
    constexpr float kShaftRad  = 0.045f;
    constexpr float kHeadRad   = 0.13f;

    // First build the arrow oriented along +X (shaft from (0,0,0) to (kShaftLen,0,0)
    // and a cone tip ending at (kLength,0,0)).  Then we'll rotate.

    std::vector<float> base;   // flat list of xyz triples (triangles)
    base.reserve(kSegments * 6 * 3);

    auto pushTri = [&](float ax, float ay, float az,
                       float bx, float by, float bz,
                       float cx, float cy, float cz)
    {
        base.push_back(ax); base.push_back(ay); base.push_back(az);
        base.push_back(bx); base.push_back(by); base.push_back(bz);
        base.push_back(cx); base.push_back(cy); base.push_back(cz);
    };

    // Shaft: cylinder side as a triangle strip unrolled into pairs of triangles.
    // Local +X is the axial direction, Y and Z are the radial.
    for (int i = 0; i < kSegments; ++i)
    {
        const float a0 = juce::MathConstants<float>::twoPi * (float) i       / kSegments;
        const float a1 = juce::MathConstants<float>::twoPi * (float)(i + 1)  / kSegments;
        const float y0 = std::cos(a0) * kShaftRad, z0 = std::sin(a0) * kShaftRad;
        const float y1 = std::cos(a1) * kShaftRad, z1 = std::sin(a1) * kShaftRad;

        pushTri(0.f, y0, z0,  kShaftLen, y0, z0,  kShaftLen, y1, z1);
        pushTri(0.f, y0, z0,  kShaftLen, y1, z1,  0.f,        y1, z1);
    }

    // Cone tip: base circle at x=kShaftLen radius=kHeadRad, apex at x=kLength.
    for (int i = 0; i < kSegments; ++i)
    {
        const float a0 = juce::MathConstants<float>::twoPi * (float) i       / kSegments;
        const float a1 = juce::MathConstants<float>::twoPi * (float)(i + 1)  / kSegments;
        const float y0 = std::cos(a0) * kHeadRad, z0 = std::sin(a0) * kHeadRad;
        const float y1 = std::cos(a1) * kHeadRad, z1 = std::sin(a1) * kHeadRad;

        // Side
        pushTri(kShaftLen, y0, z0,  kLength, 0.f, 0.f,  kShaftLen, y1, z1);
        // Base cap (so the cone looks closed when viewed from below)
        pushTri(kShaftLen, 0.f, 0.f,  kShaftLen, y1, z1,  kShaftLen, y0, z0);
    }

    // Rotate the +X arrow into +Y and +Z.  Each axis gets a fresh vertex list.
    auto rotate = [&](int axis, std::vector<float>& out)
    {
        out.resize(base.size());
        for (size_t i = 0; i + 2 < base.size(); i += 3)
        {
            const float x = base[i + 0], y = base[i + 1], z = base[i + 2];
            switch (axis)
            {
                case 0:  // +X (identity)
                    out[i] = x; out[i + 1] = y; out[i + 2] = z;
                    break;
                case 1:  // +Y : (x, y, z) -> (-y, x, z)  rotates +X to +Y around +Z
                    out[i] = -y; out[i + 1] = x; out[i + 2] = z;
                    break;
                case 2:  // +Z : (x, y, z) -> (z, y, -x)  rotates +X to +Z around +Y
                    out[i] = z; out[i + 1] = y; out[i + 2] = -x;
                    break;
            }
        }
    };

    for (int axis = 0; axis < 3; ++axis)
    {
        std::vector<float> v;
        rotate(axis, v);

        glGenVertexArrays(1, &vaoArrow[axis]);
        glGenBuffers     (1, &vboArrow[axis]);

        glBindVertexArray(vaoArrow[axis]);
        glBindBuffer(GL_ARRAY_BUFFER, vboArrow[axis]);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(v.size() * sizeof(float)),
                     v.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);

        arrowVertCount[axis] = static_cast<int>(v.size() / 3);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// buildWireframeCube  –  12 edges of a unit cube at origin
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::buildWireframeCube()
{
    wireVertCount = 24;   // 12 edges × 2 endpoints

    glGenVertexArrays(1, &vaoWire);
    glGenBuffers     (1, &vboWire);

    glBindVertexArray(vaoWire);
    glBindBuffer(GL_ARRAY_BUFFER, vboWire);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(sizeof(kWireVerts)),
                 kWireVerts, GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuildVoxelMesh  –  collapse all voxels into a single VBO
//
// Vertex format : position (3f) + normal (3f) = 6 floats
// Per voxel     : 6 faces × 6 verts = 36 vertices × 6 floats = 216 floats
//
// Future optimisation: skip faces shared with adjacent voxels (face culling).
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::rebuildVoxelMesh(const VoxelGrid& grid)
{
    std::vector<float> verts;
    verts.reserve(grid.size() * 36 * 6);

    for (const Vec3i& vox : grid.getVoxels())
    {
        const float ox = static_cast<float>(vox.x);
        const float oy = static_cast<float>(vox.y);
        const float oz = static_cast<float>(vox.z);

        for (int face = 0; face < 6; ++face)
        {
            const FaceVert* fv = kFaces[face];
            for (int tri = 0; tri < 6; ++tri)
            {
                const FaceVert& v = fv[kQuadIdx[tri]];
                verts.push_back(v.px + ox);
                verts.push_back(v.py + oy);
                verts.push_back(v.pz + oz);
                verts.push_back(v.nx);
                verts.push_back(v.ny);
                verts.push_back(v.nz);
            }
        }
    }

    voxelVertCount = static_cast<int>(verts.size() / 6);

    glBindVertexArray(vaoVoxels);
    glBindBuffer(GL_ARRAY_BUFFER, vboVoxels);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.empty() ? nullptr : verts.data(),
                 GL_DYNAMIC_DRAW);

    // Re-assert attrib pointers after glBufferData (driver may re-map)
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    meshDirty = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// buildOriginCube  –  single solid cube at (0,0,0) for the origin marker
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::buildOriginCube()
{
    std::vector<float> verts;
    verts.reserve(36 * 6);

    for (int face = 0; face < 6; ++face)
    {
        const FaceVert* fv = kFaces[face];
        for (int tri = 0; tri < 6; ++tri)
        {
            const FaceVert& v = fv[kQuadIdx[tri]];
            verts.push_back(v.px);
            verts.push_back(v.py);
            verts.push_back(v.pz);
            verts.push_back(v.nx);
            verts.push_back(v.ny);
            verts.push_back(v.nz);
        }
    }

    cubeVertCount = static_cast<int>(verts.size() / 6);

    glGenVertexArrays(1, &vaoCube);
    glGenBuffers     (1, &vboCube);

    glBindVertexArray(vaoCube);
    glBindBuffer(GL_ARRAY_BUFFER, vboCube);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE,
                          6 * sizeof(float), (void*)(3 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderOriginMarker  –  permanent red cube at (0,0,0)
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::renderOriginMarker(const Mat4& vp, const Vec3f& lightDir)
{
    if (!progVoxels || cubeVertCount == 0) return;

    glUseProgram(progVoxels);
    glUniformMatrix4fv(uVP_vox, 1, GL_FALSE, vp.m);
    glUniform3f(uLight,   lightDir.x, lightDir.y, lightDir.z);
    glUniform3f(uColor_v, 1.00f, 0.55f, 0.10f);   // orange
    glUniform3f(uModelOffset_vox, 0.f, 0.f, 0.f);

    glBindVertexArray(vaoCube);
    glDrawArrays(GL_TRIANGLES, 0, cubeVertCount);
    glBindVertexArray(0);
}


// ─────────────────────────────────────────────────────────────────────────────

void Renderer::render(const Mat4& vp, const Vec3f& lightDir)
{
    if (!progVoxels || voxelVertCount == 0) return;

    glUseProgram(progVoxels);
    glUniformMatrix4fv(uVP_vox, 1, GL_FALSE, vp.m);
    glUniform3f(uLight,   lightDir.x, lightDir.y, lightDir.z);
    glUniform3f(uColor_v, 0.38f, 0.62f, 0.92f);
    glUniform3f(uModelOffset_vox, 0.f, 0.f, 0.f);

    glBindVertexArray(vaoVoxels);
    glDrawArrays(GL_TRIANGLES, 0, voxelVertCount);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderSolidBlock — one colored cube at an arbitrary grid position
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::renderSolidBlock(const Mat4& vp, const Vec3f& lightDir,
                                const Vec3i& pos, const Vec3f& color)
{
    if (!progVoxels || cubeVertCount == 0) return;

    glUseProgram(progVoxels);
    glUniformMatrix4fv(uVP_vox, 1, GL_FALSE, vp.m);
    glUniform3f(uLight,   lightDir.x, lightDir.y, lightDir.z);
    glUniform3f(uColor_v, color.x, color.y, color.z);
    glUniform3f(uModelOffset_vox,
                static_cast<float>(pos.x),
                static_cast<float>(pos.y),
                static_cast<float>(pos.z));

    glBindVertexArray(vaoCube);
    glDrawArrays(GL_TRIANGLES, 0, cubeVertCount);
    glBindVertexArray(0);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderGrid  –  horizontal reference grid at y = 0
// ─────────────────────────────────────────────────────────────────────────────

// Legacy single-plane entry point — equivalent to renderPlaneXZ().
void Renderer::renderGrid(const Mat4& vp)
{
    renderPlaneXZ(vp);
}

namespace
{
    inline void drawLinePlane(GLuint prog, GLint uVP, GLint uColor, GLint uOff,
                              const Mat4& vp, const Vec3f& color,
                              GLuint vao, int vertCount)
    {
        if (!prog || vertCount == 0) return;
        glUseProgram(prog);
        glUniformMatrix4fv(uVP, 1, GL_FALSE, vp.m);
        glUniform3f(uColor, color.x, color.y, color.z);
        glUniform3f(uOff, 0.f, 0.f, 0.f);
        glBindVertexArray(vao);
        glDrawArrays(GL_LINES, 0, vertCount);
        glBindVertexArray(0);
    }
}

void Renderer::renderPlaneXZ(const Mat4& vp)
{
    drawLinePlane(progUnlit, uVP_unlit, uColor_unlit, uOffset,
                  vp, { 0.30f, 0.30f, 0.35f },
                  vaoPlaneXZ, planeXZVertCount);
}

void Renderer::renderPlaneXY(const Mat4& vp)
{
    drawLinePlane(progUnlit, uVP_unlit, uColor_unlit, uOffset,
                  vp, { 0.30f, 0.20f, 0.35f },     // faint magenta tint (Z-facing)
                  vaoPlaneXY, planeXYVertCount);
}

void Renderer::renderPlaneYZ(const Mat4& vp)
{
    drawLinePlane(progUnlit, uVP_unlit, uColor_unlit, uOffset,
                  vp, { 0.35f, 0.20f, 0.20f },     // faint red tint (X-facing)
                  vaoPlaneYZ, planeYZVertCount);
}

void Renderer::renderArrow(const Mat4& vp, const Vec3f& origin,
                           int axis, float length,
                           const Vec3f& color, bool highlighted)
{
    if (!progUnlit) return;
    axis = juce::jlimit(0, 2, axis);
    if (arrowVertCount[axis] == 0) return;

    glUseProgram(progUnlit);
    glUniformMatrix4fv(uVP_unlit, 1, GL_FALSE, vp.m);

    // The base mesh is unit-length (1.0).  We don't have a model-matrix uniform,
    // so for a quick first cut we just translate to @p origin and accept the
    // unit length.  The caller picks a sensible @p length that's already baked
    // into the call site; if you need variable-length arrows, swap this for a
    // per-axis pre-scaled VBO or extend the shader.  For now we use length to
    // brighten/dim if needed.
    juce::ignoreUnused(length);

    const float boost = highlighted ? 0.35f : 0.0f;
    glUniform3f(uColor_unlit,
                juce::jmin(1.f, color.x + boost),
                juce::jmin(1.f, color.y + boost),
                juce::jmin(1.f, color.z + boost));
    glUniform3f(uOffset, origin.x, origin.y, origin.z);

    glDisable(GL_CULL_FACE);     // cone tip flips winding on far side
    glBindVertexArray(vaoArrow[axis]);
    glDrawArrays(GL_TRIANGLES, 0, arrowVertCount[axis]);
    glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}

// ─────────────────────────────────────────────────────────────────────────────
// renderHighlight  –  wireframe outline at a given grid position
// ─────────────────────────────────────────────────────────────────────────────

void Renderer::renderHighlight(const Mat4& vp, const Vec3i& pos,
                                const Vec3f& color)
{
    if (!progUnlit || wireVertCount == 0) return;

    glUseProgram(progUnlit);
    glUniformMatrix4fv(uVP_unlit, 1, GL_FALSE, vp.m);
    glUniform3f(uColor_unlit, color.x, color.y, color.z);
    glUniform3f(uOffset,
                static_cast<float>(pos.x),
                static_cast<float>(pos.y),
                static_cast<float>(pos.z));

    // Draw slightly larger to avoid Z-fighting with solid faces
    glLineWidth(1.5f);
    glBindVertexArray(vaoWire);
    glDrawArrays(GL_LINES, 0, wireVertCount);
    glBindVertexArray(0);
    glLineWidth(1.f);
}
