#include "ui/Panels.h"
#include "core/Geometry.h"
#include "core/Solids.h"
#include "view/Viewport.h"
#include "ui/Icons.h"
#include <QDate>
#include <QFileInfo>
#include <QBuffer>
#include <QColorDialog>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QScrollBar>
#include <QTimer>
#include <QMessageBox>
#include <QPainter>
#include <QRandomGenerator>
#include <QtMath>
#include <functional>

namespace gable {

// ======================================================================= small widgets

ColorButton::ColorButton(QWidget* parent) : QToolButton(parent) {
    setFixedSize(42, 22);
    connect(this, &QToolButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(c_, this, "Choose color", QColorDialog::ShowAlphaChannel);
        if (c.isValid()) { setColor(c); emit colorChanged(c); }
    });
}

void ColorButton::setColor(const QColor& c) {
    c_ = c;
    QPixmap pm(36, 16);
    pm.fill(c);
    QPainter p(&pm);
    p.setPen(QColor(90, 90, 90));
    p.drawRect(0, 0, 35, 15);
    setIcon(pm);
    setIconSize(QSize(36, 16));
}

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* content, QWidget* parent)
    : QWidget(parent), content_(content), title_(title) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);
    header_ = new QToolButton(this);
    header_->setText(title);
    header_->setCheckable(true);
    header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header_->setProperty("paneHeader", true);
    header_->setFocusPolicy(Qt::NoFocus);
    lay->addWidget(header_);
    auto* wrap = new QWidget(this);
    auto* wl = new QVBoxLayout(wrap);
    wl->setContentsMargins(8, 6, 8, 10);
    wl->addWidget(content_);
    lay->addWidget(wrap);
    connect(header_, &QToolButton::toggled, this, [this, wrap](bool on) {
        wrap->setVisible(on);
        header_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
    });
    content_->setParent(wrap);
    setExpanded(false);
    wrap->setVisible(false);
}

void CollapsibleSection::setExpanded(bool e) {
    header_->setChecked(e);
    header_->setArrowType(e ? Qt::DownArrow : Qt::RightArrow);
}
bool CollapsibleSection::expanded() const { return header_->isChecked(); }

Tray::Tray(QWidget* parent) : QScrollArea(parent) {
    auto* w = new QWidget;
    lay_ = new QVBoxLayout(w);
    lay_->setContentsMargins(0, 0, 0, 0);
    lay_->setSpacing(0);
    lay_->addStretch(1);
    lay_->setSizeConstraint(QLayout::SetNoConstraint);
    setWidget(w);
    setObjectName("trayPane");
    w->setObjectName("trayBody");
    setFrameShape(QFrame::NoFrame);
    setWidgetResizable(true);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setMinimumWidth(300);
    widget()->setMinimumWidth(0);
}

CollapsibleSection* Tray::addPanel(const QString& title, QWidget* w, bool expanded) {
    auto* s = new CollapsibleSection(title, w);
    lay_->insertWidget(lay_->count() - 1, s);
    s->setExpanded(expanded);
    sections_.push_back(s);
    return s;
}

void Tray::showPanel(const QString& title) {
    for (auto* s : sections_)
        if (s->title() == title) {
            s->setExpanded(true);
            // scroll vertically only; the tray never scrolls sideways
            QTimer::singleShot(0, this, [this, s] {
                verticalScrollBar()->setValue(s->y());
                horizontalScrollBar()->setValue(0);
            });
        }
}

static QFrame* hline() {
    auto* f = new QFrame;
    f->setFrameShape(QFrame::HLine);
    f->setFrameShadow(QFrame::Sunken);
    return f;
}

// ======================================================================= Entity Info

EntityInfoPanel::EntityInfoPanel(Document* d, QWidget* parent) : QWidget(parent), doc_(d) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    title_ = new QLabel("No Selection");
    QFont f = title_->font();
    f.setBold(true);
    title_->setFont(f);
    lay->addWidget(title_);
    body_ = new QWidget;
    bodyLay_ = new QVBoxLayout(body_);
    bodyLay_->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(body_);
    connect(d, &Document::selectionChanged, this, &EntityInfoPanel::refresh);
    connect(d, &Document::changed, this, &EntityInfoPanel::refresh);
}

void EntityInfoPanel::clearBody() {
    // Replace the whole body widget: everything the last selection showed goes with it
    // (rows, nested layouts, labels made by QFormLayout), immediately hidden.
    if (body_) {
        body_->hide();
        layout()->removeWidget(body_);
        body_->deleteLater();
    }
    body_ = new QWidget;
    bodyLay_ = new QVBoxLayout(body_);
    bodyLay_->setContentsMargins(0, 0, 0, 0);
    layout()->addWidget(body_);
}

QComboBox* EntityInfoPanel::tagCombo(int current) {
    auto* cb = new QComboBox;
    cb->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    cb->setMinimumContentsLength(8);
    for (auto& t : doc_->model.tags) cb->addItem(t.name);
    cb->setCurrentIndex(current < 0 ? -1 : current);
    connect(cb, QOverload<int>::of(&QComboBox::activated), this, [this](int i) {
        doc_->begin("Change Tag");
        doc_->setTagOnSelection(i);
        doc_->commit();
    });
    return cb;
}

