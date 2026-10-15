#include "view/Renderer.h"
#include "core/Geometry.h"
#include "core/Tessellate.h"
#include "ui/Document.h"
#include <QOpenGLContext>
#include <functional>
#include <unordered_set>

namespace gable {

// ------------------------------------------------------------------ shaders

static const char* kFaceVS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aNrm;
layout(location=2) in vec4 aUV;
uniform mat4 uModel;
uniform mat4 uVP;
uniform mat3 uNrmMat;
uniform mat4 uLightVP;
uniform vec4 uClip;
uniform bool uUseClip;
out vec3 vN;
out vec4 vUV;
out vec3 vW;
out vec4 vL;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    vW = w.xyz;
    vN = uNrmMat * aNrm;
    vUV = aUV;
    vL = uLightVP * w;
    gl_ClipDistance[0] = uUseClip ? dot(uClip, w) : 1.0;
    gl_Position = uVP * w;
}
)";

static const char* kFaceFS = R"(
#version 330 core
in vec3 vN;
in vec4 vUV;
in vec3 vW;
in vec4 vL;
uniform vec4 uFrontColor;
uniform vec4 uBackColor;
uniform bool uFrontTex;
uniform bool uBackTex;
uniform vec2 uFrontTexSize;
uniform vec2 uBackTexSize;
uniform sampler2D uTexF;
uniform sampler2D uTexB;
uniform vec3 uLightDir;
uniform float uLight;
uniform float uDark;
uniform bool uUnlit;
uniform float uFade;
uniform bool uShadows;
uniform sampler2DShadow uShadowMap;
uniform bool uShadowOnly;
uniform bool uFog;
uniform vec2 uFogRange;
uniform vec4 uFogColor;
uniform vec3 uEye;
uniform float uAlphaMul;
uniform bool uColorize;
out vec4 oColor;

float shadowFactor(vec3 n) {
    vec3 p = vL.xyz / vL.w * 0.5 + 0.5;
    if (p.x < 0.0 || p.x > 1.0 || p.y < 0.0 || p.y > 1.0 || p.z > 1.0) return 1.0;
    float ndl = max(dot(n, uLightDir), 0.0);
    float bias = mix(0.0015, 0.0002, ndl);
    float s = 0.0;
    vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
    for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
            s += texture(uShadowMap, vec3(p.xy + vec2(x, y) * texel, p.z - bias));
    return s / 9.0;
}

void main() {
    bool front = gl_FrontFacing;
    vec3 n = normalize(vN);
    if (!front) n = -n;
    vec4 base;
    if (front) base = uFrontTex ? texture(uTexF, vUV.xy / uFrontTexSize) * vec4(1.0, 1.0, 1.0, uFrontColor.a) : uFrontColor;
    else base = uBackTex ? texture(uTexB, vUV.zw / uBackTexSize) * vec4(1.0, 1.0, 1.0, uBackColor.a) : uBackColor;
    if (uColorize && front && uFrontTex) base.rgb = base.rgb * uFrontColor.rgb * 2.0;
    float ndl = dot(n, uLightDir);
    float s = 1.0;
    if (uShadows) {
        s = shadowFactor(n);
        if (ndl <= 0.0) s = 0.0;
    }
    if (uShadowOnly) {
        oColor = vec4(0.0, 0.0, 0.0, (1.0 - s) * (1.0 - uDark) * 0.6);
        if (oColor.a < 0.01) discard;
        return;
    }
    float lit = uUnlit ? 1.0 : uDark + (uLight - uDark) * max(ndl, 0.0) * s;
    vec3 c = base.rgb * lit;
    if (uFog) {
        float d = length(vW - uEye);
        float f = clamp((d - uFogRange.x) / max(uFogRange.y - uFogRange.x, 1.0), 0.0, 1.0);
        c = mix(c, uFogColor.rgb, f);
    }
    c = mix(c, vec3(1.0), uFade);
    oColor = vec4(c, base.a * uAlphaMul);
}
)";

static const char* kStippleFS = R"(
#version 330 core
uniform vec4 uColor;
uniform int uPattern;
out vec4 oColor;
void main() {
    ivec2 c = ivec2(gl_FragCoord.xy);
    if (uPattern == 0) { if ((c.x % 3) != 0 || (c.y % 3) != 0) discard; }
    else { if (((c.x + c.y) % 4) != 0) discard; }
    oColor = uColor;
}
)";

static const char* kDepthFS = R"(
#version 330 core
void main() {}
)";

static const char* kLineVS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
layout(location=1) in vec4 aCol;
layout(location=2) in vec3 aN1;
layout(location=3) in vec3 aN2;
layout(location=4) in float aFlags;
uniform mat4 uModel;
uniform mat4 uVP;
uniform mat3 uNrmMat;
uniform vec4 uClip;
uniform bool uUseClip;
out VS { vec4 col; vec3 n1; vec3 n2; float flags; vec3 w; float clip; } o;
void main() {
    vec4 w = uModel * vec4(aPos, 1.0);
    o.w = w.xyz;
    o.col = aCol;
    o.n1 = length(aN1) > 0.0 ? normalize(uNrmMat * aN1) : vec3(0.0);
    o.n2 = length(aN2) > 0.0 ? normalize(uNrmMat * aN2) : vec3(0.0);
    o.flags = aFlags;
    o.clip = uUseClip ? dot(uClip, w) : 1.0;
    gl_Position = uVP * w;
}
)";

static const char* kLineGS = R"(
#version 330 core
layout(lines) in;
layout(triangle_strip, max_vertices = 4) out;
in VS { vec4 col; vec3 n1; vec3 n2; float flags; vec3 w; float clip; } gi[];
uniform vec2 uViewport;
uniform float uWidth;
uniform int uMode;
uniform vec3 uEye;
uniform bool uOrtho;
uniform vec3 uViewDir;
uniform float uExt;
uniform int uColorMode;
uniform vec4 uColor;
uniform float uFade;
uniform float uDepthBias;
uniform float uNear;
out vec4 fCol;
noperspective out float fDist;
void main() {
    int f = int(gi[0].flags + 0.5);
    bool soft = (f & 4) != 0;
    if (uMode == 0 && soft) return;
    if (uMode == 2 && !soft) return;
    if (uMode == 1) {
        if (soft) return;
        bool prof = (f & 1) != 0;
        if ((f & 2) != 0) {
            vec3 mid = (gi[0].w + gi[1].w) * 0.5;
            vec3 V = uOrtho ? -uViewDir : (uEye - mid);
            float a = dot(gi[0].n1, V), b = dot(gi[0].n2, V);
            prof = (a > 0.0) != (b > 0.0);
        }
        if (!prof) return;
    }
    vec4 p0 = gl_in[0].gl_Position, p1 = gl_in[1].gl_Position;
    float c0 = gi[0].clip, c1 = gi[1].clip;
    float e = max(uNear * 1.001, 1e-4);
    if (p0.w < e && p1.w < e) return;
    if (p0.w < e) { float t = (e - p0.w) / (p1.w - p0.w); p0 = mix(p0, p1, t); c0 = mix(c0, c1, t); }
    else if (p1.w < e) { float t = (e - p1.w) / (p0.w - p1.w); p1 = mix(p1, p0, t); c1 = mix(c1, c0, t); }
    vec2 s0 = p0.xy / p0.w * uViewport * 0.5;
    vec2 s1 = p1.xy / p1.w * uViewport * 0.5;
    vec2 d = s1 - s0;
    float len = length(d);
    if (len < 1e-6) { d = vec2(1.0, 0.0); } else { d /= len; }
    vec2 nrm = vec2(-d.y, d.x) * (uWidth * 0.5);
    vec2 ext = d * uExt;
    vec4 col;
    if (uColorMode == 1) col = gi[0].col;
    else if (uColorMode == 2) {
        vec3 dir = normalize(gi[1].w - gi[0].w);
        if (abs(dir.x) > 0.9999) col = vec4(0.85, 0.1, 0.1, 1.0);
        else if (abs(dir.y) > 0.9999) col = vec4(0.1, 0.65, 0.1, 1.0);
        else if (abs(dir.z) > 0.9999) col = vec4(0.1, 0.2, 0.9, 1.0);
        else col = uColor;
    } else col = uColor;
    col.rgb = mix(col.rgb, vec3(1.0), uFade);
    vec2 halfVp = uViewport * 0.5;
    vec2 corners[4] = vec2[4](s0 - ext + nrm, s0 - ext - nrm, s1 + ext + nrm, s1 + ext - nrm);
    float dists[4] = float[4](-uExt, -uExt, len + uExt, len + uExt);
    for (int i = 0; i < 4; ++i) {
        vec4 p = (i < 2) ? p0 : p1;
        float zb = p.z - uDepthBias * p.w;
        gl_Position = vec4(corners[i] / halfVp * p.w, zb, p.w);
        gl_ClipDistance[0] = (i < 2) ? c0 : c1;
        fCol = col;
        fDist = dists[i];
        EmitVertex();
    }
    EndPrimitive();
}
)";

