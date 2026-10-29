// Folio Gable — the suite chrome around the modeler: title bar with the Quick Access Toolbar
// and command search, the ribbon (tabs, groups, KeyTips) and the File view.
#include "ui/Backstage.h"
#include "ui/Icons.h"
#include "ui/MainWindow.h"
#include "ui/Panels.h"
#include "ui/Ribbon.h"
#include "ui/Theme.h"
#include "ui/TitleBar.h"
#include "view/Viewport.h"
#include "tools/Tool.h"
#include <QApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QMenu>
#include <QResizeEvent>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabBar>
#include <QVBoxLayout>

namespace gable {

// ------------------------------------------------------------------ icons and screentips

static QString iconFor(const QString& id) {
    static const std::map<QString, QString> m = {
        {"component", "makecomponent"}, {"group", "makegroup"}, {"solid", "solid_outer"}, {"wireframe", "fs_wire"},
        {"hiddenline", "fs_hidden"}, {"shaded", "fs_shaded"}, {"textured", "fs_textured"}, {"mono", "fs_mono"},
        {"xray", "fs_xray"}, {"backedges", "fs_backedges"}, {"export3d", "export"}, {"exportstl", "solid_outer"},
        {"exportobj", "export"}, {"exportgltf", "export"}, {"export3mf", "solid_union"}, {"export3ds", "materials"}, {"exportkmz", "shadows"}, {"exportifc", "component"}, {"export2d", "exportimage"}, {"quit", "delete"}, {"unhideall", "unhide"},
        {"unhidesel", "unhide"}, {"intersectmodel", "intersect"}, {"intersectsel", "intersect"},
        {"intersectctx", "intersect"}, {"axes", "showaxes"}, {"prevview", "previous"}, {"text3d", "text3d"},
        {"sbdetail", "sbdetail"}, {"fs_wire", "fs_wire"}, {"fs_hidden", "fs_hidden"}, {"fs_shaded", "fs_shaded"},
        {"fs_textured", "fs_textured"}, {"fs_mono", "fs_mono"}, {"edges", "edges"}, {"profiles", "profiles"},
        {"extension", "extension"}, {"guides", "guides"}, {"hiddengeom", "hiddengeom"}, {"fog", "fog"},
        {"shadows", "shadows"}, {"sectionplanes", "sectionplanes"}, {"sectioncuts", "sectioncuts"},
        {"sectionfill", "sectionfill"}, {"fov", "fov"}, {"zoomsel", "zoomsel"}, {"alignview", "alignview"},
        {"playscenes", "playscenes"}, {"scenetabs", "scenetabs"}, {"hiderest", "hiderest"}, {"divide", "divide"},
        {"weld", "weld"}, {"explodecurve", "explodecurve"}, {"modelinfo", "modelinfo"}, {"preferences", "preferences"},
        {"shortcuts", "shortcuts"}, {"about", "about"}, {"deleteguides", "deleteguides"}, {"selectall", "selectall"},
        {"selectnone", "selectnone"}, {"invertsel", "invertsel"}, {"closegroup", "closegroup"},
        {"makeunique", "makeunique"}, {"reversefaces", "reversefaces"}, {"orientfaces", "orientfaces"},
        {"pasteinplace", "pasteinplace"}, {"solid_outer", "solid_outer"}, {"solid_union", "solid_union"},
        {"solid_subtract", "solid_subtract"}, {"solid_intersect", "solid_intersect"}, {"solid_trim", "solid_trim"},
        {"solid_split", "solid_split"}, {"import", "import"}, {"print", "print"}, {"saveas", "saveas"},
        {"lock", "lock"}, {"unlock", "unlock"}, {"hide", "hide"}, {"explode", "explode"}, {"cut", "cut"},
        {"copy", "copy"}, {"paste", "paste"}, {"delete", "delete"}, {"addscene", "addscene"},
        {"nextscene", "nextscene"}, {"prevscene", "prevscene"}, {"perspective", "perspective"}, {"parallel", "parallel"},
        {"twopoint", "twopoint"}, {"matchphoto", "matchphoto"}, {"matchphoto_edit", "matchphoto"}, {"matchphoto_done", "matchphoto"},
        {"matchphoto_turn", "matchphoto"},
    };
    auto it = m.find(id);
    if (it != m.end()) return it->second;
    return id;
}

static const std::map<QString, QString>& descriptions() {
    static const std::map<QString, QString> d = {
        {"new", "Start a new model."},
        {"open", "Open a Folio Gable model, or a 3D model in another format."},
        {"save", "Save this model."},
        {"saveas", "Save this model under a new name or in another folder."},
        {"import", "Bring an OBJ, STL, COLLADA, DXF or Gable model into this one as a group."},
        {"export3d", "Save the model as STL, OBJ, COLLADA or DXF."},
        {"exportstl", "Save the model as an STL file for 3D printing."},
        {"exportobj", "Save the model as a Wavefront OBJ file with its materials."},
        {"exportgltf", "Save the model as glTF for Blender, game engines and the web."},
        {"export3mf", "Save the model as 3MF for 3D printing, with colours."},
        {"export3ds", "Save the model as 3D Studio (.3ds), with materials."},
        {"exportkmz", "Save the model for Google Earth, placed at its location."},
        {"exportifc", "Save the model as IFC 2x3: each group a building element."},
        {"copyview", "Copy what the view shows as a picture, to paste into Draw, Sheet or other programs."},
        {"interact", "Click dynamic components to make them act: open a door, change a finish."},
        {"componentoptions", "Change the options of the selected dynamic component: its size, finish and so on."},
        {"componentattributes", "Give a component behaviour: attributes and formulas that size, place, copy and animate its parts."},
        {"extensions", "Not available: Folio Gable can't run SketchUp extensions."},
        {"rubyconsole", "Not available: Folio Gable can't run Ruby scripts."},
        {"sendtolayout", "Drawing sheets: put scenes on paper with dimensions, labels and a title block; print them or save a PDF."},
        {"sendemail", "Send this model in a new email message, as an attachment."},
        {"sendpicture", "Send a picture of the view in a new email message."},
        {"solidinspector", "Show why the selected group isn't a solid (holes, stray edges, reversed faces) and fix it."},
        {"export2d", "Save the current view as a PNG or JPEG image at any size."},
        {"print", "Print the current view."},
        {"quit", "Close Folio Gable."},
        {"undo", "Undo the last change."},
        {"redo", "Redo the change you just undid."},
        {"cut", "Remove the selection and put it on the clipboard."},
        {"copy", "Put a copy of the selection on the clipboard."},
        {"paste", "Place the clipboard contents with the mouse."},
        {"pasteinplace", "Paste the clipboard contents at the position they were copied from."},
        {"delete", "Erase the selected entities."},
        {"deleteguides", "Erase every guide line and guide point."},
        {"selectall", "Select everything in the current context."},
        {"selectnone", "Clear the selection."},
        {"invertsel", "Select everything that is not selected, and deselect the rest."},
        {"hide", "Hide the selected entities. Show them again with Unhide or View > Hidden Geometry."},
        {"unhideall", "Show every hidden entity in the current context."},
        {"unhidesel", "Show the selected hidden entities."},
        {"lock", "Protect the selected groups and components from changes."},
        {"unlock", "Allow the selected groups and components to be changed again."},
        {"makecomponent", "Turn the selection into a component: every copy shares the same definition."},
        {"makegroup", "Gather the selection into a group so it no longer sticks to other geometry."},
        {"closegroup", "Stop editing the group or component and return to the model."},
        {"explode", "Break the selected groups and components back into loose geometry."},
        {"makeunique", "Give the selected component copies a definition of their own."},
        {"reversefaces", "Swap the front and back of the selected faces."},
        {"orientfaces", "Turn every face connected to the selected one the same way round."},
        {"intersectmodel", "Draw edges where the selection crosses the rest of the model."},
        {"intersectsel", "Draw edges where the selected faces cross each other."},
        {"intersectctx", "Draw edges where the selection crosses the rest of its group or component."},
        {"hiddengeom", "Show hidden and smoothed geometry as dashed lines."},
        {"sectionplanes", "Show the section plane markers."},
        {"sectioncuts", "Cut the model at the active section plane."},
        {"sectionfill", "Fill the cut surfaces of a section."},
        {"axes", "Show the drawing axes."},
        {"guides", "Show guide lines and guide points."},
        {"edges", "Draw the edges of the model."},
        {"backedges", "Show the edges behind faces as dashed lines."},
        {"profiles", "Draw the outline of shapes with a thicker line."},
        {"extension", "Let edges run slightly past their ends, like a sketch."},
        {"xray", "See through faces to the geometry behind them."},
        {"fog", "Fade the model into the background with distance."},
        {"shadows", "Cast sun shadows for the date, time and place set in the Shadows panel."},
        {"fs_wire", "Show only edges."},
        {"fs_hidden", "Show edges and plain white faces."},
        {"fs_shaded", "Show faces in their colours without textures."},
        {"fs_textured", "Show faces with their colours and textures."},
        {"fs_mono", "Show faces in the default front and back colours only."},
        {"hiderest", "While editing a group or component, hide the rest of the model."},
        {"scenetabs", "Show the scene tabs above the drawing area."},
        {"nextscene", "Go to the next scene."},
        {"prevscene", "Go to the previous scene."},
        {"addscene", "Save the current view and display settings as a scene."},
        {"playscenes", "Play the scenes as an animation."},
        {"prevview", "Go back to the previous camera position."},
        {"view_top", "Look straight down on the model."},
        {"view_bottom", "Look straight up at the model."},
        {"view_front", "Look at the model from the front."},
        {"view_back", "Look at the model from the back."},
        {"view_left", "Look at the model from the left."},
        {"view_right", "Look at the model from the right."},
        {"view_iso", "Look at the model from above at an angle."},
        {"parallel", "Show the model without perspective, like a technical drawing."},
        {"perspective", "Show the model in perspective, as the eye sees it."},
        {"fov", "Set how wide the camera sees."},
        {"twopoint", "Keep vertical lines vertical, like an architectural photo. Orbiting turns it off."},
        {"matchphoto", "Match the camera to a photograph: place lines on its edges and model over the picture."},
        {"matchphoto_edit", "Move the grips of the matched photo shown in the view."},
        {"matchphoto_done", "Finish placing the grips and keep the matched camera."},
        {"matchphoto_turn", "Turn the red and green axes of the matched photo around by 180 degrees."},
        {"zoomextents", "Zoom out or in so the whole model fits the window."},
        {"zoomsel", "Zoom so the selection fills the window."},
        {"alignview", "Look straight at the selected face."},
        {"text3d", "Create letters as 3D geometry, with or without depth."},
        {"sbcontours", "Build a terrain surface from the selected contour lines."},
        {"sbdetail", "Split the selected terrain faces into smaller ones for more detail."},
        {"solid_outer", "Keep only the outer surface of the selected solids, joined into one."},
        {"solid_union", "Join the selected solids into one, keeping inner volumes."},
        {"solid_subtract", "Cut the first selected solid away from the second."},
        {"solid_intersect", "Keep only where the selected solids overlap."},
        {"solid_trim", "Cut the first solid out of the second but keep the first."},
        {"solid_split", "Split the selected solids into their overlapping and separate parts."},
        {"modelinfo", "Units, dimensions, text, statistics and other settings of this model."},
        {"shortcuts", "List every keyboard shortcut."},
        {"about", "Version and credits."},
        {"preferences", "Shortcuts, auto-save, drawing and window options."},
        {"divide", "Split the selected edge into equal segments."},
        {"weld", "Join the selected edges into one curve."},
        {"explodecurve", "Break a curve back into separate edges."},
        // tools
        {"select", "Select entities. Double-click a face to add its edges; triple-click for everything connected."},
        {"line", "Draw edges. Lines that close a loop make a face."},
        {"freehand", "Draw a freehand curve."},
        {"rect", "Draw a rectangle from corner to corner."},
        {"rotrect", "Draw a rectangle at any angle: two corners for one side, then the width."},
        {"circle", "Draw a circle from its centre. Type a number followed by s to set the segments."},
        {"polygon", "Draw a regular polygon from its centre."},
        {"arc2", "Draw an arc from its two ends, then its bulge."},
        {"arc", "Draw an arc from its centre, a start point and an end point."},
        {"arc3", "Draw an arc through three points."},
        {"pie", "Draw a pie slice from its centre."},
        {"pushpull", "Push or pull a face to add or remove volume. Ctrl starts a new face."},
        {"move", "Move, stretch or copy (Ctrl) the selection. Type x3 after a copy for an array."},
        {"rotate", "Rotate or rotate-copy (Ctrl) the selection about a point."},
        {"scale", "Resize the selection with grips. Ctrl scales about the centre, Shift keeps proportions."},
        {"offset", "Draw a copy of a face's edges at a distance inside or outside."},
        {"followme", "Sweep a face along a path of edges. Alt follows a face's perimeter."},
        {"eraser", "Erase edges by clicking or dragging. Shift hides, Ctrl softens."},
        {"paint", "Paint faces with the current material. Ctrl fills connected, Shift replaces, Alt samples."},
        {"tape", "Measure distances and draw guide lines. Ctrl draws no guide."},
        {"protractor", "Measure angles and draw angled guide lines."},
        {"dimension", "Place a linear or radial dimension."},
        {"text", "Place a label with a leader, or screen text."},
        {"axes", "Move or reorient the drawing axes."},
        {"section", "Place a section plane to cut through the model."},
        {"orbit", "Turn the view around the model. The middle mouse button orbits too."},
        {"pan", "Slide the view. Shift with the middle mouse button pans too."},
        {"zoom", "Zoom by dragging up and down, or use the scroll wheel."},
        {"zoomwindow", "Zoom to a rectangle you drag."},
        {"walk", "Walk through the model at eye height."},
        {"lookaround", "Turn the camera from where it stands."},
        {"poscam", "Stand at a point and look in a direction, at eye height."},
        {"sbscratch", "Draw a flat terrain grid to sculpt."},
        {"smoove", "Raise or lower terrain smoothly."},
        {"flipedge", "Flip the diagonal of two terrain triangles."},
        {"drape", "Project edges down onto a terrain surface."},
    };
    return d;
}

void MainWindow::decorateActions() {
    const auto& d = descriptions();
    for (auto& [id, a] : act_) {
        QString key = id.startsWith("tool_") ? id.mid(5) : id;
        QString ic = a->property("iconName").toString();
        ic = iconFor(ic.isEmpty() ? key : ic);
        if (!theme::hasIcon(ic)) ic = iconFor(key);
        if (theme::hasIcon(ic)) {
            a->setProperty("iconName", ic);
            a->setIcon(theme::icon(ic, false));
        }
        auto it = d.find(key);
        setScreentip(a, it != d.end() ? it->second : QString());
    }
}

// ------------------------------------------------------------------ chrome

void MainWindow::buildChrome() {
    QSettings s;
    bool systemFrame = s.value("systemTitleBar", false).toBool();
    if (!systemFrame) setWindowFlag(Qt::FramelessWindowHint, true);
    auto* top = new QWidget;
    auto* tv = new QVBoxLayout(top);
    tv->setContentsMargins(0, 0, 0, 0);
    tv->setSpacing(0);
    titleBar_ = new TitleBar(this);
    titleBar_->setWindowButtonsVisible(!systemFrame);
    tv->addWidget(titleBar_);
    ribbon_ = new Ribbon;
    tv->addWidget(ribbon_);
    setMenuWidget(top);
    resizer_ = new FramelessResizer(this);

    buildRibbon();

    // Quick Access Toolbar
    qatIds_ = s.value("qat", QStringList{"save", "undo", "redo"}).toStringList();
    auto* qm = new QMenu(this);
    titleBar_->qatMenuButton()->setMenu(qm);
    connect(qm, &QMenu::aboutToShow, this, [this, qm] {
        qm->clear();
        qm->addSection("Customize Quick Access Toolbar");
        const char* ids[] = {"new", "open", "save", "undo", "redo", "print", "export2d", "exportstl", "zoomextents",
                             "tool_orbit", "tool_select", "makegroup"};
        for (auto id : ids) {
            QString key = id;
            if (!act_.count(key)) continue;
            QAction* x = qm->addAction(plainText(act_[key]->text()));
            x->setCheckable(true);
            x->setChecked(qatIds_.contains(key));
            connect(x, &QAction::triggered, this, [this, key](bool on) {
                if (on && !qatIds_.contains(key)) qatIds_ << key;
                if (!on) qatIds_.removeAll(key);
                QSettings().setValue("qat", qatIds_);
                applyQat();
            });
        }
        qm->addSeparator();
        qm->addAction(ribbon_->qatShown ? "Hide Quick Access Toolbar" : "Show Quick Access Toolbar", this, [this] {
            ribbon_->qatShown = !ribbon_->qatShown;
            ribbon_->onQatToggled(ribbon_->qatShown);
        });
    });
    ribbon_->onQatToggled = [this](bool on) {
        for (auto* b : titleBar_->qatButtons()) b->setVisible(on);
        QSettings().setValue("qatShown", on);
    };
    ribbon_->qatShown = s.value("qatShown", true).toBool();
    applyQat();

    // command search
    std::vector<QAction*> all;
    for (auto& [id, a] : act_) all.push_back(a);
    titleBar_->setSearchActions(all);
    addAct("search", "Search Commands", QKeySequence("Alt+Q"), [this] { titleBar_->focusSearch(); });

    // KeyTips
    keytips_ = new KeyTips(this, ribbon_);
    keytips_->allowed = [this] { return !(view_->tool() && view_->tool()->busy()) && !(backstage_ && backstage_->isOpen()); };
    keytips_->onFile = [this] { showBackstage(); };
    applyQat();

    // File view
    BackstageHost h;
    h.templates = [] {
        return std::vector<ModelTemplate>{
            {"Millimetres", "Blank model"},
            {"Centimetres", "Blank model"},
            {"Metres", "Blank model"},
            {"Inches", "Blank model"},
            {"Architectural", "Feet and inches"},
            {"Woodworking", "Inches to 1/16\""},
            {"3D Printing", "Millimetres, 0.01 mm"},
        };
    };
    h.newFromTemplate = [this](int i) { newFromTemplate(i); };
    h.openDialog = [this] { open(); };
    h.openPath = [this](const QString& p) { if (maybeSave()) openFile(p); };
    h.recent = [] { return QSettings().value("recent").toStringList(); };
    h.forgetRecent = [this](const QString& p) { forgetRecent(p); };
    h.save = [this] { save(); };
    h.saveAs = [this] { saveAs(); };
    h.importFile = [this] { importFile(); };
    h.print = [this] { print(); };
    h.exportImage = [this] { exportImage(); };
    h.exportModel = [this](const QString& ext) { exportModel(ext); };
    h.notAvailable = [this](const QString& what) { notAvailable(what); };
    h.drawingSheets = [this] { drawingSheets(); };
    h.sendEmail = [this](const QString& what) { sendEmail(what); };
    h.options = [this] { preferences(); };
    h.modelInfo = [this] { modelInfo(); };
    h.purge = [this] {
        doc_->begin("Purge Unused");
        doc_->model.purgeUnused();
        doc_->commit();
        statusBar()->showMessage("Unused components, materials and tags purged.", 4000);
    };
    h.closeModel = [this] { newFile(); };
    h.filePath = [this] { return doc_->filePath; };
    h.properties = [this] {
        std::vector<std::pair<QString, QString>> r;
        const Model& m = doc_->model;
        if (!doc_->filePath.isEmpty()) {
            QFileInfo fi(doc_->filePath);
            if (fi.exists()) {
                r.push_back({"Size", QString("%1 KB").arg(fi.size() / 1024.0, 0, 'f', 1)});
                r.push_back({"Last modified", fi.lastModified().toString("M/d/yyyy h:mm AP")});
            }
        }
        static const char* units[] = {"Millimetres", "Centimetres", "Metres", "Inches", "Feet", "Feet and inches"};
        r.push_back({"Units", units[std::clamp((int)m.units.unit, 0, 5)]});
        int faces = 0, edges = 0, groups = 0, comps = 0;
        std::function<void(const Entities&)> count = [&](const Entities& E) {
            faces += E.aliveCount(EntKind::Face);
            edges += E.aliveCount(EntKind::Edge);
        };
        count(m.root);
        for (auto& dfn : m.defs) { count(dfn.ents); if (dfn.isGroup) ++groups; else ++comps; }
        r.push_back({"Faces", QString::number(faces)});
        r.push_back({"Edges", QString::number(edges)});
        r.push_back({"Groups", QString::number(groups)});
        r.push_back({"Component definitions", QString::number(comps)});
        r.push_back({"Materials", QString::number(m.materials.size())});
        r.push_back({"Tags", QString::number(m.tags.size())});
        r.push_back({"Scenes", QString::number(m.scenes.size())});
        BBox b = m.worldBBox();
        if (b.valid()) {
            Vec3 sz = b.hi - b.lo;
            r.push_back({"Size of model", formatLength(sz.x, m.units) + " × " + formatLength(sz.y, m.units) + " × " +
                                              formatLength(sz.z, m.units)});
        }
        return r;
    };
    h.thumbnail = [this](const QString& p) { return thumbnailFor(p); };
    backstage_ = new Backstage(h, this);
    connect(ribbon_, &Ribbon::fileClicked, this, [this] { showBackstage(); });
    connect(backstage_, &Backstage::closed, this, [this] { view_->setFocus(); });

    // ribbon display mode
    connect(ribbon_, &Ribbon::modeChanged, this, [this](int m) {
        QSettings().setValue("ribbonMode", m);
        titleBar_->setVisible(m != Ribbon::FullScreen);
        if (m == Ribbon::FullScreen) showFullScreen();
        else if (isFullScreen()) showMaximized();
    });
    int mode = s.value("ribbonMode", 0).toInt();
    if (mode == Ribbon::TabsOnly) ribbon_->setMode(Ribbon::TabsOnly);
    addAct("ribboncollapse", "Collapse the Ribbon", QKeySequence("Ctrl+F1"), [this] { ribbon_->toggleCollapsed(); });
    // Esc leaves full-screen mode (when no tool is using Esc)
    addAct("leavefullscreen", "Leave Full-screen Mode", QKeySequence(), [this] { ribbon_->setMode(Ribbon::AlwaysShow); });

    // contextual tab while a group or component is open
    connect(doc_, &Document::contextChanged, this, [this] {
        if (groupTab_) {
            bool on = doc_->inContext();
            ribbon_->setTabVisible(groupTab_, on);
            if (on) ribbon_->select(groupTab_);
        }
    });
    updateTitle();
    syncViewToggles();
}

void MainWindow::applyQat() {
    std::vector<std::pair<QAction*, QString>> v;
    std::vector<std::pair<QString, QAbstractButton*>> kt;
    for (const QString& id : qatIds_)
        if (act_.count(id)) v.push_back({act_[id], act_[id]->property("iconName").toString()});
    titleBar_->setQat(v);
    int n = 1;
    for (auto* b : titleBar_->qatButtons()) {
        b->setVisible(ribbon_->qatShown);
        kt.push_back({QString::number(n++), b});
    }
    if (keytips_) keytips_->qat = kt;
}

void MainWindow::showBackstage(const QString& page) {
    if (view_->tool()) view_->tool()->reset();
    placeBackstage();
    backstage_->open(page);
}

void MainWindow::placeBackstage() {
    if (!backstage_) return;
    int y = titleBar_ && titleBar_->isVisible() ? titleBar_->height() : 0;
    backstage_->setGeometry(0, y, width(), height() - y);
}

void MainWindow::resizeEvent(QResizeEvent* e) {
    QMainWindow::resizeEvent(e);
    placeBackstage();
}

void MainWindow::changeEvent(QEvent* e) {
    QMainWindow::changeEvent(e);
    if (e->type() == QEvent::WindowStateChange && titleBar_) titleBar_->syncMaximized();
}

// ------------------------------------------------------------------ templates, thumbnails, recent

void MainWindow::newFromTemplate(int i) {
    if (!maybeSave()) return;
    newFile();
    UnitSettings& u = doc_->model.units;
    switch (i) {
    case 0: u.unit = LengthUnit::Millimeter; u.precision = 0; break;
    case 1: u.unit = LengthUnit::Centimeter; u.precision = 1; break;
    case 2: u.unit = LengthUnit::Meter; u.precision = 2; break;
    case 3: u.unit = LengthUnit::Inch; u.precision = 2; break;
    case 4: u.unit = LengthUnit::ArchitecturalInch; u.precision = 3; break;
    case 5: u.unit = LengthUnit::ArchitecturalInch; u.precision = 4; break;
    case 6: u.unit = LengthUnit::Millimeter; u.precision = 2; break;
    default: break;
    }
    doc_->modified = false;
    updateTitle();
    view_->update();
}

static QString thumbPath(const QString& file) {
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbs";
    QByteArray h = QCryptographicHash::hash(QFileInfo(file).absoluteFilePath().toUtf8(), QCryptographicHash::Md5).toHex();
    return dir + "/" + QString::fromLatin1(h) + ".png";
}

void MainWindow::saveThumbnail(const QString& path) {
    QString tp = thumbPath(path);
    QDir().mkpath(QFileInfo(tp).absolutePath());
    QImage img = view_->renderImage(240, 170, false);
    if (!img.isNull()) img.scaled(96, 68, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(tp);
}

QPixmap MainWindow::thumbnailFor(const QString& path) const {
    QString tp = thumbPath(path);
    if (!QFileInfo::exists(tp)) return QPixmap();
    if (QFileInfo(tp).lastModified() < QFileInfo(path).lastModified().addSecs(-5)) return QPixmap();
    return QPixmap(tp);
}

void MainWindow::forgetRecent(const QString& p) {
    QSettings s;
    QStringList r = s.value("recent").toStringList();
    r.removeAll(p);
    s.setValue("recent", r);
}

// ------------------------------------------------------------------ the ribbon

void MainWindow::buildRibbon() {
    auto A = [this](const QString& id) -> QAction* {
        if (act_.count(id)) return act_[id];
        if (toolAct_.count(id)) return toolAct_[id];
        qWarning("ribbon: no action %s", qPrintable(id));
        return nullptr;
    };
    auto T = [this](const QString& id) { return toolAct_.at(id); };
    auto menuOf = [this](std::initializer_list<const char*> ids) {
        auto* m = new QMenu(this);
        for (auto id : ids) {
            if (QString(id) == "-") { m->addSeparator(); continue; }
            QAction* a = act_.count(id) ? act_[id] : toolAct_[id];
            m->addAction(a);
        }
        return m;
    };
    // panel actions (open a tray panel)
    auto panel = [this](const QString& id, const QString& title, const QString& icon, const QString& desc) {
        QAction* a = addAct(id, title, QKeySequence(), [this, title] {
            if (trayDock_) { trayDock_->show(); trayDock_->raise(); }
            tray_->showPanel(title);
        }, icon);
        a->setIcon(theme::icon(icon, false));
        setScreentip(a, desc);
        return a;
    };
    QAction* pInfo = panel("panel_info", "Entity Info", "entityinfo", "Show the properties of the selection.");
    QAction* pMat = panel("panel_materials", "Materials", "materials", "Choose, create and edit materials.");
    QAction* pComp = panel("panel_components", "Components", "components", "Place and manage component definitions.");
    QAction* pStyles = panel("panel_styles", "Styles", "styles", "Edge, face, background and watermark settings.");
    QAction* pTags = panel("panel_tags", "Tags", "tags", "Show, hide and colour groups of entities.");
    QAction* pScenes = panel("panel_scenes", "Scenes", "scenes", "Create, update and reorder scenes.");
    QAction* pShadows = panel("panel_shadows", "Shadows", "shadows", "Time, date and location of the sun.");
    QAction* pFog = panel("panel_fog", "Fog", "fog", "Distance and colour of the fog.");
    QAction* pOutliner = panel("panel_outliner", "Outliner", "outliner", "The groups and components of the model as a tree.");
    QAction* pSoften = panel("panel_soften", "Soften Edges", "soften", "Soften and smooth edges by the angle between faces.");
    QAction* trayToggle = addAct("panel_tray", "Default Tray", QKeySequence(), [this] {
        if (!trayDock_) return;
        trayDock_->setVisible(!trayDock_->isVisible());
    }, "outliner");
    trayToggle->setCheckable(true);
    trayToggle->setChecked(true);
    if (trayDock_) connect(trayDock_, &QDockWidget::visibilityChanged, trayToggle, [trayToggle](bool v) {
        trayToggle->setChecked(v);
    });
    trayToggle->setIcon(theme::icon("outliner", false));
    setScreentip(trayToggle, "Show or hide the tray of panels on the right.");
    // flip along
    QAction* flipR = addAct("flip_red", "Red Direction", QKeySequence(), [this] { flipAlong(0); }, "flip");
    QAction* flipG = addAct("flip_green", "Green Direction", QKeySequence(), [this] { flipAlong(1); }, "flip");
    QAction* flipB = addAct("flip_blue", "Blue Direction", QKeySequence(), [this] { flipAlong(2); }, "flip");
    for (auto* a : {flipR, flipG, flipB}) { a->setIcon(theme::icon("flip", false)); setScreentip(a, "Mirror the selection along this axis."); }
    auto* flipMenu = new QMenu(this);
    flipMenu->addActions({flipR, flipG, flipB});
    auto* intersectMenu = menuOf({"intersectmodel", "intersectsel", "intersectctx"});
    auto* pasteMenu = menuOf({"paste", "pasteinplace"});

    Ribbon* R = ribbon_;
    // ---------------------------------------------------------------- Home
    {
        RibbonTab* t = R->addTab("Home", "H");
        RibbonGroup* g = t->group("Clipboard");
        g->largeMenu("Paste", "paste", pasteMenu, A("paste"));
        g->column({A("cut"), A("copy"), A("delete")});
        g->column({A("copyview")});
        g = t->group("Select");
        g->large(T("select"), "Select", "select");
        g->column({A("selectall"), A("selectnone"), A("invertsel")});
        g = t->group("Draw");
        g->large(T("line"), "Line", "line");
        g->column({T("rect"), T("circle"), T("arc2")});
        g = t->group("Edit");
        g->large(T("pushpull"), "Push/Pull", "pushpull");
        g->column({T("move"), T("rotate"), T("scale")});
        g->column({T("offset"), T("followme"), T("eraser")});
        g = t->group("Paint and Measure");
        g->large(T("paint"), "Paint Bucket", "paint");
        g->column({T("tape"), T("dimension"), T("text")});
        g = t->group("Groups");
        g->large(A("makegroup"), "Make Group", "makegroup");
        g->column({A("makecomponent"), A("explode"), A("closegroup")});
        g = t->group("Navigate");
        g->column({T("orbit"), T("pan"), T("zoom")});
        g->column({A("zoomextents"), T("zoomwindow"), A("prevview")});
    }
    // ---------------------------------------------------------------- Draw
    {
        RibbonTab* t = R->addTab("Draw", "D");
        RibbonGroup* g = t->group("Lines");
        g->large(T("line"), "Line", "line");
        g->large(T("freehand"), "Freehand", "freehand");
        g = t->group("Shapes");
        g->large(T("rect"), "Rectangle", "rect");
        g->large(T("rotrect"), "Rotated Rectangle", "rotrect");
        g->large(T("circle"), "Circle", "circle");
        g->large(T("polygon"), "Polygon", "polygon");
        g = t->group("Arcs");
        g->large(T("arc"), "Arc", "arc");
        g->large(T("arc2"), "2 Point Arc", "arc2");
        g->large(T("arc3"), "3 Point Arc", "arc3");
        g->large(T("pie"), "Pie", "pie");
        g = t->group("Text");
        g->large(T("text"), "Text", "text");
        g->large(A("text3d"), "3D Text", "text3d");
        g = t->group("Terrain");
        g->large(A("sbcontours"), "From Contours", "sbcontours");
        g->large(T("sbscratch"), "From Scratch", "sbscratch");
    }
    // ---------------------------------------------------------------- Modify
    {
        RibbonTab* t = R->addTab("Modify", "M");
        RibbonGroup* g = t->group("Push and Sweep");
        g->large(T("pushpull"), "Push/Pull", "pushpull");
        g->large(T("followme"), "Follow Me", "followme");
        g->large(T("offset"), "Offset", "offset");
        g = t->group("Transform");
        g->large(T("move"), "Move", "move");
        g->large(T("rotate"), "Rotate", "rotate");
        g->large(T("scale"), "Scale", "scale");
        g->largeMenu("Flip Along", "flip", flipMenu);
        g = t->group("Erase and Paint");
        g->large(T("eraser"), "Eraser", "eraser");
        g->large(T("paint"), "Paint Bucket", "paint");
        g = t->group("Faces");
        g->columnWidgets({new RibbonButton(A("reversefaces"), RibbonButton::Small, QString(), "reversefaces"),
                          new RibbonButton(A("orientfaces"), RibbonButton::Small, QString(), "orientfaces"),
                          g->smallMenu(intersectMenu, "Intersect Faces", "intersect")});
        g = t->group("Edges and Curves");
        g->column({A("divide"), A("weld"), A("explodecurve")});
        g->column({pSoften});
        g = t->group("Visibility");
        g->column({A("hide"), A("unhidesel"), A("unhideall")});
        g->column({A("lock"), A("unlock")});
        g = t->group("Groups and Components");
        g->large(A("makegroup"), "Make Group", "makegroup");
        g->large(A("makecomponent"), "Make Component", "makecomponent");
        g->column({A("explode"), A("makeunique"), A("closegroup")});
        g = t->group("Dynamic Components");
        g->large(T("interact"), "Interact", "interact");
        g->column({A("componentoptions"), A("componentattributes")});
    }
    // ---------------------------------------------------------------- Construct
    {
        RibbonTab* t = R->addTab("Construct", "C");
        RibbonGroup* g = t->group("Measure");
        g->large(T("tape"), "Tape Measure", "tape");
        g->large(T("protractor"), "Protractor", "protractor");
        g->large(T("dimension"), "Dimensions", "dimension");
        g = t->group("Annotate");
        g->large(T("text"), "Text", "text");
        g->large(A("text3d"), "3D Text", "text3d");
        g = t->group("Guides");
        g->column({A("guides"), A("deleteguides")});
        g = t->group("Axes");
        g->large(T("axes"), "Axes", "axes");
        g->column({A("axes")});
        g = t->group("Sections");
        g->large(T("section"), "Section Plane", "section");
        g->column({A("sectionplanes"), A("sectioncuts"), A("sectionfill")});
    }
    // ---------------------------------------------------------------- Solids
    {
        RibbonTab* t = R->addTab("Solids", "S");
        RibbonGroup* g = t->group("Solid Tools");
        g->large(A("solid_outer"), "Outer Shell", "solid_outer");
        g->large(A("solid_intersect"), "Intersect", "solid_intersect");
        g->large(A("solid_union"), "Union", "solid_union");
        g->large(A("solid_subtract"), "Subtract", "solid_subtract");
        g->large(A("solid_trim"), "Trim", "solid_trim");
        g->large(A("solid_split"), "Split", "solid_split");
        g = t->group("Check");
        g->large(A("solidinspector"), "Solid Inspector", "solidinspector");
        g = t->group("Intersect Faces");
        g->column({A("intersectmodel"), A("intersectsel"), A("intersectctx")});
    }
    // ---------------------------------------------------------------- Terrain
    {
        RibbonTab* t = R->addTab("Terrain", "T");
        RibbonGroup* g = t->group("Create");
        g->large(A("sbcontours"), "From Contours", "sbcontours");
        g->large(T("sbscratch"), "From Scratch", "sbscratch");
        g = t->group("Shape");
        g->large(T("smoove"), "Smoove", "smoove");
        g->large(T("drape"), "Drape", "drape");
        g->large(A("sbdetail"), "Add Detail", "sbdetail");
        g->large(T("flipedge"), "Flip Edge", "flipedge");
    }
    // ---------------------------------------------------------------- Camera
    {
        RibbonTab* t = R->addTab("Camera", "A");
        RibbonGroup* g = t->group("Navigate");
        g->large(T("orbit"), "Orbit", "orbit");
        g->large(T("pan"), "Pan", "pan");
        g->large(T("zoom"), "Zoom", "zoom");
        g->column({T("zoomwindow"), A("zoomextents"), A("zoomsel")});
        g->column({A("prevview"), A("alignview")});
        g = t->group("Standard Views");
        g->large(A("view_iso"), "Iso", "view_iso");
        g->column({A("view_top"), A("view_front"), A("view_right")});
        g->column({A("view_back"), A("view_left"), A("view_bottom")});
        g = t->group("Projection");
        g->large(A("perspective"), "Perspective", "perspective");
        g->large(A("parallel"), "Parallel Projection", "parallel");
        g->large(A("twopoint"), "Two-Point", "twopoint");
        g->column({A("fov")});
        g = t->group("Match Photo");
        g->large(A("matchphoto"), "Match Photo", "matchphoto");
        g->column({A("matchphoto_edit"), A("matchphoto_done"), A("matchphoto_turn")});
        g = t->group("Walkthrough");
        g->large(T("poscam"), "Position Camera", "poscam");
        g->large(T("walk"), "Walk", "walk");
        g->large(T("lookaround"), "Look Around", "lookaround");
        g = t->group("Scenes");
        g->large(A("addscene"), "Add Scene", "addscene");
        g->column({A("prevscene"), A("nextscene"), A("playscenes")});
        g->column({A("scenetabs"), pScenes});
    }
    // ---------------------------------------------------------------- View
    {
        RibbonTab* t = R->addTab("View", "W");
        RibbonGroup* g = t->group("Face Style", pStyles);
        g->large(A("fs_wire"), "Wireframe", "fs_wire");
        g->large(A("fs_hidden"), "Hidden Line", "fs_hidden");
        g->large(A("fs_shaded"), "Shaded", "fs_shaded");
        g->large(A("fs_textured"), "Shaded With Textures", "fs_textured");
        g->large(A("fs_mono"), "Monochrome", "fs_mono");
        g->column({A("xray"), A("backedges")});
        g = t->group("Edge Style", pStyles);
        g->column({A("edges"), A("profiles"), A("extension")});
        g = t->group("Show");
        g->column({A("hiddengeom"), A("axes"), A("guides")});
        g->column({A("sectionplanes"), A("sectioncuts"), A("sectionfill")});
        g->column({A("shadows"), A("fog")});
        g = t->group("Panels");
        g->large(trayToggle, "Default Tray", "outliner");
        g->column({pInfo, pMat, pComp});
        g->column({pStyles, pTags, pScenes});
        g->column({pShadows, pFog, pOutliner});
        g = t->group("Model");
        g->large(A("modelinfo"), "Model Info", "modelinfo");
        g->large(A("preferences"), "Preferences", "preferences");
    }
    // ---------------------------------------------------------------- Help
    {
        RibbonTab* t = R->addTab("Help", "Y");
        RibbonGroup* g = t->group("Help");
        g->large(A("shortcuts"), "Keyboard Shortcuts", "shortcuts");
        g->large(A("about"), "About Folio Gable", "about");
        g = t->group("Extensions");
        g->column({A("extensions"), A("rubyconsole")});
    }
    // ---------------------------------------------------------------- contextual: editing a group
    {
        groupTab_ = R->addTab("Group Editing", "G", true);
        RibbonGroup* g = groupTab_->group("Context");
        g->large(A("closegroup"), "Close Group", "closegroup");
        g->large(A("hiderest"), "Hide Rest of Model", "hiderest");
        g = groupTab_->group("Component");
        g->large(A("makeunique"), "Make Unique", "makeunique");
        g->large(A("explode"), "Explode", "explode");
    }
    // the first tab
    R->select(R->tabs().front());
    // running any ribbon command folds a floating (tabs-only) ribbon away
    for (auto* tab : R->tabs())
        for (auto* g : tab->groups)
            for (auto* b : g->buttons)
                connect(b, &QToolButton::clicked, R, &Ribbon::commandRan);
}

}  // namespace gable
