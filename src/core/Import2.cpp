// Folio Gable — more import formats (G2-04): 3D Studio (.3ds), Google Earth (.kmz) and IFC (.ifc).
// All read here, from the formats' published structure; no outside libraries besides zlib for the
// zip inside a .kmz.
#include "core/FileFormats.h"
#include "core/Solids.h"
#include "core/Geometry.h"
#include <QDir>
#include <QUrl>
#include <QJsonObject>
#include <QJsonDocument>
#include <QJsonArray>
#include <QImage>
#include <QBuffer>
#include <QDomDocument>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QTemporaryDir>
#include <QtEndian>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <zlib.h>

namespace gable {

int importDAEInto(Model& m, Entities& target, const QString& path, const ImportOptions& opt, QString* err);

namespace {

Vec3 scaled(const Vec3& p, const ImportOptions& o) {
    Vec3 q = o.swapYZ ? Vec3(p.x, -p.z, p.y) : p;
    return q * o.scale;
}

// A polygon (with holes) as a face; returns the face or -1. Points in model space.
int addFaceWithHoles(Entities& E, const std::vector<Vec3>& outerPts, const std::vector<std::vector<Vec3>>& holePts,
                     int mat, std::set<std::vector<int>>* seen) {
    auto loopOf = [&](const std::vector<Vec3>& pts) {
        std::vector<int> loop;
        for (auto& p : pts) {
            int v = E.vertexAt(p);
            if (loop.empty() || loop.back() != v) loop.push_back(v);
        }
        while (loop.size() > 1 && loop.front() == loop.back()) loop.pop_back();
        return loop;
    };
    std::vector<int> outer = loopOf(outerPts);
    if (outer.size() < 3) return -1;
    if (std::set<int>(outer.begin(), outer.end()).size() != outer.size()) return -1;
    if (seen && holePts.empty()) {
        std::vector<int> key(outer);
        std::sort(key.begin(), key.end());
        if (!seen->insert(key).second) return -1;
    }
    Vec3 n = geom::newellNormal(E, outer);
    if (n.isZero() || n.length() < 1e-9) return -1;
    n = n.normalized();
    std::vector<std::vector<int>> holes;
    for (auto& h : holePts) {
        auto l = loopOf(h);
        if (l.size() < 3) continue;
        Vec3 hn = geom::newellNormal(E, l);
        if (hn.dot(n) > 0) std::reverse(l.begin(), l.end());   // holes run the other way round
        holes.push_back(l);
    }
    int f = E.addFace(outer, holes, Plane(n, E.verts[outer[0]].p));
    if (f >= 0) E.faces[f].matFront = mat;
    if (f >= 0 && outer.size() > 3 && holes.empty()) geom::fixWarpedFaces(E, {f});
    return f;
}

// ============================================================== 3D Studio

struct Chunk {
    const uchar* p;
    quint32 len;  // whole chunk, header included
    quint16 id;
};

bool read3DS(Model& m, Entities& E, const QByteArray& data, const ImportOptions& opt, QString* err) {
    const uchar* base = reinterpret_cast<const uchar*>(data.constData());
    const qint64 size = data.size();
    if (size < 6 || qFromLittleEndian<quint16>(base) != 0x4D4D) {
        if (err) *err = "This isn't a 3D Studio (.3ds) file.";
        return false;
    }
    auto u16 = [](const uchar* p) { return qFromLittleEndian<quint16>(p); };
    auto u32 = [](const uchar* p) { return qFromLittleEndian<quint32>(p); };
    auto f32 = [](const uchar* p) { float f; quint32 v = qFromLittleEndian<quint32>(p); std::memcpy(&f, &v, 4); return f; };
    struct Mat3ds { QString name; QColor color{200, 200, 200}; double alpha = 1; };
    std::map<QString, Mat3ds> mats;
    struct Mesh { QString name; std::vector<Vec3> v; std::vector<std::array<int, 3>> f; std::vector<std::pair<QString, std::vector<int>>> faceMats; };
    std::vector<Mesh> meshes;
    std::function<void(const uchar*, const uchar*, int)> walk;
    Mat3ds* curMat = nullptr;
    Mesh* cur = nullptr;
    auto colourIn = [&](const uchar* p, const uchar* end) -> QColor {
        // a colour chunk holds a float (0x0010, 0x0013) or byte (0x0011, 0x0012) RGB sub-chunk
        while (p + 6 <= end) {
            quint16 id = u16(p);
            quint32 len = u32(p + 2);
            if (len < 6 || p + len > end) break;
            if ((id == 0x0011 || id == 0x0012) && len >= 9) return QColor(p[6], p[7], p[8]);
            if ((id == 0x0010 || id == 0x0013) && len >= 18)
                return QColor::fromRgbF(std::clamp(f32(p + 6), 0.f, 1.f), std::clamp(f32(p + 10), 0.f, 1.f), std::clamp(f32(p + 14), 0.f, 1.f));
            p += len;
        }
        return QColor(200, 200, 200);
    };
    walk = [&](const uchar* p, const uchar* end, int depth) {
        while (p + 6 <= end) {
            quint16 id = u16(p);
            quint32 len = u32(p + 2);
            if (len < 6 || p + len > end) return;   // damaged: keep what was read
            const uchar* body = p + 6;
            const uchar* stop = p + len;
            switch (id) {
            case 0x4D4D: case 0x3D3D: walk(body, stop, depth + 1); break;
            case 0xAFFF: mats[QString()] = Mat3ds(); curMat = nullptr; walk(body, stop, depth + 1); break;
            case 0xA000: {
                QString n = QString::fromLatin1(reinterpret_cast<const char*>(body), (int)strnlen(reinterpret_cast<const char*>(body), stop - body));
                mats[n] = mats[QString()];
                mats[n].name = n;
                mats.erase(QString());
                curMat = &mats[n];
                break;
            }
            case 0xA020: if (curMat) curMat->color = colourIn(body, stop); break;
            case 0xA050:
                if (curMat && stop - body >= 8 && u16(body) == 0x0030) curMat->alpha = 1.0 - std::clamp(u16(body + 6) / 100.0, 0.0, 1.0);
                break;
            case 0x4000: {
                size_t n = strnlen(reinterpret_cast<const char*>(body), stop - body);
                meshes.push_back(Mesh());
                cur = &meshes.back();
                cur->name = QString::fromLatin1(reinterpret_cast<const char*>(body), (int)n);
                walk(body + n + 1, stop, depth + 1);
                break;
            }
            case 0x4100: walk(body, stop, depth + 1); break;
            case 0x4110:
                if (cur && stop - body >= 2) {
                    int n = u16(body);
                    for (int i = 0; i < n && body + 2 + i * 12 + 12 <= stop; ++i) {
                        const uchar* q = body + 2 + i * 12;
                        cur->v.push_back(Vec3(f32(q), f32(q + 4), f32(q + 8)));
                    }
                }
                break;
            case 0x4120:
                if (cur && stop - body >= 2) {
                    int n = u16(body);
                    const uchar* q = body + 2;
                    for (int i = 0; i < n && q + 8 <= stop; ++i, q += 8) cur->f.push_back({u16(q), u16(q + 2), u16(q + 4)});
                    walk(q, stop, depth + 1);   // face materials follow
                }
                break;
            case 0x4130:
                if (cur) {
                    size_t n = strnlen(reinterpret_cast<const char*>(body), stop - body);
                    QString mn = QString::fromLatin1(reinterpret_cast<const char*>(body), (int)n);
                    const uchar* q = body + n + 1;
                    std::vector<int> fs;
                    if (q + 2 <= stop) {
                        int c = u16(q);
                        q += 2;
                        for (int i = 0; i < c && q + 2 <= stop; ++i, q += 2) fs.push_back(u16(q));
                    }
                    cur->faceMats.push_back({mn, fs});
                }
                break;
            default: break;
            }
            p += len;
        }
    };
    walk(base, base + size, 0);
    if (meshes.empty()) {
        if (err) *err = "The 3D Studio file has no meshes in it.";
        return false;
    }
    std::map<QString, int> matIds;
    auto matId = [&](const QString& n) {
        auto it = matIds.find(n);
        if (it != matIds.end()) return it->second;
        int id = -1;
        auto mi = mats.find(n);
        if (mi != mats.end()) {
            id = m.findMaterial(n);
            if (id < 0) {
                id = m.addMaterial(n, mi->second.color);
                m.materials[id].alpha = mi->second.alpha;
            }
        }
        matIds[n] = id;
        return id;
    };
    for (auto& mesh : meshes) {
        if (mesh.f.empty()) continue;
        int def = m.addDef(mesh.name.isEmpty() ? "Mesh" : mesh.name, true);
        Entities sub;
        std::vector<int> fmat(mesh.f.size(), -1);
        for (auto& [mn, fs] : mesh.faceMats) {
            int id = matId(mn);
            for (int fi : fs) if (fi >= 0 && fi < (int)fmat.size()) fmat[fi] = id;
        }
        std::set<std::vector<int>> seen;
        for (size_t i = 0; i < mesh.f.size(); ++i) {
            auto& t = mesh.f[i];
            if (t[0] >= (int)mesh.v.size() || t[1] >= (int)mesh.v.size() || t[2] >= (int)mesh.v.size()) continue;
            addFaceWithHoles(sub, {scaled(mesh.v[t[0]], opt), scaled(mesh.v[t[1]], opt), scaled(mesh.v[t[2]], opt)}, {}, fmat[i], &seen);
        }
        cleanupImportedMesh(sub, opt);
        m.defs[def].ents = std::move(sub);
        Instance in;
        in.def = def;
        in.name = mesh.name;
        E.insts.push_back(in);
    }
    E.touch();
    return true;
}

// ============================================================== zip (for .kmz)

bool unzipTo(const QByteArray& z, const QString& dir, QString* err) {
    const uchar* b = reinterpret_cast<const uchar*>(z.constData());
    qint64 n = z.size();
    // the end of central directory record
    qint64 eocd = -1;
    for (qint64 i = n - 22; i >= std::max<qint64>(0, n - 66000); --i)
        if (qFromLittleEndian<quint32>(b + i) == 0x06054b50) { eocd = i; break; }
    if (eocd < 0) { if (err) *err = "The .kmz file is damaged (no zip directory)."; return false; }
    int count = qFromLittleEndian<quint16>(b + eocd + 10);
    qint64 cd = qFromLittleEndian<quint32>(b + eocd + 16);
    for (int k = 0; k < count && cd + 46 <= n; ++k) {
        if (qFromLittleEndian<quint32>(b + cd) != 0x02014b50) break;
        int method = qFromLittleEndian<quint16>(b + cd + 10);
        quint32 csize = qFromLittleEndian<quint32>(b + cd + 20), usize = qFromLittleEndian<quint32>(b + cd + 24);
        int nl = qFromLittleEndian<quint16>(b + cd + 28), xl = qFromLittleEndian<quint16>(b + cd + 30), cl = qFromLittleEndian<quint16>(b + cd + 32);
        qint64 lh = qFromLittleEndian<quint32>(b + cd + 42);
        QString name = QString::fromUtf8(reinterpret_cast<const char*>(b + cd + 46), nl);
        cd += 46 + nl + xl + cl;
        if (name.endsWith('/') || name.contains("..") || lh + 30 > n) continue;
        int lnl = qFromLittleEndian<quint16>(b + lh + 26), lxl = qFromLittleEndian<quint16>(b + lh + 28);
        qint64 data = lh + 30 + lnl + lxl;
        if (data + csize > n || usize > (1u << 30)) continue;
        QByteArray out;
        if (method == 0) out = QByteArray(reinterpret_cast<const char*>(b + data), csize);
        else if (method == 8) {
            out.resize(usize);
            z_stream s{};
            if (inflateInit2(&s, -MAX_WBITS) != Z_OK) continue;
            s.next_in = const_cast<Bytef*>(b + data);
            s.avail_in = csize;
            s.next_out = reinterpret_cast<Bytef*>(out.data());
            s.avail_out = usize;
            int r = inflate(&s, Z_FINISH);
            inflateEnd(&s);
            if (r != Z_STREAM_END) continue;
        } else continue;
        QString path = QDir(dir).filePath(name);
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile f(path);
        if (f.open(QIODevice::WriteOnly)) f.write(out);
    }
    return true;
}

// ============================================================== IFC (STEP physical file)

struct SV;  // a STEP value
using SVP = std::shared_ptr<SV>;
struct SV {
    enum K { Null, Num, Str, Enum, Ref, List, Typed } k = Null;
    double num = 0;
    QString str;              // Str, Enum (without dots), Typed (type name)
    int ref = 0;
    std::vector<SV> list;     // List, Typed (its arguments)
};

struct Ent {
    QString type;
    std::vector<SV> args;
};

class StepParser {
public:
    explicit StepParser(const QByteArray& d) : s(d.constData()), n(d.size()) {}
    std::unordered_map<int, Ent> ents;
    QString schema;

