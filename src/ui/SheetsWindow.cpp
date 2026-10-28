// Drawing sheets (G2-03): scenes on paper with dimensions, labels and a title block.
#include "ui/SheetsWindow.h"
#include "view/Viewport.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPrintDialog>
#include <QPrinter>
#include <QPushButton>
#include <QSettings>
#include <QSplitter>
#include <QStackedWidget>
#include <QToolBar>
#include <QVBoxLayout>
#include <cmath>

namespace gable {

static const double kScales[] = {1, 2, 5, 10, 20, 25, 50, 100, 200, 250, 500, 1000, 2500, 5000};
static const char* kPapers[] = {"A4", "A3", "A2", "A1", "A0", "Letter", "Legal", "Tabloid", "ArchC", "ArchD"};
static const double kTitleW = 170, kTitleH = 40;   // title block, mm

QString scaleText(double scale) {
    if (scale <= 0) return QString();
    if (scale >= 1) return QString("1:%1").arg(scale, 0, 'g', 6);
    return QString("%1:1").arg(1.0 / scale, 0, 'g', 6);
}

static QRectF titleRect(const Sheet& s) {
    QSizeF pg = paperSizeMm(s.paper, s.landscape);
    double w = std::min(kTitleW, pg.width() - 2 * s.margin);
    return QRectF(pg.width() - s.margin - w, pg.height() - s.margin - kTitleH, w, kTitleH);
}

QString dimensionText(const Sheet& s, const SheetDimension& d, const Model& m) {
    if (!d.text.isEmpty()) return d.text;
    double paper = std::hypot(d.b.x() - d.a.x(), d.b.y() - d.a.y());
    if (d.viewport >= 0 && d.viewport < (int)s.viewports.size()) {
        const SheetViewport& v = s.viewports[d.viewport];
        double sc = v.scale;
        if (sc <= 0 && v.scene >= 0 && v.scene < (int)m.scenes.size() && !m.scenes[v.scene].cam.perspective && v.rect.height() > 0)
            sc = m.scenes[v.scene].cam.orthoHeight / v.rect.height();
        bool persp = v.scene >= 0 && v.scene < (int)m.scenes.size() ? m.scenes[v.scene].cam.perspective : m.camera.perspective;
        if (sc > 0 && !persp) return formatLength(paper * sc, m.units);
    }
    return QString("%1 mm").arg(paper, 0, 'f', 1);
}

static double sheetScaleOf(const Sheet& s, const Model& m) {
    for (auto& v : s.viewports) {
        if (v.scale > 0) return v.scale;
        if (v.scene >= 0 && v.scene < (int)m.scenes.size() && !m.scenes[v.scene].cam.perspective && v.rect.height() > 0)
            return m.scenes[v.scene].cam.orthoHeight / v.rect.height();
    }
    return 0;
}

void paintSheet(QPainter& p, const Sheet& s, const Model& m, double k, const std::function<QImage(int, QSize)>& viewImage, const SheetSel* sel) {
    QSizeF pg = paperSizeMm(s.paper, s.landscape);
    auto R = [k](const QRectF& r) { return QRectF(r.x() * k, r.y() * k, r.width() * k, r.height() * k); };
    auto P = [k](const QPointF& q) { return QPointF(q.x() * k, q.y() * k); };
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.fillRect(QRectF(0, 0, pg.width() * k, pg.height() * k), Qt::white);
    // the drawing border
    p.setPen(QPen(Qt::black, std::max(1.0, 0.5 * k)));
    p.setBrush(Qt::NoBrush);
    p.drawRect(R(QRectF(s.margin, s.margin, pg.width() - 2 * s.margin, pg.height() - 2 * s.margin)));
    QColor selCol(0x5b, 0x3c, 0x8a);
    // viewports
    for (int i = 0; i < (int)s.viewports.size(); ++i) {
        const SheetViewport& v = s.viewports[i];
        QRectF r = R(v.rect);
        QSize px(std::max(8, (int)std::lround(r.width())), std::max(8, (int)std::lround(r.height())));
        QImage img = viewImage ? viewImage(i, px) : QImage();
        if (!img.isNull()) p.drawImage(r, img);
        else p.fillRect(r, QColor(235, 235, 240));
        if (v.frame) {
            p.setPen(QPen(QColor(90, 90, 90), std::max(1.0, 0.25 * k)));
            p.drawRect(r);
        }
        if (sel && sel->kind == SheetSel::View && sel->index == i) {
            p.setPen(QPen(selCol, 2, Qt::DashLine));
            p.drawRect(r.adjusted(-2, -2, 2, 2));
            p.setBrush(selCol);
            for (QPointF c : {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()}) p.drawRect(QRectF(c - QPointF(4, 4), QSizeF(8, 8)));
            p.setBrush(Qt::NoBrush);
        }
    }
    // dimensions
    QFont f("Sans");
    for (int i = 0; i < (int)s.dims.size(); ++i) {
        const SheetDimension& d = s.dims[i];
        QPointF a = d.a, b = d.b;
        QPointF dir = b - a;
        double len = std::hypot(dir.x(), dir.y());
        if (len < 1e-6) continue;
        QPointF u = dir / len, n(-u.y(), u.x());
        QPointF a2 = a + n * d.offset, b2 = b + n * d.offset;
        bool on = sel && sel->kind == SheetSel::Dim && sel->index == i;
        QPen pen(on ? selCol : Qt::black, std::max(1.0, 0.18 * k));
        p.setPen(pen);
        double ext = d.offset >= 0 ? 2 : -2;
        p.drawLine(P(a + n * (d.offset >= 0 ? 1 : -1)), P(a2 + n * ext));
        p.drawLine(P(b + n * (d.offset >= 0 ? 1 : -1)), P(b2 + n * ext));
        p.drawLine(P(a2), P(b2));
        // architectural ticks
        for (QPointF c : {a2, b2}) p.drawLine(P(c - (u + n) * 1.2), P(c + (u + n) * 1.2));
        f.setPixelSize(std::max(6, (int)std::lround(2.5 * k)));
        p.setFont(f);
        QString t = dimensionText(s, d, m);
        QPointF mid = P((a2 + b2) / 2 + n * (d.offset >= 0 ? 1.2 : -1.2));
        double ang = std::atan2(u.y(), u.x()) * 180 / M_PI;
        if (ang > 90.01 || ang < -89.99) ang += 180;
        p.save();
        p.translate(mid);
        p.rotate(ang);
        QRectF tr = p.fontMetrics().boundingRect(t);
        p.fillRect(QRectF(-tr.width() / 2 - 2, -tr.height() * 0.9, tr.width() + 4, tr.height()), Qt::white);
        p.drawText(QPointF(-tr.width() / 2, -p.fontMetrics().descent()), t);
        p.restore();
    }
    // labels
    for (int i = 0; i < (int)s.labels.size(); ++i) {
        const SheetLabel& l = s.labels[i];
        bool on = sel && sel->kind == SheetSel::Label && sel->index == i;
        f.setPixelSize(std::max(6, (int)std::lround(l.size * k)));
        p.setFont(f);
        QFontMetricsF fm(f);
        QStringList lines = l.text.split('\n');
        double w = 0;
        for (auto& ln : lines) w = std::max(w, fm.horizontalAdvance(ln));
        QRectF box(P(l.at), QSizeF(w, fm.lineSpacing() * lines.size()));
        if (l.leader) {
            p.setPen(QPen(on ? selCol : Qt::black, std::max(1.0, 0.18 * k)));
            QPointF from = box.left() > P(l.target).x() ? QPointF(box.left() - 2, box.top() + fm.ascent() / 2)
                                                       : QPointF(box.right() + 2, box.top() + fm.ascent() / 2);
            p.drawLine(from, P(l.target));
            p.setBrush(on ? selCol : Qt::black);
            p.drawEllipse(P(l.target), 0.6 * k, 0.6 * k);
            p.setBrush(Qt::NoBrush);
        }
        p.setPen(on ? selCol : Qt::black);
        for (int k2 = 0; k2 < lines.size(); ++k2) p.drawText(QPointF(box.left(), box.top() + fm.ascent() + k2 * fm.lineSpacing()), lines[k2]);
        if (on) {
            p.setPen(QPen(selCol, 1, Qt::DashLine));
            p.drawRect(box.adjusted(-2, -2, 2, 2));
        }
    }
    // the title block
    if (s.title.show) {
        QRectF t = titleRect(s);
        bool on = sel && sel->kind == SheetSel::Title;
        p.setPen(QPen(on ? selCol : Qt::black, std::max(1.0, 0.35 * k)));
        p.fillRect(R(t), Qt::white);
        p.drawRect(R(t));
        double rowH = t.height() / 4;
        double x1 = t.left() + t.width() * 0.62;
        p.setPen(QPen(Qt::black, std::max(1.0, 0.18 * k)));
        for (int r = 1; r < 4; ++r) p.drawLine(P(QPointF(r == 1 ? t.left() : t.left(), t.top() + r * rowH)), P(QPointF(t.right(), t.top() + r * rowH)));
        p.drawLine(P(QPointF(x1, t.top() + 2 * rowH)), P(QPointF(x1, t.bottom())));
        auto cell = [&](const QRectF& c, const QString& cap, const QString& val, double size) {
            QFont cf("Sans");
            cf.setPixelSize(std::max(5, (int)std::lround(1.8 * k)));
            p.setFont(cf);
            p.setPen(QColor(90, 90, 90));
            p.drawText(P(c.topLeft() + QPointF(1.2, 2.4)), cap);
            QFont vf("Sans");
            vf.setPixelSize(std::max(6, (int)std::lround(size * k)));
            p.setFont(vf);
            p.setPen(Qt::black);
            QRectF vr = R(c.adjusted(1.2, 2.6, -1, -0.6));
            p.drawText(vr, Qt::AlignLeft | Qt::AlignVCenter, QFontMetrics(vf).elidedText(val, Qt::ElideRight, (int)vr.width()));
        };
        QString scale = s.title.scale.isEmpty() ? scaleText(sheetScaleOf(s, m)) : s.title.scale;
        cell(QRectF(t.left(), t.top(), t.width(), rowH), "PROJECT", s.title.project, 3.0);
        cell(QRectF(t.left(), t.top() + rowH, t.width(), rowH), "DRAWING", s.title.title.isEmpty() ? s.name : s.title.title, 3.0);
        cell(QRectF(t.left(), t.top() + 2 * rowH, x1 - t.left(), rowH), "DRAWN BY", s.title.drawnBy, 2.5);
        cell(QRectF(x1, t.top() + 2 * rowH, t.right() - x1, rowH), "DATE", s.title.date, 2.5);
        cell(QRectF(t.left(), t.top() + 3 * rowH, x1 - t.left(), rowH), "SCALE", scale.isEmpty() ? QString("Not to scale") : scale, 2.5);
        cell(QRectF(x1, t.top() + 3 * rowH, t.right() - x1, rowH), "SHEET", s.title.sheetNo, 2.5);
    }
    p.restore();
}

// ======================================================================= canvas

SheetCanvas::SheetCanvas(SheetsWindow* w) : QWidget(w), w_(w) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setMinimumSize(400, 300);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(0xd9, 0xd9, 0xd9));
    setPalette(pal);
}