void EntityInfoPanel::refresh() {
    if (updating_) return;
    clearBody();
    Document& D = *doc_;
    const UnitSettings& U = D.model.units;
    Entities& E = D.active();
    D.pruneSelection();
    auto* form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setRowWrapPolicy(QFormLayout::DontWrapRows);
    auto addRow = [&](const QString& l, QWidget* w) { form->addRow(l, w); };
    auto ro = [&](const QString& s) {
        auto* l = new QLabel(s);
        l->setTextInteractionFlags(Qt::TextSelectableByMouse);
        l->setWordWrap(true);
        l->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        return l;
    };
    if (D.sel.empty()) {
        title_->setText("No Selection");
        BBox b = D.model.worldBBox();
        if (b.valid()) {
            Vec3 s = b.size();
            addRow("Model size:", ro(formatLength(s.x, U) + " × " + formatLength(s.y, U) + " × " + formatLength(s.z, U)));
        }
        bodyLay_->addLayout(form);
        return;
    }
    if (D.sel.size() == 1) {
        EntRef r = D.sel[0];
        switch (r.kind) {
        case EntKind::Edge: {
            Edge& Ed = E.edges[r.id];
            bool arc = Ed.curve >= 0 && Ed.curve < (int)E.curves.size() && E.curves[Ed.curve].isArc;
            title_->setText(arc ? (E.curves[Ed.curve].closed ? "Circle" : "Arc") : "Edge");
            addRow("Tag:", tagCombo(Ed.tag));
            {
                // typing a new length moves the edge's second end along the edge, as in the reference app
                auto* len = new QLineEdit(formatLength(E.edgeLength(r.id), U));
                int eid = r.id;
                connect(len, &QLineEdit::editingFinished, this, [this, eid, len] {
                    Entities& A = doc_->active();
                    if (eid >= (int)A.edges.size() || !A.edges[eid].alive) return;
                    double L;
                    if (!parseLength(len->text(), doc_->model.units, L) || L <= kTol) return;
                    double cur = A.edgeLength(eid);
                    if (std::fabs(L - cur) < kTol) return;
                    Vec3 dir = A.edgeDir(eid);
                    doc_->begin("Edge Length");
                    geom::transformVertices(A, {A.edges[eid].v[1]}, Mat4::translation(dir * (L - cur)), true);
                    updating_ = true;
                    doc_->commit();
                    updating_ = false;
                    refresh();
                });
                addRow("Length:", len);
            }
            auto* soft = new QCheckBox("Soft");
            soft->setChecked(Ed.soft);
            auto* smooth = new QCheckBox("Smooth");
            smooth->setChecked(Ed.smooth);
            auto* hidden = new QCheckBox("Hidden");
            hidden->setChecked(Ed.hidden);
            auto* row = new QWidget;
            auto* hl = new QHBoxLayout(row);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->addWidget(soft); hl->addWidget(smooth); hl->addWidget(hidden);
            addRow("", row);
            int id = r.id;
            connect(soft, &QCheckBox::toggled, this, [this, id](bool v) { doc_->begin("Soften"); doc_->active().edges[id].soft = v; doc_->active().touch(); doc_->commit(); });
            connect(smooth, &QCheckBox::toggled, this, [this, id](bool v) { doc_->begin("Smooth"); doc_->active().edges[id].smooth = v; doc_->active().touch(); doc_->commit(); });
            connect(hidden, &QCheckBox::toggled, this, [this, id](bool v) { doc_->begin("Hide"); doc_->active().edges[id].hidden = v; doc_->active().touch(); doc_->commit(); });
            break;
        }
        case EntKind::Face: {
            Face& F = E.faces[r.id];
            title_->setText("Face");
            addRow("Tag:", tagCombo(F.tag));
            auto matName = [&](int m) { return m >= 0 && m < (int)D.model.materials.size() ? D.model.materials[m].name : QString("Default"); };
            addRow("Material:", ro(matName(F.matFront)));
            addRow("Back material:", ro(matName(F.matBack)));
            Mat4 xf = D.activeXf();
            double sc = xf.vector(Vec3(1, 0, 0)).length() * xf.vector(Vec3(0, 1, 0)).length();
            addRow("Area:", ro(formatArea(E.faceArea(r.id) * sc, U)));
            auto* hidden = new QCheckBox("Hidden");
            hidden->setChecked(F.hidden);
            int id = r.id;
            connect(hidden, &QCheckBox::toggled, this, [this, id](bool v) { doc_->begin("Hide"); doc_->active().faces[id].hidden = v; doc_->active().touch(); doc_->commit(); });
            addRow("", hidden);
            break;
        }
        case EntKind::Instance: {
            Instance& in = E.insts[r.id];
            ComponentDef& def = D.model.defs[in.def];
            title_->setText(def.isGroup ? "Group" : QString("Component (%1 in model)").arg(D.model.instanceCount(in.def)));
            addRow("Tag:", tagCombo(in.tag));
            auto* inst = new QLineEdit(in.name);
            int id = r.id;
            connect(inst, &QLineEdit::editingFinished, this, [this, id, inst] {
                if (doc_->active().insts[id].name == inst->text()) return;
                doc_->begin("Rename");
                doc_->active().insts[id].name = inst->text();
                updating_ = true;
                doc_->commit();
                updating_ = false;
            });
            addRow("Instance:", inst);
            auto* dn = new QLineEdit(def.name);
            int di = in.def;
            connect(dn, &QLineEdit::editingFinished, this, [this, di, dn] {
                if (doc_->model.defs[di].name == dn->text() || dn->text().isEmpty()) return;
                doc_->begin("Rename Definition");
                doc_->model.defs[di].name = dn->text();
                updating_ = true;
                doc_->commit();
                updating_ = false;
            });
            addRow("Definition:", dn);
            if (!def.isGroup) {
                auto* desc = new QLineEdit(def.description);
                connect(desc, &QLineEdit::editingFinished, this, [this, di, desc] {
                    doc_->model.defs[di].description = desc->text();
                    doc_->modified = true;
                });
                addRow("Description:", desc);
            }
            auto matName = [&](int m) { return m >= 0 && m < (int)D.model.materials.size() ? D.model.materials[m].name : QString("Default"); };
            addRow("Material:", ro(matName(in.material)));
            auto* lock = new QCheckBox("Locked");
            lock->setChecked(in.locked);
            connect(lock, &QCheckBox::toggled, this, [this, id](bool v) { doc_->begin(v ? "Lock" : "Unlock"); doc_->active().insts[id].locked = v; doc_->commit(); });
            auto* hidden = new QCheckBox("Hidden");
            hidden->setChecked(in.hidden);
            connect(hidden, &QCheckBox::toggled, this, [this, id](bool v) { doc_->begin("Hide"); doc_->active().insts[id].hidden = v; doc_->active().touch(); doc_->commit(); });
            auto* row = new QWidget;
            auto* hl = new QHBoxLayout(row);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->addWidget(lock); hl->addWidget(hidden);
            addRow("", row);
            Mat4 world = D.activeXf() * in.xf;
            if (instanceIsSolid(D.model, in)) {
                Instance tmp = in;
                tmp.xf = world;
                double v = instanceVolume(D.model, tmp);
                addRow("Volume:", ro(formatVolume(std::fabs(v), U)));
            }
            BBox b = D.model.defBBox(in.def);
            if (b.valid()) {
                Vec3 s = b.size();
                double sx = world.vector(Vec3(1, 0, 0)).length(), sy = world.vector(Vec3(0, 1, 0)).length(), sz = world.vector(Vec3(0, 0, 1)).length();
                // Position (of the group's origin) and size, both editable
                auto triple = [&](const Vec3& v, std::function<void(int, double)> set) {
                    auto* row = new QWidget;
                    auto* hl = new QHBoxLayout(row);
                    hl->setContentsMargins(0, 0, 0, 0);
                    hl->setSpacing(3);
                    for (int k = 0; k < 3; ++k) {
                        auto* le = new QLineEdit(formatLength(k == 0 ? v.x : (k == 1 ? v.y : v.z), U));
                        le->setMinimumWidth(40);
                        connect(le, &QLineEdit::editingFinished, this, [this, le, k, set] {
                            double x;
                            if (parseLength(le->text(), doc_->model.units, x)) set(k, x);
                        });
                        hl->addWidget(le);
                    }
                    return row;
                };
                Vec3 o = world.origin();
                addRow("Position:", triple(o, [this, id](int k, double x) {
                    Entities& A = doc_->active();
                    Mat4 w = doc_->activeXf() * A.insts[id].xf;
                    Vec3 cur = w.origin();
                    double c = k == 0 ? cur.x : (k == 1 ? cur.y : cur.z);
                    if (std::fabs(c - x) < kTol) return;
                    Vec3 dv(k == 0 ? x - c : 0, k == 1 ? x - c : 0, k == 2 ? x - c : 0);
                    doc_->begin("Move");
                    A.insts[id].xf = doc_->activeInv() * Mat4::translation(dv) * w;
                    A.touch();
                    updating_ = true;
                    doc_->commit();
                    updating_ = false;
                    refresh();
                }));
                Vec3 sz3(s.x * sx, s.y * sy, s.z * sz);
                addRow("Size:", triple(sz3, [this, id, sz3](int k, double x) {
                    double c = k == 0 ? sz3.x : (k == 1 ? sz3.y : sz3.z);
                    if (x <= kTol || c <= kTol || std::fabs(c - x) < kTol) return;
                    Entities& A = doc_->active();
                    BBox bb = doc_->model.defBBox(A.insts[id].def);
                    Vec3 f(1, 1, 1);
                    (k == 0 ? f.x : (k == 1 ? f.y : f.z)) = x / c;
                    doc_->begin("Scale");
                    // scale along the group's own axes, keeping its lowest corner in place
                    A.insts[id].xf = A.insts[id].xf * Mat4::scalingAbout(bb.lo, f);
                    A.touch();
                    updating_ = true;
                    doc_->commit();
                    updating_ = false;
                    refresh();
                }));
            }
            break;
        }
        case EntKind::Dimension: {
            title_->setText("Dimension");
            auto* t = new QLineEdit(E.dims[r.id].textOverride.isEmpty() ? "<>" : E.dims[r.id].textOverride);
            int id = r.id;
            connect(t, &QLineEdit::editingFinished, this, [this, id, t] {
                doc_->begin("Edit Dimension");
                doc_->active().dims[id].textOverride = t->text() == "<>" ? QString() : t->text();
                updating_ = true;
                doc_->commit();
                updating_ = false;
            });
            addRow("Text:", t);
            addRow("", ro("Use <> for the measured value."));
            addRow("Tag:", tagCombo(E.dims[r.id].tag));
            break;
        }
        case EntKind::Text: {
            title_->setText("Text");
            auto* t = new QPlainTextEdit(E.texts[r.id].text);
            t->setMaximumHeight(80);
            int id = r.id;
            connect(t, &QPlainTextEdit::textChanged, this, [this, id, t] {
                doc_->active().texts[id].text = t->toPlainText();
                doc_->active().touch();
                doc_->modified = true;
                emit requestRedraw();
            });
            addRow("Text:", t);
            addRow("Tag:", tagCombo(E.texts[r.id].tag));
            break;
        }
        case EntKind::Guide: {
            title_->setText(E.guides[r.id].isPoint ? "Guide Point" : "Guide Line");
            addRow("Tag:", tagCombo(E.guides[r.id].tag));
            break;
        }
        case EntKind::Section: {
            SectionPlane& s = E.sections[r.id];
            title_->setText("Section Plane");
            auto* n = new QLineEdit(s.name);
            int id = r.id;
            connect(n, &QLineEdit::editingFinished, this, [this, id, n] { doc_->active().sections[id].name = n->text(); doc_->modified = true; });
            addRow("Name:", n);
            auto* act = new QCheckBox("Active cut");
            act->setChecked(s.active);
            connect(act, &QCheckBox::toggled, this, [this, id](bool v) {
                doc_->begin("Active Cut");
                auto& secs = doc_->active().sections;
                for (auto& x : secs) x.active = false;
                secs[id].active = v;
                doc_->active().touch();
                doc_->commit();
            });
            addRow("", act);
            break;
        }
        default: break;
        }
        bodyLay_->addLayout(form);
        // Arc/circle parameters
        if (r.kind == EntKind::Edge) {
            Edge& Ed = E.edges[r.id];
            if (Ed.curve >= 0 && Ed.curve < (int)E.curves.size() && E.curves[Ed.curve].isArc) {
                int cid = Ed.curve;
                auto* f2 = new QFormLayout;
                auto* rad = new QLineEdit(formatLength(E.curves[cid].radius, U));
                auto* seg = new QSpinBox;
                seg->setRange(3, 999);
                seg->setValue(std::max(3, E.curves[cid].segments));
                f2->addRow("Radius:", rad);
                f2->addRow("Segments:", seg);
                bodyLay_->addLayout(f2);
                connect(rad, &QLineEdit::editingFinished, this, [this, cid, rad] {
                    double R;
                    Entities& A = doc_->active();
                    Curve& cv = A.curves[cid];
                    if (!parseLength(rad->text(), doc_->model.units, R) || R <= 0 || std::fabs(R - cv.radius) < kTol) return;
                    doc_->begin("Change Radius");
                    double k = R / cv.radius;
                    std::set<int> vs;
                    for (int e : geom::curveEdges(A, cid)) { vs.insert(A.edges[e].v[0]); vs.insert(A.edges[e].v[1]); }
                    for (int v : vs) {
                        Vec3 d = A.verts[v].p - cv.center;
                        Vec3 axial = cv.normal * d.dot(cv.normal);
                        A.verts[v].p = cv.center + axial + (d - axial) * k;
                    }
                    cv.radius = R;
                    A.invalidateIndex();
                    std::set<int> fs;
                    for (int v : vs) for (int e : A.verts[v].edges) for (int f : A.edges[e].faces) fs.insert(f);
                    geom::fixWarpedFaces(A, std::vector<int>(fs.begin(), fs.end()));
                    A.touch();
                    doc_->commit();
                });
                connect(seg, &QSpinBox::editingFinished, this, [this, cid, seg] {
                    Entities& A = doc_->active();
                    Curve cv = A.curves[cid];
                    int n = seg->value();
                    if (n == cv.segments) return;
                    doc_->begin("Change Segments");
                    auto es = geom::curveEdges(A, cid);
                    geom::eraseEdges(A, es);
                    std::vector<Vec3> pts;
                    Vec3 x = cv.xaxis.normalized(), y = cv.normal.cross(x).normalized();
                    double sweep = cv.closed ? 2 * kPi : cv.endAng - cv.startAng;
                    int count = cv.closed ? n : n + 1;
                    for (int i = 0; i < count; ++i) {
                        double a = cv.startAng + sweep * i / n;
                        pts.push_back(cv.center + (x * std::cos(a) + y * std::sin(a)) * cv.radius);
                    }
                    cv.segments = n;
                    geom::addPolyline(A, pts, cv.closed, &cv);
                    doc_->commit();
                    doc_->clearSelection();
                });
            }
        }
        return;
    }
    // multiple
    int ne = 0, nf = 0, ni = 0, no = 0;
    double area = 0;
    for (auto& r : D.sel) {
        if (r.kind == EntKind::Edge) ++ne;
        else if (r.kind == EntKind::Face) { ++nf; area += E.faceArea(r.id); }
        else if (r.kind == EntKind::Instance) ++ni;
        else ++no;
    }
    title_->setText(QString("%1 Entities").arg(D.sel.size()));
    QStringList parts;
    if (ne) parts << QString("%1 edges").arg(ne);
    if (nf) parts << QString("%1 faces").arg(nf);
    if (ni) parts << QString("%1 groups/components").arg(ni);
    if (no) parts << QString("%1 other").arg(no);
    addRow("Contains:", ro(parts.join(", ")));
    addRow("Tag:", tagCombo(-1));
    if (nf) addRow("Total area:", ro(formatArea(area, U)));
    if (ne) {
        auto* soft = new QPushButton("Soften");
        connect(soft, &QPushButton::clicked, this, [this] {
            doc_->begin("Soften Edges");
            for (int e : doc_->selectedEdges()) doc_->active().edges[e].soft = doc_->active().edges[e].smooth = true;
            doc_->active().touch();
            doc_->commit();
        });
        auto* hard = new QPushButton("Unsoften");
        connect(hard, &QPushButton::clicked, this, [this] {
            doc_->begin("Unsoften Edges");
            for (int e : doc_->selectedEdges()) doc_->active().edges[e].soft = doc_->active().edges[e].smooth = false;
            doc_->active().touch();
            doc_->commit();
        });
        auto* row = new QWidget;
        auto* hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->addWidget(soft); hl->addWidget(hard);
        addRow("", row);
    }
    bodyLay_->addLayout(form);
}

