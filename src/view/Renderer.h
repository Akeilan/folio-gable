#pragma once
#include "core/Model.h"
#include "view/Camera.h"
#include "view/Overlay.h"
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <map>
#include <memory>
#include <unordered_map>

namespace gable {

class Document;

class Renderer : protected QOpenGLFunctions_3_3_Core {
public:
    Renderer();
    ~Renderer();
    bool init(QString* err);
    void cleanup();

    struct Params {
        Document* doc = nullptr;
        Camera* cam = nullptr;
        int w = 0, h = 0;          // device pixels
        double dpr = 1;
        const Overlay* overlay = nullptr;
        std::vector<EntRef> hover;  // pre-selection highlight (active entities)
        bool hoverInstance = false;
        bool drawOverlay = true;
        bool transparentBackground = false;
        int defaultFbo = 0;
        bool forExport = false;
    };
    void render(const Params& p);

    // Section cut geometry, world space (for picking/inference reuse)
    struct SectionCache {
        uint64_t key = 0;
        Plane plane;
        std::vector<Vec3> lines;  // pairs
        std::vector<Vec3> fill;   // triangles
    };

private:
    struct Batch { int start = 0, count = 0; int matF = -1, matB = -1; bool transparent = false; };
    struct Mesh {
        uint64_t rev = 0, sig = 0;
        GLuint vaoF = 0, vboF = 0, vaoE = 0, vboE = 0;
        std::vector<Batch> batches;
        int nEdgeVerts = 0;
        std::unordered_map<int, std::pair<int, int>> faceRange;  // face -> (first, count)
        std::unordered_map<int, int> edgeFirst;                  // edge -> first vertex
        bool used = false;
    };
    std::map<int, Mesh> meshes_;  // key: def index, -1 = root
    std::map<qint64, std::unique_ptr<QOpenGLTexture>> textures_;  // key: material index
    std::map<qint64, uint64_t> textureRev_;

    QOpenGLShaderProgram faceProg_, lineProg_, stippleProg_, bgProg_, depthProg_, flatProg_;
    GLuint dynVao_ = 0, dynVbo_ = 0, triVao_ = 0, triVbo_ = 0, bgVao_ = 0;
    GLuint shadowFbo_ = 0, shadowTex_ = 0;
    int shadowSize_ = 4096;
    bool inited_ = false;
    SectionCache section_;

    // per-frame state
    const Params* P = nullptr;
    Document* D = nullptr;
    Mat4 vp_, lightVP_;
    bool hasClip_ = false;
    Plane clip_;
    Vec3 lightDir_;
    bool shadowsOn_ = false;

    Mesh& mesh(int def);
    void buildMesh(Mesh& m, const Entities& E, uint64_t sig);
    uint64_t styleSig() const;
    QOpenGLTexture* texture(int mat);

    struct DrawItem { int def; Mat4 xf; int inheritMat; bool faded; bool activeCtx; };
    void collect(int def, const Mat4& xf, int inheritMat, int depth, int pathPos, bool onPath, std::vector<DrawItem>& out);
    void drawFaces(const std::vector<DrawItem>& items, bool transparentPass, bool xray);
    void drawEdges(const std::vector<DrawItem>& items, int mode, float width, const QColor& col, int dash, bool backEdges);
    void drawShadowMap(const std::vector<DrawItem>& items, const BBox& scene);
    void drawBackground();
    void drawSelection(const std::vector<DrawItem>& items);
    void drawGround(const BBox& scene);
    void drawSection(const std::vector<DrawItem>& items);

    // immediate-mode style helpers
    struct LV { float p[3]; float c[4]; float n1[3]; float n2[3]; float f; };
    std::vector<LV> lineBuf_;
    void pushLine(const Vec3& a, const Vec3& b, const QColor& c);
    void flushLines(float width, int dash, bool depthTest, bool depthWrite = false);
    std::vector<float> triBuf_;
    void pushTri(const Vec3& a, const Vec3& b, const Vec3& c);
    void flushTris(const QColor& c, bool depthTest, bool stipple = false, bool depthWrite = false);
    void setLineUniforms(float width, int dash, int mode, const Mat4& model);
};

}  // namespace gable
