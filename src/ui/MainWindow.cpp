#include "ui/MainWindow.h"
#include "ui/TitleBar.h"
#include "ui/Ribbon.h"
#include "ui/Backstage.h"
#include "core/FileFormats.h"
#include "core/Geometry.h"
#include "core/Serializer.h"
#include "core/Solids.h"
#include "core/Sandbox.h"
#include "tools/Tools.h"
#include "ui/Icons.h"
#include "ui/Panels.h"
#include "view/Viewport.h"
#include <QApplication>
#include <QBuffer>
#include <QDialog>
#include <QDockWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QGridLayout>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPrintDialog>
#include <QPrinter>
#include <QPdfWriter>
#include <QPageSize>
#include <QSettings>
#include <QSpinBox>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableWidget>
#include <QHeaderView>
#include <QTextBrowser>
#include <QToolBar>
#include <QKeySequenceEdit>
#include <QTabWidget>
#include <QDateTime>
#include <QToolButton>
#include <QVBoxLayout>
#include <functional>

namespace gable {

MainWindow::MainWindow() {
    setWindowIcon(QIcon(appIconPixmap(256)));
    doc_ = new Document(this);
    view_ = new Viewport(doc_, this);
    auto* central = new QWidget;
    auto* cl = new QVBoxLayout(central);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    sceneTabs_ = new QTabBar;
    sceneTabs_->setExpanding(false);
    sceneTabs_->setDrawBase(true);
    sceneTabs_->setDocumentMode(true);
    sceneTabs_->setContextMenuPolicy(Qt::CustomContextMenu);
    cl->addWidget(sceneTabs_);
    cl->addWidget(view_, 1);
    setCentralWidget(central);
    setAcceptDrops(true);

    buildTools();
    buildTray();
    buildActions();
    decorateActions();
    buildStatus();
    buildChrome();
    for (auto& [id, a] : act_) defaultShortcuts_[id] = a->shortcut();
    loadShortcuts();
    connect(&playTimer_, &QTimer::timeout, this, [this] {
        int n = (int)doc_->model.scenes.size();
        if (n == 0) { playScenes(false); return; }
        playIndex_ = (playIndex_ + 1) % n;
        sceneTabs_->setCurrentIndex(playIndex_);
        scenes_->applyScene(playIndex_);
    });

    view_->installEventFilter(this);
    connect(view_, &Viewport::contextMenuRequested, this, &MainWindow::contextMenu);
    connect(view_, &Viewport::toolChanged, this, [this](const QString& id) {
        for (auto& [k, a] : toolAct_) a->setChecked(k == id);
    });
    connect(doc_, &Document::changed, this, &MainWindow::updateTitle);
    connect(doc_, &Document::fileChanged, this, [this] { updateTitle(); updateSceneTabs(); syncViewToggles(); view_->update(); });
    connect(doc_, &Document::undoStateChanged, this, &MainWindow::updateUndoActions);
    connect(sceneTabs_, &QTabBar::tabBarClicked, this, [this](int i) { if (i >= 0) scenes_->applyScene(i); });
    connect(sceneTabs_, &QTabBar::customContextMenuRequested, this, [this](const QPoint& p) {
        int i = sceneTabs_->tabAt(p);
        QMenu m;
        m.addAction("Add Scene", [this] { scenes_->addScene(); });
        if (i >= 0) {
            m.addAction("Update Scene", [this, i] { scenes_->updateScene(i); });
            m.addAction("Rename Scene…", [this, i] {
                bool ok;
                QString n = QInputDialog::getText(this, "Rename Scene", "Name:", QLineEdit::Normal, doc_->model.scenes[i].name, &ok);
                if (ok && !n.isEmpty()) { doc_->model.scenes[i].name = n; doc_->modified = true; updateSceneTabs(); scenes_->refresh(); }
            });
            m.addAction("Delete Scene", [this, i] {
                doc_->begin("Delete Scene");
                doc_->model.scenes.erase(doc_->model.scenes.begin() + i);
                doc_->commit();
                updateSceneTabs();
                scenes_->refresh();
            });
        }
        m.exec(sceneTabs_->mapToGlobal(p));
    });
    connect(scenes_, &ScenesPanel::scenesChanged, this, &MainWindow::updateSceneTabs);

    connect(&autosave_, &QTimer::timeout, this, &MainWindow::doAutosave);
    {
        int mins = QSettings().value("autosaveMinutes", 5).toInt();
        if (mins > 0) autosave_.start(mins * 60 * 1000);
    }

    QSettings s;
    restoreGeometry(s.value("geometry").toByteArray());
    restoreState(s.value("windowState").toByteArray());
    if (!s.contains("geometry")) resize(1500, 920);
    view_->setTool("select");
    updateTitle();
    updateSceneTabs();
    updateUndoActions();
    syncViewToggles();
    QTimer::singleShot(0, this, [this] { view_->zoomExtents(); view_->setFocus(); checkRecovery(); });
}

MainWindow::~MainWindow() = default;

// ======================================================================= setup

void MainWindow::buildTools() {
    auto* sel = new SelectTool(view_);
    view_->registerTool(sel);
    view_->registerTool(new LineTool(view_));
    view_->registerTool(new FreehandTool(view_));
    view_->registerTool(new RectTool(view_, false));
    view_->registerTool(new RectTool(view_, true));
    view_->registerTool(new CircleTool(view_, false));
    view_->registerTool(new CircleTool(view_, true));
    view_->registerTool(new ArcTool(view_, ArcTool::TwoPoint));
    view_->registerTool(new ArcTool(view_, ArcTool::Center));
    view_->registerTool(new ArcTool(view_, ArcTool::ThreePoint));
    view_->registerTool(new ArcTool(view_, ArcTool::Pie));
    view_->registerTool(new PushPullTool(view_));
    view_->registerTool(new MoveTool(view_));
    view_->registerTool(new RotateTool(view_));
    view_->registerTool(new ScaleTool(view_));
    view_->registerTool(new OffsetTool(view_));
    view_->registerTool(new FollowMeTool(view_));
    view_->registerTool(new EraserTool(view_));
    auto* paint = new PaintTool(view_);
    view_->registerTool(paint);
    auto* tape = new TapeTool(view_, false);
    view_->registerTool(tape);
    view_->registerTool(new TapeTool(view_, true));
    view_->registerTool(new DimensionTool(view_));
    auto* text = new TextTool(view_);
    view_->registerTool(text);
    view_->registerTool(new AxesTool(view_));
    view_->registerTool(new SectionTool(view_));
    view_->registerTool(new Text3DPlaceTool(view_));
    view_->registerTool(new PasteTool(view_));
    view_->registerTool(new OrbitTool(view_));
    view_->registerTool(new PanTool(view_));
    view_->registerTool(new ZoomTool(view_));
    view_->registerTool(new ZoomWindowTool(view_));
    view_->registerTool(new WalkTool(view_, false));
    view_->registerTool(new WalkTool(view_, true));
    view_->registerTool(new PositionCameraTool(view_));
    view_->registerTool(new SandboxScratchTool(view_));
    view_->registerTool(new SmooveTool(view_));
    view_->registerTool(new FlipEdgeTool(view_));
    view_->registerTool(new DrapeTool(view_));
    view_->registerTool(new ImageTool(view_));
    paint->currentMaterial = [this] { return materials_ ? materials_->current() : -1; };
    paint->sampled = [this](int m) { if (materials_) materials_->setCurrent(m); };
    text->editText = [this](QString& t) {
        bool ok;
        QString r = QInputDialog::getMultiLineText(this, "Text", "Text:", t, &ok);
        if (ok) t = r;
        return ok;
    };
    tape->confirmResize = [this](double f) {
        return QMessageBox::question(this, "Resize", QString("Do you want to resize the model (scale factor %1)?").arg(f, 0, 'f', 4)) == QMessageBox::Yes;
    };
}

QAction* MainWindow::addAct(const QString& id, const QString& text, const QKeySequence& ks, std::function<void()> fn, const QString& icon) {
    auto* a = new QAction(text, this);
    if (!ks.isEmpty()) a->setShortcut(ks);
    a->setProperty("actionId", id);
    if (!icon.isEmpty()) a->setProperty("iconName", icon);
    a->setShortcutContext(Qt::WindowShortcut);
    connect(a, &QAction::triggered, this, [fn] { fn(); });
    addAction(a);
    act_[id] = a;
    return a;
}

QAction* MainWindow::addToolAct(const QString& toolId, const QString& text, const QKeySequence& ks, const QString& icon) {
    auto* a = addAct("tool_" + toolId, text, ks, [this, toolId] { view_->setTool(toolId); view_->setFocus(); }, icon);
    a->setCheckable(true);
    QString tip = text;
    if (!ks.isEmpty()) tip += "  (" + ks.toString(QKeySequence::NativeText) + ")";
    a->setToolTip(tip);
    toolAct_[toolId] = a;
    return a;
}

void MainWindow::buildActions() {
    // File
    addAct("new", "&New", QKeySequence::New, [this] { newFile(); }, "new");
    addAct("open", "&Open…", QKeySequence::Open, [this] { open(); }, "open");
    addAct("save", "&Save", QKeySequence::Save, [this] { save(); }, "save");
    addAct("saveas", "Save &As…", QKeySequence("Ctrl+Shift+S"), [this] { saveAs(); });
    addAct("import", "&Import…", QKeySequence("Ctrl+I"), [this] { importFile(); });
    addAct("export3d", "Export 3D Model…", QKeySequence(), [this] { exportModel(); });
    addAct("exportstl", "Export STL…", QKeySequence(), [this] { exportModel("stl"); });
    addAct("exportobj", "Export OBJ…", QKeySequence(), [this] { exportModel("obj"); });
    addAct("export2d", "Export 2D Graphic…", QKeySequence(), [this] { exportImage(); });
    addAct("print", "&Print…", QKeySequence::Print, [this] { print(); });
    addAct("quit", "E&xit", QKeySequence::Quit, [this] { close(); });
    // Edit
    addAct("undo", "&Undo", QKeySequence::Undo, [this] { if (view_->tool()) view_->tool()->reset(); doc_->undo(); }, "undo");
    addAct("redo", "&Redo", QKeySequence("Ctrl+Y"), [this] { doc_->redo(); }, "redo");
    act_["redo"]->setShortcuts({QKeySequence("Ctrl+Y"), QKeySequence("Ctrl+Shift+Z")});
    addAct("cut", "Cu&t", QKeySequence::Cut, [this] { copy(true); });
    addAct("copy", "&Copy", QKeySequence::Copy, [this] { copy(false); });
    addAct("paste", "&Paste", QKeySequence::Paste, [this] { paste(false); });
    addAct("pasteinplace", "Paste In Place", QKeySequence(), [this] { paste(true); });
    addAct("delete", "&Delete", QKeySequence::Delete, [this] {
        if (doc_->sel.empty()) return;
        doc_->begin("Erase");
        doc_->eraseSelection();
        doc_->commit();
    });
    addAct("deleteguides", "Delete Guides", QKeySequence(), [this] { deleteGuides(); });
    addAct("selectall", "Select &All", QKeySequence::SelectAll, [this] { doc_->selectAll(); });
    addAct("selectnone", "Select &None", QKeySequence("Ctrl+T"), [this] { doc_->clearSelection(); });
    addAct("invertsel", "Invert Selection", QKeySequence("Ctrl+Shift+I"), [this] { invertSelection(); });
    addAct("hide", "&Hide", QKeySequence(), [this] { doc_->begin("Hide"); doc_->hideSelection(true); doc_->commit(); });
    addAct("unhideall", "Unhide All", QKeySequence(), [this] { doc_->begin("Unhide"); doc_->unhideAll(); doc_->commit(); });
    addAct("unhidesel", "Unhide Selected", QKeySequence(), [this] { doc_->begin("Unhide"); doc_->hideSelection(false); doc_->commit(); });
    addAct("lock", "&Lock", QKeySequence(), [this] { doc_->begin("Lock"); doc_->lockSelection(true); doc_->commit(); });
    addAct("unlock", "Unlock", QKeySequence(), [this] { doc_->begin("Unlock"); doc_->lockSelection(false); doc_->commit(); });
    addAct("makecomponent", "Make &Component…", QKeySequence("G"), [this] { makeComponentDialog(); }, "component");
    addAct("makegroup", "Make &Group", QKeySequence("Ctrl+G"), [this] {
        if (doc_->sel.empty()) return;
        doc_->begin("Make Group");
        doc_->makeGroup(doc_->sel, QString(), false);
        doc_->commit();
    }, "group");
    addAct("closegroup", "Close Group/Component", QKeySequence(), [this] { doc_->closeContext(); });
    addAct("explode", "&Explode", QKeySequence(), [this] {
        auto is = doc_->selectedInstances();
        if (is.empty()) return;
        doc_->begin("Explode");
        std::vector<EntRef> all;
        for (int i : is) {
            doc_->explode(i);
            all.insert(all.end(), doc_->sel.begin(), doc_->sel.end());
        }
        doc_->commit();
        doc_->setSelection(all);
    });
    addAct("makeunique", "Make &Unique", QKeySequence(), [this] {
        doc_->begin("Make Unique");
        for (int i : doc_->selectedInstances()) doc_->makeUnique(i);
        doc_->commit();
    });
    addAct("reversefaces", "Reverse Faces", QKeySequence(), [this] { reverseFaces(); });
    addAct("orientfaces", "Orient Faces", QKeySequence(), [this] { orientFaces(); });
    addAct("intersectmodel", "Intersect With Model", QKeySequence(), [this] { intersect(0); });
    addAct("intersectsel", "Intersect With Selection", QKeySequence(), [this] { intersect(1); });
    addAct("intersectctx", "Intersect With Context", QKeySequence(), [this] { intersect(2); });
    // View
    auto toggle = [this](const QString& id, const QString& text, const QKeySequence& ks, std::function<bool&()> ref, const QString& icon = QString()) {
        auto* a = addAct(id, text, ks, [this, ref, id] {
            ref() = act_[id]->isChecked();
            doc_->model.root.touch();
            for (auto& d : doc_->model.defs) d.ents.touch();
            doc_->modified = true;
            if (styles_) styles_->refresh();
            view_->update();
        }, icon);
        a->setCheckable(true);
    };
    Style* st = &doc_->model.style;
    toggle("hiddengeom", "Hidden &Geometry", QKeySequence("Alt+H"), [st]() -> bool& { return st->showHidden; });
    toggle("sectionplanes", "Section Planes", QKeySequence(), [st]() -> bool& { return st->showSectionPlanes; });
    toggle("sectioncuts", "Section Cuts", QKeySequence(), [st]() -> bool& { return st->showSectionCuts; });
    toggle("sectionfill", "Section Fill", QKeySequence(), [st]() -> bool& { return st->sectionFill; });
    toggle("axes", "Show Axes", QKeySequence(), [st]() -> bool& { return st->showAxes; });
    toggle("guides", "Show Guides", QKeySequence(), [st]() -> bool& { return st->showGuides; });
    toggle("edges", "Edges", QKeySequence(), [st]() -> bool& { return st->edges; });
    toggle("backedges", "Back Edges", QKeySequence("K"), [st]() -> bool& { return st->backEdges; }, "backedges");
    toggle("profiles", "Profiles", QKeySequence(), [st]() -> bool& { return st->profiles; });
    toggle("extension", "Extension", QKeySequence(), [st]() -> bool& { return st->extension; });
    toggle("xray", "X-ray", QKeySequence("Alt+X"), [st]() -> bool& { return st->xray; }, "xray");
    toggle("fog", "Fog", QKeySequence(), [st]() -> bool& { return st->fog; });
    ShadowSettings* sh = &doc_->model.shadow;
    toggle("shadows", "Shadows", QKeySequence(), [sh]() -> bool& { return sh->enabled; }, "shadows");
    auto faceStyle = [this](const QString& id, const QString& text, FaceStyle fs, const QString& icon) {
        auto* a = addAct(id, text, QKeySequence(), [this, fs] {
            doc_->model.style.faceStyle = fs;
            doc_->modified = true;
            syncViewToggles();
            if (styles_) styles_->refresh();
            view_->update();
        }, icon);
        a->setCheckable(true);
    };
    faceStyle("fs_wire", "Wireframe", FaceStyle::Wireframe, "wireframe");
    faceStyle("fs_hidden", "Hidden Line", FaceStyle::HiddenLine, "hiddenline");
    faceStyle("fs_shaded", "Shaded", FaceStyle::Shaded, "shaded");
    faceStyle("fs_textured", "Shaded With Textures", FaceStyle::ShadedTextured, "textured");
    faceStyle("fs_mono", "Monochrome", FaceStyle::Monochrome, "mono");
    addAct("hiderest", "Hide Rest of Model", QKeySequence(), [this] { hideRest(act_["hiderest"]->isChecked()); });
    act_["hiderest"]->setCheckable(true);
    addAct("scenetabs", "Scene Tabs", QKeySequence(), [this] { sceneTabs_->setVisible(act_["scenetabs"]->isChecked()); });
    act_["scenetabs"]->setCheckable(true);
    act_["scenetabs"]->setChecked(true);
    addAct("nextscene", "Next Scene", QKeySequence("PgDown"), [this] {
        int n = (int)doc_->model.scenes.size();
        if (!n) return;
        int i = (sceneTabs_->currentIndex() + 1) % n;
        sceneTabs_->setCurrentIndex(i);
        scenes_->applyScene(i);
    });
    addAct("prevscene", "Previous Scene", QKeySequence("PgUp"), [this] {
        int n = (int)doc_->model.scenes.size();
        if (!n) return;
        int i = (sceneTabs_->currentIndex() - 1 + n) % n;
        sceneTabs_->setCurrentIndex(i);
        scenes_->applyScene(i);
    });
    addAct("addscene", "Add Scene", QKeySequence(), [this] { scenes_->addScene(); });
    // Camera
    addAct("prevview", "&Previous", QKeySequence(), [this] { view_->previousView(); }, "previous");
    addAct("view_top", "&Top", QKeySequence("F2"), [this] { view_->standardView(0); }, "view_top");
    addAct("view_bottom", "&Bottom", QKeySequence("F7"), [this] { view_->standardView(1); }, "view_bottom");
    addAct("view_front", "&Front", QKeySequence("F3"), [this] { view_->standardView(2); }, "view_front");
    addAct("view_back", "B&ack", QKeySequence("F5"), [this] { view_->standardView(3); }, "view_back");
    addAct("view_left", "&Left", QKeySequence("F6"), [this] { view_->standardView(4); }, "view_left");
    addAct("view_right", "&Right", QKeySequence("F4"), [this] { view_->standardView(5); }, "view_right");
    addAct("view_iso", "&Iso", QKeySequence("F8"), [this] { view_->standardView(6); }, "view_iso");
    addAct("parallel", "Parallel Projection", QKeySequence(), [this] { view_->setPerspective(false); syncViewToggles(); }, "parallel");
    act_["parallel"]->setCheckable(true);
    addAct("perspective", "Perspective", QKeySequence(), [this] { view_->setPerspective(true); syncViewToggles(); }, "perspective");
    act_["perspective"]->setCheckable(true);
    addAct("fov", "Field of View…", QKeySequence(), [this] {
        bool ok;
        double v = QInputDialog::getDouble(this, "Field of View", "Vertical field of view (degrees):", view_->cam.s->fov, 1, 120, 1, &ok);
        if (ok) { view_->cam.setFov(v); view_->update(); }
    });
    addAct("zoomextents", "Zoom E&xtents", QKeySequence("Shift+Z"), [this] { view_->zoomExtents(); }, "zoomextents");
    act_["zoomextents"]->setShortcuts({QKeySequence("Shift+Z"), QKeySequence("Ctrl+Shift+E")});
    addAct("zoomsel", "Zoom Selection", QKeySequence(), [this] { view_->zoomSelection(); });
    addAct("alignview", "Align View", QKeySequence(), [this] { alignView(); });
    // Tools (SketchUp default shortcuts)
    addToolAct("select", "Select", QKeySequence(Qt::Key_Space), "select");
    addToolAct("line", "Line", QKeySequence("L"), "line");
    addToolAct("freehand", "Freehand", QKeySequence(), "freehand");
    addToolAct("rect", "Rectangle", QKeySequence("R"), "rect");
    addToolAct("rotrect", "Rotated Rectangle", QKeySequence(), "rotrect");
    addToolAct("circle", "Circle", QKeySequence("C"), "circle");
    addToolAct("polygon", "Polygon", QKeySequence(), "polygon");
    addToolAct("arc2", "2 Point Arc", QKeySequence("A"), "arc2");
    addToolAct("arc", "Arc", QKeySequence(), "arc");
    addToolAct("arc3", "3 Point Arc", QKeySequence(), "arc3");
    addToolAct("pie", "Pie", QKeySequence(), "pie");
    addToolAct("pushpull", "Push/Pull", QKeySequence("P"), "pushpull");
    addToolAct("move", "Move", QKeySequence("M"), "move");
    addToolAct("rotate", "Rotate", QKeySequence("Q"), "rotate");
    addToolAct("scale", "Scale", QKeySequence("S"), "scale");
    addToolAct("offset", "Offset", QKeySequence("F"), "offset");
    addToolAct("followme", "Follow Me", QKeySequence(), "followme");
    addToolAct("eraser", "Eraser", QKeySequence("E"), "eraser");
    addToolAct("paint", "Paint Bucket", QKeySequence("B"), "paint");
    addToolAct("tape", "Tape Measure", QKeySequence("T"), "tape");
    addToolAct("protractor", "Protractor", QKeySequence(), "protractor");
    addToolAct("dimension", "Dimensions", QKeySequence(), "dimension");
    addToolAct("text", "Text", QKeySequence(), "text");
    addToolAct("axes", "Axes", QKeySequence(), "axes");
    addToolAct("section", "Section Plane", QKeySequence(), "section");
    addToolAct("orbit", "Orbit", QKeySequence("O"), "orbit");
    addToolAct("pan", "Pan", QKeySequence("H"), "pan");
    addToolAct("zoom", "Zoom", QKeySequence("Z"), "zoom");
    addToolAct("zoomwindow", "Zoom Window", QKeySequence("Ctrl+Shift+W"), "zoomwindow");
    addToolAct("walk", "Walk", QKeySequence(), "walk");
    addToolAct("lookaround", "Look Around", QKeySequence(), "lookaround");
    addToolAct("poscam", "Position Camera", QKeySequence(), "poscam");
    addAct("text3d", "3D Text…", QKeySequence(), [this] { text3D(); }, "text3d");
    addToolAct("sbscratch", "From Scratch", QKeySequence(), "sbscratch");
    addToolAct("smoove", "Smoove", QKeySequence(), "smoove");
    addToolAct("flipedge", "Flip Edge", QKeySequence(), "flipedge");
    addToolAct("drape", "Drape", QKeySequence(), "drape");
    addAct("sbcontours", "From Contours", QKeySequence(), [this] { fromContours(); }, "sbcontours");
    addAct("sbdetail", "Add Detail", QKeySequence(), [this] { addDetail(); });
    addAct("solid_outer", "Outer Shell", QKeySequence(), [this] { solidOp((int)SolidOp::OuterShell); }, "solid");
    addAct("solid_union", "Union", QKeySequence(), [this] { solidOp((int)SolidOp::Union); });
    addAct("solid_subtract", "Subtract", QKeySequence(), [this] { solidOp((int)SolidOp::Subtract); });
    addAct("solid_intersect", "Intersect", QKeySequence(), [this] { solidOp((int)SolidOp::Intersect); });
    addAct("solid_trim", "Trim", QKeySequence(), [this] { solidOp((int)SolidOp::Trim); });
    addAct("solid_split", "Split", QKeySequence(), [this] { solidOp((int)SolidOp::Split); });
    // Window / Help
    addAct("modelinfo", "Model Info…", QKeySequence(), [this] { modelInfo(); });
    addAct("shortcuts", "Keyboard Shortcuts", QKeySequence("F1"), [this] { shortcutsHelp(); });
    addAct("about", "About Folio Gable", QKeySequence(), [this] { about(); });
    addAct("preferences", "Preferences…", QKeySequence("Ctrl+,"), [this] { preferences(); });
    addAct("divide", "Divide…", QKeySequence(), [this] { divideEdge(); });
    addAct("weld", "Weld Edges", QKeySequence(), [this] { weldEdges(); });
    addAct("explodecurve", "Explode Curve", QKeySequence(), [this] { explodeCurve(); });
    addAct("playscenes", "Play", QKeySequence(), [this] { playScenes(act_["playscenes"]->isChecked()); });
    act_["playscenes"]->setCheckable(true);
}

void MainWindow::buildTray() {
    tray_ = new Tray;
    auto* info = new EntityInfoPanel(doc_);
    connect(info, &EntityInfoPanel::requestRedraw, view_, [this] { view_->update(); });
    tray_->addPanel("Entity Info", info, true);
    materials_ = new MaterialsPanel(doc_);
    connect(materials_, &MaterialsPanel::materialChosen, this, [this](int) { view_->setTool("paint"); });
    tray_->addPanel("Materials", materials_, true);
    auto* comps = new ComponentsPanel(doc_);
    connect(comps, &ComponentsPanel::placeComponent, this, [this](int def) {
        auto* t = static_cast<Text3DPlaceTool*>(view_->toolById("place"));
        t->def = def;
        view_->setTool("place");
        view_->setFocus();
    });
    tray_->addPanel("Components", comps, false);
    styles_ = new StylesPanel(doc_);
    connect(styles_, &StylesPanel::styleChanged, this, [this] {
        doc_->model.root.touch();
        for (auto& d : doc_->model.defs) d.ents.touch();
        syncViewToggles();
        view_->update();
    });
    tray_->addPanel("Styles", styles_, false);
    tray_->addPanel("Tags", new TagsPanel(doc_), false);
    scenes_ = new ScenesPanel(doc_, view_);
    tray_->addPanel("Scenes", scenes_, false);
    tray_->addPanel("Shadows", new ShadowsPanel(doc_), false);
    tray_->addPanel("Fog", new FogPanel(doc_), false);
    tray_->addPanel("Outliner", new OutlinerPanel(doc_), false);
    tray_->addPanel("Soften Edges", new SoftenPanel(doc_), false);
    auto* dock = new QDockWidget("Default Tray", this);
    trayDock_ = dock;
    dock->setObjectName("dock_tray");
    dock->setWidget(tray_);
    dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::RightDockWidgetArea, dock);
}

