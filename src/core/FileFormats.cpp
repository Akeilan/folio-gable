#include "core/FileFormats.h"
#include "core/Geometry.h"
#include "core/Serializer.h"
#include <QBuffer>
#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QUrl>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>

namespace gable {

// ------------------------------------------------------------------ helpers

static Vec3 inP(const Vec3& p, const ImportOptions& o) {
    Vec3 q = o.swapYZ ? Vec3(p.x, -p.z, p.y) : p;
    return q * o.scale;
}
static Vec3 outP(const Vec3& p, const ExportOptions& o) {
    Vec3 q = p / o.scale;
    return o.swapYZ ? Vec3(q.x, q.z, -q.y) : q;
}
static Vec3 outN(const Vec3& n, const ExportOptions& o) { return o.swapYZ ? Vec3(n.x, n.z, -n.y) : n; }

// Adds a polygon face from positions; returns face id or -1.
static int addPolyFace(Entities& E, const std::vector<Vec3>& pts, int mat, std::set<std::vector<int>>& seen) {
    std::vector<int> loop;
    for (auto& p : pts) {
        int v = E.vertexAt(p);
        if (loop.empty() || loop.back() != v) loop.push_back(v);
    }
    while (loop.size() > 1 && loop.front() == loop.back()) loop.pop_back();
    if (loop.size() < 3) return -1;
    std::set<int> uniq(loop.begin(), loop.end());
    if (uniq.size() != loop.size()) return -1;
    std::vector<int> key(loop.begin(), loop.end());
    std::sort(key.begin(), key.end());
    if (!seen.insert(key).second) return -1;
    Vec3 n = geom::newellNormal(E, loop);
    if (n.isZero()) return -1;
    double area = 0;
    {
        Vec3 s;
        for (size_t i = 0; i < loop.size(); ++i) s += E.verts[loop[i]].p.cross(E.verts[loop[(i + 1) % loop.size()]].p);
        area = s.length() * 0.5;
    }
    if (area < 1e-8) return -1;
    int f = E.addFace(loop, {}, Plane(n, E.verts[loop[0]].p));
    if (f >= 0) E.faces[f].matFront = mat;
    if (loop.size() > 3) geom::fixWarpedFaces(E, {f});
    return f;
}

void cleanupImportedMesh(Entities& E, const ImportOptions& opt) {
    if (opt.mergeCoplanar) {
        std::vector<int> all;
        for (int e = 0; e < (int)E.edges.size(); ++e)
            if (E.edges[e].alive) {
                auto& fl = E.edges[e].faces;
                if (fl.size() == 2) {
                    const Face& A = E.faces[fl[0]];
                    const Face& B = E.faces[fl[1]];
                    if (A.matFront == B.matFront && A.plane.n.dot(B.plane.n) > 1 - 1e-9) all.push_back(e);
                }
            }
        if (!all.empty()) geom::mergeCoplanarAcross(E, all);
    }
    double cosLim = std::cos(opt.smoothAngle * kPi / 180);
    for (auto& e : E.edges) {
        if (!e.alive || e.faces.size() != 2) continue;
        const Face& A = E.faces[e.faces[0]];
        const Face& B = E.faces[e.faces[1]];
        double c = A.plane.n.dot(B.plane.n);
        if (c > cosLim && c < 1 - 1e-9) e.soft = e.smooth = true;
    }
    E.touch();
}

// ------------------------------------------------------------------ world collection

static Vec2 faceUV(const Face& F, const TexMap& t, const Vec3& p, const Material* m) {
    double w = m ? m->texW : 1000, h = m ? m->texH : 1000;
    if (t.custom) return {(p.dot(t.uAxis) + t.uOff) / w, (p.dot(t.vAxis) + t.vOff) / h};
    Frame2 fr(F.plane);
    // Use a world-anchored frame so textures line up across faces.
    Vec3 u = fr.u, v = fr.v;
    return {p.dot(u) / w, p.dot(v) / h};
}

void collectWorld(const Model& m, std::vector<WorldTri>& tris, std::vector<WorldEdge>* edges, bool visibleOnly,
                  const std::vector<EntRef>* sel) {
    std::function<void(const Entities&, const Mat4&, int, const std::vector<EntRef>*)> walk =
        [&](const Entities& E, const Mat4& xf, int inheritMat, const std::vector<EntRef>* s) {
            std::set<EntRef> ss;
            if (s) ss.insert(s->begin(), s->end());
            auto tagVis = [&](int t) { return !visibleOnly || t < 0 || t >= (int)m.tags.size() || m.tags[t].visible; };
            std::vector<Vec3> buf;
            bool mirror = xf.det3() < 0;
            for (int f = 0; f < (int)E.faces.size(); ++f) {
                const Face& F = E.faces[f];
                if (!F.alive || (visibleOnly && F.hidden) || !tagVis(F.tag)) continue;
                if (s && !ss.count({EntKind::Face, f})) continue;
                buf.clear();
                geom::faceTriangles(E, f, buf);
                Vec3 n = xf.normal(F.plane.n);
                int mat = F.matFront >= 0 ? F.matFront : inheritMat;
                TexMap tm = geom::effectiveTexMap(F, false);
                double tw = 1000, th = 1000;
                if (mat >= 0 && mat < (int)m.materials.size()) {
                    tw = std::max(m.materials[mat].texW, 1e-6);
                    th = std::max(m.materials[mat].texH, 1e-6);
                }
                auto uvOf = [&](const Vec3& p) { return Vec2((p.dot(tm.uAxis) + tm.uOff) / tw, (p.dot(tm.vAxis) + tm.vOff) / th); };
                for (size_t i = 0; i + 2 < buf.size(); i += 3) {
                    WorldTri t;
                    size_t k1 = mirror ? i + 2 : i + 1, k2 = mirror ? i + 1 : i + 2;
                    t.p[0] = xf.point(buf[i]);
                    t.p[1] = xf.point(buf[k1]);
                    t.p[2] = xf.point(buf[k2]);
                    t.uv[0] = uvOf(buf[i]);
                    t.uv[1] = uvOf(buf[k1]);
                    t.uv[2] = uvOf(buf[k2]);
                    t.n = n;
                    t.material = mat;
                    tris.push_back(t);
                }
            }
            if (edges)
                for (int e = 0; e < (int)E.edges.size(); ++e) {
                    const Edge& Ed = E.edges[e];
                    if (!Ed.alive || Ed.soft || Ed.hidden || !tagVis(Ed.tag)) continue;
                    if (s && !ss.count({EntKind::Edge, e})) continue;
                    edges->push_back({xf.point(E.verts[Ed.v[0]].p), xf.point(E.verts[Ed.v[1]].p)});
                }
            for (int i = 0; i < (int)E.insts.size(); ++i) {
                const Instance& in = E.insts[i];
                if (!in.alive || in.def < 0 || (visibleOnly && in.hidden) || !tagVis(in.tag)) continue;
                if (s && !ss.count({EntKind::Instance, i})) continue;
                walk(m.defs[in.def].ents, xf * in.xf, in.material >= 0 ? in.material : inheritMat, nullptr);
            }
        };
    walk(m.root, Mat4(), -1, sel);
}

// ------------------------------------------------------------------ OBJ

struct ObjMat { QString name; QColor color{200, 200, 200}; double alpha = 1; QString tex; };

static std::map<QString, ObjMat> readMtl(const QString& path) {
    std::map<QString, ObjMat> r;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return r;
    QTextStream ts(&f);
    ObjMat* cur = nullptr;
    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line.startsWith('#')) continue;
        QStringList p = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        if (p[0] == "newmtl" && p.size() > 1) { cur = &r[p[1]]; cur->name = p.mid(1).join(' '); }
        else if (!cur) continue;
        else if (p[0] == "Kd" && p.size() >= 4)
            cur->color = QColor::fromRgbF(std::clamp(p[1].toFloat(), 0.f, 1.f), std::clamp(p[2].toFloat(), 0.f, 1.f), std::clamp(p[3].toFloat(), 0.f, 1.f));
        else if (p[0] == "d" && p.size() > 1) cur->alpha = p[1].toDouble();
        else if (p[0] == "Tr" && p.size() > 1) cur->alpha = 1 - p[1].toDouble();
        else if (p[0] == "map_Kd" && p.size() > 1) cur->tex = QFileInfo(path).dir().filePath(p.last());
    }
    return r;
}

static int materialFor(Model& m, const ObjMat& om, std::map<QString, int>& cache) {
    auto it = cache.find(om.name);
    if (it != cache.end()) return it->second;
    int id = m.findMaterial(om.name);
    if (id < 0) {
        id = m.addMaterial(om.name, om.color);
        m.materials[id].alpha = om.alpha;
        if (!om.tex.isEmpty()) {
            QFile tf(om.tex);
            if (tf.open(QIODevice::ReadOnly)) {
                QImage img;
                QByteArray data = tf.readAll();
                if (img.loadFromData(data)) {
                    QByteArray png;
                    QBuffer b(&png);
                    b.open(QIODevice::WriteOnly);
                    img.save(&b, "PNG");
                    m.materials[id].hasTexture = true;
                    m.materials[id].texturePng = png;
                }
            }
        }
    }
    cache[om.name] = id;
    return id;
}

