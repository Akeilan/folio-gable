// Geometry engine tests (headless).
#include "core/Geometry.h"
#include "core/Serializer.h"
#include "core/Solids.h"
#include "core/FileFormats.h"
#include "core/Sandbox.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <cstdio>

using namespace gable;
static int g_fail = 0, g_pass = 0;
#define CHECK(c) do { if (c) ++g_pass; else { ++g_fail; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } } while (0)
#define NEAR(a, b, eps) CHECK(std::fabs((a) - (b)) < (eps))

static int faces(const Entities& E) { return E.aliveCount(EntKind::Face); }
static int edges(const Entities& E) { return E.aliveCount(EntKind::Edge); }

static std::vector<Vec3> rect(double x0, double y0, double x1, double y1, double z = 0) {
    return {Vec3(x0, y0, z), Vec3(x1, y0, z), Vec3(x1, y1, z), Vec3(x0, y1, z)};
}
static int findFaceAt(const Entities& E, const Vec3& p, const Vec3& n) {
    for (int f = 0; f < (int)E.faces.size(); ++f)
        if (E.faces[f].alive && std::fabs(E.faces[f].plane.n.dot(n)) > 0.999 && std::fabs(E.faces[f].plane.dist(p)) < 1e-6 &&
            E.pointInFace(f, p))
            return f;
    return -1;
}
static void box(Entities& E, double x0, double y0, double x1, double y1, double h, double z = 0) {
    geom::addPolyline(E, rect(x0, y0, x1, y1, z), true);
    int f = findFaceAt(E, Vec3((x0 + x1) / 2, (y0 + y1) / 2, z), Vec3(0, 0, 1));
    // ground faces point down; pull "up" = negative along normal
    double d = E.faces[f].plane.n.z < 0 ? -h : h;
    geom::pushPull(E, f, d, false);
}

static void testLines() {
    Entities E;
    geom::addLine(E, Vec3(0, 0, 0), Vec3(1000, 0, 0));
    geom::addLine(E, Vec3(1000, 0, 0), Vec3(1000, 1000, 0));
    geom::addLine(E, Vec3(1000, 1000, 0), Vec3(0, 1000, 0));
    CHECK(faces(E) == 0);
    geom::addLine(E, Vec3(0, 1000, 0), Vec3(0, 0, 0));
    CHECK(faces(E) == 1);
    int f = findFaceAt(E, Vec3(500, 500, 0), Vec3(0, 0, 1));
    CHECK(f >= 0);
    if (f >= 0) { NEAR(E.faceArea(f), 1e6, 1e-3); CHECK(E.faces[f].plane.n.z < 0); }
    // split by diagonal
    geom::addLine(E, Vec3(0, 0, 0), Vec3(1000, 1000, 0));
    CHECK(faces(E) == 2);
    // line crossing the diagonal from edge to edge: 4 faces
    geom::addLine(E, Vec3(0, 1000, 0), Vec3(1000, 0, 0));
    CHECK(faces(E) == 4);
    CHECK(edges(E) == 8);
    // erase the second diagonal halves -> merges back to 2
    std::vector<int> es;
    for (int e = 0; e < (int)E.edges.size(); ++e) {
        if (!E.edges[e].alive) continue;
        Vec3 a = E.verts[E.edges[e].v[0]].p, b = E.verts[E.edges[e].v[1]].p;
        Vec3 m = (a + b) * 0.5;
        if (std::fabs(m.x + m.y - 1000) < 1e-6 && !(std::fabs(m.x - 500) < 1e-6 && std::fabs(m.y - 500) < 1e-6 && false)) {
            // edges on line x+y=1000
            if (std::fabs((a.x + a.y) - 1000) < 1e-6 && std::fabs((b.x + b.y) - 1000) < 1e-6) es.push_back(e);
        }
    }
    CHECK(es.size() == 2);
    geom::eraseEdges(E, es);
    CHECK(faces(E) == 2);
    std::printf("lines ok: faces=%d edges=%d\n", faces(E), edges(E));
}

