#include "view/Renderer.h"
#include <QElapsedTimer>
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
layout(triangle_strip, max_vertices = 20) out;
in VS { vec4 col; vec3 n1; vec3 n2; float flags; vec3 w; float clip; } gi[];
uniform vec2 uViewport;
uniform float uWidth;
uniform float uProfileWidth;
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
// edge style: endpoint length, jitter amount, depth cue (max width, near, far), all in px / model depth
uniform float uEndLen;
uniform float uJitter;
uniform vec3 uCue;
// halo: widen every edge by uHaloGap on each side and push it back (depth only)
uniform float uHaloGap;
uniform vec3 uHaloPush;   // x: relative push (perspective), y, z: near and far planes
uniform float uHaloOrtho; // NDC push when orthographic
out vec4 fCol;
noperspective out float fDist;

vec4 P0, P1;
float C0, C1, LEN;
vec2 S0, S1, DIR, HALF;
vec4 COL;

float hash(vec3 p) {
    p = fract(p * 0.0137 + vec3(0.31, 0.17, 0.73));
    p += dot(p, p.yzx + 19.19);
    return fract((p.x + p.y) * p.z);
}

// sp lies at screen fraction s along the edge (outside 0..1 when extended); depth and clip
// distance come from the matching point of the 3D edge, so long extensions keep correct depth
void corner(vec2 sp, float s, float dist) {
    float den = (1.0 - s) * P1.w + s * P0.w;
    float t = abs(den) > 1e-9 ? s * P0.w / den : s;
    t = clamp(t, -4.0, 5.0);
    vec4 p = mix(P0, P1, t);
    float zb = p.z - uDepthBias * p.w;
    if (uHaloGap > 0.0) {
        if (uOrtho) zb += uHaloOrtho * p.w;
        else {
            float n = uHaloPush.y, f = uHaloPush.z;
            float de = p.w * (1.0 + uHaloPush.x);
            zb = ((f + n) / (f - n) - 2.0 * f * n / ((f - n) * de)) * p.w;
        }
    }
    gl_Position = vec4(sp / HALF * p.w, zb, p.w);
    gl_ClipDistance[0] = mix(C0, C1, t);
    fCol = COL;
    fDist = dist;
    EmitVertex();
}

// a stroke from fraction t0 to t1 of the edge, moved sideways by off px, its ends pushed out by e0/e1 px
void stroke(float t0, float t1, float off, float wid, float e0, float e1) {
    vec2 nrm = vec2(-DIR.y, DIR.x);
    vec2 a = mix(S0, S1, t0) + nrm * off - DIR * e0;
    vec2 b = mix(S0, S1, t1) + nrm * off + DIR * e1;
    vec2 h = nrm * (wid * 0.5);
    float L = max(LEN, 1e-6);
    float s0 = t0 - e0 / L, s1 = t1 + e1 / L;
    corner(a + h, s0, t0 * LEN - e0);
    corner(a - h, s0, t0 * LEN - e0);
    corner(b + h, s1, t1 * LEN + e1);
    corner(b - h, s1, t1 * LEN + e1);
    EndPrimitive();
}