static bool importOBJ(Model& m, Entities& E, const QString& path, const ImportOptions& opt, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { if (err) *err = "Cannot open file."; return false; }
    QTextStream ts(&f);
    std::vector<Vec3> vs;
    std::map<QString, ObjMat> mats;
    std::map<QString, int> cache;
    int curMat = -1;
    std::set<std::vector<int>> seen;
    while (!ts.atEnd()) {
        QString line = ts.readLine().trimmed();
        if (line.isEmpty() || line[0] == '#') continue;
        QStringList p = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
        const QString& k = p[0];
        if (k == "v" && p.size() >= 4) vs.push_back(inP(Vec3(p[1].toDouble(), p[2].toDouble(), p[3].toDouble()), opt));
        else if (k == "mtllib" && p.size() > 1) {
            auto mm = readMtl(QFileInfo(path).dir().filePath(p.mid(1).join(' ')));
            mats.insert(mm.begin(), mm.end());
        } else if (k == "usemtl" && p.size() > 1) {
            auto it = mats.find(p[1]);
            ObjMat om;
            om.name = p[1];
            if (it != mats.end()) om = it->second;
            curMat = materialFor(m, om, cache);
        } else if (k == "f" && p.size() >= 4) {
            std::vector<Vec3> pts;
            bool ok = true;
            for (int i = 1; i < p.size(); ++i) {
                int idx = p[i].section('/', 0, 0).toInt();
                if (idx < 0) idx = (int)vs.size() + idx + 1;
                if (idx < 1 || idx > (int)vs.size()) { ok = false; break; }
                pts.push_back(vs[idx - 1]);
            }
            if (ok) addPolyFace(E, pts, curMat, seen);
        } else if (k == "l" && p.size() >= 3) {
            for (int i = 1; i + 1 < p.size(); ++i) {
                int a = p[i].section('/', 0, 0).toInt(), b = p[i + 1].section('/', 0, 0).toInt();
                if (a < 0) a = (int)vs.size() + a + 1;
                if (b < 0) b = (int)vs.size() + b + 1;
                if (a >= 1 && b >= 1 && a <= (int)vs.size() && b <= (int)vs.size())
                    E.addEdge(E.vertexAt(vs[a - 1]), E.vertexAt(vs[b - 1]));
            }
        }
    }
    cleanupImportedMesh(E, opt);
    return true;
}

static bool exportOBJ(const Model& m, const QString& path, const ExportOptions& opt, QString* err, const std::vector<EntRef>* sel) {
    std::vector<WorldTri> tris;
    std::vector<WorldEdge> edges;
    collectWorld(m, tris, opt.exportEdges ? &edges : nullptr, true, sel);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) { if (err) *err = "Cannot write file."; return false; }
    QString mtlName = QFileInfo(path).completeBaseName() + ".mtl";
    QTextStream ts(&f);
    ts << "# Exported by Folio Gable\nmtllib " << mtlName << "\n";
    std::map<int, std::vector<const WorldTri*>> byMat;
    for (auto& t : tris) byMat[t.material].push_back(&t);
    int vbase = 1, nbase = 1;
    for (auto& [mat, list] : byMat) {
        ts << "usemtl " << (mat >= 0 ? QString(m.materials[mat].name).replace(' ', '_') : QString("Default")) << "\n";
        for (auto* t : list) {
            for (int k = 0; k < 3; ++k) {
                Vec3 p = outP(t->p[k], opt);
                ts << "v " << p.x << " " << p.y << " " << p.z << "\n";
            }
            Vec3 n = outN(t->n, opt);
            ts << "vn " << n.x << " " << n.y << " " << n.z << "\n";
            ts << "f " << vbase << "//" << nbase << " " << vbase + 1 << "//" << nbase << " " << vbase + 2 << "//" << nbase << "\n";
            vbase += 3;
            nbase += 1;
        }
    }
    for (auto& e : edges) {
        Vec3 a = outP(e.a, opt), b = outP(e.b, opt);
        ts << "v " << a.x << " " << a.y << " " << a.z << "\nv " << b.x << " " << b.y << " " << b.z << "\n";
        ts << "l " << vbase << " " << vbase + 1 << "\n";
        vbase += 2;
    }
    QFile mf(QFileInfo(path).dir().filePath(mtlName));
    if (mf.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ms(&mf);
        ms << "newmtl Default\nKd 1 1 1\n\n";
        for (auto& [mat, list] : byMat) {
            if (mat < 0) continue;
            const Material& M = m.materials[mat];
            ms << "newmtl " << QString(M.name).replace(' ', '_') << "\n";
            ms << "Kd " << M.color.redF() << " " << M.color.greenF() << " " << M.color.blueF() << "\n";
            ms << "d " << M.alpha << "\n\n";
        }
    }
    return true;
}

// ------------------------------------------------------------------ STL

static bool importSTL(Model&, Entities& E, const QString& path, const ImportOptions& opt, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = "Cannot open file."; return false; }
    QByteArray data = f.readAll();
    std::set<std::vector<int>> seen;
    bool binary = false;
    if (data.size() >= 84) {
        quint32 n;
        memcpy(&n, data.constData() + 80, 4);
        if ((qint64)84 + (qint64)n * 50 == data.size()) binary = true;
    }
    if (binary) {
        quint32 n;
        memcpy(&n, data.constData() + 80, 4);
        const char* p = data.constData() + 84;
        for (quint32 i = 0; i < n; ++i, p += 50) {
            float v[12];
            memcpy(v, p, 48);
            addPolyFace(E, {inP(Vec3(v[3], v[4], v[5]), opt), inP(Vec3(v[6], v[7], v[8]), opt), inP(Vec3(v[9], v[10], v[11]), opt)}, -1, seen);
        }
    } else {
        QTextStream ts(data);
        std::vector<Vec3> pts;
        while (!ts.atEnd()) {
            QString line = ts.readLine().trimmed();
            if (line.startsWith("vertex")) {
                QStringList p = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
                if (p.size() >= 4) pts.push_back(inP(Vec3(p[1].toDouble(), p[2].toDouble(), p[3].toDouble()), opt));
            } else if (line.startsWith("endloop")) {
                if (pts.size() >= 3) addPolyFace(E, pts, -1, seen);
                pts.clear();
            }
        }
    }
    cleanupImportedMesh(E, opt);
    return true;
}

// Makes an STL-ready triangle soup: drops degenerate triangles, makes the winding agree with the
// normal, and orients every closed shell outward (by signed volume). Returns the number of open
// (non-manifold) edges.
static int prepareSTL(std::vector<WorldTri>& tris) {
    std::vector<WorldTri> keep;
    keep.reserve(tris.size());
    for (auto& t : tris) {
        Vec3 c = (t.p[1] - t.p[0]).cross(t.p[2] - t.p[0]);
        if (c.length() < 1e-12) continue;
        if (!t.n.isZero() && c.dot(t.n) < 0) std::swap(t.p[1], t.p[2]);
        keep.push_back(t);
    }
    tris.swap(keep);
    for (auto& t : tris) t.n = (t.p[1] - t.p[0]).cross(t.p[2] - t.p[0]).normalized();
    // weld positions
    std::map<std::tuple<long long, long long, long long>, int> ids;
    auto key = [](const Vec3& p) {
        return std::make_tuple((long long)std::llround(p.x * 1e3), (long long)std::llround(p.y * 1e3), (long long)std::llround(p.z * 1e3));
    };
    std::vector<std::array<int, 3>> tv(tris.size());
    for (size_t i = 0; i < tris.size(); ++i)
        for (int k = 0; k < 3; ++k) {
            auto it = ids.emplace(key(tris[i].p[k]), (int)ids.size()).first;
            tv[i][k] = it->second;
        }
    // undirected edge -> triangles
    std::map<std::pair<int, int>, std::vector<int>> em;
    for (size_t i = 0; i < tris.size(); ++i)
        for (int k = 0; k < 3; ++k) {
            int a = tv[i][k], b = tv[i][(k + 1) % 3];
            if (a == b) continue;
            em[{std::min(a, b), std::max(a, b)}].push_back((int)i);
        }
    int open = 0;
    for (auto& [e, ts] : em) if (ts.size() != 2) ++open;
    // shells (connected through manifold edges), each consistently oriented by propagation
    std::vector<int> shell(tris.size(), -1);
    std::vector<char> flip(tris.size(), 0);
    auto dirOf = [&](int t, int a, int b) {  // +1 if t walks a->b, -1 if b->a
        for (int k = 0; k < 3; ++k) {
            if (tv[t][k] == a && tv[t][(k + 1) % 3] == b) return 1;
            if (tv[t][k] == b && tv[t][(k + 1) % 3] == a) return -1;
        }
        return 0;
    };
    int nsh = 0;
    for (size_t s0 = 0; s0 < tris.size(); ++s0) {
        if (shell[s0] >= 0) continue;
        std::vector<int> stack{(int)s0}, members;
        shell[s0] = nsh;
        bool closed = true;
        while (!stack.empty()) {
            int t = stack.back(); stack.pop_back();
            members.push_back(t);
            for (int k = 0; k < 3; ++k) {
                int a = tv[t][k], b = tv[t][(k + 1) % 3];
                if (a == b) continue;
                auto& ts = em[{std::min(a, b), std::max(a, b)}];
                if (ts.size() != 2) { closed = false; continue; }
                int o = ts[0] == t ? ts[1] : ts[0];
                if (shell[o] >= 0) continue;
                shell[o] = nsh;
                // neighbours must traverse the shared edge in opposite directions
                int dt = dirOf(t, a, b) * (flip[t] ? -1 : 1);
                int dn = dirOf(o, a, b);
                flip[o] = (dn == dt);
                stack.push_back(o);
            }
        }
        for (int t : members) if (flip[t]) { std::swap(tris[t].p[1], tris[t].p[2]); std::swap(tv[t][1], tv[t][2]); }
        if (closed) {
            double vol = 0;
            for (int t : members) vol += tris[t].p[0].dot(tris[t].p[1].cross(tris[t].p[2]));
            if (vol < 0)
                for (int t : members) { std::swap(tris[t].p[1], tris[t].p[2]); std::swap(tv[t][1], tv[t][2]); }
        }
        ++nsh;
    }
    for (auto& t : tris) t.n = (t.p[1] - t.p[0]).cross(t.p[2] - t.p[0]).normalized();
    return open;
}