static void testHole() {
    Entities E;
    geom::addPolyline(E, rect(0, 0, 1000, 1000), true);
    CHECK(faces(E) == 1);
    geom::addPolyline(E, rect(300, 300, 600, 600), true);
    CHECK(faces(E) == 2);
    int outer = findFaceAt(E, Vec3(100, 100, 0), Vec3(0, 0, 1));
    int inner = findFaceAt(E, Vec3(400, 400, 0), Vec3(0, 0, 1));
    CHECK(outer >= 0 && inner >= 0 && outer != inner);
    if (outer >= 0) { CHECK(E.faces[outer].holes.size() == 1); NEAR(E.faceArea(outer), 1e6 - 9e4, 1e-3); }
    // Rect drawn around an existing face: big face gets a hole
    Entities F;
    geom::addPolyline(F, rect(300, 300, 600, 600), true);
    geom::addPolyline(F, rect(0, 0, 1000, 1000), true);
    CHECK(faces(F) == 2);
    int big = findFaceAt(F, Vec3(100, 100, 0), Vec3(0, 0, 1));
    CHECK(big >= 0 && F.faces[big].holes.size() == 1);
    // Floating line inside a face does not split it
    geom::addLine(F, Vec3(50, 50, 0), Vec3(200, 60, 0));
    CHECK(faces(F) == 2);
    // Rect touching the outer boundary splits the face
    Entities G;
    geom::addPolyline(G, rect(0, 0, 1000, 1000), true);
    geom::addPolyline(G, {Vec3(0, 200, 0), Vec3(300, 200, 0), Vec3(300, 500, 0), Vec3(0, 500, 0)}, false);
    CHECK(faces(G) == 2);
    std::printf("holes ok\n");
}

static void testPushPull() {
    Entities E;
    box(E, 0, 0, 1000, 2000, 500);
    CHECK(faces(E) == 6);
    CHECK(edges(E) == 12);
    CHECK(geom::isSolid(E));
    NEAR(geom::volume(E), 1e9, 1);
    // stretch: pull the top up by 500
    int top = findFaceAt(E, Vec3(500, 1000, 500), Vec3(0, 0, 1));
    CHECK(top >= 0 && E.faces[top].plane.n.z > 0);
    geom::pushPull(E, top, 500, false);
    CHECK(faces(E) == 6);
    NEAR(geom::volume(E), 2e9, 1);
    // pocket
    geom::addPolyline(E, rect(200, 200, 800, 800, 1000), true);
    CHECK(faces(E) == 7);
    int in = findFaceAt(E, Vec3(500, 500, 1000), Vec3(0, 0, 1));
    CHECK(in >= 0);
    geom::pushPull(E, in, -300, false);
    CHECK(geom::isSolid(E));
    NEAR(geom::volume(E), 2e9 - 0.36e6 * 300, 1);
    std::printf("pocket faces=%d\n", faces(E));
    CHECK(faces(E) == 11);
    // push through: hole
    Entities H;
    box(H, 0, 0, 1000, 1000, 200);
    geom::addPolyline(H, rect(300, 300, 700, 700, 200), true);
    int hf = findFaceAt(H, Vec3(500, 500, 200), Vec3(0, 0, 1));
    geom::pushPull(H, hf, -200, false);
    CHECK(geom::isSolid(H));
    NEAR(geom::volume(H), (1e6 - 1.6e5) * 200, 1);
    CHECK(faces(H) == 10);
    std::printf("hole-through faces=%d vol=%.0f\n", faces(H), geom::volume(H));
    // pull a face out of a side (extrusion with merge)
    Entities S;
    box(S, 0, 0, 1000, 1000, 1000);
    geom::addPolyline(S, {Vec3(1000, 200, 200), Vec3(1000, 800, 200), Vec3(1000, 800, 800), Vec3(1000, 200, 800)}, true);
    int sf = findFaceAt(S, Vec3(1000, 500, 500), Vec3(1, 0, 0));
    CHECK(sf >= 0);
    geom::pushPull(S, sf, 400, false);
    CHECK(geom::isSolid(S));
    NEAR(geom::volume(S), 1e9 + 0.36e6 * 400, 1);
    // notch at edge: rect on top touching the side edge, pushed down
    Entities N;
    box(N, 0, 0, 1000, 1000, 1000);
    geom::addPolyline(N, {Vec3(1000, 300, 1000), Vec3(700, 300, 1000), Vec3(700, 700, 1000), Vec3(1000, 700, 1000)}, false);
    int nf = findFaceAt(N, Vec3(850, 500, 1000), Vec3(0, 0, 1));
    CHECK(nf >= 0);
    geom::pushPull(N, nf, -400, false);
    CHECK(geom::isSolid(N));
    NEAR(geom::volume(N), 1e9 - 0.12e6 * 400, 1);
    std::printf("notch faces=%d solid=%d\n", faces(N), geom::isSolid(N));
}

static void testCylinder() {
    Entities E;
    std::vector<Vec3> c;
    int n = 24;
    for (int i = 0; i < n; ++i) c.push_back(Vec3(std::cos(2 * kPi * i / n), std::sin(2 * kPi * i / n), 0) * 500);
    Curve cv; cv.isArc = true; cv.closed = true; cv.radius = 500; cv.segments = n;
    geom::addPolyline(E, c, true, &cv);
    CHECK(faces(E) == 1);
    int f = findFaceAt(E, Vec3(0, 0, 0), Vec3(0, 0, 1));
    geom::pushPull(E, f, -1000, false);
    CHECK(geom::isSolid(E));
    CHECK(faces(E) == n + 2);
    int soft = 0;
    for (auto& e : E.edges) if (e.alive && e.soft) ++soft;
    CHECK(soft == n);
    std::printf("cylinder ok soft=%d\n", soft);
}

