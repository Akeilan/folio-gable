#include "ui/Ribbon.h"
#include "ui/Theme.h"
#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QRegularExpression>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <set>

namespace gable {

// ------------------------------------------------------------------ helpers

QString plainText(const QString& t) {
    QString s = t;
    s.remove(QRegularExpression("&(?!&)"));
    s.replace("&&", "&");
    s.remove(QChar(0x2026));
    if (s.endsWith("...")) s.chop(3);
    return s.trimmed();
}

void setScreentip(QAction* a, const QString& description) {
    a->setProperty("screentip", description);
    a->setToolTip(screentipHtml(a));
}

QString screentipHtml(QAction* a) {
    QString title = plainText(a->text());
    QKeySequence ks = a->shortcut();
    if (!ks.isEmpty()) title += " (" + ks.toString(QKeySequence::NativeText) + ")";
    QString d = a->property("screentip").toString();
    QString html = "<b>" + title.toHtmlEscaped() + "</b>";
    if (!d.isEmpty()) html += "<br>" + d.toHtmlEscaped();
    return "<qt>" + html + "</qt>";
}

static void repolish(QWidget* w) {
    w->style()->unpolish(w);
    w->style()->polish(w);
    w->update();
}

static QString wrapLabel(const QString& label) {
    QString s = label;
    if (s.contains('\n')) return s;
    if (s.size() > 8 && s.contains(' ')) {
        int mid = s.size() / 2, best = -1;
        for (int i = 0; i < s.size(); ++i)
            if (s[i] == ' ' && (best < 0 || std::abs(i - mid) < std::abs(best - mid))) best = i;
        if (best > 0) { s[best] = '\n'; return s; }
    }
    return s + "\n ";   // keep every large button two lines tall so the icons line up
}

// ------------------------------------------------------------------ RibbonButton

RibbonButton::RibbonButton(QAction* a, Size size, const QString& label, const QString& iconName, QWidget* parent)
    : QToolButton(parent), act_(a), size_(size), label_(label), iconName_(iconName) {
    setProperty("ribbonBtn", true);
    setProperty("large", size == Large);
    setFocusPolicy(Qt::NoFocus);
    setAutoRaise(true);
    QIcon ic;
    if (!iconName_.isEmpty() && theme::hasIcon(iconName_)) ic = theme::icon(iconName_, size == Large);
    else if (a) ic = a->icon();
    setIcon(ic);
    if (label_.isEmpty() && a) label_ = plainText(a->text());
    switch (size) {
    case Large:
        setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
        setIconSize(QSize(32, 32));
        setText(wrapLabel(label_));
        setMinimumWidth(44);
        setFixedHeight(72);
        break;
    case Small:
        setToolButtonStyle(ic.isNull() ? Qt::ToolButtonTextOnly : Qt::ToolButtonTextBesideIcon);
        setIconSize(QSize(16, 16));
        setText(label_);
        setFixedHeight(23);
        break;
    case IconOnly:
        setToolButtonStyle(Qt::ToolButtonIconOnly);
        setIconSize(QSize(16, 16));
        setFixedSize(24, 23);
        break;
    }
    if (a) {
        connect(this, &QToolButton::clicked, a, [a] { a->trigger(); });
        connect(a, &QAction::changed, this, &RibbonButton::sync);
        connect(a, &QAction::toggled, this, [this](bool) { sync(); });
        sync();
    }
}

void RibbonButton::sync() {
    if (!act_) return;
    setCheckable(act_->isCheckable());
    if (act_->isCheckable()) {
        QSignalBlocker b(this);
        setChecked(act_->isChecked());
    }
    setEnabled(act_->isEnabled());
    setToolTip(screentipHtml(act_));
    setVisible(act_->isVisible());
}

void RibbonButton::mousePressEvent(QMouseEvent* e) {
    if (splitBelow && menu() && e->button() == Qt::LeftButton && e->position().y() > height() * 0.56) {
        setDown(true);
        showMenu();
        setDown(false);
        return;
    }
    QToolButton::mousePressEvent(e);
}

void RibbonButton::press() {
    if (menu() && (popupMode() == QToolButton::InstantPopup || !act_)) { showMenu(); return; }
    if (act_) act_->trigger();
    else click();
}

// ------------------------------------------------------------------ RibbonGroup

RibbonGroup::RibbonGroup(const QString& caption, Ribbon* ribbon, QAction* launcher)
    : caption_(caption), ribbon_(ribbon) {
    outer_ = new QVBoxLayout(this);
    outer_->setContentsMargins(0, 0, 0, 0);
    outer_->setSpacing(0);
    content_ = new QWidget;
    auto* v = new QVBoxLayout(content_);
    v->setContentsMargins(3, 2, 3, 0);
    v->setSpacing(0);
    auto* bodyW = new QWidget;
    body_ = new QHBoxLayout(bodyW);
    body_->setContentsMargins(0, 0, 0, 0);
    body_->setSpacing(1);
    body_->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    v->addWidget(bodyW, 1);
    auto* capRow = new QWidget;
    auto* ch = new QHBoxLayout(capRow);
    ch->setContentsMargins(0, 0, 0, 1);
    ch->setSpacing(0);
    auto* cap = new QLabel(caption);
    cap->setProperty("caption", true);
    cap->setAlignment(Qt::AlignCenter);
    ch->addSpacing(launcher ? 14 : 0);
    ch->addWidget(cap, 1);
    if (launcher) {
        launcherButton = new RibbonButton(launcher, RibbonButton::IconOnly, caption + " Settings", "launcher");
        launcherButton->setProperty("launcher", true);
        launcherButton->setIconSize(QSize(10, 10));
        launcherButton->setFixedSize(14, 14);
        if (launcher->property("screentip").toString().isEmpty())
            launcherButton->setToolTip("<qt><b>" + caption.toHtmlEscaped() + " Settings</b></qt>");
        ch->addWidget(launcherButton);
    }
    capRow->setFixedHeight(17);
    v->addWidget(capRow);
    outer_->addWidget(content_);
    compactAct_ = new QAction(caption, this);
    connect(compactAct_, &QAction::triggered, this, &RibbonGroup::openCompact);
}

RibbonButton* RibbonGroup::large(QAction* a, const QString& label, const QString& icon) {
    auto* b = new RibbonButton(a, RibbonButton::Large, label, icon);
    body_->addWidget(b, 0, Qt::AlignTop);
    buttons.push_back(b);
    if (firstIcon_.isEmpty()) firstIcon_ = icon;
    return b;
}

RibbonButton* RibbonGroup::largeMenu(const QString& label, const QString& icon, QMenu* menu, QAction* def) {
    auto* b = new RibbonButton(def, RibbonButton::Large, label, icon);
    b->setMenu(menu);
    // the arrow sits under the label, like the suite's large split buttons; with a default
    // action the upper part runs it and the label part opens the menu
    b->setPopupMode(def ? QToolButton::DelayedPopup : QToolButton::InstantPopup);
    b->setProperty("menuBelow", true);
    b->splitBelow = def != nullptr;
    QString t = wrapLabel(label);
    if (t.endsWith("\n ")) t = t.left(t.size() - 1) + QString(QChar(0x25BE));
    else t += " " + QString(QChar(0x25BE));
    b->setText(t);
    if (!def) b->setToolTip("<qt><b>" + plainText(label).toHtmlEscaped() + "</b></qt>");
    body_->addWidget(b, 0, Qt::AlignTop);
    buttons.push_back(b);
    if (firstIcon_.isEmpty()) firstIcon_ = icon;
    return b;
}

std::vector<RibbonButton*> RibbonGroup::column(const std::vector<QAction*>& acts, bool iconOnly) {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 1, 0, 0);
    v->setSpacing(1);
    std::vector<RibbonButton*> out;
    for (QAction* a : acts) {
        if (!a) { v->addSpacing(23); continue; }
        auto* b = new RibbonButton(a, iconOnly ? RibbonButton::IconOnly : RibbonButton::Small, QString(),
                                   a->property("iconName").toString());
        v->addWidget(b, 0, Qt::AlignLeft);
        buttons.push_back(b);
        out.push_back(b);
        if (firstIcon_.isEmpty()) firstIcon_ = a->property("iconName").toString();
    }
    v->addStretch(1);
    body_->addWidget(w, 0, Qt::AlignTop);
    return out;
}

