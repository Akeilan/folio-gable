// Folio Gable — all interactive tools.
#pragma once
#include "tools/Tool.h"
#include "ui/Document.h"
#include <QElapsedTimer>
#include <functional>
#include <map>
#include <memory>

namespace gable {

// Base for tools that pick 3D points with inference (shared Shift/arrow lock handling).
class InferTool : public Tool {
public:
    using Tool::Tool;
protected:
    InfResult cur;
    double lastX = 0, lastY = 0;
    void infer(const MouseEv& e);
    void reinfer();
    bool lockKeys(QKeyEvent* e);
    bool lockRelease(QKeyEvent* e);
    void drawInf(Overlay& ov) const;
    // Parses typed values: "a,b,c" or "a;b;c". Returns lengths in mm.
    bool parseList(const QString& s, std::vector<double>& out) const;
    bool parseCoords(const QString& s, Vec3& v, bool& relative) const;
    // Plane under the cursor for shape tools (face, locked axis or ground).
    Plane shapePlane(const InfResult& r, int lockedNormalAxis) const;
    Vec3 toLocal(const Vec3& w) const;
    Vec3 dirToLocal(const Vec3& w) const;
};

class SelectTool : public Tool {
public:
    explicit SelectTool(Viewport* v) : Tool(v) {}
    QString id() const override { return "select"; }
    QString title() const override { return "Select"; }
    void mousePress(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void doubleClick(const MouseEv& e) override;
    void draw(Overlay& ov) override;
    void reset() override { dragging_ = false; }
    QString status() const override { return "Select objects. Shift to extend select. Drag mouse to select multiple."; }
    QCursor cursor() const override { return Qt::ArrowCursor; }
private:
    bool pressed_ = false, dragging_ = false;
    double px_ = 0, py_ = 0, cx_ = 0, cy_ = 0;
    QElapsedTimer dblTimer_;
    EntRef dblRef_;
    std::vector<EntRef> expand(EntRef r) const;
    void applyClick(const std::vector<EntRef>& refs, Qt::KeyboardModifiers m);
};

class LineTool : public InferTool {
public:
    explicit LineTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "line"; }
    QString title() const override { return "Line"; }
    void activate() override { reset(); }
    void reset() override;
    void mousePress(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void doubleClick(const MouseEv&) override { reset(); }
    bool keyPress(QKeyEvent* e) override { return lockKeys(e); }
    bool keyRelease(QKeyEvent* e) override { return lockRelease(e); }
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return "Length"; }
    void draw(Overlay& ov) override;
    QString status() const override { return started_ ? "Select end point or enter value." : "Select start point."; }
    bool busy() const override { return started_; }
private:
    bool started_ = false;
    Vec3 start_, chainStart_;
    double pressX_ = 0, pressY_ = 0;
    bool pressedNoMove_ = false;
    void placeEnd(const Vec3& p);
};

class FreehandTool : public InferTool {
public:
    explicit FreehandTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "freehand"; }
    QString title() const override { return "Freehand"; }
    void reset() override { pts_.clear(); drawing_ = false; }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void draw(Overlay& ov) override;
    QString status() const override { return "Click and drag to draw a freehand curve."; }
private:
    std::vector<Vec3> pts_;
    bool drawing_ = false;
    Plane plane_;
};

class RectTool : public InferTool {
public:
    RectTool(Viewport* v, bool rotated) : InferTool(v), rotated_(rotated) {}
    QString id() const override { return rotated_ ? "rotrect" : "rect"; }
    QString title() const override { return rotated_ ? "Rotated Rectangle" : "Rectangle"; }
    void reset() override { stage_ = 0; lockAxis_ = -1; centered_ = false; inf().reset(); }
    void activate() override { reset(); }
    void mousePress(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    bool keyPress(QKeyEvent* e) override;
    bool keyRelease(QKeyEvent* e) override { return lockRelease(e); }
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return rotated_ && stage_ == 1 ? "Length, Angle" : (rotated_ && stage_ == 2 ? "Width, Angle" : "Dimensions"); }
    bool centered_ = false;
    void draw(Overlay& ov) override;
    QString status() const override;
    bool busy() const override { return stage_ > 0; }
private:
    bool rotated_;
    int stage_ = 0;
    int lockAxis_ = -1;
    Vec3 p0_, p1_, p2_;
    Plane plane_;
    Vec3 u_, v_;
    double pressX_ = 0, pressY_ = 0;
    std::vector<Vec3> corners() const;
    void finish();
    QString special_;
};