static bool exportSTL(const Model& m, const QString& path, const ExportOptions& opt, QString* err, const std::vector<EntRef>* sel) {
    std::vector<WorldTri> tris;
    collectWorld(m, tris, nullptr, true, sel);
    if (tris.empty()) { if (err) *err = "There are no faces to export."; return false; }
    int open = prepareSTL(tris);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (err) *err = "Cannot write " + path; return false; }
    if (err) *err = open ? QString("warning: %1 open edge%2, the mesh is not watertight").arg(open).arg(open == 1 ? "" : "s") : QString();
    if (opt.binary) {
        QByteArray hdr(80, ' ');
        memcpy(hdr.data(), "Folio Gable STL export", 22);
        f.write(hdr);
        quint32 n = (quint32)tris.size();
        f.write((const char*)&n, 4);
        for (auto& t : tris) {
            float v[12];
            Vec3 nn = outN(t.n, opt);
            v[0] = nn.x; v[1] = nn.y; v[2] = nn.z;
            for (int k = 0; k < 3; ++k) {
                Vec3 p = outP(t.p[k], opt);
                v[3 + k * 3] = p.x; v[4 + k * 3] = p.y; v[5 + k * 3] = p.z;
            }
            f.write((const char*)v, 48);
            quint16 attr = 0;
            f.write((const char*)&attr, 2);
        }
    } else {
        QTextStream ts(&f);
        ts.setRealNumberNotation(QTextStream::SmartNotation);
        ts.setRealNumberPrecision(9);
        ts << "solid gable\n";
        for (auto& t : tris) {
            Vec3 nn = outN(t.n, opt);
            ts << " facet normal " << nn.x << " " << nn.y << " " << nn.z << "\n  outer loop\n";
            for (int k = 0; k < 3; ++k) {
                Vec3 p = outP(t.p[k], opt);
                ts << "   vertex " << p.x << " " << p.y << " " << p.z << "\n";
            }
            ts << "  endloop\n endfacet\n";
        }
        ts << "endsolid gable\n";
    }
    return true;
}

// ------------------------------------------------------------------ DXF

static bool importDXF(Model&, Entities& E, const QString& path, const ImportOptions& opt, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) { if (err) *err = "Cannot open file."; return false; }
    QTextStream ts(&f);
    std::vector<std::pair<int, QString>> pairs;
    while (!ts.atEnd()) {
        QString code = ts.readLine().trimmed();
        if (ts.atEnd()) break;
        QString val = ts.readLine().trimmed();
        pairs.push_back({code.toInt(), val});
    }
    bool inEntities = false;
    std::set<std::vector<int>> seen;
    size_t i = 0;
    auto readEntity = [&](size_t& j) {
        std::multimap<int, QString> g;
        ++j;
        while (j < pairs.size() && pairs[j].first != 0) { g.insert({pairs[j].first, pairs[j].second}); ++j; }
        return g;
    };
    auto num = [](const std::multimap<int, QString>& g, int c, double def = 0) {
        auto it = g.find(c);
        return it == g.end() ? def : it->second.toDouble();
    };
    std::vector<int> created;
    while (i < pairs.size()) {
        auto& [c, v] = pairs[i];
        if (c == 2 && v == "ENTITIES") { inEntities = true; ++i; continue; }
        if (c == 0 && v == "ENDSEC") { inEntities = false; ++i; continue; }
        if (!inEntities || c != 0) { ++i; continue; }
        QString type = v;
        if (type == "LINE") {
            auto g = readEntity(i);
            Vec3 a = inP(Vec3(num(g, 10), num(g, 20), num(g, 30)), opt), b = inP(Vec3(num(g, 11), num(g, 21), num(g, 31)), opt);
            auto p = geom::insertSegment(E, a, b, &created);
        } else if (type == "CIRCLE" || type == "ARC") {
            auto g = readEntity(i);
            Vec3 c0 = inP(Vec3(num(g, 10), num(g, 20), num(g, 30)), opt);
            double r = num(g, 40) * opt.scale;
            double a0 = type == "ARC" ? num(g, 50) * kPi / 180 : 0, a1 = type == "ARC" ? num(g, 51) * kPi / 180 : 2 * kPi;
            if (a1 <= a0) a1 += 2 * kPi;
            int segs = std::max(6, (int)std::ceil(24 * (a1 - a0) / (2 * kPi)));
            std::vector<Vec3> pts;
            bool closed = type == "CIRCLE";
            int n = closed ? segs : segs + 1;
            for (int k = 0; k < n; ++k) {
                double a = a0 + (a1 - a0) * k / segs;
                pts.push_back(c0 + Vec3(std::cos(a), std::sin(a), 0) * r);
            }
            Curve cv;
            cv.isArc = true; cv.closed = closed; cv.center = c0; cv.radius = r; cv.startAng = a0; cv.endAng = a1; cv.segments = segs;
            geom::addPolyline(E, pts, closed, &cv);
        } else if (type == "LWPOLYLINE") {
            auto g = readEntity(i);
            std::vector<double> xs, ys;
            for (auto it = g.begin(); it != g.end(); ++it) {
                if (it->first == 10) xs.push_back(it->second.toDouble());
                if (it->first == 20) ys.push_back(it->second.toDouble());
            }
            bool closed = ((int)num(g, 70) & 1) != 0;
            double z = num(g, 38);
            std::vector<Vec3> pts;
            for (size_t k = 0; k < std::min(xs.size(), ys.size()); ++k) pts.push_back(inP(Vec3(xs[k], ys[k], z), opt));
            geom::addPolyline(E, pts, closed);
        } else if (type == "POLYLINE") {
            auto g = readEntity(i);
            bool closed = ((int)num(g, 70) & 1) != 0;
            std::vector<Vec3> pts;
            while (i < pairs.size() && pairs[i].first == 0 && pairs[i].second == "VERTEX") {
                auto vg = readEntity(i);
                pts.push_back(inP(Vec3(num(vg, 10), num(vg, 20), num(vg, 30)), opt));
            }
            if (i < pairs.size() && pairs[i].second == "SEQEND") readEntity(i);
            geom::addPolyline(E, pts, closed);
        } else if (type == "3DFACE") {
            auto g = readEntity(i);
            std::vector<Vec3> pts;
            for (int k = 0; k < 4; ++k) pts.push_back(inP(Vec3(num(g, 10 + k), num(g, 20 + k), num(g, 30 + k)), opt));
            if (pts[3].near(pts[2])) pts.pop_back();
            addPolyFace(E, pts, -1, seen);
        } else {
            readEntity(i);
        }
    }
    geom::findFaces(E, created);
    return true;
}