RibbonButton* RibbonGroup::smallMenu(QMenu* menu, const QString& label, const QString& icon, QAction* def) {
    auto* b = new RibbonButton(def, RibbonButton::Small, label, icon);
    b->setMenu(menu);
    b->setPopupMode(def ? QToolButton::MenuButtonPopup : QToolButton::InstantPopup);
    buttons.push_back(b);
    return b;
}

void RibbonGroup::columnWidgets(const std::vector<QWidget*>& ws) {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 1, 0, 0);
    v->setSpacing(1);
    for (QWidget* x : ws) {
        if (!x) { v->addSpacing(23); continue; }
        v->addWidget(x, 0, Qt::AlignLeft);
        if (auto* rb = qobject_cast<RibbonButton*>(x)) {
            if (std::find(buttons.begin(), buttons.end(), rb) == buttons.end()) buttons.push_back(rb);
        }
    }
    v->addStretch(1);
    body_->addWidget(w, 0, Qt::AlignTop);
}

void RibbonGroup::addWidget(QWidget* w) { body_->addWidget(w, 0, Qt::AlignTop); }

void RibbonGroup::setCollapsed(bool on) {
    if (on == collapsed_) return;
    collapsed_ = on;
    if (!compact_) {
        compact_ = new RibbonButton(compactAct_, RibbonButton::Large, caption_, firstIcon_.isEmpty() ? "chevron" : firstIcon_);
        compact_->setText(wrapLabel(caption_ + " ▾"));
        compact_->setToolTip("<qt><b>" + caption_.toHtmlEscaped() + "</b></qt>");
        outer_->addWidget(compact_, 0, Qt::AlignTop);
    }
    content_->setVisible(!on);
    compact_->setVisible(on);
}