void MainWindow::buildStatus() {
    auto* sb = statusBar();
    status_ = new QLabel;
    sb->addWidget(status_, 1);
    vcbLabel_ = new QLabel("Measurements");
    vcb_ = new QLineEdit;
    vcb_->setMinimumWidth(200);
    vcb_->setMaximumWidth(260);
    sb->addPermanentWidget(vcbLabel_);
    sb->addPermanentWidget(vcb_);
    connect(view_, &Viewport::statusChanged, status_, &QLabel::setText);
    connect(view_, &Viewport::vcbChanged, this, [this](const QString& label, const QString& value) {
        vcbLabel_->setText(label);
        if (!vcb_->hasFocus()) vcb_->setText(value);
    });
    connect(vcb_, &QLineEdit::returnPressed, this, [this] {
        QString t = vcb_->text();
        vcb_->clear();
        view_->setFocus();
        if (view_->tool()) {
            view_->tool()->vcbEnter(t);
            if (view_->tool()) emit view_->statusChanged(view_->tool()->status());
            vcbLabel_->setText(view_->tool()->vcbLabel());
        }
        view_->update();
    });
    vcb_->installEventFilter(this);
}

// ======================================================================= state sync

void MainWindow::updateTitle() {
    QString name = doc_->filePath.isEmpty() ? "Untitled" : QFileInfo(doc_->filePath).fileName();
    setWindowTitle(QString("%1%2 - Folio Gable").arg(doc_->modified ? "*" : "", name));
    if (titleBar_) titleBar_->setTitle(windowTitle());
}