// ======================================================================= Material library

static QImage noiseFill(int px, QColor base, int amount, quint32 seed) {
    QImage img(px, px, QImage::Format_RGB32);
    QRandomGenerator rng(seed);
    for (int y = 0; y < px; ++y)
        for (int x = 0; x < px; ++x) {
            int n = (int)rng.bounded(amount * 2 + 1) - amount;
            img.setPixelColor(x, y, QColor(std::clamp(base.red() + n, 0, 255), std::clamp(base.green() + n, 0, 255), std::clamp(base.blue() + n, 0, 255)));
        }
    return img;
}

QImage proceduralTexture(const QString& kind, int px) {
    QRandomGenerator rng(qHash(kind));
    if (kind == "brick") {
        QImage img = noiseFill(px, QColor(200, 200, 195), 10, 7);
        QPainter p(&img);
        int rows = 8, bw = px / 2, bh = px / rows;
        for (int r = 0; r < rows; ++r)
            for (int c = -1; c < 3; ++c) {
                int x = c * bw + ((r % 2) ? bw / 2 : 0);
                QColor col(150 + rng.bounded(40), 60 + rng.bounded(25), 40 + rng.bounded(20));
                p.fillRect(QRect(x + 2, r * bh + 2, bw - 4, bh - 4), col);
            }
        return img;
    }
    if (kind == "wood" || kind == "planks") {
        QImage img(px, px, QImage::Format_RGB32);
        for (int y = 0; y < px; ++y)
            for (int x = 0; x < px; ++x) {
                double g = std::sin((x * 0.06) + 4 * std::sin(y * 0.02) + std::sin(y * 0.11) * 0.5) * 0.5 + 0.5;
                int r = (int)(160 + 40 * g), gg = (int)(110 + 30 * g), b = (int)(60 + 20 * g);
                img.setPixelColor(x, y, QColor(r, gg, b));
            }
        if (kind == "planks") {
            QPainter p(&img);
            p.setPen(QColor(90, 60, 30));
            for (int i = 0; i < 4; ++i) p.drawLine(i * px / 4, 0, i * px / 4, px);
            p.drawLine(px / 8, px / 2, px / 4 + px / 8, px / 2);
        }
        return img;
    }
    if (kind == "concrete") return noiseFill(px, QColor(170, 170, 165), 14, 3);
    if (kind == "asphalt") return noiseFill(px, QColor(70, 70, 72), 18, 5);
    if (kind == "gravel") {
        QImage img = noiseFill(px, QColor(150, 145, 135), 20, 9);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        for (int i = 0; i < px; ++i) {
            int v = 110 + rng.bounded(90);
            p.setBrush(QColor(v, v - 5, v - 12));
            p.setPen(Qt::NoPen);
            double s = 2 + rng.bounded(6);
            p.drawEllipse(QPointF(rng.bounded(px), rng.bounded(px)), s, s * 0.8);
        }
        return img;
    }
    if (kind == "grass") {
        QImage img = noiseFill(px, QColor(80, 130, 50), 22, 11);
        QPainter p(&img);
        for (int i = 0; i < px * 6; ++i) {
            int x = rng.bounded(px), y = rng.bounded(px);
            p.setPen(QColor(60 + rng.bounded(40), 110 + rng.bounded(60), 30 + rng.bounded(30)));
            p.drawLine(x, y, x + rng.bounded(3) - 1, y - 3 - rng.bounded(4));
        }
        return img;
    }
    if (kind == "stone") {
        QImage img = noiseFill(px, QColor(180, 175, 165), 8, 13);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        int n = 9;
        for (int i = 0; i < n * 2; ++i) {
            int v = 140 + rng.bounded(60);
            p.setBrush(QColor(v, v - 6, v - 14));
            p.setPen(QPen(QColor(110, 105, 95), 2));
            double w = px / 3.0 + rng.bounded(px / 5), h = px / 4.0 + rng.bounded(px / 6);
            p.drawRoundedRect(QRectF(rng.bounded(px) - w / 2, rng.bounded(px) - h / 2, w, h), 12, 12);
        }
        return img;
    }
    if (kind == "tiles" || kind == "roof") {
        QImage img(px, px, QImage::Format_RGB32);
        img.fill(kind == "roof" ? QColor(120, 50, 35) : QColor(220, 222, 224));
        QPainter p(&img);
        int n = kind == "roof" ? 6 : 4;
        int s = px / n;
        for (int r = 0; r < n; ++r)
            for (int c = 0; c < n + 1; ++c) {
                int off = (kind == "roof" && r % 2) ? s / 2 : 0;
                int v = rng.bounded(25);
                QColor col = kind == "roof" ? QColor(160 + v, 70 + v / 2, 45) : QColor(238 - v, 240 - v, 242 - v);
                p.fillRect(QRect(c * s - off + 1, r * s + 1, s - 2, s - 2), col);
                if (kind == "roof") { p.setPen(QColor(90, 35, 25)); p.drawLine(c * s - off, r * s + s - 1, c * s - off + s, r * s + s - 1); }
            }
        return img;
    }
    if (kind == "marble") {
        QImage img(px, px, QImage::Format_RGB32);
        for (int y = 0; y < px; ++y)
            for (int x = 0; x < px; ++x) {
                double t = std::sin((x + y) * 0.03 + 3 * std::sin(x * 0.05) * std::cos(y * 0.04));
                int v = (int)(225 + 25 * t);
                img.setPixelColor(x, y, QColor(std::min(v, 255), std::min(v, 255), std::min(v + 4, 255)));
            }
        return img;
    }
    if (kind == "metal") {
        QImage img(px, px, QImage::Format_RGB32);
        for (int y = 0; y < px; ++y) {
            int base = 170 + rng.bounded(16);
            for (int x = 0; x < px; ++x) {
                int v = base + rng.bounded(8);
                img.setPixelColor(x, y, QColor(v, v + 2, v + 6));
            }
        }
        return img;
    }
    return noiseFill(px, QColor(200, 200, 200), 5, 1);
}