class CircleTool : public InferTool {
public:
    CircleTool(Viewport* v, bool polygon) : InferTool(v), polygon_(polygon), segs_(polygon ? 6 : 24) {}
    QString id() const override { return polygon_ ? "polygon" : "circle"; }
    QString title() const override { return polygon_ ? "Polygon" : "Circle"; }
    void reset() override { stage_ = 0; inf().reset(); lockAxis_ = -1; }
    void activate() override { reset(); setVCB(QString::number(segs_)); }
    void mousePress(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    bool keyPress(QKeyEvent* e) override;
    bool keyRelease(QKeyEvent* e) override { return lockRelease(e); }
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return stage_ == 0 ? "Sides" : "Radius"; }
    void draw(Overlay& ov) override;
    QString status() const override { return stage_ == 0 ? "Select center point." : "Select edge of circle or enter radius."; }
    bool busy() const override { return stage_ > 0; }
private:
    bool polygon_;
    int segs_;
    int stage_ = 0;
    int lockAxis_ = -1;
    Vec3 c_, r_;
    Plane plane_;
    double pressX_ = 0, pressY_ = 0;
    std::vector<Vec3> points(const Vec3& radiusPt) const;
    void finish(const Vec3& radiusPt);
};

class ArcTool : public InferTool {
public:
    enum Mode { TwoPoint, Center, ThreePoint, Pie };
    ArcTool(Viewport* v, Mode m) : InferTool(v), mode_(m) {}
    QString id() const override { return mode_ == TwoPoint ? "arc2" : (mode_ == Center ? "arc" : (mode_ == ThreePoint ? "arc3" : "pie")); }
    QString title() const override { return mode_ == TwoPoint ? "2 Point Arc" : (mode_ == Center ? "Arc" : (mode_ == ThreePoint ? "3 Point Arc" : "Pie")); }
    void reset() override { stage_ = 0; inf().reset(); }
    void activate() override { reset(); }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    bool keyPress(QKeyEvent* e) override { return lockKeys(e); }
    bool keyRelease(QKeyEvent* e) override { return lockRelease(e); }
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override;
    void draw(Overlay& ov) override;
    QString status() const override;
    bool busy() const override { return stage_ > 0; }
private:
    Mode mode_;
    int stage_ = 0;
    int segs_ = 12;
    Vec3 a_, b_, c_;
    Plane plane_;
    bool halfCircle_ = false;
    // computes arc polyline from current state + candidate point
    bool arcPoints(const Vec3& p, std::vector<Vec3>& pts, Curve& cv, double* bulge = nullptr) const;
    void finish(const Vec3& p);
    Vec3 planePoint(const InfResult& r) const;
};

class PushPullTool : public InferTool {
public:
    explicit PushPullTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "pushpull"; }
    QString title() const override { return "Push/Pull"; }
    void reset() override { active_ = false; face_ = -1; }
    void mousePress(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void doubleClick(const MouseEv& e) override;
    bool keyPress(QKeyEvent* e) override;
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return "Distance"; }
    void draw(Overlay& ov) override;
    QString status() const override { return active_ ? "Select distance to push or pull. Ctrl = create new starting face." : "Pick a face to push or pull."; }
    bool busy() const override { return active_; }
private:
    bool active_ = false;
    bool copyMode_ = false;
    int face_ = -1;
    int hoverFace_ = -1;
    Vec3 startW_, normalW_;
    double dist_ = 0;
    double lastDist_ = 0;
    bool haveLast_ = false;
    int lastResultFace_ = -1;
    double pressX_ = 0, pressY_ = 0;
    void commit(double d);
    int faceUnderCursor(const MouseEv& e, Vec3* hit = nullptr) const;
};