static void testSaveLoad() {
    Model m;
    box(m.root, 0, 0, 1000, 1000, 1000);
    int mat = m.addMaterial("Brick", QColor(150, 60, 40));
    m.root.faces[0].matFront = mat;
    QByteArray d = saveModel(m);
    Model m2;
    QString err;
    CHECK(loadModel(m2, d, &err));
    CHECK(faces(m2.root) == 6);
    CHECK(m2.materials.size() == 1);
    CHECK(geom::isSolid(m2.root));
    NEAR(geom::volume(m2.root), 1e9, 1);
}

static void testSolids() {
    Model m;
    int d1 = m.addDef("A", true), d2 = m.addDef("B", true);
    box(m.defs[d1].ents, 0, 0, 1000, 1000, 1000);
    box(m.defs[d2].ents, 0, 0, 1000, 1000, 1000);
    Instance a; a.def = d1;
    Instance b; b.def = d2; b.xf = Mat4::translation(Vec3(500, 500, 500));
    m.root.insts.push_back(a);
    m.root.insts.push_back(b);
    CHECK(instanceIsSolid(m, m.root.insts[0]));
    QString err;
    std::vector<int> res;
    CHECK(solidOperation(m, m.root, 1, 0, SolidOp::Union, &err, &res));
    CHECK(res.size() == 1);
    if (!res.empty()) {
        double v = instanceVolume(m, m.root.insts[res[0]]);
        NEAR(v, 2e9 - 1.25e8, 10);
        std::printf("union volume %.0f err=%s\n", v, qPrintable(err));
    }
    // subtract
    Model m2;
    int e1 = m2.addDef("A", true), e2 = m2.addDef("B", true);
    box(m2.defs[e1].ents, 0, 0, 1000, 1000, 1000);
    box(m2.defs[e2].ents, 0, 0, 1000, 1000, 1000);
    Instance a2; a2.def = e1;
    Instance b2; b2.def = e2; b2.xf = Mat4::translation(Vec3(500, 500, 500));
    m2.root.insts.push_back(a2);
    m2.root.insts.push_back(b2);
    res.clear();
    CHECK(solidOperation(m2, m2.root, 1, 0, SolidOp::Subtract, &err, &res));
    if (!res.empty()) {
        double v = instanceVolume(m2, m2.root.insts[res[0]]);
        NEAR(v, 1e9 - 1.25e8, 10);
        std::printf("subtract volume %.0f\n", v);
    }
}

static void testFollowMe() {
    Entities E;
    // profile: square in XZ plane at origin; path along +Y then +X
    geom::addPolyline(E, {Vec3(-50, 0, 0), Vec3(50, 0, 0), Vec3(50, 0, 100), Vec3(-50, 0, 100)}, true);
    int f = findFaceAt(E, Vec3(0, 0, 50), Vec3(0, 1, 0));
    CHECK(f >= 0);
    std::vector<Vec3> path = {Vec3(0, 0, 0), Vec3(0, 1000, 0), Vec3(1000, 1000, 0)};
    CHECK(geom::followMe(E, f, path, false));
    CHECK(geom::isSolid(E));
    std::printf("followme faces=%d vol=%.0f solid=%d\n", faces(E), geom::volume(E), geom::isSolid(E));
    NEAR(std::fabs(geom::volume(E)), 100.0 * 100 * (1000 + 1000), 1e3);
}

static void testIntersect() {
    Entities E;
    box(E, 0, 0, 1000, 1000, 1000);
    // vertical plane crossing the box
    geom::addPolyline(E, {Vec3(500, -500, -500), Vec3(500, 1500, -500), Vec3(500, 1500, 1500), Vec3(500, -500, 1500)}, true);
    int before = faces(E);
    std::vector<int> all;
    for (int f = 0; f < (int)E.faces.size(); ++f) if (E.faces[f].alive) all.push_back(f);
    int n = geom::intersectFaces(E, all, {});
    std::printf("intersect segs=%d faces %d -> %d\n", n, before, faces(E));
    CHECK(faces(E) > before);
}