    bool parse(QString* err) {
        const char* data = strstr(s, "DATA;");
        const char* hdr = strstr(s, "FILE_SCHEMA");
        if (hdr && (!data || hdr < data)) {
            const char* q = strchr(hdr, '\'');
            if (q) { const char* e = strchr(q + 1, '\''); if (e) schema = QString::fromLatin1(q + 1, int(e - q - 1)); }
        }
        if (!data) { if (err) *err = "This isn't an IFC file (no DATA section)."; return false; }
        i = (data - s) + 5;
        while (i < n) {
            skip();
            if (i >= n) break;
            if (s[i] == '#') {
                ++i;
                int id = (int)num_int();
                skip();
                if (i < n && s[i] == '=') ++i;
                skip();
                Ent e;
                e.type = word();
                skip();
                if (i < n && s[i] == '(') {
                    SV l = list();
                    e.args = std::move(l.list);
                }
                ents.emplace(id, std::move(e));
                while (i < n && s[i] != ';') ++i;
                ++i;
            } else if (strncmp(s + i, "ENDSEC", 6) == 0) break;
            else ++i;
        }
        if (ents.empty()) { if (err) *err = "The IFC file has no entities in it."; return false; }
        return true;
    }

private:
    const char* s;
    qint64 n, i = 0;
    void skip() {
        while (i < n) {
            if (s[i] == ' ' || s[i] == '\n' || s[i] == '\r' || s[i] == '\t') ++i;
            else if (s[i] == '/' && i + 1 < n && s[i + 1] == '*') {
                const char* e = strstr(s + i + 2, "*/");
                i = e ? (e - s) + 2 : n;
            } else break;
        }
    }
    long long num_int() {
        long long v = 0;
        while (i < n && s[i] >= '0' && s[i] <= '9') v = v * 10 + (s[i++] - '0');
        return v;
    }
    QString word() {
        qint64 a = i;
        while (i < n && (isalnum((uchar)s[i]) || s[i] == '_')) ++i;
        return QString::fromLatin1(s + a, int(i - a)).toUpper();
    }
    static QString decodeString(const QByteArray& raw) {
        // STEP strings: '' is a quote, \X2\hhhh...\X0\ is UTF-16, \X\hh is Latin-1, \S\c is c+128
        QString out;
        int k = 0;
        while (k < raw.size()) {
            if (raw[k] == '\\' && raw.mid(k, 4) == "\\X2\\") {
                k += 4;
                while (k + 4 <= raw.size() && raw[k] != '\\') { out += QChar(raw.mid(k, 4).toUShort(nullptr, 16)); k += 4; }
                if (raw.mid(k, 4) == "\\X0\\") k += 4;
            } else if (raw[k] == '\\' && raw.mid(k, 3) == "\\X\\" && k + 5 <= raw.size()) {
                out += QChar(raw.mid(k + 3, 2).toUShort(nullptr, 16));
                k += 5;
            } else if (raw[k] == '\\' && raw.mid(k, 3) == "\\S\\" && k + 4 <= raw.size()) {
                out += QChar((uchar)raw[k + 3] + 128);
                k += 4;
            } else out += QChar((uchar)raw[k++]);
        }
        return out;
    }
    SV value() {
        skip();
        SV v;
        if (i >= n) return v;
        char c = s[i];
        if (c == '$' || c == '*') { ++i; return v; }
        if (c == '#') { ++i; v.k = SV::Ref; v.ref = (int)num_int(); return v; }
        if (c == '\'') {
            ++i;
            QByteArray raw;
            while (i < n) {
                if (s[i] == '\'') {
                    if (i + 1 < n && s[i + 1] == '\'') { raw += '\''; i += 2; continue; }
                    ++i;
                    break;
                }
                raw += s[i++];
            }
            v.k = SV::Str;
            v.str = decodeString(raw);
            return v;
        }
        if (c == '.') {
            ++i;
            qint64 a = i;
            while (i < n && s[i] != '.') ++i;
            v.k = SV::Enum;
            v.str = QString::fromLatin1(s + a, int(i - a)).toUpper();
            ++i;
            return v;
        }
        if (c == '(') return list();
        if (c == '-' || c == '+' || (c >= '0' && c <= '9')) {
            char* e = nullptr;
            v.k = SV::Num;
            v.num = strtod(s + i, &e);
            i = e - s;
            return v;
        }
        if (c == '"') {  // binary: skip
            ++i;
            while (i < n && s[i] != '"') ++i;
            ++i;
            return v;
        }
        if (isalpha((uchar)c)) {
            v.k = SV::Typed;
            v.str = word();
            skip();
            if (i < n && s[i] == '(') v.list = list().list;
            return v;
        }
        ++i;
        return v;
    }
    SV list() {
        SV l;
        l.k = SV::List;
        ++i;  // (
        skip();
        if (i < n && s[i] == ')') { ++i; return l; }
        while (i < n) {
            l.list.push_back(value());
            skip();
            if (i < n && s[i] == ',') { ++i; continue; }
            if (i < n && s[i] == ')') { ++i; break; }
            if (i < n && s[i] == ';') break;
        }
        return l;
    }
};

class IfcReader {
public:
    IfcReader(Model& m, const ImportOptions& opt) : m(m), opt(opt) {}
    Model& m;
    const ImportOptions& opt;
    StepParser* P = nullptr;
    double unit = 1.0;   // file length unit -> mm
    int skippedOpenings = 0;
    QStringList notes;

    const Ent* ent(int id) const {
        auto it = P->ents.find(id);
        return it == P->ents.end() ? nullptr : &it->second;
    }
    const Ent* ent(const SV& v) const { return v.k == SV::Ref ? ent(v.ref) : nullptr; }
    static double num(const SV& v, double d = 0) {
        if (v.k == SV::Num) return v.num;
        if (v.k == SV::Typed && !v.list.empty()) return num(v.list[0], d);
        return d;
    }
    static const SV& arg(const Ent* e, size_t k) {
        static SV none;
        return e && k < e->args.size() ? e->args[k] : none;
    }

    Vec3 point(const SV& ref) const {
        const Ent* e = ent(ref);
        if (!e || e->type != "IFCCARTESIANPOINT") return {};
        const SV& c = arg(e, 0);
        double x = c.list.size() > 0 ? num(c.list[0]) : 0, y = c.list.size() > 1 ? num(c.list[1]) : 0, z = c.list.size() > 2 ? num(c.list[2]) : 0;
        return Vec3(x, y, z) * unit;
    }
    Vec3 dir(const SV& ref, const Vec3& def) const {
        const Ent* e = ent(ref);
        if (!e || e->type != "IFCDIRECTION") return def;
        const SV& c = arg(e, 0);
        Vec3 d(c.list.size() > 0 ? num(c.list[0]) : 0, c.list.size() > 1 ? num(c.list[1]) : 0, c.list.size() > 2 ? num(c.list[2]) : 0);
        return d.length() < 1e-12 ? def : d.normalized();
    }
    // IfcAxis2Placement3D / 2D as a matrix (location, Z axis, X axis)
    Mat4 placement(const SV& ref) const {
        const Ent* e = ent(ref);
        if (!e) return Mat4();
        if (e->type == "IFCAXIS2PLACEMENT3D") {
            Vec3 o = point(arg(e, 0));
            Vec3 z = dir(arg(e, 1), {0, 0, 1});
            Vec3 x = dir(arg(e, 2), {1, 0, 0});
            x = (x - z * x.dot(z));
            if (x.length() < 1e-9) x = std::fabs(z.x) < 0.9 ? Vec3(1, 0, 0).cross(z).cross(z) * -1 : Vec3(0, 1, 0).cross(z).cross(z) * -1;
            x = x.normalized();
            Vec3 y = z.cross(x);
            return Mat4::fromAxes(o, x, y, z);
        }
        if (e->type == "IFCAXIS2PLACEMENT2D") {
            Vec3 o = point(arg(e, 0));
            Vec3 x = dir(arg(e, 1), {1, 0, 0});
            x.z = 0;
            x = x.length() < 1e-9 ? Vec3(1, 0, 0) : x.normalized();
            return Mat4::fromAxes(o, x, Vec3(-x.y, x.x, 0), {0, 0, 1});
        }
        return Mat4();
    }
    // IfcLocalPlacement chain
    Mat4 objectPlacement(const SV& ref, int depth = 0) const {
        const Ent* e = ent(ref);
        if (!e || depth > 64) return Mat4();
        if (e->type == "IFCLOCALPLACEMENT") return objectPlacement(arg(e, 0), depth + 1) * placement(arg(e, 1));
        return Mat4();
    }
    Mat4 transformOperator(const SV& ref) const {
        const Ent* e = ent(ref);
        if (!e) return Mat4();
        // IfcCartesianTransformationOperator3D(Axis1, Axis2, LocalOrigin, Scale, Axis3) and the non-uniform one
        Vec3 x = dir(arg(e, 0), {1, 0, 0}), y = dir(arg(e, 1), {0, 1, 0});
        Vec3 o = point(arg(e, 2));
        double s = num(arg(e, 3), 1.0);
        if (s == 0) s = 1;
        Vec3 z = dir(arg(e, 4), x.cross(y).normalized());
        double sy = s, sz = s;
        if (e->type == "IFCCARTESIANTRANSFORMATIONOPERATOR3DNONUNIFORM") { sy = num(arg(e, 5), s); sz = num(arg(e, 6), s); }
        return Mat4::fromAxes(o, x * s, y * sy, z * sz);
    }