static bool exportDXF(const Model& m, const QString& path, const ExportOptions& opt, QString* err, const std::vector<EntRef>* sel) {
    std::vector<WorldTri> tris;
    std::vector<WorldEdge> edges;
    collectWorld(m, tris, &edges, true, sel);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) { if (err) *err = "Cannot write file."; return false; }
    QTextStream ts(&f);
    ts << "0\nSECTION\n2\nENTITIES\n";
    for (auto& e : edges) {
        Vec3 a = outP(e.a, opt), b = outP(e.b, opt);
        ts << "0\nLINE\n8\n0\n10\n" << a.x << "\n20\n" << a.y << "\n30\n" << a.z << "\n11\n" << b.x << "\n21\n" << b.y << "\n31\n" << b.z << "\n";
    }
    if (opt.triangulate)
        for (auto& t : tris) {
            ts << "0\n3DFACE\n8\n0\n";
            for (int k = 0; k < 4; ++k) {
                Vec3 p = outP(t.p[std::min(k, 2)], opt);
                ts << (10 + k) << "\n" << p.x << "\n" << (20 + k) << "\n" << p.y << "\n" << (30 + k) << "\n" << p.z << "\n";
            }
        }
    ts << "0\nENDSEC\n0\nEOF\n";
    return true;
}

// ------------------------------------------------------------------ COLLADA

namespace {
struct DaeCtx {
    Model* m;
    QDomDocument doc;
    QDir dir;
    double unit = 1000;  // metres -> mm
    bool zUp = true;
    std::map<QString, QDomElement> geometries, nodes, materials, effects, images;
    std::map<QString, int> matIds;
    std::map<QString, int> geomDefs;
    ImportOptions opt;
};

std::vector<double> floats(const QString& s) {
    std::vector<double> r;
    for (auto& t : s.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts)) r.push_back(t.toDouble());
    return r;
}

QString refId(const QString& url) { return url.startsWith('#') ? url.mid(1) : url; }

void indexById(const QDomElement& root, const QString& tag, std::map<QString, QDomElement>& out) {
    auto list = root.elementsByTagName(tag);
    for (int i = 0; i < list.size(); ++i) {
        QDomElement e = list.at(i).toElement();
        if (e.hasAttribute("id")) out[e.attribute("id")] = e;
    }
}

int daeMaterial(DaeCtx& c, const QString& matId) {
    auto it = c.matIds.find(matId);
    if (it != c.matIds.end()) return it->second;
    int id = -1;
    auto mi = c.materials.find(matId);
    if (mi != c.materials.end()) {
        QString name = mi->second.attribute("name", matId);
        QColor col(200, 200, 200);
        double alpha = 1;
        QString texFile;
        QDomElement ie = mi->second.firstChildElement("instance_effect");
        auto ei = c.effects.find(refId(ie.attribute("url")));
        if (ei != c.effects.end()) {
            QDomNodeList dl = ei->second.elementsByTagName("diffuse");
            if (dl.size()) {
                QDomElement d = dl.at(0).toElement();
                QDomElement ce = d.firstChildElement("color");
                if (!ce.isNull()) {
                    auto f = floats(ce.text());
                    if (f.size() >= 3) col = QColor::fromRgbF(std::clamp(f[0], 0.0, 1.0), std::clamp(f[1], 0.0, 1.0), std::clamp(f[2], 0.0, 1.0));
                    if (f.size() >= 4) alpha = f[3];
                }
                QDomElement te = d.firstChildElement("texture");
                if (!te.isNull()) {
                    // texture -> sampler -> surface -> image
                    QString sampler = te.attribute("texture");
                    QString imageId = sampler;
                    QDomNodeList params = ei->second.elementsByTagName("newparam");
                    for (int pass = 0; pass < 2; ++pass)
                        for (int k = 0; k < params.size(); ++k) {
                            QDomElement p = params.at(k).toElement();
                            if (p.attribute("sid") != imageId) continue;
                            QDomElement s2 = p.firstChildElement("sampler2D");
                            QDomElement sf = p.firstChildElement("surface");
                            if (!s2.isNull()) imageId = s2.firstChildElement("source").text();
                            else if (!sf.isNull()) imageId = sf.firstChildElement("init_from").text();
                        }
                    auto ii = c.images.find(imageId);
                    if (ii != c.images.end()) {
                        QString fn = ii->second.firstChildElement("init_from").text();
                        if (fn.startsWith("file://")) fn = fn.mid(7);
                        texFile = c.dir.filePath(QUrl::fromPercentEncoding(fn.toUtf8()));
                    }
                }
            }
            QDomNodeList tl = ei->second.elementsByTagName("transparency");
            if (tl.size()) {
                QDomElement fe = tl.at(0).toElement().firstChildElement("float");
                if (!fe.isNull()) { double t = fe.text().toDouble(); if (t > 0 && t < 1) alpha = t; }
            }
        }
        id = c.m->findMaterial(name);
        if (id < 0) {
            id = c.m->addMaterial(name, col);
            c.m->materials[id].alpha = alpha;
            if (!texFile.isEmpty()) {
                QImage img(texFile);
                if (!img.isNull()) {
                    QByteArray png;
                    QBuffer b(&png);
                    b.open(QIODevice::WriteOnly);
                    img.save(&b, "PNG");
                    c.m->materials[id].hasTexture = true;
                    c.m->materials[id].texturePng = png;
                }
            }
        }
    }
    c.matIds[matId] = id;
    return id;
}

// Builds a definition for a COLLADA geometry (unscaled; transforms applied via instance).
int daeGeometryDef(DaeCtx& c, const QString& gid, const std::map<QString, QString>& symbolToMat) {
    QString key = gid;
    for (auto& [s, mm] : symbolToMat) key += "|" + s + "=" + mm;
    auto it = c.geomDefs.find(key);
    if (it != c.geomDefs.end()) return it->second;
    auto gi = c.geometries.find(gid);
    if (gi == c.geometries.end()) return -1;
    QDomElement mesh = gi->second.firstChildElement("mesh");
    if (mesh.isNull()) return -1;
    std::map<QString, std::vector<double>> sources;
    for (QDomElement s = mesh.firstChildElement("source"); !s.isNull(); s = s.nextSiblingElement("source"))
        sources[s.attribute("id")] = floats(s.firstChildElement("float_array").text());
    QDomElement verts = mesh.firstChildElement("vertices");
    QString posSrc;
    for (QDomElement in = verts.firstChildElement("input"); !in.isNull(); in = in.nextSiblingElement("input"))
        if (in.attribute("semantic") == "POSITION") posSrc = refId(in.attribute("source"));
    QString vertsId = verts.attribute("id");
    const std::vector<double>& pos = sources[posSrc];
    int def = c.m->addDef(gi->second.attribute("name", gid), false);
    Entities& E = c.m->defs[def].ents;
    std::set<std::vector<int>> seen;
    auto P = [&](int i) { return Vec3(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2]); };
    int npos = (int)pos.size() / 3;
    for (QDomElement prim = mesh.firstChildElement(); !prim.isNull(); prim = prim.nextSiblingElement()) {
        QString t = prim.tagName();
        if (t != "triangles" && t != "polylist" && t != "polygons" && t != "lines") continue;
        int mat = -1;
        QString sym = prim.attribute("material");
        auto sm = symbolToMat.find(sym);
        if (sm != symbolToMat.end()) mat = daeMaterial(c, sm->second);
        int stride = 0, voff = 0;
        for (QDomElement in = prim.firstChildElement("input"); !in.isNull(); in = in.nextSiblingElement("input")) {
            int off = in.attribute("offset").toInt();
            stride = std::max(stride, off + 1);
            if (in.attribute("semantic") == "VERTEX") voff = off;
        }
        if (stride == 0) stride = 1;
        auto polyFrom = [&](const std::vector<double>& idx, size_t start, int count) {
            std::vector<Vec3> pts;
            for (int k = 0; k < count; ++k) {
                size_t ii = start + (size_t)k * stride + voff;
                if (ii >= idx.size()) return;
                int vi = (int)idx[ii];
                if (vi < 0 || vi >= npos) return;
                pts.push_back(P(vi));
            }
            addPolyFace(E, pts, mat, seen);
        };
        if (t == "triangles") {
            auto idx = floats(prim.firstChildElement("p").text());
            for (size_t s = 0; s + 3 * stride <= idx.size(); s += 3 * stride) polyFrom(idx, s, 3);
        } else if (t == "polylist") {
            auto vc = floats(prim.firstChildElement("vcount").text());
            auto idx = floats(prim.firstChildElement("p").text());
            size_t s = 0;
            for (double c2 : vc) { polyFrom(idx, s, (int)c2); s += (size_t)c2 * stride; }
        } else if (t == "polygons") {
            for (QDomElement pe = prim.firstChildElement("p"); !pe.isNull(); pe = pe.nextSiblingElement("p")) {
                auto idx = floats(pe.text());
                polyFrom(idx, 0, (int)(idx.size() / stride));
            }
        } else if (t == "lines") {
            auto idx = floats(prim.firstChildElement("p").text());
            for (size_t s = 0; s + 2 * stride <= idx.size(); s += 2 * stride) {
                int a = (int)idx[s + voff], b = (int)idx[s + stride + voff];
                if (a >= 0 && b >= 0 && a < npos && b < npos) E.addEdge(E.vertexAt(P(a)), E.vertexAt(P(b)));
            }
        }
    }
    ImportOptions o2 = c.opt;
    cleanupImportedMesh(E, o2);
    c.geomDefs[key] = def;
    return def;
}