void MainWindow::updateSceneTabs() {
    while (sceneTabs_->count()) sceneTabs_->removeTab(0);
    for (auto& s : doc_->model.scenes) sceneTabs_->addTab(s.name);
    sceneTabs_->setVisible(act_["scenetabs"]->isChecked() && !doc_->model.scenes.empty());
}

void MainWindow::updateUndoActions() {
    act_["undo"]->setText(doc_->canUndo() ? "&Undo " + doc_->undoName() : "&Undo");
    act_["redo"]->setText(doc_->canRedo() ? "&Redo " + doc_->redoName() : "&Redo");
    act_["undo"]->setEnabled(doc_->canUndo());
    act_["redo"]->setEnabled(doc_->canRedo());
}

void MainWindow::syncViewToggles() {
    const Style& s = doc_->model.style;
    // setChecked only emits toggled(), which nothing acts on, so the ribbon buttons follow along
    auto set = [&](const char* id, bool v) { act_[id]->setChecked(v); };
    set("hiddengeom", s.showHidden);
    set("sectionplanes", s.showSectionPlanes);
    set("sectioncuts", s.showSectionCuts);
    set("sectionfill", s.sectionFill);
    set("axes", s.showAxes);
    set("guides", s.showGuides);
    set("edges", s.edges);
    set("backedges", s.backEdges);
    set("profiles", s.profiles);
    set("extension", s.extension);
    set("xray", s.xray);
    set("fog", s.fog);
    set("shadows", doc_->model.shadow.enabled);
    set("fs_wire", s.faceStyle == FaceStyle::Wireframe);
    set("fs_hidden", s.faceStyle == FaceStyle::HiddenLine);
    set("fs_shaded", s.faceStyle == FaceStyle::Shaded);
    set("fs_textured", s.faceStyle == FaceStyle::ShadedTextured);
    set("fs_mono", s.faceStyle == FaceStyle::Monochrome);
    set("parallel", !doc_->model.camera.perspective);
    set("perspective", doc_->model.camera.perspective);
}

// ======================================================================= events

bool MainWindow::eventFilter(QObject* o, QEvent* e) {
    if (o == view_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        QString t = k->text();
        if (!t.isEmpty() && !(k->modifiers() & (Qt::ControlModifier | Qt::AltModifier))) {
            QChar c = t[0];
            static const QString starters = "0123456789.,;-[]<>/*x~'\"";
            bool startsValue = starters.contains(c) && !(c == 'x' && !view_->tool());
            // 'x' only starts input for array syntax after move/rotate
            if (c == 'x' && view_->tool() && view_->tool()->id() != "move" && view_->tool()->id() != "rotate") startsValue = false;
            if (startsValue) {
                vcb_->setFocus();
                vcb_->setText(t);
                return true;
            }
        }
        if (k->key() == Qt::Key_Escape && vcb_->hasFocus()) {
            vcb_->clear();
            view_->setFocus();
            return true;
        }
        // Esc leaves full-screen mode once no tool is using it
        if (k->key() == Qt::Key_Escape && ribbon_ && ribbon_->mode() == Ribbon::FullScreen &&
            !(view_->tool() && view_->tool()->busy())) {
            ribbon_->setMode(Ribbon::AlwaysShow);
            return true;
        }
    }
    if (o == vcb_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Escape) {
            vcb_->clear();
            view_->setFocus();
            return true;
        }
    }
    return QMainWindow::eventFilter(o, e);
}