const std::vector<LibraryMaterial>& materialLibrary() {
    static std::vector<LibraryMaterial> lib = {
        {"White", QColor(255, 255, 255), 1, "", 0}, {"Black", QColor(20, 20, 20), 1, "", 0},
        {"Light Gray", QColor(200, 200, 200), 1, "", 0}, {"Charcoal", QColor(70, 72, 76), 1, "", 0},
        {"Red", QColor(200, 40, 35), 1, "", 0}, {"Brick Red", QColor(150, 60, 45), 1, "", 0},
        {"Orange", QColor(240, 140, 40), 1, "", 0}, {"Sand", QColor(220, 200, 150), 1, "", 0},
        {"Yellow", QColor(245, 210, 50), 1, "", 0}, {"Olive", QColor(130, 130, 60), 1, "", 0},
        {"Green", QColor(60, 150, 70), 1, "", 0}, {"Forest Green", QColor(34, 90, 50), 1, "", 0},
        {"Teal", QColor(40, 140, 140), 1, "", 0}, {"Sky Blue", QColor(120, 180, 230), 1, "", 0},
        {"Blue", QColor(40, 90, 200), 1, "", 0}, {"Navy", QColor(30, 40, 90), 1, "", 0},
        {"Purple", QColor(120, 70, 160), 1, "", 0}, {"Pink", QColor(230, 150, 180), 1, "", 0},
        {"Brown", QColor(120, 80, 50), 1, "", 0}, {"Beige", QColor(225, 210, 180), 1, "", 0},
        {"Glass", QColor(160, 200, 230), 0.35, "", 0}, {"Tinted Glass", QColor(80, 110, 120), 0.5, "", 0},
        {"Water", QColor(60, 120, 180), 0.6, "", 0},
        {"Brick", QColor(160, 70, 50), 1, "brick", 900}, {"Wood Planks", QColor(180, 130, 80), 1, "planks", 1200},
        {"Wood Grain", QColor(180, 130, 80), 1, "wood", 600}, {"Concrete", QColor(170, 170, 165), 1, "concrete", 1500},
        {"Asphalt", QColor(70, 70, 72), 1, "asphalt", 2000}, {"Gravel", QColor(150, 145, 135), 1, "gravel", 800},
        {"Grass", QColor(80, 130, 50), 1, "grass", 1500}, {"Stone Wall", QColor(170, 165, 155), 1, "stone", 1200},
        {"Ceramic Tiles", QColor(230, 230, 232), 1, "tiles", 600}, {"Roof Tiles", QColor(150, 60, 40), 1, "roof", 1200},
        {"Marble", QColor(235, 235, 238), 1, "marble", 1000}, {"Brushed Metal", QColor(180, 182, 188), 1, "metal", 500},
    };
    return lib;
}

// ======================================================================= Materials

QIcon MaterialsPanel::swatch(const Material& m) const {
    QPixmap pm(48, 48);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    // checker for transparency
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 6; ++x) p.fillRect(x * 8, y * 8, 8, 8, ((x + y) % 2) ? QColor(200, 200, 200) : QColor(255, 255, 255));
    if (m.hasTexture) {
        QImage img = m.image();
        if (!img.isNull()) {
            p.setOpacity(m.alpha);
            p.drawImage(QRect(0, 0, 48, 48), img);
            p.setOpacity(1);
        }
    } else {
        QColor c = m.color;
        c.setAlphaF(m.alpha);
        p.fillRect(0, 0, 48, 48, c);
    }
    p.setPen(QColor(110, 110, 110));
    p.drawRect(0, 0, 47, 47);
    return QIcon(pm);
}

MaterialsPanel::MaterialsPanel(Document* d, QWidget* parent) : QWidget(parent), doc_(d) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto* top = new QHBoxLayout;
    currentSwatch_ = new QLabel;
    currentSwatch_->setFixedSize(40, 40);
    currentLabel_ = new QLabel("Default");
    top->addWidget(currentSwatch_);
    top->addWidget(currentLabel_, 1);
    auto* newB = new QToolButton;
    newB->setText("+");
    newB->setToolTip("Create material");
    auto* editB = new QToolButton;
    editB->setText("Edit");
    auto* moreB = new QToolButton;
    moreB->setText("⋯");
    moreB->setPopupMode(QToolButton::InstantPopup);
    auto* more = new QMenu(moreB);
    more->addAction("Delete current material", this, [this] {
        if (current_ < 0) return;
        int m = current_;
        doc_->begin("Delete Material");
        std::function<void(Entities&)> fix = [&](Entities& E) {
            auto f = [&](int& x) { if (x == m) x = -1; else if (x > m) --x; };
            for (auto& F : E.faces) { f(F.matFront); f(F.matBack); }
            for (auto& Ed : E.edges) f(Ed.material);
            for (auto& in : E.insts) f(in.material);
            E.touch();
        };
        fix(doc_->model.root);
        for (auto& def : doc_->model.defs) fix(def.ents);
        doc_->model.materials.erase(doc_->model.materials.begin() + m);
        doc_->commit();
        setCurrent(-1);
    });
    more->addAction("Purge unused materials", this, [this] {
        std::vector<char> used(doc_->model.materials.size(), 0);
        std::function<void(const Entities&)> mark = [&](const Entities& E) {
            auto u = [&](int x) { if (x >= 0 && x < (int)used.size()) used[x] = 1; };
            for (auto& F : E.faces) if (F.alive) { u(F.matFront); u(F.matBack); }
            for (auto& Ed : E.edges) if (Ed.alive) u(Ed.material);
            for (auto& in : E.insts) if (in.alive) u(in.material);
        };
        mark(doc_->model.root);
        for (auto& def : doc_->model.defs) if (def.alive) mark(def.ents);
        std::vector<int> remap(used.size(), -1);
        int n = 0;
        for (size_t i = 0; i < used.size(); ++i) if (used[i]) remap[i] = n++;
        if (n == (int)used.size()) return;
        doc_->begin("Purge Materials");
        std::function<void(Entities&)> fix = [&](Entities& E) {
            auto f = [&](int& x) { if (x >= 0 && x < (int)remap.size()) x = remap[x]; };
            for (auto& F : E.faces) { f(F.matFront); f(F.matBack); }
            for (auto& Ed : E.edges) f(Ed.material);
            for (auto& in : E.insts) f(in.material);
            E.touch();
        };
        fix(doc_->model.root);
        for (auto& def : doc_->model.defs) fix(def.ents);
        std::vector<Material> keep;
        for (size_t i = 0; i < used.size(); ++i) if (used[i]) keep.push_back(doc_->model.materials[i]);
        doc_->model.materials = keep;
        doc_->commit();
        setCurrent(-1);
    });
    more->addAction("Import texture image…", this, [this] {
        QString fn = QFileDialog::getOpenFileName(this, "Texture image", QString(), "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp)");
        if (fn.isEmpty()) return;
        QImage img(fn);
        if (img.isNull()) return;
        doc_->begin("New Material");
        int m = doc_->model.addMaterial(QFileInfo(fn).completeBaseName(), QColor(200, 200, 200));
        QByteArray png;
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        img.save(&b, "PNG");
        Material& M = doc_->model.materials[m];
        M.hasTexture = true;
        M.texturePng = png;
        M.texW = 1000;
        M.texH = 1000.0 * img.height() / std::max(1, img.width());
        doc_->commit();
        setCurrent(m);
        editMaterial(m);
    });
    moreB->setMenu(more);
    top->addWidget(newB);
    top->addWidget(editB);
    top->addWidget(moreB);
    lay->addLayout(top);
    source_ = new QComboBox;
    source_->addItems({"In Model", "Colors", "Textures"});
    lay->addWidget(source_);
    list_ = new QListWidget;
    list_->setViewMode(QListView::IconMode);
    list_->setIconSize(QSize(44, 44));
    list_->setGridSize(QSize(60, 70));
    list_->setResizeMode(QListView::Adjust);
    list_->setMovement(QListView::Static);
    list_->setWordWrap(true);
    list_->setMinimumHeight(200);
    QFont lf = list_->font();
    lf.setPointSizeF(lf.pointSizeF() * 0.85);
    list_->setFont(lf);
    lay->addWidget(list_);
    connect(source_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MaterialsPanel::refresh);
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem* it) {
        int v = it->data(Qt::UserRole).toInt();
        if (source_->currentIndex() == 0) setCurrent(v);
        else addFromLibrary(v);
        emit materialChosen(current_);
    });
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) {
        if (source_->currentIndex() == 0 && it->data(Qt::UserRole).toInt() >= 0) editMaterial(it->data(Qt::UserRole).toInt());
    });
    connect(newB, &QToolButton::clicked, this, [this] {
        QColor c = QColorDialog::getColor(QColor(180, 180, 180), this, "New material", QColorDialog::ShowAlphaChannel);
        if (!c.isValid()) return;
        doc_->begin("New Material");
        int m = doc_->model.addMaterial("Material", c);
        doc_->model.materials[m].alpha = c.alphaF();
        doc_->model.materials[m].color.setAlpha(255);
        doc_->commit();
        source_->setCurrentIndex(0);
        setCurrent(m);
        emit materialChosen(m);
    });
    connect(editB, &QToolButton::clicked, this, [this] { if (current_ >= 0) editMaterial(current_); });
    connect(d, &Document::changed, this, [this] {
        if (current_ >= (int)doc_->model.materials.size()) current_ = -1;
        if (source_->currentIndex() == 0) refresh();
    });
    refresh();
    setCurrent(-1);
}

