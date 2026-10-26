#pragma once
#include "math/Vec.h"
#include <QColor>
#include <QString>
#include <vector>

namespace gable {

// Temporary drawing produced by tools each frame (world coordinates).
struct Overlay {
    struct Line { Vec3 a, b; QColor c; float width = 1; int dash = 0; bool onTop = true; };
    struct Poly { std::vector<Vec3> pts; std::vector<std::vector<Vec3>> holes; QColor c; };
    enum MarkerShape { Circle, Square, Diamond, Cross, FilledCircle, Triangle };
    struct Marker { Vec3 p; QColor c; MarkerShape shape = Circle; int size = 10; };
    struct Label { Vec3 p; QString text; QColor c{0, 0, 0}; bool box = true; int dx = 0, dy = 0; };
    struct Box2D { double x0, y0, x1, y1; bool dashed; QColor c; };
    // screen-space lines and grips (device-independent pixels), for tools working on a picture
    struct Line2D { double x0, y0, x1, y1; QColor c; float w = 1; bool dashed = false; };
    struct Grip2D { double x, y; QColor c; int size = 9; bool hot = false; };
    std::vector<Line2D> lines2d;
    std::vector<Grip2D> grips2d;
    std::vector<Line> lines;
    std::vector<Poly> polys;
    std::vector<Marker> markers;
    std::vector<Label> labels;
    std::vector<Box2D> boxes;
    QString tooltip;      // inference tooltip near cursor
    double tipX = 0, tipY = 0;

    void clear() { lines.clear(); polys.clear(); markers.clear(); labels.clear(); boxes.clear(); lines2d.clear(); grips2d.clear(); tooltip.clear(); }
    void line(const Vec3& a, const Vec3& b, const QColor& c, float w = 1, int dash = 0, bool onTop = true) {
        lines.push_back({a, b, c, w, dash, onTop});
    }
    void marker(const Vec3& p, const QColor& c, MarkerShape s = Circle, int size = 10) { markers.push_back({p, c, s, size}); }
    void label(const Vec3& p, const QString& t, const QColor& c = QColor(0, 0, 0), bool box = true) { labels.push_back({p, t, c, box}); }
};

// Standard SketchUp-like inference colors.
namespace icolor {
inline QColor red() { return QColor(220, 30, 30); }
inline QColor green() { return QColor(20, 170, 30); }
inline QColor blue() { return QColor(30, 60, 230); }
inline QColor magenta() { return QColor(220, 0, 220); }
inline QColor endpoint() { return QColor(20, 170, 30); }
inline QColor midpoint() { return QColor(0, 200, 220); }
inline QColor onEdge() { return QColor(220, 30, 30); }
inline QColor onFace() { return QColor(30, 60, 230); }
inline QColor black() { return QColor(0, 0, 0); }
inline QColor selection() { return QColor(10, 80, 240); }
}  // namespace icolor

}  // namespace gable
