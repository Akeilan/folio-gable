// Exports to 3D Studio (.3ds), Google Earth (.kmz) and IFC 2x3 (.ifc) (G3-02).
#include "core/FileFormats.h"
#include "core/Geometry.h"
#include "core/Tessellate.h"
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <set>

namespace gable {

bool exportDAE(const Model& m, const QString& path, const ExportOptions& opt, QString* err, const std::vector<EntRef>* sel);
QByteArray zipArchive(const std::vector<std::pair<QString, QByteArray>>& files);

namespace {

// A face in world space: its outline, its holes and its material.
struct WPoly {
    std::vector<Vec3> outer;
    std::vector<std::vector<Vec3>> holes;
    int material = -1;
};

// Triangles of a planar polygon with holes, turning the same way as its outline.
void triangulate(const std::vector<Vec3>& outer, const std::vector<std::vector<Vec3>>& holes, std::vector<Vec3>& out) {
    Vec3 n(0, 0, 0);
    for (size_t i = 0; i < outer.size(); ++i) {
        const Vec3& a = outer[i];
        const Vec3& b = outer[(i + 1) % outer.size()];
        n = n + Vec3((a.y - b.y) * (a.z + b.z), (a.z - b.z) * (a.x + b.x), (a.x - b.x) * (a.y + b.y));
    }
    if (n.length() < 1e-12) return;
    n = n.normalized();
    Vec3 u = std::fabs(n.x) < 0.9 ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
    u = (u - n * u.dot(n)).normalized();
    Vec3 v = n.cross(u);
    Vec3 o = outer[0];
    std::vector<std::vector<Vec2>> loops;
    auto flat = [&](const std::vector<Vec3>& l) {
        std::vector<Vec2> r;
        for (auto& p : l) r.push_back(Vec2((p - o).dot(u), (p - o).dot(v)));
        return r;
    };
    loops.push_back(flat(outer));
    for (auto& h : holes) loops.push_back(flat(h));
    TessResult t;
    if (!tessellate(loops, t)) return;
    for (size_t i = 0; i + 2 < t.tris.size(); i += 3) {
        Vec2 a = t.pts[t.tris[i]], b = t.pts[t.tris[i + 1]], c = t.pts[t.tris[i + 2]];
        double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::fabs(area) < 1e-12) continue;
        if (area < 0) std::swap(b, c);
        for (const Vec2& q : {a, b, c}) out.push_back(o + u * q.x + v * q.y);
    }
}

bool tagVisible(const Model& m, int t) { return t < 0 || t >= (int)m.tags.size() || m.tags[t].visible; }

void worldPolys(const Model& m, const Entities& E, const Mat4& xf, int inheritMat, std::vector<WPoly>& out, bool withInstances = true) {
    bool mirror = xf.det3() < 0;
    auto loop = [&](const std::vector<int>& ids) {
        std::vector<Vec3> r;
        for (int v : ids) r.push_back(xf.point(E.verts[v].p));
        if (mirror) std::reverse(r.begin(), r.end());
        return r;
    };
    for (auto& F : E.faces) {
        if (!F.alive || F.hidden || !tagVisible(m, F.tag) || F.outer.size() < 3) continue;
        WPoly p;
        p.outer = loop(F.outer);
        for (auto& h : F.holes) if (h.size() >= 3) p.holes.push_back(loop(h));
        p.material = F.matFront >= 0 ? F.matFront : inheritMat;
        out.push_back(std::move(p));
    }
    if (!withInstances) return;
    for (auto& in : E.insts) {
        if (!in.alive || in.def < 0 || in.hidden || !tagVisible(m, in.tag)) continue;
        worldPolys(m, m.defs[in.def].ents, xf * in.xf, in.material >= 0 ? in.material : inheritMat, out);
    }
}

// ------------------------------------------------------------------ the parts of the model

// IFC classes a group can stand for, with what IFC 2x3 adds after the eight IfcElement attributes.
struct IfcClass { const char* name; const char* tail; };
const IfcClass kClasses[] = {
    {"IfcWall", ""}, {"IfcWallStandardCase", ""}, {"IfcSlab", ",$"}, {"IfcRoof", ",.NOTDEFINED."},
    {"IfcDoor", ",$,$"}, {"IfcWindow", ",$,$"}, {"IfcColumn", ""}, {"IfcBeam", ""},
    {"IfcStair", ",.NOTDEFINED."}, {"IfcStairFlight", ",$,$,$,$"}, {"IfcRailing", ",$"}, {"IfcSpace", ""},
    {"IfcFurnishingElement", ""}, {"IfcMember", ""}, {"IfcPlate", ""}, {"IfcCovering", ",$"},
    {"IfcBuildingElementProxy", ",$"}, {"IfcFooting", ",.NOTDEFINED."}, {"IfcCurtainWall", ""},
    {"IfcRamp", ",.NOTDEFINED."}, {"IfcFlowTerminal", ""}, {"IfcFlowSegment", ""},
};

// "IfcWall", "Wall", "walls", "IfcFurniture" (IFC4) -> an IFC 2x3 class, or empty.
QString ifcClassFor(const QString& s0) {
    QString s = s0.trimmed().toLower();
    if (s.isEmpty()) return {};
    if (s == "ifcfurniture" || s == "furniture") return "IfcFurnishingElement";
    for (auto& c : kClasses) {
        QString n = QString(c.name).toLower();
        QString bare = n.mid(3);
        if (s == n || s == bare || s == bare + "s") return c.name;
    }
    return {};
}

struct Part {
    QString name, objectType, cls;   // cls: an IFC class (empty: none given)
    std::vector<WPoly> polys;
};

QString classOf(const Model& m, const Instance& in) {
    QString c;
    if (in.tag >= 0 && in.tag < (int)m.tags.size()) c = ifcClassFor(m.tags[in.tag].name);
    if (c.isEmpty() && in.def >= 0) c = ifcClassFor(m.defs[in.def].description);
    return c;
}

bool hasTypedInside(const Model& m, const Entities& E, int depth = 0) {
    if (depth > 32) return false;
    for (auto& in : E.insts) {
        if (!in.alive || in.def < 0) continue;
        if (!classOf(m, in).isEmpty() || hasTypedInside(m, m.defs[in.def].ents, depth + 1)) return true;
    }
    return false;
}

// Each top-level group or component is a part. A group without an IFC class of its own that holds
// groups with one (such as an imported IFC file) is opened up, so its building elements stay apart.
void collectParts(const Model& m, const Entities& E, const Mat4& xf, int inheritMat, const std::vector<EntRef>* sel,
                  const QString& looseName, std::vector<Part>& parts, int depth = 0) {
    std::set<EntRef> ss;
    if (sel) ss.insert(sel->begin(), sel->end());
    Part loose;
    loose.name = looseName;
    bool mirror = xf.det3() < 0;
    for (int f = 0; f < (int)E.faces.size(); ++f) {
        const Face& F = E.faces[f];
        if (!F.alive || F.hidden || !tagVisible(m, F.tag) || F.outer.size() < 3) continue;
        if (sel && !ss.count({EntKind::Face, f})) continue;
        WPoly p;
        for (int v : F.outer) p.outer.push_back(xf.point(E.verts[v].p));
        for (auto& h : F.holes) {
            std::vector<Vec3> hp;
            for (int v : h) hp.push_back(xf.point(E.verts[v].p));
            if (hp.size() >= 3) p.holes.push_back(hp);
        }
        if (mirror) {
            std::reverse(p.outer.begin(), p.outer.end());
            for (auto& h : p.holes) std::reverse(h.begin(), h.end());
        }
        p.material = F.matFront >= 0 ? F.matFront : inheritMat;
        loose.polys.push_back(std::move(p));
    }
    if (!loose.polys.empty()) parts.push_back(std::move(loose));
    for (int i = 0; i < (int)E.insts.size(); ++i) {
        const Instance& in = E.insts[i];
        if (!in.alive || in.def < 0 || in.hidden || !tagVisible(m, in.tag)) continue;
        if (sel && !ss.count({EntKind::Instance, i})) continue;
        const ComponentDef& d = m.defs[in.def];
        QString nm = !in.name.isEmpty() ? in.name : d.name;
        QString cls = classOf(m, in);
        int mat = in.material >= 0 ? in.material : inheritMat;
        if (cls.isEmpty() && depth < 16 && hasTypedInside(m, d.ents)) {
            collectParts(m, d.ents, xf * in.xf, mat, nullptr, nm, parts, depth + 1);
            continue;
        }
        Part p;
        p.name = nm;
        p.cls = cls;
        p.objectType = d.isGroup ? QString() : d.name;
        worldPolys(m, d.ents, xf * in.xf, mat, p.polys);
        if (!p.polys.empty()) parts.push_back(std::move(p));
    }
}

QString cleanName(const QString& s, int max) {
    QString r;
    for (QChar c : s) {
        if (c.unicode() < 32 || c.unicode() > 126) r += '_';
        else r += c;
    }
    return r.left(max);
}

// ------------------------------------------------------------------ 3D Studio

struct Chunk {
    QByteArray b;
    int at = 0;
    Chunk& u16(quint16 v) { char c[2]; qToLittleEndian(v, c); b.append(c, 2); return *this; }
    Chunk& u32(quint32 v) { char c[4]; qToLittleEndian(v, c); b.append(c, 4); return *this; }
    Chunk& f32(float f) { quint32 v; std::memcpy(&v, &f, 4); return u32(v); }
    Chunk& str(const QString& s, int max) { b.append(cleanName(s, max).toLatin1()); b.append('\0'); return *this; }
    Chunk& raw(const QByteArray& x) { b.append(x); return *this; }
};
QByteArray chunk(quint16 id, const QByteArray& body) {
    Chunk c;
    c.u16(id).u32((quint32)body.size() + 6).raw(body);
    return c.b;
}
QByteArray rgb24(const QColor& col) {
    QByteArray x;
    x.append((char)col.red());
    x.append((char)col.green());
    x.append((char)col.blue());
    return chunk(0x0011, x);
}
QByteArray percent(int v) {
    Chunk c;
    c.u16((quint16)v);
    return chunk(0x0030, c.b);
}

}  // namespace