Mat4 daeNodeTransform(const QDomElement& node) {
    Mat4 m;
    for (QDomElement t = node.firstChildElement(); !t.isNull(); t = t.nextSiblingElement()) {
        auto f = floats(t.text());
        if (t.tagName() == "matrix" && f.size() == 16) {
            Mat4 x;
            for (int r = 0; r < 4; ++r)
                for (int cc = 0; cc < 4; ++cc) x.at(r, cc) = f[r * 4 + cc];
            m = m * x;
        } else if (t.tagName() == "translate" && f.size() == 3) m = m * Mat4::translation(Vec3(f[0], f[1], f[2]));
        else if (t.tagName() == "rotate" && f.size() == 4) m = m * Mat4::rotation(Vec3(f[0], f[1], f[2]), f[3] * kPi / 180);
        else if (t.tagName() == "scale" && f.size() == 3) m = m * Mat4::scaling(Vec3(f[0], f[1], f[2]));
    }
    return m;
}

void daeNode(DaeCtx& c, const QDomElement& node, Entities& parent, int depth) {
    if (depth > 64) return;
    Mat4 xf = daeNodeTransform(node);
    // Children are collected into a definition for this node (if it has content).
    bool hasContent = false;
    for (QDomElement ch = node.firstChildElement(); !ch.isNull(); ch = ch.nextSiblingElement())
        if (ch.tagName() == "instance_geometry" || ch.tagName() == "node" || ch.tagName() == "instance_node") hasContent = true;
    if (!hasContent) return;
    int def = c.m->addDef(node.attribute("name", node.attribute("id", "Node")), true);
    for (QDomElement ch = node.firstChildElement(); !ch.isNull(); ch = ch.nextSiblingElement()) {
        if (ch.tagName() == "instance_geometry") {
            std::map<QString, QString> sym;
            auto list = ch.elementsByTagName("instance_material");
            for (int k = 0; k < list.size(); ++k) {
                QDomElement im = list.at(k).toElement();
                sym[im.attribute("symbol")] = refId(im.attribute("target"));
            }
            int gd = daeGeometryDef(c, refId(ch.attribute("url")), sym);
            if (gd >= 0) {
                // explode geometry directly into node def for single use; else instance
                Instance in;
                in.def = gd;
                c.m->defs[def].ents.insts.push_back(in);
            }
        } else if (ch.tagName() == "node") {
            daeNode(c, ch, c.m->defs[def].ents, depth + 1);
        } else if (ch.tagName() == "instance_node") {
            auto ni = c.nodes.find(refId(ch.attribute("url")));
            if (ni != c.nodes.end()) daeNode(c, ni->second, c.m->defs[def].ents, depth + 1);
        }
    }
    Instance in;
    in.def = def;
    in.xf = xf;
    in.name = node.attribute("name");
    parent.insts.push_back(in);
}
}  // namespace

static bool importDAE(Model& m, Entities& E, const QString& path, const ImportOptions& opt, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = "Cannot open file."; return false; }
    DaeCtx c;
    c.m = &m;
    c.dir = QFileInfo(path).dir();
    c.opt = opt;
    if (!c.doc.setContent(&f)) { if (err) *err = "Invalid COLLADA XML."; return false; }
    QDomElement root = c.doc.documentElement();
    QDomElement asset = root.firstChildElement("asset");
    QDomElement unit = asset.firstChildElement("unit");
    if (!unit.isNull()) c.unit = unit.attribute("meter", "1").toDouble() * 1000;
    QString up = asset.firstChildElement("up_axis").text().trimmed();
    c.zUp = up.isEmpty() || up == "Z_UP";
    indexById(root, "geometry", c.geometries);
    indexById(root, "node", c.nodes);
    indexById(root, "material", c.materials);
    indexById(root, "effect", c.effects);
    indexById(root, "image", c.images);
    QDomElement scene = root.firstChildElement("scene").firstChildElement("instance_visual_scene");
    std::map<QString, QDomElement> vscenes;
    indexById(root, "visual_scene", vscenes);
    QDomElement vs;
    auto vi = vscenes.find(refId(scene.attribute("url")));
    if (vi != vscenes.end()) vs = vi->second;
    else if (!vscenes.empty()) vs = vscenes.begin()->second;
    if (vs.isNull()) { if (err) *err = "No visual scene in COLLADA file."; return false; }
    // Root transform: unit scale and axis conversion.
    Mat4 rootXf = Mat4::scaling(Vec3(c.unit, c.unit, c.unit) * opt.scale);
    if (!c.zUp && up == "Y_UP") rootXf = rootXf * Mat4::rotation(Vec3(1, 0, 0), kPi / 2);
    size_t before = E.insts.size();
    for (QDomElement n = vs.firstChildElement("node"); !n.isNull(); n = n.nextSiblingElement("node")) daeNode(c, n, E, 0);
    for (size_t i = before; i < E.insts.size(); ++i) E.insts[i].xf = rootXf * E.insts[i].xf;
    E.touch();
    return true;
}

static void xmlColor(QTextStream& ts, const QColor& c, double a) {
    ts << c.redF() << " " << c.greenF() << " " << c.blueF() << " " << a;
}