void RibbonGroup::openCompact() {
    auto* pop = new QFrame(this, Qt::Popup);
    pop->setObjectName("ribbonPopup");
    pop->setAttribute(Qt::WA_DeleteOnClose);
    auto* l = new QVBoxLayout(pop);
    l->setContentsMargins(2, 2, 2, 2);
    outer_->removeWidget(content_);
    l->addWidget(content_);
    content_->show();
    pop->adjustSize();
    QPoint at = compact_->mapToGlobal(QPoint(0, compact_->height()));
    pop->move(at);
    // put the commands back when the popup closes
    connect(pop, &QObject::destroyed, this, [this] {
        content_->setParent(this);
        outer_->insertWidget(0, content_);
        content_->setVisible(!collapsed_);
    });
    // running any command closes the popup
    for (auto* b : buttons)
        connect(b, &QToolButton::clicked, pop, &QWidget::close, Qt::UniqueConnection);
    pop->show();
}

// ------------------------------------------------------------------ RibbonTab

RibbonGroup* RibbonTab::group(const QString& caption, QAction* launcher) {
    auto* g = new RibbonGroup(caption, ribbon, launcher);
    if (!groups.empty()) {
        auto* sep = new QFrame;
        sep->setProperty("ribbonSep", true);
        sep->setFixedWidth(1);
        lay->insertWidget(lay->count() - 1, sep);
    }
    lay->insertWidget(lay->count() - 1, g);
    groups.push_back(g);
    return g;
}

// ------------------------------------------------------------------ Ribbon