static const char* kLineFS = R"(
#version 330 core
in vec4 fCol;
noperspective in float fDist;
uniform int uDash;
uniform float uDashScale;
out vec4 oColor;
void main() {
    float d = fDist / uDashScale;
    if (uDash == 1) { if (mod(d, 12.0) > 7.0) discard; }
    else if (uDash == 2) { if (mod(d, 5.0) > 2.0) discard; }
    else if (uDash == 3) { float m = mod(d, 20.0); if ((m > 9.0 && m < 12.0) || m > 14.0) discard; }
    oColor = fCol;
}
)";

static const char* kBgVS = R"(
#version 330 core
out vec2 vNdc;
void main() {
    vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0);
    vNdc = p;
    gl_Position = vec4(p, 0.999999, 1.0);
}
)";

static const char* kBgFS = R"(
#version 330 core
in vec2 vNdc;
uniform mat4 uInvVP;
uniform vec4 uBg;
uniform vec4 uSky;
uniform vec4 uGround;
uniform bool uShowSky;
uniform bool uShowGround;
uniform float uGroundAlpha;
out vec4 oColor;
void main() {
    vec4 a = uInvVP * vec4(vNdc, -1.0, 1.0);
    vec4 b = uInvVP * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(b.xyz / b.w - a.xyz / a.w);
    vec3 c = uBg.rgb;
    if (dir.z >= 0.0) {
        if (uShowSky) c = mix(uBg.rgb, uSky.rgb, smoothstep(0.0, 0.3, dir.z));
    } else if (uShowGround) {
        c = mix(uBg.rgb, uGround.rgb, (1.0 - uGroundAlpha) * smoothstep(0.0, 0.05, -dir.z));
    }
    oColor = vec4(c, 1.0);
}
)";

static const char* kFlatVS = R"(
#version 330 core
layout(location=0) in vec3 aPos;
uniform mat4 uVP;
uniform vec4 uClip;
uniform bool uUseClip;
void main() {
    gl_ClipDistance[0] = uUseClip ? dot(uClip, vec4(aPos, 1.0)) : 1.0;
    gl_Position = uVP * vec4(aPos, 1.0);
}
)";

static const char* kFlatFS = R"(
#version 330 core
uniform vec4 uColor;
uniform int uPattern;
out vec4 oColor;
void main() {
    if (uPattern == 1) {
        ivec2 c = ivec2(gl_FragCoord.xy);
        if ((c.x % 3) != 0 || (c.y % 3) != 0) discard;
    }
    oColor = uColor;
}
)";

static void toGL(const Mat4& m, float* out) { m.toFloat(out); }
static void nrm3(const Mat4& m, float* out) {
    Mat4 inv = m.inverted();
    // inverse transpose of upper 3x3, column-major
    for (int c = 0; c < 3; ++c)
        for (int r = 0; r < 3; ++r) out[c * 3 + r] = (float)inv.at(c, r);
}
static QVector4D qc(const QColor& c, double a = -1) {
    return QVector4D(c.redF(), c.greenF(), c.blueF(), a < 0 ? c.alphaF() : a);
}

Renderer::Renderer() {}
Renderer::~Renderer() {}

bool Renderer::init(QString* err) {
    initializeOpenGLFunctions();
    auto build = [&](QOpenGLShaderProgram& p, const char* vs, const char* gs, const char* fs) {
        if (!p.addShaderFromSourceCode(QOpenGLShader::Vertex, vs)) { if (err) *err = p.log(); return false; }
        if (gs && !p.addShaderFromSourceCode(QOpenGLShader::Geometry, gs)) { if (err) *err = p.log(); return false; }
        if (!p.addShaderFromSourceCode(QOpenGLShader::Fragment, fs)) { if (err) *err = p.log(); return false; }
        if (!p.link()) { if (err) *err = p.log(); return false; }
        return true;
    };
    if (!build(faceProg_, kFaceVS, nullptr, kFaceFS)) return false;
    if (!build(stippleProg_, kFaceVS, nullptr, kStippleFS)) return false;
    if (!build(depthProg_, kFaceVS, nullptr, kDepthFS)) return false;
    if (!build(lineProg_, kLineVS, kLineGS, kLineFS)) return false;
    if (!build(bgProg_, kBgVS, nullptr, kBgFS)) return false;
    if (!build(flatProg_, kFlatVS, nullptr, kFlatFS)) return false;

    glGenVertexArrays(1, &dynVao_);
    glGenBuffers(1, &dynVbo_);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    GLsizei st = sizeof(LV);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(LV, p));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, st, (void*)offsetof(LV, c));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(LV, n1));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, st, (void*)offsetof(LV, n2));
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, st, (void*)offsetof(LV, f));
    glGenVertexArrays(1, &triVao_);
    glGenBuffers(1, &triVbo_);
    glBindVertexArray(triVao_);
    glBindBuffer(GL_ARRAY_BUFFER, triVbo_);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 12, nullptr);
    glGenVertexArrays(1, &bgVao_);
    glBindVertexArray(0);

    // Shadow map
    glGenTextures(1, &shadowTex_);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, shadowSize_, shadowSize_, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTex_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    inited_ = true;
    return true;
}

void Renderer::cleanup() {
    if (!inited_) return;
    for (auto& [k, m] : meshes_) {
        glDeleteVertexArrays(1, &m.vaoF); glDeleteBuffers(1, &m.vboF);
        glDeleteVertexArrays(1, &m.vaoE); glDeleteBuffers(1, &m.vboE);
    }
    meshes_.clear();
    textures_.clear();
    glDeleteVertexArrays(1, &dynVao_); glDeleteBuffers(1, &dynVbo_);
    glDeleteVertexArrays(1, &triVao_); glDeleteBuffers(1, &triVbo_);
    glDeleteVertexArrays(1, &bgVao_);
    glDeleteFramebuffers(1, &shadowFbo_); glDeleteTextures(1, &shadowTex_);
    inited_ = false;
}

// ------------------------------------------------------------------ meshes

uint64_t Renderer::styleSig() const {
    uint64_t h = 1469598103934665603ULL;
    auto mix = [&](uint64_t v) { h ^= v; h *= 1099511628211ULL; };
    mix(D->model.style.showHidden);
    mix(D->model.style.colorByTag);
    for (auto& t : D->model.tags) { mix(t.visible); mix(t.color.rgba()); }
    return h;
}