bool exportDAE(const Model& m, const QString& path, const ExportOptions& opt, QString* err, const std::vector<EntRef>* sel) {
    std::vector<WorldTri> tris;
    std::vector<WorldEdge> edges;
    collectWorld(m, tris, opt.exportEdges ? &edges : nullptr, true, sel);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) { if (err) *err = "Cannot write file."; return false; }
    std::map<int, std::vector<const WorldTri*>> byMat;
    for (auto& t : tris) byMat[t.material].push_back(&t);
    QDir dir = QFileInfo(path).dir();
    QString base = QFileInfo(path).completeBaseName();
    QTextStream ts(&f);
    ts << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
          "<COLLADA xmlns=\"http://www.collada.org/2005/11/COLLADASchema\" version=\"1.4.1\">\n"
          "<asset><contributor><authoring_tool>Folio Gable</authoring_tool></contributor>"
          "<unit meter=\"0.001\" name=\"millimeter\"/><up_axis>Z_UP</up_axis></asset>\n";
    // images
    ts << "<library_images>\n";
    for (auto& [mat, list] : byMat) {
        if (mat < 0 || !m.materials[mat].hasTexture) continue;
        QString fn = QString("%1_tex%2.png").arg(base).arg(mat);
        m.materials[mat].image().save(dir.filePath(fn));
        ts << "<image id=\"img" << mat << "\"><init_from>" << fn << "</init_from></image>\n";
    }
    ts << "</library_images>\n<library_effects>\n";
    for (auto& [mat, list] : byMat) {
        ts << "<effect id=\"fx" << (mat + 1) << "\"><profile_COMMON>";
        bool tex = mat >= 0 && m.materials[mat].hasTexture;
        if (tex)
            ts << "<newparam sid=\"surf\"><surface type=\"2D\"><init_from>img" << mat << "</init_from></surface></newparam>"
               << "<newparam sid=\"samp\"><sampler2D><source>surf</source></sampler2D></newparam>";
        ts << "<technique sid=\"common\"><lambert><diffuse>";
        if (tex) ts << "<texture texture=\"samp\" texcoord=\"UVSET0\"/>";
        else {
            ts << "<color>";
            if (mat >= 0) xmlColor(ts, m.materials[mat].color, m.materials[mat].alpha);
            else ts << "1 1 1 1";
            ts << "</color>";
        }
        ts << "</diffuse>";
        if (mat >= 0 && m.materials[mat].alpha < 1)
            ts << "<transparent opaque=\"A_ONE\"><color>1 1 1 1</color></transparent><transparency><float>"
               << m.materials[mat].alpha << "</float></transparency>";
        ts << "</lambert></technique></profile_COMMON></effect>\n";
    }
    ts << "</library_effects>\n<library_materials>\n";
    for (auto& [mat, list] : byMat)
        ts << "<material id=\"mat" << (mat + 1) << "\" name=\""
           << (mat >= 0 ? m.materials[mat].name.toHtmlEscaped() : QString("Default")) << "\"><instance_effect url=\"#fx"
           << (mat + 1) << "\"/></material>\n";
    ts << "</library_materials>\n<library_geometries>\n<geometry id=\"mesh\"><mesh>\n";
    // positions & normals
    ts << "<source id=\"pos\"><float_array id=\"posa\" count=\"" << tris.size() * 9 << "\">";
    std::vector<const WorldTri*> order;
    for (auto& [mat, list] : byMat) order.insert(order.end(), list.begin(), list.end());
    for (auto* t : order)
        for (int k = 0; k < 3; ++k) { Vec3 p = outP(t->p[k], opt) ; ts << p.x << " " << p.y << " " << p.z << " "; }
    ts << "</float_array><technique_common><accessor source=\"#posa\" count=\"" << tris.size() * 3
       << "\" stride=\"3\"><param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/><param name=\"Z\" type=\"float\"/></accessor></technique_common></source>\n";
    ts << "<source id=\"nrm\"><float_array id=\"nrma\" count=\"" << tris.size() * 3 << "\">";
    for (auto* t : order) { Vec3 n = outN(t->n, opt); ts << n.x << " " << n.y << " " << n.z << " "; }
    ts << "</float_array><technique_common><accessor source=\"#nrma\" count=\"" << tris.size()
       << "\" stride=\"3\"><param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/><param name=\"Z\" type=\"float\"/></accessor></technique_common></source>\n";
    ts << "<source id=\"uv\"><float_array id=\"uva\" count=\"" << tris.size() * 6 << "\">";
    for (auto* t : order) {
        Frame2 fr(Plane(t->n, t->p[0]));
        double w = 1000, h = 1000;
        if (t->material >= 0) { w = m.materials[t->material].texW; h = m.materials[t->material].texH; }
        for (int k = 0; k < 3; ++k) ts << t->p[k].dot(fr.u) / w << " " << t->p[k].dot(fr.v) / h << " ";
    }
    ts << "</float_array><technique_common><accessor source=\"#uva\" count=\"" << tris.size() * 3
       << "\" stride=\"2\"><param name=\"S\" type=\"float\"/><param name=\"T\" type=\"float\"/></accessor></technique_common></source>\n";
    ts << "<vertices id=\"verts\"><input semantic=\"POSITION\" source=\"#pos\"/></vertices>\n";
    size_t triBase = 0;
    for (auto& [mat, list] : byMat) {
        ts << "<triangles material=\"m" << (mat + 1) << "\" count=\"" << list.size() << "\">"
           << "<input semantic=\"VERTEX\" source=\"#verts\" offset=\"0\"/><input semantic=\"NORMAL\" source=\"#nrm\" offset=\"1\"/>"
           << "<input semantic=\"TEXCOORD\" source=\"#uv\" offset=\"2\" set=\"0\"/><p>";
        for (size_t i = 0; i < list.size(); ++i) {
            size_t ti = triBase + i;
            for (int k = 0; k < 3; ++k) ts << ti * 3 + k << " " << ti << " " << ti * 3 + k << " ";
        }
        ts << "</p></triangles>\n";
        triBase += list.size();
    }
    ts << "</mesh></geometry>\n";
    if (!edges.empty()) {
        ts << "<geometry id=\"edges\"><mesh><source id=\"epos\"><float_array id=\"eposa\" count=\"" << edges.size() * 6 << "\">";
        for (auto& e : edges) {
            Vec3 a = outP(e.a, opt), b = outP(e.b, opt);
            ts << a.x << " " << a.y << " " << a.z << " " << b.x << " " << b.y << " " << b.z << " ";
        }
        ts << "</float_array><technique_common><accessor source=\"#eposa\" count=\"" << edges.size() * 2
           << "\" stride=\"3\"><param name=\"X\" type=\"float\"/><param name=\"Y\" type=\"float\"/><param name=\"Z\" type=\"float\"/></accessor></technique_common></source>"
           << "<vertices id=\"everts\"><input semantic=\"POSITION\" source=\"#epos\"/></vertices><lines count=\"" << edges.size()
           << "\"><input semantic=\"VERTEX\" source=\"#everts\" offset=\"0\"/><p>";
        for (size_t i = 0; i < edges.size() * 2; ++i) ts << i << " ";
        ts << "</p></lines></mesh></geometry>\n";
    }
    ts << "</library_geometries>\n<library_visual_scenes><visual_scene id=\"scene\"><node id=\"model\" name=\"Model\">"
          "<instance_geometry url=\"#mesh\"><bind_material><technique_common>";
    for (auto& [mat, list] : byMat)
        ts << "<instance_material symbol=\"m" << (mat + 1) << "\" target=\"#mat" << (mat + 1)
           << "\"><bind_vertex_input semantic=\"UVSET0\" input_semantic=\"TEXCOORD\" input_set=\"0\"/></instance_material>";
    ts << "</technique_common></bind_material></instance_geometry>";
    if (!edges.empty()) ts << "<instance_geometry url=\"#edges\"/>";
    ts << "</node></visual_scene></library_visual_scenes>\n<scene><instance_visual_scene url=\"#scene\"/></scene>\n</COLLADA>\n";
    return true;
}

// ------------------------------------------------------------------ glTF 2.0

static void putF(QByteArray& b, float v) { b.append((const char*)&v, 4); }
static void putU32(QByteArray& b, quint32 v) { b.append((const char*)&v, 4); }   // little-endian hosts
static void pad4(QByteArray& b, char c = 0) { while (b.size() % 4) b.append(c); }