bool export3DS(const Model& m, const QString& path, const ExportOptions& opt, QString* err, const std::vector<EntRef>* sel) {
    std::vector<Part> parts;
    collectParts(m, m.root, Mat4(), -1, sel, "Geometry", parts);
    double k = opt.scale > 0 ? 1.0 / opt.scale : 1.0;
    // materials, named uniquely within the 16 characters 3D Studio allows
    std::map<int, QString> matName;
    std::set<QString> usedNames;
    auto nameFor = [&](int mat) {
        auto it = matName.find(mat);
        if (it != matName.end()) return it->second;
        QString base = cleanName(mat >= 0 ? m.materials[mat].name : QString("Default"), 16);
        if (base.isEmpty()) base = "Material";
        QString n = base;
        for (int i = 2; usedNames.count(n.toLower()); ++i) n = base.left(16 - QString::number(i).size() - 1) + "_" + QString::number(i);
        usedNames.insert(n.toLower());
        matName[mat] = n;
        return n;
    };
    QByteArray objects;
    std::set<QString> objNames;
    int meshes = 0;
    for (auto& part : parts) {
        // triangles of the part, corners welded
        Entities tmp;
        std::vector<Vec3> verts;
        std::vector<std::array<int, 3>> tris;
        std::vector<int> triMat;
        std::map<std::tuple<long long, long long, long long>, int> ids;
        auto vid = [&](const Vec3& p0) {
            Vec3 p = p0 * k;
            if (opt.swapYZ) p = Vec3(p.x, p.z, -p.y);
            auto key = std::make_tuple(std::llround(p.x * 1e4), std::llround(p.y * 1e4), std::llround(p.z * 1e4));
            auto it = ids.find(key);
            if (it != ids.end()) return it->second;
            ids[key] = (int)verts.size();
            verts.push_back(p);
            return (int)verts.size() - 1;
        };
        for (auto& poly : part.polys) {
            std::vector<Vec3> buf;
            triangulate(poly.outer, poly.holes, buf);
            for (size_t i = 0; i + 2 < buf.size(); i += 3) {
                std::array<int, 3> t{vid(buf[i]), vid(buf[i + 1]), vid(buf[i + 2])};
                if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) continue;
                tris.push_back(t);
                triMat.push_back(poly.material);
            }
        }
        if (tris.empty()) continue;
        // a mesh holds at most 65535 corners and triangles: larger parts are split
        size_t first = 0;
        int piece = 0;
        while (first < tris.size()) {
            std::map<int, int> local;
            std::vector<int> order;
            size_t last = first;
            while (last < tris.size()) {
                int fresh = 0;
                for (int c : tris[last]) if (!local.count(c)) ++fresh;
                if (local.size() + fresh > 65535 || last - first >= 65535) break;
                for (int c : tris[last])
                    if (!local.count(c)) { local[c] = (int)order.size(); order.push_back(c); }
                ++last;
            }
            QString base = cleanName(part.name, 10);
            if (base.isEmpty()) base = "Object";
            QString on = base;
            for (int i = 2; objNames.count(on.toLower()); ++i) on = base.left(10 - QString::number(i).size()) + QString::number(i);
            objNames.insert(on.toLower());
            Chunk pts;
            pts.u16((quint16)order.size());
            for (int c : order) pts.f32((float)verts[c].x).f32((float)verts[c].y).f32((float)verts[c].z);
            Chunk faces;
            faces.u16((quint16)(last - first));
            std::map<int, std::vector<int>> byMat;
            for (size_t t = first; t < last; ++t) {
                faces.u16((quint16)local[tris[t][0]]).u16((quint16)local[tris[t][1]]).u16((quint16)local[tris[t][2]]).u16(7);
                byMat[triMat[t]].push_back((int)(t - first));
            }
            for (auto& [mat, list] : byMat) {
                Chunk g;
                g.str(nameFor(mat), 16).u16((quint16)list.size());
                for (int f : list) g.u16((quint16)f);
                faces.raw(chunk(0x4130, g.b));
            }
            Chunk axes;   // the local axes: none
            for (float v : {1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f, 1.f, 0.f, 0.f, 0.f}) axes.f32(v);
            QByteArray tri = chunk(0x4110, pts.b) + chunk(0x4160, axes.b) + chunk(0x4120, faces.b);
            Chunk obj;
            obj.str(on, 10).raw(chunk(0x4100, tri));
            objects += chunk(0x4000, obj.b);
            ++meshes;
            ++piece;
            first = last;
        }
    }
    if (!meshes) { if (err) *err = "There are no faces to export."; return false; }
    QByteArray mats;
    for (auto& [mat, nm] : matName) {
        QColor c = mat >= 0 ? m.materials[mat].color : m.style.frontColor;
        double a = mat >= 0 ? m.materials[mat].alpha : 1.0;
        Chunk n;
        n.str(nm, 16);
        QByteArray body = chunk(0xA000, n.b) + chunk(0xA010, rgb24(c.darker(150))) + chunk(0xA020, rgb24(c)) +
                          chunk(0xA030, rgb24(QColor(40, 40, 40))) + chunk(0xA050, percent((int)std::lround((1 - a) * 100)));
        mats += chunk(0xAFFF, body);
    }
    Chunk ver, mver, scale;
    ver.u32(3);
    mver.u32(3);
    scale.f32(1.f);
    QByteArray editor = chunk(0x3D3E, mver.b) + mats + chunk(0x0100, scale.b) + objects;
    QByteArray file = chunk(0x4D4D, chunk(0x0002, ver.b) + chunk(0x3D3D, editor));
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (err) *err = "Cannot write " + path; return false; }
    f.write(file);
    if (!f.commit()) { if (err) *err = "Cannot write " + path; return false; }
    if (err) err->clear();
    return true;
}