void MaterialsPanel::setCurrent(int m) {
    current_ = m;
    Material def;
    def.name = "Default";
    def.color = doc_->model.style.frontColor;
    const Material& M = (m >= 0 && m < (int)doc_->model.materials.size()) ? doc_->model.materials[m] : def;
    currentSwatch_->setPixmap(swatch(M).pixmap(40, 40));
    currentLabel_->setText(M.name);
}

void MaterialsPanel::addFromLibrary(int li) {
    const auto& lib = materialLibrary();
    if (li < 0 || li >= (int)lib.size()) return;
    const LibraryMaterial& L = lib[li];
    int m = doc_->model.findMaterial(L.name);
    if (m < 0) {
        doc_->begin("Add Material");
        m = doc_->model.addMaterial(L.name, L.color);
        Material& M = doc_->model.materials[m];
        M.alpha = L.alpha;
        if (!L.texture.isEmpty()) {
            QImage img = proceduralTexture(L.texture, 256);
            QByteArray png;
            QBuffer b(&png);
            b.open(QIODevice::WriteOnly);
            img.save(&b, "PNG");
            M.hasTexture = true;
            M.texturePng = png;
            M.texW = M.texH = L.size;
        }
        doc_->commit();
    }
    setCurrent(m);
}

void MaterialsPanel::refresh() {
    list_->clear();
    int src = source_->currentIndex();
    if (src == 0) {
        Material def;
        def.name = "Default";
        def.color = doc_->model.style.frontColor;
        auto* it = new QListWidgetItem(swatch(def), "Default");
        it->setData(Qt::UserRole, -1);
        list_->addItem(it);
        for (int i = 0; i < (int)doc_->model.materials.size(); ++i) {
            auto* m = new QListWidgetItem(swatch(doc_->model.materials[i]), doc_->model.materials[i].name);
            m->setData(Qt::UserRole, i);
            list_->addItem(m);
        }
    } else {
        const auto& lib = materialLibrary();
        for (int i = 0; i < (int)lib.size(); ++i) {
            bool tex = !lib[i].texture.isEmpty();
            if ((src == 2) != tex) continue;
            Material m;
            m.name = lib[i].name;
            m.color = lib[i].color;
            m.alpha = lib[i].alpha;
            if (tex) {
                QImage img = proceduralTexture(lib[i].texture, 64);
                QByteArray png;
                QBuffer b(&png);
                b.open(QIODevice::WriteOnly);
                img.save(&b, "PNG");
                m.hasTexture = true;
                m.texturePng = png;
            }
            auto* it = new QListWidgetItem(swatch(m), m.name);
            it->setData(Qt::UserRole, i);
            list_->addItem(it);
        }
    }
}

void MaterialsPanel::editMaterial(int mi) {
    if (mi < 0 || mi >= (int)doc_->model.materials.size()) return;
    Material M = doc_->model.materials[mi];
    QDialog dlg(this);
    dlg.setWindowTitle("Edit Material");
    auto* form = new QFormLayout(&dlg);
    auto* name = new QLineEdit(M.name);
    form->addRow("Name:", name);
    auto* color = new ColorButton;
    color->setColor(M.color);
    form->addRow("Color:", color);
    auto* opacity = new QSlider(Qt::Horizontal);
    opacity->setRange(0, 100);
    opacity->setValue((int)std::lround(M.alpha * 100));
    form->addRow("Opacity:", opacity);
    auto* texRow = new QWidget;
    auto* tl = new QHBoxLayout(texRow);
    tl->setContentsMargins(0, 0, 0, 0);
    auto* texLabel = new QLabel(M.hasTexture ? "Texture loaded" : "No texture");
    auto* load = new QPushButton("Load…");
    auto* clear = new QPushButton("Clear");
    tl->addWidget(texLabel, 1); tl->addWidget(load); tl->addWidget(clear);
    form->addRow("Texture:", texRow);
    auto* tw = new QLineEdit(formatLength(M.texW, doc_->model.units));
    auto* th = new QLineEdit(formatLength(M.texH, doc_->model.units));
    form->addRow("Texture width:", tw);
    form->addRow("Texture height:", th);
    auto* colorize = new QCheckBox("Colorize texture with color");
    colorize->setChecked(M.colorize);
    form->addRow("", colorize);
    connect(load, &QPushButton::clicked, &dlg, [&] {
        QString fn = QFileDialog::getOpenFileName(&dlg, "Texture image", QString(), "Images (*.png *.jpg *.jpeg *.bmp *.tif *.tiff *.webp)");
        if (fn.isEmpty()) return;
        QImage img(fn);
        if (img.isNull()) return;
        QByteArray png;
        QBuffer b(&png);
        b.open(QIODevice::WriteOnly);
        img.save(&b, "PNG");
        M.texturePng = png;
        M.hasTexture = true;
        double w;
        if (!parseLength(tw->text(), doc_->model.units, w) || w <= 0) w = 1000;
        th->setText(formatLength(w * img.height() / std::max(1, img.width()), doc_->model.units));
        texLabel->setText(QFileInfo(fn).fileName());
    });
    connect(clear, &QPushButton::clicked, &dlg, [&] { M.hasTexture = false; M.texturePng.clear(); texLabel->setText("No texture"); });
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    M.name = name->text().isEmpty() ? M.name : name->text();
    M.color = color->color();
    M.color.setAlpha(255);
    M.alpha = opacity->value() / 100.0;
    double w, h;
    if (parseLength(tw->text(), doc_->model.units, w) && w > 0) M.texW = w;
    if (parseLength(th->text(), doc_->model.units, h) && h > 0) M.texH = h;
    M.colorize = colorize->isChecked();
    M.rev = nextRevision();
    doc_->begin("Edit Material");
    doc_->model.materials[mi] = M;
    // material alpha changes batching: touch everything
    doc_->model.root.touch();
    for (auto& d : doc_->model.defs) d.ents.touch();
    doc_->commit();
    setCurrent(mi);
}

// ======================================================================= Components

ComponentsPanel::ComponentsPanel(Document* d, QWidget* parent) : QWidget(parent), doc_(d) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    showGroups_ = new QCheckBox("Show groups");
    lay->addWidget(showGroups_);
    list_ = new QListWidget;
    list_->setMinimumHeight(160);
    lay->addWidget(list_);
    auto* row = new QHBoxLayout;
    auto* place = new QPushButton("Place");
    auto* rename = new QPushButton("Rename");
    auto* purge = new QPushButton("Purge unused");
    row->addWidget(place); row->addWidget(rename); row->addWidget(purge);
    lay->addLayout(row);
    auto current = [this]() { auto* it = list_->currentItem(); return it ? it->data(Qt::UserRole).toInt() : -1; };
    connect(list_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* it) { emit placeComponent(it->data(Qt::UserRole).toInt()); });
    connect(place, &QPushButton::clicked, this, [this, current] { if (current() >= 0) emit placeComponent(current()); });
    connect(rename, &QPushButton::clicked, this, [this, current] {
        int d = current();
        if (d < 0) return;
        bool ok;
        QString n = QInputDialog::getText(this, "Rename", "Definition name:", QLineEdit::Normal, doc_->model.defs[d].name, &ok);
        if (!ok || n.isEmpty()) return;
        doc_->begin("Rename Definition");
        doc_->model.defs[d].name = n;
        doc_->commit();
    });
    connect(purge, &QPushButton::clicked, this, [this] {
        doc_->begin("Purge Components");
        doc_->model.purgeUnused();
        doc_->commit();
    });
    connect(showGroups_, &QCheckBox::toggled, this, &ComponentsPanel::refresh);
    connect(d, &Document::changed, this, &ComponentsPanel::refresh);
    refresh();
}

void ComponentsPanel::refresh() {
    list_->clear();
    for (int i = 0; i < (int)doc_->model.defs.size(); ++i) {
        const ComponentDef& d = doc_->model.defs[i];
        if (!d.alive || (d.isGroup && !showGroups_->isChecked())) continue;
        int n = doc_->model.instanceCount(i);
        auto* it = new QListWidgetItem(toolIcon(d.isGroup ? "group" : "component"), QString("%1   (%2)").arg(d.name).arg(n));
        it->setData(Qt::UserRole, i);
        it->setToolTip(d.description);
        list_->addItem(it);
    }
}