void main() {
    int f = int(gi[0].flags + 0.5);
    bool soft = (f & 4) != 0;
    if (uMode == 0 && soft) return;
    if (uMode == 2 && !soft) return;
    float width = uWidth;
    bool prof = false;
    if (uMode == 1 || uMode == 3) {
        if (soft) return;
        prof = (f & 1) != 0;
        if ((f & 2) != 0) {
            vec3 mid = (gi[0].w + gi[1].w) * 0.5;
            vec3 V = uOrtho ? -uViewDir : (uEye - mid);
            float a = dot(gi[0].n1, V), b = dot(gi[0].n2, V);
            prof = (a > 0.0) != (b > 0.0);
        }
        if (uMode == 1 && !prof) return;
        // mode 3: every hard edge in one pass, profiles drawn wider
        if (uMode == 3 && prof) width = uProfileWidth;
    }
    // depth cue: near edges wide, far ones one pixel (profiles keep their width when wider)
    if (uCue.x > 0.0 && (uMode == 0 || uMode == 3)) {
        float de = dot((gi[0].w + gi[1].w) * 0.5 - uEye, uViewDir);
        float t = clamp((de - uCue.y) / max(uCue.z - uCue.y, 1e-6), 0.0, 1.0);
        float cw = mix(uCue.x, uWidth, t);
        width = prof ? max(width, cw) : cw;
    }
    P0 = gl_in[0].gl_Position; P1 = gl_in[1].gl_Position;
    C0 = gi[0].clip; C1 = gi[1].clip;
    float e = max(uNear * 1.001, 1e-4);
    if (P0.w < e && P1.w < e) return;
    if (P0.w < e) { float t = (e - P0.w) / (P1.w - P0.w); P0 = mix(P0, P1, t); C0 = mix(C0, C1, t); }
    else if (P1.w < e) { float t = (e - P1.w) / (P0.w - P1.w); P1 = mix(P1, P0, t); C1 = mix(C1, C0, t); }
    HALF = uViewport * 0.5;
    S0 = P0.xy / P0.w * HALF;
    S1 = P1.xy / P1.w * HALF;
    vec2 d = S1 - S0;
    LEN = length(d);
    DIR = LEN < 1e-6 ? vec2(1.0, 0.0) : d / LEN;
    if (uColorMode == 1) COL = gi[0].col;
    else if (uColorMode == 2) {
        vec3 dir = normalize(gi[1].w - gi[0].w);
        if (abs(dir.x) > 0.9999) COL = vec4(0.85, 0.1, 0.1, 1.0);
        else if (abs(dir.y) > 0.9999) COL = vec4(0.1, 0.65, 0.1, 1.0);
        else if (abs(dir.z) > 0.9999) COL = vec4(0.1, 0.2, 0.9, 1.0);
        else COL = uColor;
    } else COL = uColor;
    COL.rgb = mix(COL.rgb, vec3(1.0), uFade);
    if (uHaloGap > 0.0) width += 2.0 * uHaloGap;
    stroke(0.0, 1.0, 0.0, width, uExt, uExt);
    // endpoints: the line thickens over its last few pixels at each end
    if (uEndLen > 0.0 && LEN > 1.0) {
        float t = min(uEndLen / LEN, 0.5);
        float w2 = width + max(uWidth, 1.0) * 1.5;
        stroke(0.0, t, 0.0, w2, 0.0, 0.0);
        stroke(1.0 - t, 1.0, 0.0, w2, 0.0, 0.0);
    }
    // jitter: two more strokes wandering a little off the line, the same for this edge every frame
    if (uJitter > 0.0 && LEN > 1.0) {
        vec3 k = gi[0].w + gi[1].w * 1.618;
        for (int i = 0; i < 2; ++i) {
            float r1 = hash(k + float(i) * 17.0), r2 = hash(k * 1.3 + float(i) * 5.0), r3 = hash(k * 0.7 - float(i) * 3.0);
            float off = (r1 * 2.0 - 1.0) * uJitter;
            stroke(0.0, 1.0, off, max(uWidth, 1.0), (r2 - 0.3) * uJitter * 2.0, (r3 - 0.3) * uJitter * 2.0);
        }
    }
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

static const char* kPhotoVS = R"(
#version 330 core
uniform vec4 uRect;   // x0, y0, x1, y1 in normalized device coordinates
out vec2 vUv;
void main() {
    vec2 k = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1));
    vUv = k;
    gl_Position = vec4(mix(uRect.xy, uRect.zw, k), 0.999998, 1.0);
}
)";

static const char* kPhotoFS = R"(
#version 330 core
in vec2 vUv;
uniform sampler2D uTex;
out vec4 oColor;
void main() { oColor = vec4(texture(uTex, vUv).rgb, 1.0); }
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
    if (!build(photoProg_, kPhotoVS, nullptr, kPhotoFS)) return false;
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
    photoTex_.reset();
    if (selCache_.vao) { glDeleteVertexArrays(1, &selCache_.vao); glDeleteBuffers(1, &selCache_.vbo); }
    selCache_ = SelCache();
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

// ------------------------------------------------------------------ mesh building