void SheetCanvas::setTool(Tool t) {
    tool_ = t;
    dimStage_ = 0;
    dragging_ = false;
    setCursor(t == Select ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
}

double SheetCanvas::pxPerMm() const {
    QSizeF pg = paperSizeMm(w_->sheet()->paper, w_->sheet()->landscape);
    return std::max(0.2, std::min((width() - 40) / pg.width(), (height() - 40) / pg.height()));
}

QPointF SheetCanvas::toMm(const QPointF& px) const {
    QSizeF pg = paperSizeMm(w_->sheet()->paper, w_->sheet()->landscape);
    double k = pxPerMm();
    QPointF o((width() - pg.width() * k) / 2, (height() - pg.height() * k) / 2);
    return (px - o) / k;
}

QPointF SheetCanvas::toPx(const QPointF& mm) const {
    QSizeF pg = paperSizeMm(w_->sheet()->paper, w_->sheet()->landscape);
    double k = pxPerMm();
    QPointF o((width() - pg.width() * k) / 2, (height() - pg.height() * k) / 2);
    return o + mm * k;
}

void SheetCanvas::paintEvent(QPaintEvent*) {
    QPainter p(this);
    Sheet* s = w_->sheet();
    QSizeF pg = paperSizeMm(s->paper, s->landscape);
    double k = pxPerMm();
    QPointF o = toPx(QPointF(0, 0));
    p.fillRect(QRectF(o + QPointF(4, 4), QSizeF(pg.width() * k, pg.height() * k)), QColor(0, 0, 0, 50));
    p.translate(o);
    int idx = w_->current();
    paintSheet(p, *s, w_->doc()->model, k, [&](int i, QSize px) { return w_->viewImage(idx, i, px); }, &w_->sel);
    // what is being drawn
    p.setPen(QPen(QColor(0x5b, 0x3c, 0x8a), 1, Qt::DashLine));
    if (dragging_ && tool_ == AddView) p.drawRect(QRectF(start_ * k, now_ * k).normalized());
    if (dragging_ && tool_ == AddLabel) p.drawLine(start_ * k, now_ * k);
    if (tool_ == AddDim && dimStage_ == 1) p.drawLine(dimA_ * k, now_ * k);
    if (tool_ == AddDim && dimStage_ == 2) {
        SheetDimension d;
        d.a = dimA_;
        d.b = dimB_;
        QPointF u = dimB_ - dimA_;
        double len = std::hypot(u.x(), u.y());
        if (len > 0) {
            QPointF n(-u.y() / len, u.x() / len);
            d.offset = QPointF::dotProduct(now_ - dimA_, n);
            p.drawLine((d.a + n * d.offset) * k, (d.b + n * d.offset) * k);
        }
    }
}

QPointF SheetCanvas::snap(const QPointF& mm) const {
    // to viewport corners and edges' middles, within 2 mm of paper
    Sheet* s = w_->sheet();
    double best = 2.0;
    QPointF out = mm;
    for (auto& v : s->viewports)
        for (QPointF c : {v.rect.topLeft(), v.rect.topRight(), v.rect.bottomLeft(), v.rect.bottomRight(), v.rect.center()}) {
            double d = std::hypot(c.x() - mm.x(), c.y() - mm.y());
            if (d < best) { best = d; out = c; }
        }
    return out;
}

SheetSel SheetCanvas::hit(const QPointF& mm) const {
    Sheet* s = w_->sheet();
    for (int i = (int)s->labels.size() - 1; i >= 0; --i) {
        const SheetLabel& l = s->labels[i];
        QFont f("Sans");
        f.setPixelSize(std::max(1, (int)std::lround(l.size * 10)));
        QFontMetricsF fm(f);
        double w = 0;
        QStringList lines = l.text.split('\n');
        for (auto& ln : lines) w = std::max(w, fm.horizontalAdvance(ln) / 10);
        QRectF box(l.at, QSizeF(w, fm.lineSpacing() / 10 * lines.size()));
        if (box.adjusted(-1, -1, 1, 1).contains(mm)) return {SheetSel::Label, i};
        if (l.leader && std::hypot(l.target.x() - mm.x(), l.target.y() - mm.y()) < 1.5) return {SheetSel::Label, i};
    }
    for (int i = (int)s->dims.size() - 1; i >= 0; --i) {
        const SheetDimension& d = s->dims[i];
        QPointF u = d.b - d.a;
        double len = std::hypot(u.x(), u.y());
        if (len < 1e-6) continue;
        QPointF n(-u.y() / len, u.x() / len);
        QPointF a2 = d.a + n * d.offset;
        double t = QPointF::dotProduct(mm - a2, u / len);
        double off = QPointF::dotProduct(mm - a2, n);
        if (t > -1 && t < len + 1 && std::fabs(off) < 2.5) return {SheetSel::Dim, i};
    }
    if (s->title.show && titleRect(*s).contains(mm)) return {SheetSel::Title, 0};
    for (int i = (int)s->viewports.size() - 1; i >= 0; --i)
        if (s->viewports[i].rect.adjusted(-1, -1, 1, 1).contains(mm)) return {SheetSel::View, i};
    return {};
}

void SheetCanvas::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    QPointF mm = toMm(e->position());
    start_ = now_ = mm;
    Sheet* s = w_->sheet();
    if (tool_ == AddDim) {
        if (dimStage_ == 0) { dimA_ = snap(mm); dimStage_ = 1; }
        else if (dimStage_ == 1) { dimB_ = snap(mm); dimStage_ = 2; }
        else {
            SheetDimension d;
            d.a = dimA_;
            d.b = dimB_;
            QPointF u = dimB_ - dimA_;
            double len = std::hypot(u.x(), u.y());
            if (len > 0.5) {
                QPointF n(-u.y() / len, u.x() / len);
                d.offset = QPointF::dotProduct(mm - dimA_, n);
                for (int i = 0; i < (int)s->viewports.size(); ++i)
                    if (s->viewports[i].rect.adjusted(-0.5, -0.5, 0.5, 0.5).contains(d.a) && s->viewports[i].rect.adjusted(-0.5, -0.5, 0.5, 0.5).contains(d.b)) d.viewport = i;
                int n2 = (int)s->dims.size();
                w_->edit("Add Dimension", [&](Sheet& sh) { sh.dims.push_back(d); });
                w_->select({SheetSel::Dim, n2});
            }
            dimStage_ = 0;
        }
        update();
        return;
    }
    if (tool_ == AddView || tool_ == AddLabel) { dragging_ = true; return; }
    // Select: corner handles of the selected viewport first
    handle_ = -1;
    if (w_->sel.kind == SheetSel::View && w_->sel.index < (int)s->viewports.size()) {
        QRectF r = s->viewports[w_->sel.index].rect;
        QPointF cs[4] = {r.topLeft(), r.topRight(), r.bottomRight(), r.bottomLeft()};
        for (int k = 0; k < 4; ++k)
            if (std::hypot(cs[k].x() - mm.x(), cs[k].y() - mm.y()) * pxPerMm() < 8) handle_ = k;
    }
    if (handle_ < 0) w_->select(hit(mm));
    if (w_->sel.kind != SheetSel::None) {
        dragging_ = true;
        before_ = *s;
        w_->beginDrag();
    }
}