static void testExport() {
    QTemporaryDir dir;
    Model m;
    box(m.root, 0, 0, 1000, 1000, 1000);
    QString err;
    ExportOptions eo;
    for (QString ext : {"obj", "stl", "dae", "dxf"}) {
        QString p = dir.filePath("t." + ext);
        CHECK(exportFile(m, p, eo, &err));
        Model m2;
        ImportOptions io;
        int idx = importFile(m2, m2.root, p, io, &err);
        CHECK(idx >= 0);
        if (idx >= 0) {
            Entities flat;
            flattenInstance(m2, m2.root.insts[idx], Mat4(), flat);
            std::printf("%s roundtrip: faces=%d solid=%d vol=%.0f %s\n", qPrintable(ext), faces(flat), geom::isSolid(flat),
                        geom::volume(flat), qPrintable(err));
            if (ext != "dxf") { CHECK(faces(flat) == 6); NEAR(geom::volume(flat), 1e9, 10); }
        }
    }
}

void testHouse();
void testSandbox();
static void testSTL() {
    QTemporaryDir td;
    Model m;
    box(m.root, 0, 0, 1000, 600, 400);
    // reverse one face: export must still be outward and watertight
    for (int f = 0; f < (int)m.root.faces.size(); ++f) if (m.root.faces[f].alive) { geom::reverseFace(m.root, f); break; }
    for (bool bin : {true, false}) {
        ExportOptions eo;
        eo.binary = bin;
        QString path = td.path() + (bin ? "/b.stl" : "/a.stl");
        QString err;
        CHECK(exportFile(m, path, eo, &err));
        CHECK(err.isEmpty());
        Model m2;
        ImportOptions io;
        int n = importFile(m2, m2.root, path, io, &err);
        CHECK(n >= 0);
        // the import may wrap the mesh in a group; measure whatever came in
        Entities* E = &m2.root;
        for (auto& in : m2.root.insts) if (in.alive && in.def >= 0) E = &m2.defs[in.def].ents;
        int fc = 0;
        for (auto& f : E->faces) if (f.alive) ++fc;
        CHECK(fc == 6);
        CHECK(geom::isSolid(*E));
        NEAR(geom::volume(*E), 1000.0 * 600 * 400, 1);
        std::printf("stl %s: faces=%d vol=%.0f\n", bin ? "binary" : "ascii", fc, geom::volume(*E));
    }
    // empty model fails cleanly
    Model e;
    QString err;
    CHECK(!exportFile(e, td.path() + "/e.stl", ExportOptions(), &err) && !err.isEmpty());
    // open box reports open edges
    Model o;
    box(o.root, 0, 0, 100, 100, 100);
    for (int f = 0; f < (int)o.root.faces.size(); ++f) if (o.root.faces[f].alive) { o.root.removeFace(f); break; }
    CHECK(exportFile(o, td.path() + "/o.stl", ExportOptions(), &err) && err.contains("open edge"));
}

static void testSolidsCoplanar() {
    // boxes resting on the same ground / sharing walls: coplanar overlapping faces
    SolidOp ops[] = {SolidOp::Union, SolidOp::Subtract, SolidOp::Intersect, SolidOp::Trim};
    Vec3 shifts[] = {Vec3(500, 500, 0), Vec3(500, 0, 0), Vec3(1000, 0, 0)};
    double want[3][4] = {{1.75e9, 0.75e9, 0.25e9, 0.75e9}, {1.5e9, 0.5e9, 0.5e9, 0.5e9}, {2e9, 1e9, 0, 1e9}};
    for (int k = 0; k < 4; ++k)
        for (int sh = 0; sh < 3; ++sh) {
            Model m;
            int d1 = m.addDef("A", true), d2 = m.addDef("B", true);
            box(m.defs[d1].ents, 0, 0, 1000, 1000, 1000);
            box(m.defs[d2].ents, 0, 0, 1000, 1000, 1000);
            Instance a; a.def = d1;
            Instance b; b.def = d2; b.xf = Mat4::translation(shifts[sh]);
            m.root.insts.push_back(a);
            m.root.insts.push_back(b);
            QString err;
            std::vector<int> res;
            bool ok = solidOperation(m, m.root, 1, 0, ops[k], &err, &res);
            if (want[sh][k] == 0) { CHECK(ok); continue; }
            CHECK(ok && !res.empty());
            if (res.empty()) continue;
            double v = instanceVolume(m, m.root.insts[res[0]]);
            NEAR(v, want[sh][k], 1e3);
            if (k == 0 && sh == 0) {
                const Entities& R = m.defs[m.root.insts[res[0]].def].ents;
                CHECK(R.aliveCount(EntKind::Face) == 10);  // 8-sided prism, coplanar pieces merged
            }
        }
}