void MainWindow::closeEvent(QCloseEvent* e) {
    if (!maybeSave()) { e->ignore(); return; }
    QSettings s;
    s.setValue("geometry", saveGeometry());
    s.setValue("windowState", saveState());
    QFile::remove(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/autosave.gbl");
    e->accept();
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* e) {
    for (auto& u : e->mimeData()->urls()) {
        QString p = u.toLocalFile();
        if (p.isEmpty()) continue;
        QString ext = QFileInfo(p).suffix().toLower();
        if (ext == "gbl") { if (maybeSave()) openFile(p); }
        else if (QStringList{"png", "jpg", "jpeg", "bmp", "gif", "webp", "tif", "tiff"}.contains(ext)) {
            // a picture dropped on the window is placed as an image, like Import
            auto* it = static_cast<ImageTool*>(view_->toolById("image"));
            QString err;
            if (!it->setImage(p, &err)) { QMessageBox::warning(this, "Import", err); continue; }
            view_->setTool("image");
            view_->setFocus();
            break;
        } else {
            QString err;
            ImportOptions io;
            doc_->begin("Import");
            int idx = gable::importFile(doc_->model, doc_->active(), p, io, &err);
            doc_->commit();
            if (idx < 0) QMessageBox::warning(this, "Import", err);
            else doc_->setSelection({{EntKind::Instance, idx}});
        }
    }
}

// ======================================================================= file commands

bool MainWindow::maybeSave() {
    if (!doc_->modified) return true;
    auto r = QMessageBox::warning(this, "Folio Gable", "Save changes to the model?", QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (r == QMessageBox::Cancel) return false;
    if (r == QMessageBox::Save) return save();
    return true;
}

void MainWindow::newFile() {
    if (!maybeSave()) return;
    doc_->newModel();
    if (materials_) materials_->setCurrent(-1);
    view_->setTool("select");
    view_->zoomExtents();
}

bool MainWindow::openFile(const QString& path) {
    QString err;
    if (QFileInfo(path).suffix().toLower() != "gbl") {
        // open a foreign format as a new model
        doc_->newModel();
        ImportOptions io;
        int idx = gable::importFile(doc_->model, doc_->model.root, path, io, &err);
        if (idx < 0) { QMessageBox::warning(this, "Open", err); return false; }
        doc_->explode(idx);
        doc_->clearSelection();
        doc_->modified = true;
        updateTitle();
        view_->zoomExtents();
        return true;
    }
    if (!doc_->load(path, &err)) {
        QMessageBox::warning(this, "Open", "Could not open " + path + ":\n" + err);
        return false;
    }
    addRecent(path);
    view_->setTool("select");
    if (!doc_->model.scenes.empty()) view_->animateTo(doc_->model.scenes[0].cam, 1);
    view_->update();
    return true;
}

void MainWindow::open() {
    if (!maybeSave()) return;
    QSettings s;
    QString fn = QFileDialog::getOpenFileName(this, "Open Model", s.value("lastDir", QDir::homePath()).toString(),
                                              "Folio Gable models (*.gbl);;All supported (*.gbl *.obj *.stl *.dae *.dxf);;All files (*)");
    if (fn.isEmpty()) return;
    s.setValue("lastDir", QFileInfo(fn).absolutePath());
    openFile(fn);
}

bool MainWindow::save() {
    if (doc_->filePath.isEmpty()) return saveAs();
    QString err;
    if (!doc_->save(doc_->filePath, &err)) {
        QMessageBox::warning(this, "Save", err);
        return false;
    }
    statusBar()->showMessage("Saved " + doc_->filePath, 3000);
    addRecent(doc_->filePath);
    saveThumbnail(doc_->filePath);
    updateTitle();
    return true;
}

bool MainWindow::saveAs() {
    QSettings s;
    QString fn = QFileDialog::getSaveFileName(this, "Save Model", s.value("lastDir", QDir::homePath()).toString() + "/Untitled.gbl", "Folio Gable models (*.gbl)");
    if (fn.isEmpty()) return false;
    if (!fn.endsWith(".gbl", Qt::CaseInsensitive)) fn += ".gbl";
    s.setValue("lastDir", QFileInfo(fn).absolutePath());
    QString err;
    if (!doc_->save(fn, &err)) {
        QMessageBox::warning(this, "Save", err);
        return false;
    }
    addRecent(fn);
    saveThumbnail(fn);
    updateTitle();
    return true;
}

void MainWindow::addRecent(const QString& path) {
    QSettings s;
    QStringList r = s.value("recent").toStringList();
    r.removeAll(path);
    r.prepend(path);
    while (r.size() > 10) r.removeLast();
    s.setValue("recent", r);
    refreshRecent();
}

void MainWindow::refreshRecent() {
    if (!recentMenu_) return;
    recentMenu_->clear();
    QSettings s;
    for (auto& p : s.value("recent").toStringList())
        recentMenu_->addAction(QFileInfo(p).fileName(), [this, p] { if (maybeSave()) openFile(p); })->setToolTip(p);
}

void MainWindow::importFile() {
    QSettings s;
    QString fn = QFileDialog::getOpenFileName(this, "Import", s.value("lastDir", QDir::homePath()).toString(),
                                              "3D models and pictures (*.obj *.stl *.dae *.dxf *.gbl *.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff);;"
                                              "Wavefront OBJ (*.obj);;STL (*.stl);;COLLADA (*.dae);;DXF (*.dxf);;Folio Gable (*.gbl);;"
                                              "Pictures (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff)");
    if (fn.isEmpty()) return;
    s.setValue("lastDir", QFileInfo(fn).absolutePath());
    static const QStringList pics = {"png", "jpg", "jpeg", "bmp", "gif", "webp", "tif", "tiff"};
    if (pics.contains(QFileInfo(fn).suffix().toLower())) {
        bool ok = false;
        QString use = QInputDialog::getItem(this, "Import Picture", "Use the picture as:",
                                            {"Image — a picture placed in the model", "Texture — a material to paint faces with"},
                                            0, false, &ok);
        if (!ok) return;
        if (use.startsWith("Texture")) {
            QImage img(fn);
            if (img.isNull()) { QMessageBox::warning(this, "Import", "Cannot read the picture."); return; }
            doc_->begin("Import Texture");
            int m = doc_->model.addMaterial(QFileInfo(fn).completeBaseName(), QColor(200, 200, 200));
            QByteArray png;
            QBuffer b(&png);
            b.open(QIODevice::WriteOnly);
            img.save(&b, "PNG");
            doc_->model.materials[m].hasTexture = true;
            doc_->model.materials[m].texturePng = png;
            doc_->model.materials[m].texH = 1000.0 * img.height() / std::max(1, img.width());
            doc_->commit();
            materials_->refresh();
            materials_->setCurrent(m);
            view_->setTool("paint");
            return;
        }
        auto* it = static_cast<ImageTool*>(view_->toolById("image"));
        QString err;
        if (!it->setImage(fn, &err)) { QMessageBox::warning(this, "Import", err); return; }
        view_->setTool("image");
        view_->setFocus();
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle("Import Options");
    auto* form = new QFormLayout(&dlg);
    auto* units = new QComboBox;
    units->addItems({"Millimetres", "Centimetres", "Metres", "Inches", "Feet"});
    QString ext = QFileInfo(fn).suffix().toLower();
    units->setCurrentIndex(ext == "dae" ? 0 : 0);
    auto* swap = new QCheckBox("Source is Y-up (swap Y/Z)");
    auto* merge = new QCheckBox("Merge coplanar faces");
    merge->setChecked(true);
    form->addRow("File units:", units);
    form->addRow("", swap);
    form->addRow("", merge);
    if (ext == "dae" || ext == "gbl") { units->setEnabled(false); swap->setEnabled(false); }
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    static const double f[] = {1, 10, 1000, 25.4, 304.8};
    ImportOptions io;
    io.scale = ext == "dae" ? 1.0 : f[units->currentIndex()];
    io.swapYZ = swap->isChecked();
    io.mergeCoplanar = merge->isChecked();
    QString err;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    doc_->begin("Import");
    int idx = gable::importFile(doc_->model, doc_->active(), fn, io, &err);
    doc_->commit();
    QApplication::restoreOverrideCursor();
    if (idx < 0) { QMessageBox::warning(this, "Import", err); return; }
    doc_->setSelection({{EntKind::Instance, idx}});
    view_->zoomSelection();
}

void MainWindow::exportModel(const QString& forcedExt) {
    QSettings s;
    struct Fmt { const char* ext; const char* name; };
    static const Fmt fmts[] = {{"stl", "STL (3D printing)"}, {"obj", "Wavefront OBJ"}, {"dae", "COLLADA"}, {"dxf", "AutoCAD DXF"}};
    QString ext = forcedExt;
    QDialog dlg(this);
    dlg.setWindowTitle("Export 3D Model");
    auto* form = new QFormLayout(&dlg);
    auto* fmt = new QComboBox;
    for (auto& f : fmts) fmt->addItem(QString("%1 (*.%2)").arg(f.name, f.ext), QString(f.ext));
    if (ext.isEmpty()) ext = s.value("lastExport3d", "stl").toString();
    fmt->setCurrentIndex(std::max(0, fmt->findData(ext)));
    auto* units = new QComboBox;
    units->addItems({"Millimetres", "Centimetres", "Metres", "Inches", "Feet"});
    auto* yup = new QCheckBox("Y-up (for game engines / some renderers)");
    auto* edges = new QCheckBox("Export edges");
    auto* selOnly = new QCheckBox("Selection only");
    selOnly->setEnabled(!doc_->sel.empty() && doc_->editPath.empty());
    selOnly->setChecked(selOnly->isEnabled());
    auto* binary = new QCheckBox("Binary STL (smaller; ASCII if unchecked)");
    binary->setChecked(s.value("stlBinary", true).toBool());
    form->addRow("Format:", fmt);
    form->addRow("Units:", units);
    form->addRow("", yup);
    form->addRow("", edges);
    form->addRow("", selOnly);
    form->addRow("", binary);
    auto sync = [&] {
        QString e = fmt->currentData().toString();
        binary->setEnabled(e == "stl");
        edges->setEnabled(e == "obj" || e == "dxf");
        units->setEnabled(e != "dae");
        units->setCurrentIndex(e == "obj" ? 2 : 0);
        yup->setChecked(false);
    };
    sync();
    connect(fmt, QOverload<int>::of(&QComboBox::currentIndexChanged), &dlg, [&](int) { sync(); });
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Export…");
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    ext = fmt->currentData().toString();
    s.setValue("lastExport3d", ext);
    s.setValue("stlBinary", binary->isChecked());

    QString base = doc_->filePath.isEmpty() ? QString("model") : QFileInfo(doc_->filePath).completeBaseName();
    QString dir = s.value("lastDir", QDir::homePath()).toString();
    QString filter = QString("%1 (*.%2)").arg(fmt->currentText().section(" (", 0, 0), ext);
    QFileDialog fd(this, "Export " + fmt->currentText().section(" (", 0, 0), dir, filter);
    fd.setAcceptMode(QFileDialog::AcceptSave);
    fd.setDefaultSuffix(ext);
    fd.selectFile(base + "." + ext);
    if (fd.exec() != QDialog::Accepted || fd.selectedFiles().isEmpty()) return;
    QString fn = fd.selectedFiles().first();
    // Whatever name was typed, the file gets the chosen format's extension.
    if (QFileInfo(fn).suffix().toLower() != ext) fn += "." + ext;
    s.setValue("lastDir", QFileInfo(fn).absolutePath());

    static const double f[] = {1, 10, 1000, 25.4, 304.8};
    ExportOptions eo;
    eo.scale = ext == "dae" ? 1.0 : f[units->currentIndex()];
    eo.swapYZ = yup->isChecked();
    eo.exportEdges = edges->isEnabled() && edges->isChecked();
    eo.binary = binary->isChecked();
    QString err;
    std::vector<EntRef> selv = doc_->sel;
    QApplication::setOverrideCursor(Qt::WaitCursor);
    bool ok = gable::exportFile(doc_->model, fn, eo, &err, selOnly->isChecked() ? &selv : nullptr);
    QApplication::restoreOverrideCursor();
    if (!ok) { QMessageBox::warning(this, "Export", err); return; }
    QString msg = "Exported " + QFileInfo(fn).fileName();
    if (ext == "stl" && !err.isEmpty()) msg += " — " + err;
    statusBar()->showMessage(msg, 8000);
}

void MainWindow::exportImage() {
    QDialog dlg(this);
    dlg.setWindowTitle("Export 2D Graphic");
    auto* form = new QFormLayout(&dlg);
    auto* w = new QSpinBox;
    w->setRange(64, 16000);
    w->setValue(view_->width() * 2);
    auto* h = new QSpinBox;
    h->setRange(64, 16000);
    h->setValue(view_->height() * 2);
    auto* keep = new QCheckBox("Use view aspect ratio");
    keep->setChecked(true);
    auto* transp = new QCheckBox("Transparent background (PNG)");
    form->addRow("Width:", w);
    form->addRow("Height:", h);
    form->addRow("", keep);
    form->addRow("", transp);
    connect(w, QOverload<int>::of(&QSpinBox::valueChanged), &dlg, [&](int v) {
        if (keep->isChecked()) { QSignalBlocker b(h); h->setValue((int)std::lround(v * (double)view_->height() / std::max(1, view_->width()))); }
    });
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    QSettings s;
    QString imgSel;
    QString fn = QFileDialog::getSaveFileName(this, "Export Image", s.value("lastDir", QDir::homePath()).toString() + "/view.png",
                                              "PNG (*.png);;JPEG (*.jpg *.jpeg);;PDF (*.pdf)", &imgSel);
    if (fn.isEmpty()) return;
    {
        QString sx = QFileInfo(fn).suffix().toLower();
        QString want = imgSel.startsWith("JPEG") ? "jpg" : (imgSel.startsWith("PDF") ? "pdf" : "png");
        if (sx != "png" && sx != "jpg" && sx != "jpeg" && sx != "pdf") fn += "." + want;
        else if (sx != want && !(want == "jpg" && sx == "jpeg")) fn = fn.left(fn.size() - sx.size()) + want;
    }
    s.setValue("lastDir", QFileInfo(fn).absolutePath());
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QImage img = view_->renderImage(w->value(), h->value(), transp->isChecked());
    QApplication::restoreOverrideCursor();
    bool ok;
    if (QFileInfo(fn).suffix().toLower() == "pdf") {
        // one page, the view fitted inside the margins, landscape when the view is wide
        QPdfWriter pdf(fn);
        pdf.setCreator("Folio Gable");
        pdf.setTitle(QFileInfo(doc_->filePath.isEmpty() ? QString("Untitled") : doc_->filePath).completeBaseName());
        pdf.setPageSize(QPageSize(QPageSize::A4));
        pdf.setPageOrientation(img.width() >= img.height() ? QPageLayout::Landscape : QPageLayout::Portrait);
        pdf.setPageMargins(QMarginsF(12, 12, 12, 12), QPageLayout::Millimeter);
        pdf.setResolution(300);
        QPainter p(&pdf);
        ok = p.isActive();
        if (ok) {
            QRect page = p.viewport();
            QSize sz = img.size().scaled(page.size(), Qt::KeepAspectRatio);
            QRect target(page.x() + (page.width() - sz.width()) / 2, page.y() + (page.height() - sz.height()) / 2, sz.width(), sz.height());
            p.setRenderHint(QPainter::SmoothPixmapTransform);
            p.drawImage(target, img);
        }
    } else {
        ok = img.save(fn);
    }
    if (!ok) QMessageBox::warning(this, "Export", "Could not write " + fn);
    else statusBar()->showMessage("Exported " + fn, 4000);
}

void MainWindow::print() {
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dlg(&printer, this);
    if (dlg.exec() != QDialog::Accepted) return;
    QRect page = printer.pageLayout().paintRectPixels(printer.resolution());
    double aspect = (double)view_->width() / std::max(1, view_->height());
    int w = std::min(page.width(), 6000), h = (int)(w / aspect);
    if (h > page.height()) { h = std::min(page.height(), 6000); w = (int)(h * aspect); }
    QImage img = view_->renderImage(w, h, false);
    QPainter p(&printer);
    QRect target(0, 0, page.width(), (int)(page.width() / aspect));
    if (target.height() > page.height()) target = QRect(0, 0, (int)(page.height() * aspect), page.height());
    p.drawImage(target, img);
}

void MainWindow::doAutosave() {
    if (!doc_->modified) return;
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    QFile f(dir + "/autosave.gbl");
    if (f.open(QIODevice::WriteOnly)) f.write(saveModel(doc_->model, true));
}

// ======================================================================= clipboard

void MainWindow::copy(bool cut) {
    if (doc_->sel.empty()) return;
    auto clip = std::make_shared<Model>(doc_->model);
    clip->root.clear();
    clip->scenes.clear();
    geom::copyEntities(doc_->active(), doc_->sel, clip->root, doc_->activeXf(), false);
    clip->purgeUnused();
    clipboard_ = clip;
    BBox b = doc_->selectionBBox();
    clipRef_ = b.valid() ? b.lo : Vec3();
    if (cut) {
        doc_->begin("Cut");
        doc_->eraseSelection();
        doc_->commit();
    }
}

void MainWindow::paste(bool inPlace) {
    if (!clipboard_) return;
    if (inPlace) {
        doc_->begin("Paste In Place");
        auto res = mergeModelInto(doc_->model, doc_->active(), *clipboard_, doc_->activeInv());
        doc_->commit();
        doc_->setSelection(res);
        return;
    }
    auto* t = static_cast<PasteTool*>(view_->toolById("paste"));
    t->clip = clipboard_;
    t->ref = clipRef_;
    view_->setTool("paste");
    view_->setFocus();
}

// ======================================================================= edit commands

void MainWindow::makeComponentDialog() {
    if (doc_->sel.empty()) return;
    QDialog dlg(this);
    dlg.setWindowTitle("Create Component");
    auto* form = new QFormLayout(&dlg);
    auto* name = new QLineEdit(doc_->model.uniqueDefName("Component"));
    auto* desc = new QLineEdit;
    auto* glue = new QCheckBox("Glue to faces");
    auto* face = new QCheckBox("Always face camera");
    auto* cut = new QCheckBox("Cut opening (when glued to a face)");
    auto* repl = new QCheckBox("Replace selection with component");
    repl->setChecked(true);
    form->addRow("Definition:", name);
    form->addRow("Description:", desc);
    form->addRow("", glue);
    form->addRow("", cut);
    form->addRow("", face);
    form->addRow("", repl);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Create");
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    doc_->begin("Make Component");
    auto selCopy = doc_->sel;
    int idx = doc_->makeGroup(selCopy, name->text(), true);
    if (idx >= 0) {
        int def = doc_->active().insts[idx].def;
        doc_->model.defs[def].description = desc->text();
        doc_->model.defs[def].glue = glue->isChecked();
        doc_->model.defs[def].alwaysFaceCamera = face->isChecked();
        doc_->model.defs[def].cutsOpening = cut->isChecked();
        if (cut->isChecked()) doc_->model.defs[def].glue = true;
        if (!repl->isChecked()) {
            // keep the original geometry: undo the replacement by exploding a copy back
            Instance in = doc_->active().insts[idx];
            doc_->active().insts[idx].alive = false;
            doc_->active().insts.push_back(in);
            doc_->explode((int)doc_->active().insts.size() - 1);
        }
    }
    doc_->commit();
}

void MainWindow::deleteGuides() {
    doc_->begin("Delete Guides");
    std::function<void(Entities&)> f = [&](Entities& E) { for (auto& g : E.guides) g.alive = false; E.touch(); };
    f(doc_->model.root);
    for (auto& d : doc_->model.defs) f(d.ents);
    doc_->commit();
}

void MainWindow::invertSelection() {
    std::vector<EntRef> cur = doc_->sel;
    std::set<EntRef> s(cur.begin(), cur.end());
    doc_->selectAll();
    std::vector<EntRef> all = doc_->sel, inv;
    for (auto& r : all) if (!s.count(r)) inv.push_back(r);
    doc_->setSelection(inv);
}

void MainWindow::reverseFaces() {
    auto fs = doc_->selectedFaces();
    if (fs.empty()) return;
    doc_->begin("Reverse Faces");
    for (int f : fs) geom::reverseFace(doc_->active(), f);
    doc_->commit();
}

void MainWindow::orientFaces() {
    auto fs = doc_->selectedFaces();
    if (fs.empty()) return;
    Entities& E = doc_->active();
    doc_->begin("Orient Faces");
    std::set<int> done = {fs[0]};
    std::vector<int> q = {fs[0]};
    auto dirIn = [&](const Face& F, int a, int b) {
        auto chk = [&](const std::vector<int>& L) {
            for (size_t k = 0; k < L.size(); ++k) {
                int x = L[k], y = L[(k + 1) % L.size()];
                if (x == a && y == b) return 1;
                if (x == b && y == a) return -1;
            }
            return 0;
        };
        int d = chk(F.outer);
        for (auto& h : F.holes) if (!d) d = chk(h);
        return d;
    };
    for (size_t i = 0; i < q.size(); ++i) {
        int f = q[i];
        for (int e : E.faceEdges(f)) {
            const Edge& Ed = E.edges[e];
            if (Ed.faces.size() != 2) continue;
            int g = Ed.faces[0] == f ? Ed.faces[1] : Ed.faces[0];
            if (done.count(g) || !E.faces[g].alive) continue;
            int df = dirIn(E.faces[f], Ed.v[0], Ed.v[1]), dg = dirIn(E.faces[g], Ed.v[0], Ed.v[1]);
            if (df != 0 && df == dg) geom::reverseFace(E, g);
            done.insert(g);
            q.push_back(g);
        }
    }
    doc_->commit();
}

void MainWindow::intersect(int mode) {
    Entities& E = doc_->active();
    std::vector<int> a = doc_->selectedFaces(), b;
    if (a.empty()) return;
    if (mode == 1) b = a;
    doc_->begin("Intersect Faces");
    int n = geom::intersectFaces(E, a, b);
    doc_->commit();
    statusBar()->showMessage(QString("%1 intersection segments").arg(n), 3000);
}

void MainWindow::solidOp(int op) {
    auto is = doc_->selectedInstances();
    if (is.size() < 2) {
        QMessageBox::information(this, "Solid Tools", "Select two solid groups or components (the first selected is the cutter for Subtract/Trim).");
        return;
    }
    QString err;
    doc_->begin("Solid Operation");
    int a = is[0];
    std::vector<int> res;
    bool ok = true;
    for (size_t k = 1; k < is.size() && ok; ++k) {
        res.clear();
        ok = solidOperation(doc_->model, doc_->active(), a, is[k], (SolidOp)op, &err, &res);
        if (ok && !res.empty()) a = res[0];
        if (op != (int)SolidOp::Union && op != (int)SolidOp::OuterShell) break;
    }
    if (!ok) {
        doc_->cancel();
        QMessageBox::warning(this, "Solid Tools", err);
        return;
    }
    doc_->commit();
    std::vector<EntRef> sel;
    for (int r : res) sel.push_back({EntKind::Instance, r});
    doc_->setSelection(sel);
}

void MainWindow::flipAlong(int axis) {
    if (doc_->sel.empty()) return;
    BBox b = doc_->selectionBBox();
    Mat4 inv = doc_->activeInv();
    Vec3 c = inv.point(b.center());
    Vec3 s(1, 1, 1);
    s[axis] = -1;
    Mat4 L = Mat4::scalingAbout(c, s);
    Entities& E = doc_->active();
    doc_->begin("Flip");
    auto vs = geom::selectionVertices(E, doc_->sel);
    if (!vs.empty()) geom::transformVertices(E, vs, L, true);
    for (auto& r : doc_->sel)
        if (r.kind == EntKind::Instance) E.insts[r.id].xf = L * E.insts[r.id].xf;
    E.touch();
    doc_->commit();
}

void MainWindow::hideRest(bool on) {
    doc_->hideRest = on;
    view_->update();
}

void MainWindow::alignView() {
    auto fs = doc_->selectedFaces();
    if (fs.empty()) return;
    const Entities& E = doc_->active();
    Mat4 xf = doc_->activeXf();
    Vec3 c = xf.point(E.faceCentroid(fs[0]));
    Vec3 n = xf.normal(E.faces[fs[0]].plane.n);
    CameraState to = *view_->cam.s;
    double d = std::max(view_->cam.distance(), 1000.0);
    to.target = c;
    to.eye = c + n * d;
    to.up = std::fabs(n.z) > 0.99 ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
    view_->animateTo(to, 400);
}

void MainWindow::selectSame(int what) {
    if (doc_->sel.empty()) return;
    Entities& E = doc_->active();
    EntRef r = doc_->sel[0];
    std::vector<EntRef> out;
    if (what == 0) {  // same tag
        int tag = r.kind == EntKind::Face ? E.faces[r.id].tag : r.kind == EntKind::Edge ? E.edges[r.id].tag : r.kind == EntKind::Instance ? E.insts[r.id].tag : 0;
        for (int i = 0; i < (int)E.faces.size(); ++i) if (E.faces[i].alive && E.faces[i].tag == tag) out.push_back({EntKind::Face, i});
        for (int i = 0; i < (int)E.edges.size(); ++i) if (E.edges[i].alive && E.edges[i].tag == tag) out.push_back({EntKind::Edge, i});
        for (int i = 0; i < (int)E.insts.size(); ++i) if (E.insts[i].alive && E.insts[i].tag == tag) out.push_back({EntKind::Instance, i});
    } else {  // same material
        int m = r.kind == EntKind::Face ? E.faces[r.id].matFront : r.kind == EntKind::Instance ? E.insts[r.id].material : -1;
        for (int i = 0; i < (int)E.faces.size(); ++i) if (E.faces[i].alive && (E.faces[i].matFront == m || E.faces[i].matBack == m)) out.push_back({EntKind::Face, i});
        for (int i = 0; i < (int)E.insts.size(); ++i) if (E.insts[i].alive && E.insts[i].material == m) out.push_back({EntKind::Instance, i});
    }
    doc_->setSelection(out);
}

void MainWindow::text3D() {
    QDialog dlg(this);
    dlg.setWindowTitle("Place 3D Text");
    auto* form = new QFormLayout(&dlg);
    auto* text = new QPlainTextEdit("Enter text");
    text->setMaximumHeight(70);
    auto* font = new QFontComboBox;
    auto* bold = new QCheckBox("Bold");
    auto* italic = new QCheckBox("Italic");
    auto* height = new QLineEdit(formatLength(250, doc_->model.units));
    auto* filled = new QCheckBox("Filled");
    filled->setChecked(true);
    auto* extruded = new QCheckBox("Extruded");
    extruded->setChecked(true);
    auto* depth = new QLineEdit(formatLength(50, doc_->model.units));
    form->addRow("Text:", text);
    form->addRow("Font:", font);
    auto* fr = new QWidget;
    auto* fl = new QHBoxLayout(fr);
    fl->setContentsMargins(0, 0, 0, 0);
    fl->addWidget(bold); fl->addWidget(italic);
    form->addRow("", fr);
    form->addRow("Height:", height);
    form->addRow("", filled);
    form->addRow("", extruded);
    form->addRow("Depth:", depth);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setText("Place");
    form->addRow(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    double H, Dp = 0;
    if (!parseLength(height->text(), doc_->model.units, H) || H <= 0) return;
    if (extruded->isChecked() && (!parseLength(depth->text(), doc_->model.units, Dp) || Dp <= 0)) Dp = 0;
    QFont f = font->currentFont();
    f.setPointSizeF(100);
    f.setBold(bold->isChecked());
    f.setItalic(italic->isChecked());
    QFontMetricsF fm(f);
    QPainterPath path;
    QStringList lines = text->toPlainText().split('\n');
    double y = 0;
    for (auto& l : lines) {
        path.addText(0, y, f, l);
        y += fm.lineSpacing();
    }
    double k = H / std::max(1.0, fm.capHeight());
    QTransform tf;
    tf.scale(k, -k);
    auto polys = path.toSubpathPolygons(tf);
    Entities E;
    struct P { std::vector<Vec2> pts; double area; };
    std::vector<P> ps;
    for (auto& poly : polys) {
        P p;
        for (auto& q : poly) {
            Vec2 v(q.x(), q.y());
            if (p.pts.empty() || (p.pts.back() - v).length() > 0.01) p.pts.push_back(v);
        }
        while (p.pts.size() > 2 && (p.pts.front() - p.pts.back()).length() < 0.01) p.pts.pop_back();
        if (p.pts.size() < 3) continue;
        p.area = signedArea2(p.pts.data(), p.pts.size());
        if (std::fabs(p.area) < 1e-3) continue;
        ps.push_back(p);
    }
    // nesting depth
    std::vector<int> depthOf(ps.size(), 0), parent(ps.size(), -1);
    for (size_t i = 0; i < ps.size(); ++i) {
        double bestA = 1e300;
        for (size_t j = 0; j < ps.size(); ++j) {
            if (i == j) continue;
            if (std::fabs(ps[j].area) <= std::fabs(ps[i].area)) continue;
            if (pointInPoly2(ps[i].pts[0], ps[j].pts.data(), ps[j].pts.size())) {
                ++depthOf[i];
                if (std::fabs(ps[j].area) < bestA) { bestA = std::fabs(ps[j].area); parent[i] = (int)j; }
            }
        }
    }
    auto toVerts = [&](const std::vector<Vec2>& pts, bool ccw) {
        std::vector<int> ids;
        for (auto& q : pts) ids.push_back(E.vertexAt(Vec3(q.x, q.y, 0)));
        double a = signedArea2(pts.data(), pts.size());
        if ((a > 0) != ccw) std::reverse(ids.begin(), ids.end());
        // remove consecutive duplicates
        std::vector<int> r;
        for (int v : ids) if (r.empty() || r.back() != v) r.push_back(v);
        while (r.size() > 1 && r.front() == r.back()) r.pop_back();
        return r;
    };
    std::vector<int> faces;
    for (size_t i = 0; i < ps.size(); ++i) {
        if (depthOf[i] % 2) continue;  // holes handled by their parent
        std::vector<int> outer = toVerts(ps[i].pts, true);
        if (outer.size() < 3) continue;
        std::vector<std::vector<int>> holes;
        for (size_t j = 0; j < ps.size(); ++j)
            if (parent[j] == (int)i && depthOf[j] % 2) {
                auto h = toVerts(ps[j].pts, false);
                if (h.size() >= 3) holes.push_back(h);
            }
        if (filled->isChecked()) {
            int fc = E.addFace(outer, holes, Plane(Vec3(0, 0, 1), Vec3()));
            if (fc >= 0) faces.push_back(fc);
        } else {
            for (size_t k2 = 0; k2 < outer.size(); ++k2) E.addEdge(outer[k2], outer[(k2 + 1) % outer.size()]);
            for (auto& h : holes) for (size_t k2 = 0; k2 < h.size(); ++k2) E.addEdge(h[k2], h[(k2 + 1) % h.size()]);
        }
    }
    if (filled->isChecked() && Dp > 0)
        for (int fc : faces) geom::pushPull(E, fc, Dp, false);
    // stand the text up: XY text plane -> XZ (readable from the front)
    Mat4 stand = Mat4::rotation(Vec3(1, 0, 0), kPi / 2);
    std::vector<int> allv;
    for (int v = 0; v < (int)E.verts.size(); ++v) if (E.verts[v].alive) allv.push_back(v);
    geom::transformVertices(E, allv, stand, false);
    doc_->begin("3D Text");
    int def = doc_->model.addDef("3D Text", false);
    doc_->model.defs[def].ents = std::move(E);
    doc_->commit();
    auto* t = static_cast<Text3DPlaceTool*>(view_->toolById("place"));
    t->def = def;
    view_->setTool("place");
    view_->setFocus();
}

// ======================================================================= context menu

void MainWindow::contextMenu(const QPoint& at) {
    if (view_->tool() && view_->tool()->busy()) return;
    QMenu m;
    const auto& sel = doc_->sel;
    int nf = 0, ne = 0, ni = 0;
    for (auto& r : sel) { if (r.kind == EntKind::Face) ++nf; else if (r.kind == EntKind::Edge) ++ne; else if (r.kind == EntKind::Instance) ++ni; }
    m.addAction("Entity Info", [this] { tray_->showPanel("Entity Info"); });
    if (!sel.empty()) {
        m.addAction(act_["delete"]);
        m.addAction(act_["hide"]);
        m.addAction(act_["zoomsel"]);
        auto* s = m.addMenu("Select");
        s->addAction("Bounding Edges", [this] {
            std::vector<EntRef> out;
            for (int f : doc_->selectedFaces())
                for (int e : doc_->active().faceEdges(f)) out.push_back({EntKind::Edge, e});
            doc_->setSelection(out);
        });
        s->addAction("Connected Faces", [this] {
            std::vector<EntRef> out;
            for (auto& r : doc_->sel)
                for (auto& c : geom::connected(doc_->active(), r, true))
                    if (c.kind == EntKind::Face) out.push_back(c);
            doc_->setSelection(out);
        });
        s->addAction("All Connected", [this] {
            std::vector<EntRef> out;
            for (auto& r : doc_->sel) { auto c = geom::connected(doc_->active(), r, true); out.insert(out.end(), c.begin(), c.end()); }
            doc_->setSelection(out);
        });
        s->addAction("All on Same Tag", [this] { selectSame(0); });
        s->addAction("All with Same Material", [this] { selectSame(1); });
        m.addSeparator();
    }
    if (ni == 1 && sel.size() == 1) {
        int i = sel[0].id;
        m.addAction("Edit Group/Component", [this, i] { doc_->openInstance(i); });
    }
    if (ni) {
        m.addAction(act_["explode"]);
        m.addAction(act_["makeunique"]);
        bool locked = false;
        for (int i : doc_->selectedInstances()) locked |= doc_->active().insts[i].locked;
        m.addAction(locked ? act_["unlock"] : act_["lock"]);
        m.addAction("Reset Scale", [this] {
            doc_->begin("Reset Scale");
            for (int i : doc_->selectedInstances()) {
                Mat4& xf = doc_->active().insts[i].xf;
                Vec3 o = xf.origin(), x = xf.axis(0).normalized(), y = xf.axis(1).normalized(), z = xf.axis(2).normalized();
                if (x.cross(y).dot(z) < 0) z = -z;   // undo mirroring too
                xf = Mat4::fromAxes(o, x, y, z);
            }
            doc_->active().touch();
            doc_->commit();
        });
    }
    if (!sel.empty()) {
        m.addAction(act_["makegroup"]);
        m.addAction(act_["makecomponent"]);
        auto* flip = m.addMenu("Flip Along");
        flip->addAction("Red Direction", [this] { flipAlong(0); });
        flip->addAction("Green Direction", [this] { flipAlong(1); });
        flip->addAction("Blue Direction", [this] { flipAlong(2); });
    }
    if (nf) {
        m.addSeparator();
        m.addAction(act_["reversefaces"]);
        m.addAction(act_["orientfaces"]);
        auto* in = m.addMenu("Intersect Faces");
        for (auto id : {"intersectmodel", "intersectsel", "intersectctx"}) in->addAction(act_[id]);
        m.addAction(act_["alignview"]);
        m.addAction("Align Axes", [this] {
            auto fs = doc_->selectedFaces();
            const Entities& E = doc_->active();
            Mat4 xf = doc_->activeXf();
            const Face& F = E.faces[fs[0]];
            Vec3 o = xf.point(E.verts[F.outer[0]].p);
            Vec3 x = (xf.point(E.verts[F.outer[1]].p) - o).normalized();
            Vec3 z = xf.normal(F.plane.n);
            doc_->begin("Align Axes");
            doc_->model.axesOrigin = o;
            doc_->model.axesX = x;
            doc_->model.axesZ = z;
            doc_->model.axesY = z.cross(x).normalized();
            doc_->commit();
        });
        double area = 0;
        for (int f : doc_->selectedFaces()) area += doc_->active().faceArea(f);
        m.addAction("Area: " + formatArea(area, doc_->model.units))->setEnabled(false);
    }
    if (ne) {
        m.addSeparator();
        m.addAction("Soften/Smooth Edges", [this] { tray_->showPanel("Soften Edges"); });
        if (ne == 1 && sel.size() == 1) m.addAction(act_["divide"]);
        if (ne > 1) m.addAction(act_["weld"]);
        bool anyCurve = false;
        for (int e : doc_->selectedEdges()) if (doc_->active().edges[e].curve >= 0) anyCurve = true;
        if (anyCurve) m.addAction(act_["explodecurve"]);
    }
    if (sel.size() == 1 && sel[0].kind == EntKind::Section) {
        int i = sel[0].id;
        m.addSeparator();
        m.addAction("Active Cut", [this, i] {
            doc_->begin("Active Cut");
            auto& s = doc_->active().sections;
            bool was = s[i].active;
            for (auto& x : s) x.active = false;
            s[i].active = !was;
            doc_->active().touch();
            doc_->commit();
        });
        m.addAction("Reverse", [this, i] {
            doc_->begin("Reverse Section");
            Plane& p = doc_->active().sections[i].plane;
            p.n = -p.n; p.d = -p.d;
            doc_->active().touch();
            doc_->commit();
        });
        m.addAction("Align View", [this, i] {
            const SectionPlane& s = doc_->active().sections[i];
            Mat4 xf = doc_->activeXf();
            Vec3 n = xf.normal(s.plane.n);
            CameraState to = *view_->cam.s;
            BBox b = doc_->model.worldBBox();
            Vec3 c = b.valid() ? Plane(n, xf.point(s.plane.origin())).project(b.center()) : xf.point(s.plane.origin());
            to.target = c;
            to.eye = c + n * std::max(view_->cam.distance(), 1000.0);
            to.up = std::fabs(n.z) > 0.99 ? Vec3(0, 1, 0) : Vec3(0, 0, 1);
            view_->animateTo(to, 400);
        });
    }
    if (doc_->inContext()) {
        m.addSeparator();
        m.addAction(act_["closegroup"]);
    }
    m.addSeparator();
    m.addAction(act_["zoomextents"]);
    if (!sel.empty()) m.addAction(act_["zoomsel"]);
    m.exec(at);
}

// ======================================================================= dialogs

void MainWindow::modelInfo() {
    QDialog dlg(this);
    dlg.setWindowTitle("Model Info");
    auto* lay = new QVBoxLayout(&dlg);
    auto* ug = new QGroupBox("Units");
    auto* uf = new QFormLayout(ug);
    UnitSettings& U = doc_->model.units;
    auto* fmt = new QComboBox;
    fmt->addItems({"Decimal — Millimetres", "Decimal — Centimetres", "Decimal — Metres", "Decimal — Inches", "Decimal — Feet", "Architectural (ft/in)"});
    fmt->setCurrentIndex((int)U.unit);
    auto* prec = new QSpinBox;
    prec->setRange(0, 6);
    prec->setValue(U.precision);
    auto* showU = new QCheckBox("Display units format");
    showU->setChecked(U.showUnits);
    auto* snap = new QCheckBox("Enable length snapping");
    snap->setChecked(U.lengthSnap);
    auto* snapV = new QLineEdit(formatLength(U.snapLength, U));
    auto* aprec = new QSpinBox;
    aprec->setRange(0, 4);
    aprec->setValue(U.anglePrecision);
    auto* asnap = new QCheckBox("Enable angle snapping");
    asnap->setChecked(U.angleSnapOn);
    auto* asnapV = new QDoubleSpinBox;
    asnapV->setRange(0.1, 90);
    asnapV->setValue(U.angleSnap);
    uf->addRow("Format:", fmt);
    uf->addRow("Precision:", prec);
    uf->addRow("", showU);
    uf->addRow("", snap);
    uf->addRow("Snap length:", snapV);
    uf->addRow("Angle precision:", aprec);
    uf->addRow("", asnap);
    uf->addRow("Angle snap:", asnapV);
    lay->addWidget(ug);
    auto* cg = new QGroupBox("Credits");
    auto* cf = new QFormLayout(cg);
    auto* author = new QLineEdit(doc_->model.author);
    auto* desc = new QPlainTextEdit(doc_->model.description);
    desc->setMaximumHeight(60);
    cf->addRow("Author:", author);
    cf->addRow("Description:", desc);
    lay->addWidget(cg);
    auto* sg = new QGroupBox("Statistics");
    auto* sf = new QFormLayout(sg);
    int edges = 0, faces = 0, groups = 0, comps = 0;
    std::function<void(const Entities&, int)> count = [&](const Entities& E, int depth) {
        if (depth > 32) return;
        edges += E.aliveCount(EntKind::Edge);
        faces += E.aliveCount(EntKind::Face);
        for (auto& in : E.insts) {
            if (!in.alive) continue;
            if (doc_->model.defs[in.def].isGroup) ++groups; else ++comps;
            count(doc_->model.defs[in.def].ents, depth + 1);
        }
    };
    count(doc_->model.root, 0);
    sf->addRow("Edges:", new QLabel(QString::number(edges)));
    sf->addRow("Faces:", new QLabel(QString::number(faces)));
    sf->addRow("Groups:", new QLabel(QString::number(groups)));
    sf->addRow("Component instances:", new QLabel(QString::number(comps)));
    sf->addRow("Materials:", new QLabel(QString::number(doc_->model.materials.size())));
    auto* purge = new QPushButton("Purge Unused");
    sf->addRow("", purge);
    connect(purge, &QPushButton::clicked, &dlg, [this] {
        doc_->begin("Purge Unused");
        doc_->model.purgeUnused();
        doc_->commit();
    });
    lay->addWidget(sg);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    U.unit = (LengthUnit)fmt->currentIndex();
    U.precision = prec->value();
    U.showUnits = showU->isChecked();
    U.lengthSnap = snap->isChecked();
    double sv;
    if (parseLength(snapV->text(), U, sv) && sv > 0) U.snapLength = sv;
    U.anglePrecision = aprec->value();
    U.angleSnapOn = asnap->isChecked();
    U.angleSnap = asnapV->value();
    doc_->model.author = author->text();
    doc_->model.description = desc->toPlainText();
    doc_->notifyChanged();
}

void MainWindow::shortcutsHelp() {
    QDialog dlg(this);
    dlg.setWindowTitle("Keyboard Shortcuts");
    dlg.resize(560, 640);
    auto* lay = new QVBoxLayout(&dlg);
    auto* table = new QTableWidget;
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({"Command", "Shortcut"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->verticalHeader()->hide();
    std::vector<std::pair<QString, QString>> rows;
    for (auto& [id, a] : act_)
        if (!a->shortcut().isEmpty()) rows.push_back({QString(a->text()).remove('&'), a->shortcuts().size() > 1 ? a->shortcuts()[0].toString() + ", " + a->shortcuts()[1].toString() : a->shortcut().toString()});
    std::sort(rows.begin(), rows.end());
    rows.push_back({"— Mouse —", ""});
    rows.push_back({"Orbit", "Middle drag"});
    rows.push_back({"Pan", "Shift + middle drag"});
    rows.push_back({"Zoom", "Scroll wheel"});
    rows.push_back({"Center view on point", "Middle double-click"});
    rows.push_back({"— Inference —", ""});
    rows.push_back({"Lock current inference", "Hold Shift"});
    rows.push_back({"Lock red / green / blue axis", "→ / ← / ↑"});
    rows.push_back({"Lock parallel to edge", "↓"});
    rows.push_back({"Move: toggle copy;  Push/Pull: new face", "Ctrl"});
    rows.push_back({"Array after copy", "type x5, 5x or /5 then Enter"});
    rows.push_back({"Absolute / relative coordinates", "[x,y,z] / <x,y,z>"});
    table->setRowCount((int)rows.size());
    for (int i = 0; i < (int)rows.size(); ++i) {
        table->setItem(i, 0, new QTableWidgetItem(rows[i].first));
        table->setItem(i, 1, new QTableWidgetItem(rows[i].second));
    }
    lay->addWidget(table);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    dlg.exec();
}

void MainWindow::checkRecovery() {
    QString fn = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/autosave.gbl";
    QFileInfo fi(fn);
    if (!fi.exists() || fi.size() == 0) return;
    auto r = QMessageBox::question(this, "Recover Model",
                                   QString("Folio Gable did not close normally. Recover the model auto-saved at %1?")
                                       .arg(fi.lastModified().toString("d MMM yyyy hh:mm")));
    if (r == QMessageBox::Yes) {
        QString err;
        if (doc_->load(fn, &err)) {
            doc_->filePath.clear();
            doc_->modified = true;
            updateTitle();
            view_->zoomExtents();
        }
    }
    QFile::remove(fn);
}

void MainWindow::loadShortcuts() {
    QSettings s;
    s.beginGroup("shortcuts");
    for (auto& key : s.childKeys()) {
        auto it = act_.find(key);
        if (it != act_.end()) it->second->setShortcut(QKeySequence(s.value(key).toString()));
    }
    s.endGroup();
    for (auto& [id, a] : toolAct_) {
        QString tip = QString(a->text()).remove('&');
        if (!a->shortcut().isEmpty()) tip += "  (" + a->shortcut().toString(QKeySequence::NativeText) + ")";
        a->setToolTip(tip);
    }
}

void MainWindow::preferences() {
    QDialog dlg(this);
    dlg.setWindowTitle("Preferences");
    dlg.resize(620, 680);
    auto* lay = new QVBoxLayout(&dlg);
    auto* tabs = new QTabWidget;
    lay->addWidget(tabs);
    // --- Shortcuts
    auto* sw = new QWidget;
    auto* sl = new QVBoxLayout(sw);
    auto* filter = new QLineEdit;
    filter->setPlaceholderText("Filter commands");
    filter->setClearButtonEnabled(true);
    sl->addWidget(filter);
    auto* table = new QTableWidget;
    table->setColumnCount(2);
    table->setHorizontalHeaderLabels({"Command", "Shortcut"});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
    table->setColumnWidth(1, 220);
    table->verticalHeader()->hide();
    std::vector<std::pair<QString, QString>> rows;
    for (auto& [id, a] : act_) rows.push_back({QString(a->text()).remove('&'), id});
    std::sort(rows.begin(), rows.end());
    table->setRowCount((int)rows.size());
    std::map<QString, QKeySequenceEdit*> editors;
    for (int i = 0; i < (int)rows.size(); ++i) {
        auto* item = new QTableWidgetItem(rows[i].first);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        table->setItem(i, 0, item);
        auto* ed = new QKeySequenceEdit(act_[rows[i].second]->shortcut());
        table->setCellWidget(i, 1, ed);
        editors[rows[i].second] = ed;
    }
    sl->addWidget(table);
    auto* reset = new QPushButton("Reset All to Defaults");
    sl->addWidget(reset, 0, Qt::AlignRight);
    connect(filter, &QLineEdit::textChanged, &dlg, [table](const QString& t) {
        for (int i = 0; i < table->rowCount(); ++i) table->setRowHidden(i, !table->item(i, 0)->text().contains(t, Qt::CaseInsensitive));
    });
    connect(reset, &QPushButton::clicked, &dlg, [&] {
        for (auto& [id, ed] : editors) ed->setKeySequence(defaultShortcuts_[id]);
    });
    tabs->addTab(sw, "Shortcuts");
    // --- General
    auto* gw = new QWidget;
    auto* gf = new QFormLayout(gw);
    QSettings st;
    auto* autosave = new QSpinBox;
    autosave->setRange(0, 120);
    autosave->setSuffix(" min");
    autosave->setSpecialValueText("Off");
    autosave->setValue(st.value("autosaveMinutes", 5).toInt());
    gf->addRow("Auto-save every:", autosave);
    auto* sysFrame = new QCheckBox("Use the system title bar instead of the Folio one (after a restart)");
    sysFrame->setChecked(st.value("systemTitleBar", false).toBool());
    gf->addRow("Window:", sysFrame);
    auto* ribbonMode = new QComboBox;
    ribbonMode->addItems({"Always show the ribbon", "Show tabs only"});
    ribbonMode->setCurrentIndex(ribbon_ && ribbon_->mode() == Ribbon::TabsOnly ? 1 : 0);
    gf->addRow("Ribbon:", ribbonMode);
    auto* qatShow = new QCheckBox("Show the Quick Access Toolbar");
    qatShow->setChecked(ribbon_ ? ribbon_->qatShown : true);
    gf->addRow("", qatShow);
    tabs->addTab(gw, "General");
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    lay->addWidget(bb);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    // detect conflicts
    std::map<QString, QString> used;
    for (auto& [id, ed] : editors) {
        QString k = ed->keySequence().toString();
        if (k.isEmpty()) continue;
        if (used.count(k)) {
            QMessageBox::warning(this, "Shortcuts", QString("\"%1\" is assigned to both \"%2\" and \"%3\". The second assignment was cleared.")
                                                        .arg(k, QString(act_[used[k]]->text()).remove('&'), QString(act_[id]->text()).remove('&')));
            ed->clear();
            continue;
        }
        used[k] = id;
    }
    st.beginGroup("shortcuts");
    st.remove("");
    for (auto& [id, ed] : editors) {
        QKeySequence ks = ed->keySequence();
        act_[id]->setShortcut(ks);
        if (ks != defaultShortcuts_[id]) st.setValue(id, ks.toString());
    }
    st.endGroup();
    st.setValue("autosaveMinutes", autosave->value());
    st.setValue("systemTitleBar", sysFrame->isChecked());
    if (ribbon_) {
        ribbon_->setMode(ribbonMode->currentIndex() == 1 ? Ribbon::TabsOnly : Ribbon::AlwaysShow);
        if (qatShow->isChecked() != ribbon_->qatShown) { ribbon_->qatShown = qatShow->isChecked(); ribbon_->onQatToggled(ribbon_->qatShown); }
    }
    if (autosave->value() > 0) autosave_.start(autosave->value() * 60 * 1000);
    else autosave_.stop();
    loadShortcuts();
}

void MainWindow::divideEdge() {
    auto es = doc_->selectedEdges();
    if (es.size() != 1) return;
    bool ok;
    int n = QInputDialog::getInt(this, "Divide", "Number of segments:", 2, 2, 999, 1, &ok);
    if (!ok) return;
    Entities& E = doc_->active();
    int e = es[0];
    int vb = E.edges[e].v[1];
    Vec3 a = E.verts[E.edges[e].v[0]].p, b = E.verts[vb].p;
    doc_->begin("Divide");
    int cur = e;
    for (int k = 1; k < n && cur >= 0; ++k) {
        int m = geom::splitEdge(E, cur, a + (b - a) * ((double)k / n));
        cur = E.findEdge(m, vb);
    }
    E.touch();
    doc_->commit();
    doc_->clearSelection();
}

void MainWindow::weldEdges() {
    auto es = doc_->selectedEdges();
    if (es.size() < 2) return;
    Entities& E = doc_->active();
    doc_->begin("Weld Edges");
    Curve c;
    c.closed = false;
    E.curves.push_back(c);
    int id = (int)E.curves.size() - 1;
    for (int e : es) E.edges[e].curve = id;
    E.touch();
    doc_->commit();
}

void MainWindow::explodeCurve() {
    Entities& E = doc_->active();
    doc_->begin("Explode Curve");
    for (int e : doc_->selectedEdges()) E.edges[e].curve = -1;
    E.touch();
    doc_->commit();
}

void MainWindow::playScenes(bool on) {
    QSignalBlocker b(act_["playscenes"]);
    act_["playscenes"]->setChecked(on && !doc_->model.scenes.empty());
    if (on && !doc_->model.scenes.empty()) {
        playIndex_ = std::max(0, sceneTabs_->currentIndex()) - 1;
        playTimer_.start(2600);
        int n = (int)doc_->model.scenes.size();
        playIndex_ = (playIndex_ + 1 + n) % n;
        sceneTabs_->setCurrentIndex(playIndex_);
        scenes_->applyScene(playIndex_);
    } else playTimer_.stop();
}

void MainWindow::fromContours() {
    const Entities& A = doc_->active();
    std::vector<Vec3> pts;
    BBox box;
    auto addEdge = [&](const Vec3& a, const Vec3& b) {
        pts.push_back(a);
        pts.push_back(b);
        box.add(a);
        box.add(b);
    };
    for (auto& r : doc_->sel) {
        if (r.kind == EntKind::Edge) {
            const Edge& Ed = A.edges[r.id];
            addEdge(A.verts[Ed.v[0]].p, A.verts[Ed.v[1]].p);
        } else if (r.kind == EntKind::Instance) {
            Entities flat;
            flattenInstance(doc_->model, A.insts[r.id], Mat4(), flat);
            for (auto& Ed : flat.edges)
                if (Ed.alive) addEdge(flat.verts[Ed.v[0]].p, flat.verts[Ed.v[1]].p);
        }
    }
    if (pts.size() < 3) {
        QMessageBox::information(this, "From Contours", "Select contour lines (edges or groups of edges) first.");
        return;
    }
    // densify long segments so the surface follows the contours
    double step = std::max(box.size().x, box.size().y) / 60;
    std::vector<Vec3> dense;
    for (size_t i = 0; i + 1 < pts.size(); i += 2) {
        Vec3 a = pts[i], b = pts[i + 1];
        int n = std::max(1, (int)std::ceil((b - a).length() / std::max(step, 1.0)));
        for (int k = 0; k <= n; ++k) dense.push_back(a + (b - a) * ((double)k / n));
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    doc_->begin("Sandbox From Contours");
    int def = doc_->model.addDef("Terrain", true);
    Entities T;
    int n = sandbox::tinFromPoints(T, dense);
    doc_->model.defs[def].ents = std::move(T);
    Instance in;
    in.def = def;
    doc_->active().insts.push_back(in);
    doc_->active().touch();
    doc_->commit();
    QApplication::restoreOverrideCursor();
    doc_->setSelection({{EntKind::Instance, (int)doc_->active().insts.size() - 1}});
    statusBar()->showMessage(QString("Created terrain with %1 triangles").arg(n), 4000);
}

void MainWindow::addDetail() {
    auto fs = doc_->selectedFaces();
    if (fs.empty()) return;
    doc_->begin("Add Detail");
    sandbox::subdivide(doc_->active(), fs);
    doc_->commit();
}

void MainWindow::about() {
    QMessageBox box(this);
    box.setWindowTitle("About Folio Gable");
    box.setIconPixmap(appIconPixmap(96));
    box.setText("<b>Folio Gable " + QApplication::applicationVersion() + "</b><br>The 3D modeler of the Folio office suite.<br><br>"
                "Sticky geometry, inference, push/pull, groups &amp; components, solid tools, "
                "sections, shadows, scenes and OBJ/STL/DAE/DXF interchange.");
    box.exec();
}

}  // namespace gable
