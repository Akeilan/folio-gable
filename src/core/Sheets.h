#pragma once
// Folio Gable — drawing sheets (G2-03): scenes placed on paper with dimensions, labels and a
// title block, printed or saved as PDF. What SketchUp users get from Send to LayOut, kept in the
// model file. Everything on a sheet is in millimetres of paper, origin at the top left.
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <vector>

namespace gable {

struct SheetViewport {
    int scene = -1;          // index in Model::scenes; -1 = the camera as it was when placed
    QRectF rect;             // on the paper, mm
    double scale = 0;        // model mm per paper mm in parallel projection (50 = 1:50); 0 = fit what the scene shows
    bool frame = true;       // a thin border round it
};

struct SheetLabel {
    QString text;
    QPointF at;              // the text's top-left, mm
    bool leader = false;
    QPointF target;          // where the leader points, mm
    double size = 3.5;       // text height, mm (3.5 mm is the usual drawing lettering)
};

struct SheetDimension {
    QPointF a, b;            // measured points, mm
    double offset = 8;       // the dimension line's distance from a–b, mm (sign: which side)
    int viewport = -1;       // whose scale turns paper mm into model lengths (-1: none, paper mm)
    QString text;            // typed over the measured value; empty = measured
};

struct TitleBlock {
    bool show = true;
    QString project, title, drawnBy, date, sheetNo, scale;   // scale empty = from the first scaled viewport
};

struct Sheet {
    QString name = "Sheet 1";
    QString paper = "A3";    // A4 A3 A2 A1 A0 Letter Legal Tabloid ArchC ArchD
    bool landscape = true;
    double margin = 10;      // mm
    std::vector<SheetViewport> viewports;
    std::vector<SheetLabel> labels;
    std::vector<SheetDimension> dims;
    TitleBlock title;
};

inline QSizeF paperSizeMm(const QString& p, bool landscape) {
    static const struct { const char* n; double w, h; } sizes[] = {
        {"A4", 210, 297}, {"A3", 297, 420}, {"A2", 420, 594}, {"A1", 594, 841}, {"A0", 841, 1189},
        {"Letter", 215.9, 279.4}, {"Legal", 215.9, 355.6}, {"Tabloid", 279.4, 431.8}, {"ArchC", 457.2, 609.6}, {"ArchD", 609.6, 914.4}};
    double w = 297, h = 420;
    for (auto& s : sizes) if (p == s.n) { w = s.w; h = s.h; }
    return landscape ? QSizeF(h, w) : QSizeF(w, h);
}

inline QJsonObject toJson(const Sheet& s) {
    auto pt = [](const QPointF& p) { return QJsonArray{p.x(), p.y()}; };
    QJsonObject o{{"name", s.name}, {"paper", s.paper}, {"landscape", s.landscape}, {"margin", s.margin}};
    QJsonArray vs, ls, ds;
    for (auto& v : s.viewports)
        vs.append(QJsonObject{{"scene", v.scene}, {"rect", QJsonArray{v.rect.x(), v.rect.y(), v.rect.width(), v.rect.height()}},
                              {"scale", v.scale}, {"frame", v.frame}});
    for (auto& l : s.labels)
        ls.append(QJsonObject{{"text", l.text}, {"at", pt(l.at)}, {"leader", l.leader}, {"target", pt(l.target)}, {"size", l.size}});
    for (auto& d : s.dims)
        ds.append(QJsonObject{{"a", pt(d.a)}, {"b", pt(d.b)}, {"offset", d.offset}, {"viewport", d.viewport}, {"text", d.text}});
    o["viewports"] = vs;
    o["labels"] = ls;
    o["dims"] = ds;
    const TitleBlock& t = s.title;
    o["title"] = QJsonObject{{"show", t.show}, {"project", t.project}, {"title", t.title}, {"drawnBy", t.drawnBy},
                             {"date", t.date}, {"sheetNo", t.sheetNo}, {"scale", t.scale}};
    return o;
}

inline Sheet sheetFromJson(const QJsonObject& o) {
    auto pt = [](const QJsonValue& v) { QJsonArray a = v.toArray(); return QPointF(a.at(0).toDouble(), a.at(1).toDouble()); };
    Sheet s;
    s.name = o["name"].toString(s.name);
    s.paper = o["paper"].toString(s.paper);
    s.landscape = o["landscape"].toBool(true);
    s.margin = o["margin"].toDouble(10);
    for (auto v : o["viewports"].toArray()) {
        QJsonObject j = v.toObject();
        SheetViewport vp;
        vp.scene = j["scene"].toInt(-1);
        QJsonArray r = j["rect"].toArray();
        vp.rect = QRectF(r.at(0).toDouble(), r.at(1).toDouble(), r.at(2).toDouble(), r.at(3).toDouble());
        vp.scale = j["scale"].toDouble();
        vp.frame = j["frame"].toBool(true);
        s.viewports.push_back(vp);
    }
    for (auto v : o["labels"].toArray()) {
        QJsonObject j = v.toObject();
        SheetLabel l;
        l.text = j["text"].toString();
        l.at = pt(j["at"]);
        l.leader = j["leader"].toBool();
        l.target = pt(j["target"]);
        l.size = j["size"].toDouble(3.5);
        s.labels.push_back(l);
    }
    for (auto v : o["dims"].toArray()) {
        QJsonObject j = v.toObject();
        SheetDimension d;
        d.a = pt(j["a"]);
        d.b = pt(j["b"]);
        d.offset = j["offset"].toDouble(8);
        d.viewport = j["viewport"].toInt(-1);
        d.text = j["text"].toString();
        s.dims.push_back(d);
    }
    QJsonObject t = o["title"].toObject();
    s.title.show = t["show"].toBool(true);
    s.title.project = t["project"].toString();
    s.title.title = t["title"].toString();
    s.title.drawnBy = t["drawnBy"].toString();
    s.title.date = t["date"].toString();
    s.title.sheetNo = t["sheetNo"].toString();
    s.title.scale = t["scale"].toString();
    return s;
}

inline QByteArray sheetsToJson(const std::vector<Sheet>& sheets) {
    QJsonArray a;
    for (auto& s : sheets) a.append(toJson(s));
    return QJsonDocument(a).toJson(QJsonDocument::Compact);
}

inline std::vector<Sheet> sheetsFromJson(const QByteArray& data) {
    std::vector<Sheet> out;
    for (auto v : QJsonDocument::fromJson(data).array()) out.push_back(sheetFromJson(v.toObject()));
    return out;
}

}  // namespace gable