namespace {
inline void mixH(uint64_t& h, uint64_t v) { h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2); }
inline void mixD(uint64_t& h, double d) { uint64_t u; memcpy(&u, &d, 8); mixH(h, u); }
inline void mixV(uint64_t& h, const Vec3& v) { mixD(h, v.x); mixD(h, v.y); mixD(h, v.z); }
}  // namespace

// Everything a face's triangles depend on except the normals of its smooth neighbours.
static uint64_t faceSignature(const Entities& E, int f) {
    const Face& F = E.faces[f];
    uint64_t h = 1469598103934665603ULL;
    mixH(h, F.alive | (F.hidden << 1) | ((uint64_t)(uint32_t)F.tag << 8));
    mixH(h, (uint64_t)(uint32_t)F.matFront << 32 | (uint32_t)F.matBack);
    mixV(h, F.plane.n);
    mixD(h, F.plane.d);
    for (const TexMap* t : {&F.texFront, &F.texBack})
        if (t->custom) { mixV(h, t->uAxis); mixV(h, t->vAxis); mixD(h, t->uOff); mixD(h, t->vOff); }
    for (int v : F.outer) { mixH(h, (uint64_t)v); mixV(h, E.verts[v].p); }
    for (auto& L : F.holes) {
        mixH(h, 0xfeedULL);
        for (int v : L) { mixH(h, (uint64_t)v); mixV(h, E.verts[v].p); }
    }
    return h;
}

static uint64_t edgeSignature(const Entities& E, int e) {
    const Edge& Ed = E.edges[e];
    uint64_t h = 1469598103934665603ULL;
    mixH(h, Ed.alive | (Ed.soft << 1) | (Ed.smooth << 2) | (Ed.hidden << 3) | ((uint64_t)(uint32_t)Ed.tag << 8));
    mixV(h, E.verts[Ed.v[0]].p);
    mixV(h, E.verts[Ed.v[1]].p);
    for (int f : Ed.faces) { mixH(h, (uint64_t)f | ((uint64_t)E.faces[f].alive << 40)); mixV(h, E.faces[f].plane.n); }
    return h;
}

// Builds the triangles of single faces: tessellation, smooth normals across smooth edges, UVs.
struct Renderer::FaceGen {
    const Entities& E;
    std::vector<char> vSmooth;
    std::vector<int> fanOff, fanLen, fanFaces;
    std::vector<uint64_t> fanAdj;
    std::vector<std::pair<int, Vec3>> vn;
    std::vector<std::vector<Vec2>> loops;
    std::vector<int> ids;
    TessResult tr;

    explicit FaceGen(const Entities& e) : E(e), vSmooth(e.verts.size(), 0), fanOff(e.verts.size(), -1), fanLen(e.verts.size(), 0) {
        for (const Edge& Ed : E.edges)
            if (Ed.alive && Ed.smooth) vSmooth[Ed.v[0]] = vSmooth[Ed.v[1]] = 1;
    }

    void buildFan(int v) {
        fanOff[v] = (int)fanFaces.size();
        for (int e : E.verts[v].edges)
            for (int h : E.edges[e].faces)
                if (E.faces[h].alive && std::find(fanFaces.begin() + fanOff[v], fanFaces.end(), h) == fanFaces.end() &&
                    (int)fanFaces.size() - fanOff[v] < 64) fanFaces.push_back(h);
        int cnt = (int)fanFaces.size() - fanOff[v];
        fanLen[v] = cnt;
        fanAdj.resize(fanFaces.size(), 0);
        auto slot = [&](int h) {
            for (int k = 0; k < cnt; ++k) if (fanFaces[fanOff[v] + k] == h) return k;
            return -1;
        };
        for (int e : E.verts[v].edges) {
            const Edge& Ed = E.edges[e];
            if (!Ed.alive || !Ed.smooth) continue;
            for (int a : Ed.faces)
                for (int b : Ed.faces) {
                    int sa = slot(a), sb = slot(b);
                    if (sa >= 0 && sb >= 0 && sa != sb) fanAdj[fanOff[v] + sa] |= 1ULL << sb;
                }
        }
    }