    // 2-D curve as points (in the curve's own plane, z = 0)
    std::vector<Vec3> curve2d(const SV& ref, int depth = 0) const {
        std::vector<Vec3> pts;
        const Ent* e = ent(ref);
        if (!e || depth > 16) return pts;
        if (e->type == "IFCPOLYLINE") {
            for (auto& p : arg(e, 0).list) pts.push_back(point(p));
        } else if (e->type == "IFCINDEXEDPOLYCURVE") {
            const Ent* pl = ent(arg(e, 0));
            std::vector<Vec3> all;
            if (pl)
                for (auto& c : arg(pl, 0).list)
                    all.push_back(Vec3(c.list.size() > 0 ? num(c.list[0]) : 0, c.list.size() > 1 ? num(c.list[1]) : 0, c.list.size() > 2 ? num(c.list[2]) : 0) * unit);
            const SV& segs = arg(e, 1);
            if (segs.k == SV::List && !segs.list.empty()) {
                for (auto& sg : segs.list) {
                    bool arc = sg.str == "IFCARCINDEX";
                    std::vector<int> ix;
                    for (auto& v : (sg.list.size() == 1 && sg.list[0].k == SV::List ? sg.list[0].list : sg.list)) ix.push_back((int)num(v) - 1);
                    if (arc && ix.size() == 3) {
                        auto a = arcPoints(all[ix[0]], all[ix[1]], all[ix[2]]);
                        for (auto& p : a) if (pts.empty() || !pts.back().near(p)) pts.push_back(p);
                    } else
                        for (int k : ix) if (k >= 0 && k < (int)all.size() && (pts.empty() || !pts.back().near(all[k]))) pts.push_back(all[k]);
                }
            } else pts = all;
        } else if (e->type == "IFCCOMPOSITECURVE") {
            for (auto& seg : arg(e, 0).list) {
                const Ent* s = ent(seg);
                auto sub = curve2d(arg(s, 2), depth + 1);
                if (s && arg(s, 1).k == SV::Enum && arg(s, 1).str == "F") std::reverse(sub.begin(), sub.end());
                for (auto& p : sub) if (pts.empty() || !pts.back().near(p)) pts.push_back(p);
            }
        } else if (e->type == "IFCTRIMMEDCURVE") {
            const Ent* basis = ent(arg(e, 0));
            if (basis && basis->type == "IFCCIRCLE") {
                Mat4 pl = placement(arg(basis, 0));
                double r = num(arg(basis, 1)) * unit;
                auto param = [&](const SV& trim, double def) {
                    for (auto& t : trim.list) if (t.k == SV::Typed && t.str == "IFCPARAMETERVALUE") return num(t) * angleUnit;
                    return def;
                };
                double a0 = param(arg(e, 1), 0), a1 = param(arg(e, 2), 2 * kPi);
                bool sense = !(arg(e, 3).k == SV::Enum && arg(e, 3).str == "F");
                if (!sense) std::swap(a0, a1);
                if (a1 <= a0) a1 += 2 * kPi;
                int segs = std::max(4, (int)std::ceil(24 * (a1 - a0) / (2 * kPi)));
                for (int k = 0; k <= segs; ++k) {
                    double a = a0 + (a1 - a0) * k / segs;
                    pts.push_back(pl.point(Vec3(std::cos(a) * r, std::sin(a) * r, 0)));
                }
                if (!sense) std::reverse(pts.begin(), pts.end());
            } else pts = curve2d(arg(e, 0), depth + 1);
        } else if (e->type == "IFCCIRCLE") {
            Mat4 pl = placement(arg(e, 0));
            double r = num(arg(e, 1)) * unit;
            for (int k = 0; k < 24; ++k) pts.push_back(pl.point(Vec3(std::cos(2 * kPi * k / 24) * r, std::sin(2 * kPi * k / 24) * r, 0)));
        }
        return pts;
    }
    static std::vector<Vec3> arcPoints(const Vec3& a, const Vec3& b, const Vec3& c) {
        // circle through three points (2-D)
        double d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
        if (std::fabs(d) < 1e-12) return {a, b, c};
        double ux = ((a.x * a.x + a.y * a.y) * (b.y - c.y) + (b.x * b.x + b.y * b.y) * (c.y - a.y) + (c.x * c.x + c.y * c.y) * (a.y - b.y)) / d;
        double uy = ((a.x * a.x + a.y * a.y) * (c.x - b.x) + (b.x * b.x + b.y * b.y) * (a.x - c.x) + (c.x * c.x + c.y * c.y) * (b.x - a.x)) / d;
        Vec3 o(ux, uy, 0);
        double r = (a - o).length();
        double t0 = std::atan2(a.y - uy, a.x - ux), t1 = std::atan2(b.y - uy, b.x - ux), t2 = std::atan2(c.y - uy, c.x - ux);
        auto norm = [](double t) { while (t < 0) t += 2 * kPi; while (t >= 2 * kPi) t -= 2 * kPi; return t; };
        double s1 = norm(t1 - t0), s2 = norm(t2 - t0);
        bool ccw = s1 < s2;
        double sweep = ccw ? s2 : -(2 * kPi - s2);
        int segs = std::max(3, (int)std::ceil(24 * std::fabs(sweep) / (2 * kPi)));
        std::vector<Vec3> out;
        for (int k = 0; k <= segs; ++k) {
            double t = t0 + sweep * k / segs;
            out.push_back(o + Vec3(std::cos(t), std::sin(t), 0) * r);
        }
        return out;
    }
    double angleUnit = 1.0;  // plane angle unit -> radians

    // A profile as an outer loop and holes, 2-D (z = 0)
    bool profile(const SV& ref, std::vector<Vec3>& outer, std::vector<std::vector<Vec3>>& holes) const {
        const Ent* e = ent(ref);
        if (!e) return false;
        const QString& t = e->type;
        auto rect = [](double w, double h) {
            return std::vector<Vec3>{{-w / 2, -h / 2, 0}, {w / 2, -h / 2, 0}, {w / 2, h / 2, 0}, {-w / 2, h / 2, 0}};
        };
        auto circle = [](double r, int n = 24) {
            std::vector<Vec3> p;
            for (int k = 0; k < n; ++k) p.push_back(Vec3(std::cos(2 * kPi * k / n) * r, std::sin(2 * kPi * k / n) * r, 0));
            return p;
        };
        auto place = [&](std::vector<Vec3>& pts, const SV& pos) {
            Mat4 pl = placement(pos);
            for (auto& p : pts) p = pl.point(p);
        };
        if (t == "IFCARBITRARYCLOSEDPROFILEDEF" || t == "IFCARBITRARYPROFILEDEFWITHVOIDS") {
            outer = curve2d(arg(e, 2));
            if (t == "IFCARBITRARYPROFILEDEFWITHVOIDS")
                for (auto& h : arg(e, 3).list) { auto hp = curve2d(h); if (hp.size() >= 3) holes.push_back(hp); }
        } else if (t == "IFCRECTANGLEPROFILEDEF" || t == "IFCROUNDEDRECTANGLEPROFILEDEF") {
            outer = rect(num(arg(e, 3)) * unit, num(arg(e, 4)) * unit);
            place(outer, arg(e, 2));
        } else if (t == "IFCRECTANGLEHOLLOWPROFILEDEF") {
            double w = num(arg(e, 3)) * unit, h = num(arg(e, 4)) * unit, wt = num(arg(e, 5)) * unit;
            outer = rect(w, h);
            holes.push_back(rect(w - 2 * wt, h - 2 * wt));
            place(outer, arg(e, 2));
            place(holes[0], arg(e, 2));
        } else if (t == "IFCCIRCLEPROFILEDEF" || t == "IFCCIRCLEHOLLOWPROFILEDEF") {
            double r = num(arg(e, 3)) * unit;
            outer = circle(r);
            place(outer, arg(e, 2));
            if (t == "IFCCIRCLEHOLLOWPROFILEDEF") {
                holes.push_back(circle(r - num(arg(e, 4)) * unit));
                place(holes[0], arg(e, 2));
            }
        } else if (t == "IFCELLIPSEPROFILEDEF") {
            double a = num(arg(e, 3)) * unit, b = num(arg(e, 4)) * unit;
            for (int k = 0; k < 24; ++k) outer.push_back(Vec3(std::cos(2 * kPi * k / 24) * a, std::sin(2 * kPi * k / 24) * b, 0));
            place(outer, arg(e, 2));
        } else if (t == "IFCISHAPEPROFILEDEF") {
            // overall width, depth, web thickness, flange thickness
            double w = num(arg(e, 3)) * unit, d = num(arg(e, 4)) * unit, tw = num(arg(e, 5)) * unit, tf = num(arg(e, 6)) * unit;
            outer = {{-w / 2, -d / 2, 0}, {w / 2, -d / 2, 0}, {w / 2, -d / 2 + tf, 0}, {tw / 2, -d / 2 + tf, 0}, {tw / 2, d / 2 - tf, 0},
                     {w / 2, d / 2 - tf, 0}, {w / 2, d / 2, 0}, {-w / 2, d / 2, 0}, {-w / 2, d / 2 - tf, 0}, {-tw / 2, d / 2 - tf, 0},
                     {-tw / 2, -d / 2 + tf, 0}, {-w / 2, -d / 2 + tf, 0}};
            place(outer, arg(e, 2));
        } else if (t == "IFCLSHAPEPROFILEDEF") {
            double d = num(arg(e, 3)) * unit, w = num(arg(e, 4), num(arg(e, 3))) * unit, th = num(arg(e, 5)) * unit;
            outer = {{-w / 2, -d / 2, 0}, {w / 2, -d / 2, 0}, {w / 2, -d / 2 + th, 0}, {-w / 2 + th, -d / 2 + th, 0}, {-w / 2 + th, d / 2, 0}, {-w / 2, d / 2, 0}};
            place(outer, arg(e, 2));
        } else if (t == "IFCDERIVEDPROFILEDEF") {
            if (!profile(arg(e, 2), outer, holes)) return false;
            Mat4 op = transformOperator(arg(e, 3));
            for (auto& p : outer) p = op.point(p);
            for (auto& h : holes) for (auto& p : h) p = op.point(p);
        } else return false;
        while (outer.size() > 1 && outer.front().near(outer.back())) outer.pop_back();
        for (auto& h : holes) while (h.size() > 1 && h.front().near(h.back())) h.pop_back();
        return outer.size() >= 3;
    }