Ribbon::Ribbon(QWidget* parent) : QWidget(parent) {
    auto* v = new QVBoxLayout(this);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    tabRow_ = new QWidget;
    tabRow_->setObjectName("folioTabs");
    tabRow_->setAttribute(Qt::WA_StyledBackground);
    tabLay_ = new QHBoxLayout(tabRow_);
    tabLay_->setContentsMargins(6, 0, 6, 0);
    tabLay_->setSpacing(0);
    file_ = new QToolButton;
    file_->setText("File");
    file_->setObjectName("fileTab");
    file_->setFocusPolicy(Qt::NoFocus);
    file_->setToolTip("<qt><b>File</b><br>Open, save, print and export models, and set options.</qt>");
    connect(file_, &QToolButton::clicked, this, &Ribbon::fileClicked);
    tabLay_->addWidget(file_);
    tabLay_->addStretch(1);
    options_ = new QToolButton;
    options_->setObjectName("ribbonOptions");
    options_->setIcon(theme::icon("chevron", false));
    options_->setIconSize(QSize(12, 12));
    options_->setFocusPolicy(Qt::NoFocus);
    options_->setToolTip("<qt><b>Ribbon Display Options</b><br>Show the whole ribbon or only its tabs.</qt>");
    connect(options_, &QToolButton::clicked, this, &Ribbon::showOptionsMenu);
    tabLay_->addWidget(options_);
    v->addWidget(tabRow_);

    body_ = new QWidget;
    body_->setObjectName("folioRibbonBody");
    body_->setAttribute(Qt::WA_StyledBackground);
    auto* bl = new QVBoxLayout(body_);
    bl->setContentsMargins(0, 0, 0, 0);
    stack_ = new QStackedWidget;
    bl->addWidget(stack_);
    body_->setFixedHeight(96);
    v->addWidget(body_);
    qApp->installEventFilter(this);
}

Ribbon::~Ribbon() {
    qApp->removeEventFilter(this);
    for (auto* t : tabs_) delete t;
}

RibbonTab* Ribbon::addTab(const QString& name, const QString& keytip, bool contextual) {
    auto* t = new RibbonTab;
    t->name = name;
    t->keytip = keytip;
    t->contextual = contextual;
    t->ribbon = this;
    t->button = new QToolButton;
    t->button->setText(name);
    t->button->setFocusPolicy(Qt::NoFocus);
    t->button->setProperty("active", false);
    t->button->setProperty("contextual", contextual);
    t->button->installEventFilter(this);
    connect(t->button, &QToolButton::clicked, this, [this, t] { select(t); });
    tabLay_->insertWidget(tabLay_->count() - 2, t->button);
    t->page = new QWidget;
    t->page->setProperty("ribbonPage", true);
    t->page->setAttribute(Qt::WA_StyledBackground);
    t->lay = new QHBoxLayout(t->page);
    t->lay->setContentsMargins(4, 2, 4, 0);
    t->lay->setSpacing(0);
    t->lay->addStretch(1);
    stack_->addWidget(t->page);
    tabs_.push_back(t);
    if (contextual) setTabVisible(t, false);
    if (!current_ && !contextual) select(t);
    return t;
}

void Ribbon::setTabVisible(RibbonTab* t, bool on) {
    t->visible = on;
    t->button->setVisible(on);
    if (!on && current_ == t) {
        for (auto* x : tabs_)
            if (x->visible) { select(x); break; }
    }
}

void Ribbon::select(RibbonTab* t) {
    if (!t) return;
    if (current_ && current_->button) {
        current_->button->setProperty("active", false);
        repolish(current_->button);
    }
    current_ = t;
    t->button->setProperty("active", true);
    repolish(t->button);
    stack_->setCurrentWidget(t->page);
    if (mode_ == TabsOnly) {
        placeFloatingBody();
        body_->show();
        body_->raise();
    }
    fit();
}

bool Ribbon::bodyShown() const { return body_->isVisible(); }

void Ribbon::placeFloatingBody() {
    QWidget* w = window();
    if (body_->parentWidget() != w) body_->setParent(w);
    QPoint p = tabRow_->mapTo(w, QPoint(0, tabRow_->height()));
    body_->setGeometry(p.x(), p.y(), tabRow_->width(), 96);
}

