// ---- G2-04: 3D Studio, KMZ and IFC import (included by geom_tests.cpp)
#include <QDir>
#include <QFile>
#include <zlib.h>

static void put16(QByteArray& b, int v) { b.append(char(v & 255)); b.append(char((v >> 8) & 255)); }
static void put32(QByteArray& b, quint32 v) { for (int k = 0; k < 4; ++k) b.append(char((v >> (8 * k)) & 255)); }
static void putF(QByteArray& b, float f) { quint32 v; memcpy(&v, &f, 4); put32(b, v); }
static QByteArray chunk(int id, const QByteArray& body) { QByteArray c; put16(c, id); put32(c, 6 + body.size()); c += body; return c; }

static double defVolume(const Model& m, const Entities& E, int inst) {
    return geom::volume(m.defs[E.insts[inst].def].ents);
}
static const Entities& groupOf(const Model& m, const Entities& E, int inst) { return m.defs[E.insts[inst].def].ents; }

static void test3DS(const QString& dir) {
    // a 1 x 2 x 3 box in two halves of triangles, red, as 3ds Max writes it
    Vec3 v[8] = {{0, 0, 0}, {1, 0, 0}, {1, 2, 0}, {0, 2, 0}, {0, 0, 3}, {1, 0, 3}, {1, 2, 3}, {0, 2, 3}};
    int t[12][3] = {{0, 2, 1}, {0, 3, 2}, {4, 5, 6}, {4, 6, 7}, {0, 1, 5}, {0, 5, 4}, {1, 2, 6}, {1, 6, 5}, {2, 3, 7}, {2, 7, 6}, {3, 0, 4}, {3, 4, 7}};
    QByteArray verts; put16(verts, 8); for (auto& p : v) { putF(verts, p.x); putF(verts, p.y); putF(verts, p.z); }
    QByteArray fm("Red brick"); fm.append('\0'); put16(fm, 12); for (int i = 0; i < 12; ++i) put16(fm, i);
    QByteArray fcs; put16(fcs, 12); for (auto& f : t) { put16(fcs, f[0]); put16(fcs, f[1]); put16(fcs, f[2]); put16(fcs, 7); }
    fcs += chunk(0x4130, fm);
    QByteArray tri = chunk(0x4110, verts) + chunk(0x4120, fcs);
    QByteArray obj("Box01"); obj.append('\0'); obj += chunk(0x4100, tri);
    QByteArray matName("Red brick"); matName.append('\0');
    QByteArray rgb; rgb.append(char(200)); rgb.append(char(30)); rgb.append(char(20));
    QByteArray mat = chunk(0xA000, matName) + chunk(0xA020, chunk(0x0011, rgb));
    QByteArray ed = chunk(0xAFFF, mat) + chunk(0x4000, obj);
    QByteArray file = chunk(0x4D4D, chunk(0x0002, QByteArray(4, 3)) + chunk(0x3D3D, ed));
    QString p = dir + "/box.3ds";
    { QFile f(p); f.open(QIODevice::WriteOnly); f.write(file); }
    Model m;
    ImportOptions io;
    io.scale = 1000;   // metres
    QString err;
    int idx = importFile(m, m.root, p, io, &err);
    CHECK(idx >= 0);
    if (idx < 0) { std::printf("  3ds: %s\n", qPrintable(err)); return; }
    const Entities& outer = groupOf(m, m.root, idx);
    CHECK(outer.insts.size() == 1);
    CHECK(m.defs[outer.insts[0].def].name.startsWith("Box01"));
    const Entities& box = groupOf(m, outer, 0);
    CHECK(faces(box) == 6);            // coplanar triangles merged
    CHECK(geom::isSolid(box));
    NEAR(geom::volume(box), 1000.0 * 2000 * 3000, 1e3);
    int mf = -1;
    for (auto& f : box.faces) if (f.alive) mf = f.matFront;
    CHECK(mf >= 0 && m.materials[mf].name == "Red brick" && m.materials[mf].color == QColor(200, 30, 20));
    // damaged and wrong files
    { QFile f(dir + "/bad.3ds"); f.open(QIODevice::WriteOnly); f.write(file.left(40)); }
    Model m2;
    importFile(m2, m2.root, dir + "/bad.3ds", io, &err);   // must not crash
    { QFile f(dir + "/no.3ds"); f.open(QIODevice::WriteOnly); f.write("hello"); }
    CHECK(importFile(m2, m2.root, dir + "/no.3ds", io, &err) < 0 && err.contains("3D Studio"));
}