    void extrude(Entities& E, const std::vector<Vec3>& outer, const std::vector<std::vector<Vec3>>& holes, const Mat4& xf,
                 const Vec3& dirIn, double depth, int mat) {
        Vec3 d = dirIn.normalized() * depth;
        auto P = [&](const Vec3& p) { return scaled(xf.point(p), opt); };
        // profile winding: counter-clockwise about +Z
        auto area = [](const std::vector<Vec3>& pts) { double a = 0; for (size_t k = 0; k < pts.size(); ++k) a += pts[k].x * pts[(k + 1) % pts.size()].y - pts[(k + 1) % pts.size()].x * pts[k].y; return a; };
        std::vector<Vec3> o = outer;
        if (area(o) < 0) std::reverse(o.begin(), o.end());
        bool flip = d.z < 0;   // extruding the other way: the base faces +Z
        std::vector<Vec3> bot, top;
        for (auto& p : o) { bot.push_back(P(p)); top.push_back(P(p + d)); }
        std::vector<std::vector<Vec3>> hb, ht;
        for (auto h : holes) {
            if (area(h) > 0) std::reverse(h.begin(), h.end());
            std::vector<Vec3> a, b;
            for (auto& p : h) { a.push_back(P(p)); b.push_back(P(p + d)); }
            hb.push_back(a);
            ht.push_back(b);
        }
        std::vector<Vec3> botRev(bot.rbegin(), bot.rend());
        addFaceWithHoles(E, flip ? bot : botRev, hb, mat, nullptr);
        std::vector<Vec3> topRev(top.rbegin(), top.rend());
        addFaceWithHoles(E, flip ? topRev : top, ht, mat, nullptr);
        auto sides = [&](const std::vector<Vec3>& a, const std::vector<Vec3>& b) {
            for (size_t k = 0; k < a.size(); ++k) {
                size_t k2 = (k + 1) % a.size();
                std::vector<Vec3> q = {a[k], a[k2], b[k2], b[k]};
                if (flip) std::reverse(q.begin(), q.end());
                addFaceWithHoles(E, q, {}, mat, nullptr);
            }
        };
        sides(bot, top);
        for (size_t h = 0; h < hb.size(); ++h) sides(hb[h], ht[h]);
    }

    // A representation item into E (in the product's coordinates, already including xf)
    void item(Entities& E, const SV& ref, const Mat4& xf, int mat, int depth = 0) {
        const Ent* e = ent(ref);
        if (!e || depth > 32) return;
        int own = styleOf(ref.ref);
        if (own >= 0) mat = own;
        const QString& t = e->type;
        if (t == "IFCEXTRUDEDAREASOLID" || t == "IFCEXTRUDEDAREASOLIDTAPERED") {
            std::vector<Vec3> outer;
            std::vector<std::vector<Vec3>> holes;
            if (!profile(arg(e, 0), outer, holes)) { ++unknown["profile " + (ent(arg(e, 0)) ? ent(arg(e, 0))->type : QString("?"))]; return; }
            Mat4 pos = xf * placement(arg(e, 1));
            extrude(E, outer, holes, pos, dir(arg(e, 2), {0, 0, 1}), num(arg(e, 3)) * unit, mat);
        } else if (t == "IFCFACETEDBREP" || t == "IFCFACETEDBREPWITHVOIDS") {
            shell(E, arg(e, 0), xf, mat);
        } else if (t == "IFCCLOSEDSHELL" || t == "IFCOPENSHELL" || t == "IFCCONNECTEDFACESET") {
            shell(E, ref, xf, mat);
        } else if (t == "IFCSHELLBASEDSURFACEMODEL" || t == "IFCFACEBASEDSURFACEMODEL") {
            for (auto& s : arg(e, 0).list) shell(E, s, xf, mat);
        } else if (t == "IFCTRIANGULATEDFACESET" || t == "IFCTRIANGULATEDIRREGULARNETWORK" || t == "IFCPOLYGONALFACESET") {
            const Ent* pl = ent(arg(e, 0));
            std::vector<Vec3> pts;
            if (pl)
                for (auto& c : arg(pl, 0).list)
                    pts.push_back(Vec3(c.list.size() > 0 ? num(c.list[0]) : 0, c.list.size() > 1 ? num(c.list[1]) : 0, c.list.size() > 2 ? num(c.list[2]) : 0) * unit);
            auto P = [&](int k) { return scaled(xf.point(pts[k]), opt); };
            std::set<std::vector<int>> seen;
            if (t == "IFCPOLYGONALFACESET") {
                // (Coordinates, Closed, Faces, PnIndex)
                for (auto& fr : arg(e, 2).list) {
                    const Ent* f = ent(fr);
                    if (!f) continue;
                    std::vector<Vec3> loop;
                    for (auto& ix : arg(f, 0).list) { int k = (int)num(ix) - 1; if (k >= 0 && k < (int)pts.size()) loop.push_back(P(k)); }
                    std::vector<std::vector<Vec3>> hs;
                    if (f->type == "IFCINDEXEDPOLYGONALFACEWITHVOIDS")
                        for (auto& hl : arg(f, 1).list) {
                            std::vector<Vec3> h;
                            for (auto& ix : hl.list) { int k = (int)num(ix) - 1; if (k >= 0 && k < (int)pts.size()) h.push_back(P(k)); }
                            hs.push_back(h);
                        }
                    addFaceWithHoles(E, loop, hs, mat, hs.empty() ? &seen : nullptr);
                }
            } else {
                // (Coordinates, Normals, Closed, CoordIndex, PnIndex)
                for (auto& tri : arg(e, 3).list) {
                    if (tri.list.size() < 3) continue;
                    int a = (int)num(tri.list[0]) - 1, b = (int)num(tri.list[1]) - 1, c = (int)num(tri.list[2]) - 1;
                    if (std::min({a, b, c}) < 0 || std::max({a, b, c}) >= (int)pts.size()) continue;
                    addFaceWithHoles(E, {P(a), P(b), P(c)}, {}, mat, &seen);
                }
            }
        } else if (t == "IFCMAPPEDITEM") {
            const Ent* map = ent(arg(e, 0));
            if (!map) return;
            Mat4 origin = placementAny(arg(map, 0));
            Mat4 target = transformOperator(arg(e, 1));
            const Ent* rep = ent(arg(map, 1));
            if (!rep) return;
            for (auto& it : arg(rep, 3).list) item(E, it, xf * target * origin, mat, depth + 1);
        } else if (t == "IFCBOOLEANCLIPPINGRESULT" || t == "IFCBOOLEANRESULT") {
            // the solid without what is cut away (clipped tops of walls come in at their full height)
            item(E, arg(e, 1), xf, mat, depth + 1);
            ++clipped;
        } else if (t == "IFCBLOCK") {
            Mat4 pos = xf * placement(arg(e, 0));
            double x = num(arg(e, 1)) * unit, y = num(arg(e, 2)) * unit, z = num(arg(e, 3)) * unit;
            extrude(E, {{0, 0, 0}, {x, 0, 0}, {x, y, 0}, {0, y, 0}}, {}, pos, {0, 0, 1}, z, mat);
        } else if (t == "IFCRIGHTCIRCULARCYLINDER") {
            Mat4 pos = xf * placement(arg(e, 0));
            double h = num(arg(e, 1)) * unit, r = num(arg(e, 2)) * unit;
            std::vector<Vec3> c;
            for (int k = 0; k < 24; ++k) c.push_back(Vec3(std::cos(2 * kPi * k / 24) * r, std::sin(2 * kPi * k / 24) * r, 0));
            extrude(E, c, {}, pos, {0, 0, 1}, h, mat);
        } else {
            ++unknown[t];
        }
    }
    Mat4 placementAny(const SV& ref) const {
        const Ent* e = ent(ref);
        if (!e) return Mat4();
        if (e->type.startsWith("IFCCARTESIANTRANSFORMATIONOPERATOR")) return transformOperator(ref);
        return placement(ref);
    }
    void shell(Entities& E, const SV& ref, const Mat4& xf, int mat) {
        const Ent* sh = ent(ref);
        if (!sh) return;
        std::set<std::vector<int>> seen;
        for (auto& fr : arg(sh, 0).list) {
            const Ent* f = ent(fr);
            if (!f) continue;
            std::vector<Vec3> outer;
            std::vector<std::vector<Vec3>> holes;
            for (auto& br : arg(f, 0).list) {
                const Ent* b = ent(br);
                if (!b) continue;
                const Ent* loop = ent(arg(b, 0));
                if (!loop || loop->type != "IFCPOLYLOOP") continue;
                std::vector<Vec3> pts;
                for (auto& p : arg(loop, 0).list) pts.push_back(scaled(xf.point(point(p)), opt));
                bool forward = !(arg(b, 1).k == SV::Enum && arg(b, 1).str == "F");
                if (!forward) std::reverse(pts.begin(), pts.end());
                if (b->type == "IFCFACEOUTERBOUND" && outer.empty()) outer = pts;
                else holes.push_back(pts);
            }
            if (outer.empty() && !holes.empty()) { outer = holes[0]; holes.erase(holes.begin()); }
            int mf = styleOf(fr.ref);
            addFaceWithHoles(E, outer, holes, mf >= 0 ? mf : mat, holes.empty() ? &seen : nullptr);
        }
    }