void SheetCanvas::mouseMoveEvent(QMouseEvent* e) {
    now_ = toMm(e->position());
    if (tool_ == AddDim) { if (dimStage_) update(); return; }
    if (!dragging_) return;
    if (tool_ != Select) { update(); return; }
    Sheet* s = w_->sheet();
    QPointF d = now_ - start_;
    SheetSel sl = w_->sel;
    if (sl.kind == SheetSel::View && sl.index < (int)s->viewports.size()) {
        QRectF r = before_.viewports[sl.index].rect;
        if (handle_ < 0) r.translate(d);
        else {
            QPointF p = snap(now_);
            if (handle_ == 0) r.setTopLeft(p);
            if (handle_ == 1) r.setTopRight(p);
            if (handle_ == 2) r.setBottomRight(p);
            if (handle_ == 3) r.setBottomLeft(p);
            r = r.normalized();
            if (r.width() < 10) r.setWidth(10);
            if (r.height() < 10) r.setHeight(10);
        }
        s->viewports[sl.index].rect = r;
    } else if (sl.kind == SheetSel::Label && sl.index < (int)s->labels.size()) {
        s->labels[sl.index].at = before_.labels[sl.index].at + d;
    } else if (sl.kind == SheetSel::Dim && sl.index < (int)s->dims.size()) {
        // dragging a dimension moves its line nearer or further
        const SheetDimension& b = before_.dims[sl.index];
        QPointF u = b.b - b.a;
        double len = std::hypot(u.x(), u.y());
        if (len > 0) s->dims[sl.index].offset = b.offset + QPointF::dotProduct(d, QPointF(-u.y() / len, u.x() / len));
    }
    update();
}

void SheetCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !dragging_) return;
    dragging_ = false;
    now_ = toMm(e->position());
    if (tool_ == AddView) {
        QRectF r = QRectF(start_, now_).normalized();
        if (r.width() < 10 || r.height() < 10) {
            // a click: a viewport of a sensible size there
            Sheet* s = w_->sheet();
            QSizeF pg = paperSizeMm(s->paper, s->landscape);
            r = QRectF(start_, QSizeF(pg.width() * 0.4, pg.height() * 0.4));
        }
        w_->addViewport(r);
        setTool(Select);
        return;
    }
    if (tool_ == AddLabel) {
        bool ok = false;
        QString t = QInputDialog::getMultiLineText(this, "Label", "Text:", QString(), &ok);
        if (ok && !t.trimmed().isEmpty()) {
            SheetLabel l;
            l.text = t.trimmed();
            bool drag = std::hypot(now_.x() - start_.x(), now_.y() - start_.y()) > 3;
            l.leader = drag;
            l.target = start_;
            l.at = drag ? now_ : start_;
            int n = (int)w_->sheet()->labels.size();
            w_->edit("Add Label", [&](Sheet& sh) { sh.labels.push_back(l); });
            w_->select({SheetSel::Label, n});
        }
        setTool(Select);
        return;
    }
    w_->endDrag("Move");
}

void SheetCanvas::mouseDoubleClickEvent(QMouseEvent* e) {
    SheetSel h = hit(toMm(e->position()));
    Sheet* s = w_->sheet();
    if (h.kind == SheetSel::Label) {
        bool ok = false;
        QString t = QInputDialog::getMultiLineText(this, "Label", "Text:", s->labels[h.index].text, &ok);
        if (ok) w_->edit("Edit Label", [&](Sheet& sh) { sh.labels[h.index].text = t; });
    }
}

void SheetCanvas::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { setTool(Select); w_->select({}); return; }
    if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) { w_->deleteSelected(); return; }
    QWidget::keyPressEvent(e);
}

// ======================================================================= window

SheetsWindow::SheetsWindow(Document* doc, Viewport* view, QWidget* parent) : QMainWindow(parent, Qt::Window), doc_(doc), view_(view) {
    setObjectName("sheetsWindow");
    setWindowTitle("Drawing Sheets — Folio Gable");
    resize(1200, 800);
    buildUi();
    connect(doc_, &Document::changed, this, [this] { if (!inDrag_) refreshAll(); });
    refreshAll();
}

int SheetsWindow::defaultScene() const {
    return doc_->model.scenes.empty() ? -1 : 0;
}

Sheet* SheetsWindow::sheet() {
    auto& sh = doc_->model.sheets;
    if (sh.empty()) {
        Sheet s;
        s.title.date = QDate::currentDate().toString("d MMM yyyy");
        s.title.sheetNo = "1";
        QFileInfo fi(doc_->filePath);
        s.title.project = doc_->filePath.isEmpty() ? QString() : fi.completeBaseName();
        sh.push_back(s);
    }
    current_ = std::clamp(current_, 0, (int)sh.size() - 1);
    return &sh[current_];
}

