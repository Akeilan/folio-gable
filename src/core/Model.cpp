#include "core/Model.h"
#include "core/Tessellate.h"
#include <QRegularExpression>
#include <atomic>
#include <cmath>
#include <set>
#include <functional>
#include <numeric>

namespace gable {

static std::atomic<uint64_t> g_rev{100};
uint64_t nextRevision() { return ++g_rev; }

// ---------------------------------------------------------------- Entities

int64_t Entities::cellKey(int64_t x, int64_t y, int64_t z) {
    return (x * 73856093LL) ^ (y * 19349663LL) ^ (z * 83492791LL);
}

void Entities::indexInsert(int v) const {
    const Vec3& p = verts[v].p;
    int64_t cx = (int64_t)std::floor(p.x / kCell), cy = (int64_t)std::floor(p.y / kCell), cz = (int64_t)std::floor(p.z / kCell);
    grid_[cellKey(cx, cy, cz)].push_back(v);
}

void Entities::ensureIndex() const {
    if (!indexDirty_) return;
    grid_.clear();
    grid_.reserve(verts.size() * 2 + 16);
    for (int i = 0; i < (int)verts.size(); ++i)
        if (verts[i].alive) indexInsert(i);
    indexDirty_ = false;
}

int Entities::addVertex(const Vec3& p) {
    Vertex v;
    v.p = p;
    verts.push_back(v);
    int id = (int)verts.size() - 1;
    if (!indexDirty_) indexInsert(id);
    touch();
    return id;
}

int Entities::findVertex(const Vec3& p, double tol) const {
    ensureIndex();
    int64_t cx = (int64_t)std::floor(p.x / kCell), cy = (int64_t)std::floor(p.y / kCell), cz = (int64_t)std::floor(p.z / kCell);
    int best = -1;
    double bd = tol * tol;
    int r = tol > kCell ? (int)std::ceil(tol / kCell) : 1;
    for (int dx = -r; dx <= r; ++dx)
        for (int dy = -r; dy <= r; ++dy)
            for (int dz = -r; dz <= r; ++dz) {
                auto it = grid_.find(cellKey(cx + dx, cy + dy, cz + dz));
                if (it == grid_.end()) continue;
                for (int v : it->second) {
                    if (v >= (int)verts.size() || !verts[v].alive) continue;
                    double d = (verts[v].p - p).length2();
                    if (d <= bd) { bd = d; best = v; }
                }
            }
    return best;
}

int Entities::vertexAt(const Vec3& p) {
    int v = findVertex(p);
    return v >= 0 ? v : addVertex(p);
}

void Entities::moveVertex(int v, const Vec3& p) {
    verts[v].p = p;
    indexDirty_ = true;
    touch();
}

int Entities::findEdge(int a, int b) const {
    if (a < 0 || b < 0) return -1;
    const Vertex& va = verts[a];
    for (int e : va.edges) {
        const Edge& E = edges[e];
        if (!E.alive) continue;
        if ((E.v[0] == a && E.v[1] == b) || (E.v[0] == b && E.v[1] == a)) return e;
    }
    return -1;
}

int Entities::addEdge(int a, int b) {
    if (a == b || a < 0 || b < 0) return -1;
    int e = findEdge(a, b);
    if (e >= 0) return e;
    Edge E;
    E.v[0] = a; E.v[1] = b;
    edges.push_back(E);
    e = (int)edges.size() - 1;
    verts[a].edges.push_back(e);
    verts[b].edges.push_back(e);
    touch();
    return e;
}

void Entities::removeFace(int f) {
    if (f < 0 || f >= (int)faces.size() || !faces[f].alive) return;
    unlinkFace(f);
    faces[f].alive = false;
    touch();
}

void Entities::removeEdge(int e) {
    if (e < 0 || e >= (int)edges.size() || !edges[e].alive) return;
    std::vector<int> fs = edges[e].faces;
    for (int f : fs) removeFace(f);
    Edge& E = edges[e];
    for (int k = 0; k < 2; ++k) {
        auto& ve = verts[E.v[k]].edges;
        ve.erase(std::remove(ve.begin(), ve.end(), e), ve.end());
    }
    E.alive = false;
    removeVertexIfOrphan(E.v[0]);
    removeVertexIfOrphan(E.v[1]);
    touch();
}

void Entities::removeVertexIfOrphan(int v) {
    if (v < 0 || !verts[v].alive) return;
    for (int e : verts[v].edges)
        if (edges[e].alive) return;
    verts[v].alive = false;
    verts[v].edges.clear();
    indexDirty_ = true;
}

static void forEachLoopEdge(const std::vector<int>& loop, const std::function<void(int, int)>& fn) {
    for (size_t i = 0; i < loop.size(); ++i) fn(loop[i], loop[(i + 1) % loop.size()]);
}

void Entities::linkFace(int f) {
    Face& F = faces[f];
    auto link = [&](int a, int b) {
        int e = addEdge(a, b);
        if (e < 0) return;
        auto& fl = edges[e].faces;
        if (std::find(fl.begin(), fl.end(), f) == fl.end()) fl.push_back(f);
    };
    forEachLoopEdge(F.outer, link);
    for (auto& h : F.holes) forEachLoopEdge(h, link);
}

void Entities::unlinkFace(int f) {
    Face& F = faces[f];
    auto unlink = [&](int a, int b) {
        int e = findEdge(a, b);
        if (e < 0) return;
        auto& fl = edges[e].faces;
        fl.erase(std::remove(fl.begin(), fl.end(), f), fl.end());
    };
    forEachLoopEdge(F.outer, unlink);
    for (auto& h : F.holes) forEachLoopEdge(h, unlink);
}

int Entities::addFace(const std::vector<int>& outer, const std::vector<std::vector<int>>& holes, const Plane& plane) {
    if (outer.size() < 3) return -1;
    Face F;
    F.outer = outer;
    F.holes = holes;
    F.plane = plane;
    faces.push_back(F);
    int f = (int)faces.size() - 1;
    linkFace(f);
    touch();
    return f;
}

void Entities::clear() {
    verts.clear(); edges.clear(); faces.clear(); curves.clear(); insts.clear();
    guides.clear(); dims.clear(); texts.clear(); sections.clear();
    grid_.clear(); indexDirty_ = true;
    touch();
}

static double loopArea3(const Entities& E, const std::vector<int>& loop, const Vec3& n) {
    Vec3 s;
    for (size_t i = 0; i < loop.size(); ++i) s += E.verts[loop[i]].p.cross(E.verts[loop[(i + 1) % loop.size()]].p);
    return s.dot(n) * 0.5;
}

double Entities::faceArea(int f) const {
    const Face& F = faces[f];
    double a = std::fabs(loopArea3(*this, F.outer, F.plane.n));
    for (auto& h : F.holes) a -= std::fabs(loopArea3(*this, h, F.plane.n));
    return a;
}

Vec3 Entities::faceCentroid(int f) const {
    const Face& F = faces[f];
    Vec3 c;
    for (int v : F.outer) c += verts[v].p;
    return F.outer.empty() ? c : c / (double)F.outer.size();
}

bool Entities::pointInFace(int f, const Vec3& p, double tol) const {
    const Face& F = faces[f];
    Frame2 fr(F.plane);
    Vec2 q = fr.to2(p);
    std::vector<Vec2> pts;
    auto inLoop = [&](const std::vector<int>& loop, bool& onBoundary) {
        pts.clear();
        for (int v : loop) pts.push_back(fr.to2(verts[v].p));
        for (size_t i = 0; i < pts.size(); ++i)
            if (distPointSeg2(q, pts[i], pts[(i + 1) % pts.size()]) < tol) { onBoundary = true; return true; }
        return pointInPoly2(q, pts.data(), pts.size());
    };
    bool onB = false;
    if (!inLoop(F.outer, onB)) return false;
    if (onB) return true;
    for (auto& h : F.holes) {
        bool hb = false;
        if (inLoop(h, hb)) return hb;
    }
    return true;
}

std::vector<int> Entities::faceEdges(int f) const {
    std::vector<int> r;
    const Face& F = faces[f];
    auto add = [&](const std::vector<int>& loop) {
        for (size_t i = 0; i < loop.size(); ++i) {
            int e = findEdge(loop[i], loop[(i + 1) % loop.size()]);
            if (e >= 0) r.push_back(e);
        }
    };
    add(F.outer);
    for (auto& h : F.holes) add(h);
    return r;
}

std::vector<int> Entities::faceVerts(int f) const {
    std::vector<int> r = faces[f].outer;
    for (auto& h : faces[f].holes) r.insert(r.end(), h.begin(), h.end());
    return r;
}

BBox Entities::bbox() const {
    BBox b;
    for (auto& v : verts)
        if (v.alive) b.add(v.p);
    return b;
}

bool Entities::empty() const {
    for (auto& e : edges) if (e.alive) return false;
    for (auto& i : insts) if (i.alive) return false;
    for (auto& g : guides) if (g.alive) return false;
    for (auto& d : dims) if (d.alive) return false;
    for (auto& t : texts) if (t.alive) return false;
    for (auto& s : sections) if (s.alive) return false;
    return true;
}

int Entities::aliveCount(EntKind k) const {
    int n = 0;
    switch (k) {
    case EntKind::Vertex: for (auto& x : verts) n += x.alive; break;
    case EntKind::Edge: for (auto& x : edges) n += x.alive; break;
    case EntKind::Face: for (auto& x : faces) n += x.alive; break;
    case EntKind::Instance: for (auto& x : insts) n += x.alive; break;
    case EntKind::Guide: for (auto& x : guides) n += x.alive; break;
    case EntKind::Dimension: for (auto& x : dims) n += x.alive; break;
    case EntKind::Text: for (auto& x : texts) n += x.alive; break;
    case EntKind::Section: for (auto& x : sections) n += x.alive; break;
    default: break;
    }
    return n;
}

// ---------------------------------------------------------------- Material

QImage Material::image() const {
    if (!hasTexture || texturePng.isEmpty()) return QImage();
    return QImage::fromData(texturePng);
}

// ---------------------------------------------------------------- Shadows

Vec3 ShadowSettings::sunDirection() const {
    static const int cum[] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
    int n = cum[std::clamp(month, 1, 12) - 1] + day;
    double B = 2 * kPi * (n - 81) / 364.0;
    double eot = 9.87 * std::sin(2 * B) - 7.53 * std::cos(B) - 1.5 * std::sin(B);  // minutes
    double decl = 23.45 * std::sin(2 * kPi * (284 + n) / 365.0) * kPi / 180;
    double solar = hour + (4 * longitude - 60 * utcOffset + eot) / 60.0;
    double H = (solar - 12) * 15 * kPi / 180;
    double phi = latitude * kPi / 180;
    double sinAlt = std::sin(phi) * std::sin(decl) + std::cos(phi) * std::cos(decl) * std::cos(H);
    double alt = std::asin(std::clamp(sinAlt, -1.0, 1.0));
    double az = std::atan2(std::sin(H), std::cos(H) * std::sin(phi) - std::tan(decl) * std::cos(phi)) + kPi;
    double east = std::cos(alt) * std::sin(az), north = std::cos(alt) * std::cos(az), up = std::sin(alt);
    double na = northAngle * kPi / 180;
    // North is +Y rotated by northAngle (clockwise from +Y).
    Vec3 N(std::sin(na), std::cos(na), 0), E(std::cos(na), -std::sin(na), 0);
    return (E * east + N * north + Vec3(0, 0, 1) * up).normalized();
}

// ---------------------------------------------------------------- Model

Model::Model() { reset(); }

void Model::reset() {
    materials.clear();
    tags.clear();
    defs.clear();
    root.clear();
    scenes.clear();
    style = Style();
    shadow = ShadowSettings();
    units = UnitSettings();
    camera = CameraState();
    author.clear();
    description.clear();
    Tag untagged;
    untagged.name = "Untagged";
    tags.push_back(untagged);
    axesOrigin = Vec3();
    axesX = {1, 0, 0}; axesY = {0, 1, 0}; axesZ = {0, 0, 1};
}

int Model::addMaterial(const QString& name, const QColor& c) {
    Material m;
    m.name = uniqueMaterialName(name);
    m.color = c;
    m.rev = nextRevision();
    materials.push_back(m);
    return (int)materials.size() - 1;
}

int Model::findMaterial(const QString& name) const {
    for (int i = 0; i < (int)materials.size(); ++i)
        if (materials[i].name == name) return i;
    return -1;
}

QString Model::uniqueMaterialName(const QString& base) const {
    if (findMaterial(base) < 0) return base;
    for (int i = 1;; ++i) {
        QString n = QString("%1 %2").arg(base).arg(i);
        if (findMaterial(n) < 0) return n;
    }
}

QString Model::uniqueDefName(const QString& base) const {
    auto exists = [&](const QString& n) {
        for (auto& d : defs) if (d.alive && d.name == n) return true;
        return false;
    };
    if (!exists(base)) return base;
    for (int i = 1;; ++i) {
        QString n = QString("%1#%2").arg(base).arg(i);
        if (!exists(n)) return n;
    }
}

int Model::addDef(const QString& name, bool group) {
    ComponentDef d;
    d.name = uniqueDefName(name);
    d.isGroup = group;
    defs.push_back(std::move(d));
    return (int)defs.size() - 1;
}

int Model::instanceCount(int def) const {
    int n = 0;
    std::function<void(const Entities&, int)> walk = [&](const Entities& e, int mult) {
        for (auto& in : e.insts) {
            if (!in.alive) continue;
            if (in.def == def) n += mult;
            else walk(defs[in.def].ents, mult);
        }
    };
    walk(root, 1);
    return n;
}

BBox Model::entitiesBBox(const Entities& e, bool includeHidden) const {
    BBox b;
    for (auto& v : e.verts)
        if (v.alive) b.add(v.p);
    for (auto& in : e.insts) {
        if (!in.alive || in.def < 0 || (!includeHidden && in.hidden)) continue;
        b.add(defBBox(in.def).transformed(in.xf));
    }
    for (auto& g : e.guides)
        if (g.alive && g.isPoint) b.add(g.p);
    for (auto& d : e.dims)
        if (d.alive) { b.add(d.a); b.add(d.b); }
    return b;
}

BBox Model::defBBox(int def) const { return entitiesBBox(defs[def].ents); }

void Model::purgeUnused() {
    // Remove definitions with no instances.
    bool changed = true;
    while (changed) {
        changed = false;
        for (int d = 0; d < (int)defs.size(); ++d)
            if (defs[d].alive && instanceCount(d) == 0) { defs[d].alive = false; defs[d].ents.clear(); changed = true; }
    }
}

// ---------------------------------------------------------------- Units

static double unitFactor(LengthUnit u) {
    switch (u) {
    case LengthUnit::Millimeter: return 1;
    case LengthUnit::Centimeter: return 10;
    case LengthUnit::Meter: return 1000;
    case LengthUnit::Inch: case LengthUnit::ArchitecturalInch: return 25.4;
    case LengthUnit::Foot: return 304.8;
    }
    return 1;
}
static const char* unitSuffix(LengthUnit u) {
    switch (u) {
    case LengthUnit::Millimeter: return "mm";
    case LengthUnit::Centimeter: return "cm";
    case LengthUnit::Meter: return "m";
    case LengthUnit::Inch: case LengthUnit::ArchitecturalInch: return "\"";
    case LengthUnit::Foot: return "'";
    }
    return "";
}

QString formatLength(double mm, const UnitSettings& u, bool withUnit) {
    if (u.unit == LengthUnit::ArchitecturalInch) {
        bool neg = mm < 0;
        double inches = std::fabs(mm) / 25.4;
        int denom = 1 << std::clamp(u.precision, 0, 6);  // 1, 1/2 ... 1/64
        long total = std::lround(inches * denom);
        long feet = total / (12 * denom);
        long rem = total % (12 * denom);
        long whole = rem / denom, frac = rem % denom;
        QString s = neg ? "-" : "";
        if (feet) s += QString("%1' ").arg(feet);
        s += QString::number(whole);
        if (frac) {
            long g = std::gcd(frac, (long)denom);
            s += QString(" %1/%2").arg(frac / g).arg(denom / g);
        }
        s += "\"";
        if (std::fabs(inches * denom - std::round(inches * denom)) > 1e-6) s = "~ " + s;
        return s;
    }
    double v = mm / unitFactor(u.unit);
    QString s = QString::number(v, 'f', u.precision);
    double back = s.toDouble() * unitFactor(u.unit);
    if (std::fabs(back - mm) > 1e-6 * std::max(1.0, std::fabs(mm))) s = "~ " + s;
    if (withUnit && u.showUnits) s += unitSuffix(u.unit);
    return s;
}

QString formatArea(double mm2, const UnitSettings& u) {
    switch (u.unit) {
    case LengthUnit::Millimeter: return QString::number(mm2, 'f', u.precision) + " mm²";
    case LengthUnit::Centimeter: return QString::number(mm2 / 100, 'f', std::max(u.precision, 1)) + " cm²";
    case LengthUnit::Meter: return QString::number(mm2 / 1e6, 'f', std::max(u.precision, 2)) + " m²";
    case LengthUnit::Inch: return QString::number(mm2 / 645.16, 'f', std::max(u.precision, 2)) + " in²";
    case LengthUnit::Foot: case LengthUnit::ArchitecturalInch:
        return QString::number(mm2 / 92903.04, 'f', 2) + " ft²";
    }
    return {};
}

QString formatVolume(double mm3, const UnitSettings& u) {
    switch (u.unit) {
    case LengthUnit::Millimeter: return QString::number(mm3, 'f', 0) + " mm³";
    case LengthUnit::Centimeter: return QString::number(mm3 / 1000, 'f', 2) + " cm³";
    case LengthUnit::Meter: return QString::number(mm3 / 1e9, 'f', 3) + " m³";
    case LengthUnit::Inch: return QString::number(mm3 / 16387.064, 'f', 2) + " in³";
    default: return QString::number(mm3 / 28316846.6, 'f', 2) + " ft³";
    }
}

QString formatAngle(double rad, const UnitSettings& u) {
    return QString::number(rad * 180 / kPi, 'f', u.anglePrecision) + "°";
}

bool parseLength(const QString& in, const UnitSettings& u, double& mm) {
    QString s = in.trimmed();
    s.replace(',', '.');
    if (s.isEmpty()) return false;
    // feet & inches: 5'6", 5' 6 1/2", 6"
    static QRegularExpression ftIn(R"(^(-?\d+(?:\.\d+)?)\s*'\s*(?:(\d+(?:\.\d+)?)?\s*(?:(\d+)\s*/\s*(\d+))?\s*"?)?$)");
    static QRegularExpression inch(R"(^(-?\d+(?:\.\d+)?)?\s*(?:(\d+)\s*/\s*(\d+))?\s*"$)");
    static QRegularExpression num(R"(^(-?(?:\d+(?:\.\d*)?|\.\d+))\s*([a-zA-Z]*)$)");
    auto m = ftIn.match(s);
    if (m.hasMatch()) {
        double ft = m.captured(1).toDouble();
        double ins = m.captured(2).isEmpty() ? 0 : m.captured(2).toDouble();
        if (!m.captured(3).isEmpty() && m.captured(4).toDouble() != 0) ins += m.captured(3).toDouble() / m.captured(4).toDouble();
        mm = (ft * 12 + (ft < 0 ? -ins : ins)) * 25.4;
        return true;
    }
    m = inch.match(s);
    if (m.hasMatch() && (!m.captured(1).isEmpty() || !m.captured(2).isEmpty())) {
        double ins = m.captured(1).isEmpty() ? 0 : m.captured(1).toDouble();
        if (!m.captured(2).isEmpty() && m.captured(3).toDouble() != 0) ins += m.captured(2).toDouble() / m.captured(3).toDouble();
        mm = ins * 25.4;
        return true;
    }
    m = num.match(s);
    if (!m.hasMatch()) return false;
    double v = m.captured(1).toDouble();
    QString suf = m.captured(2).toLower();
    double f;
    if (suf.isEmpty()) f = unitFactor(u.unit);
    else if (suf == "mm") f = 1;
    else if (suf == "cm") f = 10;
    else if (suf == "m") f = 1000;
    else if (suf == "km") f = 1e6;
    else if (suf == "in") f = 25.4;
    else if (suf == "ft") f = 304.8;
    else if (suf == "yd") f = 914.4;
    else return false;
    mm = v * f;
    return true;
}

}  // namespace gable