// Shared machinery for Move/Rotate/Scale: live preview of a transform on the selection.
class TransformTool : public InferTool {
public:
    using InferTool::InferTool;
protected:
    std::vector<EntRef> targets_;
    std::vector<int> verts_;
    std::vector<Vec3> origPos_;
    std::vector<std::pair<int, Mat4>> origInst_;
    std::vector<std::pair<int, Vec3>> origGuide_;
    bool preview_ = false;
    bool autoSelected_ = false;
    bool copy_ = false;
    Mat4 lastXf_;
    bool haveLastXf_ = false;
    std::vector<EntRef> lastCopies_;
    bool pickTargets(const MouseEv& e);
    void beginPreview();
    void applyPreview(const Mat4& worldXf);
    void endPreview();         // restore originals
    void commitTransform(const Mat4& worldXf, const QString& name);
    void drawCopyGhost(Overlay& ov, const Mat4& worldXf) const;
    Mat4 toLocalXf(const Mat4& worldXf) const;
};

class MoveTool : public TransformTool {
public:
    explicit MoveTool(Viewport* v) : TransformTool(v) {}
    QString id() const override { return "move"; }
    QString title() const override { return "Move"; }
    void activate() override { reset(); }
    void deactivate() override { if (stage_) { endPreview(); } stage_ = 0; }
    void reset() override;
    void mousePress(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    bool keyPress(QKeyEvent* e) override;
    bool keyRelease(QKeyEvent* e) override { return lockRelease(e); }
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return "Distance"; }
    void draw(Overlay& ov) override;
    QString status() const override { return stage_ ? "Select destination. Ctrl = toggle copy. Arrow keys lock direction." : "Select objects to move, or pick a point to move from."; }
    QCursor cursor() const override { return Qt::SizeAllCursor; }
    bool busy() const override { return stage_ > 0; }
private:
    int stage_ = 0;
    Vec3 from_;
    double pressX_ = 0, pressY_ = 0;
    Vec3 lastDelta_;
    void finish(const Vec3& to);
};

class RotateTool : public TransformTool {
public:
    explicit RotateTool(Viewport* v) : TransformTool(v) {}
    QString id() const override { return "rotate"; }
    QString title() const override { return "Rotate"; }
    void activate() override { reset(); }
    void deactivate() override { if (stage_ == 2) endPreview(); stage_ = 0; }
    void reset() override;
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    bool keyPress(QKeyEvent* e) override;
    bool keyRelease(QKeyEvent* e) override { return lockRelease(e); }
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return "Angle"; }
    void draw(Overlay& ov) override;
    QString status() const override;
    bool busy() const override { return stage_ > 0; }
private:
    int stage_ = 0;
    int lockAxis_ = -1;
    Vec3 center_, ref_, normal_;
    double angle_ = 0;
    double lastAngle_ = 0;
    Vec3 planeNormalAt(const InfResult& r) const;
    double angleFor(const Vec3& p) const;
    void finish(double ang);
};

class ScaleTool : public TransformTool {
public:
    explicit ScaleTool(Viewport* v) : TransformTool(v) {}
    QString id() const override { return "scale"; }
    QString title() const override { return "Scale"; }
    void activate() override;
    void deactivate() override { if (dragging_) endPreview(); dragging_ = false; }
    void reset() override;
    void mousePress(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return "Scale"; }
    void draw(Overlay& ov) override;
    QString status() const override { return "Select a grip to scale. Shift = uniform toggle, Ctrl = about center."; }
    bool busy() const override { return dragging_; }
private:
    BBox box_;        // world, axis aligned in context frame
    Mat4 frame_;      // context -> world
    int hoverGrip_ = -1, grip_ = -1;
    bool dragging_ = false;
    Vec3 scale_{1, 1, 1};
    Vec3 gripPos(int g) const;
    int gripAxes(int g, int out[3]) const;
    void refreshBox();
    Mat4 scaleXf(const Vec3& s, bool center) const;
    bool ctr_ = false, uniformToggle_ = false;
    double pressX_ = 0, pressY_ = 0;
};

class OffsetTool : public InferTool {
public:
    explicit OffsetTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "offset"; }
    QString title() const override { return "Offset"; }
    void reset() override { active_ = false; }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void doubleClick(const MouseEv& e) override;
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return "Distance"; }
    void draw(Overlay& ov) override;
    QString status() const override { return "Pick a face to offset, then drag to set distance."; }
    bool busy() const override { return active_; }