void SheetsWindow::setCurrent(int i) {
    current_ = std::clamp(i, 0, std::max(0, (int)doc_->model.sheets.size() - 1));
    sel = {};
    refreshAll();
}

void SheetsWindow::select(const SheetSel& s) {
    sel = s;
    refreshProps();
    canvas_->update();
}

void SheetsWindow::edit(const QString& name, const std::function<void(Sheet&)>& fn) {
    sheet();
    doc_->begin(name);
    fn(doc_->model.sheets[current_]);
    doc_->commit();
    doc_->notifyChanged();
}

void SheetsWindow::beginDrag() {
    sheet();
    inDrag_ = true;
    doc_->begin("Move");
}

void SheetsWindow::endDrag(const QString&) {
    inDrag_ = false;
    doc_->commit();
    doc_->notifyChanged();
}

QString SheetsWindow::viewKey(int si, int vi, QSize px) const {
    const Model& m = doc_->model;
    const SheetViewport& v = m.sheets[si].viewports[vi];
    quint64 rev = m.root.rev;
    for (auto& d : m.defs) rev ^= d.ents.rev * 1315423911ull;
    QString cam;
    if (v.scene >= 0 && v.scene < (int)m.scenes.size()) {
        const CameraState& c = m.scenes[v.scene].cam;
        cam = QString("%1,%2,%3|%4,%5,%6|%7|%8").arg(c.eye.x).arg(c.eye.y).arg(c.eye.z).arg(c.target.x).arg(c.target.y).arg(c.target.z).arg(c.perspective).arg(c.orthoHeight);
    }
    return QString("%1|%2|%3x%4|%5|%6|%7").arg(v.scene).arg(v.scale).arg(px.width()).arg(px.height()).arg(rev).arg(cam).arg(m.materials.size());
}

QImage SheetsWindow::viewImage(int si, int vi, QSize px, bool cached) {
    Model& m = doc_->model;
    if (si < 0 || si >= (int)m.sheets.size() || vi < 0 || vi >= (int)m.sheets[si].viewports.size() || !view_) return QImage();
    // a screen picture is drawn at most 1600 px wide: dragging a big viewport stays smooth
    QString key = viewKey(si, vi, px);
    auto it = cache_.find({si, vi});
    if (cached && it != cache_.end() && it->second.key == key) return it->second.img;
    const SheetViewport& v = m.sheets[si].viewports[vi];
    // the scene's camera, tags, style and shadows for the picture, then back as they were
    CameraState cam0 = m.camera;
    std::vector<bool> tags0;
    for (auto& t : m.tags) tags0.push_back(t.visible);
    Style style0 = m.style;
    ShadowSettings shadow0 = m.shadow;
    int shown0 = m.shownPhoto;
    bool changedTags = false;
    if (v.scene >= 0 && v.scene < (int)m.scenes.size()) {
        const Scene& sc = m.scenes[v.scene];
        m.camera = sc.cam;
        for (size_t k = 0; k < sc.tagVisible.size() && k < m.tags.size(); ++k)
            if (m.tags[k].visible != sc.tagVisible[k]) { m.tags[k].visible = sc.tagVisible[k]; changedTags = true; }
        if (sc.hasStyle) m.style = sc.style;
        m.shadow = sc.shadow;
        m.shownPhoto = -1;
    }
    if (v.scale > 0 && !m.camera.perspective) m.camera.orthoHeight = v.rect.height() * v.scale;
    if (changedTags) { m.root.touch(); for (auto& d : m.defs) d.ents.touch(); }
    QImage img = view_->renderImage(std::max(8, px.width()), std::max(8, px.height()), false);
    m.camera = cam0;
    for (size_t k = 0; k < tags0.size() && k < m.tags.size(); ++k) m.tags[k].visible = tags0[k];
    m.style = style0;
    m.shadow = shadow0;
    m.shownPhoto = shown0;
    if (changedTags) { m.root.touch(); for (auto& d : m.defs) d.ents.touch(); view_->update(); }
    if (cached) cache_[{si, vi}] = {viewKey(si, vi, px), img};
    return img;
}

void SheetsWindow::addViewport(const QRectF& mm) {
    SheetViewport v;
    v.scene = newScene_->currentData().toInt();
    v.rect = mm;
    int n = (int)sheet()->viewports.size();
    edit("Add View", [&](Sheet& s) { s.viewports.push_back(v); });
    select({SheetSel::View, n});
}

void SheetsWindow::addSheet() {
    Sheet s;
    Sheet* cur = sheet();
    s.paper = cur->paper;
    s.landscape = cur->landscape;
    s.title = cur->title;
    s.title.title.clear();
    int n = (int)doc_->model.sheets.size();
    s.name = QString("Sheet %1").arg(n + 1);
    s.title.sheetNo = QString::number(n + 1);
    doc_->begin("Add Sheet");
    doc_->model.sheets.push_back(s);
    doc_->commit();
    current_ = n;
    doc_->notifyChanged();
}

void SheetsWindow::duplicateSheet() {
    Sheet s = *sheet();
    s.name += " copy";
    doc_->begin("Duplicate Sheet");
    doc_->model.sheets.insert(doc_->model.sheets.begin() + current_ + 1, s);
    doc_->commit();
    ++current_;
    doc_->notifyChanged();
}

void SheetsWindow::deleteSheet() {
    if (doc_->model.sheets.empty()) return;
    doc_->begin("Delete Sheet");
    doc_->model.sheets.erase(doc_->model.sheets.begin() + current_);
    doc_->commit();
    cache_.clear();
    current_ = std::max(0, current_ - 1);
    sel = {};
    doc_->notifyChanged();
}

