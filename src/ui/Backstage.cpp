#include "ui/Backstage.h"
#include "ui/Icons.h"
#include "ui/Theme.h"
#include <QDateTime>
#include <QDir>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace gable {

static QLabel* label(const QString& t, const char* prop) {
    auto* l = new QLabel(t);
    l->setProperty(prop, true);
    return l;
}

static QString when(const QDateTime& d) {
    QDateTime now = QDateTime::currentDateTime();
    if (d.date() == now.date()) return d.toString("h:mm AP");
    if (d.daysTo(now) < 7) return d.toString("dddd");
    return d.toString("M/d/yyyy");
}

Backstage::Backstage(const BackstageHost& host, QWidget* parent) : QWidget(parent), host_(host) {
    setObjectName("folioBackstage");
    setAttribute(Qt::WA_StyledBackground);
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);
    auto* navW = new QWidget;
    navW->setObjectName("bsNav");
    navW->setAttribute(Qt::WA_StyledBackground);
    navW->setFixedWidth(180);
    auto* nv = new QVBoxLayout(navW);
    nv->setContentsMargins(0, 6, 0, 8);
    nv->setSpacing(0);
    struct E { const char* key; const char* label; };
    const E entries[] = {{"back", ""}, {"home", "Home"}, {"new", "New"}, {"open", "Open"}, {nullptr, nullptr},
                         {"info", "Info"}, {"save", "Save"}, {"saveas", "Save As"}, {"import", "Import"},
                         {"print", "Print"}, {"export", "Export"}, {"close", "Close"}, {"spacer", ""}, {"options", "Options"}};
    for (auto& e : entries) {
        if (!e.key) {
            auto* s = new QFrame;
            s->setProperty("bsSep", true);
            nv->addWidget(s);
            continue;
        }
        QString key = e.key;
        if (key == "spacer") { nv->addStretch(1); continue; }
        auto* b = new QPushButton(e.label);
        b->setFocusPolicy(Qt::NoFocus);
        b->setCursor(Qt::PointingHandCursor);
        if (key == "back") {
            b->setObjectName("bsBack");
            b->setIcon(theme::whiteIcon("undo", 18));
            b->setIconSize(QSize(18, 18));
            b->setToolTip("Back (Esc)");
        }
        connect(b, &QPushButton::clicked, this, [this, key] { nav(key); });
        nv->addWidget(b);
        nav_[key] = b;
    }
    h->addWidget(navW);
    stack_ = new QStackedWidget;
    stack_->setObjectName("bsBody");
    h->addWidget(stack_, 1);
    hide();
}

void Backstage::open(const QString& pageKey) {
    show();
    raise();
    setFocus();
    nav(pageKey);
}

void Backstage::close() {
    hide();
    emit closed();
}

void Backstage::run(std::function<void()> fn) {
    close();
    if (fn) fn();
}

void Backstage::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { close(); return; }
    QWidget::keyPressEvent(e);
}

void Backstage::nav(const QString& key) {
    if (key == "back") { close(); return; }
    if (key == "save") { run(host_.save); return; }
    if (key == "close") { run(host_.closeModel); return; }
    if (key == "options") { run(host_.options); return; }
    if (key == "import") { run(host_.importFile); return; }
    current_ = key;
    for (auto& [k, b] : nav_) {
        b->setProperty("active", k == key);
        b->style()->unpolish(b);
        b->style()->polish(b);
    }
    while (stack_->count()) {
        QWidget* w = stack_->widget(0);
        stack_->removeWidget(w);
        w->deleteLater();
    }
    QWidget* p = page(key);
    auto* sc = new QScrollArea;
    sc->setObjectName("bsScroll");
    sc->setFrameShape(QFrame::NoFrame);
    sc->setWidgetResizable(true);
    sc->setWidget(p);
    stack_->addWidget(sc);
    stack_->setCurrentWidget(sc);
}

QWidget* Backstage::page(const QString& key) {
    if (key == "new") return pageNew();
    if (key == "open") return pageOpen();
    if (key == "info") return pageInfo();
    if (key == "saveas") return pageSaveAs();
    if (key == "print") return pagePrint();
    if (key == "export") return pageExport();
    return pageHome();
}

QWidget* Backstage::frame(const QString& title, QVBoxLayout*& lay) {
    auto* w = new QWidget;
    w->setAttribute(Qt::WA_StyledBackground);
    w->setStyleSheet("background: #ffffff;");
    lay = new QVBoxLayout(w);
    lay->setContentsMargins(36, 28, 36, 28);
    lay->setSpacing(10);
    lay->addWidget(label(title, "bsTitle"));
    return w;
}