void Ribbon::hideFloatingBody() {
    if (mode_ == TabsOnly) body_->hide();
}

void Ribbon::commandRan() { hideFloatingBody(); }

void Ribbon::setMode(Mode m) {
    if (m == mode_) return;
    Mode old = mode_;
    mode_ = m;
    auto* v = static_cast<QVBoxLayout*>(layout());
    if (old == TabsOnly && m != TabsOnly) {
        body_->setParent(this);
        v->addWidget(body_);
        body_->show();
    } else if (m == TabsOnly) {
        v->removeWidget(body_);
        placeFloatingBody();
        body_->hide();
    }
    setVisible(m != FullScreen);
    emit modeChanged(m);
    fit();
}

void Ribbon::toggleCollapsed() { setMode(mode_ == TabsOnly ? AlwaysShow : TabsOnly); }

void Ribbon::showOptionsMenu() {
    QMenu m;
    struct M { Mode mode; const char* title; const char* desc; };
    const M modes[] = {{FullScreen, "Full-screen mode", "Hide the ribbon and title bar for the largest drawing area. Press Esc to return."},
                       {TabsOnly, "Show tabs only", "Show only the ribbon tabs. Select a tab to show its commands."},
                       {AlwaysShow, "Always show Ribbon", "Keep the ribbon always showing."}};
    for (auto& x : modes) {
        QAction* a = m.addAction(QString(x.title));
        a->setCheckable(true);
        a->setChecked(mode_ == x.mode);
        a->setToolTip(x.desc);
        Mode md = x.mode;
        connect(a, &QAction::triggered, this, [this, md] { setMode(md); });
    }
    m.setToolTipsVisible(true);
    m.addSeparator();
    QAction* q = m.addAction(qatShown ? "Hide Quick Access Toolbar" : "Show Quick Access Toolbar");
    connect(q, &QAction::triggered, this, [this] {
        qatShown = !qatShown;
        if (onQatToggled) onQatToggled(qatShown);
    });
    m.exec(options_->mapToGlobal(QPoint(options_->width() - m.sizeHint().width(), options_->height())));
}

void Ribbon::fit() {
    if (fitQueued_) return;
    fitQueued_ = true;
    QTimer::singleShot(0, this, [this] {
        fitQueued_ = false;
        RibbonTab* t = current_;
        if (!t) return;
        int avail = (mode_ == TabsOnly ? tabRow_->width() : width()) - 8;
        if (avail < 80) return;
        for (auto* g : t->groups) g->setCollapsed(false);
        t->page->layout()->activate();
        int need = t->page->sizeHint().width();
        for (int i = (int)t->groups.size() - 1; i >= 0 && need > avail; --i) {
            t->groups[i]->setCollapsed(true);
            t->page->layout()->activate();
            need = t->page->sizeHint().width();
        }
    });
}

void Ribbon::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (mode_ == TabsOnly && body_->isVisible()) placeFloatingBody();
    fit();
}

bool Ribbon::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::MouseButtonDblClick) {
        for (auto* t : tabs_)
            if (o == t->button) { toggleCollapsed(); if (mode_ == TabsOnly) body_->hide(); return true; }
    }
    if (mode_ == TabsOnly && body_->isVisible() && e->type() == QEvent::MouseButtonPress) {
        auto* w = qobject_cast<QWidget*>(o);
        if (w && w->window() == window() && !body_->isAncestorOf(w) && w != body_ && !tabRow_->isAncestorOf(w) && w != tabRow_)
            body_->hide();
    }
    return QWidget::eventFilter(o, e);
}

// ------------------------------------------------------------------ KeyTips

KeyTips::KeyTips(QWidget* window, Ribbon* ribbon) : QObject(window), win_(window), ribbon_(ribbon) {
    qApp->installEventFilter(this);
}