private:
    bool active_ = false;
    int face_ = -1;
    double startD_ = 0, dist_ = 0, last_ = 0;
    bool haveLast_ = false;
    double pressX_ = 0, pressY_ = 0;
    double boundaryDist(const Vec3& localP) const;
    void commit(double d);
};

class FollowMeTool : public InferTool {
public:
    explicit FollowMeTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "followme"; }
    QString title() const override { return "Follow Me"; }
    void reset() override { face_ = -1; path_.clear(); }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void draw(Overlay& ov) override;
    QString status() const override { return "Select a path first, then click the profile face. Or click a face and drag along edges. Alt = use face perimeter."; }
    bool busy() const override { return face_ >= 0; }
private:
    int face_ = -1;
    std::vector<int> path_;  // vertex chain
    bool buildPath(int toEdge);
};

class EraserTool : public Tool {
public:
    explicit EraserTool(Viewport* v) : Tool(v) {}
    QString id() const override { return "eraser"; }
    QString title() const override { return "Eraser"; }
    void reset() override { marked_.clear(); down_ = false; }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void draw(Overlay& ov) override;
    QString status() const override { return "Select objects to erase, or drag over them. Shift = hide, Ctrl = soften/smooth, Ctrl+Shift = unsoften."; }
    QCursor cursor() const override { return Qt::PointingHandCursor; }
private:
    bool down_ = false;
    std::set<EntRef> marked_;
    Qt::KeyboardModifiers mods_;
    void markAt(double x, double y);
};

class PaintTool : public Tool {
public:
    explicit PaintTool(Viewport* v) : Tool(v) {}
    QString id() const override { return "paint"; }
    QString title() const override { return "Paint Bucket"; }
    void mousePress(const MouseEv& e) override;
    QString status() const override { return "Select object to paint. Ctrl = fill connected, Shift = replace all, Alt = sample."; }
    QCursor cursor() const override { return Qt::PointingHandCursor; }
    std::function<int()> currentMaterial;
    std::function<void(int)> sampled;
};

class TapeTool : public InferTool {
public:
    explicit TapeTool(Viewport* v, bool protractor) : InferTool(v), protractor_(protractor) {}
    QString id() const override { return protractor_ ? "protractor" : "tape"; }
    QString title() const override { return protractor_ ? "Protractor" : "Tape Measure"; }
    void reset() override { stage_ = 0; inf().reset(); }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    bool keyPress(QKeyEvent* e) override;
    bool keyRelease(QKeyEvent* e) override { return lockRelease(e); }
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return protractor_ ? "Angle" : "Length"; }
    void draw(Overlay& ov) override;
    QString status() const override;
    bool busy() const override { return stage_ > 0; }
    std::function<bool(double)> confirmResize;
private:
    bool protractor_;
    int stage_ = 0;
    bool createGuides_ = true;
    Vec3 a_, b_, normal_;
    bool fromEdge_ = false;
    Vec3 edgeDir_;
    double lastMeasure_ = 0;
    bool haveMeasure_ = false;
};

class DimensionTool : public InferTool {
public:
    explicit DimensionTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "dimension"; }
    QString title() const override { return "Dimension"; }
    void reset() override { stage_ = 0; inf().reset(); }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void draw(Overlay& ov) override;
    QString status() const override { return stage_ == 0 ? "Select start point or edge." : (stage_ == 1 ? "Select end point." : "Drag to place dimension."); }
    bool busy() const override { return stage_ > 0; }
private:
    int stage_ = 0;
    bool radial_ = false;
    Vec3 a_, b_, off_;
    void finish();
};

class TextTool : public InferTool {
public:
    explicit TextTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "text"; }
    QString title() const override { return "Text"; }
    void reset() override { stage_ = 0; }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void draw(Overlay& ov) override;
    QString status() const override { return "Select entity to attach a leader, or empty space for screen text."; }
    std::function<bool(QString&)> editText;
    bool busy() const override { return stage_ > 0; }
private:
    int stage_ = 0;
    Vec3 anchor_;
    QString text_;
};