QWidget* Backstage::templateTile(int i, const ModelTemplate& t) {
    auto* b = new QToolButton;
    b->setProperty("bsTile", true);
    b->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    b->setCursor(Qt::PointingHandCursor);
    QPixmap pm(QSize(176, 124) * devicePixelRatioF());
    pm.setDevicePixelRatio(devicePixelRatioF());
    pm.fill(Qt::transparent);
    {
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor(0xc8, 0xc8, 0xc8));
        p.setBrush(Qt::white);
        p.drawRect(QRectF(0.5, 0.5, 175, 123));
        // ground grid and axes, like an empty model
        p.setPen(QPen(QColor(0xe6, 0xe6, 0xe6), 1));
        for (int k = 0; k < 7; ++k) {
            p.drawLine(QPointF(20 + k * 20, 104), QPointF(60 + k * 20, 74));
        }
        p.setPen(QPen(QColor(0xd9, 0x34, 0x2b), 1.2));
        p.drawLine(QPointF(60, 96), QPointF(160, 70));
        p.setPen(QPen(QColor(0x2e, 0x9a, 0x45), 1.2));
        p.drawLine(QPointF(60, 96), QPointF(16, 66));
        p.setPen(QPen(QColor(0x2f, 0x5f, 0xd0), 1.2));
        p.drawLine(QPointF(60, 96), QPointF(60, 14));
        static const char* glyphs[] = {"", "", "", "", "view_iso", "tape", "solid_outer"};
        if (i >= 0 && i < 7 && *glyphs[i]) p.drawPixmap(QPointF(130, 12), theme::pixmap(glyphs[i], 32, devicePixelRatioF()));
        p.setPen(QColor(0x4b, 0x2a, 0x78));
        QFont f = p.font();
        f.setPointSizeF(8);
        p.setFont(f);
        p.drawText(QRectF(8, 104, 160, 18), Qt::AlignLeft | Qt::AlignVCenter, t.note);
    }
    b->setIcon(QIcon(pm));
    b->setIconSize(QSize(176, 124));
    b->setText(t.name);
    connect(b, &QToolButton::clicked, this, [this, i] { run([this, i] { host_.newFromTemplate(i); }); });
    return b;
}

QTreeWidget* Backstage::recentList(int limit, const QString& filter) {
    auto* t = new QTreeWidget;
    t->setObjectName("bsRecent");
    t->setColumnCount(3);
    t->setHeaderLabels({"Name", "Location", "Date modified"});
    t->setRootIsDecorated(false);
    t->setIconSize(QSize(48, 34));
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->header()->setStretchLastSection(false);
    t->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    t->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    t->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    t->setContextMenuPolicy(Qt::CustomContextMenu);
    int n = 0;
    for (const QString& p : host_.recent()) {
        QFileInfo fi(p);
        if (!filter.isEmpty() && !fi.fileName().contains(filter, Qt::CaseInsensitive)) continue;
        auto* it = new QTreeWidgetItem(t);
        it->setText(0, fi.fileName());
        it->setText(1, QDir::toNativeSeparators(fi.absolutePath()).replace(QDir::homePath(), "~"));
        it->setText(2, fi.exists() ? when(fi.lastModified()) : "Not found");
        it->setData(0, Qt::UserRole, p);
        QFont f = it->font(0);
        f.setBold(true);
        it->setFont(0, f);
        it->setForeground(1, QColor(0x6b, 0x6b, 0x6b));
        it->setForeground(2, QColor(0x6b, 0x6b, 0x6b));
        QPixmap th = host_.thumbnail ? host_.thumbnail(p) : QPixmap();
        if (th.isNull()) th = appIconPixmap(34);
        it->setIcon(0, QIcon(th));
        it->setToolTip(0, p);
        if (++n >= limit) break;
    }
    if (n == 0) {
        auto* it = new QTreeWidgetItem(t);
        it->setText(0, filter.isEmpty() ? "No recent models yet." : "No recent models match.");
        it->setFlags(Qt::NoItemFlags);
    }
    t->setMinimumHeight(std::min(12, std::max(n, 1)) * 44 + 30);
    connect(t, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem* it) {
        QString p = it->data(0, Qt::UserRole).toString();
        if (!p.isEmpty()) run([this, p] { host_.openPath(p); });
    });
    connect(t, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem* it) {
        QString p = it->data(0, Qt::UserRole).toString();
        if (!p.isEmpty()) run([this, p] { host_.openPath(p); });
    });
    connect(t, &QTreeWidget::customContextMenuRequested, this, [this, t](const QPoint& at) {
        auto* it = t->itemAt(at);
        if (!it) return;
        QString p = it->data(0, Qt::UserRole).toString();
        if (p.isEmpty()) return;
        QMenu m;
        m.addAction("Open", [this, p] { run([this, p] { host_.openPath(p); }); });
        m.addAction("Open File Location", [p] { QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(p).absolutePath())); });
        m.addAction("Remove from List", [this, p] { host_.forgetRecent(p); nav(current_); });
        m.exec(t->viewport()->mapToGlobal(at));
    });
    return t;
}