// ------------------------------------------------------------------ Google Earth

bool exportKMZ(const Model& m, const QString& path, QString* err, const std::vector<EntRef>* sel) {
    QTemporaryDir tmp;
    if (!tmp.isValid()) { if (err) *err = "Cannot write " + path; return false; }
    QDir d(tmp.path());
    d.mkpath("models");
    ExportOptions eo;
    eo.scale = 1.0;
    QString e2;
    if (!exportDAE(m, d.filePath("models/model.dae"), eo, &e2, sel)) { if (err) *err = e2; return false; }
    QFile dae(d.filePath("models/model.dae"));
    if (!dae.open(QIODevice::ReadOnly) || !dae.readAll().contains("<triangles") ) {
        if (err) *err = "There are no faces to export.";
        return false;
    }
    dae.close();
    QString name = QFileInfo(path).completeBaseName().toHtmlEscaped();
    double heading = std::fmod(360.0 - m.shadow.northAngle, 360.0);
    if (heading < 0) heading += 360;
    QString kml = QString(
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<kml xmlns=\"http://www.opengis.net/kml/2.2\">\n"
        "<Document>\n  <name>%1</name>\n  <Placemark>\n    <name>%1</name>\n"
        "    <LookAt><longitude>%3</longitude><latitude>%2</latitude><altitude>0</altitude><heading>0</heading>"
        "<tilt>45</tilt><range>100</range></LookAt>\n"
        "    <Model id=\"model\">\n      <altitudeMode>relativeToGround</altitudeMode>\n"
        "      <Location><longitude>%3</longitude><latitude>%2</latitude><altitude>0</altitude></Location>\n"
        "      <Orientation><heading>%4</heading><tilt>0</tilt><roll>0</roll></Orientation>\n"
        "      <Scale><x>1</x><y>1</y><z>1</z></Scale>\n"
        "      <Link><href>models/model.dae</href></Link>\n    </Model>\n  </Placemark>\n</Document>\n</kml>\n")
        .arg(name).arg(m.shadow.latitude, 0, 'f', 8).arg(m.shadow.longitude, 0, 'f', 8).arg(heading, 0, 'f', 4);
    std::vector<std::pair<QString, QByteArray>> files{{"doc.kml", kml.toUtf8()}};
    QDirIterator it(d.filePath("models"), QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString fp = it.next();
        QFile f(fp);
        if (f.open(QIODevice::ReadOnly)) files.push_back({d.relativeFilePath(fp), f.readAll()});
    }
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly)) { if (err) *err = "Cannot write " + path; return false; }
    out.write(zipArchive(files));
    if (!out.commit()) { if (err) *err = "Cannot write " + path; return false; }
    if (err) err->clear();
    return true;
}