    // colours: IfcStyledItem(Item, Styles, Name) -> material
    std::unordered_map<int, int> styled;   // item id -> material
    int styleOf(int id) const {
        auto it = styled.find(id);
        return it == styled.end() ? -1 : it->second;
    }
    std::map<QString, int> matByKey;
    int surfaceStyleMaterial(const SV& ref, int depth = 0) {
        const Ent* e = ent(ref);
        if (!e || depth > 8) return -1;
        if (e->type == "IFCPRESENTATIONSTYLEASSIGNMENT") {
            for (auto& s : arg(e, 0).list) { int k = surfaceStyleMaterial(s, depth + 1); if (k >= 0) return k; }
            return -1;
        }
        if (e->type != "IFCSURFACESTYLE") return -1;
        QString name = arg(e, 0).str;
        for (auto& st : arg(e, 2).list) {
            const Ent* r = ent(st);
            if (!r || (r->type != "IFCSURFACESTYLERENDERING" && r->type != "IFCSURFACESTYLESHADING")) continue;
            const Ent* c = ent(arg(r, 0));
            if (!c) continue;
            QColor col = QColor::fromRgbF(std::clamp(num(arg(c, 1)), 0.0, 1.0), std::clamp(num(arg(c, 2)), 0.0, 1.0), std::clamp(num(arg(c, 3)), 0.0, 1.0));
            double tr = std::clamp(num(arg(r, 1), 0.0), 0.0, 1.0);
            if (name.isEmpty()) name = col.name();
            QString key = name + "|" + col.name() + "|" + QString::number(tr);
            auto it = matByKey.find(key);
            if (it != matByKey.end()) return it->second;
            int id = m.findMaterial(name);
            if (id < 0 || m.materials[id].color != col) {
                id = m.addMaterial(name, col);
                m.materials[id].alpha = 1.0 - tr;
            }
            matByKey[key] = id;
            return id;
        }
        return -1;
    }

    std::map<QString, int> unknown;
    int clipped = 0;
    std::unordered_map<int, Entities> openingShapes;   // IfcOpeningElement id -> its solid
    std::unordered_map<int, int> instOf;               // product id -> instance in the import
    int cutCount = 0;

    // IfcRelVoidsElement(GlobalId, OwnerHistory, Name, Description, RelatingBuildingElement,
    // RelatedOpeningElement): each opening is subtracted from its wall, as a solid operation.
    void cutOpenings(Model& model, Entities& E, const std::vector<std::pair<int, const Ent*>>& list) {
        for (auto& [id, e] : list) {
            if (e->type != "IFCRELVOIDSELEMENT") continue;
            const SV& wallRef = arg(e, 4);
            const SV& openRef = arg(e, 5);
            if (wallRef.k != SV::Ref || openRef.k != SV::Ref) continue;
            auto os = openingShapes.find(openRef.ref);
            auto wi = instOf.find(wallRef.ref);
            if (os == openingShapes.end() || wi == instOf.end()) continue;
            int wallIdx = wi->second;
            int wallDef = E.insts[wallIdx].def;
            int wallMat = -1;
            for (auto& f : model.defs[wallDef].ents.faces) if (f.alive && f.matFront >= 0) { wallMat = f.matFront; break; }
            int odef = model.addDef("Opening", true);
            model.defs[odef].ents = os->second;
            Instance oi;
            oi.def = odef;
            E.insts.push_back(oi);
            int openIdx = (int)E.insts.size() - 1;
            std::vector<int> res;
            QString why;
            bool ok = solidOperation(model, E, openIdx, wallIdx, SolidOp::Subtract, &why, &res);
            model.defs[odef].alive = false;
            model.defs[odef].ents.clear();
            E.insts[openIdx].alive = false;
            if (!ok || res.empty()) {
                ++skippedOpenings;
                continue;
            }
            // the cut wall keeps its name, its IFC class and its place in the model
            int nd = E.insts[res[0]].def;
            // the reveals of the hole are the wall's own surface
            if (wallMat >= 0)
                for (auto& f : model.defs[nd].ents.faces) if (f.alive && f.matFront < 0) f.matFront = wallMat;
            model.defs[nd].name = model.defs[wallDef].name;
            model.defs[nd].description = model.defs[wallDef].description;
            model.defs[wallDef].alive = false;
            model.defs[wallDef].ents.clear();
            wi->second = res[0];
            ++cutCount;
        }
        // openings that void nothing that was read
        for (auto& [oid, sh] : openingShapes) {
            bool used = false;
            for (auto& [id, e] : list)
                if (e->type == "IFCRELVOIDSELEMENT" && arg(e, 5).k == SV::Ref && arg(e, 5).ref == oid) { used = true; break; }
            if (!used) ++skippedOpenings;
        }
        // the import is read into its own group: drop the walls that were replaced and the cutters
        std::vector<Instance> keep;
        for (auto& in : E.insts) if (in.alive) keep.push_back(in);
        E.insts = std::move(keep);
    }

    bool read(Model& model, Entities& E, const QByteArray& data, QString* err) {
        StepParser p(data);
        if (!p.parse(err)) return false;
        P = &p;
        // units
        for (auto& [id, e] : p.ents) {
            if (e.type == "IFCSIUNIT") {
                QString kind = arg(&e, 1).str, prefix = arg(&e, 2).str, name = arg(&e, 3).str;
                if (kind == "LENGTHUNIT" && name == "METRE") {
                    static const std::map<QString, double> pre = {{"MILLI", 1}, {"CENTI", 10}, {"DECI", 100}, {"KILO", 1e6}};
                    auto it = pre.find(prefix);
                    unit = it == pre.end() ? 1000.0 : it->second;
                }
                if (kind == "PLANEANGLEUNIT" && name == "RADIAN") angleUnit = 1.0;
            } else if (e.type == "IFCCONVERSIONBASEDUNIT") {
                QString kind = arg(&e, 1).str;
                QString nm = arg(&e, 2).str.toUpper();
                const Ent* mw = ent(arg(&e, 3));
                double factor = mw ? num(arg(mw, 0), 1) : 1;
                if (kind == "LENGTHUNIT") unit = nm.contains("INCH") ? 25.4 : nm.contains("FOOT") ? 304.8 : factor * 1000;
                if (kind == "PLANEANGLEUNIT") angleUnit = nm.contains("DEGREE") ? kPi / 180 : factor;
            }
        }
        // styles
        for (auto& [id, e] : p.ents) {
            if (e.type != "IFCSTYLEDITEM" || arg(&e, 0).k != SV::Ref) continue;
            for (auto& s : arg(&e, 1).list) {
                int k = surfaceStyleMaterial(s);
                if (k >= 0) { styled[arg(&e, 0).ref] = k; break; }
            }
        }
        // the products, grouped by their IFC class on tags
        std::map<QString, int> tagOf;
        auto tagFor = [&](const QString& cls) {
            auto it = tagOf.find(cls);
            if (it != tagOf.end()) return it->second;
            int idx = -1;
            for (size_t k = 0; k < model.tags.size(); ++k) if (model.tags[k].name == cls) idx = (int)k;
            if (idx < 0) {
                Tag t;
                t.name = cls;
                t.visible = cls != "IfcSpace" && cls != "IfcOpeningElement";
                model.tags.push_back(t);
                idx = (int)model.tags.size() - 1;
            }
            tagOf[cls] = idx;
            return idx;
        };
        static const QSet<QString> skip = {"IFCOPENINGELEMENT", "IFCSITE", "IFCBUILDING", "IFCBUILDINGSTOREY", "IFCPROJECT", "IFCANNOTATION", "IFCGRID"};
        int products = 0;
        std::vector<std::pair<int, const Ent*>> list;
        for (auto& [id, e] : p.ents) list.push_back({id, &e});
        std::sort(list.begin(), list.end(), [](auto& a, auto& b) { return a.first < b.first; });
        for (auto& [id, e] : list) {
            // IfcProduct subtypes: (GlobalId, OwnerHistory, Name, Description, ObjectType, ObjectPlacement, Representation, ...)
            if (e->args.size() < 7 || arg(e, 6).k != SV::Ref || !e->type.startsWith("IFC")) continue;
            const Ent* pds = ent(arg(e, 6));
            if (!pds || pds->type != "IFCPRODUCTDEFINITIONSHAPE") continue;
            bool opening = e->type == "IFCOPENINGELEMENT";
            if (!opening && skip.contains(e->type) && e->type != "IFCSITE") continue;
            Mat4 place = objectPlacement(arg(e, 5));
            Entities sub;
            int mat = styleOf(id);
            for (auto& repRef : arg(pds, 2).list) {
                const Ent* rep = ent(repRef);
                if (!rep) continue;
                QString ident = arg(rep, 1).str;
                if (!ident.isEmpty() && ident != "Body" && ident != "Facetation" && ident != "Mesh" && ident != "Brep") continue;
                for (auto& it : arg(rep, 3).list) item(sub, it, place, mat);
            }
            if (sub.faces.empty()) { if (opening) ++skippedOpenings; continue; }
            cleanupImportedMesh(sub, opt);
            if (opening) {
                // kept aside: cut out of the wall it voids below (G3-01)
                openingShapes[id] = std::move(sub);
                continue;
            }
            QString cls = "Ifc" + e->type.mid(3, 1) + e->type.mid(4).toLower();
            // keep the usual capitals of IFC class names
            static const QHash<QString, QString> names = {
                {"IFCWALL", "IfcWall"}, {"IFCWALLSTANDARDCASE", "IfcWallStandardCase"}, {"IFCSLAB", "IfcSlab"}, {"IFCROOF", "IfcRoof"},
                {"IFCDOOR", "IfcDoor"}, {"IFCWINDOW", "IfcWindow"}, {"IFCCOLUMN", "IfcColumn"}, {"IFCBEAM", "IfcBeam"},
                {"IFCSTAIR", "IfcStair"}, {"IFCSTAIRFLIGHT", "IfcStairFlight"}, {"IFCRAILING", "IfcRailing"}, {"IFCSPACE", "IfcSpace"},
                {"IFCFURNISHINGELEMENT", "IfcFurnishingElement"}, {"IFCFURNITURE", "IfcFurniture"}, {"IFCMEMBER", "IfcMember"},
                {"IFCPLATE", "IfcPlate"}, {"IFCCOVERING", "IfcCovering"}, {"IFCBUILDINGELEMENTPROXY", "IfcBuildingElementProxy"},
                {"IFCFOOTING", "IfcFooting"}, {"IFCSITE", "IfcSite"}, {"IFCCURTAINWALL", "IfcCurtainWall"}, {"IFCRAMP", "IfcRamp"},
                {"IFCFLOWTERMINAL", "IfcFlowTerminal"}, {"IFCFLOWSEGMENT", "IfcFlowSegment"}};
            cls = names.value(e->type, cls);
            QString name = arg(e, 2).str;
            int def = model.addDef(name.isEmpty() ? cls : name, true);
            model.defs[def].description = cls;
            model.defs[def].ents = std::move(sub);
            Instance in;
            in.def = def;
            in.name = name;
            in.tag = tagFor(cls);
            E.insts.push_back(in);
            instOf[id] = (int)E.insts.size() - 1;
            ++products;
        }
        cutOpenings(model, E, list);
        if (!products) {
            QString why;
            for (auto& [t, c] : unknown) why += QString("\n  %1 (%2)").arg(t).arg(c);
            if (err) *err = "No building parts with shapes Folio Gable can read were found in the IFC file." + (why.isEmpty() ? QString() : "\nShapes not read:" + why);
            return false;
        }
        if (cutCount) notes << QString("%1 openings for doors and windows cut into the walls.").arg(cutCount);
        if (skippedOpenings) notes << QString("%1 openings could not be cut (a wall or the opening isn't a closed solid).").arg(skippedOpenings);
        if (clipped) notes << QString("%1 clipped shapes come in without the clipping.").arg(clipped);
        E.touch();
        return true;
    }
};

}  // namespace

bool import3DS(Model& m, Entities& E, const QString& path, const ImportOptions& opt, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = "Cannot open file."; return false; }
    return read3DS(m, E, f.readAll(), opt, err);
}

