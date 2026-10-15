#include "tools/Tool.h"
#include "view/Viewport.h"

namespace gable {

Document* Tool::doc() const { return view->doc; }
Inference& Tool::inf() const { return view->inference; }
void Tool::setVCB(const QString& value) const { view->setVCB(value); }
void Tool::setStatus(const QString& s) const { view->setStatus(s); }
void Tool::redraw() const { view->update(); }
void Tool::toSelectTool() const { view->setTool("select"); }
double Tool::pixelTol(const Vec3& p, double px) const { return view->cam.pixelSize(p) * px; }

}  // namespace gable