// ======================================================================= Styles

StylesPanel::StylesPanel(Document* d, QWidget* parent) : QWidget(parent), doc_(d) {
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    preset_ = new QComboBox;
    preset_->addItems({"(custom)", "Default", "Architectural Design", "Simple Style", "Shaded with Textures", "Hidden Line",
                       "Blueprint", "Monochrome", "Sketchy Profiles", "X-Ray Review"});
    form->addRow("Preset:", preset_);
    faceStyle_ = new QComboBox;
    faceStyle_->addItems({"Wireframe", "Hidden Line", "Shaded", "Shaded with Textures", "Monochrome"});
    form->addRow("Faces:", faceStyle_);
    xray_ = new QCheckBox("X-ray");
    form->addRow("", xray_);
    edges_ = new QCheckBox("Edges");
    backEdges_ = new QCheckBox("Back edges");
    profiles_ = new QCheckBox("Profiles");
    profileW_ = new QSpinBox;
    profileW_->setRange(1, 12);
    extension_ = new QCheckBox("Extension");
    byAxis_ = new QCheckBox("Color by axis");
    byTag_ = new QCheckBox("Color by tag");
    auto* er = new QWidget;
    auto* eg = new QGridLayout(er);
    eg->setContentsMargins(0, 0, 0, 0);
    eg->addWidget(edges_, 0, 0); eg->addWidget(backEdges_, 0, 1);
    eg->addWidget(profiles_, 1, 0); eg->addWidget(profileW_, 1, 1);
    eg->addWidget(extension_, 2, 0); eg->addWidget(byAxis_, 2, 1);
    eg->addWidget(byTag_, 3, 0);
    form->addRow("Edges:", er);
    edgeC_ = new ColorButton;
    front_ = new ColorButton;
    back_ = new ColorButton;
    bg_ = new ColorButton;
    skyC_ = new ColorButton;
    groundC_ = new ColorButton;
    sky_ = new QCheckBox("Sky");
    ground_ = new QCheckBox("Ground");
    guides_ = new QCheckBox("Guides");
    axes_ = new QCheckBox("Axes");
    form->addRow("Edge color:", edgeC_);
    form->addRow("Front face:", front_);
    form->addRow("Back face:", back_);
    form->addRow("Background:", bg_);
    auto* sr = new QWidget;
    auto* sl = new QHBoxLayout(sr);
    sl->setContentsMargins(0, 0, 0, 0);
    sl->addWidget(sky_); sl->addWidget(skyC_); sl->addWidget(ground_); sl->addWidget(groundC_);
    form->addRow("", sr);
    auto* gr = new QWidget;
    auto* gl = new QHBoxLayout(gr);
    gl->setContentsMargins(0, 0, 0, 0);
    gl->addWidget(guides_); gl->addWidget(axes_);
    form->addRow("Show:", gr);

    auto changed = [this] {
        if (updating_) return;
        Style& s = doc_->model.style;
        s.faceStyle = (FaceStyle)faceStyle_->currentIndex();
        s.xray = xray_->isChecked();
        s.edges = edges_->isChecked();
        s.backEdges = backEdges_->isChecked();
        s.profiles = profiles_->isChecked();
        s.profileWidth = profileW_->value();
        s.extension = extension_->isChecked();
        s.colorByAxis = byAxis_->isChecked();
        s.colorByTag = byTag_->isChecked();
        s.edgeColor = edgeC_->color();
        s.frontColor = front_->color();
        s.backColor = back_->color();
        s.background = bg_->color();
        s.sky = skyC_->color();
        s.ground = groundC_->color();
        s.showSky = sky_->isChecked();
        s.showGround = ground_->isChecked();
        s.showGuides = guides_->isChecked();
        s.showAxes = axes_->isChecked();
        doc_->modified = true;
        updating_ = true;
        preset_->setCurrentIndex(0);
        updating_ = false;
        emit styleChanged();
    };
    for (auto* c : {xray_, edges_, backEdges_, profiles_, extension_, byAxis_, byTag_, sky_, ground_, guides_, axes_})
        connect(c, &QCheckBox::toggled, this, changed);
    connect(faceStyle_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, changed);
    connect(profileW_, QOverload<int>::of(&QSpinBox::valueChanged), this, changed);
    for (auto* c : {edgeC_, front_, back_, bg_, skyC_, groundC_}) connect(c, &ColorButton::colorChanged, this, changed);
    connect(preset_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int i) {
        if (updating_ || i <= 0) return;
        Style s;
        switch (i) {
        case 1: break;
        case 2: s.profileWidth = 2; s.background = QColor(250, 250, 248); s.sky = QColor(225, 232, 240); s.frontColor = QColor(250, 250, 250); break;
        case 3: s.profiles = false; s.showSky = false; break;
        case 4: s.faceStyle = FaceStyle::ShadedTextured; break;
        case 5: s.faceStyle = FaceStyle::HiddenLine; s.showSky = false; break;
        case 6: s.faceStyle = FaceStyle::HiddenLine; s.background = QColor(30, 70, 140); s.edgeColor = QColor(235, 240, 255); s.showSky = false; s.profiles = true; break;
        case 7: s.faceStyle = FaceStyle::Monochrome; s.showSky = false; break;
        case 8: s.profileWidth = 4; s.extension = true; s.frontColor = QColor(255, 252, 240); s.background = QColor(255, 253, 245); s.showSky = false; break;
        case 9: s.xray = true; s.backEdges = false; break;
        }
        s.name = preset_->currentText();
        Style& cur = doc_->model.style;
        s.showHidden = cur.showHidden;
        s.showSectionPlanes = cur.showSectionPlanes;
        s.showSectionCuts = cur.showSectionCuts;
        s.fog = cur.fog; s.fogStart = cur.fogStart; s.fogEnd = cur.fogEnd; s.fogColor = cur.fogColor;
        cur = s;
        doc_->modified = true;
        refresh();
        updating_ = true;
        preset_->setCurrentIndex(i);
        updating_ = false;
        emit styleChanged();
    });
    connect(d, &Document::changed, this, &StylesPanel::refresh);
    connect(d, &Document::fileChanged, this, &StylesPanel::refresh);
    refresh();
}

void StylesPanel::refresh() {
    updating_ = true;
    const Style& s = doc_->model.style;
    faceStyle_->setCurrentIndex((int)s.faceStyle);
    xray_->setChecked(s.xray);
    edges_->setChecked(s.edges);
    backEdges_->setChecked(s.backEdges);
    profiles_->setChecked(s.profiles);
    profileW_->setValue(s.profileWidth);
    extension_->setChecked(s.extension);
    byAxis_->setChecked(s.colorByAxis);
    byTag_->setChecked(s.colorByTag);
    edgeC_->setColor(s.edgeColor);
    front_->setColor(s.frontColor);
    back_->setColor(s.backColor);
    bg_->setColor(s.background);
    skyC_->setColor(s.sky);
    groundC_->setColor(s.ground);
    sky_->setChecked(s.showSky);
    ground_->setChecked(s.showGround);
    guides_->setChecked(s.showGuides);
    axes_->setChecked(s.showAxes);
    updating_ = false;
}

// ======================================================================= Tags

TagsPanel::TagsPanel(Document* d, QWidget* parent) : QWidget(parent), doc_(d) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton("+ Tag");
    auto* del = new QPushButton("Delete");
    auto* assign = new QPushButton("Assign selection");
    row->addWidget(add); row->addWidget(del); row->addWidget(assign);
    lay->addLayout(row);
    tree_ = new QTreeWidget;
    tree_->setColumnCount(3);
    tree_->setHeaderLabels({"Name", "", "Color"});
    tree_->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    tree_->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    tree_->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    tree_->setRootIsDecorated(false);
    tree_->setMinimumHeight(140);
    lay->addWidget(tree_);
    connect(add, &QPushButton::clicked, this, [this] {
        doc_->begin("Add Tag");
        int t = doc_->addTag(QString("Tag %1").arg(doc_->model.tags.size()));
        doc_->commit();
        refresh();
        tree_->setCurrentItem(tree_->topLevelItem(t));
        tree_->editItem(tree_->topLevelItem(t), 0);
    });
    connect(del, &QPushButton::clicked, this, [this] {
        auto* it = tree_->currentItem();
        if (!it) return;
        int t = it->data(0, Qt::UserRole).toInt();
        if (t <= 0) return;
        auto r = QMessageBox::question(this, "Delete Tag", "Move tagged entities to Untagged?\n(No = delete the entities too)",
                                       QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (r == QMessageBox::Cancel) return;
        doc_->begin("Delete Tag");
        doc_->deleteTag(t, r == QMessageBox::Yes);
        doc_->commit();
    });
    connect(assign, &QPushButton::clicked, this, [this] {
        auto* it = tree_->currentItem();
        if (!it) return;
        doc_->begin("Assign Tag");
        doc_->setTagOnSelection(it->data(0, Qt::UserRole).toInt());
        doc_->commit();
    });
    connect(tree_, &QTreeWidget::itemChanged, this, [this](QTreeWidgetItem* it, int col) {
        if (updating_) return;
        int t = it->data(0, Qt::UserRole).toInt();
        Tag& T = doc_->model.tags[t];
        if (col == 0 && t > 0 && it->text(0) != T.name && !it->text(0).isEmpty()) { T.name = it->text(0); doc_->modified = true; }
        if (col == 1) {
            T.visible = it->checkState(1) == Qt::Checked;
            doc_->modified = true;
            doc_->notifyView();
            doc_->model.root.touch();
            for (auto& d : doc_->model.defs) d.ents.touch();
            doc_->notifyChanged();
        }
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it, int col) {
        int t = it->data(0, Qt::UserRole).toInt();
        if (col == 2) {
            QColor c = QColorDialog::getColor(doc_->model.tags[t].color, this, "Tag color");
            if (c.isValid()) { doc_->model.tags[t].color = c; doc_->notifyChanged(); }
        } else if (col == 0 && t > 0) tree_->editItem(it, 0);
    });
    connect(d, &Document::changed, this, &TagsPanel::refresh);
    refresh();
}