static std::vector<QString> assignCodes(const std::vector<std::pair<QString, QString>>& items) {
    // items: (label, fixed code or empty)
    std::vector<QString> out(items.size());
    std::vector<QString> used;
    auto conflicts = [&](const QString& c) {
        for (auto& u : used)
            if (u.startsWith(c) || c.startsWith(u)) return true;
        return false;
    };
    for (size_t i = 0; i < items.size(); ++i)
        if (!items[i].second.isEmpty()) { out[i] = items[i].second.toUpper(); used.push_back(out[i]); }
    // single letters from word initials, then any letter of the label
    std::vector<size_t> pending;
    for (size_t i = 0; i < items.size(); ++i) {
        if (!out[i].isEmpty()) continue;
        QString lab = items[i].first.toUpper();
        QString cands;
        for (const QString& w : lab.split(QRegularExpression("[^A-Z0-9]+"), Qt::SkipEmptyParts)) cands += w[0];
        for (QChar c : lab) if (c.isLetterOrNumber()) cands += c;
        for (QChar c : cands) {
            QString code(c);
            if (!conflicts(code)) { out[i] = code; used.push_back(code); break; }
        }
        if (out[i].isEmpty()) pending.push_back(i);
    }
    // two-letter codes, led by a letter no single code took
    for (size_t i : pending) {
        QString lab = items[i].first.toUpper();
        QString letters;
        for (QChar c : lab) if (c.isLetter()) letters += c;
        letters += "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        bool done = false;
        for (int a = 0; a < letters.size() && !done; ++a)
            for (int b = 0; b < letters.size() && !done; ++b) {
                QString code = QString(letters[a]) + letters[b];
                if (!conflicts(code)) { out[i] = code; used.push_back(code); done = true; }
            }
        if (!done) out[i] = "?";
    }
    return out;
}

void KeyTips::start(RibbonTab* direct) {
    if (allowed && !allowed()) return;
    if (ribbon_->mode() == Ribbon::FullScreen) return;
    if (direct) showCommands(direct);
    else showTabs();
}

void KeyTips::stop() {
    clearBadges();
    targets_.clear();
    typed_.clear();
    level_ = 0;
}

void KeyTips::clearBadges() {
    for (auto& b : badges_)
        if (b) b->deleteLater();
    badges_.clear();
}

void KeyTips::badge(QWidget* w, const QString& code, bool below) {
    if (!w || !w->isVisible()) { badges_.push_back(nullptr); return; }
    auto* l = new QLabel(code, win_);
    l->setProperty("keytip", true);
    l->setAttribute(Qt::WA_TransparentForMouseEvents);
    l->adjustSize();
    QPoint c;
    if (below) c = w->mapTo(win_, QPoint(w->width() / 2 - l->width() / 2, w->height() - l->height() / 2 - 2));
    else c = w->mapTo(win_, QPoint(6, w->height() - l->height() + 2));
    l->move(c);
    l->show();
    l->raise();
    badges_.push_back(l);
}

void KeyTips::showTabs() {
    clearBadges();
    targets_.clear();
    typed_.clear();
    level_ = 1;
    targets_.push_back({"F", [this] { stop(); if (onFile) onFile(); }, ribbon_->fileButton()});
    for (auto* t : ribbon_->tabs()) {
        if (!t->visible) continue;
        targets_.push_back({t->keytip.toUpper(), [this, t] { showCommands(t); }, t->button});
    }
    for (auto& [code, b] : qat) {
        QAbstractButton* bb = b;
        if (bb && bb->isVisible()) targets_.push_back({code, [this, bb] { stop(); bb->click(); }, bb});
    }
    for (auto& t : targets_) badge(t.w, t.code, true);
}