void Renderer::buildMesh(Mesh& M, const Entities& E, uint64_t sig) {
    const Style& st = D->model.style;
    auto tagVis = [&](int t) { return D->tagVisible(t); };
    // ---- faces, batched by (front, back) material
    std::map<std::pair<int, int>, std::vector<int>> groups;
    for (int f = 0; f < (int)E.faces.size(); ++f) {
        const Face& F = E.faces[f];
        if (!F.alive || (F.hidden && !st.showHidden) || !tagVis(F.tag)) continue;
        groups[{F.matFront, F.matBack}].push_back(f);
    }
    std::vector<float> vb;
    M.batches.clear();
    M.faceRange.clear();
    std::vector<Vec2> l2;
    for (auto& [key, fl] : groups) {
        Batch b;
        b.start = (int)(vb.size() / 10);
        b.matF = key.first;
        b.matB = key.second;
        auto alpha = [&](int m) { return m >= 0 && m < (int)D->model.materials.size() ? D->model.materials[m].alpha : 1.0; };
        b.transparent = alpha(b.matF) < 0.999 || alpha(b.matB) < 0.999;
        for (int f : fl) {
            const Face& F = E.faces[f];
            int first = (int)(vb.size() / 10);
            Frame2 fr(F.plane);
            Plane back = F.plane;
            back.n = -back.n; back.d = -back.d;
            Frame2 frb(back);
            std::vector<std::vector<Vec2>> loops;
            std::vector<int> ids;
            auto addL = [&](const std::vector<int>& L) {
                std::vector<Vec2> q;
                for (int v : L) { q.push_back(fr.to2(E.verts[v].p)); ids.push_back(v); }
                loops.push_back(q);
            };
            addL(F.outer);
            for (auto& h : F.holes) addL(h);
            TessResult tr;
            if (!tessellate(loops, tr)) continue;
            // smooth normals per vertex
            std::unordered_map<int, Vec3> vn;
            auto vertexNormal = [&](int v) -> Vec3 {
                auto it = vn.find(v);
                if (it != vn.end()) return it->second;
                bool anySmooth = false;
                for (int e : E.verts[v].edges)
                    if (E.edges[e].alive && E.edges[e].smooth) { anySmooth = true; break; }
                Vec3 n = F.plane.n;
                if (anySmooth) {
                    std::unordered_set<int> seen = {f};
                    std::vector<int> q = {f};
                    Vec3 sum;
                    for (size_t qi = 0; qi < q.size(); ++qi) {
                        int g = q[qi];
                        sum += E.faces[g].plane.n;
                        for (int e : E.verts[v].edges) {
                            const Edge& Ed = E.edges[e];
                            if (!Ed.alive || !Ed.smooth) continue;
                            if (std::find(Ed.faces.begin(), Ed.faces.end(), g) == Ed.faces.end()) continue;
                            for (int h : Ed.faces) {
                                if (seen.count(h) || !E.faces[h].alive) continue;
                                // only blend faces whose normals point roughly the same way
                                if (E.faces[h].plane.n.dot(F.plane.n) < 0.2) continue;
                                seen.insert(h);
                                q.push_back(h);
                            }
                        }
                    }
                    n = sum.normalized();
                    if (n.isZero()) n = F.plane.n;
                }
                vn[v] = n;
                return n;
            };
            for (int idx : tr.tris) {
                Vec3 p, n;
                if (idx < (int)ids.size()) { p = E.verts[ids[idx]].p; n = vertexNormal(ids[idx]); }
                else { p = fr.to3(tr.pts[idx]); n = F.plane.n; }
                double uf, vf, ub, vbk;
                if (F.texFront.custom) { uf = p.dot(F.texFront.uAxis) + F.texFront.uOff; vf = p.dot(F.texFront.vAxis) + F.texFront.vOff; }
                else { uf = p.dot(fr.u); vf = p.dot(fr.v); }
                if (F.texBack.custom) { ub = p.dot(F.texBack.uAxis) + F.texBack.uOff; vbk = p.dot(F.texBack.vAxis) + F.texBack.vOff; }
                else { ub = p.dot(frb.u); vbk = p.dot(frb.v); }
                float data[10] = {(float)p.x, (float)p.y, (float)p.z, (float)n.x, (float)n.y, (float)n.z,
                                  (float)uf, (float)vf, (float)ub, (float)vbk};
                vb.insert(vb.end(), data, data + 10);
            }
            M.faceRange[f] = {first, (int)(vb.size() / 10) - first};
        }
        b.count = (int)(vb.size() / 10) - b.start;
        if (b.count > 0) M.batches.push_back(b);
    }
    if (!M.vaoF) { glGenVertexArrays(1, &M.vaoF); glGenBuffers(1, &M.vboF); }
    glBindVertexArray(M.vaoF);
    glBindBuffer(GL_ARRAY_BUFFER, M.vboF);
    glBufferData(GL_ARRAY_BUFFER, vb.size() * sizeof(float), vb.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 40, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 40, (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 40, (void*)24);

    // ---- edges
    std::vector<LV> eb;
    M.edgeFirst.clear();
    for (int e = 0; e < (int)E.edges.size(); ++e) {
        const Edge& Ed = E.edges[e];
        if (!Ed.alive || (Ed.hidden && !st.showHidden) || !tagVis(Ed.tag)) continue;
        LV a{}, b{};
        Vec3 p0 = E.verts[Ed.v[0]].p, p1 = E.verts[Ed.v[1]].p;
        a.p[0] = (float)p0.x; a.p[1] = (float)p0.y; a.p[2] = (float)p0.z;
        b.p[0] = (float)p1.x; b.p[1] = (float)p1.y; b.p[2] = (float)p1.z;
        QColor c = st.colorByTag && Ed.tag < (int)D->model.tags.size() ? D->model.tags[Ed.tag].color : st.edgeColor;
        float cc[4] = {(float)c.redF(), (float)c.greenF(), (float)c.blueF(), 1.f};
        memcpy(a.c, cc, 16); memcpy(b.c, cc, 16);
        int nf = 0;
        Vec3 n1, n2;
        for (int f : Ed.faces) {
            if (!E.faces[f].alive) continue;
            if (nf == 0) n1 = E.faces[f].plane.n; else if (nf == 1) n2 = E.faces[f].plane.n;
            ++nf;
        }
        int flags = 0;
        if (nf == 1) flags |= 1;
        if (nf == 2) flags |= 2;
        if (Ed.soft || (Ed.hidden && st.showHidden)) flags |= 4;
        if (Ed.smooth) flags |= 8;
        float fn1[3] = {(float)n1.x, (float)n1.y, (float)n1.z}, fn2[3] = {(float)n2.x, (float)n2.y, (float)n2.z};
        memcpy(a.n1, fn1, 12); memcpy(b.n1, fn1, 12);
        memcpy(a.n2, fn2, 12); memcpy(b.n2, fn2, 12);
        a.f = b.f = (float)flags;
        M.edgeFirst[e] = (int)eb.size();
        eb.push_back(a);
        eb.push_back(b);
    }
    M.nEdgeVerts = (int)eb.size();
    if (!M.vaoE) { glGenVertexArrays(1, &M.vaoE); glGenBuffers(1, &M.vboE); }
    glBindVertexArray(M.vaoE);
    glBindBuffer(GL_ARRAY_BUFFER, M.vboE);
    glBufferData(GL_ARRAY_BUFFER, eb.size() * sizeof(LV), eb.data(), GL_STATIC_DRAW);
    GLsizei st2 = sizeof(LV);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st2, (void*)offsetof(LV, p));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, st2, (void*)offsetof(LV, c));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, st2, (void*)offsetof(LV, n1));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, st2, (void*)offsetof(LV, n2));
    glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, st2, (void*)offsetof(LV, f));
    glBindVertexArray(0);
    M.rev = E.rev;
    M.sig = sig;
}