static QByteArray deflateRaw(const QByteArray& in) {
    z_stream s{};
    deflateInit2(&s, 9, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
    QByteArray out(int(deflateBound(&s, in.size())), 0);
    s.next_in = (Bytef*)in.constData(); s.avail_in = in.size();
    s.next_out = (Bytef*)out.data(); s.avail_out = out.size();
    deflate(&s, Z_FINISH);
    out.resize(int(s.total_out));
    deflateEnd(&s);
    return out;
}
static QByteArray zipOf(const std::vector<std::pair<QString, QByteArray>>& files) {
    QByteArray z, cd;
    for (size_t k = 0; k < files.size(); ++k) {
        bool comp = k % 2 == 1;
        QByteArray data = comp ? deflateRaw(files[k].second) : files[k].second;
        quint32 crc = crc32(0, (const Bytef*)files[k].second.constData(), files[k].second.size());
        QByteArray name = files[k].first.toUtf8();
        quint32 off = z.size();
        put32(z, 0x04034b50); put16(z, 20); put16(z, 0); put16(z, comp ? 8 : 0); put16(z, 0); put16(z, 0); put32(z, crc);
        put32(z, data.size()); put32(z, files[k].second.size()); put16(z, name.size()); put16(z, 0); z += name; z += data;
        put32(cd, 0x02014b50); put16(cd, 20); put16(cd, 20); put16(cd, 0); put16(cd, comp ? 8 : 0); put16(cd, 0); put16(cd, 0); put32(cd, crc);
        put32(cd, data.size()); put32(cd, files[k].second.size()); put16(cd, name.size()); put16(cd, 0); put16(cd, 0); put16(cd, 0);
        put16(cd, 0); put32(cd, 0); put32(cd, off); cd += name;
    }
    quint32 cdo = z.size();
    z += cd;
    put32(z, 0x06054b50); put16(z, 0); put16(z, 0); put16(z, files.size()); put16(z, files.size()); put32(z, cd.size()); put32(z, cdo); put16(z, 0);
    return z;
}

static void testKMZ(const QString& dir) {
    // a 1 m cube in COLLADA, placed twice: at the reference point, and 10 m east turned 90°
    QString dae = R"(<?xml version="1.0"?><COLLADA xmlns="http://www.collada.org/2005/11/COLLADASchema" version="1.4.1">
<asset><unit meter="1" name="meter"/><up_axis>Z_UP</up_axis></asset>
<library_geometries><geometry id="g"><mesh><source id="p"><float_array id="pa" count="24">0 0 0 1 0 0 1 1 0 0 1 0 0 0 1 1 0 1 1 1 1 0 1 1</float_array>
<technique_common><accessor source="#pa" count="8" stride="3"><param name="X" type="float"/><param name="Y" type="float"/><param name="Z" type="float"/></accessor></technique_common></source>
<vertices id="v"><input semantic="POSITION" source="#p"/></vertices>
<triangles count="12"><input semantic="VERTEX" source="#v" offset="0"/><p>0 2 1 0 3 2 4 5 6 4 6 7 0 1 5 0 5 4 1 2 6 1 6 5 2 3 7 2 7 6 3 0 4 3 4 7</p></triangles></mesh></geometry></library_geometries>
<library_visual_scenes><visual_scene id="s"><node id="n"><instance_geometry url="#g"/></node></visual_scene></library_visual_scenes>
<scene><instance_visual_scene url="#s"/></scene></COLLADA>)";
    double lon = 4.9, lat = 52.37, dlon = 10.0 / (6378137.0 * std::cos(lat * kPi / 180)) * 180 / kPi;
    QString kml = QString(R"(<?xml version="1.0" encoding="UTF-8"?><kml xmlns="http://www.opengis.net/kml/2.2"><Document>
<Placemark><name>Shed</name><Model><altitudeMode>relativeToGround</altitudeMode><Location><longitude>%1</longitude><latitude>%2</latitude><altitude>0</altitude></Location>
<Orientation><heading>0</heading><tilt>0</tilt><roll>0</roll></Orientation><Scale><x>1</x><y>1</y><z>1</z></Scale><Link><href>models/cube.dae</href></Link></Model></Placemark>
<Placemark><name>Barn</name><Model><Location><longitude>%3</longitude><latitude>%2</latitude><altitude>0</altitude></Location>
<Orientation><heading>90</heading></Orientation><Scale><x>2</x><y>2</y><z>2</z></Scale><Link><href>models/cube.dae</href></Link></Model></Placemark>
</Document></kml>)").arg(lon, 0, 'f', 8).arg(lat, 0, 'f', 8).arg(lon + dlon, 0, 'f', 8);
    QString p = dir + "/site.kmz";
    { QFile f(p); f.open(QIODevice::WriteOnly); f.write(zipOf({{"doc.kml", kml.toUtf8()}, {"models/cube.dae", dae.toUtf8()}})); }
    Model m;
    ImportOptions io;
    QString err;
    int idx = importFile(m, m.root, p, io, &err);
    CHECK(idx >= 0);
    if (idx < 0) { std::printf("  kmz: %s\n", qPrintable(err)); return; }
    const Entities& g = groupOf(m, m.root, idx);
    CHECK(g.insts.size() == 2);
    if (g.insts.size() != 2) return;
    CHECK(g.insts[0].name == "Shed" && g.insts[1].name == "Barn");
    Vec3 o = g.insts[1].xf.origin();
    NEAR(o.x, 10000, 5);           // 10 m east
    NEAR(o.y, 0, 5);
    Vec3 x = g.insts[1].xf.vector({1, 0, 0}), x0 = g.insts[0].xf.vector({1, 0, 0});
    NEAR(x.x, 0, 1e-6);            // heading 90°: turned clockwise...
    CHECK(x.y < 0);
    NEAR(x.length() / x0.length(), 2, 1e-9);   // ...and twice the size
    // the cube itself: 1 m, solid
    const Entities* cube = &groupOf(m, g, 0);
    while (cube->faces.empty() && !cube->insts.empty()) cube = &m.defs[cube->insts[0].def].ents;
    CHECK(faces(*cube) == 6 && geom::isSolid(*cube));
    NEAR(x0.length(), 1000, 1e-6);   // COLLADA's metres
    { QFile f(dir + "/empty.kmz"); f.open(QIODevice::WriteOnly); f.write(zipOf({{"doc.kml", QByteArray("<kml><Document/></kml>")}})); }
    CHECK(importFile(m, m.root, dir + "/empty.kmz", io, &err) < 0 && err.contains("no 3D models"));
}

