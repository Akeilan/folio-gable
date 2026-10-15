#include "ui/Icons.h"
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QtMath>
#include <map>

namespace gable {

static const QColor kInk(40, 44, 52);
static const QColor kAccent(32, 112, 214);
static const QColor kRed(214, 52, 48);
static const QColor kGreen(46, 150, 64);
static const QColor kBlue(40, 80, 220);
static const QColor kFill(150, 180, 230, 140);

static void drawIcon(QPainter& p, const QString& n) {
    // 32 x 32 design grid
    QPen ink(kInk, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen thin(kInk, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    QPen acc(kAccent, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(ink);
    p.setBrush(Qt::NoBrush);
    auto box3d = [&](QRectF r, double d, bool fill) {
        QPolygonF front;
        front << r.topLeft() << r.topRight() << r.bottomRight() << r.bottomLeft();
        QPolygonF top;
        top << r.topLeft() << r.topLeft() + QPointF(d, -d) << r.topRight() + QPointF(d, -d) << r.topRight();
        QPolygonF side;
        side << r.topRight() << r.topRight() + QPointF(d, -d) << r.bottomRight() + QPointF(d, -d) << r.bottomRight();
        if (fill) {
            p.setBrush(QColor(230, 232, 236)); p.drawPolygon(front);
            p.setBrush(QColor(250, 250, 252)); p.drawPolygon(top);
            p.setBrush(QColor(196, 202, 212)); p.drawPolygon(side);
            p.setBrush(Qt::NoBrush);
        } else {
            p.drawPolygon(front); p.drawPolygon(top); p.drawPolygon(side);
        }
    };
    if (n == "select") {
        QPainterPath a;
        a.moveTo(9, 4); a.lineTo(9, 25); a.lineTo(14, 20); a.lineTo(18, 28); a.lineTo(21, 26.5); a.lineTo(17, 18.5); a.lineTo(24, 18.5); a.closeSubpath();
        p.setBrush(Qt::white);
        p.drawPath(a);
    } else if (n == "line") {
        p.setPen(QPen(kInk, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(6, 26), QPointF(26, 6));
        p.setPen(thin); p.setBrush(kAccent);
        p.drawEllipse(QPointF(6, 26), 2.5, 2.5); p.drawEllipse(QPointF(26, 6), 2.5, 2.5);
    } else if (n == "freehand") {
        QPainterPath s; s.moveTo(4, 22); s.cubicTo(10, 4, 14, 30, 20, 14); s.cubicTo(23, 7, 26, 12, 28, 8);
        p.drawPath(s);
    } else if (n == "rect" || n == "rotrect") {
        p.save();
        if (n == "rotrect") { p.translate(16, 16); p.rotate(-20); p.translate(-16, -16); }
        p.setBrush(kFill);
        p.drawRect(QRectF(5, 9, 22, 15));
        p.restore();
        if (n == "rotrect") { p.setPen(QPen(kAccent, 1.6)); p.drawArc(QRectF(20, 18, 10, 10), 0, 90 * 16); }
    } else if (n == "circle" || n == "polygon") {
        p.setBrush(kFill);
        if (n == "circle") p.drawEllipse(QPointF(16, 16), 11, 11);
        else {
            QPolygonF h;
            for (int i = 0; i < 6; ++i) h << QPointF(16 + 11 * std::cos(i * M_PI / 3), 16 + 11 * std::sin(i * M_PI / 3));
            p.drawPolygon(h);
        }
        p.setPen(thin); p.setBrush(kAccent); p.drawEllipse(QPointF(16, 16), 1.8, 1.8);
    } else if (n == "arc" || n == "arc2" || n == "arc3" || n == "pie") {
        if (n == "pie") {
            p.setBrush(kFill);
            p.drawPie(QRectF(4, 6, 24, 24), 0, 120 * 16);
        } else p.drawArc(QRectF(4, 8, 24, 24), 0, 180 * 16);
        p.setPen(thin); p.setBrush(kAccent);
        if (n == "arc2") { p.drawEllipse(QPointF(4, 20), 2.2, 2.2); p.drawEllipse(QPointF(28, 20), 2.2, 2.2); }
        if (n == "arc3") { p.drawEllipse(QPointF(4, 20), 2.2, 2.2); p.drawEllipse(QPointF(16, 8), 2.2, 2.2); p.drawEllipse(QPointF(28, 20), 2.2, 2.2); }
        if (n == "arc") { p.drawEllipse(QPointF(16, 20), 2.2, 2.2); p.setPen(QPen(kInk, 1.2, Qt::DashLine)); p.drawLine(QPointF(16, 20), QPointF(28, 20)); }
    } else if (n == "pushpull") {
        box3d(QRectF(6, 14, 14, 13), 6, true);
        p.setPen(acc);
        p.drawLine(QPointF(17, 12), QPointF(17, 2));
        p.drawLine(QPointF(17, 2), QPointF(13, 6)); p.drawLine(QPointF(17, 2), QPointF(21, 6));
    } else if (n == "move") {
        p.setPen(QPen(kRed, 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(16, 4), QPointF(16, 28)); p.drawLine(QPointF(4, 16), QPointF(28, 16));
        for (auto [a, b, c] : {std::tuple{QPointF(16, 4), QPointF(12, 8), QPointF(20, 8)}, std::tuple{QPointF(16, 28), QPointF(12, 24), QPointF(20, 24)},
                               std::tuple{QPointF(4, 16), QPointF(8, 12), QPointF(8, 20)}, std::tuple{QPointF(28, 16), QPointF(24, 12), QPointF(24, 20)}}) {
            p.drawLine(a, b); p.drawLine(a, c);
        }
    } else if (n == "rotate") {
        p.setPen(QPen(kRed, 2.4, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(QRectF(5, 5, 22, 22), 30 * 16, 280 * 16);
        p.drawLine(QPointF(25.5, 10), QPointF(26.5, 4)); p.drawLine(QPointF(25.5, 10), QPointF(20, 8.5));
    } else if (n == "scale") {
        p.setPen(QPen(kInk, 1.4, Qt::DashLine));
        p.drawRect(QRectF(5, 11, 16, 16));
        p.setPen(ink); p.setBrush(kFill);
        p.drawRect(QRectF(5, 5, 22, 22));
        p.setPen(Qt::NoPen); p.setBrush(kGreen);
        for (QPointF c : {QPointF(5, 5), QPointF(27, 5), QPointF(5, 27), QPointF(27, 27), QPointF(16, 5), QPointF(27, 16)})
            p.drawRect(QRectF(c.x() - 2.5, c.y() - 2.5, 5, 5));
    } else if (n == "offset") {
        p.setBrush(kFill);
        p.drawRect(QRectF(4, 4, 24, 24));
        p.setPen(QPen(kAccent, 2.2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(10, 10, 12, 12));
    } else if (n == "followme") {
        QPainterPath s; s.moveTo(4, 26); s.lineTo(4, 12); s.quadTo(4, 6, 10, 6); s.lineTo(26, 6);
        p.setPen(QPen(kRed, 2.0, Qt::DashLine)); p.drawPath(s);
        p.setPen(ink); p.setBrush(kFill);
        p.drawRect(QRectF(1, 22, 8, 7));
    } else if (n == "eraser") {
        p.save(); p.translate(16, 16); p.rotate(-40);
        p.setBrush(QColor(240, 130, 150)); p.drawRoundedRect(QRectF(-11, -6, 14, 12), 2, 2);
        p.setBrush(QColor(240, 240, 245)); p.drawRoundedRect(QRectF(3, -6, 9, 12), 2, 2);
        p.restore();
        p.drawLine(QPointF(5, 28), QPointF(27, 28));
    } else if (n == "paint") {
        QPolygonF b; b << QPointF(8, 10) << QPointF(18, 4) << QPointF(26, 16) << QPointF(16, 22);
        p.setBrush(QColor(235, 235, 240)); p.drawPolygon(b);
        p.setPen(Qt::NoPen); p.setBrush(QColor(60, 120, 230));
        QPainterPath drip; drip.moveTo(8, 12); drip.cubicTo(4, 18, 3, 24, 6, 26); drip.cubicTo(9, 26, 9, 20, 8, 12);
        p.drawPath(drip);
        p.setPen(ink); p.drawLine(QPointF(18, 4), QPointF(22, 1));
    } else if (n == "tape") {
        p.setBrush(QColor(250, 210, 60)); p.drawEllipse(QPointF(12, 18), 9, 9);
        p.setBrush(Qt::white); p.drawEllipse(QPointF(12, 18), 3, 3);
        p.setBrush(QColor(250, 210, 60)); p.drawRect(QRectF(19, 22, 10, 5));
    } else if (n == "protractor") {
        p.setBrush(QColor(250, 210, 60, 160));
        p.drawPie(QRectF(3, 8, 26, 26), 0, 180 * 16);
        p.setPen(thin);
        for (int i = 1; i < 6; ++i) {
            double a = M_PI * i / 6;
            p.drawLine(QPointF(16 + 9 * std::cos(a), 21 - 9 * std::sin(a)), QPointF(16 + 12 * std::cos(a), 21 - 12 * std::sin(a)));
        }
    } else if (n == "dimension") {
        p.drawLine(QPointF(5, 8), QPointF(5, 26)); p.drawLine(QPointF(27, 8), QPointF(27, 26));
        p.drawLine(QPointF(5, 16), QPointF(27, 16));
        p.drawLine(QPointF(5, 16), QPointF(9, 13)); p.drawLine(QPointF(5, 16), QPointF(9, 19));
        p.drawLine(QPointF(27, 16), QPointF(23, 13)); p.drawLine(QPointF(27, 16), QPointF(23, 19));
    } else if (n == "text") {
        p.setBrush(Qt::white); p.drawRect(QRectF(10, 4, 19, 12));
        p.drawLine(QPointF(10, 16), QPointF(3, 27));
        p.setPen(thin); p.drawLine(QPointF(13, 8), QPointF(26, 8)); p.drawLine(QPointF(13, 12), QPointF(22, 12));
    } else if (n == "text3d") {
        QFont f("Sans", 18, QFont::Black);
        QPainterPath t; t.addText(4, 26, f, "A");
        p.setBrush(QColor(200, 200, 210)); p.translate(3, -2); p.drawPath(t); p.translate(-3, 2);
        p.setBrush(kFill); p.drawPath(t);
    } else if (n == "axes") {
        p.setPen(QPen(kRed, 2.4)); p.drawLine(QPointF(10, 22), QPointF(28, 22));
        p.setPen(QPen(kGreen, 2.4)); p.drawLine(QPointF(10, 22), QPointF(22, 12));
        p.setPen(QPen(kBlue, 2.4)); p.drawLine(QPointF(10, 22), QPointF(10, 3));
    } else if (n == "section") {
        p.setPen(QPen(QColor(230, 120, 30), 2.2));
        p.drawRect(QRectF(6, 6, 20, 20));
        p.drawLine(QPointF(6, 6), QPointF(2, 2)); p.drawLine(QPointF(26, 6), QPointF(30, 2));
    } else if (n == "orbit") {
        p.setPen(QPen(kInk, 2.2));
        p.drawEllipse(QPointF(16, 16), 12, 6);
        p.setPen(QPen(kRed, 2.2)); p.drawArc(QRectF(4, 4, 24, 24), 200 * 16, 140 * 16);
        p.setBrush(kInk); p.setPen(Qt::NoPen); p.drawEllipse(QPointF(16, 16), 3, 3);
    } else if (n == "pan") {
        QPainterPath h;
        h.moveTo(10, 28); h.lineTo(7, 18); h.lineTo(9, 16); h.lineTo(11, 19); h.lineTo(11, 7); h.lineTo(13.5, 6); h.lineTo(14, 15);
        h.lineTo(14, 5); h.lineTo(16.5, 4.5); h.lineTo(17, 15); h.lineTo(17, 6); h.lineTo(19.5, 6); h.lineTo(20, 16);
        h.lineTo(20, 9); h.lineTo(22.5, 9); h.lineTo(23, 22); h.lineTo(20, 28); h.closeSubpath();
        p.setBrush(QColor(250, 230, 200)); p.drawPath(h);
    } else if (n == "zoom" || n == "zoomwindow" || n == "zoomextents") {
        p.setBrush(Qt::white);
        p.drawEllipse(QPointF(13, 13), 8, 8);
        p.setPen(QPen(kInk, 3.6, Qt::SolidLine, Qt::RoundCap)); p.drawLine(QPointF(19, 19), QPointF(27, 27));
        p.setPen(thin);
        if (n == "zoomwindow") { p.setPen(QPen(kRed, 1.4)); p.drawRect(QRectF(9, 9, 8, 8)); }
        if (n == "zoomextents") { p.setPen(QPen(kRed, 1.6)); p.drawLine(QPointF(9, 13), QPointF(17, 13)); p.drawLine(QPointF(13, 9), QPointF(13, 17)); }
    } else if (n == "walk") {
        p.drawEllipse(QPointF(10, 25), 4, 3); p.drawEllipse(QPointF(21, 13), 4, 3);
        p.drawEllipse(QPointF(8, 19), 1.5, 1.5); p.drawEllipse(QPointF(23, 7), 1.5, 1.5);
    } else if (n == "lookaround") {
        p.setBrush(Qt::white); p.drawEllipse(QPointF(16, 16), 11, 7);
        p.setBrush(kInk); p.drawEllipse(QPointF(16, 16), 4, 4);
    } else if (n == "poscam") {
        p.setBrush(QColor(220, 220, 228)); p.drawRect(QRectF(4, 10, 17, 12));
        QPolygonF l; l << QPointF(21, 13) << QPointF(28, 9) << QPointF(28, 23) << QPointF(21, 19); p.drawPolygon(l);
    } else if (n == "new") {
        QPolygonF d; d << QPointF(7, 3) << QPointF(20, 3) << QPointF(26, 9) << QPointF(26, 29) << QPointF(7, 29);
        p.setBrush(Qt::white); p.drawPolygon(d); p.drawLine(QPointF(20, 3), QPointF(20, 9)); p.drawLine(QPointF(20, 9), QPointF(26, 9));
    } else if (n == "open") {
        p.setBrush(QColor(250, 210, 120)); p.drawRect(QRectF(3, 9, 24, 18));
        p.setBrush(QColor(255, 230, 160)); QPolygonF f; f << QPointF(6, 14) << QPointF(30, 14) << QPointF(26, 27) << QPointF(3, 27); p.drawPolygon(f);
    } else if (n == "save") {
        p.setBrush(kAccent); p.drawRoundedRect(QRectF(4, 4, 24, 24), 2, 2);
        p.setBrush(Qt::white); p.drawRect(QRectF(9, 4, 14, 9)); p.drawRect(QRectF(8, 18, 16, 10));
    } else if (n == "undo" || n == "redo") {
        if (n == "redo") { p.translate(32, 0); p.scale(-1, 1); }
        p.setPen(QPen(kInk, 2.6, Qt::SolidLine, Qt::RoundCap));
        QPainterPath s; s.moveTo(8, 13); s.lineTo(20, 13); s.cubicTo(28, 13, 28, 26, 20, 26); s.lineTo(12, 26);
        p.drawPath(s); p.drawLine(QPointF(8, 13), QPointF(13, 8)); p.drawLine(QPointF(8, 13), QPointF(13, 18));
    } else if (n.startsWith("view_")) {
        QString v = n.mid(5);
        if (v == "iso") box3d(QRectF(6, 12, 14, 14), 7, true);
        else {
            p.setBrush(QColor(230, 232, 236));
            p.drawRect(QRectF(7, 7, 18, 18));
            p.setPen(QPen(kAccent, 2.4));
            if (v == "top") p.drawLine(QPointF(7, 7), QPointF(25, 7));
            if (v == "front") p.drawLine(QPointF(7, 25), QPointF(25, 25));
            if (v == "right") p.drawLine(QPointF(25, 7), QPointF(25, 25));
            if (v == "back") { p.drawLine(QPointF(7, 7), QPointF(25, 7)); p.drawLine(QPointF(7, 25), QPointF(25, 25)); }
            if (v == "left") p.drawLine(QPointF(7, 7), QPointF(7, 25));
            if (v == "bottom") { p.setBrush(kFill); p.drawRect(QRectF(7, 7, 18, 18)); }
        }
    } else if (n == "xray") {
        box3d(QRectF(6, 12, 14, 14), 7, false);
        p.setPen(QPen(kInk, 1.2, Qt::DashLine)); p.drawLine(QPointF(6, 26), QPointF(13, 19));
    } else if (n == "wireframe") {
        box3d(QRectF(6, 12, 14, 14), 7, false);
    } else if (n == "hiddenline") {
        p.setBrush(Qt::white); box3d(QRectF(6, 12, 14, 14), 7, false);
    } else if (n == "shaded" || n == "textured" || n == "mono") {
        box3d(QRectF(6, 12, 14, 14), 7, true);
        if (n == "textured") {
            p.setPen(QPen(QColor(170, 90, 60), 1.2));
            for (int y = 15; y < 26; y += 3) p.drawLine(QPointF(7, y), QPointF(19, y));
        }
        if (n == "mono") { p.setBrush(QColor(255, 255, 255, 160)); p.setPen(Qt::NoPen); p.drawRect(QRectF(6, 12, 14, 14)); }
    } else if (n == "backedges") {
        box3d(QRectF(6, 12, 14, 14), 7, true);
        p.setPen(QPen(kInk, 1.4, Qt::DashLine)); p.drawLine(QPointF(13, 5), QPointF(13, 19)); p.drawLine(QPointF(6, 26), QPointF(13, 19)); p.drawLine(QPointF(13, 19), QPointF(27, 19));
    } else if (n == "shadows") {
        p.setPen(Qt::NoPen); p.setBrush(QColor(0, 0, 0, 70));
        QPolygonF s; s << QPointF(12, 27) << QPointF(30, 27) << QPointF(24, 20) << QPointF(18, 20); p.drawPolygon(s);
        p.setPen(ink); box3d(QRectF(5, 15, 10, 12), 5, true);
        p.setBrush(QColor(250, 200, 40)); p.setPen(Qt::NoPen); p.drawEllipse(QPointF(25, 7), 4.5, 4.5);
    } else if (n == "group") {
        p.setPen(QPen(kAccent, 1.6, Qt::DashLine)); p.drawRect(QRectF(3, 3, 26, 26));
        p.setPen(ink); box3d(QRectF(8, 14, 10, 10), 5, true);
    } else if (n == "component") {
        p.setPen(QPen(kAccent, 1.6, Qt::DashLine)); p.drawRect(QRectF(3, 3, 26, 26));
        p.setPen(ink);
        box3d(QRectF(6, 15, 7, 8), 4, true);
        box3d(QRectF(16, 15, 7, 8), 4, true);
    } else if (n == "solid") {
        p.setBrush(kFill); p.drawEllipse(QPointF(12, 16), 8, 8); p.setBrush(QColor(230, 160, 80, 150)); p.drawEllipse(QPointF(20, 16), 8, 8);
    } else if (n == "materials") {
        p.setBrush(QColor(220, 80, 60)); p.drawRect(QRectF(4, 4, 11, 11));
        p.setBrush(QColor(80, 160, 70)); p.drawRect(QRectF(17, 4, 11, 11));
        p.setBrush(QColor(70, 110, 210)); p.drawRect(QRectF(4, 17, 11, 11));
        p.setBrush(QColor(240, 200, 60)); p.drawRect(QRectF(17, 17, 11, 11));
    } else if (n == "parallel" || n == "perspective") {
        p.setBrush(QColor(230, 232, 236));
        if (n == "parallel") { p.drawRect(QRectF(5, 9, 15, 15)); p.drawLine(QPointF(5, 9), QPointF(12, 3)); p.drawLine(QPointF(20, 9), QPointF(27, 3)); p.drawLine(QPointF(27, 3), QPointF(27, 18)); p.drawLine(QPointF(20, 24), QPointF(27, 18)); p.drawLine(QPointF(12, 3), QPointF(27, 3)); }
        else { QPolygonF f; f << QPointF(4, 8) << QPointF(18, 11) << QPointF(18, 22) << QPointF(4, 26); p.drawPolygon(f); QPolygonF s; s << QPointF(18, 11) << QPointF(28, 13) << QPointF(28, 20) << QPointF(18, 22); p.drawPolygon(s); }
    } else if (n == "sbscratch" || n == "sbcontours" || n == "smoove" || n == "flipedge" || n == "drape") {
        QPolygonF g;
        g << QPointF(3, 22) << QPointF(16, 28) << QPointF(29, 20) << QPointF(16, 14);
        p.setBrush(QColor(170, 200, 140));
        p.drawPolygon(g);
        p.setPen(QPen(kInk, 1.0));
        p.drawLine(QPointF(9.5, 25), QPointF(22.5, 17));
        p.drawLine(QPointF(9.5, 18), QPointF(22.5, 24));
        p.setPen(ink);
        if (n == "smoove") { p.setPen(QPen(kRed, 2.2)); p.drawLine(QPointF(16, 20), QPointF(16, 5)); p.drawLine(QPointF(16, 5), QPointF(12, 9)); p.drawLine(QPointF(16, 5), QPointF(20, 9)); }
        if (n == "sbcontours") { p.setPen(QPen(QColor(140, 90, 40), 1.6)); p.drawEllipse(QPointF(16, 10), 10, 4); p.drawEllipse(QPointF(16, 9), 5, 2); }
        if (n == "flipedge") { p.setPen(QPen(kRed, 2.0)); p.drawLine(QPointF(10, 13), QPointF(22, 5)); p.drawLine(QPointF(22, 5), QPointF(18, 5)); p.drawLine(QPointF(22, 5), QPointF(21, 9)); }
        if (n == "drape") { p.setPen(QPen(kRed, 2.0, Qt::DashLine)); p.drawLine(QPointF(8, 6), QPointF(24, 6)); p.drawLine(QPointF(16, 7), QPointF(16, 18)); }
    } else if (n == "previous") {
        p.setPen(QPen(kInk, 2.6, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(QPointF(24, 16), QPointF(8, 16)); p.drawLine(QPointF(8, 16), QPointF(14, 10)); p.drawLine(QPointF(8, 16), QPointF(14, 22));
    } else {
        p.drawRect(QRectF(6, 6, 20, 20));
    }
}

QIcon toolIcon(const QString& name) {
    static std::map<QString, QIcon> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return it->second;
    QIcon icon;
    for (int sz : {16, 24, 32, 48, 64}) {
        QPixmap pm(sz, sz);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(sz / 32.0, sz / 32.0);
        drawIcon(p, name);
        p.end();
        icon.addPixmap(pm);
    }
    cache[name] = icon;
    return icon;
}

QPixmap appIconPixmap(int size) {
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.scale(size / 64.0, size / 64.0);
    // rounded tile
    QLinearGradient g(0, 0, 0, 64);
    g.setColorAt(0, QColor(42, 131, 80));
    g.setColorAt(1, QColor(22, 82, 48));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawRoundedRect(QRectF(2, 2, 60, 60), 13, 13);
    // isometric gable house (matches resources/icons/gable.svg)
    auto poly = [](std::initializer_list<QPointF> pts) { return QPolygonF(QVector<QPointF>(pts)); };
    p.setPen(QPen(QColor(16, 32, 26), 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(QColor(233, 238, 233));
    p.drawPolygon(poly({{12.3, 42.9}, {34.8, 55.9}, {34.8, 39.7}, {12.3, 26.7}}));
    p.setBrush(QColor(185, 198, 189));
    p.drawPolygon(poly({{34.8, 55.9}, {51.7, 46.2}, {51.7, 29.9}, {43.3, 23.1}, {34.8, 39.7}}));
    p.setBrush(QColor(251, 253, 251));
    p.drawPolygon(poly({{12.3, 26.7}, {34.8, 39.7}, {43.3, 23.1}, {20.7, 10.1}}));
    return pm;
}

}  // namespace gable