bool importIFC(Model& m, Entities& E, const QString& path, const ImportOptions& opt, QString* err, QStringList* notes) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = "Cannot open file."; return false; }
    QByteArray data = f.readAll();
    if (!data.startsWith("ISO-10303-21")) {
        if (data.startsWith("PK")) { if (err) *err = "This is a zipped IFC (.ifczip): unzip it first."; }
        else if (err) *err = "This isn't an IFC file (STEP format).";
        return false;
    }
    ImportOptions o = opt;
    o.scale = 1.0;    // the file says its units
    IfcReader r(m, o);
    bool ok = r.read(m, E, data, err);
    if (ok && notes) *notes = r.notes;
    return ok;
}

// Google Earth .kmz: a zip with a KML file whose <Model>s point at COLLADA files. Each model comes
// in as a group at its place, metres east and north of the first one.
bool importKMZ(Model& m, Entities& E, const QString& path, const ImportOptions& opt, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = "Cannot open file."; return false; }
    QTemporaryDir tmp;
    if (!tmp.isValid() || !unzipTo(f.readAll(), tmp.path(), err)) return false;
    QString kml;
    QDir d(tmp.path());
    if (QFile::exists(d.filePath("doc.kml"))) kml = d.filePath("doc.kml");
    else {
        std::function<void(const QDir&)> find = [&](const QDir& dir) {
            for (auto& fi : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (!kml.isEmpty()) return;
                if (fi.isDir()) find(QDir(fi.filePath()));
                else if (fi.suffix().toLower() == "kml") kml = fi.filePath();
            }
        };
        find(d);
    }
    struct M { QString href; double lon = 0, lat = 0, alt = 0, heading = 0, tilt = 0, roll = 0, sx = 1, sy = 1, sz = 1; QString name; };
    std::vector<M> models;
    if (!kml.isEmpty()) {
        QFile kf(kml);
        QDomDocument doc;
        if (kf.open(QIODevice::ReadOnly) && doc.setContent(&kf)) {
            auto list = doc.elementsByTagName("Model");
            for (int i = 0; i < list.size(); ++i) {
                QDomElement e = list.at(i).toElement();
                M mm;
                auto txt = [&](const QString& parent, const QString& tag) {
                    QDomElement p = parent.isEmpty() ? e : e.firstChildElement(parent);
                    return p.firstChildElement(tag).text().trimmed();
                };
                mm.href = txt("Link", "href");
                mm.lon = txt("Location", "longitude").toDouble();
                mm.lat = txt("Location", "latitude").toDouble();
                mm.alt = txt("Location", "altitude").toDouble();
                mm.heading = txt("Orientation", "heading").toDouble();
                mm.tilt = txt("Orientation", "tilt").toDouble();
                mm.roll = txt("Orientation", "roll").toDouble();
                QString sx = txt("Scale", "x"), sy = txt("Scale", "y"), sz = txt("Scale", "z");
                if (!sx.isEmpty()) mm.sx = sx.toDouble();
                if (!sy.isEmpty()) mm.sy = sy.toDouble();
                if (!sz.isEmpty()) mm.sz = sz.toDouble();
                QDomNode pm = e.parentNode();
                while (!pm.isNull() && pm.nodeName() != "Placemark") pm = pm.parentNode();
                if (!pm.isNull()) mm.name = pm.firstChildElement("name").text().trimmed();
                if (!mm.href.isEmpty()) models.push_back(mm);
            }
        }
    }
    if (models.empty()) {
        // no KML: every COLLADA file in the archive, at the origin
        std::function<void(const QDir&)> find = [&](const QDir& dir) {
            for (auto& fi : dir.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot)) {
                if (fi.isDir()) find(QDir(fi.filePath()));
                else if (fi.suffix().toLower() == "dae") { M mm; mm.href = d.relativeFilePath(fi.filePath()); models.push_back(mm); }
            }
        };
        find(d);
    }
    if (models.empty()) { if (err) *err = "The .kmz file has no 3D models in it (only placemarks or pictures)."; return false; }
    const double R = 6378137.0;   // metres
    double lat0 = models[0].lat * kPi / 180, lon0 = models[0].lon * kPi / 180, alt0 = models[0].alt;
    int done = 0;
    for (auto& mm : models) {
        QString dae = QDir(QFileInfo(kml.isEmpty() ? d.filePath("x") : kml).absolutePath()).filePath(mm.href);
        if (!QFile::exists(dae)) dae = d.filePath(mm.href);
        if (!QFile::exists(dae)) continue;
        QString e2;
        size_t before = E.insts.size();
        if (importDAEInto(m, E, dae, opt, &e2) < 0) continue;
        double east = (mm.lon * kPi / 180 - lon0) * std::cos(lat0) * R, north = (mm.lat * kPi / 180 - lat0) * R, up = mm.alt - alt0;
        // KML heading turns clockwise from north
        Mat4 xf = Mat4::translation(Vec3(east, north, up) * 1000.0) * Mat4::rotation({0, 0, 1}, -mm.heading * kPi / 180) *
                  Mat4::rotation({1, 0, 0}, mm.tilt * kPi / 180) * Mat4::rotation({0, 1, 0}, mm.roll * kPi / 180) *
                  Mat4::scaling({mm.sx, mm.sy, mm.sz});
        for (size_t k = before; k < E.insts.size(); ++k) {
            E.insts[k].xf = xf * E.insts[k].xf;
            if (!mm.name.isEmpty()) E.insts[k].name = mm.name;
        }
        ++done;
    }
    if (!done) { if (err) *err = "The .kmz file's 3D models couldn't be read."; return false; }
    E.touch();
    return true;
}

// ============================================================== glTF 2.0 (G3-03)
// .gltf (JSON, with its buffers inside as data: URIs or beside it) and .glb (one binary file).
// glTF is Y-up in metres: points come in Z-up in millimetres. Each node with a mesh becomes a
// group at the node's place; a mesh used by several nodes becomes one component.

namespace {

QImage imageFromBytes(const QByteArray& b) {
    QImage img;
    img.loadFromData(b);
    return img;
}

QByteArray pngOf(const QImage& img) {
    QByteArray png;
    QBuffer buf(&png);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return png;
}

}  // namespace