QPushButton* Backstage::bigButton(const QString& icon, const QString& title, const QString& desc, std::function<void()> fn) {
    auto* b = new QPushButton;
    b->setProperty("bsButton", true);
    b->setCursor(Qt::PointingHandCursor);
    b->setIcon(theme::icon(icon));
    b->setIconSize(QSize(32, 32));
    b->setText(desc.isEmpty() ? title : title + "\n" + desc);
    b->setFixedWidth(460);
    connect(b, &QPushButton::clicked, this, [this, fn] { run(fn); });
    return b;
}

QWidget* Backstage::pageHome() {
    int hr = QTime::currentTime().hour();
    QVBoxLayout* v;
    QWidget* w = frame(hr < 12 ? "Good morning" : (hr < 18 ? "Good afternoon" : "Good evening"), v);
    auto* strip = new QHBoxLayout;
    strip->setSpacing(14);
    auto tps = host_.templates();
    for (int i = 0; i < (int)tps.size() && i < 4; ++i) strip->addWidget(templateTile(i, tps[i]));
    auto* more = new QToolButton;
    more->setText("More templates →");
    more->setProperty("bsPlace", true);
    more->setCursor(Qt::PointingHandCursor);
    connect(more, &QToolButton::clicked, this, [this] { nav("new"); });
    strip->addWidget(more, 0, Qt::AlignBottom);
    strip->addStretch(1);
    v->addLayout(strip);
    v->addSpacing(8);
    auto* search = new QLineEdit;
    search->setPlaceholderText("Search");
    search->setFixedWidth(400);
    search->setClearButtonEnabled(true);
    v->addWidget(search);
    v->addWidget(label("Recent", "bsCard"));
    auto* holder = new QVBoxLayout;
    holder->addWidget(recentList(12, QString()));
    v->addLayout(holder);
    connect(search, &QLineEdit::textChanged, this, [this, holder](const QString& f) {
        while (auto* it = holder->takeAt(0)) { if (it->widget()) it->widget()->deleteLater(); delete it; }
        holder->addWidget(recentList(12, f));
    });
    v->addStretch(1);
    return w;
}

QWidget* Backstage::pageNew() {
    QVBoxLayout* v;
    QWidget* w = frame("New", v);
    v->addWidget(label("Choose a template. Units can be changed later in Model Info.", "dim"));
    auto* g = new QGridLayout;
    g->setHorizontalSpacing(14);
    g->setVerticalSpacing(14);
    auto tps = host_.templates();
    for (int i = 0; i < (int)tps.size(); ++i) g->addWidget(templateTile(i, tps[i]), i / 4, i % 4);
    g->setColumnStretch(4, 1);
    v->addLayout(g);
    v->addStretch(1);
    return w;
}