static void testTextures() {
    // pins -> mapping -> points, on a sloped plane
    Plane pl(Vec3(0.3, -0.4, 0.86).normalized(), Vec3(100, 200, 300));
    Vec3 p0 = pl.project(Vec3(10, 20, 0)), p1 = pl.project(Vec3(900, 120, 0)), p2 = pl.project(Vec3(-50, 700, 0));
    TexMap t;
    CHECK(geom::texMapFromPins(pl, p0, p1, p2, 1000, 500, t));
    auto uv = [&](const Vec3& p) { return Vec2(p.dot(t.uAxis) + t.uOff, p.dot(t.vAxis) + t.vOff); };
    NEAR(uv(p0).x, 0, 1e-6); NEAR(uv(p0).y, 0, 1e-6);
    NEAR(uv(p1).x, 1000, 1e-6); NEAR(uv(p1).y, 0, 1e-6);
    NEAR(uv(p2).x, 0, 1e-6); NEAR(uv(p2).y, 500, 1e-6);
    CHECK((geom::texPoint(pl, t, 1000, 0) - p1).length() < 1e-6);
    CHECK((geom::texPoint(pl, t, 0, 500) - p2).length() < 1e-6);
    // collinear pins are refused
    CHECK(!geom::texMapFromPins(pl, p0, p1, p0 + (p1 - p0) * 2, 1000, 500, t));
    // a moved mapping gives the moved point the same coordinates
    CHECK(geom::texMapFromPins(pl, p0, p1, p2, 1000, 500, t));
    Mat4 m = Mat4::translation(Vec3(50, -70, 20)) * Mat4::rotation(Vec3(0, 0, 1), 0.7);
    TexMap tm = geom::transformTexMap(t, m);
    Vec3 q = p1 + (p2 - p1) * 0.3;
    Vec2 a = uv(q);
    Vec3 mq = m.point(q);
    NEAR(mq.dot(tm.uAxis) + tm.uOff, a.x, 1e-6);
    NEAR(mq.dot(tm.vAxis) + tm.vOff, a.y, 1e-6);
    // a face moved as a whole keeps its texture on it
    Entities E;
    geom::addPolyline(E, rect(0, 0, 2000, 1000), true);
    int f = findFaceAt(E, Vec3(1000, 500, 0), Vec3(0, 0, 1));
    CHECK(f >= 0);
    if (f >= 0) {
        Face& F = E.faces[f];
        CHECK(geom::texMapFromPins(F.plane, Vec3(100, 100, 0), Vec3(600, 100, 0), Vec3(100, 400, 0), 500, 300, F.texFront));
        TexMap before = F.texFront;
        Vec3 c(700, 300, 0);
        double u0 = c.dot(before.uAxis) + before.uOff;
        std::vector<int> vs = E.faceVerts(f);
        Mat4 mv = Mat4::translation(Vec3(300, 200, 50));
        geom::transformVertices(E, vs, mv, false);
        const TexMap& after = E.faces[f].texFront;
        Vec3 mc = mv.point(c);
        NEAR(mc.dot(after.uAxis) + after.uOff, u0, 1e-6);
        // the projected flag survives saving
        E.faces[f].texFront.projected = true;
        Model M;
        M.root = E;
        QByteArray data = saveModel(M);
        Model R;
        QString err;
        CHECK(loadModel(R, data, &err));
        bool proj = false;
        for (auto& G : R.root.faces) if (G.alive && G.texFront.projected) proj = true;
        CHECK(proj);
    }
    // the same through a healing move of a whole box
    {
        Entities B;
        box(B, 0, 0, 2000, 1500, 1000);
        int tf = findFaceAt(B, Vec3(1000, 750, 1000), Vec3(0, 0, 1));
        CHECK(tf >= 0);
        if (tf >= 0) {
            B.faces[tf].texFront = geom::effectiveTexMap(B.faces[tf], false);
            Vec3 c(700, 400, 1000);
            double u0 = c.dot(B.faces[tf].texFront.uAxis) + B.faces[tf].texFront.uOff;
            std::vector<int> all;
            for (int v = 0; v < (int)B.verts.size(); ++v) if (B.verts[v].alive) all.push_back(v);
            geom::transformVertices(B, all, Mat4::translation(Vec3(300, 0, 0)), true);
            int tf2 = findFaceAt(B, Vec3(1300, 750, 1000), Vec3(0, 0, 1));
            CHECK(tf2 >= 0);
            if (tf2 >= 0) {
                const TexMap& t = B.faces[tf2].texFront;
                Vec3 mc = c + Vec3(300, 0, 0);
                NEAR(mc.dot(t.uAxis) + t.uOff, u0, 1e-6);
                if (std::fabs(mc.dot(t.uAxis) + t.uOff - u0) > 1e-6) std::printf("   tf %d -> %d custom %d\n", tf, tf2, t.custom);
            }
        }
    }
    std::printf("textures: placement ok\n");
}