Renderer::Mesh& Renderer::mesh(int def) {
    Mesh& M = meshes_[def];
    const Entities& E = def < 0 ? D->model.root : D->model.defs[def].ents;
    uint64_t sig = styleSig();
    if (M.rev != E.rev || M.sig != sig || (!M.vaoF)) buildMesh(M, E, sig);
    M.used = true;
    return M;
}

QOpenGLTexture* Renderer::texture(int mat) {
    if (mat < 0 || mat >= (int)D->model.materials.size()) return nullptr;
    const Material& m = D->model.materials[mat];
    if (!m.hasTexture) return nullptr;
    auto it = textures_.find(mat);
    if (it != textures_.end() && textureRev_[mat] == m.rev) return it->second.get();
    QImage img = m.image();
    if (img.isNull()) return nullptr;
    auto t = std::make_unique<QOpenGLTexture>(img.mirrored().convertToFormat(QImage::Format_RGBA8888));
    t->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
    t->setMagnificationFilter(QOpenGLTexture::Linear);
    t->setWrapMode(QOpenGLTexture::Repeat);
    t->generateMipMaps();
    QOpenGLTexture* r = t.get();
    textures_[mat] = std::move(t);
    textureRev_[mat] = m.rev;
    return r;
}

// ------------------------------------------------------------------ collection

void Renderer::collect(int def, const Mat4& xf, int inheritMat, int depth, int pathPos, bool onPath,
                       std::vector<DrawItem>& out) {
    if (depth > 32) return;
    const InstPath& path = D->editPath;
    bool insideActive = onPath ? (pathPos >= (int)path.size()) : (depth > 0 && out.size() && false);
    (void)insideActive;
    // "faded" means outside of the active context.
    bool activeCtx = onPath && pathPos == (int)path.size();
    out.push_back({def, xf, inheritMat, false, activeCtx});
    const Entities& E = def < 0 ? D->model.root : D->model.defs[def].ents;
    const Style& st = D->model.style;
    for (int i = 0; i < (int)E.insts.size(); ++i) {
        const Instance& in = E.insts[i];
        if (!in.alive || in.def < 0 || !D->tagVisible(in.tag)) continue;
        if (in.hidden && !st.showHidden) continue;
        bool childOnPath = onPath && pathPos < (int)path.size() && path[pathPos] == i;
        collect(in.def, xf * in.xf, in.material >= 0 ? in.material : inheritMat, depth + 1, pathPos + 1, childOnPath, out);
    }
}

// ------------------------------------------------------------------ draw passes