bool importGLTF(Model& m, Entities& E, const QString& path, const ImportOptions& opt, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = "Cannot open file."; return false; }
    QByteArray data = f.readAll();
    QByteArray json, bin;
    if (data.startsWith("glTF")) {
        if (data.size() < 20) { if (err) *err = "The .glb file is damaged."; return false; }
        const uchar* b = reinterpret_cast<const uchar*>(data.constData());
        qint64 p = 12;
        while (p + 8 <= data.size()) {
            quint32 len = qFromLittleEndian<quint32>(b + p), type = qFromLittleEndian<quint32>(b + p + 4);
            if (p + 8 + len > (quint64)data.size()) break;
            if (type == 0x4E4F534A) json = data.mid(p + 8, len);
            else if (type == 0x004E4942) bin = data.mid(p + 8, len);
            p += 8 + ((len + 3) & ~3u);
        }
    } else json = data;
    QJsonParseError pe;
    QJsonObject doc = QJsonDocument::fromJson(json, &pe).object();
    if (pe.error != QJsonParseError::NoError || !doc.contains("asset")) { if (err) *err = "This isn't a glTF file."; return false; }
    if (!doc["asset"].toObject()["version"].toString().startsWith("2")) { if (err) *err = "Only glTF 2.0 files can be imported."; return false; }
    for (auto x : doc["extensionsRequired"].toArray()) {
        QString ext = x.toString();
        if (ext == "KHR_draco_mesh_compression" || ext == "EXT_meshopt_compression") {
            if (err) *err = "This glTF file's meshes are compressed (" + ext + "), which isn't available.";
            return false;
        }
    }
    QDir dir = QFileInfo(path).dir();
    // buffers
    std::vector<QByteArray> buffers;
    for (auto bv : doc["buffers"].toArray()) {
        QString uri = bv.toObject()["uri"].toString();
        if (uri.isEmpty()) buffers.push_back(bin);
        else if (uri.startsWith("data:")) buffers.push_back(QByteArray::fromBase64(uri.section(',', 1).toLatin1()));
        else {
            QFile bf(dir.filePath(QUrl::fromPercentEncoding(uri.toUtf8())));
            buffers.push_back(bf.open(QIODevice::ReadOnly) ? bf.readAll() : QByteArray());
        }
    }
    QJsonArray views = doc["bufferViews"].toArray(), accs = doc["accessors"].toArray();
    auto viewBytes = [&](int vi, int* stride) -> QByteArray {
        if (vi < 0 || vi >= views.size()) return {};
        QJsonObject v = views[vi].toObject();
        int bi = v["buffer"].toInt(-1);
        if (bi < 0 || bi >= (int)buffers.size()) return {};
        if (stride) *stride = v["byteStride"].toInt(0);
        qint64 off = v["byteOffset"].toInteger(0), len = v["byteLength"].toInteger(0);
        if (off < 0 || len < 0 || off + len > buffers[bi].size()) return {};
        return buffers[bi].mid(off, len);
    };
    // an accessor as doubles, `comps` per element
    auto read = [&](int ai, int comps) -> std::vector<double> {
        std::vector<double> out;
        if (ai < 0 || ai >= accs.size()) return out;
        QJsonObject a = accs[ai].toObject();
        int count = a["count"].toInt(), ct = a["componentType"].toInt();
        bool norm = a["normalized"].toBool();
        int size = ct == 5126 || ct == 5125 ? 4 : ct == 5123 || ct == 5122 ? 2 : 1;
        int stride = 0;
        QByteArray v = viewBytes(a["bufferView"].toInt(-1), &stride);
        if (v.isEmpty() || count <= 0) return out;
        if (!stride) stride = size * comps;
        qint64 off = a["byteOffset"].toInteger(0);
        if (off + (qint64)(count - 1) * stride + size * comps > v.size()) return out;
        const uchar* b = reinterpret_cast<const uchar*>(v.constData()) + off;
        out.reserve((size_t)count * comps);
        for (int i = 0; i < count; ++i)
            for (int c = 0; c < comps; ++c) {
                const uchar* p = b + (qint64)i * stride + c * size;
                double x = 0;
                switch (ct) {
                case 5126: { quint32 u = qFromLittleEndian<quint32>(p); float fl; std::memcpy(&fl, &u, 4); x = fl; break; }
                case 5125: x = qFromLittleEndian<quint32>(p); break;
                case 5123: x = qFromLittleEndian<quint16>(p); if (norm) x /= 65535.0; break;
                case 5122: x = (qint16)qFromLittleEndian<quint16>(p); if (norm) x = std::max(x / 32767.0, -1.0); break;
                case 5121: x = *p; if (norm) x /= 255.0; break;
                case 5120: x = (qint8)*p; if (norm) x = std::max(x / 127.0, -1.0); break;
                }
                out.push_back(x);
            }
        return out;
    };
    // materials
    std::vector<int> mats;
    QJsonArray images = doc["images"].toArray(), textures = doc["textures"].toArray();
    auto imageBytes = [&](int ii) -> QByteArray {
        if (ii < 0 || ii >= images.size()) return {};
        QJsonObject im = images[ii].toObject();
        if (im.contains("bufferView")) return viewBytes(im["bufferView"].toInt(), nullptr);
        QString uri = im["uri"].toString();
        if (uri.startsWith("data:")) return QByteArray::fromBase64(uri.section(',', 1).toLatin1());
        QFile imf(dir.filePath(QUrl::fromPercentEncoding(uri.toUtf8())));
        return imf.open(QIODevice::ReadOnly) ? imf.readAll() : QByteArray();
    };
    int k = 0;
    for (auto mv : doc["materials"].toArray()) {
        QJsonObject mo = mv.toObject();
        QJsonObject pbr = mo["pbrMetallicRoughness"].toObject();
        QJsonArray cf = pbr["baseColorFactor"].toArray();
        double r = 1, g = 1, b = 1, a = 1;
        if (cf.size() >= 4) { r = cf[0].toDouble(); g = cf[1].toDouble(); b = cf[2].toDouble(); a = cf[3].toDouble(); }
        // the factors are linear light: back to the sRGB colours people pick
        auto srgb = [](double c) { c = std::clamp(c, 0.0, 1.0); return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1 / 2.4) - 0.055; };
        QColor col = QColor::fromRgbF(srgb(r), srgb(g), srgb(b));
        QString name = mo["name"].toString();
        if (name.isEmpty()) name = QString("Material %1").arg(++k);
        int id = m.addMaterial(name, col);
        if (mo["alphaMode"].toString() == "BLEND") m.materials[id].alpha = std::clamp(a, 0.0, 1.0);
        int ti = pbr["baseColorTexture"].toObject()["index"].toInt(-1);
        if (ti >= 0 && ti < textures.size()) {
            QImage img = imageFromBytes(imageBytes(textures[ti].toObject()["source"].toInt(-1)));
            if (!img.isNull()) {
                m.materials[id].texturePng = pngOf(img);
                m.materials[id].hasTexture = true;
                m.materials[id].texW = m.materials[id].texH = 1000;
            }
        }
        mats.push_back(id);
    }
    // meshes as definitions (built when first used)
    QJsonArray meshes = doc["meshes"].toArray(), nodes = doc["nodes"].toArray();
    std::vector<int> useCount(meshes.size(), 0);
    std::function<void(int, int)> countUses = [&](int ni, int depth) {
        if (ni < 0 || ni >= nodes.size() || depth > 64) return;
        QJsonObject n = nodes[ni].toObject();
        int mi = n["mesh"].toInt(-1);
        if (mi >= 0 && mi < meshes.size()) useCount[mi]++;
        for (auto c : n["children"].toArray()) countUses(c.toInt(), depth + 1);
    };
    QJsonArray scenes = doc["scenes"].toArray();
    int si = doc["scene"].toInt(0);
    QJsonArray rootNodes = si < scenes.size() ? scenes[si].toObject()["nodes"].toArray() : QJsonArray();
    if (rootNodes.isEmpty()) {
        // no scene: every node no other node holds
        std::set<int> held;
        for (auto nv : nodes) for (auto c : nv.toObject()["children"].toArray()) held.insert(c.toInt());
        for (int i = 0; i < nodes.size(); ++i) if (!held.count(i)) rootNodes.append(i);
    }
    for (auto r : rootNodes) countUses(r.toInt(), 0);
    // to Z-up millimetres: (x, y, z) -> (x, -z, y) * 1000
    Mat4 C = Mat4::scaling({1000, 1000, 1000}) * Mat4::rotation({1, 0, 0}, kPi / 2);
    Mat4 Ci = Mat4::rotation({1, 0, 0}, -kPi / 2) * Mat4::scaling({0.001, 0.001, 0.001});
    std::map<int, int> defOf;
    int faces = 0;
    auto meshDef = [&](int mi) -> int {
        auto it = defOf.find(mi);
        if (it != defOf.end()) return it->second;
        QJsonObject mo = meshes[mi].toObject();
        QString name = mo["name"].toString();
        if (name.isEmpty()) name = QString("Mesh %1").arg(mi + 1);
        int def = m.addDef(name, useCount[mi] <= 1);
        Entities ents;
        std::set<std::vector<int>> seen;
        for (auto pv : mo["primitives"].toArray()) {
            QJsonObject pr = pv.toObject();
            int mode = pr["mode"].toInt(4);
            if (mode < 4) continue;   // points and lines
            std::vector<double> pos = read(pr["attributes"].toObject()["POSITION"].toInt(-1), 3);
            size_t nv = pos.size() / 3;
            if (!nv) continue;
            std::vector<int> idx;
            if (pr.contains("indices")) for (double x : read(pr["indices"].toInt(), 1)) idx.push_back((int)x);
            else for (size_t i = 0; i < nv; ++i) idx.push_back((int)i);
            std::vector<std::array<int, 3>> tris;
            if (mode == 4) for (size_t i = 0; i + 2 < idx.size(); i += 3) tris.push_back({idx[i], idx[i + 1], idx[i + 2]});
            else if (mode == 5) for (size_t i = 0; i + 2 < idx.size(); ++i) tris.push_back(i % 2 ? std::array<int, 3>{idx[i + 1], idx[i], idx[i + 2]} : std::array<int, 3>{idx[i], idx[i + 1], idx[i + 2]});
            else if (mode == 6) for (size_t i = 1; i + 1 < idx.size(); ++i) tris.push_back({idx[0], idx[i], idx[i + 1]});
            int mat = pr["material"].toInt(-1);
            mat = mat >= 0 && mat < (int)mats.size() ? mats[mat] : -1;
            auto P = [&](int i) { return C.point(Vec3(pos[i * 3], pos[i * 3 + 1], pos[i * 3 + 2])); };
            for (auto& t : tris) {
                if (t[0] < 0 || t[1] < 0 || t[2] < 0 || t[0] >= (int)nv || t[1] >= (int)nv || t[2] >= (int)nv) continue;
                if (addFaceWithHoles(ents, {P(t[0]), P(t[1]), P(t[2])}, {}, mat, &seen) >= 0) ++faces;
            }
        }
        cleanupImportedMesh(ents, opt);
        m.defs[def].ents = std::move(ents);
        defOf[mi] = def;
        return def;
    };
    auto nodeXf = [&](const QJsonObject& n) {
        QJsonArray mx = n["matrix"].toArray();
        Mat4 L;
        if (mx.size() == 16) {
            for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r) L.at(r, c) = mx[c * 4 + r].toDouble();
            return L;
        }
        QJsonArray t = n["translation"].toArray(), q = n["rotation"].toArray(), s = n["scale"].toArray();
        Mat4 T, R, S;
        if (t.size() == 3) T = Mat4::translation({t[0].toDouble(), t[1].toDouble(), t[2].toDouble()});
        if (q.size() == 4) {
            double x = q[0].toDouble(), y = q[1].toDouble(), z = q[2].toDouble(), w = q[3].toDouble();
            R.at(0, 0) = 1 - 2 * (y * y + z * z); R.at(0, 1) = 2 * (x * y - z * w); R.at(0, 2) = 2 * (x * z + y * w);
            R.at(1, 0) = 2 * (x * y + z * w); R.at(1, 1) = 1 - 2 * (x * x + z * z); R.at(1, 2) = 2 * (y * z - x * w);
            R.at(2, 0) = 2 * (x * z - y * w); R.at(2, 1) = 2 * (y * z + x * w); R.at(2, 2) = 1 - 2 * (x * x + y * y);
        }
        if (s.size() == 3) S = Mat4::scaling({s[0].toDouble(), s[1].toDouble(), s[2].toDouble()});
        return T * R * S;
    };
    // nodes: a node with a mesh is a group (or a component's instance) at its place; children nest
    std::function<void(Entities&, int, int)> place = [&](Entities& into, int ni, int depth) {
        if (ni < 0 || ni >= nodes.size() || depth > 64) return;
        QJsonObject n = nodes[ni].toObject();
        Mat4 xf = C * nodeXf(n) * Ci;
        int mi = n["mesh"].toInt(-1);
        QJsonArray kids = n["children"].toArray();
        QString name = n["name"].toString();
        if (kids.isEmpty()) {
            if (mi < 0 || mi >= meshes.size()) return;
            Instance in;
            in.def = meshDef(mi);
            in.xf = xf;
            in.name = name;
            into.insts.push_back(in);
            return;
        }
        int g = m.addDef(name.isEmpty() ? QString("Group") : name, true);
        Entities sub;
        if (mi >= 0 && mi < meshes.size()) {
            Instance in;
            in.def = meshDef(mi);
            sub.insts.push_back(in);
        }
        for (auto c : kids) place(sub, c.toInt(), depth + 1);
        if (sub.insts.empty()) { m.defs[g].alive = false; return; }
        m.defs[g].ents = std::move(sub);
        Instance in;
        in.def = g;
        in.xf = xf;
        in.name = name;
        into.insts.push_back(in);
    };
    for (auto r : rootNodes) place(E, r.toInt(), 0);
    if (!faces || E.insts.empty()) { if (err) *err = "The glTF file has no meshes with faces in it."; return false; }
    E.touch();
    return true;
}