class AxesTool : public InferTool {
public:
    explicit AxesTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "axes"; }
    QString title() const override { return "Axes"; }
    void reset() override { stage_ = 0; inf().reset(); }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    bool keyPress(QKeyEvent* e) override { return lockKeys(e); }
    bool keyRelease(QKeyEvent* e) override { return lockRelease(e); }
    void draw(Overlay& ov) override;
    QString status() const override { return stage_ == 0 ? "Select origin." : (stage_ == 1 ? "Select red axis direction." : "Select green axis direction."); }
private:
    int stage_ = 0;
    Vec3 o_, x_;
};

class SectionTool : public InferTool {
public:
    explicit SectionTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "section"; }
    QString title() const override { return "Section Plane"; }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    bool keyPress(QKeyEvent* e) override;
    void draw(Overlay& ov) override;
    QString status() const override { return "Place section plane on a face. Arrow keys lock orientation."; }
private:
    int lockAxis_ = -1;
    Vec3 n_;
    bool valid_ = false;
};

class Text3DPlaceTool : public InferTool {
public:
    explicit Text3DPlaceTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "place"; }
    QString title() const override { return "Place Component"; }
    int def = -1;
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void draw(Overlay& ov) override;
    QString status() const override { return "Click to place the component."; }
};

// Places clipboard contents at the cursor.
class PasteTool : public InferTool {
public:
    explicit PasteTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "paste"; }
    QString title() const override { return "Paste"; }
    std::shared_ptr<Model> clip;
    Vec3 ref;   // world reference point of the clipboard contents
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override { infer(e); }
    void draw(Overlay& ov) override;
    QString status() const override { return "Click to place the pasted entities."; }
};

// Camera tools
class OrbitTool : public Tool {
public:
    explicit OrbitTool(Viewport* v) : Tool(v) {}
    QString id() const override { return "orbit"; }
    QString title() const override { return "Orbit"; }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void mouseRelease(const MouseEv&) override;
    QString status() const override { return "Drag to orbit about the point under the cursor. Shift = pan, Ctrl = orbit without gravity."; }
    QCursor cursor() const override { return Qt::OpenHandCursor; }
private:
    bool down_ = false;
    double lx_ = 0, ly_ = 0;
    Vec3 pivot_;
};

class PanTool : public Tool {
public:
    explicit PanTool(Viewport* v) : Tool(v) {}
    QString id() const override { return "pan"; }
    QString title() const override { return "Pan"; }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void mouseRelease(const MouseEv&) override { down_ = false; }
    QString status() const override { return "Drag to pan."; }
    QCursor cursor() const override { return Qt::SizeAllCursor; }
private:
    bool down_ = false;
    double lx_ = 0, ly_ = 0;
    Vec3 pivot_;
};

class ZoomTool : public Tool {
public:
    explicit ZoomTool(Viewport* v) : Tool(v) {}
    QString id() const override { return "zoom"; }
    QString title() const override { return "Zoom"; }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void mouseRelease(const MouseEv&) override { down_ = false; }
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return "Field of View"; }
    QString status() const override { return "Drag up/down to zoom. Shift = change field of view."; }
    QCursor cursor() const override { return Qt::SizeVerCursor; }
private:
    bool down_ = false;
    double ly_ = 0, px_ = 0, py_ = 0;
    Vec3 pivot_;
};

class ZoomWindowTool : public Tool {
public:
    explicit ZoomWindowTool(Viewport* v) : Tool(v) {}
    QString id() const override { return "zoomwindow"; }
    QString title() const override { return "Zoom Window"; }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void draw(Overlay& ov) override;
    QString status() const override { return "Drag a window to zoom into."; }
private:
    bool down_ = false;
    double x0_ = 0, y0_ = 0, x1_ = 0, y1_ = 0;
};

class WalkTool : public Tool {
public:
    WalkTool(Viewport* v, bool lookAround) : Tool(v), look_(lookAround) {}
    QString id() const override { return look_ ? "lookaround" : "walk"; }
    QString title() const override { return look_ ? "Look Around" : "Walk"; }
    void activate() override;
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void mouseRelease(const MouseEv&) override { down_ = false; }
    bool keyPress(QKeyEvent* e) override;
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return look_ ? "Eye Height" : "Eye Height"; }
    QString status() const override { return look_ ? "Drag to look around. Type an eye height." : "Drag to walk (up/down = forward/back, left/right = turn). Shift = vertical, Ctrl = run."; }
    QCursor cursor() const override { return Qt::CrossCursor; }