// ------------------------------------------------------------------ IFC 2x3

namespace {

QString ifcGuid() {
    static const char* chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz_$";
    QByteArray b = QUuid::createUuid().toRfc4122();
    // 128 bits as 22 base-64 digits: 2 bits, then 21 times 6 bits
    QString r;
    auto bit = [&](int i) { return (b[i / 8] >> (7 - i % 8)) & 1; };
    int v = bit(0) * 2 + bit(1);
    r += chars[v];
    for (int c = 0; c < 21; ++c) {
        int x = 0;
        for (int k = 0; k < 6; ++k) x = x * 2 + bit(2 + c * 6 + k);
        r += chars[x];
    }
    return r;
}

// a STEP string: quotes doubled, everything outside printable ASCII as \X2\hhhh\X0\.
QString stepStr(const QString& s) {
    QString r = "'";
    QString wide;
    auto flush = [&] { if (!wide.isEmpty()) { r += "\\X2\\" + wide + "\\X0\\"; wide.clear(); } };
    for (QChar c : s) {
        ushort u = c.unicode();
        if (u >= 32 && u < 127) {
            flush();
            if (c == '\'') r += "''";
            else if (c == '\\') r += "\\\\";
            else r += c;
        } else if (u >= 32) {
            wide += QString("%1").arg(u, 4, 16, QChar('0')).toUpper();
        }
    }
    flush();
    return r + "'";
}

QString num(double v) {
    if (std::fabs(v) < 1e-9) v = 0;
    QString s = QString::number(v, 'f', 4);
    while (s.endsWith('0')) s.chop(1);
    return s;   // "12." is how STEP writes a real
}

struct Step {
    QString body;
    int next = 1;
    int add(const QString& e) { int id = next++; body += QString("#%1=%2;\n").arg(id).arg(e); return id; }
};

QString refs(const std::vector<int>& ids) {
    QStringList l;
    for (int i : ids) l << QString("#%1").arg(i);
    return "(" + l.join(",") + ")";
}

}  // namespace