// A camera seen through its picture: project known edges of a box, put the grips on them, and
// the solver must give back the camera.
// glTF/GLB/3MF: files written for other programs. The structure is checked here; set
// GABLE_KEEP_EXPORTS to a folder to keep them for outside validators.
static void testExport3D() {
    QTemporaryDir td;
    QString dir = qEnvironmentVariableIsSet("GABLE_KEEP_EXPORTS") ? qEnvironmentVariable("GABLE_KEEP_EXPORTS") : td.path();
    Model m;
    box(m.root, 0, 0, 2000, 1000, 500);
    box(m.root, 3000, 0, 3600, 600, 900);   // a second, separate solid
    int red = m.addMaterial("Red", QColor(200, 30, 30));
    int glass = m.addMaterial("Glass", QColor(120, 180, 220));
    m.materials[glass].alpha = 0.4;
    int tex = m.addMaterial("Checker", QColor(255, 255, 255));
    {
        QImage img(32, 32, QImage::Format_RGB32);
        img.fill(Qt::white);
        for (int y = 0; y < 16; ++y) for (int x = 0; x < 16; ++x) img.setPixel(x, y, 0xff2040c0);
        QBuffer b(&m.materials[tex].texturePng);
        b.open(QIODevice::WriteOnly);
        img.save(&b, "PNG");
        m.materials[tex].hasTexture = true;
        m.materials[tex].texW = m.materials[tex].texH = 500;
    }
    int nf = 0;
    for (auto& F : m.root.faces) if (F.alive) F.matFront = (nf++ % 3 == 0) ? red : (nf % 3 == 0 ? glass : tex);
    QString err;
    ExportOptions eo;
    eo.exportEdges = true;
    // glTF: JSON with the data inside
    CHECK(exportFile(m, dir + "/t.gltf", eo, &err));
    QFile f(dir + "/t.gltf");
    CHECK(f.open(QIODevice::ReadOnly));
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    CHECK(root["asset"].toObject()["version"].toString() == "2.0");
    QJsonArray prims = root["meshes"].toArray()[0].toObject()["primitives"].toArray();
    CHECK(prims.size() == 4);   // three materials and the edges
    CHECK(root["images"].toArray().size() == 1 && root["textures"].toArray().size() == 1);
    QJsonArray acc = root["accessors"].toArray();
    QJsonArray mx = acc[prims[0].toObject()["attributes"].toObject()["POSITION"].toInt()].toObject()["max"].toArray();
    CHECK(mx.size() == 3 && mx[1].toDouble() <= 0.9 + 1e-6);   // Y is up, in metres
    bool blend = false;
    for (auto v : root["materials"].toArray()) blend |= v.toObject()["alphaMode"].toString() == "BLEND";
    CHECK(blend);
    // GLB: the same in one binary file
    CHECK(exportFile(m, dir + "/t.glb", eo, &err));
    QFile g(dir + "/t.glb");
    CHECK(g.open(QIODevice::ReadOnly));
    QByteArray glb = g.readAll();
    CHECK(glb.size() > 20 && glb.startsWith("glTF"));
    quint32 total = 0;
    memcpy(&total, glb.constData() + 8, 4);
    CHECK(total == (quint32)glb.size() && glb.size() % 4 == 0);
    // 3MF: a ZIP holding one object per closed shell
    CHECK(exportFile(m, dir + "/t.3mf", eo, &err));
    CHECK(err.isEmpty());
    QFile z(dir + "/t.3mf");
    CHECK(z.open(QIODevice::ReadOnly));
    QByteArray zip = z.readAll();
    CHECK(zip.startsWith("PK\x03\x04") && zip.contains("3D/3dmodel.model") && zip.contains("[Content_Types].xml"));
}