private:
    bool look_;
    bool down_ = false;
    double px_ = 0, py_ = 0, lx_ = 0, ly_ = 0;
};

class PositionCameraTool : public InferTool {
public:
    explicit PositionCameraTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "poscam"; }
    QString title() const override { return "Position Camera"; }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return "Eye Height"; }
    void draw(Overlay& ov) override;
    QString status() const override { return "Click to place the camera at eye height, or drag from eye position toward target."; }
private:
    bool down_ = false;
    Vec3 p0_;
    double eyeH_ = 1676;
};

}  // namespace gable

namespace gable {

// ---- Sandbox (terrain) tools
class SandboxScratchTool : public InferTool {
public:
    explicit SandboxScratchTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "sbscratch"; }
    QString title() const override { return "From Scratch"; }
    void reset() override { stage_ = 0; inf().reset(); }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override { infer(e); }
    bool keyPress(QKeyEvent* e) override { return lockKeys(e); }
    bool keyRelease(QKeyEvent* e) override { return lockRelease(e); }
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return stage_ == 0 ? "Grid Spacing" : "Length"; }
    void draw(Overlay& ov) override;
    QString status() const override { return stage_ == 0 ? "Click the grid origin. Type a grid spacing first if needed." : (stage_ == 1 ? "Click to set the grid length." : "Click to set the grid width."); }
    bool busy() const override { return stage_ > 0; }
private:
    int stage_ = 0;
    double cell_ = 1000;
    Vec3 o_, x_;
    void finish(const Vec3& p);
};

class SmooveTool : public InferTool {
public:
    explicit SmooveTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "smoove"; }
    QString title() const override { return "Smoove"; }
    void reset() override { active_ = false; }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void mouseRelease(const MouseEv& e) override;
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return active_ ? "Offset" : "Radius"; }
    void draw(Overlay& ov) override;
    QString status() const override { return "Click a terrain surface and move up or down. Type a radius before clicking."; }
    bool busy() const override { return active_; }
private:
    bool active_ = false;
    double radius_ = 3000, dz_ = 0;
    double pressX_ = 0, pressY_ = 0;
    InstPath path_;
    Mat4 xf_;
    Vec3 centerW_, hoverW_;
    bool hoverOk_ = false;
    void apply(double dz);
};

class FlipEdgeTool : public Tool {
public:
    explicit FlipEdgeTool(Viewport* v) : Tool(v) {}
    QString id() const override { return "flipedge"; }
    QString title() const override { return "Flip Edge"; }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    QString status() const override { return "Click the diagonal edge between two triangles to flip it."; }
    QCursor cursor() const override { return Qt::PointingHandCursor; }
};

class DrapeTool : public Tool {
public:
    explicit DrapeTool(Viewport* v) : Tool(v) {}
    QString id() const override { return "drape"; }
    QString title() const override { return "Drape"; }
    void mousePress(const MouseEv& e) override;
    QString status() const override { return "Select edges first, then click the terrain to drape them onto it."; }
    QCursor cursor() const override { return Qt::PointingHandCursor; }
};


// Places an imported picture as a textured rectangle (in a group), sized with the mouse
// or by typing its width; it stays proportional to the picture.
class ImageTool : public InferTool {
public:
    explicit ImageTool(Viewport* v) : InferTool(v) {}
    QString id() const override { return "image"; }
    QString title() const override { return "Place Image"; }
    bool setImage(const QString& path, QString* err);
    void reset() override { stage_ = 0; inf().reset(); }
    void mousePress(const MouseEv& e) override;
    void mouseMove(const MouseEv& e) override;
    void vcbEnter(const QString& s) override;
    QString vcbLabel() const override { return "Width"; }
    void draw(Overlay& ov) override;
    QString status() const override {
        return stage_ == 0 ? "Click to place the corner of the image." : "Click or type a width to size the image.";
    }
    bool busy() const override { return stage_ > 0; }
private:
    QString name_;
    QByteArray png_;
    double aspect_ = 1;     // width / height
    int stage_ = 0;
    Vec3 o_, x_, y_, n_;
    double width_ = 0;
    void place(double width);
    void frameFrom(const Vec3& p);
};

}  // namespace gable