bool exportIFC(const Model& m, const QString& path, QString* err, const std::vector<EntRef>* sel) {
    std::vector<Part> parts;
    QString base = QFileInfo(path).completeBaseName();
    collectParts(m, m.root, Mat4(), -1, sel, "Geometry", parts);
    if (parts.empty()) { if (err) *err = "There are no faces to export."; return false; }
    Step s;
    // who made it, units and the 3D context
    int person = s.add("IFCPERSON($,$,'',$,$,$,$,$)");
    int org = s.add("IFCORGANIZATION($,'Folio',$,$,$)");
    int po = s.add(QString("IFCPERSONANDORGANIZATION(#%1,#%2,$)").arg(person).arg(org));
    int app = s.add(QString("IFCAPPLICATION(#%1,'%2','Folio Gable','FolioGable')").arg(org).arg(GABLE_VERSION));
    qint64 now = QDateTime::currentSecsSinceEpoch();
    int oh = s.add(QString("IFCOWNERHISTORY(#%1,#%2,$,.ADDED.,$,$,$,%3)").arg(po).arg(app).arg(now));
    QString OH = QString("#%1").arg(oh);
    int uLen = s.add("IFCSIUNIT(*,.LENGTHUNIT.,.MILLI.,.METRE.)");
    int uArea = s.add("IFCSIUNIT(*,.AREAUNIT.,$,.SQUARE_METRE.)");
    int uVol = s.add("IFCSIUNIT(*,.VOLUMEUNIT.,$,.CUBIC_METRE.)");
    int uAng = s.add("IFCSIUNIT(*,.PLANEANGLEUNIT.,$,.RADIAN.)");
    int units = s.add(QString("IFCUNITASSIGNMENT(%1)").arg(refs({uLen, uArea, uVol, uAng})));
    int o = s.add("IFCCARTESIANPOINT((0.,0.,0.))");
    int z = s.add("IFCDIRECTION((0.,0.,1.))");
    int x = s.add("IFCDIRECTION((1.,0.,0.))");
    int ax = s.add(QString("IFCAXIS2PLACEMENT3D(#%1,#%2,#%3)").arg(o).arg(z).arg(x));
    double na = m.shadow.northAngle * kPi / 180;
    int north = s.add(QString("IFCDIRECTION((%1,%2))").arg(num(std::sin(na))).arg(num(std::cos(na))));
    int ctx = s.add(QString("IFCGEOMETRICREPRESENTATIONCONTEXT($,'Model',3,1.E-05,#%1,#%2)").arg(ax).arg(north));
    QString projName = !m.description.isEmpty() ? m.description.section('\n', 0, 0) : base;
    int proj = s.add(QString("IFCPROJECT('%1',%2,%3,$,$,$,$,(#%4),#%5)").arg(ifcGuid(), OH, stepStr(projName)).arg(ctx).arg(units));
    // site (with the model's location), building, one storey
    auto dms = [](double deg) {
        double a = std::fabs(deg);
        int d = (int)a;
        double mr = (a - d) * 60;
        int mi = (int)mr;
        double sr = (mr - mi) * 60;
        int se = (int)sr;
        int mil = (int)std::lround((sr - se) * 1e6);
        int sg = deg < 0 ? -1 : 1;
        return QString("(%1,%2,%3,%4)").arg(sg * d).arg(sg * mi).arg(sg * se).arg(sg * mil);
    };
    int sitePl = s.add(QString("IFCLOCALPLACEMENT($,#%1)").arg(ax));
    int site = s.add(QString("IFCSITE('%1',%2,'Site',$,$,#%3,$,$,.ELEMENT.,%4,%5,0.,$,$)")
                         .arg(ifcGuid(), OH).arg(sitePl).arg(dms(m.shadow.latitude), dms(m.shadow.longitude)));
    int bPl = s.add(QString("IFCLOCALPLACEMENT(#%1,#%2)").arg(sitePl).arg(ax));
    int bldg = s.add(QString("IFCBUILDING('%1',%2,%3,$,$,#%4,$,$,.ELEMENT.,$,$,$)").arg(ifcGuid(), OH, stepStr(base)).arg(bPl));
    int stPl = s.add(QString("IFCLOCALPLACEMENT(#%1,#%2)").arg(bPl).arg(ax));
    int storey = s.add(QString("IFCBUILDINGSTOREY('%1',%2,'Level 1',$,$,#%3,$,$,.ELEMENT.,0.)").arg(ifcGuid(), OH).arg(stPl));
    s.add(QString("IFCRELAGGREGATES('%1',%2,$,$,#%3,(#%4))").arg(ifcGuid(), OH).arg(proj).arg(site));
    s.add(QString("IFCRELAGGREGATES('%1',%2,$,$,#%3,(#%4))").arg(ifcGuid(), OH).arg(site).arg(bldg));
    s.add(QString("IFCRELAGGREGATES('%1',%2,$,$,#%3,(#%4))").arg(ifcGuid(), OH).arg(bldg).arg(storey));
    // the materials' colours, as presentation styles
    std::map<int, int> styleOf, ifcMat;
    auto style = [&](int mat) {
        auto it = styleOf.find(mat);
        if (it != styleOf.end()) return it->second;
        QColor c = mat >= 0 ? m.materials[mat].color : m.style.frontColor;
        double a = mat >= 0 ? m.materials[mat].alpha : 1.0;
        QString nm = mat >= 0 ? m.materials[mat].name : QString("Default");
        int col = s.add(QString("IFCCOLOURRGB($,%1,%2,%3)").arg(num(c.redF()), num(c.greenF()), num(c.blueF())));
        int rend = s.add(QString("IFCSURFACESTYLERENDERING(#%1,%2,$,$,$,$,$,$,.FLAT.)").arg(col).arg(num(1 - a)));
        int ss = s.add(QString("IFCSURFACESTYLE(%1,.BOTH.,(#%2))").arg(stepStr(nm)).arg(rend));
        int psa = s.add(QString("IFCPRESENTATIONSTYLEASSIGNMENT((#%1))").arg(ss));
        styleOf[mat] = psa;
        return psa;
    };
    std::vector<int> contained, spaces;
    std::map<int, std::vector<int>> byMaterial;   // IfcMaterial associations
    int elements = 0;
    for (auto& part : parts) {
        // welded corners, so shells (faces joined at their edges) can be told apart
        std::map<std::tuple<long long, long long, long long>, int> ids;
        std::vector<Vec3> verts;
        auto vid = [&](const Vec3& p) {
            auto key = std::make_tuple(std::llround(p.x * 100), std::llround(p.y * 100), std::llround(p.z * 100));
            auto it = ids.find(key);
            if (it != ids.end()) return it->second;
            ids[key] = (int)verts.size();
            verts.push_back(p);
            return (int)verts.size() - 1;
        };
        struct IPoly { std::vector<int> outer; std::vector<std::vector<int>> holes; int material; };
        std::vector<IPoly> polys;
        for (auto& p : part.polys) {
            IPoly ip;
            ip.material = p.material;
            auto conv = [&](const std::vector<Vec3>& l) {
                std::vector<int> r;
                for (auto& v : l) { int i = vid(v); if (r.empty() || r.back() != i) r.push_back(i); }
                while (r.size() > 1 && r.front() == r.back()) r.pop_back();
                return r;
            };
            ip.outer = conv(p.outer);
            if (ip.outer.size() < 3) continue;
            for (auto& h : p.holes) { auto hh = conv(h); if (hh.size() >= 3) ip.holes.push_back(hh); }
            polys.push_back(std::move(ip));
        }
        if (polys.empty()) continue;
        // shells: faces sharing a corner
        std::vector<int> parent(verts.size());
        for (size_t i = 0; i < parent.size(); ++i) parent[i] = (int)i;
        std::function<int(int)> root = [&](int a) { while (parent[a] != a) a = parent[a] = parent[parent[a]]; return a; };
        for (auto& p : polys) {
            for (int v : p.outer) parent[root(v)] = root(p.outer[0]);
            for (auto& h : p.holes) for (int v : h) parent[root(v)] = root(p.outer[0]);
        }
        std::map<int, std::vector<int>> shells;
        for (size_t i = 0; i < polys.size(); ++i) shells[root(polys[i].outer[0])].push_back((int)i);
        std::map<int, int> pt;   // vertex -> IfcCartesianPoint
        auto point = [&](int v) {
            auto it = pt.find(v);
            if (it != pt.end()) return it->second;
            const Vec3& p = verts[v];
            int id = s.add(QString("IFCCARTESIANPOINT((%1,%2,%3))").arg(num(p.x), num(p.y), num(p.z)));
            pt[v] = id;
            return id;
        };
        std::vector<int> items;
        bool allClosed = true;
        std::map<int, int> matCount;
        for (auto& [r, list] : shells) {
            // closed when every edge is used by two faces, once each way
            std::map<std::pair<int, int>, int> edgeUse;
            std::map<int, int> mats;
            for (int i : list) {
                auto addLoop = [&](const std::vector<int>& l) {
                    for (size_t k = 0; k < l.size(); ++k) edgeUse[{l[k], l[(k + 1) % l.size()]}]++;
                };
                addLoop(polys[i].outer);
                for (auto& h : polys[i].holes) addLoop(h);
                mats[polys[i].material]++;
                matCount[polys[i].material]++;
            }
            bool closed = true;
            for (auto& [e, n] : edgeUse) {
                auto back = edgeUse.find({e.second, e.first});
                if (n != 1 || back == edgeUse.end() || back->second != 1) { closed = false; break; }
            }
            std::vector<int> faces;
            for (int i : list) {
                std::vector<int> bounds;
                auto loopOf = [&](const std::vector<int>& l) {
                    std::vector<int> ps;
                    for (int v : l) ps.push_back(point(v));
                    return s.add("IFCPOLYLOOP(" + refs(ps) + ")");
                };
                bounds.push_back(s.add(QString("IFCFACEOUTERBOUND(#%1,.T.)").arg(loopOf(polys[i].outer))));
                for (auto& h : polys[i].holes) bounds.push_back(s.add(QString("IFCFACEBOUND(#%1,.T.)").arg(loopOf(h))));
                faces.push_back(s.add("IFCFACE(" + refs(bounds) + ")"));
            }
            int item;
            if (closed) {
                int sh = s.add("IFCCLOSEDSHELL(" + refs(faces) + ")");
                item = s.add(QString("IFCFACETEDBREP(#%1)").arg(sh));
            } else {
                allClosed = false;
                int sh = s.add("IFCOPENSHELL(" + refs(faces) + ")");
                item = s.add(QString("IFCSHELLBASEDSURFACEMODEL((#%1))").arg(sh));
            }
            int dom = -1, best = -1;
            for (auto& [mt, n] : mats) if (n > best) { best = n; dom = mt; }
            s.add(QString("IFCSTYLEDITEM(#%1,(#%2),$)").arg(item).arg(style(dom)));
            items.push_back(item);
        }
        int rep = s.add(QString("IFCSHAPEREPRESENTATION(#%1,'Body','%2',%3)").arg(ctx).arg(allClosed ? "Brep" : "SurfaceModel").arg(refs(items)));
        int pds = s.add(QString("IFCPRODUCTDEFINITIONSHAPE($,$,(#%1))").arg(rep));
        int pl = s.add(QString("IFCLOCALPLACEMENT(#%1,#%2)").arg(stPl).arg(ax));
        QString cls = part.cls.isEmpty() ? QString("IfcBuildingElementProxy") : part.cls;
        QString objType = part.objectType.isEmpty() ? QString("$") : stepStr(part.objectType);
        QString head = QString("'%1',%2,%3,$,%4,#%5,#%6").arg(ifcGuid(), OH, stepStr(part.name.isEmpty() ? cls.mid(3) : part.name), objType).arg(pl).arg(pds);
        int el;
        if (cls == "IfcSpace") {
            el = s.add(QString("IFCSPACE(%1,$,.ELEMENT.,.INTERNAL.,$)").arg(head));
            spaces.push_back(el);
        } else {
            QString tail;
            for (auto& c : kClasses) if (cls == c.name) tail = c.tail;
            el = s.add(QString("%1(%2,$%3)").arg(cls.toUpper(), head, tail));
            contained.push_back(el);
        }
        int dom = -1, best = -1;
        for (auto& [mt, n] : matCount) if (n > best) { best = n; dom = mt; }
        if (dom >= 0) byMaterial[dom].push_back(el);
        ++elements;
    }
    if (!elements) { if (err) *err = "There are no faces to export."; return false; }
    if (!contained.empty())
        s.add(QString("IFCRELCONTAINEDINSPATIALSTRUCTURE('%1',%2,$,$,%3,#%4)").arg(ifcGuid(), OH, refs(contained)).arg(storey));
    if (!spaces.empty()) s.add(QString("IFCRELAGGREGATES('%1',%2,$,$,#%3,%4)").arg(ifcGuid(), OH).arg(storey).arg(refs(spaces)));
    for (auto& [mat, list] : byMaterial) {
        int im = s.add(QString("IFCMATERIAL(%1)").arg(stepStr(m.materials[mat].name)));
        s.add(QString("IFCRELASSOCIATESMATERIAL('%1',%2,$,$,%3,#%4)").arg(ifcGuid(), OH, refs(list)).arg(im));
    }
    QString file = QString("ISO-10303-21;\nHEADER;\nFILE_DESCRIPTION(('ViewDefinition [CoordinationView_V2.0]'),'2;1');\n"
                           "FILE_NAME(%1,'%2',(''),(''),'Folio Gable %3','Folio Gable','');\nFILE_SCHEMA(('IFC2X3'));\nENDSEC;\nDATA;\n")
                       .arg(stepStr(QFileInfo(path).fileName()), QDateTime::currentDateTime().toString("yyyy-MM-ddTHH:mm:ss"), GABLE_VERSION) +
                   s.body + "ENDSEC;\nEND-ISO-10303-21;\n";
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) { if (err) *err = "Cannot write " + path; return false; }
    f.write(file.toLatin1());
    if (!f.commit()) { if (err) *err = "Cannot write " + path; return false; }
    if (err) err->clear();
    return true;
}

}  // namespace gable