static void testIFC(const QString& dir) {
    QString ifc = R"(ISO-10303-21;
HEADER;
FILE_DESCRIPTION(('ViewDefinition [CoordinationView]'),'2;1');
FILE_NAME('house.ifc','2026-10-28T00:00:00',(''),(''),'','','');
FILE_SCHEMA(('IFC4'));
ENDSEC;
DATA;
#1=IFCSIUNIT(*,.LENGTHUNIT.,.MILLI.,.METRE.);
#2=IFCUNITASSIGNMENT((#1));
#3=IFCPROJECT('0001',$,'House',$,$,$,$,$,#2);
#10=IFCCARTESIANPOINT((0.,0.,0.));
#11=IFCDIRECTION((0.,0.,1.));
#12=IFCDIRECTION((1.,0.,0.));
#13=IFCAXIS2PLACEMENT3D(#10,#11,#12);
#14=IFCLOCALPLACEMENT($,#13);
#20=IFCCARTESIANPOINT((1000.,500.,0.));
#21=IFCAXIS2PLACEMENT3D(#20,$,$);
#22=IFCLOCALPLACEMENT(#14,#21);
#30=IFCCARTESIANPOINT((2000.,100.));
#31=IFCAXIS2PLACEMENT2D(#30,$);
#32=IFCRECTANGLEPROFILEDEF(.AREA.,'Wall',#31,4000.,200.);
#33=IFCEXTRUDEDAREASOLID(#32,#13,#11,2700.);
#34=IFCSHAPEREPRESENTATION($,'Body','SweptSolid',(#33));
#35=IFCPRODUCTDEFINITIONSHAPE($,$,(#34));
#36=IFCWALL('0002',$,'Wall \X2\00C9\X0\ast',$,$,#22,#35,$,$);
#40=IFCCOLOURRGB($,0.8,0.1,0.1);
#41=IFCSURFACESTYLERENDERING(#40,0.,$,$,$,$,$,$,.NOTDEFINED.);
#42=IFCSURFACESTYLE('Red plaster',.BOTH.,(#41));
#43=IFCSTYLEDITEM(#33,(#42),$);
#50=IFCCARTESIANPOINT((0.,0.,0.));
#51=IFCCARTESIANPOINT((3000.,0.,0.));
#52=IFCCARTESIANPOINT((3000.,3000.,0.));
#53=IFCCARTESIANPOINT((0.,3000.,0.));
#54=IFCCARTESIANPOINT((0.,0.,-200.));
#55=IFCCARTESIANPOINT((3000.,0.,-200.));
#56=IFCCARTESIANPOINT((3000.,3000.,-200.));
#57=IFCCARTESIANPOINT((0.,3000.,-200.));
#60=IFCPOLYLOOP((#50,#51,#52,#53));
#61=IFCPOLYLOOP((#57,#56,#55,#54));
#62=IFCPOLYLOOP((#50,#54,#55,#51));
#63=IFCPOLYLOOP((#51,#55,#56,#52));
#64=IFCPOLYLOOP((#52,#56,#57,#53));
#65=IFCPOLYLOOP((#53,#57,#54,#50));
#140=IFCFACEOUTERBOUND(#60,.T.);
#70=IFCFACE((#140));
#141=IFCFACEOUTERBOUND(#61,.T.);
#71=IFCFACE((#141));
#142=IFCFACEOUTERBOUND(#62,.T.);
#72=IFCFACE((#142));
#143=IFCFACEOUTERBOUND(#63,.T.);
#73=IFCFACE((#143));
#144=IFCFACEOUTERBOUND(#64,.T.);
#74=IFCFACE((#144));
#145=IFCFACEOUTERBOUND(#65,.T.);
#75=IFCFACE((#145));
#76=IFCCLOSEDSHELL((#70,#71,#72,#73,#74,#75));
#77=IFCFACETEDBREP(#76);
#78=IFCSHAPEREPRESENTATION($,'Body','Brep',(#77));
#79=IFCPRODUCTDEFINITIONSHAPE($,$,(#78));
#80=IFCSLAB('0003',$,'Floor',$,$,#14,#79,$,.FLOOR.);
#90=IFCCIRCLEPROFILEDEF(.AREA.,$,$,150.);
#91=IFCEXTRUDEDAREASOLID(#90,#13,#11,2500.);
#92=IFCSHAPEREPRESENTATION($,'Body','SweptSolid',(#91));
#93=IFCREPRESENTATIONMAP(#13,#92);
#94=IFCCARTESIANPOINT((2500.,2500.,0.));
#95=IFCCARTESIANTRANSFORMATIONOPERATOR3D($,$,#94,1.,$);
#96=IFCMAPPEDITEM(#93,#95);
#97=IFCSHAPEREPRESENTATION($,'Body','MappedRepresentation',(#96));
#98=IFCPRODUCTDEFINITIONSHAPE($,$,(#97));
#99=IFCCOLUMN('0004',$,'Column',$,$,#14,#98,$,$);
#100=IFCRECTANGLEPROFILEDEF(.AREA.,$,$,1000.,2000.);
#101=IFCEXTRUDEDAREASOLID(#100,#13,#11,200.);
#102=IFCSHAPEREPRESENTATION($,'Body','SweptSolid',(#101));
#103=IFCPRODUCTDEFINITIONSHAPE($,$,(#102));
#104=IFCOPENINGELEMENT('0005',$,'Window hole',$,$,#22,#103,$,$);
#110=IFCCARTESIANPOINT((0.,0.));
#111=IFCCARTESIANPOINT((2000.,0.));
#112=IFCCARTESIANPOINT((2000.,2000.));
#113=IFCCARTESIANPOINT((0.,2000.));
#114=IFCPOLYLINE((#110,#111,#112,#113,#110));
#115=IFCCARTESIANPOINT((500.,500.));
#116=IFCCARTESIANPOINT((1500.,500.));
#117=IFCCARTESIANPOINT((1500.,1500.));
#118=IFCCARTESIANPOINT((500.,1500.));
#119=IFCPOLYLINE((#115,#116,#117,#118,#115));
#120=IFCARBITRARYPROFILEDEFWITHVOIDS(.AREA.,$,#114,(#119));
#121=IFCEXTRUDEDAREASOLID(#120,#13,#11,3000.);
#122=IFCSHAPEREPRESENTATION($,'Body','SweptSolid',(#121));
#123=IFCPRODUCTDEFINITIONSHAPE($,$,(#122));
#124=IFCSPACE('0006',$,'Courtyard',$,$,#14,#123,$,$,$,$);
#130=IFCCARTESIANPOINTLIST3D(((0.,0.,0.),(1000.,0.,0.),(0.,1000.,0.),(0.,0.,1000.)));
#131=IFCTRIANGULATEDFACESET(#130,$,.T.,((1,3,2),(1,2,4),(2,3,4),(3,1,4)),$);
#132=IFCSHAPEREPRESENTATION($,'Body','Tessellation',(#131));
#133=IFCPRODUCTDEFINITIONSHAPE($,$,(#132));
#134=IFCBUILDINGELEMENTPROXY('0007',$,'Tetra',$,$,#14,#133,$,$);
ENDSEC;
END-ISO-10303-21;
)";
    QString p = dir + "/house.ifc";
    { QFile f(p); f.open(QIODevice::WriteOnly); f.write(ifc.toUtf8()); }
    Model m;
    ImportOptions io;
    QString err;
    int idx = importFile(m, m.root, p, io, &err);
    CHECK(idx >= 0);
    if (idx < 0) { std::printf("  ifc: %s\n", qPrintable(err)); return; }
    QStringList notes = takeImportNotes();
    CHECK(notes.size() == 1 && notes[0].contains("openings"));
    const Entities& g = groupOf(m, m.root, idx);
    std::map<QString, int> byName;
    for (int k = 0; k < (int)g.insts.size(); ++k) byName[g.insts[k].name] = k;
    CHECK(g.insts.size() == 5);          // wall, slab, column, space, proxy: the opening is not a part
    CHECK(byName.count(QString::fromUtf8("Wall Éast")));
    if (!byName.count(QString::fromUtf8("Wall Éast")) || !byName.count("Floor") || !byName.count("Column")) return;
    int w = byName[QString::fromUtf8("Wall Éast")];
    NEAR(defVolume(m, g, w), 4000.0 * 200 * 2700, 1e3);
    BBox wb = groupOf(m, g, w).bbox();
    NEAR(wb.lo.x, 1000, 1e-3);           // placement (1000, 500) plus the profile's centre (2000, 100) less half its size
    NEAR(wb.lo.y, 500, 1e-3);
    NEAR(wb.hi.z, 2700, 1e-3);
    CHECK(m.tags[g.insts[w].tag].name == "IfcWall");
    int mf = -1;
    for (auto& f : groupOf(m, g, w).faces) if (f.alive) mf = f.matFront;
    CHECK(mf >= 0 && m.materials[mf].name == "Red plaster");
    int s = byName["Floor"];
    CHECK(geom::isSolid(groupOf(m, g, s)));
    NEAR(defVolume(m, g, s), 3000.0 * 3000 * 200, 1e3);
    int c = byName["Column"];
    BBox cb = groupOf(m, g, c).bbox();
    NEAR((cb.lo.x + cb.hi.x) / 2, 2500, 1);
    NEAR(cb.hi.z, 2500, 1e-3);
    int sp = byName["Courtyard"];
    CHECK(!m.tags[g.insts[sp].tag].visible);   // spaces start hidden
    NEAR(defVolume(m, g, sp), (4e6 - 1e6) * 3000, 1e4);  // the void is kept as a hole
    int t = byName["Tetra"];
    NEAR(defVolume(m, g, t), 1e9 / 6, 1e3);
    { QFile f(dir + "/bad.ifc"); f.open(QIODevice::WriteOnly); f.write("ISO-10303-21;\nDATA;\n#1=IFCWALL('x',$,$,$,$,#99,#98,$);\nENDSEC;"); }
    CHECK(importFile(m, m.root, dir + "/bad.ifc", io, &err) < 0 && err.contains("No building parts"));
    { QFile f(dir + "/cut.ifc"); f.open(QIODevice::WriteOnly); f.write(ifc.toUtf8().left(ifc.size() / 2)); }
    importFile(m, m.root, dir + "/cut.ifc", io, &err);   // a file cut short must not crash
}

static void testImports() {
    QTemporaryDir d;
    test3DS(d.path());
    testKMZ(d.path());
    testIFC(d.path());
}
