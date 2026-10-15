#pragma once
#include "tools/Inference.h"
#include "view/Overlay.h"
#include <QCursor>
#include <QKeyEvent>
#include <QString>

namespace gable {

class Viewport;
class Document;

struct MouseEv {
    double x = 0, y = 0;
    Qt::MouseButton button = Qt::NoButton;
    Qt::MouseButtons buttons;
    Qt::KeyboardModifiers mods;
    bool shift() const { return mods & Qt::ShiftModifier; }
    bool ctrl() const { return mods & Qt::ControlModifier; }
    bool alt() const { return mods & Qt::AltModifier; }
};

class Tool {
public:
    explicit Tool(Viewport* v) : view(v) {}
    virtual ~Tool() = default;
    virtual QString id() const = 0;
    virtual QString title() const = 0;
    virtual void activate() {}
    virtual void deactivate() {}
    virtual void reset() {}          // Esc
    virtual void mousePress(const MouseEv&) {}
    virtual void mouseRelease(const MouseEv&) {}
    virtual void mouseMove(const MouseEv&) {}
    virtual void doubleClick(const MouseEv&) {}
    virtual bool keyPress(QKeyEvent*) { return false; }
    virtual bool keyRelease(QKeyEvent*) { return false; }
    virtual void vcbEnter(const QString&) {}
    virtual QString vcbLabel() const { return "Measurements"; }
    virtual void draw(Overlay&) {}
    virtual QString status() const { return QString(); }
    virtual QCursor cursor() const { return Qt::CrossCursor; }
    virtual bool wantsContextMenu() const { return false; }
    // True while the tool is in the middle of an operation (camera tools return to it).
    virtual bool busy() const { return false; }

    Viewport* view;
    Document* doc() const;
    Inference& inf() const;
    void setVCB(const QString& value) const;
    void setStatus(const QString& s) const;
    void redraw() const;
    void toSelectTool() const;
    double pixelTol(const Vec3& p, double px = 1) const;
};

}  // namespace gable