void SheetsWindow::deleteSelected() {
    SheetSel s = sel;
    if (s.kind == SheetSel::None) return;
    edit("Delete", [&](Sheet& sh) {
        if (s.kind == SheetSel::View && s.index < (int)sh.viewports.size()) {
            sh.viewports.erase(sh.viewports.begin() + s.index);
            for (auto& d : sh.dims) { if (d.viewport == s.index) d.viewport = -1; else if (d.viewport > s.index) --d.viewport; }
        }
        if (s.kind == SheetSel::Label && s.index < (int)sh.labels.size()) sh.labels.erase(sh.labels.begin() + s.index);
        if (s.kind == SheetSel::Dim && s.index < (int)sh.dims.size()) sh.dims.erase(sh.dims.begin() + s.index);
        if (s.kind == SheetSel::Title) sh.title.show = false;
    });
    cache_.clear();
    select({});
}

bool SheetsWindow::exportPdf(const QString& path, QString* err) {
    auto& sheets = doc_->model.sheets;
    sheet();
    QPdfWriter pdf(path);
    pdf.setCreator("Folio Gable");
    pdf.setTitle(sheets[0].title.project.isEmpty() ? QFileInfo(path).completeBaseName() : sheets[0].title.project);
    pdf.setResolution(300);
    auto layout = [&](const Sheet& s) {
        QSizeF mm = paperSizeMm(s.paper, false);
        return QPageLayout(QPageSize(mm, QPageSize::Millimeter, s.paper, QPageSize::ExactMatch),
                           s.landscape ? QPageLayout::Landscape : QPageLayout::Portrait, QMarginsF(0, 0, 0, 0));
    };
    pdf.setPageLayout(layout(sheets[0]));
    QPainter p(&pdf);
    if (!p.isActive()) { if (err) *err = "Could not write " + path; return false; }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    for (int i = 0; i < (int)sheets.size(); ++i) {
        if (i > 0) { pdf.setPageLayout(layout(sheets[i])); pdf.newPage(); }
        double k = pdf.resolution() / 25.4;
        // pictures at 200 dpi: sharp on paper, files of a sensible size
        double kImg = 200 / 25.4;
        paintSheet(p, sheets[i], doc_->model, k, [&](int v, QSize) {
            const QRectF& r = sheets[i].viewports[v].rect;
            return viewImage(i, v, QSize((int)std::lround(r.width() * kImg), (int)std::lround(r.height() * kImg)), false);
        });
    }
    p.end();
    QApplication::restoreOverrideCursor();
    return true;
}

void SheetsWindow::printSheets() {
    QPrinter printer(QPrinter::HighResolution);
    sheet();
    const Sheet& s0 = doc_->model.sheets[0];
    printer.setPageSize(QPageSize(paperSizeMm(s0.paper, false), QPageSize::Millimeter, s0.paper, QPageSize::FuzzyMatch));
    printer.setPageOrientation(s0.landscape ? QPageLayout::Landscape : QPageLayout::Portrait);
    printer.setFullPage(true);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QPainter p(&printer);
    auto& sheets = doc_->model.sheets;
    for (int i = 0; i < (int)sheets.size(); ++i) {
        if (i > 0) printer.newPage();
        QSizeF pg = paperSizeMm(sheets[i].paper, sheets[i].landscape);
        QRectF area = printer.pageLayout().fullRectPixels(printer.resolution());
        double k = std::min(area.width() / pg.width(), area.height() / pg.height());
        double kImg = std::min(k, 200 / 25.4);
        paintSheet(p, sheets[i], doc_->model, k, [&](int v, QSize) {
            const QRectF& r = sheets[i].viewports[v].rect;
            return viewImage(i, v, QSize((int)std::lround(r.width() * kImg), (int)std::lround(r.height() * kImg)), false);
        });
    }
}

void SheetsWindow::changeEvent(QEvent* e) {
    if (e->type() == QEvent::ActivationChange && isActiveWindow()) refreshAll();   // the model may have changed meanwhile
    QMainWindow::changeEvent(e);
}