// ============================================================== 3MF (G3-03)
// The 3D Manufacturing Format: a zip with an XML model; objects are meshes or assemblies of
// other objects, the build lists what to print. Colours come from base materials and colour groups.

bool import3MF(Model& m, Entities& E, const QString& path, const ImportOptions& opt, QString* err) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) { if (err) *err = "Cannot open file."; return false; }
    QByteArray z = f.readAll();
    if (!z.startsWith("PK")) { if (err) *err = "This isn't a 3MF file."; return false; }
    QTemporaryDir tmp;
    QString e2;
    if (!tmp.isValid() || !unzipTo(z, tmp.path(), &e2)) { if (err) *err = "The 3MF file is damaged."; return false; }
    QDir d(tmp.path());
    // the start part from the package relationships
    QString start = "3D/3dmodel.model";
    {
        QFile rf(d.filePath("_rels/.rels"));
        QDomDocument rd;
        if (rf.open(QIODevice::ReadOnly) && rd.setContent(&rf)) {
            auto rl = rd.elementsByTagName("Relationship");
            for (int i = 0; i < rl.size(); ++i) {
                QDomElement r = rl.at(i).toElement();
                if (r.attribute("Type").endsWith("/3dmodel")) { start = r.attribute("Target"); break; }
            }
        }
        while (start.startsWith('/')) start.remove(0, 1);
    }
    struct Obj { QDomElement e; QString file; int def = -1; int uses = 0; };
    std::map<std::pair<QString, QString>, Obj> objs;
    std::map<std::pair<QString, QString>, std::vector<std::pair<QString, QColor>>> props;   // basematerials / colorgroup
    std::map<QString, QDomDocument> docs;
    std::map<QString, double> unitOf;
    std::function<bool(const QString&)> load = [&](const QString& file) {
        if (docs.count(file)) return true;
        QFile mf(d.filePath(file));
        QDomDocument md;
        if (!mf.open(QIODevice::ReadOnly) || !md.setContent(&mf, true)) return false;
        docs[file] = md;
        QDomElement root = md.documentElement();
        static const std::map<QString, double> u = {{"micron", 0.001}, {"millimeter", 1}, {"centimeter", 10}, {"inch", 25.4}, {"foot", 304.8}, {"meter", 1000}};
        auto it = u.find(root.attribute("unit", "millimeter"));
        unitOf[file] = it == u.end() ? 1.0 : it->second;
        QDomElement res = root.firstChildElement("resources");
        for (QDomElement c = res.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
            QString id = c.attribute("id");
            if (c.localName() == "object") objs[{file, id}] = Obj{c, file};
            else if (c.localName() == "basematerials") {
                auto& l = props[{file, id}];
                for (QDomElement b = c.firstChildElement(); !b.isNull(); b = b.nextSiblingElement())
                    if (b.localName() == "base") l.push_back({b.attribute("name"), QColor(b.attribute("displaycolor").left(7))});
            } else if (c.localName() == "colorgroup") {
                auto& l = props[{file, id}];
                for (QDomElement b = c.firstChildElement(); !b.isNull(); b = b.nextSiblingElement())
                    if (b.localName() == "color") {
                        QString cs = b.attribute("color");
                        QColor col(cs.left(7));
                        if (cs.size() == 9) col.setAlpha(cs.mid(7, 2).toInt(nullptr, 16));
                        l.push_back({QString(), col});
                    }
            }
        }
        return true;
    };
    if (!load(start)) { if (err) *err = "The 3MF file has no 3D model in it."; return false; }
    std::map<QString, int> matByKey;
    auto material = [&](const QString& file, const QString& pid, int index) -> int {
        auto it = props.find({file, pid});
        if (it == props.end() || index < 0 || index >= (int)it->second.size()) return -1;
        auto& [name, col] = it->second[index];
        QString key = name + "|" + col.name(QColor::HexArgb);
        auto mk = matByKey.find(key);
        if (mk != matByKey.end()) return mk->second;
        int id = m.addMaterial(name.isEmpty() ? "Color " + col.name().toUpper() : name, QColor(col.red(), col.green(), col.blue()));
        m.materials[id].alpha = col.alphaF();
        matByKey[key] = id;
        return id;
    };
    auto xfOf = [](const QString& t) {
        // "m00 m01 m02 m10 m11 m12 m20 m21 m22 m30 m31 m32": row vectors, x' = x m0 + y m1 + z m2 + m3
        Mat4 r;
        QStringList p = t.split(' ', Qt::SkipEmptyParts);
        if (p.size() != 12) return r;
        for (int row = 0; row < 4; ++row)
            for (int col = 0; col < 3; ++col) r.at(col, row) = p[row * 3 + col].toDouble();
        return r;
    };
    const QString prodNs = "http://schemas.microsoft.com/3dmanufacturing/production/2015/06";
    int faces = 0;
    std::function<int(const QString&, const QString&, int)> defFor = [&](const QString& file, const QString& id, int depth) -> int {
        auto it = objs.find({file, id});
        if (it == objs.end() || depth > 32) return -1;
        Obj& o = it->second;
        if (o.def >= 0) { m.defs[o.def].isGroup = false; return o.def; }   // used twice: a component
        double unit = unitOf[file];
        QString name = o.e.attribute("name");
        if (name.isEmpty()) name = "Object " + id;
        int def = m.addDef(name, true);
        o.def = def;
        Entities ents;
        QDomElement mesh = o.e.firstChildElement("mesh");
        if (!mesh.isNull()) {
            std::vector<Vec3> vs;
            QDomElement vv = mesh.firstChildElement("vertices");
            for (QDomElement v = vv.firstChildElement("vertex"); !v.isNull(); v = v.nextSiblingElement("vertex"))
                vs.push_back(Vec3(v.attribute("x").toDouble(), v.attribute("y").toDouble(), v.attribute("z").toDouble()) * unit);
            QString opid = o.e.attribute("pid");
            int opi = o.e.attribute("pindex", "0").toInt();
            std::set<std::vector<int>> seen;
            QDomElement tt = mesh.firstChildElement("triangles");
            for (QDomElement t = tt.firstChildElement("triangle"); !t.isNull(); t = t.nextSiblingElement("triangle")) {
                int a = t.attribute("v1").toInt(), b = t.attribute("v2").toInt(), c = t.attribute("v3").toInt();
                if (a < 0 || b < 0 || c < 0 || a >= (int)vs.size() || b >= (int)vs.size() || c >= (int)vs.size()) continue;
                int mat = t.hasAttribute("pid") || t.hasAttribute("p1")
                              ? material(file, t.attribute("pid", opid), t.attribute("p1", QString::number(opi)).toInt())
                              : material(file, opid, opi);
                if (addFaceWithHoles(ents, {vs[a], vs[b], vs[c]}, {}, mat, &seen) >= 0) ++faces;
            }
            cleanupImportedMesh(ents, opt);
        }
        QDomElement comps = o.e.firstChildElement("components");
        for (QDomElement c = comps.firstChildElement("component"); !c.isNull(); c = c.nextSiblingElement("component")) {
            QString cf = c.attributeNS(prodNs, "path");
            while (cf.startsWith('/')) cf.remove(0, 1);
            if (cf.isEmpty()) cf = file;
            else if (!load(cf)) continue;
            int cd = defFor(cf, c.attribute("objectid"), depth + 1);
            if (cd < 0) continue;
            Mat4 x = xfOf(c.attribute("transform"));
            for (int r = 0; r < 3; ++r) x.at(r, 3) *= unit;
            Instance in;
            in.def = cd;
            in.xf = x;
            ents.insts.push_back(in);
        }
        m.defs[def].ents = std::move(ents);
        return def;
    };
    QDomElement build = docs[start].documentElement().firstChildElement("build");
    double unit = unitOf[start];
    for (QDomElement it = build.firstChildElement("item"); !it.isNull(); it = it.nextSiblingElement("item")) {
        QString file = it.attributeNS(prodNs, "path");
        while (file.startsWith('/')) file.remove(0, 1);
        if (file.isEmpty()) file = start;
        else if (!load(file)) continue;
        int def = defFor(file, it.attribute("objectid"), 0);
        if (def < 0) continue;
        Mat4 x = xfOf(it.attribute("transform"));
        for (int r = 0; r < 3; ++r) x.at(r, 3) *= unit;
        Instance in;
        in.def = def;
        in.xf = x;
        in.name = m.defs[def].name;
        E.insts.push_back(in);
    }
    if (!faces || E.insts.empty()) { if (err) *err = "The 3MF file has no objects to print in it."; return false; }
    E.touch();
    return true;
}

}  // namespace gable