static void testSolidRepair() {
    // a closed box is clean
    Entities E;
    box(E, 0, 0, 1000, 800, 600);
    geom::SolidReport r = geom::inspectSolid(E);
    CHECK(r.clean());
    // a missing face: one hole of four edges, closed again with a face
    int top = findFaceAt(E, Vec3(500, 400, 600), Vec3(0, 0, 1));
    E.removeFace(top);
    r = geom::inspectSolid(E);
    CHECK(!r.solid() && r.holes.size() == 1 && r.holeEdges() == 4);
    CHECK(geom::repairSolid(E) >= 1);
    CHECK(geom::inspectSolid(E).clean() && geom::isSolid(E));
    NEAR(geom::volume(E), 1000.0 * 800 * 600, 1);
    // a stray edge and a reversed face
    geom::insertSegment(E, Vec3(2000, 0, 0), Vec3(2500, 0, 0));
    int side = findFaceAt(E, Vec3(1000, 400, 300), Vec3(1, 0, 0));
    geom::reverseFace(E, side);
    r = geom::inspectSolid(E);
    CHECK(r.strayEdges.size() == 1 && r.reversedFaces.size() == 1 && r.reversedFaces[0] == side);
    geom::repairSolid(E);
    r = geom::inspectSolid(E);
    CHECK(r.clean());
    NEAR(geom::volume(E), 1000.0 * 800 * 600, 1);
    // inside out as a whole: every face turns
    Entities I;
    box(I, 0, 0, 500, 500, 500);
    for (int f = 0; f < (int)I.faces.size(); ++f) if (I.faces[f].alive) geom::reverseFace(I, f);
    CHECK(geom::inspectSolid(I).reversedFaces.size() == 6);
    geom::repairSolid(I);
    CHECK(geom::volume(I) > 0 && geom::inspectSolid(I).clean());
    // an inner wall dividing the box: the wall goes and the split faces join
    Entities W;
    box(W, 0, 0, 1000, 1000, 1000);
    geom::addPolyline(W, {Vec3(500, 0, 0), Vec3(500, 1000, 0), Vec3(500, 1000, 1000), Vec3(500, 0, 1000)}, true);
    r = geom::inspectSolid(W);
    CHECK(r.innerFaces.size() == 1 && !r.solid());
    geom::repairSolid(W);
    CHECK(geom::inspectSolid(W).clean() && faces(W) == 6);
    NEAR(geom::volume(W), 1e9, 1);
    // a hole that isn't flat closes with soft triangles
    Entities N;
    box(N, 0, 0, 1000, 1000, 1000);
    int t2 = findFaceAt(N, Vec3(500, 500, 1000), Vec3(0, 0, 1));
    N.removeFace(t2);
    int corner = N.findVertex(Vec3(1000, 1000, 1000));
    N.moveVertex(corner, Vec3(1000, 1000, 1300));
    for (int f = 0; f < (int)N.faces.size(); ++f) if (N.faces[f].alive) N.faces[f].plane = Plane(geom::newellNormal(N, N.faces[f].outer).normalized(), N.verts[N.faces[f].outer[0]].p);
    geom::fixWarpedFaces(N, [&] { std::vector<int> v; for (int f = 0; f < (int)N.faces.size(); ++f) if (N.faces[f].alive) v.push_back(f); return v; }());
    CHECK(geom::inspectSolid(N).holes.size() == 1);
    geom::repairSolid(N);
    CHECK(geom::inspectSolid(N).clean() && geom::volume(N) > 1e9);
    // two boxes sharing an edge: reported, not "fixed"
    Entities T;
    box(T, 0, 0, 1000, 1000, 1000);
    box(T, 1000, 1000, 2000, 2000, 1000);
    r = geom::inspectSolid(T);
    CHECK(!r.extraEdges.empty());
}

static void testMatchPhoto() {
    struct Cam { Vec3 eye, target; double fov; };
    for (Cam c : {Cam{Vec3(-5200, -7300, 1600), Vec3(1200, 900, 400), 48}, Cam{Vec3(6100, -4800, 2600), Vec3(400, 700, 0), 36},
                  Cam{Vec3(-3000, 4500, 900), Vec3(800, 300, 700), 60}}) {
        double aspect = 1.5;
        Vec3 f = (c.target - c.eye).normalized();
        Vec3 r = f.cross(Vec3(0, 0, 1)).normalized();
        Vec3 u = r.cross(f);
        double t = std::tan(c.fov * kPi / 360);
        auto shot = [&](const Vec3& p) {
            Vec3 d = p - c.eye;
            double z = d.dot(f);
            double cx = d.dot(r) / (z * t) * 0.5, cy = d.dot(u) / (z * t) * 0.5;   // picture heights
            return Vec2(cx / aspect + 0.5, 0.5 - cy);
        };
        MatchPhoto mp;
        // box 2000 x 1500 x 1000 at the origin: two edges along red, two along green
        mp.lines[0][0] = shot(Vec3(0, 0, 0)); mp.lines[0][1] = shot(Vec3(2000, 0, 0));
        mp.lines[1][0] = shot(Vec3(0, 0, 1000)); mp.lines[1][1] = shot(Vec3(2000, 0, 1000));
        mp.lines[2][0] = shot(Vec3(0, 0, 0)); mp.lines[2][1] = shot(Vec3(0, 1500, 0));
        mp.lines[3][0] = shot(Vec3(0, 0, 1000)); mp.lines[3][1] = shot(Vec3(0, 1500, 1000));
        mp.origin = shot(Vec3(0, 0, 0));
        mp.eyeHeight = c.eye.z;
        // the solver takes red as heading into the picture; turned when it comes towards the camera
        mp.turned = Vec3(1, 0, 0).dot(f) < 0;
        CameraState out;
        QString why;
        bool ok = solveMatchPhoto(mp, aspect, out, &why);
        CHECK(ok);
        if (!ok) { std::printf("  %s\n", qPrintable(why)); continue; }
        NEAR(out.fov, c.fov, 1e-6);
        NEAR((out.eye - c.eye).length(), 0, 1e-3);
        Vec3 of = (out.target - out.eye).normalized();
        NEAR(of.dot(f), 1, 1e-9);
        NEAR(out.up.dot(u), 1, 1e-9);
    }
    // parallel lines and same-side vanishing points are refused
    MatchPhoto bad;
    bad.lines[1][0] = Vec2(0.55, 0.62); bad.lines[1][1] = Vec2(0.85, 0.54);
    bad.lines[0][0] = Vec2(0.55, 0.70); bad.lines[0][1] = Vec2(0.85, 0.62);
    CameraState out;
    CHECK(!solveMatchPhoto(bad, 1.5, out));
    MatchPhoto def;
    CHECK(solveMatchPhoto(def, 1.0, out));
    NEAR(out.fov, 2 * std::atan(0.5 / 0.8) * 180 / kPi, 1e-6);
    NEAR(out.eye.z, def.eyeHeight, 1e-6);
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    testLines();
    testHole();
    testPushPull();
    testCylinder();
    testSaveLoad();
    testSolids();
    testSolidsCoplanar();
    testFollowMe();
    testIntersect();
    testExport();
    testHouse();
    testSandbox();
    testSTL();
    testTextures();
    testMatchPhoto();
    testExport3D();
    testSolidRepair();
    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}