static bool exportGLTF(const Model& m, const QString& path, const ExportOptions& opt, QString* err, const std::vector<EntRef>* sel, bool glb) {
    std::vector<WorldTri> tris;
    std::vector<WorldEdge> edges;
    collectWorld(m, tris, opt.exportEdges ? &edges : nullptr, true, sel);
    if (tris.empty() && edges.empty()) { if (err) *err = "There is nothing to export."; return false; }
    // glTF is Y-up and in metres
    auto P = [](const Vec3& p) { return Vec3(p.x, p.z, -p.y) / 1000.0; };
    auto N = [](const Vec3& n) { return Vec3(n.x, n.z, -n.y); };
    QByteArray bin;
    QJsonArray views, accessors, materials, images, textures, primitives;
    auto addView = [&](const QByteArray& data, int target) {
        pad4(bin);
        QJsonObject v{{"buffer", 0}, {"byteOffset", (int)bin.size()}, {"byteLength", (int)data.size()}};
        if (target) v["target"] = target;
        bin.append(data);
        views.append(v);
        return (int)views.size() - 1;
    };
    auto addAccessor = [&](int view, int count, const char* type, const QJsonArray& mn = {}, const QJsonArray& mx = {}) {
        QJsonObject a{{"bufferView", view}, {"componentType", 5126}, {"count", count}, {"type", type}};
        if (!mn.isEmpty()) { a["min"] = mn; a["max"] = mx; }
        accessors.append(a);
        return (int)accessors.size() - 1;
    };
    std::map<int, std::vector<const WorldTri*>> byMat;
    for (auto& t : tris) byMat[t.material].push_back(&t);
    std::map<int, int> matIndex;
    auto materialFor = [&](int mat) {
        auto it = matIndex.find(mat);
        if (it != matIndex.end()) return it->second;
        QJsonObject pbr{{"metallicFactor", 0.0}, {"roughnessFactor", 0.9}};
        QJsonObject mo{{"doubleSided", true}};
        QColor c = mat >= 0 ? m.materials[mat].color : m.style.frontColor;
        double a = mat >= 0 ? m.materials[mat].alpha : 1.0;
        bool tex = mat >= 0 && m.materials[mat].hasTexture && !m.materials[mat].image().isNull();
        if (tex) {
            const Material& M = m.materials[mat];
            QByteArray png = M.texturePng;
            if (!png.startsWith("\x89PNG")) {
                png.clear();
                QBuffer b(&png);
                b.open(QIODevice::WriteOnly);
                M.image().save(&b, "PNG");
            }
            int iv = addView(png, 0);
            images.append(QJsonObject{{"bufferView", iv}, {"mimeType", "image/png"}, {"name", M.name}});
            textures.append(QJsonObject{{"source", (int)images.size() - 1}, {"sampler", 0}});
            pbr["baseColorTexture"] = QJsonObject{{"index", (int)textures.size() - 1}};
            if (!M.colorize) c = QColor(255, 255, 255);
        }
        // glTF colour factors are linear light
        auto lin = [](double v) { return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
        pbr["baseColorFactor"] = QJsonArray{lin(c.redF()), lin(c.greenF()), lin(c.blueF()), a};
        mo["pbrMetallicRoughness"] = pbr;
        mo["name"] = mat >= 0 ? m.materials[mat].name : QString("Default");
        if (a < 1) mo["alphaMode"] = "BLEND";
        materials.append(mo);
        matIndex[mat] = (int)materials.size() - 1;
        return matIndex[mat];
    };
    for (auto& [mat, list] : byMat) {
        QByteArray pos, nrm, uv;
        Vec3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
        for (auto* t : list) {
            Vec3 n = N(t->n);
            for (int k = 0; k < 3; ++k) {
                Vec3 p = P(t->p[k]);
                lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
                hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
                putF(pos, (float)p.x); putF(pos, (float)p.y); putF(pos, (float)p.z);
                putF(nrm, (float)n.x); putF(nrm, (float)n.y); putF(nrm, (float)n.z);
                // glTF puts the texture's first row at v = 0; ours runs upwards
                putF(uv, (float)t->uv[k].x); putF(uv, (float)-t->uv[k].y);
            }
        }
        int count = (int)list.size() * 3;
        // bounds from the float values actually stored
        auto fl = [](double v) { return (double)(float)v; };
        QJsonObject attrs;
        attrs["POSITION"] = addAccessor(addView(pos, 34962), count, "VEC3", QJsonArray{fl(lo.x), fl(lo.y), fl(lo.z)}, QJsonArray{fl(hi.x), fl(hi.y), fl(hi.z)});
        attrs["NORMAL"] = addAccessor(addView(nrm, 34962), count, "VEC3");
        int mi = materialFor(mat);
        if (mat >= 0 && m.materials[mat].hasTexture) attrs["TEXCOORD_0"] = addAccessor(addView(uv, 34962), count, "VEC2");
        primitives.append(QJsonObject{{"attributes", attrs}, {"material", mi}, {"mode", 4}});
    }
    if (!edges.empty()) {
        QByteArray pos;
        Vec3 lo(1e300, 1e300, 1e300), hi(-1e300, -1e300, -1e300);
        for (auto& e : edges)
            for (const Vec3& q : {e.a, e.b}) {
                Vec3 p = P(q);
                lo = Vec3(std::min(lo.x, p.x), std::min(lo.y, p.y), std::min(lo.z, p.z));
                hi = Vec3(std::max(hi.x, p.x), std::max(hi.y, p.y), std::max(hi.z, p.z));
                putF(pos, (float)p.x); putF(pos, (float)p.y); putF(pos, (float)p.z);
            }
        auto fl = [](double v) { return (double)(float)v; };
        int acc = addAccessor(addView(pos, 34962), (int)edges.size() * 2, "VEC3", QJsonArray{fl(lo.x), fl(lo.y), fl(lo.z)}, QJsonArray{fl(hi.x), fl(hi.y), fl(hi.z)});
        QColor ec = m.style.edgeColor;
        materials.append(QJsonObject{{"name", "Edges"}, {"pbrMetallicRoughness", QJsonObject{{"baseColorFactor", QJsonArray{std::pow(ec.redF(), 2.2), std::pow(ec.greenF(), 2.2), std::pow(ec.blueF(), 2.2), 1.0}}, {"metallicFactor", 0.0}}}});
        primitives.append(QJsonObject{{"attributes", QJsonObject{{"POSITION", acc}}}, {"material", (int)materials.size() - 1}, {"mode", 1}});
    }
    pad4(bin);
    QJsonObject root;
    root["asset"] = QJsonObject{{"version", "2.0"}, {"generator", "Folio Gable"}};
    root["scene"] = 0;
    root["scenes"] = QJsonArray{QJsonObject{{"nodes", QJsonArray{0}}}};
    QString name = QFileInfo(path).completeBaseName();
    root["nodes"] = QJsonArray{QJsonObject{{"mesh", 0}, {"name", name}}};
    root["meshes"] = QJsonArray{QJsonObject{{"primitives", primitives}, {"name", name}}};
    root["materials"] = materials;
    if (!images.isEmpty()) {
        root["images"] = images;
        root["textures"] = textures;
        root["samplers"] = QJsonArray{QJsonObject{{"magFilter", 9729}, {"minFilter", 9987}, {"wrapS", 10497}, {"wrapT", 10497}}};
    }
    root["accessors"] = accessors;
    root["bufferViews"] = views;
    QJsonObject buf{{"byteLength", (int)bin.size()}};
    if (!glb) buf["uri"] = QString("data:application/octet-stream;base64,") + QString::fromLatin1(bin.toBase64());
    root["buffers"] = QJsonArray{buf};
    QByteArray json = QJsonDocument(root).toJson(glb ? QJsonDocument::Compact : QJsonDocument::Indented);
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (err) *err = "Cannot write " + path; return false; }
    if (glb) {
        pad4(json, ' ');
        QByteArray out;
        putU32(out, 0x46546C67);   // "glTF"
        putU32(out, 2);
        putU32(out, (quint32)(12 + 8 + json.size() + 8 + bin.size()));
        putU32(out, (quint32)json.size());
        putU32(out, 0x4E4F534A);   // JSON
        out.append(json);
        putU32(out, (quint32)bin.size());
        putU32(out, 0x004E4942);   // BIN
        out.append(bin);
        f.write(out);
    } else {
        f.write(json);
    }
    if (!f.commit()) { if (err) *err = "Cannot write " + path; return false; }
    return true;
}

// ------------------------------------------------------------------ 3MF

static quint32 crc32Of(const QByteArray& d) {
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        init = true;
    }
    quint32 c = 0xFFFFFFFFu;
    for (unsigned char b : d) c = table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

// A ZIP archive with deflated entries (what 3MF packages are).
QByteArray zipArchive(const std::vector<std::pair<QString, QByteArray>>& files) {
    auto u16 = [](QByteArray& b, quint16 v) { b.append((char)(v & 0xff)); b.append((char)(v >> 8)); };
    auto u32 = [](QByteArray& b, quint32 v) { for (int i = 0; i < 4; ++i) b.append((char)((v >> (8 * i)) & 0xff)); };
    QByteArray out, cd;
    const quint16 time = 0, date = (quint16)(((2026 - 1980) << 9) | (1 << 5) | 1);
    for (auto& [name, data] : files) {
        QByteArray z = qCompress(data, 9);
        // qCompress: 4-byte size, then a zlib stream (2-byte header, raw deflate, 4-byte checksum)
        QByteArray raw = z.mid(6, z.size() - 10);
        bool store = raw.size() >= data.size();
        const QByteArray& body = store ? data : raw;
        quint32 crc = crc32Of(data);
        QByteArray nm = name.toUtf8();
        quint32 off = (quint32)out.size();
        u32(out, 0x04034b50); u16(out, 20); u16(out, 0x0800); u16(out, store ? 0 : 8); u16(out, time); u16(out, date);
        u32(out, crc); u32(out, (quint32)body.size()); u32(out, (quint32)data.size()); u16(out, (quint16)nm.size()); u16(out, 0);
        out.append(nm);
        out.append(body);
        u32(cd, 0x02014b50); u16(cd, 20); u16(cd, 20); u16(cd, 0x0800); u16(cd, store ? 0 : 8); u16(cd, time); u16(cd, date);
        u32(cd, crc); u32(cd, (quint32)body.size()); u32(cd, (quint32)data.size()); u16(cd, (quint16)nm.size()); u16(cd, 0); u16(cd, 0);
        u16(cd, 0); u16(cd, 0); u32(cd, 0); u32(cd, off);
        cd.append(nm);
    }
    quint32 cdOff = (quint32)out.size();
    out.append(cd);
    u32(out, 0x06054b50); u16(out, 0); u16(out, 0); u16(out, (quint16)files.size()); u16(out, (quint16)files.size());
    u32(out, (quint32)cd.size()); u32(out, cdOff); u16(out, 0);
    return out;
}

static int prepareSTL(std::vector<WorldTri>& tris);