void KeyTips::showCommands(RibbonTab* t) {
    clearBadges();
    targets_.clear();
    typed_.clear();
    level_ = 2;
    ribbon_->select(t);
    QCoreApplication::processEvents();   // let the page lay out (and fold) before placing badges
    std::vector<RibbonButton*> bs;
    for (auto* g : t->groups) {
        if (g->collapsed()) { if (g->compactButton()) bs.push_back(g->compactButton()); continue; }
        for (auto* b : g->buttons)
            if (b->isVisible()) bs.push_back(b);
        if (g->launcherButton) bs.push_back(g->launcherButton);
    }
    std::vector<std::pair<QString, QString>> items;
    for (auto* b : bs) items.push_back({b->label(), b->fixedKeytip ? b->keytip : QString()});
    auto codes = assignCodes(items);
    for (size_t i = 0; i < bs.size(); ++i) {
        RibbonButton* b = bs[i];
        b->keytip = codes[i];
        targets_.push_back({codes[i], [this, b] {
                                stop();
                                QTimer::singleShot(0, b, [b] { if (b->isEnabled()) b->press(); });
                            }, b});
        badge(b, codes[i], b->size() == RibbonButton::Large);
    }
}

void KeyTips::typedKey(const QString& ch) {
    QString t = typed_ + ch.toUpper();
    std::vector<Target*> m;
    for (auto& x : targets_)
        if (x.code.startsWith(t)) m.push_back(&x);
    if (m.empty()) { QApplication::beep(); return; }
    typed_ = t;
    for (auto* x : m)
        if (x->code == typed_) { auto run = x->run; run(); return; }
    // narrow the badges to the matching ones
    for (size_t i = 0; i < badges_.size() && i < targets_.size(); ++i)
        if (badges_[i]) badges_[i]->setVisible(targets_[i].code.startsWith(typed_));
}

bool KeyTips::eventFilter(QObject* o, QEvent* e) {
    auto* w = qobject_cast<QWidget*>(o);
    if (!w || w->window() != win_) {
        if (e->type() == QEvent::ApplicationDeactivate && level_) stop();
        return false;
    }
    switch (e->type()) {
    case QEvent::ShortcutOverride:
        // while KeyTips show, letters go to the KeyTips instead of triggering shortcuts
        if (level_ > 0) { e->accept(); return true; }
        return false;
    case QEvent::KeyPress: {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->isAutoRepeat()) return level_ > 0;
        if (k->key() == Qt::Key_Alt) { altDown_ = true; altClean_ = true; return false; }
        if (altDown_) altClean_ = false;
        if (k->key() == Qt::Key_F10 && !(k->modifiers() & ~Qt::KeypadModifier)) {
            if (level_) stop(); else start();
            return true;
        }
        if (level_ == 0) {
            // Alt+<tab letter> goes straight to that tab's KeyTips
            if ((k->modifiers() & Qt::AltModifier) && !(k->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier))) {
                QString tx = QKeySequence(k->key()).toString();
                if (tx.size() == 1) {
                    if (tx.toUpper() == "F" && onFile) { if (!allowed || allowed()) { onFile(); return true; } }
                    for (auto* t : ribbon_->tabs())
                        if (t->visible && t->keytip.toUpper() == tx.toUpper()) { start(t); return true; }
                }
            }
            return false;
        }
        if (k->key() == Qt::Key_Escape) {
            if (level_ == 2 && typed_.isEmpty()) showTabs();
            else if (!typed_.isEmpty()) { typed_.clear(); for (auto& b : badges_) if (b) b->show(); }
            else stop();
            return true;
        }
        QString tx = k->text();
        if (!tx.isEmpty() && tx[0].isLetterOrNumber()) { typedKey(tx); return true; }
        if (k->key() == Qt::Key_Shift || k->key() == Qt::Key_Control) return false;
        stop();
        return true;
    }
    case QEvent::KeyRelease: {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Alt && !k->isAutoRepeat()) {
            bool clean = altDown_ && altClean_;
            altDown_ = false;
            if (clean) {
                if (level_) stop();
                else start();
                return true;
            }
        }
        return false;
    }
    case QEvent::MouseButtonPress:
    case QEvent::Wheel:
        altClean_ = false;
        if (level_) stop();
        return false;
    case QEvent::WindowDeactivate:
        if (level_) stop();
        altDown_ = false;
        return false;
    default:
        return false;
    }
}

}  // namespace gable