void TagsPanel::refresh() {
    updating_ = true;
    tree_->clear();
    for (int i = 0; i < (int)doc_->model.tags.size(); ++i) {
        const Tag& t = doc_->model.tags[i];
        auto* it = new QTreeWidgetItem(tree_);
        it->setText(0, t.name);
        it->setData(0, Qt::UserRole, i);
        it->setCheckState(1, t.visible ? Qt::Checked : Qt::Unchecked);
        it->setToolTip(1, "Visible");
        QPixmap pm(16, 16);
        pm.fill(t.color);
        it->setIcon(2, QIcon(pm));
        if (i > 0) it->setFlags(it->flags() | Qt::ItemIsEditable);
    }
    updating_ = false;
}

// ======================================================================= Scenes

ScenesPanel::ScenesPanel(Document* d, Viewport* v, QWidget* parent) : QWidget(parent), doc_(d), view_(v) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    auto* row = new QHBoxLayout;
    auto* add = new QPushButton("+");
    add->setToolTip("Add scene");
    auto* upd = new QPushButton("Update");
    auto* del = new QPushButton("Delete");
    auto* up = new QPushButton("▲");
    auto* dn = new QPushButton("▼");
    for (auto* b : {add, up, dn}) b->setMaximumWidth(34);
    row->addWidget(add); row->addWidget(upd); row->addWidget(del); row->addWidget(up); row->addWidget(dn);
    lay->addLayout(row);
    list_ = new QListWidget;
    list_->setMinimumHeight(110);
    lay->addWidget(list_);
    auto* form = new QFormLayout;
    name_ = new QLineEdit;
    desc_ = new QPlainTextEdit;
    desc_->setMaximumHeight(54);
    form->addRow("Name:", name_);
    form->addRow("Description:", desc_);
    lay->addLayout(form);
    connect(add, &QPushButton::clicked, this, &ScenesPanel::addScene);
    connect(upd, &QPushButton::clicked, this, [this] { updateScene(list_->currentRow()); });
    connect(del, &QPushButton::clicked, this, [this] {
        int i = list_->currentRow();
        if (i < 0) return;
        doc_->begin("Delete Scene");
        doc_->model.scenes.erase(doc_->model.scenes.begin() + i);
        doc_->commit();
        refresh();
        emit scenesChanged();
    });
    auto moveBy = [this](int dlt) {
        int i = list_->currentRow(), j = i + dlt;
        auto& sc = doc_->model.scenes;
        if (i < 0 || j < 0 || j >= (int)sc.size()) return;
        std::swap(sc[i], sc[j]);
        doc_->modified = true;
        refresh();
        list_->setCurrentRow(j);
        emit scenesChanged();
    };
    connect(up, &QPushButton::clicked, this, [moveBy] { moveBy(-1); });
    connect(dn, &QPushButton::clicked, this, [moveBy] { moveBy(1); });
    connect(list_, &QListWidget::currentRowChanged, this, [this](int i) {
        if (updating_ || i < 0) return;
        updating_ = true;
        name_->setText(doc_->model.scenes[i].name);
        desc_->setPlainText(doc_->model.scenes[i].description);
        updating_ = false;
    });
    connect(list_, &QListWidget::itemClicked, this, [this](QListWidgetItem*) { applyScene(list_->currentRow()); });
    connect(name_, &QLineEdit::editingFinished, this, [this] {
        int i = list_->currentRow();
        if (i < 0 || updating_) return;
        doc_->model.scenes[i].name = name_->text();
        doc_->modified = true;
        list_->item(i)->setText(name_->text());
        emit scenesChanged();
    });
    connect(desc_, &QPlainTextEdit::textChanged, this, [this] {
        int i = list_->currentRow();
        if (i < 0 || updating_) return;
        doc_->model.scenes[i].description = desc_->toPlainText();
        doc_->modified = true;
    });
    connect(d, &Document::fileChanged, this, &ScenesPanel::refresh);
    refresh();
}

void ScenesPanel::refresh() {
    updating_ = true;
    int cur = list_->currentRow();
    list_->clear();
    for (auto& s : doc_->model.scenes) list_->addItem(s.name);
    if (cur >= 0 && cur < list_->count()) list_->setCurrentRow(cur);
    updating_ = false;
}

void ScenesPanel::addScene() {
    Scene s;
    s.name = QString("Scene %1").arg(doc_->model.scenes.size() + 1);
    doc_->model.scenes.push_back(s);
    updateScene((int)doc_->model.scenes.size() - 1);
    refresh();
    list_->setCurrentRow((int)doc_->model.scenes.size() - 1);
    emit scenesChanged();
}

void ScenesPanel::updateScene(int i) {
    if (i < 0 || i >= (int)doc_->model.scenes.size()) return;
    Scene& s = doc_->model.scenes[i];
    s.cam = doc_->model.camera;
    s.tagVisible.clear();
    for (auto& t : doc_->model.tags) s.tagVisible.push_back(t.visible);
    s.style = doc_->model.style;
    s.shadow = doc_->model.shadow;
    doc_->modified = true;
}

void ScenesPanel::applyScene(int i) {
    if (i < 0 || i >= (int)doc_->model.scenes.size()) return;
    const Scene& s = doc_->model.scenes[i];
    for (size_t k = 0; k < s.tagVisible.size() && k < doc_->model.tags.size(); ++k) doc_->model.tags[k].visible = s.tagVisible[k];
    if (s.hasStyle) doc_->model.style = s.style;
    doc_->model.shadow = s.shadow;
    doc_->model.root.touch();
    for (auto& d : doc_->model.defs) d.ents.touch();
    view_->animateTo(s.cam, 900);
    doc_->notifyChanged();
}

// ======================================================================= Shadows

ShadowsPanel::ShadowsPanel(Document* d, QWidget* parent) : QWidget(parent), doc_(d) {
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    enabled_ = new QCheckBox("Show shadows");
    form->addRow("", enabled_);
    time_ = new QSlider(Qt::Horizontal);
    time_->setRange(4 * 60, 22 * 60);
    timeL_ = new QLabel;
    auto* tr = new QWidget;
    auto* tl = new QHBoxLayout(tr);
    tl->setContentsMargins(0, 0, 0, 0);
    tl->addWidget(time_, 1); tl->addWidget(timeL_);
    form->addRow("Time:", tr);
    date_ = new QSlider(Qt::Horizontal);
    date_->setRange(1, 365);
    dateL_ = new QLabel;
    auto* dr = new QWidget;
    auto* dl = new QHBoxLayout(dr);
    dl->setContentsMargins(0, 0, 0, 0);
    dl->addWidget(date_, 1); dl->addWidget(dateL_);
    form->addRow("Date:", dr);
    utc_ = new QDoubleSpinBox; utc_->setRange(-12, 14); utc_->setSingleStep(0.5); utc_->setPrefix("UTC ");
    form->addRow("Time zone:", utc_);
    lat_ = new QDoubleSpinBox; lat_->setRange(-90, 90); lat_->setDecimals(4);
    lon_ = new QDoubleSpinBox; lon_->setRange(-180, 180); lon_->setDecimals(4);
    north_ = new QDoubleSpinBox; north_->setRange(-360, 360); north_->setSuffix("°");
    form->addRow("Latitude:", lat_);
    form->addRow("Longitude:", lon_);
    form->addRow("North angle:", north_);
    light_ = new QSlider(Qt::Horizontal); light_->setRange(0, 100);
    dark_ = new QSlider(Qt::Horizontal); dark_->setRange(0, 100);
    form->addRow("Light:", light_);
    form->addRow("Dark:", dark_);
    sunShading_ = new QCheckBox("Use sun for shading");
    onFaces_ = new QCheckBox("On faces");
    onGround_ = new QCheckBox("On ground");
    form->addRow("", sunShading_);
    auto* dsp = new QWidget;
    auto* dspl = new QHBoxLayout(dsp);
    dspl->setContentsMargins(0, 0, 0, 0);
    dspl->addWidget(onFaces_); dspl->addWidget(onGround_);
    form->addRow("Display:", dsp);
    auto changed = [this] {
        if (updating_) return;
        ShadowSettings& s = doc_->model.shadow;
        s.enabled = enabled_->isChecked();
        s.hour = time_->value() / 60.0;
        QDate dd = QDate(2026, 1, 1).addDays(date_->value() - 1);
        s.month = dd.month();
        s.day = dd.day();
        s.utcOffset = utc_->value();
        s.latitude = lat_->value();
        s.longitude = lon_->value();
        s.northAngle = north_->value();
        s.light = light_->value();
        s.dark = dark_->value();
        s.sunForShading = sunShading_->isChecked();
        s.onFaces = onFaces_->isChecked();
        s.onGround = onGround_->isChecked();
        timeL_->setText(QString("%1:%2").arg((int)s.hour, 2, 10, QChar('0')).arg(time_->value() % 60, 2, 10, QChar('0')));
        dateL_->setText(dd.toString("d MMM"));
        doc_->modified = true;
        doc_->notifyView();
    };
    for (auto* c : {enabled_, sunShading_, onFaces_, onGround_}) connect(c, &QCheckBox::toggled, this, changed);
    for (auto* s : {time_, date_, light_, dark_}) connect(s, &QSlider::valueChanged, this, changed);
    for (auto* s : {utc_, lat_, lon_, north_}) connect(s, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, changed);
    connect(d, &Document::fileChanged, this, &ShadowsPanel::refresh);
    connect(d, &Document::changed, this, &ShadowsPanel::refresh);
    refresh();
}

