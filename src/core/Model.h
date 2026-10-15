// Gable — model data structures.
#pragma once
#include "math/Vec.h"
#include <QString>
#include <QByteArray>
#include <QImage>
#include <QColor>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>

namespace gable {

uint64_t nextRevision();

enum class EntKind : uint8_t { None, Vertex, Edge, Face, Instance, Guide, Dimension, Text, Section };

struct EntRef {
    EntKind kind = EntKind::None;
    int id = -1;
    bool operator==(const EntRef& o) const { return kind == o.kind && id == o.id; }
    bool operator!=(const EntRef& o) const { return !(*this == o); }
    bool operator<(const EntRef& o) const { return kind != o.kind ? kind < o.kind : id < o.id; }
    bool valid() const { return kind != EntKind::None && id >= 0; }
};

struct Vertex {
    Vec3 p;
    std::vector<int> edges;
    bool alive = true;
};

// A curve groups edges drawn by one arc/circle/polygon/freehand command so they select together.
struct Curve {
    bool isArc = false;   // circle or arc (has center/radius)
    bool closed = false;  // circle / polygon
    bool polygon = false;
    Vec3 center, normal{0, 0, 1}, xaxis{1, 0, 0};
    double radius = 0, startAng = 0, endAng = 0;
    int segments = 0;
    bool alive = true;
};

struct Edge {
    int v[2] = {-1, -1};
    std::vector<int> faces;
    bool soft = false, smooth = false, hidden = false;
    int tag = 0;
    int material = -1;
    int curve = -1;
    bool alive = true;
    int other(int vi) const { return v[0] == vi ? v[1] : v[0]; }
};

// Texture placement on a face: uv = (p·uAxis + uOff, p·vAxis + vOff) / texture size.
struct TexMap {
    bool custom = false;
    Vec3 uAxis{1, 0, 0}, vAxis{0, 1, 0};
    double uOff = 0, vOff = 0;
};

struct Face {
    std::vector<int> outer;                 // CCW about plane.n
    std::vector<std::vector<int>> holes;    // CW about plane.n
    Plane plane;
    int matFront = -1, matBack = -1;
    TexMap texFront, texBack;
    bool hidden = false;
    int tag = 0;
    bool alive = true;
};

struct Instance {
    int def = -1;
    Mat4 xf;
    QString name;
    int tag = 0;
    int material = -1;
    bool hidden = false, locked = false;
    bool alive = true;
};

struct Guide {
    bool isPoint = false;
    Vec3 p, dir;
    bool hidden = false;
    int tag = 0;
    bool alive = true;
};

struct Dimension {
    bool radial = false;   // radius/diameter dimension on a curve
    bool diameter = false;
    Vec3 a, b;             // measured points (radial: center, point on arc)
    Vec3 offset;           // offset of the dimension line from a-b
    QString textOverride;
    bool hidden = false;
    int tag = 0;
    bool alive = true;
};

struct Text {
    Vec3 anchor;
    Vec3 leader;           // offset of text from anchor (model units); zero + screenText=true => screen text
    bool screenText = false;
    double sx = 0, sy = 0; // screen position (fractions) for screen text
    QString text;
    bool hidden = false;
    int tag = 0;
    bool alive = true;
};

struct SectionPlane {
    Plane plane;
    QString name;
    bool active = true;
    bool hidden = false;
    int tag = 0;
    bool alive = true;
};

class Entities {
public:
    std::vector<Vertex> verts;
    std::vector<Edge> edges;
    std::vector<Face> faces;
    std::vector<Curve> curves;
    std::vector<Instance> insts;
    std::vector<Guide> guides;
    std::vector<Dimension> dims;
    std::vector<Text> texts;
    std::vector<SectionPlane> sections;
    uint64_t rev = 1;

    void touch() { rev = nextRevision(); }

    // --- low level ---
    int addVertex(const Vec3& p);
    int findVertex(const Vec3& p, double tol = kTol) const;
    int vertexAt(const Vec3& p);  // find or create
    void moveVertex(int v, const Vec3& p);
    int findEdge(int a, int b) const;
    int addEdge(int a, int b);  // returns existing if already present; -1 if degenerate
    void removeEdge(int e);     // also removes faces bounded by it
    void removeFace(int f);
    void removeVertexIfOrphan(int v);
    int addFace(const std::vector<int>& outer, const std::vector<std::vector<int>>& holes, const Plane& plane);
    void linkFace(int f);
    void unlinkFace(int f);
    void clear();

    // --- queries ---
    Vec3 edgeMid(int e) const { return (verts[edges[e].v[0]].p + verts[edges[e].v[1]].p) * 0.5; }
    double edgeLength(int e) const { return (verts[edges[e].v[0]].p - verts[edges[e].v[1]].p).length(); }
    Vec3 edgeDir(int e) const { return (verts[edges[e].v[1]].p - verts[edges[e].v[0]].p).normalized(); }
    double faceArea(int f) const;
    Vec3 faceCentroid(int f) const;
    bool pointInFace(int f, const Vec3& p, double tol = kTol) const;   // p assumed on plane
    std::vector<int> faceEdges(int f) const;
    std::vector<int> faceVerts(int f) const;  // all loop vertices
    BBox bbox() const;  // loose geometry only (not instances)
    bool empty() const;
    int aliveCount(EntKind k) const;