void SheetsWindow::buildUi() {
    auto* tb = addToolBar("Sheet");
    tb->setObjectName("sheetTools");
    tb->setMovable(false);
    tools_ = new QActionGroup(this);
    auto tool = [&](const QString& label, const QString& tip, SheetCanvas::Tool t, const QString& name) {
        QAction* a = tb->addAction(label);
        a->setObjectName(name);
        a->setToolTip(tip);
        a->setCheckable(true);
        tools_->addAction(a);
        connect(a, &QAction::triggered, this, [this, t] { canvas_->setTool(t); });
        return a;
    };
    tool("Select", "Select, move and resize what is on the sheet (Esc)", SheetCanvas::Select, "sheetSelect")->setChecked(true);
    tool("Add View", "Drag a rectangle on the sheet for a scene of the model", SheetCanvas::AddView, "sheetAddView");
    newScene_ = new QComboBox;
    newScene_->setObjectName("sheetNewScene");
    newScene_->setToolTip("The scene a new view shows");
    tb->addWidget(newScene_);
    tool("Label", "Click for text, or drag from what it points at to where the text goes", SheetCanvas::AddLabel, "sheetAddLabel");
    tool("Dimension", "Click two points, then where the dimension line goes. In a view with a scale it measures the model.",
         SheetCanvas::AddDim, "sheetAddDim");
    tb->addSeparator();
    QAction* del = tb->addAction("Delete", this, [this] { deleteSelected(); });
    del->setObjectName("sheetDelete");
    tb->addSeparator();
    QAction* pr = tb->addAction("Print…", this, [this] { printSheets(); });
    pr->setObjectName("sheetPrint");
    QAction* pdf = tb->addAction("Save as PDF…", this, [this] {
        QSettings st;
        QString fn = QFileDialog::getSaveFileName(this, "Save Sheets as PDF",
                                                  st.value("lastDir", QDir::homePath()).toString() + "/" +
                                                      (sheet()->title.project.isEmpty() ? QString("Sheets") : sheet()->title.project) + ".pdf",
                                                  "PDF (*.pdf)");
        if (fn.isEmpty()) return;
        if (!fn.endsWith(".pdf", Qt::CaseInsensitive)) fn += ".pdf";
        QString err;
        if (!exportPdf(fn, &err)) QMessageBox::warning(this, "Save as PDF", err);
    });
    pdf->setObjectName("sheetPdf");

    auto* split = new QSplitter;
    // left: the sheets and the page
    auto* left = new QWidget;
    left->setObjectName("sheetSide");
    auto* lv = new QVBoxLayout(left);
    lv->addWidget(new QLabel("Sheets"));
    list_ = new QListWidget;
    list_->setObjectName("sheetList");
    connect(list_, &QListWidget::currentRowChanged, this, [this](int r) { if (!syncing_ && r >= 0) setCurrent(r); });
    connect(list_, &QListWidget::itemChanged, this, [this](QListWidgetItem* it) {
        if (syncing_) return;
        int r = list_->row(it);
        QString n = it->text();
        if (r >= 0 && r < (int)doc_->model.sheets.size() && doc_->model.sheets[r].name != n) {
            current_ = r;
            edit("Rename Sheet", [&](Sheet& s) { s.name = n; });
        }
    });
    lv->addWidget(list_);
    auto* lb = new QHBoxLayout;
    auto btn = [&](const QString& t, const QString& name, std::function<void()> fn) {
        auto* b = new QPushButton(t);
        b->setObjectName(name);
        connect(b, &QPushButton::clicked, this, fn);
        lb->addWidget(b);
    };
    btn("Add", "sheetAdd", [this] { addSheet(); });
    btn("Duplicate", "sheetDuplicate", [this] { duplicateSheet(); });
    btn("Delete", "sheetRemove", [this] { deleteSheet(); });
    lv->addLayout(lb);
    auto* pageBox = new QGroupBox("Paper");
    auto* pf = new QFormLayout(pageBox);
    paper_ = new QComboBox;
    paper_->setObjectName("sheetPaper");
    for (auto* p : kPapers) paper_->addItem(QString(p) == "ArchC" ? "Arch C" : QString(p) == "ArchD" ? "Arch D" : QString(p), QString(p));
    orient_ = new QComboBox;
    orient_->setObjectName("sheetOrientation");
    orient_->addItems({"Landscape", "Portrait"});
    pf->addRow("Size:", paper_);
    pf->addRow("Orientation:", orient_);
    connect(paper_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!syncing_) edit("Paper Size", [&](Sheet& s) { s.paper = paper_->currentData().toString(); });
    });
    connect(orient_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (!syncing_) edit("Orientation", [&](Sheet& s) { s.landscape = i == 0; });
    });
    lv->addWidget(pageBox);
    auto* titleBox = new QGroupBox("Title block");
    auto* tf = new QFormLayout(titleBox);
    tShow_ = new QCheckBox("Show the title block");
    tShow_->setObjectName("sheetTitleShow");
    tf->addRow(tShow_);
    connect(tShow_, &QCheckBox::toggled, this, [this](bool on) { if (!syncing_) edit("Title Block", [&](Sheet& s) { s.title.show = on; }); });
    auto field = [&](const QString& cap, const QString& name, QLineEdit*& e, std::function<void(Sheet&, const QString&)> set) {
        e = new QLineEdit;
        e->setObjectName(name);
        tf->addRow(cap, e);
        QLineEdit* ed = e;
        connect(e, &QLineEdit::editingFinished, this, [this, ed, set] { if (!syncing_) edit("Title Block", [&](Sheet& s) { set(s, ed->text()); }); });
    };
    field("Project:", "titleProject", tProject_, [](Sheet& s, const QString& v) { s.title.project = v; });
    field("Drawing:", "titleDrawing", tTitle_, [](Sheet& s, const QString& v) { s.title.title = v; });
    field("Drawn by:", "titleDrawnBy", tDrawn_, [](Sheet& s, const QString& v) { s.title.drawnBy = v; });
    field("Date:", "titleDate", tDate_, [](Sheet& s, const QString& v) { s.title.date = v; });
    field("Sheet:", "titleSheet", tSheet_, [](Sheet& s, const QString& v) { s.title.sheetNo = v; });
    field("Scale:", "titleScale", tScale_, [](Sheet& s, const QString& v) { s.title.scale = v; });
    tScale_->setPlaceholderText("from the views");
    lv->addWidget(titleBox);
    lv->addStretch(1);
    split->addWidget(left);
    canvas_ = new SheetCanvas(this);
    canvas_->setObjectName("sheetCanvas");
    split->addWidget(canvas_);
    // right: what is selected
    props_ = new QStackedWidget;
    props_->setObjectName("sheetProps");
    props_->addWidget(new QLabel("  Select something on the sheet."));
    auto* vpw = new QWidget;
    auto* vf = new QFormLayout(vpw);
    vpScene_ = new QComboBox;
    vpScene_->setObjectName("viewScene");
    vpScale_ = new QComboBox;
    vpScale_->setObjectName("viewScale");
    vpScale_->addItem("Fit the scene", 0.0);
    for (double s : kScales) vpScale_->addItem(scaleText(s), s);
    vpFrame_ = new QCheckBox("Frame");
    vpFrame_->setObjectName("viewFrame");
    vf->addRow(new QLabel("<b>View</b>"));
    vf->addRow("Scene:", vpScene_);
    vf->addRow("Scale:", vpScale_);
    vf->addRow("", vpFrame_);
    auto* note = new QLabel("A scale needs a scene in parallel projection (Camera › Parallel Projection).");
    note->setWordWrap(true);
    vf->addRow(note);
    connect(vpScene_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!syncing_ && sel.kind == SheetSel::View) edit("View Scene", [&](Sheet& s) { s.viewports[sel.index].scene = vpScene_->currentData().toInt(); });
    });
    connect(vpScale_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        if (!syncing_ && sel.kind == SheetSel::View) edit("View Scale", [&](Sheet& s) { s.viewports[sel.index].scale = vpScale_->currentData().toDouble(); });
    });
    connect(vpFrame_, &QCheckBox::toggled, this, [this](bool on) {
        if (!syncing_ && sel.kind == SheetSel::View) edit("View Frame", [&](Sheet& s) { s.viewports[sel.index].frame = on; });
    });
    props_->addWidget(vpw);
    auto* lw = new QWidget;
    auto* lf = new QFormLayout(lw);
    lbText_ = new QLineEdit;
    lbText_->setObjectName("labelText");
    lbSize_ = new QDoubleSpinBox;
    lbSize_->setObjectName("labelSize");
    lbSize_->setRange(1, 50);
    lbSize_->setSuffix(" mm");
    lbSize_->setSingleStep(0.5);
    lf->addRow(new QLabel("<b>Label</b>"));
    lf->addRow("Text:", lbText_);
    lf->addRow("Height:", lbSize_);
    connect(lbText_, &QLineEdit::editingFinished, this, [this] {
        if (!syncing_ && sel.kind == SheetSel::Label) edit("Label Text", [&](Sheet& s) { s.labels[sel.index].text = lbText_->text(); });
    });
    connect(lbSize_, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, [this](double v) {
        if (!syncing_ && sel.kind == SheetSel::Label) edit("Label Size", [&](Sheet& s) { s.labels[sel.index].size = v; });
    });
    props_->addWidget(lw);
    auto* dw = new QWidget;
    auto* df = new QFormLayout(dw);
    dmMeasured_ = new QLabel;
    dmMeasured_->setObjectName("dimMeasured");
    dmText_ = new QLineEdit;
    dmText_->setObjectName("dimText");
    dmText_->setPlaceholderText("the measured length");
    df->addRow(new QLabel("<b>Dimension</b>"));
    df->addRow("Measures:", dmMeasured_);
    df->addRow("Text:", dmText_);
    connect(dmText_, &QLineEdit::editingFinished, this, [this] {
        if (!syncing_ && sel.kind == SheetSel::Dim) edit("Dimension Text", [&](Sheet& s) { s.dims[sel.index].text = dmText_->text(); });
    });
    props_->addWidget(dw);
    props_->addWidget(new QLabel("  The title block: its fields are on the left."));
    props_->setMinimumWidth(240);
    split->addWidget(props_);
    split->setStretchFactor(1, 1);
    split->setSizes({260, 700, 260});
    setCentralWidget(split);
}