QWidget* Backstage::pageOpen() {
    QVBoxLayout* v;
    QWidget* w = frame("Open", v);
    auto* h = new QHBoxLayout;
    h->setSpacing(30);
    auto* places = new QVBoxLayout;
    places->setSpacing(2);
    auto place = [&](const QString& icon, const QString& text, std::function<void()> fn, bool checked) {
        auto* b = new QToolButton;
        b->setProperty("bsPlace", true);
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setIcon(theme::icon(icon));
        b->setIconSize(QSize(32, 32));
        b->setText("  " + text);
        b->setCheckable(true);
        b->setChecked(checked);
        b->setFixedWidth(240);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QToolButton::clicked, this, [b, fn] { b->setChecked(true); if (fn) fn(); });
        places->addWidget(b);
    };
    place("previous", "Recent", nullptr, true);
    place("open", "Browse", [this] { run(host_.openDialog); }, false);
    place("import", "Import a 3D model", [this] { run(host_.importFile); }, false);
    places->addStretch(1);
    h->addLayout(places);
    auto* right = new QVBoxLayout;
    auto* search = new QLineEdit;
    search->setPlaceholderText("Search");
    search->setClearButtonEnabled(true);
    right->addWidget(search);
    auto* holder = new QVBoxLayout;
    holder->addWidget(recentList(50, QString()));
    right->addLayout(holder, 1);
    connect(search, &QLineEdit::textChanged, this, [this, holder](const QString& f) {
        while (auto* it = holder->takeAt(0)) { if (it->widget()) it->widget()->deleteLater(); delete it; }
        holder->addWidget(recentList(50, f));
    });
    h->addLayout(right, 1);
    v->addLayout(h, 1);
    return w;
}

QWidget* Backstage::pageInfo() {
    QVBoxLayout* v;
    QWidget* w = frame("Info", v);
    QString path = host_.filePath();
    v->addWidget(label(path.isEmpty() ? "Untitled" : QFileInfo(path).fileName(), "bsSub"));
    if (!path.isEmpty()) v->addWidget(label(QDir::toNativeSeparators(QFileInfo(path).absolutePath()), "dim"));
    auto* h = new QHBoxLayout;
    h->setSpacing(40);
    auto* left = new QVBoxLayout;
    left->setSpacing(10);
    left->addWidget(bigButton("modelinfo", "Model Info", "Units, dimensions, text, geometry and statistics.", host_.modelInfo));
    left->addWidget(bigButton("delete", "Purge Unused", "Remove unused components, materials and tags.", host_.purge));
    left->addStretch(1);
    h->addLayout(left);
    auto* right = new QVBoxLayout;
    right->addWidget(label("Properties", "bsCard"));
    auto* g = new QGridLayout;
    g->setHorizontalSpacing(24);
    g->setVerticalSpacing(6);
    int r = 0;
    for (auto& [k, val] : host_.properties()) {
        auto* kl = new QLabel(k);
        kl->setProperty("dim", true);
        g->addWidget(kl, r, 0);
        g->addWidget(new QLabel(val), r, 1);
        ++r;
    }
    right->addLayout(g);
    right->addStretch(1);
    h->addLayout(right, 1);
    v->addLayout(h);
    v->addStretch(1);
    return w;
}

QWidget* Backstage::pageSaveAs() {
    QVBoxLayout* v;
    QWidget* w = frame("Save As", v);
    v->addWidget(bigButton("saveas", "Browse", "Choose a folder and name for this model (.gbl).", host_.saveAs));
    v->addSpacing(8);
    v->addWidget(label("Other formats", "bsCard"));
    v->addWidget(bigButton("export", "Export a 3D model", "STL, OBJ, COLLADA or DXF.", [this] { host_.exportModel(QString()); }));
    v->addStretch(1);
    return w;
}

QWidget* Backstage::pagePrint() {
    QVBoxLayout* v;
    QWidget* w = frame("Print", v);
    v->addWidget(bigButton("print", "Print", "Print the current view, with a preview.", host_.print));
    v->addWidget(bigButton("exportimage", "Save the view as an image", "PNG or JPEG at any resolution.", host_.exportImage));
    v->addStretch(1);
    return w;
}

QWidget* Backstage::pageExport() {
    QVBoxLayout* v;
    QWidget* w = frame("Export", v);
    v->addWidget(label("3D model", "bsCard"));
    v->addWidget(bigButton("solid_outer", "STL", "For 3D printing. Shells are checked and faced outward.", [this] { host_.exportModel("stl"); }));
    v->addWidget(bigButton("components", "Wavefront OBJ", "With materials (.mtl) and textures.", [this] { host_.exportModel("obj"); }));
    v->addWidget(bigButton("styles", "COLLADA (.dae)", "For renderers and other modelers.", [this] { host_.exportModel("dae"); }));
    v->addWidget(bigButton("dimension", "AutoCAD DXF", "Faces and edges for CAD programs.", [this] { host_.exportModel("dxf"); }));
    v->addSpacing(8);
    v->addWidget(label("2D graphic", "bsCard"));
    v->addWidget(bigButton("exportimage", "PNG or JPEG image", "The current view at any resolution.", host_.exportImage));
    v->addStretch(1);
    return w;
}

}  // namespace gable