static bool export3MF(const Model& m, const QString& path, QString* err, const std::vector<EntRef>* sel) {
    std::vector<WorldTri> tris;
    collectWorld(m, tris, nullptr, true, sel);
    if (tris.empty()) { if (err) *err = "There are no faces to export."; return false; }
    int open = prepareSTL(tris);   // consistent winding, closed shells facing outward
    // weld corners, then split into shells (triangles connected through shared corners)
    std::map<std::tuple<long long, long long, long long>, int> ids;
    std::vector<Vec3> verts;
    std::vector<std::array<int, 3>> tv(tris.size());
    for (size_t i = 0; i < tris.size(); ++i)
        for (int k = 0; k < 3; ++k) {
            const Vec3& p = tris[i].p[k];
            auto key = std::make_tuple((long long)std::llround(p.x * 1e3), (long long)std::llround(p.y * 1e3), (long long)std::llround(p.z * 1e3));
            auto it = ids.find(key);
            if (it == ids.end()) { it = ids.emplace(key, (int)verts.size()).first; verts.push_back(p); }
            tv[i][k] = it->second;
        }
    std::vector<int> parent(verts.size());
    for (size_t i = 0; i < parent.size(); ++i) parent[i] = (int)i;
    std::function<int(int)> root = [&](int x) { while (parent[x] != x) x = parent[x] = parent[parent[x]]; return x; };
    for (auto& t : tv) { parent[root(t[1])] = root(t[0]); parent[root(t[2])] = root(t[0]); }
    std::map<int, std::vector<int>> shells;
    for (size_t i = 0; i < tris.size(); ++i)
        if (tv[i][0] != tv[i][1] && tv[i][1] != tv[i][2] && tv[i][0] != tv[i][2]) shells[root(tv[i][0])].push_back((int)i);
    // colours of the materials used
    std::map<int, int> colorIdx;
    QString colors;
    auto hex = [](const QColor& c, double a) {
        return QString("#%1%2%3%4").arg(c.red(), 2, 16, QChar('0')).arg(c.green(), 2, 16, QChar('0')).arg(c.blue(), 2, 16, QChar('0'))
            .arg((int)std::lround(std::clamp(a, 0.0, 1.0) * 255), 2, 16, QChar('0')).toUpper();
    };
    for (auto& t : tris)
        if (!colorIdx.count(t.material)) {
            int k = (int)colorIdx.size();
            colorIdx[t.material] = k;
            QString nm = t.material >= 0 ? m.materials[t.material].name.toHtmlEscaped() : QString("Default");
            QColor c = t.material >= 0 ? m.materials[t.material].color : m.style.frontColor;
            double a = t.material >= 0 ? m.materials[t.material].alpha : 1.0;
            colors += QString("      <base name=\"%1\" displaycolor=\"%2\"/>\n").arg(nm, hex(c, a));
        }
    QString xml;
    QTextStream ts(&xml);
    ts.setRealNumberNotation(QTextStream::FixedNotation);
    ts.setRealNumberPrecision(4);
    ts << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
          "<model unit=\"millimeter\" xml:lang=\"en-US\" xmlns=\"http://schemas.microsoft.com/3dmanufacturing/core/2015/02\">\n"
          "  <metadata name=\"Application\">Folio Gable</metadata>\n"
          "  <resources>\n    <basematerials id=\"1\">\n" << colors << "    </basematerials>\n";
    int objId = 2;
    QString build;
    for (auto& [r, list] : shells) {
        std::map<int, int> local;
        QString vx, tx;
        QTextStream vs(&vx), tts(&tx);
        vs.setRealNumberNotation(QTextStream::FixedNotation);
        vs.setRealNumberPrecision(4);
        for (int t : list)
            for (int k = 0; k < 3; ++k)
                if (!local.count(tv[t][k])) {
                    int n = (int)local.size();
                    local[tv[t][k]] = n;
                    const Vec3& p = verts[tv[t][k]];
                    vs << "          <vertex x=\"" << p.x << "\" y=\"" << p.y << "\" z=\"" << p.z << "\"/>\n";
                }
        for (int t : list)
            tts << "          <triangle v1=\"" << local[tv[t][0]] << "\" v2=\"" << local[tv[t][1]] << "\" v3=\"" << local[tv[t][2]]
                << "\" pid=\"1\" p1=\"" << colorIdx[tris[t].material] << "\"/>\n";
        vs.flush();
        tts.flush();
        ts << "    <object id=\"" << objId << "\" type=\"model\" pid=\"1\" pindex=\"" << colorIdx[tris[list[0]].material] << "\">\n"
           << "      <mesh>\n        <vertices>\n" << vx << "        </vertices>\n        <triangles>\n" << tx
           << "        </triangles>\n      </mesh>\n    </object>\n";
        build += QString("    <item objectid=\"%1\"/>\n").arg(objId);
        ++objId;
    }
    ts << "  </resources>\n  <build>\n" << build << "  </build>\n</model>\n";
    ts.flush();
    QByteArray types = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                       "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                       "<Default Extension=\"rels\" ContentType=\"application/vnd.openxmlformats-package.relationships+xml\"/>"
                       "<Default Extension=\"model\" ContentType=\"application/vnd.ms-package.3dmanufacturing-3dmodel+xml\"/></Types>\n";
    QByteArray rels = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
                      "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                      "<Relationship Target=\"/3D/3dmodel.model\" Id=\"rel0\" "
                      "Type=\"http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel\"/></Relationships>\n";
    QByteArray zip = zipArchive({{"[Content_Types].xml", types}, {"_rels/.rels", rels}, {"3D/3dmodel.model", xml.toUtf8()}});
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (err) *err = "Cannot write " + path; return false; }
    f.write(zip);
    if (!f.commit()) { if (err) *err = "Cannot write " + path; return false; }
    if (err) *err = open ? QString("warning: %1 open edge%2, the mesh is not watertight").arg(open).arg(open == 1 ? "" : "s") : QString();
    return true;
}

// ------------------------------------------------------------------ dispatch

bool import3DS(Model& m, Entities& E, const QString& path, const ImportOptions& opt, QString* err);
bool importIFC(Model& m, Entities& E, const QString& path, const ImportOptions& opt, QString* err, QStringList* notes);
bool importKMZ(Model& m, Entities& E, const QString& path, const ImportOptions& opt, QString* err);

static QStringList g_importNotes;
QStringList takeImportNotes() {
    QStringList n = g_importNotes;
    g_importNotes.clear();
    return n;
}

int importDAEInto(Model& m, Entities& target, const QString& path, const ImportOptions& opt, QString* err) {
    return importFile(m, target, path, opt, err);
}

bool importGLTF(Model& m, Entities& E, const QString& path, const ImportOptions& opt, QString* err);
bool import3MF(Model& m, Entities& E, const QString& path, const ImportOptions& opt, QString* err);

int importFile(Model& m, Entities& target, const QString& path, const ImportOptions& opt, QString* err) {
    QString ext = QFileInfo(path).suffix().toLower();
    QString name = QFileInfo(path).completeBaseName();
    g_importNotes.clear();
    if (ext == "3ds" || ext == "kmz" || ext == "ifc") {
        // several groups (meshes, models, building parts) inside one group named after the file
        int def = m.addDef(name, true);
        Entities tmp;
        bool ok = ext == "3ds" ? import3DS(m, tmp, path, opt, err)
                : ext == "kmz" ? importKMZ(m, tmp, path, opt, err)
                : importIFC(m, tmp, path, opt, err, &g_importNotes);
        if (!ok) { m.defs[def].alive = false; return -1; }
        m.defs[def].ents = std::move(tmp);
        Instance in;
        in.def = def;
        target.insts.push_back(in);
        target.touch();
        return (int)target.insts.size() - 1;
    }
    if (ext == "dae") {
        size_t before = target.insts.size();
        if (!importDAE(m, target, path, opt, err)) return -1;
        if (target.insts.size() == before + 1) return (int)before;
        // wrap multiple roots in one group
        int def = m.addDef(name, true);
        for (size_t i = before; i < target.insts.size(); ++i) m.defs[def].ents.insts.push_back(target.insts[i]);
        target.insts.resize(before);
        Instance in;
        in.def = def;
        target.insts.push_back(in);
        target.touch();
        return (int)target.insts.size() - 1;
    }
    if (ext == "gbl") {
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly)) { if (err) *err = "Cannot open file."; return -1; }
        Model src;
        if (!loadModel(src, f.readAll(), err)) return -1;
        int def = m.addDef(name, false);
        Entities tmp;
        mergeModelInto(m, tmp, src, Mat4());
        m.defs[def].ents = std::move(tmp);
        Instance in;
        in.def = def;
        target.insts.push_back(in);
        target.touch();
        return (int)target.insts.size() - 1;
    }
    int def = m.addDef(name, opt.asGroup);
    Entities tmp;
    bool ok = false;
    if (ext == "obj") ok = importOBJ(m, tmp, path, opt, err);
    else if (ext == "gltf" || ext == "glb") ok = importGLTF(m, tmp, path, opt, err);
    else if (ext == "3mf") ok = import3MF(m, tmp, path, opt, err);
    else if (ext == "stl") ok = importSTL(m, tmp, path, opt, err);
    else if (ext == "dxf") ok = importDXF(m, tmp, path, opt, err);
    else if (err) *err = "Unsupported file type: " + ext;
    if (!ok) { m.defs[def].alive = false; return -1; }
    m.defs[def].ents = std::move(tmp);
    Instance in;
    in.def = def;
    target.insts.push_back(in);
    target.touch();
    return (int)target.insts.size() - 1;
}

bool export3DS(const Model& m, const QString& path, const ExportOptions& opt, QString* err, const std::vector<EntRef>* sel);
bool exportKMZ(const Model& m, const QString& path, QString* err, const std::vector<EntRef>* sel);
bool exportIFC(const Model& m, const QString& path, QString* err, const std::vector<EntRef>* sel);

bool exportFile(const Model& m, const QString& path, const ExportOptions& opt, QString* err, const std::vector<EntRef>* sel) {
    QString ext = QFileInfo(path).suffix().toLower();
    if (ext == "obj") return exportOBJ(m, path, opt, err, sel);
    if (ext == "stl") return exportSTL(m, path, opt, err, sel);
    if (ext == "dae") return exportDAE(m, path, opt, err, sel);
    if (ext == "dxf") return exportDXF(m, path, opt, err, sel);
    if (ext == "gltf" || ext == "glb") return exportGLTF(m, path, opt, err, sel, ext == "glb");
    if (ext == "3mf") return export3MF(m, path, err, sel);
    if (ext == "3ds") return export3DS(m, path, opt, err, sel);
    if (ext == "kmz") return exportKMZ(m, path, err, sel);
    if (ext == "ifc") return exportIFC(m, path, err, sel);
    if (err) *err = "Unsupported export type: " + ext;
    return false;
}

}  // namespace gable