    // smooth normal at v for face f: faces joined across smooth edges at v, roughly facing the same way
    Vec3 vertexNormal(int f, int v) {
        for (auto& [k, n] : vn) if (k == v) return n;
        const Face& F = E.faces[f];
        Vec3 n = F.plane.n;
        if (vSmooth[v]) {
            if (fanOff[v] < 0) buildFan(v);
            const int off = fanOff[v], cnt = fanLen[v];
            int start = -1;
            for (int k = 0; k < cnt; ++k) if (fanFaces[off + k] == f) { start = k; break; }
            Vec3 sum = F.plane.n;
            if (start >= 0) {
                uint64_t seenMask = 1ULL << start, todo = fanAdj[off + start];
                while (todo & ~seenMask) {
                    int k = __builtin_ctzll(todo & ~seenMask);
                    seenMask |= 1ULL << k;
                    const Vec3& hn = E.faces[fanFaces[off + k]].plane.n;
                    if (hn.dot(F.plane.n) < 0.2) continue;
                    sum += hn;
                    todo |= fanAdj[off + k];
                }
            }
            n = sum.normalized();
            if (n.isZero()) n = F.plane.n;
        }
        vn.emplace_back(v, n);
        return n;
    }

    // Appends the face's vertices (10 floats each); false if it cannot be tessellated.
    bool emitFace(int f, std::vector<float>& vb) {
        const Face& F = E.faces[f];
        Frame2 fr(F.plane);
        Plane back = F.plane;
        back.n = -back.n; back.d = -back.d;
        Frame2 frb(back);
        loops.resize(1 + F.holes.size());
        ids.clear();
        auto addL = [&](const std::vector<int>& L, std::vector<Vec2>& q) {
            q.clear();
            for (int v : L) { q.push_back(fr.to2(E.verts[v].p)); ids.push_back(v); }
        };
        addL(F.outer, loops[0]);
        for (size_t h = 0; h < F.holes.size(); ++h) addL(F.holes[h], loops[h + 1]);
        if (!tessellate(loops, tr)) return false;
        vn.clear();
        for (int idx : tr.tris) {
            Vec3 p, n;
            if (idx < (int)ids.size()) { p = E.verts[ids[idx]].p; n = vertexNormal(f, ids[idx]); }
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
        return true;
    }
};

void Renderer::edgeVerts(const Entities& E, int e, LV& a, LV& b) const {
    const Style& st = D->model.style;
    const Edge& Ed = E.edges[e];
    a = LV{}; b = LV{};
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
    vb.reserve(E.faces.size() * 60);
    M.batches.clear();
    M.faceRange.assign(E.faces.size(), {0, 0});
    FaceGen gen(E);
    for (auto& [key, fl] : groups) {
        Batch b;
        b.start = (int)(vb.size() / 10);
        b.matF = key.first;
        b.matB = key.second;
        auto alpha = [&](int m) { return m >= 0 && m < (int)D->model.materials.size() ? D->model.materials[m].alpha : 1.0; };
        b.transparent = alpha(b.matF) < 0.999 || alpha(b.matB) < 0.999;
        for (int f : fl) {
            int first = (int)(vb.size() / 10);
            if (!gen.emitFace(f, vb)) continue;
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
    eb.reserve(E.edges.size() * 2);
    M.edgeFirst.assign(E.edges.size(), -1);
    for (int e = 0; e < (int)E.edges.size(); ++e) {
        const Edge& Ed = E.edges[e];
        if (!Ed.alive || (Ed.hidden && !st.showHidden) || !tagVis(Ed.tag)) continue;
        LV a, b;
        edgeVerts(E, e, a, b);
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
    // signatures for later in-place updates
    M.faceSig.resize(E.faces.size());
    for (int f = 0; f < (int)E.faces.size(); ++f) M.faceSig[f] = faceSignature(E, f);
    M.edgeSig.resize(E.edges.size());
    for (int e = 0; e < (int)E.edges.size(); ++e) M.edgeSig[e] = edgeSignature(E, e);
    M.rev = E.rev;
    M.sig = sig;
}

// When only vertex positions moved (a Move/Rotate/Scale preview, or a small edit that kept the
// entity counts), rewrite just the faces and edges that changed in place. Returns false when a
// full rebuild is needed.
bool Renderer::updateMesh(Mesh& M, const Entities& E) {
    if (M.faceSig.size() != E.faces.size() || M.edgeSig.size() != E.edges.size() || M.faceRange.size() != E.faces.size())
        return false;
    const int nf = (int)E.faces.size(), ne = (int)E.edges.size();
    std::vector<int> dirtyF, dirtyE;
    if (E.movedRev == E.rev && E.movedBaseRev == M.rev) {
        // only the hinted vertices moved since this mesh was built: look at what touches them
        std::vector<char> mf(nf, 0), me(ne, 0);
        for (int v : E.movedVerts) {
            if (v < 0 || v >= (int)E.verts.size()) return false;
            for (int e : E.verts[v].edges) {
                if (!me[e]) { me[e] = 1; dirtyE.push_back(e); }
                for (int g : E.edges[e].faces)
                    if (!mf[g]) { mf[g] = 1; dirtyF.push_back(g); }
            }
        }
        // the edges of those faces carry the faces' normals (profiles)
        size_t nFaces = dirtyF.size();
        for (size_t i = 0; i < nFaces; ++i) {
            const Face& F = E.faces[dirtyF[i]];
            auto loopEdges = [&](const std::vector<int>& L) {
                for (size_t k = 0; k < L.size(); ++k) {
                    int e = E.findEdge(L[k], L[(k + 1) % L.size()]);
                    if (e >= 0 && !me[e]) { me[e] = 1; dirtyE.push_back(e); }
                }
            };
            loopEdges(F.outer);
            for (auto& h : F.holes) loopEdges(h);
        }
        if ((int)dirtyF.size() > std::max(64, nf / 8) || (int)dirtyE.size() > std::max(64, ne / 8)) return false;
        for (int f : dirtyF) M.faceSig[f] = faceSignature(E, f);
        for (int e : dirtyE) M.edgeSig[e] = edgeSignature(E, e);
    } else {
        std::vector<uint64_t> fs(nf), es(ne);
        for (int f = 0; f < nf; ++f) {
            fs[f] = faceSignature(E, f);
            if (fs[f] != M.faceSig[f]) {
                dirtyF.push_back(f);
                if ((int)dirtyF.size() > std::max(64, nf / 8)) return false;
            }
        }
        for (int e = 0; e < ne; ++e) {
            es[e] = edgeSignature(E, e);
            if (es[e] != M.edgeSig[e]) {
                dirtyE.push_back(e);
                if ((int)dirtyE.size() > std::max(64, ne / 8)) return false;
            }
        }
        M.faceSig.swap(fs);
        M.edgeSig.swap(es);
    }
    if (dirtyF.empty() && dirtyE.empty()) {
        M.rev = E.rev;
        return true;
    }
    FaceGen gen(E);
    // a moved face changes the smooth normals of the faces around its smooth vertices
    {
        std::vector<char> mark(nf, 0);
        for (int f : dirtyF) mark[f] = 1;
        size_t n0 = dirtyF.size();
        for (size_t i = 0; i < n0; ++i) {
            const Face& F = E.faces[dirtyF[i]];
            auto ring = [&](const std::vector<int>& L) {
                for (int v : L) {
                    if (!gen.vSmooth[v]) continue;
                    for (int e : E.verts[v].edges)
                        for (int g : E.edges[e].faces)
                            if (!mark[g]) { mark[g] = 1; dirtyF.push_back(g); }
                }
            };
            ring(F.outer);
            for (auto& h : F.holes) ring(h);
        }
    }
    const Style& st = D->model.style;
    std::vector<float> vb;
    glBindBuffer(GL_ARRAY_BUFFER, M.vboF);
    for (int f : dirtyF) {
        const Face& F = E.faces[f];
        bool visible = F.alive && (!F.hidden || st.showHidden) && D->tagVisible(F.tag);
        auto [first, count] = M.faceRange[f];
        if (!visible) { if (count > 0) return false; continue; }
        if (count == 0) return false;   // newly visible: belongs in a batch
        // the face must still belong to the batch (materials) it was built in
        for (auto& b : M.batches)
            if (first >= b.start && first < b.start + b.count && (b.matF != F.matFront || b.matB != F.matBack)) return false;
        vb.clear();
        if (!gen.emitFace(f, vb) || (int)vb.size() != count * 10) return false;
        glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)first * 40, (GLsizeiptr)vb.size() * sizeof(float), vb.data());
    }
    glBindBuffer(GL_ARRAY_BUFFER, M.vboE);
    for (int e : dirtyE) {
        const Edge& Ed = E.edges[e];
        bool visible = Ed.alive && (!Ed.hidden || st.showHidden) && D->tagVisible(Ed.tag);
        int first = M.edgeFirst[e];
        if (visible != (first >= 0)) return false;
        if (!visible) continue;
        LV ab[2];
        edgeVerts(E, e, ab[0], ab[1]);
        glBufferSubData(GL_ARRAY_BUFFER, (GLintptr)first * sizeof(LV), sizeof(ab), ab);
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    M.rev = E.rev;
    return true;
}

Renderer::Mesh& Renderer::mesh(int def) {
    Mesh& M = meshes_[def];
    const Entities& E = def < 0 ? D->model.root : D->model.defs[def].ents;
    uint64_t sig = styleSig();
    if (M.rev != E.rev || M.sig != sig || (!M.vaoF)) {
        QElapsedTimer t;
        t.start();
        if (!(M.vaoF && M.sig == sig && updateMesh(M, E))) buildMesh(M, E, sig);
        lastBuildMs += t.nsecsElapsed() / 1e6;
    }
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
        Mat4 ixf = in.xf;
        if (D->model.defs[in.def].alwaysFaceCamera) {
            // rotate about the instance's vertical axis so its front (-Y) faces the viewer
            Mat4 w = xf * in.xf;
            Vec3 o = w.origin();
            Vec3 v = P->cam->s->perspective ? (P->cam->s->eye - o) : -P->cam->forward();
            v.z = 0;
            if (v.length() > 1e-9) {
                v = v.normalized();
                double sx = in.xf.axis(0).length(), sy = in.xf.axis(1).length(), sz = in.xf.axis(2).length();
                Mat4 parentInv = xf.inverted();
                Vec3 vl = parentInv.vector(v);
                vl.z = 0;
                double ang = std::atan2(vl.x, -vl.y);
                ixf = Mat4::translation(in.xf.origin()) * Mat4::rotation(Vec3(0, 0, 1), ang) * Mat4::scaling(Vec3(sx, sy, sz));
            }
        }
        collect(in.def, xf * ixf, in.material >= 0 ? in.material : inheritMat, depth + 1, pathPos + 1, childOnPath, out);
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
    // a matched photo fills the view's height, centred, while the camera is the one matched to it
    if (const Scene* sc = D->model.photoInView()) {
        uint key = qHash(sc->photo.image) ^ (uint)sc->photo.image.size();
        if (!photoTex_ || key != photoKey_) {
            QImage img = QImage::fromData(sc->photo.image);
            photoTex_.reset();
            if (!img.isNull()) {
                photoAspect_ = (double)img.width() / std::max(1, img.height());
                photoTex_ = std::make_unique<QOpenGLTexture>(img.mirrored().convertToFormat(QImage::Format_RGBA8888));
                photoTex_->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
                photoTex_->setMagnificationFilter(QOpenGLTexture::Linear);
                photoTex_->setWrapMode(QOpenGLTexture::ClampToEdge);
                photoTex_->generateMipMaps();
            }
            photoKey_ = key;
        }
        if (photoTex_) {
            double hw = photoAspect_ * P->h / std::max(1, P->w);
            photoProg_.bind();
            photoProg_.setUniformValue("uRect", QVector4D((float)-hw, -1.0f, (float)hw, 1.0f));
            photoProg_.setUniformValue("uTex", 0);
            glActiveTexture(GL_TEXTURE0);
            photoTex_->bind();
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }
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
    lineProg_.setUniformValue("uEndLen", 0.0f);
    lineProg_.setUniformValue("uJitter", 0.0f);
    lineProg_.setUniformValue("uCue", QVector3D(0, 0, 1));
    lineProg_.setUniformValue("uHaloGap", 0.0f);
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
        // the sketch effects belong to the visible edges, not to hidden or back edges
        const bool styled = !backEdges && (mode == 0 || mode == 1 || mode == 3);
        lineProg_.setUniformValue("uExt", styled && mode != 1 && st.extension ? (float)(st.extensionLength * P->dpr) : 0.0f);
        lineProg_.setUniformValue("uEndLen", styled && st.endpoints ? (float)(st.endpointLength * P->dpr) : 0.0f);
        lineProg_.setUniformValue("uJitter", styled && st.jitter ? (float)(st.jitterAmount * P->dpr) : 0.0f);
        lineProg_.setUniformValue("uCue", styled && st.depthCue ? QVector3D((float)(st.depthCueWidth * P->dpr), cueNear_, cueFar_) : QVector3D(0, 0, 1));
        if (haloPass_) {
            lineProg_.setUniformValue("uHaloGap", (float)(st.haloGap * P->dpr));
            lineProg_.setUniformValue("uHaloPush", QVector3D(0.015f, (float)P->cam->nearZ, (float)P->cam->farZ));
            lineProg_.setUniformValue("uHaloOrtho", haloOrtho_);
        }
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
    // Selected faces and edges are cached per (selection, geometry) so a large selection costs
    // nothing per frame: faces as merged vertex ranges drawn in one call, edges in their own buffer.
    SelCache& SC = selCache_;
    if (SC.selRev != D->selRev || SC.geomRev != E.rev || SC.def != act->def || SC.mesh != M.vaoF || SC.meshRev != M.rev) {
        SC.selRev = D->selRev; SC.geomRev = E.rev; SC.def = act->def; SC.mesh = M.vaoF; SC.meshRev = M.rev;
        std::vector<std::pair<int, int>> rs;
        for (auto& r : D->sel)
            if (r.kind == EntKind::Face && r.id < (int)M.faceRange.size() && M.faceRange[r.id].second > 0) rs.push_back(M.faceRange[r.id]);
        std::sort(rs.begin(), rs.end());
        SC.firsts.clear(); SC.counts.clear();
        for (auto& r : rs) {
            if (!SC.firsts.empty() && SC.firsts.back() + SC.counts.back() == r.first) SC.counts.back() += r.second;
            else { SC.firsts.push_back(r.first); SC.counts.push_back(r.second); }
        }
        std::vector<LV> eb;
        QColor selC = icolor::selection();
        float cc[4] = {(float)selC.redF(), (float)selC.greenF(), (float)selC.blueF(), 1.f};
        for (auto& r : D->sel)
            if (r.kind == EntKind::Edge && r.id < (int)E.edges.size() && E.edges[r.id].alive) {
                const Edge& Ed = E.edges[r.id];
                for (int k = 0; k < 2; ++k) {
                    LV x{};
                    Vec3 p = E.verts[Ed.v[k]].p;
                    x.p[0] = (float)p.x; x.p[1] = (float)p.y; x.p[2] = (float)p.z;
                    memcpy(x.c, cc, 16);
                    eb.push_back(x);
                }
            }
        if (!SC.vao) { glGenVertexArrays(1, &SC.vao); glGenBuffers(1, &SC.vbo); }
        glBindVertexArray(SC.vao);
        glBindBuffer(GL_ARRAY_BUFFER, SC.vbo);
        glBufferData(GL_ARRAY_BUFFER, eb.size() * sizeof(LV), eb.data(), GL_STATIC_DRAW);
        GLsizei st2 = sizeof(LV);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, st2, (void*)offsetof(LV, p));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, st2, (void*)offsetof(LV, c));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, st2, (void*)offsetof(LV, n1));
        glEnableVertexAttribArray(3); glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, st2, (void*)offsetof(LV, n2));
        glEnableVertexAttribArray(4); glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, st2, (void*)offsetof(LV, f));
        SC.nEdgeVerts = (int)eb.size();
    }
    std::vector<std::pair<int, int>> hoverRanges;
    for (auto& r : P->hover)
        if (r.kind == EntKind::Face && !D->isSelected(r)) {
            if (r.id < (int)M.faceRange.size() && M.faceRange[r.id].second > 0) hoverRanges.push_back(M.faceRange[r.id]);
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
    if (!SC.firsts.empty()) glMultiDrawArrays(GL_TRIANGLES, SC.firsts.data(), SC.counts.data(), (GLsizei)SC.firsts.size());
    stippleProg_.setUniformValue("uColor", QVector4D(0.25f, 0.55f, 1.0f, 1.0f));
    stippleProg_.setUniformValue("uPattern", 1);
    for (auto& r : hoverRanges) glDrawArrays(GL_TRIANGLES, r.first, r.second);
    glDepthMask(GL_TRUE);
    glDepthFunc(GL_LESS);

    // selected / hovered edges: draw from scratch into dyn buffer
    QColor selC = icolor::selection();
    QColor hovC(80, 140, 255);
    glEnable(GL_POLYGON_OFFSET_FILL);
    if (SC.nEdgeVerts) {
        lineProg_.bind();
        setLineUniforms(2.0f, 0, 0, act->xf);
        lineProg_.setUniformValue("uColorMode", 1);
        glEnable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glBindVertexArray(SC.vao);
        glDrawArrays(GL_LINES, 0, SC.nEdgeVerts);
        glDepthMask(GL_TRUE);
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

double Renderer::lastRenderMs = 0, Renderer::lastBuildMs = 0;

void Renderer::render(const Params& p) {
    QElapsedTimer frameClock;
    frameClock.start();
    lastBuildMs = 0;
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
    const bool wantEdges = st.edges || st.faceStyle == FaceStyle::Wireframe;
    const bool wantProfiles = st.profiles && st.faceStyle != FaceStyle::Wireframe;
    // depth cue runs from the nearest to the farthest point of the model
    cueNear_ = 0;
    cueFar_ = 1;
    if (st.depthCue && scene.valid()) {
        Vec3 f = cam.forward();
        double lo = 1e300, hi = -1e300;
        for (int k = 0; k < 8; ++k) {
            Vec3 c((k & 1) ? scene.hi.x : scene.lo.x, (k & 2) ? scene.hi.y : scene.lo.y, (k & 4) ? scene.hi.z : scene.lo.z);
            double de = (c - cam.s->eye).dot(f);
            lo = std::min(lo, de);
            hi = std::max(hi, de);
        }
        cueNear_ = (float)std::max(lo, 0.0);
        cueFar_ = (float)std::max(hi, cueNear_ + 1.0);
    }
    auto visibleEdges = [&] {
        if (wantEdges && wantProfiles) {
            // one pass over the edge buffers instead of two
            lineProg_.bind();
            lineProg_.setUniformValue("uProfileWidth", (float)(st.profileWidth * P->dpr));
            drawEdges(items, 3, 1.0f, st.edgeColor, 0, false);
        } else if (wantEdges) drawEdges(items, 0, 1.0f, st.edgeColor, 0, false);
        if (wantProfiles && !wantEdges) drawEdges(items, 1, (float)st.profileWidth, st.edgeColor, 0, false);
    };
    const bool halo = st.halo && st.haloGap > 0 && (wantEdges || wantProfiles);
    if (halo) {
        // Halo: every edge first writes a wider band of depth slightly behind itself, so an edge
        // passing behind it is broken there while edges meeting it at a corner still join.
        haloPass_ = true;
        haloOrtho_ = cam.s->perspective ? 0.0f : (float)(std::max(scene.valid() ? scene.radius() : 1000.0, 1.0) * 0.03 / cam.farZ);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        visibleEdges();
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        haloPass_ = false;
    }
    visibleEdges();
    if (halo) {
        // put back the depth the faces had, for what is drawn after (transparent faces, selection)
        glClear(GL_DEPTH_BUFFER_BIT);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
        glEnable(GL_POLYGON_OFFSET_FILL);
        glPolygonOffset(1.0f, 2.0f);
        if (!st.xray) drawFaces(items, false, false);
        glDisable(GL_POLYGON_OFFSET_FILL);
        visibleEdges();   // depth only: their colour is already drawn, gaps included
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    }
    if (st.showHidden) drawEdges(items, 2, 1.0f, QColor(110, 110, 110), 2, false);
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
    lastRenderMs = frameClock.nsecsElapsed() / 1e6;
}

}  // namespace gable