// ---- consistency check appended
static bool facesConsistent(const Entities& E) {
    bool ok = true;
    for (int f = 0; f < (int)E.faces.size(); ++f) {
        if (!E.faces[f].alive) continue;
        Vec3 n = geom::newellNormal(E, E.faces[f].outer);
        if (n.dot(E.faces[f].plane.n) < 0.99) { std::printf("  face %d inconsistent: newell %.3f %.3f %.3f plane %.3f %.3f %.3f\n", f, n.x, n.y, n.z, E.faces[f].plane.n.x, E.faces[f].plane.n.y, E.faces[f].plane.n.z); ok = false; }
    }
    return ok;
}

void testHouse() {
    Entities E;
    box(E, 0, 0, 2000, 4000, 2500);
    CHECK(facesConsistent(E));
    geom::addLine(E, Vec3(1000, 0, 2500), Vec3(1000, 4000, 2500));
    CHECK(faces(E) == 7);
    CHECK(facesConsistent(E));
    int a = E.findVertex(Vec3(1000, 0, 2500)), b = E.findVertex(Vec3(1000, 4000, 2500));
    geom::transformVertices(E, {a, b}, Mat4::translation(Vec3(0, 0, 1500)), true);
    CHECK(facesConsistent(E));
    CHECK(geom::isSolid(E));
    std::printf("house faces=%d vol=%.0f\n", faces(E), geom::volume(E));
    NEAR(geom::volume(E), 2000.0 * 4000 * 2500 + 0.5 * 2000 * 1500 * 4000, 10);
}

void testSandbox() {
    Entities G;
    sandbox::makeGrid(G, Vec3(), Vec3(1, 0, 0), Vec3(0, 1, 0), 4000, 3000, 1000);
    CHECK(faces(G) == 24);
    sandbox::smoove(G, Vec3(2000, 1000, 0), 2000, 500);
    BBox b = G.bbox();
    NEAR(b.hi.z, 500, 1);
    int diag = -1;
    for (int e = 0; e < (int)G.edges.size(); ++e) if (G.edges[e].alive && G.edges[e].soft && G.edges[e].faces.size() == 2) { diag = e; break; }
    CHECK(diag >= 0 && sandbox::flipEdge(G, diag));
    CHECK(faces(G) == 24);
    Entities T;
    std::vector<Vec3> pts;
    for (int i = 0; i < 20; ++i) for (int j = 0; j < 20; ++j) pts.push_back(Vec3(i * 100.0, j * 100.0, std::sin(i * 0.3) * 50));
    int n = sandbox::tinFromPoints(T, pts);
    CHECK(n == 19 * 19 * 2);
    int d = sandbox::drape(T, {{Vec3(50, 120, 999), Vec3(1850, 1700, 999)}});
    CHECK(d > 10);
    CHECK(faces(T) > n);
    std::printf("sandbox: tin=%d draped=%d faces=%d\n", n, d, faces(T));
}
