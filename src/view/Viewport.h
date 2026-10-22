#pragma once
#include "tools/Inference.h"
#include "ui/Document.h"
#include "view/Camera.h"
#include "view/Overlay.h"
#include "view/Picker.h"
#include "view/Renderer.h"
#include <QElapsedTimer>
#include <QOpenGLWidget>
#include <QTimer>
#include <map>
#include <memory>

namespace gable {

class Tool;

class Viewport : public QOpenGLWidget {
    Q_OBJECT
public:
    explicit Viewport(Document* doc, QWidget* parent = nullptr);
    ~Viewport() override;

    Document* doc;
    Camera cam;
    Picker picker;
    Inference inference;
    Overlay overlay;
    double mouseX = 0, mouseY = 0;

    void registerTool(Tool* t);
    void setTool(const QString& id);
    Tool* tool() const { return tool_; }
    Tool* toolById(const QString& id) const;
    QString previousToolId() const { return prevTool_; }
    void returnToPreviousTool();

    void setVCB(const QString& v);
    void setStatus(const QString& s);
    void refreshToolUI();

    // Camera helpers
    void zoomExtents();
    void zoomSelection();
    void standardView(int which);  // 0 top 1 bottom 2 front 3 back 4 left 5 right 6 iso
    void setPerspective(bool p);
    void animateTo(const CameraState& target, int ms = 600);
    void stopAnimation() { animTimer_.stop(); }
    void previousView();
    void pushViewHistory();

    QImage renderImage(int w, int h, bool transparent);
    Vec3 orbitPivot(double x, double y);   // what is under the cursor, else emptyPivot()
    Vec3 emptyPivot();                      // pivot when the cursor is over empty space
    void beginOrbit(const Vec3& pivot);
    void endOrbit();

    std::vector<EntRef> hover;
    bool hoverInstance = false;

signals:
    void vcbChanged(const QString& label, const QString& value);
    void statusChanged(const QString& s);
    void toolChanged(const QString& id);
    void contextMenuRequested(const QPoint& globalPos);
    void cameraChanged();

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void enterEvent(QEnterEvent* e) override;

private:
    Renderer renderer_;
    bool glOk_ = false;
    QString glError_;
    std::map<QString, Tool*> tools_;
    Tool* tool_ = nullptr;
    QString prevTool_;
    // navigation state
    enum class Nav { None, Orbit, Pan };
    Nav nav_ = Nav::None;
    Vec3 navPivot_;
    bool orbitPivotShown_ = false;
    Vec3 orbitPivotPt_;
    double lastX_ = 0, lastY_ = 0;
    QElapsedTimer middleClickTimer_;
    double middlePressX_ = 0, middlePressY_ = 0;
    // animation
    QTimer animTimer_;
    CameraState animFrom_, animTo_;
    QElapsedTimer animClock_;
    int animMs_ = 600;
    std::vector<CameraState> viewHistory_;
    void paintOverlay2D();
    void updateHover();
};

}  // namespace gable