void SheetsWindow::syncSheetFields() {
    Sheet* s = sheet();
    syncing_ = true;
    paper_->setCurrentIndex(std::max(0, paper_->findData(s->paper)));
    orient_->setCurrentIndex(s->landscape ? 0 : 1);
    tShow_->setChecked(s->title.show);
    tProject_->setText(s->title.project);
    tTitle_->setText(s->title.title);
    tDrawn_->setText(s->title.drawnBy);
    tDate_->setText(s->title.date);
    tSheet_->setText(s->title.sheetNo);
    tScale_->setText(s->title.scale);
    syncing_ = false;
}

void SheetsWindow::refreshProps() {
    Sheet* s = sheet();
    syncing_ = true;
    auto fillScenes = [&](QComboBox* c, int cur) {
        c->clear();
        c->addItem("The view as it is now", -1);
        for (int i = 0; i < (int)doc_->model.scenes.size(); ++i) c->addItem(doc_->model.scenes[i].name, i);
        c->setCurrentIndex(std::max(0, c->findData(cur)));
    };
    int ns = newScene_->count() ? newScene_->currentData().toInt() : defaultScene();
    fillScenes(newScene_, ns);
    if (sel.kind == SheetSel::View && sel.index < (int)s->viewports.size()) {
        const SheetViewport& v = s->viewports[sel.index];
        fillScenes(vpScene_, v.scene);
        int si = vpScale_->findData(v.scale);
        if (si < 0) { vpScale_->addItem(scaleText(v.scale), v.scale); si = vpScale_->count() - 1; }
        vpScale_->setCurrentIndex(si);
        vpFrame_->setChecked(v.frame);
        props_->setCurrentIndex(1);
    } else if (sel.kind == SheetSel::Label && sel.index < (int)s->labels.size()) {
        lbText_->setText(s->labels[sel.index].text);
        lbSize_->setValue(s->labels[sel.index].size);
        props_->setCurrentIndex(2);
    } else if (sel.kind == SheetSel::Dim && sel.index < (int)s->dims.size()) {
        SheetDimension d = s->dims[sel.index];
        dmText_->setText(d.text);
        d.text.clear();
        dmMeasured_->setText(dimensionText(*s, d, doc_->model));
        props_->setCurrentIndex(3);
    } else if (sel.kind == SheetSel::Title) {
        props_->setCurrentIndex(4);
    } else {
        sel = {};
        props_->setCurrentIndex(0);
    }
    syncing_ = false;
}

void SheetsWindow::refreshAll() {
    sheet();
    syncing_ = true;
    list_->clear();
    for (auto& s : doc_->model.sheets) {
        auto* it = new QListWidgetItem(s.name);
        it->setFlags(it->flags() | Qt::ItemIsEditable);
        list_->addItem(it);
    }
    list_->setCurrentRow(current_);
    syncing_ = false;
    syncSheetFields();
    refreshProps();
    canvas_->update();
}

}  // namespace gable