void ShadowsPanel::refresh() {
    updating_ = true;
    const ShadowSettings& s = doc_->model.shadow;
    enabled_->setChecked(s.enabled);
    time_->setValue((int)std::lround(s.hour * 60));
    QDate dd(2026, std::clamp(s.month, 1, 12), std::clamp(s.day, 1, 28));
    if (QDate::isValid(2026, s.month, s.day)) dd = QDate(2026, s.month, s.day);
    date_->setValue(dd.dayOfYear());
    utc_->setValue(s.utcOffset);
    lat_->setValue(s.latitude);
    lon_->setValue(s.longitude);
    north_->setValue(s.northAngle);
    light_->setValue((int)s.light);
    dark_->setValue((int)s.dark);
    sunShading_->setChecked(s.sunForShading);
    onFaces_->setChecked(s.onFaces);
    onGround_->setChecked(s.onGround);
    timeL_->setText(QString("%1:%2").arg((int)s.hour, 2, 10, QChar('0')).arg((int)std::lround(s.hour * 60) % 60, 2, 10, QChar('0')));
    dateL_->setText(dd.toString("d MMM"));
    updating_ = false;
}

// ======================================================================= Fog

FogPanel::FogPanel(Document* d, QWidget* parent) : QWidget(parent), doc_(d) {
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    enabled_ = new QCheckBox("Display fog");
    form->addRow("", enabled_);
    start_ = new QSlider(Qt::Horizontal); start_->setRange(0, 100);
    end_ = new QSlider(Qt::Horizontal); end_->setRange(1, 200);
    color_ = new ColorButton;
    form->addRow("Start:", start_);
    form->addRow("End:", end_);
    form->addRow("Color:", color_);
    auto changed = [this] {
        if (updating_) return;
        Style& s = doc_->model.style;
        s.fog = enabled_->isChecked();
        s.fogStart = start_->value() / 100.0;
        s.fogEnd = end_->value() / 100.0;
        s.fogColor = color_->color();
        doc_->modified = true;
        doc_->notifyView();
    };
    connect(enabled_, &QCheckBox::toggled, this, changed);
    connect(start_, &QSlider::valueChanged, this, changed);
    connect(end_, &QSlider::valueChanged, this, changed);
    connect(color_, &ColorButton::colorChanged, this, changed);
    connect(d, &Document::fileChanged, this, &FogPanel::refresh);
    refresh();
}

void FogPanel::refresh() {
    updating_ = true;
    const Style& s = doc_->model.style;
    enabled_->setChecked(s.fog);
    start_->setValue((int)(s.fogStart * 100));
    end_->setValue((int)(s.fogEnd * 100));
    color_->setColor(s.fogColor);
    updating_ = false;
}

// ======================================================================= Outliner

OutlinerPanel::OutlinerPanel(Document* d, QWidget* parent) : QWidget(parent), doc_(d) {
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    filter_ = new QLineEdit;
    filter_->setPlaceholderText("Filter");
    filter_->setClearButtonEnabled(true);
    lay->addWidget(filter_);
    tree_ = new QTreeWidget;
    tree_->setHeaderHidden(true);
    tree_->setMinimumHeight(160);
    lay->addWidget(tree_);
    connect(filter_, &QLineEdit::textChanged, this, &OutlinerPanel::refresh);
    connect(d, &Document::changed, this, &OutlinerPanel::refresh);
    connect(d, &Document::contextChanged, this, &OutlinerPanel::refresh);
    connect(tree_, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
        QList<int> path = it->data(0, Qt::UserRole).value<QList<int>>();
        if (path.isEmpty()) return;
        InstPath parent(path.begin(), path.end() - 1);
        updating_ = true;
        if (doc_->editPath != parent) {
            doc_->editPath = parent;
            emit doc_->contextChanged();
        }
        doc_->setSelection({{EntKind::Instance, path.back()}});
        updating_ = false;
    });
    connect(tree_, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* it) {
        QList<int> path = it->data(0, Qt::UserRole).value<QList<int>>();
        if (path.isEmpty()) return;
        InstPath parent(path.begin(), path.end() - 1);
        doc_->editPath = parent;
        doc_->openInstance(path.back());
    });
    refresh();
}

void OutlinerPanel::refresh() {
    if (updating_) return;
    tree_->clear();
    QString flt = filter_->text().trimmed();
    std::function<void(const Entities&, QTreeWidgetItem*, QList<int>, int)> add = [&](const Entities& E, QTreeWidgetItem* parent, QList<int> path, int depth) {
        if (depth > 32) return;
        for (int i = 0; i < (int)E.insts.size(); ++i) {
            const Instance& in = E.insts[i];
            if (!in.alive || in.def < 0) continue;
            const ComponentDef& d = doc_->model.defs[in.def];
            QString label = in.name.isEmpty() ? (d.isGroup ? (d.name.startsWith("Group") ? QString("Group") : d.name) : QString("<%1>").arg(d.name))
                                              : (d.isGroup ? in.name : QString("%1 <%2>").arg(in.name, d.name));
            QList<int> p2 = path;
            p2.append(i);
            auto* it = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(tree_);
            it->setText(0, label + (in.hidden ? "  (hidden)" : "") + (in.locked ? "  🔒" : ""));
            it->setIcon(0, toolIcon(d.isGroup ? "group" : "component"));
            it->setData(0, Qt::UserRole, QVariant::fromValue(p2));
            add(d.ents, it, p2, depth + 1);
            if (!flt.isEmpty() && !label.contains(flt, Qt::CaseInsensitive) && it->childCount() == 0) delete it;
            else it->setExpanded(depth < 2);
        }
    };
    add(doc_->model.root, nullptr, {}, 0);
}

// ======================================================================= Soften edges

SoftenPanel::SoftenPanel(Document* d, QWidget* parent) : QWidget(parent), doc_(d) {
    auto* form = new QFormLayout(this);
    form->setContentsMargins(0, 0, 0, 0);
    angle_ = new QSlider(Qt::Horizontal);
    angle_->setRange(0, 180);
    angle_->setValue(20);
    angleL_ = new QLabel("20°");
    auto* r = new QWidget;
    auto* rl = new QHBoxLayout(r);
    rl->setContentsMargins(0, 0, 0, 0);
    rl->addWidget(angle_, 1); rl->addWidget(angleL_);
    form->addRow("Angle between normals:", r);
    smooth_ = new QCheckBox("Smooth normals");
    smooth_->setChecked(true);
    coplanar_ = new QCheckBox("Soften coplanar");
    coplanar_->setChecked(true);
    form->addRow("", smooth_);
    form->addRow("", coplanar_);
    auto* apply = new QPushButton("Apply to selection");
    form->addRow("", apply);
    connect(angle_, &QSlider::valueChanged, this, [this](int v) { angleL_->setText(QString("%1°").arg(v)); });
    connect(apply, &QPushButton::clicked, this, &SoftenPanel::apply);
}

void SoftenPanel::apply() {
    Entities& E = doc_->active();
    std::set<int> edges;
    for (auto& r : doc_->sel) {
        if (r.kind == EntKind::Edge) edges.insert(r.id);
        if (r.kind == EntKind::Face) for (int e : E.faceEdges(r.id)) edges.insert(e);
    }
    if (edges.empty()) return;
    double lim = std::cos(angle_->value() * kPi / 180);
    doc_->begin("Soften Edges");
    for (int e : edges) {
        Edge& Ed = E.edges[e];
        if (!Ed.alive || Ed.faces.size() != 2) continue;
        double c = E.faces[Ed.faces[0]].plane.n.dot(E.faces[Ed.faces[1]].plane.n);
        bool coplanar = c > 1 - 1e-9;
        bool soft = c >= lim && (!coplanar || coplanar_->isChecked());
        Ed.soft = soft;
        Ed.smooth = soft && smooth_->isChecked();
    }
    E.touch();
    doc_->commit();
}

}  // namespace gable