void Renderer::drawBackground() {
    const Style& st = D->model.style;
    bgProg_.bind();
    float m[16];
    toGL(vp_.inverted(), m);
    glUniformMatrix4fv(bgProg_.uniformLocation("uInvVP"), 1, GL_FALSE, m);
    bgProg_.setUniformValue("uBg", qc(st.background));
    bgProg_.setUniformValue("uSky", qc(st.sky));
    bgProg_.setUniformValue("uGround", qc(st.ground));
    bgProg_.setUniformValue("uShowSky", st.showSky && P->cam->s->perspective && st.faceStyle != FaceStyle::Wireframe ? true : st.showSky);
    bgProg_.setUniformValue("uShowGround", st.showGround);
    bgProg_.setUniformValue("uGroundAlpha", (float)st.groundTransparency);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBindVertexArray(bgVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void Renderer::drawShadowMap(const std::vector<DrawItem>& items, const BBox& scene) {
    if (!scene.valid()) return;
    Vec3 L = lightDir_;
    Vec3 c = scene.center();
    double r = std::max(scene.radius(), 100.0);
    // include ground area around the model so shadows land on it
    r *= 1.6;
    Mat4 view = Mat4::lookAt(c + L * r * 2, c, std::fabs(L.z) > 0.99 ? Vec3(0, 1, 0) : Vec3(0, 0, 1));
    Mat4 proj = Mat4::ortho(-r, r, -r, r, 0.01, r * 4);
    lightVP_ = proj * view;
    GLint prevFbo;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    GLint vpOld[4];
    glGetIntegerv(GL_VIEWPORT, vpOld);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glViewport(0, 0, shadowSize_, shadowSize_);
    glClear(GL_DEPTH_BUFFER_BIT);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(2.0f, 4.0f);
    depthProg_.bind();
    float m[16];
    toGL(lightVP_, m);
    glUniformMatrix4fv(depthProg_.uniformLocation("uVP"), 1, GL_FALSE, m);
    depthProg_.setUniformValue("uUseClip", hasClip_);
    if (hasClip_) depthProg_.setUniformValue("uClip", QVector4D(-clip_.n.x, -clip_.n.y, -clip_.n.z, -clip_.d));
    for (auto& it : items) {
        Mesh& M = mesh(it.def);
        toGL(it.xf, m);
        glUniformMatrix4fv(depthProg_.uniformLocation("uModel"), 1, GL_FALSE, m);
        glBindVertexArray(M.vaoF);
        for (auto& b : M.batches) {
            if (b.transparent) continue;
            glDrawArrays(GL_TRIANGLES, b.start, b.count);
        }
    }
    glDisable(GL_POLYGON_OFFSET_FILL);
    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(vpOld[0], vpOld[1], vpOld[2], vpOld[3]);
}

void Renderer::drawFaces(const std::vector<DrawItem>& items, bool transparentPass, bool xray) {
    const Style& st = D->model.style;
    if (st.faceStyle == FaceStyle::Wireframe) return;
    faceProg_.bind();
    float m[16], n[9];
    toGL(vp_, m);
    glUniformMatrix4fv(faceProg_.uniformLocation("uVP"), 1, GL_FALSE, m);
    toGL(lightVP_, m);
    glUniformMatrix4fv(faceProg_.uniformLocation("uLightVP"), 1, GL_FALSE, m);
    faceProg_.setUniformValue("uUseClip", hasClip_);
    if (hasClip_) faceProg_.setUniformValue("uClip", QVector4D(-clip_.n.x, -clip_.n.y, -clip_.n.z, -clip_.d));
    faceProg_.setUniformValue("uLightDir", QVector3D(lightDir_.x, lightDir_.y, lightDir_.z));
    const ShadowSettings& sh = D->model.shadow;
    bool useSun = shadowsOn_ || sh.sunForShading;
    faceProg_.setUniformValue("uLight", useSun ? (float)(sh.light / 100.0) * 1.15f : 1.0f);
    faceProg_.setUniformValue("uDark", useSun ? (float)(sh.dark / 100.0) * 1.1f : 0.56f);
    faceProg_.setUniformValue("uUnlit", st.faceStyle == FaceStyle::HiddenLine);
    faceProg_.setUniformValue("uShadows", shadowsOn_ && sh.onFaces);
    faceProg_.setUniformValue("uShadowOnly", false);
    faceProg_.setUniformValue("uFog", st.fog);
    Vec3 eye = P->cam->s->eye;
    faceProg_.setUniformValue("uEye", QVector3D(eye.x, eye.y, eye.z));
    BBox sb = D->model.worldBBox();
    double dist = sb.valid() ? (sb.center() - eye).length() + sb.radius() : 1e5;
    faceProg_.setUniformValue("uFogRange", QVector2D(st.fogStart * dist, st.fogEnd * dist));
    faceProg_.setUniformValue("uFogColor", qc(st.fogColor));
    faceProg_.setUniformValue("uTexF", 0);
    faceProg_.setUniformValue("uTexB", 1);
    faceProg_.setUniformValue("uShadowMap", 2);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadowTex_);
    glActiveTexture(GL_TEXTURE0);
    faceProg_.setUniformValue("uAlphaMul", xray ? 0.5f : 1.0f);

    for (auto& it : items) {
        Mesh& M = mesh(it.def);
        toGL(it.xf, m);
        glUniformMatrix4fv(faceProg_.uniformLocation("uModel"), 1, GL_FALSE, m);
        nrm3(it.xf, n);
        glUniformMatrix3fv(faceProg_.uniformLocation("uNrmMat"), 1, GL_FALSE, n);
        bool faded = !D->editPath.empty() && !it.activeCtx && !it.faded;
        // Fade everything that is not inside the active context.
        faded = false;
        faceProg_.setUniformValue("uFade", it.faded ? 0.55f : (faded ? 0.55f : 0.0f));
        glFrontFace(it.xf.det3() < 0 ? GL_CW : GL_CCW);
        glBindVertexArray(M.vaoF);
        for (auto& b : M.batches) {
            bool transp = b.transparent || xray;
            if (transp != transparentPass) continue;
            int mf = b.matF >= 0 ? b.matF : it.inheritMat;
            int mb = b.matB >= 0 ? b.matB : it.inheritMat;
            QColor fc = st.frontColor, bc = st.backColor;
            double fa = 1, ba = 1;
            bool ftex = false, btex = false;
            QVector2D fs(1000, 1000), bs(1000, 1000);
            bool colorize = false;
            if (st.faceStyle == FaceStyle::HiddenLine) { fc = bc = st.background; }
            else if (st.faceStyle != FaceStyle::Monochrome) {
                if (mf >= 0 && mf < (int)D->model.materials.size()) {
                    const Material& mt = D->model.materials[mf];
                    fc = mt.color; fa = mt.alpha;
                    if (st.faceStyle == FaceStyle::ShadedTextured) {
                        if (QOpenGLTexture* t = texture(mf)) { glActiveTexture(GL_TEXTURE0); t->bind(); ftex = true; fs = QVector2D(mt.texW, mt.texH); colorize = mt.colorize; }
                    }
                }
                if (mb >= 0 && mb < (int)D->model.materials.size()) {
                    const Material& mt = D->model.materials[mb];
                    bc = mt.color; ba = mt.alpha;
                    if (st.faceStyle == FaceStyle::ShadedTextured) {
                        if (QOpenGLTexture* t = texture(mb)) { glActiveTexture(GL_TEXTURE1); t->bind(); btex = true; bs = QVector2D(mt.texW, mt.texH); glActiveTexture(GL_TEXTURE0); }
                    }
                }
            }
            faceProg_.setUniformValue("uFrontColor", qc(fc, fa));
            faceProg_.setUniformValue("uBackColor", qc(bc, ba));
            faceProg_.setUniformValue("uFrontTex", ftex);
            faceProg_.setUniformValue("uBackTex", btex);
            faceProg_.setUniformValue("uFrontTexSize", fs);
            faceProg_.setUniformValue("uBackTexSize", bs);
            faceProg_.setUniformValue("uColorize", colorize);
            glDrawArrays(GL_TRIANGLES, b.start, b.count);
        }
    }
    glFrontFace(GL_CCW);
}

void Renderer::setLineUniforms(float width, int dash, int mode, const Mat4& model) {
    float m[16], n[9];
    toGL(vp_, m);
    glUniformMatrix4fv(lineProg_.uniformLocation("uVP"), 1, GL_FALSE, m);
    toGL(model, m);
    glUniformMatrix4fv(lineProg_.uniformLocation("uModel"), 1, GL_FALSE, m);
    nrm3(model, n);
    glUniformMatrix3fv(lineProg_.uniformLocation("uNrmMat"), 1, GL_FALSE, n);
    lineProg_.setUniformValue("uViewport", QVector2D(P->w, P->h));
    lineProg_.setUniformValue("uWidth", (float)(width * P->dpr));
    lineProg_.setUniformValue("uMode", mode);
    lineProg_.setUniformValue("uDash", dash);
    lineProg_.setUniformValue("uDashScale", (float)P->dpr);
    Vec3 e = P->cam->s->eye, f = P->cam->forward();
    lineProg_.setUniformValue("uEye", QVector3D(e.x, e.y, e.z));
    lineProg_.setUniformValue("uViewDir", QVector3D(f.x, f.y, f.z));
    lineProg_.setUniformValue("uOrtho", !P->cam->s->perspective);
    lineProg_.setUniformValue("uUseClip", hasClip_);
    if (hasClip_) lineProg_.setUniformValue("uClip", QVector4D(-clip_.n.x, -clip_.n.y, -clip_.n.z, -clip_.d));
    lineProg_.setUniformValue("uExt", 0.0f);
    lineProg_.setUniformValue("uFade", 0.0f);
    lineProg_.setUniformValue("uDepthBias", 0.0f);
    lineProg_.setUniformValue("uNear", P->cam->s->perspective ? (float)P->cam->nearZ : 0.0f);
}

void Renderer::drawEdges(const std::vector<DrawItem>& items, int mode, float width, const QColor& col, int dash, bool backEdges) {
    const Style& st = D->model.style;
    lineProg_.bind();
    for (auto& it : items) {
        Mesh& M = mesh(it.def);
        if (!M.nEdgeVerts) continue;
        setLineUniforms(width, dash, mode, it.xf);
        lineProg_.setUniformValue("uColor", qc(col));
        int cm = st.colorByAxis ? 2 : (st.colorByTag ? 1 : 0);
        lineProg_.setUniformValue("uColorMode", backEdges ? 0 : cm);
        lineProg_.setUniformValue("uFade", it.faded ? 0.6f : 0.0f);
        lineProg_.setUniformValue("uExt", st.extension && mode == 0 ? (float)(4 * P->dpr) : 0.0f);
        lineProg_.setUniformValue("uDepthBias", mode == 1 ? 0.0f : 0.0f);
        glBindVertexArray(M.vaoE);
        glDrawArrays(GL_LINES, 0, M.nEdgeVerts);
    }
}

void Renderer::pushLine(const Vec3& a, const Vec3& b, const QColor& c) {
    LV x{}, y{};
    x.p[0] = (float)a.x; x.p[1] = (float)a.y; x.p[2] = (float)a.z;
    y.p[0] = (float)b.x; y.p[1] = (float)b.y; y.p[2] = (float)b.z;
    float cc[4] = {(float)c.redF(), (float)c.greenF(), (float)c.blueF(), (float)c.alphaF()};
    memcpy(x.c, cc, 16); memcpy(y.c, cc, 16);
    lineBuf_.push_back(x);
    lineBuf_.push_back(y);
}

void Renderer::flushLines(float width, int dash, bool depthTest, bool depthWrite) {
    if (lineBuf_.empty()) return;
    lineProg_.bind();
    setLineUniforms(width, dash, 0, Mat4());
    lineProg_.setUniformValue("uColorMode", 1);
    lineProg_.setUniformValue("uUseClip", false);
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
    glBindVertexArray(dynVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dynVbo_);
    glBufferData(GL_ARRAY_BUFFER, lineBuf_.size() * sizeof(LV), lineBuf_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_LINES, 0, (GLsizei)lineBuf_.size());
    lineBuf_.clear();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

void Renderer::pushTri(const Vec3& a, const Vec3& b, const Vec3& c) {
    for (const Vec3* p : {&a, &b, &c}) {
        triBuf_.push_back((float)p->x);
        triBuf_.push_back((float)p->y);
        triBuf_.push_back((float)p->z);
    }
}

void Renderer::flushTris(const QColor& c, bool depthTest, bool stipple, bool depthWrite) {
    if (triBuf_.empty()) return;
    flatProg_.bind();
    float m[16];
    toGL(vp_, m);
    glUniformMatrix4fv(flatProg_.uniformLocation("uVP"), 1, GL_FALSE, m);
    flatProg_.setUniformValue("uColor", qc(c));
    flatProg_.setUniformValue("uPattern", stipple ? 1 : 0);
    flatProg_.setUniformValue("uUseClip", false);
    if (depthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    glDepthMask(depthWrite ? GL_TRUE : GL_FALSE);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(triVao_);
    glBindBuffer(GL_ARRAY_BUFFER, triVbo_);
    glBufferData(GL_ARRAY_BUFFER, triBuf_.size() * sizeof(float), triBuf_.data(), GL_STREAM_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(triBuf_.size() / 3));
    triBuf_.clear();
    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
}

void Renderer::drawGround(const BBox& scene) {
    const ShadowSettings& sh = D->model.shadow;
    if (!shadowsOn_ || !sh.onGround || !scene.valid()) return;
    double r = std::max(scene.radius(), 1000.0) * 4;
    Vec3 c = scene.center();
    c.z = 0;
    // ground plane at z=0 receiving shadows only
    std::vector<float> v = {
        (float)(c.x - r), (float)(c.y - r), 0, 0, 0, 1, 0, 0, 0, 0,
        (float)(c.x + r), (float)(c.y - r), 0, 0, 0, 1, 0, 0, 0, 0,
        (float)(c.x + r), (float)(c.y + r), 0, 0, 0, 1, 0, 0, 0, 0,
        (float)(c.x - r), (float)(c.y - r), 0, 0, 0, 1, 0, 0, 0, 0,
        (float)(c.x + r), (float)(c.y + r), 0, 0, 0, 1, 0, 0, 0, 0,
        (float)(c.x - r), (float)(c.y + r), 0, 0, 0, 1, 0, 0, 0, 0};
    GLuint vao, vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, v.size() * 4, v.data(), GL_STREAM_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 40, (void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 40, (void*)12);
    glEnableVertexAttribArray(2); glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, 40, (void*)24);
    faceProg_.bind();
    float m[16], n[9];
    toGL(Mat4(), m);
    glUniformMatrix4fv(faceProg_.uniformLocation("uModel"), 1, GL_FALSE, m);
    nrm3(Mat4(), n);
    glUniformMatrix3fv(faceProg_.uniformLocation("uNrmMat"), 1, GL_FALSE, n);
    faceProg_.setUniformValue("uShadowOnly", true);
    faceProg_.setUniformValue("uShadows", true);
    faceProg_.setUniformValue("uUseClip", false);
    glDepthMask(GL_FALSE);
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(4.0f, 8.0f);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDepthMask(GL_TRUE);
    faceProg_.setUniformValue("uShadowOnly", false);
    glDeleteBuffers(1, &vbo);
    glDeleteVertexArrays(1, &vao);
}

void Renderer::drawSelection(const std::vector<DrawItem>& items) {
    // Find the active-context item.
    const DrawItem* act = nullptr;
    for (auto& it : items)
        if (it.activeCtx) { act = &it; break; }
    if (!act) return;
    Mesh& M = mesh(act->def);
    const Entities& E = D->active();
    float m[16], n[9];
    // stipple faces
    std::vector<std::pair<int, int>> ranges;
    std::vector<std::pair<int, int>> hoverRanges;
    for (auto& r : D->sel)
        if (r.kind == EntKind::Face) {
            auto f = M.faceRange.find(r.id);
            if (f != M.faceRange.end()) ranges.push_back(f->second);
        }
    for (auto& r : P->hover)
        if (r.kind == EntKind::Face && !D->isSelected(r)) {
            auto f = M.faceRange.find(r.id);
            if (f != M.faceRange.end()) hoverRanges.push_back(f->second);
        }
    stippleProg_.bind();
    toGL(vp_, m);
    glUniformMatrix4fv(stippleProg_.uniformLocation("uVP"), 1, GL_FALSE, m);
    toGL(act->xf, m);
    glUniformMatrix4fv(stippleProg_.uniformLocation("uModel"), 1, GL_FALSE, m);
    nrm3(act->xf, n);
    glUniformMatrix3fv(stippleProg_.uniformLocation("uNrmMat"), 1, GL_FALSE, n);
    stippleProg_.setUniformValue("uUseClip", hasClip_);
    if (hasClip_) stippleProg_.setUniformValue("uClip", QVector4D(-clip_.n.x, -clip_.n.y, -clip_.n.z, -clip_.d));
    glBindVertexArray(M.vaoF);
    glDepthFunc(GL_LEQUAL);
    glDepthMask(GL_FALSE);
    stippleProg_.setUniformValue("uColor", QVector4D(0.05f, 0.3f, 0.95f, 1.0f));
    stippleProg_.setUniformValue("uPattern", 0);
    for (auto& r : ranges) glDrawArrays(GL_TRIANGLES, r.first, r.second);
    stippleProg_.setUniformValue("uColor", QVector4D(0.25f, 0.55f, 1.0f, 1.0f));
    stippleProg_.setUniformValue("uPattern", 1);
    for (auto& r : hoverRanges) glDrawArrays(GL_TRIANGLES, r.first, r.second);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    // selected / hovered edges: draw from scratch into dyn buffer
    QColor selC = icolor::selection();
    QColor hovC(80, 140, 255);
    for (auto& r : D->sel) {
        if (r.kind == EntKind::Edge && r.id < (int)E.edges.size() && E.edges[r.id].alive) {
            const Edge& Ed = E.edges[r.id];
            pushLine(act->xf.point(E.verts[Ed.v[0]].p), act->xf.point(E.verts[Ed.v[1]].p), selC);
        }
    }
    for (auto& r : P->hover)
        if (r.kind == EntKind::Edge && !D->isSelected(r) && r.id < (int)E.edges.size() && E.edges[r.id].alive) {
            const Edge& Ed = E.edges[r.id];
            pushLine(act->xf.point(E.verts[Ed.v[0]].p), act->xf.point(E.verts[Ed.v[1]].p), hovC);
        }
    glEnable(GL_POLYGON_OFFSET_FILL);
    lineProg_.bind();
    flushLines(2.0f, 0, true);
    // Instance bounding boxes
    auto bboxLines = [&](const BBox& b, const Mat4& xf, const QColor& c) {
        if (!b.valid()) return;
        static const int edges[12][2] = {{0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
        for (auto& e : edges) pushLine(xf.point(b.corner(e[0])), xf.point(b.corner(e[1])), c);
    };
    for (auto& r : D->sel)
        if (r.kind == EntKind::Instance && r.id < (int)E.insts.size() && E.insts[r.id].alive) {
            const Instance& in = E.insts[r.id];
            bboxLines(D->model.defBBox(in.def), act->xf * in.xf, selC);
        }
    for (auto& r : P->hover)
        if (r.kind == EntKind::Instance && !D->isSelected(r) && r.id < (int)E.insts.size() && E.insts[r.id].alive) {
            const Instance& in = E.insts[r.id];
            bboxLines(D->model.defBBox(in.def), act->xf * in.xf, hovC);
        }
    flushLines(1.0f, 0, false);
    // Active context bbox (dashed)
    if (!D->editPath.empty()) {
        BBox b = D->model.entitiesBBox(E);
        bboxLines(b, act->xf, QColor(90, 90, 90));
        flushLines(1.0f, 2, false);
    }
}

void Renderer::drawSection(const std::vector<DrawItem>& items) {
    if (!hasClip_ || !D->model.style.showSectionCuts) return;
    uint64_t key = 1469598103934665603ULL;
    auto mix = [&](uint64_t v) { key ^= v; key *= 1099511628211ULL; };
    for (auto& it : items) {
        const Entities& E = it.def < 0 ? D->model.root : D->model.defs[it.def].ents;
        mix(E.rev);
        for (double v : it.xf.m) mix(std::hash<double>()(v));
    }
    mix(std::hash<double>()(clip_.n.x)); mix(std::hash<double>()(clip_.n.y)); mix(std::hash<double>()(clip_.n.z)); mix(std::hash<double>()(clip_.d));
    if (section_.key != key) {
        section_.key = key;
        section_.plane = clip_;
        section_.lines.clear();
        section_.fill.clear();
        std::vector<Vec3> segs;
        std::vector<Vec3> tri;
        for (auto& it : items) {
            const Entities& E = it.def < 0 ? D->model.root : D->model.defs[it.def].ents;
            for (int f = 0; f < (int)E.faces.size(); ++f) {
                if (!E.faces[f].alive || E.faces[f].hidden || !D->tagVisible(E.faces[f].tag)) continue;
                tri.clear();
                geom::faceTriangles(E, f, tri);
                for (size_t i = 0; i + 2 < tri.size(); i += 3) {
                    Vec3 p[3] = {it.xf.point(tri[i]), it.xf.point(tri[i + 1]), it.xf.point(tri[i + 2])};
                    double d[3] = {clip_.dist(p[0]), clip_.dist(p[1]), clip_.dist(p[2])};
                    std::vector<Vec3> hits;
                    for (int k = 0; k < 3; ++k) {
                        int l = (k + 1) % 3;
                        if ((d[k] > 0) != (d[l] > 0)) {
                            double t = d[k] / (d[k] - d[l]);
                            hits.push_back(p[k] + (p[l] - p[k]) * t);
                        }
                    }
                    if (hits.size() == 2 && !hits[0].near(hits[1], 1e-6)) { segs.push_back(hits[0]); segs.push_back(hits[1]); }
                }
            }
        }
        section_.lines = segs;
        // Chain segments into loops for the fill.
        Frame2 fr(clip_);
        std::map<std::pair<long long, long long>, std::vector<int>> ends;
        auto keyOf = [&](const Vec3& p) {
            Vec2 q = fr.to2(p);
            return std::make_pair((long long)std::llround(q.x * 100), (long long)std::llround(q.y * 100));
        };
        int ns = (int)segs.size() / 2;
        for (int s = 0; s < ns; ++s) {
            ends[keyOf(segs[s * 2])].push_back(s);
            ends[keyOf(segs[s * 2 + 1])].push_back(s);
        }
        std::vector<char> used(ns, 0);
        std::vector<std::vector<Vec2>> loops;
        for (int s = 0; s < ns; ++s) {
            if (used[s]) continue;
            std::vector<Vec2> loop;
            used[s] = 1;
            Vec3 start = segs[s * 2], cur = segs[s * 2 + 1];
            loop.push_back(fr.to2(start));
            int guard = 0;
            while (guard++ < 1000000) {
                loop.push_back(fr.to2(cur));
                auto& cand = ends[keyOf(cur)];
                int nx = -1;
                for (int c : cand) if (!used[c]) { nx = c; break; }
                if (nx < 0) break;
                used[nx] = 1;
                Vec3 a = segs[nx * 2], b = segs[nx * 2 + 1];
                cur = keyOf(a) == keyOf(cur) ? b : a;
                if (keyOf(cur) == keyOf(start)) break;
            }
            if (loop.size() >= 3) loops.push_back(loop);
        }
        if (D->model.style.sectionFill && !loops.empty()) {
            TessResult tr;
            if (tessellate(loops, tr))
                for (int idx : tr.tris) section_.fill.push_back(fr.to3(tr.pts[idx]));
        }
    }
    if (!section_.fill.empty()) {
        for (size_t i = 0; i + 2 < section_.fill.size(); i += 3) pushTri(section_.fill[i], section_.fill[i + 1], section_.fill[i + 2]);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(-1, -1);
        flushTris(D->model.style.sectionFillColor, true, false, true);
        glDisable(GL_POLYGON_OFFSET_FILL);
    }
    for (size_t i = 0; i + 1 < section_.lines.size(); i += 2) pushLine(section_.lines[i], section_.lines[i + 1], QColor(0, 0, 0));
    flushLines((float)D->model.style.profileWidth + 1, 0, true);
}

// ------------------------------------------------------------------ main

void Renderer::render(const Params& p) {
    P = &p;
    D = p.doc;
    const Style& st = D->model.style;
    Camera& cam = *p.cam;
    BBox scene = D->model.worldBBox();
    cam.fitClip(scene);
    vp_ = cam.viewProj();
    for (auto& [k, m] : meshes_) m.used = false;

    glViewport(0, 0, p.w, p.h);
    glClearColor(st.background.redF(), st.background.greenF(), st.background.blueF(), p.transparentBackground ? 0.f : 1.f);
    glClearDepth(1.0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    if (!p.transparentBackground) drawBackground();

    // Active section plane (world space)
    hasClip_ = false;
    {
        std::function<bool(const Entities&, const Mat4&)> find = [&](const Entities& E, const Mat4& xf) {
            for (auto& s : E.sections)
                if (s.alive && s.active && D->tagVisible(s.tag)) {
                    Vec3 o = xf.point(s.plane.origin());
                    clip_ = Plane(xf.normal(s.plane.n), o);
                    return true;
                }
            return false;
        };
        hasClip_ = find(D->model.root, Mat4());
        if (!hasClip_ && !D->editPath.empty()) hasClip_ = find(D->active(), D->activeXf());
        if (!st.showSectionCuts) hasClip_ = false;
    }
    if (hasClip_) glEnable(GL_CLIP_DISTANCE0); else glDisable(GL_CLIP_DISTANCE0);

    std::vector<DrawItem> items;
    collect(-1, Mat4(), -1, 0, 0, true, items);
    // Fade: items outside the active context
    if (!D->editPath.empty()) {
        // Everything is faded except the active entities and its descendants.
        // Determine descendants: items appended after the active item until depth returns.
        bool inside = false;
        size_t activeIdx = 0;
        for (size_t i = 0; i < items.size(); ++i)
            if (items[i].activeCtx) { activeIdx = i; inside = true; break; }
        (void)inside;
        // Recompute descendant set by walking the active entities' instance tree.
        std::function<int(int, size_t)> countDesc = [&](int def, size_t) -> int {
            const Entities& E = def < 0 ? D->model.root : D->model.defs[def].ents;
            int n = 0;
            for (auto& in : E.insts) {
                if (!in.alive || in.def < 0 || !D->tagVisible(in.tag) || (in.hidden && !st.showHidden)) continue;
                n += 1 + countDesc(in.def, 0);
            }
            return n;
        };
        int nd = countDesc(items[activeIdx].def, 0);
        for (size_t i = 0; i < items.size(); ++i) items[i].faded = !(i >= activeIdx && i <= activeIdx + nd);
        if (D->hideRest) {
            std::vector<DrawItem> keep;
            for (auto& it : items) if (!it.faded) keep.push_back(it);
            items.swap(keep);
        }
    }

    // Lighting
    const ShadowSettings& sh = D->model.shadow;
    shadowsOn_ = sh.enabled && st.faceStyle != FaceStyle::Wireframe;
    if (shadowsOn_ || sh.sunForShading) {
        lightDir_ = sh.sunDirection();
        if (lightDir_.z < 0.02) shadowsOn_ = false;  // sun below the horizon
    } else {
        // Camera-relative key light from above-left, like a studio setup.
        Vec3 f = cam.forward(), r = cam.right();
        lightDir_ = (Vec3(0, 0, 1) * 0.6 + (-f) * 0.75 + r * -0.3).normalized();
    }
    lightVP_ = Mat4();
    if (shadowsOn_) {
        drawShadowMap(items, scene);
        glViewport(0, 0, p.w, p.h);
    }

    // Opaque faces
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 2.0f);
    if (!st.xray) drawFaces(items, false, false);
    glDisable(GL_POLYGON_OFFSET_FILL);
    drawGround(scene);

    // Section fill & cut
    drawSection(items);

    // Edges
    if (st.edges || st.faceStyle == FaceStyle::Wireframe) drawEdges(items, 0, 1.0f, st.edgeColor, 0, false);
    if (st.showHidden) drawEdges(items, 2, 1.0f, QColor(110, 110, 110), 2, false);
    if (st.profiles && st.faceStyle != FaceStyle::Wireframe) drawEdges(items, 1, (float)st.profileWidth, st.edgeColor, 0, false);
    if (st.backEdges && !st.xray && st.faceStyle != FaceStyle::Wireframe) {
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_FALSE);
        drawEdges(items, 0, 1.0f, QColor(70, 70, 70), 1, true);
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
    }

    // Transparent faces
    glDepthMask(GL_FALSE);
    drawFaces(items, true, false);
    if (st.xray) drawFaces(items, true, true), drawFaces(items, false, true);
    glDepthMask(GL_TRUE);

    if (!p.forExport) drawSelection(items);

    // Guides, section planes, axes
    QColor guideC(60, 60, 60);
    double big = std::max(scene.valid() ? scene.radius() * 20 : 0.0, 1e6);
    std::function<void(int, const Mat4&, int)> walkAnnot = [&](int def, const Mat4& xf, int depth) {
        if (depth > 32) return;
        const Entities& E = def < 0 ? D->model.root : D->model.defs[def].ents;
        if (st.showGuides)
            for (int i = 0; i < (int)E.guides.size(); ++i) {
                const Guide& g = E.guides[i];
                if (!g.alive || g.hidden || !D->tagVisible(g.tag)) continue;
                if (g.isPoint) continue;
                Vec3 a = xf.point(g.p), d = xf.vector(g.dir).normalized();
                pushLine(a - d * big, a + d * big, guideC);
            }
        flushLines(1.0f, 1, true);
        if (st.showSectionPlanes)
            for (auto& s : E.sections) {
                if (!s.alive || s.hidden || !D->tagVisible(s.tag)) continue;
                Vec3 o = xf.point(s.plane.origin());
                BBox b = scene;
                double r = b.valid() ? b.radius() * 1.1 : 2000;
                Vec3 n = xf.normal(s.plane.n);
                Vec3 c = b.valid() ? Plane(n, o).project(b.center()) : o;
                Vec3 u = n.anyPerp(), v = n.cross(u);
                QColor sc = s.active ? QColor(230, 120, 30) : QColor(150, 150, 150);
                Vec3 q[4] = {c + (u + v) * r, c + (u - v) * r, c + (-u - v) * r, c + (-u + v) * r};
                for (int k = 0; k < 4; ++k) pushLine(q[k], q[(k + 1) % 4], sc);
                // arrows at corners pointing to the cut side (opposite normal)
                for (int k = 0; k < 4; ++k) {
                    Vec3 base = q[k] + (c - q[k]) * 0.08;
                    pushLine(base, base - n * r * 0.08, sc);
                }
            }
        flushLines(1.5f, 0, true);
        for (int i = 0; i < (int)E.insts.size(); ++i) {
            const Instance& in = E.insts[i];
            if (!in.alive || in.def < 0 || in.hidden || !D->tagVisible(in.tag)) continue;
            walkAnnot(in.def, xf * in.xf, depth + 1);
        }
    };
    walkAnnot(-1, Mat4(), 0);
    flushLines(1.0f, 1, true);

    // Dimension lines (text drawn by viewport painter)
    {
        std::function<void(int, const Mat4&, int)> walkDims = [&](int def, const Mat4& xf, int depth) {
            if (depth > 32) return;
            const Entities& E = def < 0 ? D->model.root : D->model.defs[def].ents;
            for (auto& d : E.dims) {
                if (!d.alive || d.hidden || !D->tagVisible(d.tag)) continue;
                Vec3 a = xf.point(d.a), b = xf.point(d.b), off = xf.vector(d.offset);
                QColor c(0, 0, 0);
                if (d.radial) {
                    pushLine(a, b, c);
                } else {
                    pushLine(a, a + off * 1.05, c);
                    pushLine(b, b + off * 1.05, c);
                    pushLine(a + off, b + off, c);
                    // arrow ticks
                    Vec3 dir = (b - a).normalized();
                    double ps = P->cam->pixelSize(a + off) * 8;
                    Vec3 on = off.normalized();
                    pushLine(a + off, a + off + dir * ps + on * ps * 0.35, c);
                    pushLine(a + off, a + off + dir * ps - on * ps * 0.35, c);
                    pushLine(b + off, b + off - dir * ps + on * ps * 0.35, c);
                    pushLine(b + off, b + off - dir * ps - on * ps * 0.35, c);
                }
            }
            for (auto& in : E.insts)
                if (in.alive && in.def >= 0 && !in.hidden && D->tagVisible(in.tag)) walkDims(in.def, xf * in.xf, depth + 1);
        };
        walkDims(-1, Mat4(), 0);
        flushLines(1.0f, 0, true);
    }

    if (st.showAxes) {
        const Model& M = D->model;
        Vec3 o = M.axesOrigin;
        double L = big;
        pushLine(o, o + M.axesX * L, QColor(200, 30, 30));
        pushLine(o, o + M.axesY * L, QColor(20, 150, 30));
        pushLine(o, o + M.axesZ * L, QColor(30, 50, 210));
        flushLines(1.0f, 0, true);
        pushLine(o, o - M.axesX * L, QColor(200, 30, 30, 200));
        pushLine(o, o - M.axesY * L, QColor(20, 150, 30, 200));
        pushLine(o, o - M.axesZ * L, QColor(30, 50, 210, 200));
        flushLines(1.0f, 2, true);
    }

    // Tool overlay
    if (p.overlay && p.drawOverlay) {
        const Overlay& ov = *p.overlay;
        for (auto& poly : ov.polys) {
            std::vector<std::vector<Vec2>> loops;
            if (poly.pts.size() < 3) continue;
            Vec3 n;
            for (size_t i = 0; i < poly.pts.size(); ++i) n += poly.pts[i].cross(poly.pts[(i + 1) % poly.pts.size()]);
            if (n.isZero(1e-12)) continue;
            Frame2 fr(Plane(n.normalized(), poly.pts[0]));
            std::vector<Vec2> q;
            for (auto& x : poly.pts) q.push_back(fr.to2(x));
            loops.push_back(q);
            for (auto& h : poly.holes) {
                q.clear();
                for (auto& x : h) q.push_back(fr.to2(x));
                loops.push_back(q);
            }
            TessResult tr;
            if (!tessellate(loops, tr)) continue;
            for (size_t i = 0; i + 2 < tr.tris.size(); i += 3) {
                Vec3 a = tr.tris[i] < (int)poly.pts.size() ? poly.pts[tr.tris[i]] : fr.to3(tr.pts[tr.tris[i]]);
                (void)a;
                pushTri(fr.to3(tr.pts[tr.tris[i]]), fr.to3(tr.pts[tr.tris[i + 1]]), fr.to3(tr.pts[tr.tris[i + 2]]));
            }
            flushTris(poly.c, true);
        }
        // group by (width, dash, onTop)
        std::map<std::tuple<float, int, bool>, std::vector<const Overlay::Line*>> groups;
        for (auto& l : ov.lines) groups[{l.width, l.dash, l.onTop}].push_back(&l);
        for (auto& [k, ls] : groups) {
            for (auto* l : ls) pushLine(l->a, l->b, l->c);
            flushLines(std::get<0>(k), std::get<1>(k), !std::get<2>(k));
        }
    }

    glDisable(GL_CLIP_DISTANCE0);
    glBindVertexArray(0);
    glUseProgram(0);
    // Free meshes of definitions no longer drawn (keep cache bounded).
    for (auto it = meshes_.begin(); it != meshes_.end();) {
        if (!it->second.used && meshes_.size() > 256) {
            glDeleteVertexArrays(1, &it->second.vaoF); glDeleteBuffers(1, &it->second.vboF);
            glDeleteVertexArrays(1, &it->second.vaoE); glDeleteBuffers(1, &it->second.vboE);
            it = meshes_.erase(it);
        } else ++it;
    }
}

}  // namespace gable