    // Rebuild the spatial hash (call after bulk vertex moves).
    void invalidateIndex() const { indexDirty_ = true; }

private:
    mutable std::unordered_map<int64_t, std::vector<int>> grid_;
    mutable bool indexDirty_ = true;
    void ensureIndex() const;
    static int64_t cellKey(int64_t x, int64_t y, int64_t z);
    void indexInsert(int v) const;
    static constexpr double kCell = 1.0;  // mm
};

struct Material {
    QString name;
    QColor color{200, 200, 200};
    double alpha = 1.0;
    bool hasTexture = false;
    QByteArray texturePng;   // embedded image data
    double texW = 1000, texH = 1000;  // texture size in model units (mm)
    QImage image() const;
    bool colorize = false;
    uint64_t rev = 1;
};

struct Tag {
    QString name;
    bool visible = true;
    QColor color{128, 128, 128};
    int dash = 0;  // 0 solid, 1 dashed, 2 dotted, 3 dash-dot
};

struct ComponentDef {
    QString name, description;
    Entities ents;
    bool isGroup = false;
    bool alive = true;
    bool alwaysFaceCamera = false;
    bool glue = false;
    bool cutsOpening = false;
};

struct CameraState {
    Vec3 eye{-6000, -9000, 5000}, target{0, 0, 0}, up{0, 0, 1};
    bool perspective = true;
    double fov = 35.0;            // vertical, degrees
    double orthoHeight = 10000;   // visible height in parallel projection
    bool twoPoint = false;
};

enum class FaceStyle { Wireframe = 0, HiddenLine, Shaded, ShadedTextured, Monochrome };

struct Style {
    FaceStyle faceStyle = FaceStyle::ShadedTextured;
    bool xray = false;
    bool backEdges = false;
    bool edges = true;
    bool profiles = true;
    int profileWidth = 3;
    bool depthCue = false;
    bool extension = false;
    bool endpoints = false;
    bool jitter = false;
    bool colorByAxis = false;
    bool colorByTag = false;
    QColor edgeColor{0, 0, 0};
    QColor frontColor{255, 255, 255};
    QColor backColor{164, 178, 187};
    QColor background{255, 255, 255};
    QColor sky{198, 222, 240};
    QColor ground{212, 206, 190};
    bool showSky = true, showGround = false;
    double groundTransparency = 0.5;
    bool showGuides = true;
    bool showHidden = false;      // View > Hidden Geometry
    bool showAxes = true;
    bool showSectionPlanes = true, showSectionCuts = true, sectionFill = true;
    QColor sectionFillColor{40, 40, 40};
    bool fog = false;
    double fogStart = 0.3, fogEnd = 1.0;
    QColor fogColor{255, 255, 255};
    QString name = "Default";
};

struct ShadowSettings {
    bool enabled = false;
    int month = 6, day = 21;
    double hour = 13.5;          // local solar-ish clock hours
    double utcOffset = 1.0;
    double latitude = 51.9244, longitude = 4.4777;  // Rotterdam default
    double northAngle = 0;       // degrees, north direction from +Y
    double light = 80, dark = 45;  // percentages
    bool sunForShading = false;
    bool onFaces = true, onGround = true, fromEdges = false;
    Vec3 sunDirection() const;   // direction TO the sun, model coordinates
};

enum class LengthUnit { Millimeter = 0, Centimeter, Meter, Inch, Foot, ArchitecturalInch };

struct UnitSettings {
    LengthUnit unit = LengthUnit::Millimeter;
    int precision = 0;        // decimal places
    bool showUnits = true;
    bool lengthSnap = false;
    double snapLength = 1;
    int anglePrecision = 1;
    double angleSnap = 15;    // degrees
    bool angleSnapOn = true;
};

struct Scene {
    QString name, description;
    CameraState cam;
    std::vector<bool> tagVisible;
    Style style;
    bool hasStyle = true;
    ShadowSettings shadow;
    std::vector<std::pair<int, bool>> hiddenInstances;
    std::vector<int> activeSections;
};

class Model {
public:
    Model();
    std::vector<Material> materials;
    std::vector<Tag> tags;
    std::vector<ComponentDef> defs;
    Entities root;
    std::vector<Scene> scenes;
    Style style;
    ShadowSettings shadow;
    UnitSettings units;
    CameraState camera;
    QString author, description;
    Vec3 axesOrigin;  // drawing axes (Axes tool)
    Vec3 axesX{1, 0, 0}, axesY{0, 1, 0}, axesZ{0, 0, 1};

    void reset();
    Entities& ents(int defIndex) { return defIndex < 0 ? root : defs[defIndex].ents; }
    int addMaterial(const QString& name, const QColor& c);
    int findMaterial(const QString& name) const;
    QString uniqueMaterialName(const QString& base) const;
    QString uniqueDefName(const QString& base) const;
    int addDef(const QString& name, bool group);
    int instanceCount(int def) const;
    BBox defBBox(int def) const;
    BBox entitiesBBox(const Entities& e, bool includeHidden = true) const;
    BBox worldBBox() const { return entitiesBBox(root); }
    void purgeUnused();
};

// Converts a length (mm) to display text and parses user input.
QString formatLength(double mm, const UnitSettings& u, bool withUnit = true);
QString formatArea(double mm2, const UnitSettings& u);
QString formatVolume(double mm3, const UnitSettings& u);
QString formatAngle(double rad, const UnitSettings& u);
bool parseLength(const QString& s, const UnitSettings& u, double& mm);

}  // namespace gable
