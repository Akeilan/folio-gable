#include "core/Serializer.h"
#include "core/Geometry.h"
#include <QDataStream>
#include <QIODevice>
#include <functional>

namespace gable {

static const quint32 kMagic = 0x4742414C;  // "GBAL"
static const quint32 kVersion = 7;   // 4: texture mappings carry the "projected" flag; 5: edge style settings; 6: matched photos; 7: dynamic components
static quint32 gReadVersion = kVersion;

static QDataStream& operator<<(QDataStream& s, const Vec3& v) { return s << v.x << v.y << v.z; }
static QDataStream& operator>>(QDataStream& s, Vec3& v) { return s >> v.x >> v.y >> v.z; }
static QDataStream& operator<<(QDataStream& s, const Plane& p) { return s << p.n << p.d; }
static QDataStream& operator>>(QDataStream& s, Plane& p) { return s >> p.n >> p.d; }
static QDataStream& operator<<(QDataStream& s, const Mat4& m) { for (double d : m.m) s << d; return s; }
static QDataStream& operator>>(QDataStream& s, Mat4& m) { for (double& d : m.m) s >> d; return s; }
static void writeAttrs(QDataStream& s, const std::vector<DynAttr>& a) {
    s << (quint32)a.size();
    for (auto& x : a) {
        s << x.name << x.formula << x.value << x.label << x.units << (qint32)x.access << (quint32)x.options.size();
        for (auto& o : x.options) s << o.first << o.second;
    }
}
static void readAttrs(QDataStream& s, std::vector<DynAttr>& a) {
    quint32 n;
    s >> n;
    if (n > 100000) { s.setStatus(QDataStream::ReadCorruptData); return; }
    a.resize(n);
    for (auto& x : a) {
        qint32 acc;
        quint32 no;
        s >> x.name >> x.formula >> x.value >> x.label >> x.units >> acc >> no;
        x.access = acc;
        if (no > 100000) { s.setStatus(QDataStream::ReadCorruptData); return; }
        x.options.resize(no);
        for (auto& o : x.options) s >> o.first >> o.second;
    }
}
static QDataStream& operator<<(QDataStream& s, const TexMap& t) { return s << t.custom << t.uAxis << t.vAxis << t.uOff << t.vOff << t.projected; }
static QDataStream& operator>>(QDataStream& s, TexMap& t) {
    s >> t.custom >> t.uAxis >> t.vAxis >> t.uOff >> t.vOff;
    t.projected = false;
    if (gReadVersion >= 4) s >> t.projected;
    return s;
}

static void writeIntVec(QDataStream& s, const std::vector<int>& v) {
    s << (quint32)v.size();
    for (int x : v) s << (qint32)x;
}
static void readIntVec(QDataStream& s, std::vector<int>& v) {
    quint32 n; s >> n;
    v.resize(n);
    for (auto& x : v) { qint32 y; s >> y; x = y; }
}

void compactEntities(Entities& E) {
    std::vector<int> vm(E.verts.size(), -1), em(E.edges.size(), -1), fm(E.faces.size(), -1), cm(E.curves.size(), -1);
    Entities N;
    for (size_t i = 0; i < E.verts.size(); ++i)
        if (E.verts[i].alive) { vm[i] = (int)N.verts.size(); Vertex v; v.p = E.verts[i].p; N.verts.push_back(v); }
    std::vector<char> curveUsed(E.curves.size(), 0);
    for (auto& e : E.edges)
        if (e.alive && e.curve >= 0 && e.curve < (int)E.curves.size()) curveUsed[e.curve] = 1;
    for (size_t i = 0; i < E.curves.size(); ++i)
        if (curveUsed[i]) { cm[i] = (int)N.curves.size(); N.curves.push_back(E.curves[i]); }
    for (size_t i = 0; i < E.edges.size(); ++i) {
        const Edge& e = E.edges[i];
        if (!e.alive || vm[e.v[0]] < 0 || vm[e.v[1]] < 0) continue;
        em[i] = (int)N.edges.size();
        Edge ne = e;
        ne.v[0] = vm[e.v[0]]; ne.v[1] = vm[e.v[1]];
        ne.faces.clear();
        ne.curve = e.curve >= 0 && e.curve < (int)cm.size() ? cm[e.curve] : -1;
        N.edges.push_back(ne);
        N.verts[ne.v[0]].edges.push_back(em[i]);
        N.verts[ne.v[1]].edges.push_back(em[i]);
    }
    for (size_t i = 0; i < E.faces.size(); ++i) {
        const Face& f = E.faces[i];
        if (!f.alive) continue;
        Face nf = f;
        bool ok = true;
        for (int& v : nf.outer) { v = vm[v]; if (v < 0) ok = false; }
        for (auto& h : nf.holes) for (int& v : h) { v = vm[v]; if (v < 0) ok = false; }
        if (!ok) continue;
        fm[i] = (int)N.faces.size();
        N.faces.push_back(nf);
        N.linkFace(fm[i]);
    }
    std::vector<int> im(E.insts.size(), -1);
    for (size_t i = 0; i < E.insts.size(); ++i)
        if (E.insts[i].alive) { im[i] = (int)N.insts.size(); N.insts.push_back(E.insts[i]); }
    for (auto& x : N.insts)
        if (x.copyOf >= 0) x.copyOf = x.copyOf < (int)im.size() ? im[x.copyOf] : -1;
    for (auto& x : E.guides) if (x.alive) N.guides.push_back(x);
    for (auto& x : E.dims) if (x.alive) N.dims.push_back(x);
    for (auto& x : E.texts) if (x.alive) N.texts.push_back(x);
    for (auto& x : E.sections) if (x.alive) N.sections.push_back(x);
    N.invalidateIndex();
    N.touch();
    E = std::move(N);
}

static void writeEntities(QDataStream& s, const Entities& in) {
    Entities E = in;
    compactEntities(E);
    s << (quint32)E.verts.size();
    for (auto& v : E.verts) s << v.p;
    s << (quint32)E.curves.size();
    for (auto& c : E.curves)
        s << c.isArc << c.closed << c.polygon << c.center << c.normal << c.xaxis << c.radius << c.startAng << c.endAng << (qint32)c.segments;
    s << (quint32)E.edges.size();
    for (auto& e : E.edges)
        s << (qint32)e.v[0] << (qint32)e.v[1] << e.soft << e.smooth << e.hidden << (qint32)e.tag << (qint32)e.material << (qint32)e.curve;
    s << (quint32)E.faces.size();
    for (auto& f : E.faces) {
        writeIntVec(s, f.outer);
        s << (quint32)f.holes.size();
        for (auto& h : f.holes) writeIntVec(s, h);
        s << f.plane << (qint32)f.matFront << (qint32)f.matBack << f.texFront << f.texBack << f.hidden << (qint32)f.tag;
    }
    s << (quint32)E.insts.size();
    for (auto& i : E.insts)
    {
        s << (qint32)i.def << i.xf << i.name << (qint32)i.tag << (qint32)i.material << i.hidden << i.locked;
        writeAttrs(s, i.attrs);
        s << (qint32)i.copyOf << (qint32)i.copyIndex;
    }
    s << (quint32)E.guides.size();
    for (auto& g : E.guides) s << g.isPoint << g.p << g.dir << g.hidden << (qint32)g.tag;
    s << (quint32)E.dims.size();
    for (auto& d : E.dims) s << d.radial << d.diameter << d.a << d.b << d.offset << d.textOverride << d.hidden << (qint32)d.tag;
    s << (quint32)E.texts.size();
    for (auto& t : E.texts) s << t.anchor << t.leader << t.screenText << t.sx << t.sy << t.text << t.hidden << (qint32)t.tag;
    s << (quint32)E.sections.size();
    for (auto& sp : E.sections) s << sp.plane << sp.name << sp.active << sp.hidden << (qint32)sp.tag;
}

static bool readEntities(QDataStream& s, Entities& E, quint32 version) {
    E.clear();
    quint32 n;
    s >> n;
    if (n > 100000000) return false;
    E.verts.resize(n);
    for (auto& v : E.verts) s >> v.p;
    s >> n;
    E.curves.resize(n);
    for (auto& c : E.curves) {
        qint32 seg;
        s >> c.isArc >> c.closed >> c.polygon >> c.center >> c.normal >> c.xaxis >> c.radius >> c.startAng >> c.endAng >> seg;
        c.segments = seg;
    }
    s >> n;
    E.edges.resize(n);
    for (size_t i = 0; i < n; ++i) {
        Edge& e = E.edges[i];
        qint32 a, b, tag, mat, cur;
        s >> a >> b >> e.soft >> e.smooth >> e.hidden >> tag >> mat >> cur;
        e.v[0] = a; e.v[1] = b; e.tag = tag; e.material = mat; e.curve = cur;
        if (a < 0 || b < 0 || a >= (int)E.verts.size() || b >= (int)E.verts.size()) return false;
        E.verts[a].edges.push_back((int)i);
        E.verts[b].edges.push_back((int)i);
    }
    s >> n;
    E.faces.resize(n);
    for (size_t i = 0; i < n; ++i) {
        Face& f = E.faces[i];
        readIntVec(s, f.outer);
        quint32 nh; s >> nh;
        f.holes.resize(nh);
        for (auto& h : f.holes) readIntVec(s, h);
        qint32 mf, mb, tag;
        s >> f.plane >> mf >> mb >> f.texFront >> f.texBack >> f.hidden >> tag;
        f.matFront = mf; f.matBack = mb; f.tag = tag;
        for (int v : f.outer) if (v < 0 || v >= (int)E.verts.size()) return false;
        for (auto& h : f.holes) for (int v : h) if (v < 0 || v >= (int)E.verts.size()) return false;
        E.linkFace((int)i);
    }
    s >> n;
    E.insts.resize(n);
    for (auto& in : E.insts) {
        qint32 def, tag, mat;
        s >> def >> in.xf >> in.name >> tag >> mat >> in.hidden >> in.locked;
        in.def = def; in.tag = tag; in.material = mat;
        if (version >= 7) {
            readAttrs(s, in.attrs);
            qint32 co, ci;
            s >> co >> ci;
            in.copyOf = co; in.copyIndex = ci;
        }
    }
    s >> n;
    E.guides.resize(n);
    for (auto& g : E.guides) { qint32 tag; s >> g.isPoint >> g.p >> g.dir >> g.hidden >> tag; g.tag = tag; }
    s >> n;
    E.dims.resize(n);
    for (auto& d : E.dims) { qint32 tag; s >> d.radial >> d.diameter >> d.a >> d.b >> d.offset >> d.textOverride >> d.hidden >> tag; d.tag = tag; }
    s >> n;
    E.texts.resize(n);
    for (auto& t : E.texts) { qint32 tag; s >> t.anchor >> t.leader >> t.screenText >> t.sx >> t.sy >> t.text >> t.hidden >> tag; t.tag = tag; }
    s >> n;
    E.sections.resize(n);
    for (auto& sp : E.sections) { qint32 tag; s >> sp.plane >> sp.name >> sp.active >> sp.hidden >> tag; sp.tag = tag; }
    (void)version;
    E.invalidateIndex();
    E.touch();
    return s.status() == QDataStream::Ok;
}

static void writeStyle(QDataStream& s, const Style& st) {
    s << (qint32)st.faceStyle << st.xray << st.backEdges << st.edges << st.profiles << (qint32)st.profileWidth
      << st.depthCue << st.extension << st.endpoints << st.jitter << st.colorByAxis << st.colorByTag << st.edgeColor
      << st.frontColor << st.backColor << st.background << st.sky << st.ground << st.showSky << st.showGround
      << st.groundTransparency << st.showGuides << st.showHidden << st.showAxes << st.showSectionPlanes
      << st.showSectionCuts << st.sectionFill << st.sectionFillColor << st.fog << st.fogStart << st.fogEnd << st.fogColor
      << st.name;
    s << st.halo << (qint32)st.extensionLength << (qint32)st.endpointLength << (qint32)st.jitterAmount << (qint32)st.depthCueWidth
      << (qint32)st.haloGap;
}
static void readStyle(QDataStream& s, Style& st) {
    qint32 fs, pw;
    s >> fs >> st.xray >> st.backEdges >> st.edges >> st.profiles >> pw >> st.depthCue >> st.extension >> st.endpoints
      >> st.jitter >> st.colorByAxis >> st.colorByTag >> st.edgeColor >> st.frontColor >> st.backColor >> st.background
      >> st.sky >> st.ground >> st.showSky >> st.showGround >> st.groundTransparency >> st.showGuides >> st.showHidden
      >> st.showAxes >> st.showSectionPlanes >> st.showSectionCuts >> st.sectionFill >> st.sectionFillColor >> st.fog
      >> st.fogStart >> st.fogEnd >> st.fogColor >> st.name;
    st.faceStyle = (FaceStyle)fs;
    st.profileWidth = pw;
    if (gReadVersion >= 5) {
        qint32 el, en, ja, dc, hg;
        s >> st.halo >> el >> en >> ja >> dc >> hg;
        st.extensionLength = std::clamp((int)el, 1, 40);
        st.endpointLength = std::clamp((int)en, 1, 40);
        st.jitterAmount = std::clamp((int)ja, 1, 10);
        st.depthCueWidth = std::clamp((int)dc, 1, 12);
        st.haloGap = std::clamp((int)hg, 1, 20);
    }
}
static void writeShadow(QDataStream& s, const ShadowSettings& sh) {
    s << sh.enabled << (qint32)sh.month << (qint32)sh.day << sh.hour << sh.utcOffset << sh.latitude << sh.longitude
      << sh.northAngle << sh.light << sh.dark << sh.sunForShading << sh.onFaces << sh.onGround << sh.fromEdges;
}
static void readShadow(QDataStream& s, ShadowSettings& sh) {
    qint32 mo, da;
    s >> sh.enabled >> mo >> da >> sh.hour >> sh.utcOffset >> sh.latitude >> sh.longitude >> sh.northAngle >> sh.light
      >> sh.dark >> sh.sunForShading >> sh.onFaces >> sh.onGround >> sh.fromEdges;
    sh.month = mo; sh.day = da;
}
static void writeCam(QDataStream& s, const CameraState& c) {
    s << c.eye << c.target << c.up << c.perspective << c.fov << c.orthoHeight << c.twoPoint;
}
static void readCam(QDataStream& s, CameraState& c) {
    s >> c.eye >> c.target >> c.up >> c.perspective >> c.fov >> c.orthoHeight >> c.twoPoint;
}

QByteArray saveModel(const Model& m, bool compress) {
    QByteArray raw;
    {
        QDataStream s(&raw, QIODevice::WriteOnly);
        s.setVersion(QDataStream::Qt_6_0);
        s << kMagic << kVersion;
        s << m.author << m.description;
        s << (qint32)m.units.unit << (qint32)m.units.precision << m.units.showUnits << m.units.lengthSnap
          << m.units.snapLength << (qint32)m.units.anglePrecision << m.units.angleSnap << m.units.angleSnapOn;
        s << m.axesOrigin << m.axesX << m.axesY << m.axesZ;
        s << (quint32)m.materials.size();
        for (auto& mt : m.materials)
            s << mt.name << mt.color << mt.alpha << mt.hasTexture << mt.texturePng << mt.texW << mt.texH << mt.colorize;
        s << (quint32)m.tags.size();
        for (auto& t : m.tags) s << t.name << t.visible << t.color << (qint32)t.dash;
        // Definitions: write only alive ones, remapping indices.
        std::vector<int> dmap(m.defs.size(), -1);
        int nd = 0;
        for (size_t i = 0; i < m.defs.size(); ++i)
            if (m.defs[i].alive) dmap[i] = nd++;
        auto remapInsts = [&](Entities e) {
            for (auto& in : e.insts)
                if (in.alive) {
                    if (in.def < 0 || in.def >= (int)dmap.size() || dmap[in.def] < 0) in.alive = false;
                    else in.def = dmap[in.def];
                }
            return e;
        };
        s << (quint32)nd;
        for (size_t i = 0; i < m.defs.size(); ++i) {
            const ComponentDef& d = m.defs[i];
            if (!d.alive) continue;
            s << d.name << d.description << d.isGroup << d.alwaysFaceCamera << d.glue << d.cutsOpening;
            writeAttrs(s, d.attrs);
            writeEntities(s, remapInsts(d.ents));
        }
        writeEntities(s, remapInsts(m.root));
        writeStyle(s, m.style);
        writeShadow(s, m.shadow);
        writeCam(s, m.camera);
        s << (quint32)m.scenes.size();
        for (auto& sc : m.scenes) {
            s << sc.name << sc.description;
            writeCam(s, sc.cam);
            s << (quint32)sc.tagVisible.size();
            for (bool b : sc.tagVisible) s << b;
            s << sc.hasStyle;
            writeStyle(s, sc.style);
            writeShadow(s, sc.shadow);
            s << sc.matchPhoto;
            if (sc.matchPhoto) {
                const MatchPhoto& p = sc.photo;
                s << p.image << p.origin.x << p.origin.y << p.eyeHeight << p.turned;
                for (auto& l : p.lines)
                    for (auto& e : l) s << e.x << e.y;
            }
        }
    }
    if (!compress) return raw;
    QByteArray out("GABLEZ1\n");
    out += qCompress(raw, 6);
    return out;
}

bool loadModel(Model& m, const QByteArray& dataIn, QString* error) {
    QByteArray data = dataIn;
    if (data.startsWith("GABLEZ1\n")) data = qUncompress(data.mid(8));
    QDataStream s(data);
    s.setVersion(QDataStream::Qt_6_0);
    quint32 magic, version;
    s >> magic >> version;
    gReadVersion = version;
    if (magic != kMagic || version > kVersion) {
        if (error) *error = "Not a Folio Gable model file (or written by a newer version).";
        return false;
    }
    Model nm;
    nm.tags.clear();
    s >> nm.author >> nm.description;
    qint32 unit, prec, aprec;
    s >> unit >> prec >> nm.units.showUnits >> nm.units.lengthSnap >> nm.units.snapLength >> aprec >> nm.units.angleSnap >> nm.units.angleSnapOn;
    nm.units.unit = (LengthUnit)unit; nm.units.precision = prec; nm.units.anglePrecision = aprec;
    s >> nm.axesOrigin >> nm.axesX >> nm.axesY >> nm.axesZ;
    quint32 n;
    s >> n;
    nm.materials.resize(n);
    for (auto& mt : nm.materials) {
        s >> mt.name >> mt.color >> mt.alpha >> mt.hasTexture >> mt.texturePng >> mt.texW >> mt.texH >> mt.colorize;
        mt.rev = nextRevision();
    }
    s >> n;
    nm.tags.resize(n);
    for (auto& t : nm.tags) { qint32 dash; s >> t.name >> t.visible >> t.color >> dash; t.dash = dash; }
    if (nm.tags.empty()) { Tag t; t.name = "Untagged"; nm.tags.push_back(t); }
    s >> n;
    nm.defs.resize(n);
    for (auto& d : nm.defs) {
        s >> d.name >> d.description >> d.isGroup >> d.alwaysFaceCamera >> d.glue >> d.cutsOpening;
        if (version >= 7) readAttrs(s, d.attrs);
        if (!readEntities(s, d.ents, version)) { if (error) *error = "Corrupt component definition."; return false; }
    }
    if (!readEntities(s, nm.root, version)) { if (error) *error = "Corrupt model geometry."; return false; }
    readStyle(s, nm.style);
    readShadow(s, nm.shadow);
    readCam(s, nm.camera);
    s >> n;
    nm.scenes.resize(n);
    for (auto& sc : nm.scenes) {
        s >> sc.name >> sc.description;
        readCam(s, sc.cam);
        quint32 nt; s >> nt;
        sc.tagVisible.resize(nt);
        for (size_t i = 0; i < nt; ++i) { bool b; s >> b; sc.tagVisible[i] = b; }
        s >> sc.hasStyle;
        readStyle(s, sc.style);
        readShadow(s, sc.shadow);
        if (version >= 6) {
            s >> sc.matchPhoto;
            if (sc.matchPhoto) {
                MatchPhoto& p = sc.photo;
                s >> p.image >> p.origin.x >> p.origin.y >> p.eyeHeight >> p.turned;
                for (auto& l : p.lines)
                    for (auto& e : l) s >> e.x >> e.y;
            }
        }
    }
    if (s.status() != QDataStream::Ok) {
        if (error) *error = "The file is truncated or damaged.";
        return false;
    }
    m = std::move(nm);
    return true;
}

std::vector<EntRef> mergeModelInto(Model& dst, Entities& dstEnts, const Model& src, const Mat4& xf) {
    std::vector<int> matMap(src.materials.size(), -1), tagMap(src.tags.size(), 0), defMap(src.defs.size(), -1);
    for (size_t i = 0; i < src.materials.size(); ++i) {
        int f = dst.findMaterial(src.materials[i].name);
        if (f < 0) {
            dst.materials.push_back(src.materials[i]);
            f = (int)dst.materials.size() - 1;
        }
        matMap[i] = f;
    }
    for (size_t i = 0; i < src.tags.size(); ++i) {
        int f = -1;
        for (size_t j = 0; j < dst.tags.size(); ++j)
            if (dst.tags[j].name == src.tags[i].name) { f = (int)j; break; }
        if (f < 0) { dst.tags.push_back(src.tags[i]); f = (int)dst.tags.size() - 1; }
        tagMap[i] = f;
    }
    // Definitions: reuse by name+same geometry count is unreliable; append (renaming on clash).
    for (size_t i = 0; i < src.defs.size(); ++i) {
        if (!src.defs[i].alive) continue;
        ComponentDef d = src.defs[i];
        d.name = dst.uniqueDefName(d.name);
        dst.defs.push_back(std::move(d));
        defMap[i] = (int)dst.defs.size() - 1;
    }
    auto mm = [&](int m) { return m >= 0 && m < (int)matMap.size() ? matMap[m] : -1; };
    auto tm = [&](int t) { return t >= 0 && t < (int)tagMap.size() ? tagMap[t] : 0; };
    auto remap = [&](Entities& e, std::function<bool(EntKind, int)> include) {
        for (size_t i = 0; i < e.edges.size(); ++i)
            if (include(EntKind::Edge, (int)i)) { e.edges[i].material = mm(e.edges[i].material); e.edges[i].tag = tm(e.edges[i].tag); }
        for (size_t i = 0; i < e.faces.size(); ++i)
            if (include(EntKind::Face, (int)i)) {
                e.faces[i].matFront = mm(e.faces[i].matFront);
                e.faces[i].matBack = mm(e.faces[i].matBack);
                e.faces[i].tag = tm(e.faces[i].tag);
            }
        for (size_t i = 0; i < e.insts.size(); ++i)
            if (include(EntKind::Instance, (int)i)) {
                auto& in = e.insts[i];
                in.material = mm(in.material);
                in.tag = tm(in.tag);
                if (in.def >= 0 && in.def < (int)defMap.size() && defMap[in.def] >= 0) in.def = defMap[in.def];
                else in.alive = false;
            }
    };
    for (size_t i = 0; i < src.defs.size(); ++i)
        if (defMap[i] >= 0) remap(dst.defs[defMap[i]].ents, [](EntKind, int) { return true; });
    std::vector<EntRef> all;
    const Entities& R = src.root;
    for (size_t i = 0; i < R.edges.size(); ++i) if (R.edges[i].alive) all.push_back({EntKind::Edge, (int)i});
    for (size_t i = 0; i < R.faces.size(); ++i) if (R.faces[i].alive) all.push_back({EntKind::Face, (int)i});
    for (size_t i = 0; i < R.insts.size(); ++i) if (R.insts[i].alive) all.push_back({EntKind::Instance, (int)i});
    for (size_t i = 0; i < R.guides.size(); ++i) if (R.guides[i].alive) all.push_back({EntKind::Guide, (int)i});
    for (size_t i = 0; i < R.dims.size(); ++i) if (R.dims[i].alive) all.push_back({EntKind::Dimension, (int)i});
    for (size_t i = 0; i < R.texts.size(); ++i) if (R.texts[i].alive) all.push_back({EntKind::Text, (int)i});
    auto res = geom::copyEntities(R, all, dstEnts, xf, true);
    std::set<EntRef> rs(res.begin(), res.end());
    remap(dstEnts, [&](EntKind k, int i) { return rs.count({k, i}) > 0; });
    return res;
}

}  // namespace gable
