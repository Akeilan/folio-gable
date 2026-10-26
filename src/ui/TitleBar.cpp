#include "ui/TitleBar.h"
#include "ui/Ribbon.h"
#include "ui/Theme.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCompleter>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStringListModel>
#include <QTimer>
#include <QWindow>

namespace gable {

static QIcon glyph(int kind) {
    // 0 minimise, 1 maximise, 2 restore, 3 close — thin white strokes like the other Folio apps
    QIcon ic;
    for (qreal dpr : {1.0, 2.0}) {
        QPixmap pm(QSize(10, 10) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing, kind == 3);
        p.setPen(QPen(Qt::white, 1.0));
        switch (kind) {
        case 0: p.drawLine(QPointF(0, 5.5), QPointF(10, 5.5)); break;
        case 1: p.drawRect(QRectF(0.5, 0.5, 9, 9)); break;
        case 2:
            p.drawRect(QRectF(0.5, 2.5, 7, 7));
            p.drawLine(QPointF(2.5, 2.5), QPointF(2.5, 0.5));
            p.drawLine(QPointF(2.5, 0.5), QPointF(9.5, 0.5));
            p.drawLine(QPointF(9.5, 0.5), QPointF(9.5, 7.5));
            p.drawLine(QPointF(9.5, 7.5), QPointF(7.5, 7.5));
            break;
        case 3: p.drawLine(QPointF(0, 0), QPointF(10, 10)); p.drawLine(QPointF(10, 0), QPointF(0, 10)); break;
        }
        p.end();
        ic.addPixmap(pm);
    }
    return ic;
}

TitleBar::TitleBar(QWidget* window) : QWidget(window), win_(window) {
    setObjectName("folioTitleBar");
    setAttribute(Qt::WA_StyledBackground);
    setFixedHeight(40);   // suite spec: title bar 40 px
    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(4, 0, 0, 0);
    h->setSpacing(0);
    qatBox_ = new QWidget;
    qatLay_ = new QHBoxLayout(qatBox_);
    qatLay_->setContentsMargins(0, 0, 0, 0);
    qatLay_->setSpacing(0);
    h->addWidget(qatBox_);
    qatMenu_ = new QToolButton;
    qatMenu_->setIcon(theme::whiteIcon("chevron", 10));
    qatMenu_->setIconSize(QSize(10, 10));
    qatMenu_->setToolTip("<qt><b>Customize Quick Access Toolbar</b></qt>");
    qatMenu_->setFocusPolicy(Qt::NoFocus);
    qatMenu_->setPopupMode(QToolButton::InstantPopup);
    qatMenu_->setFixedWidth(20);
    h->addWidget(qatMenu_);
    h->addStretch(1);
    search_ = new QLineEdit;
    search_->setObjectName("folioSearch");
    search_->setPlaceholderText("Search (Alt+Q)");
    search_->setFixedWidth(240);
    search_->setClearButtonEnabled(true);
    search_->addAction(theme::whiteIcon("search", 14), QLineEdit::LeadingPosition);
    h->addWidget(search_);
    h->addSpacing(24);
    min_ = new QToolButton;
    max_ = new QToolButton;
    close_ = new QToolButton;
    close_->setObjectName("closeButton");
    min_->setIcon(glyph(0));
    max_->setIcon(glyph(1));
    close_->setIcon(glyph(3));
    for (auto* b : {min_, max_, close_}) {
        b->setIconSize(QSize(10, 10));
        b->setFixedSize(46, 40);
        b->setFocusPolicy(Qt::NoFocus);
        h->addWidget(b);
    }
    min_->setToolTip("Minimize");
    max_->setToolTip("Maximize");
    close_->setToolTip("Close");
    connect(min_, &QToolButton::clicked, win_, &QWidget::showMinimized);
    connect(max_, &QToolButton::clicked, this, [this] {
        if (win_->isMaximized()) win_->showNormal(); else win_->showMaximized();
    });
    connect(close_, &QToolButton::clicked, win_, &QWidget::close);
    title_ = new QLabel(this);
    title_->setObjectName("folioTitle");
    title_->setAlignment(Qt::AlignCenter);
    title_->setAttribute(Qt::WA_TransparentForMouseEvents);

    model_ = new QStringListModel(this);
    completer_ = new QCompleter(model_, this);
    completer_->setCaseSensitivity(Qt::CaseInsensitive);
    completer_->setFilterMode(Qt::MatchContains);
    completer_->setCompletionMode(QCompleter::PopupCompletion);
    completer_->setMaxVisibleItems(12);
    search_->setCompleter(completer_);
    connect(completer_, QOverload<const QString&>::of(&QCompleter::activated), this, [this](const QString& t) {
        QMetaObject::invokeMethod(this, [this, t] { runSearch(t); }, Qt::QueuedConnection);
    });
    connect(search_, &QLineEdit::returnPressed, this, [this] { runSearch(search_->text()); });
    search_->installEventFilter(this);
    win_->installEventFilter(this);
}

void TitleBar::setTitle(const QString& t) {
    title_->setText(t);
    resizeEvent(nullptr);
}

void TitleBar::resizeEvent(QResizeEvent*) {
    // the title is centred on the whole bar, but never under the QAT or the search box
    int left = qatBox_->geometry().right() + 28;
    int right = search_->geometry().left() - 8;
    int w = title_->sizeHint().width() + 16;
    int x = (width() - w) / 2;
    if (x < left) x = left;
    if (x + w > right) w = std::max(40, right - x);
    title_->setGeometry(x, 0, w, height());
}

void TitleBar::setQat(const std::vector<std::pair<QAction*, QString>>& actions) {
    for (auto* b : qatButtons_) b->deleteLater();
    qatButtons_.clear();
    for (auto& [a, icon] : actions) {
        auto* b = new QToolButton;
        b->setIcon(theme::whiteIcon(icon, 16));
        b->setIconSize(QSize(16, 16));
        b->setFocusPolicy(Qt::NoFocus);
        b->setToolTip(screentipHtml(a));
        b->setEnabled(a->isEnabled());
        connect(b, &QToolButton::clicked, a, [a] { a->trigger(); });
        connect(a, &QAction::changed, b, [a, b] { b->setEnabled(a->isEnabled()); b->setToolTip(screentipHtml(a)); });
        qatLay_->addWidget(b);
        qatButtons_.push_back(b);
    }
    resizeEvent(nullptr);
}

void TitleBar::setWindowButtonsVisible(bool on) {
    for (auto* b : {min_, max_, close_}) b->setVisible(on);
    title_->setVisible(on);   // with the system title bar the name is shown there already
}

void TitleBar::syncMaximized() {
    max_->setIcon(glyph(win_->isMaximized() ? 2 : 1));
    max_->setToolTip(win_->isMaximized() ? "Restore Down" : "Maximize");
}

void TitleBar::setSearchActions(const std::vector<QAction*>& acts) {
    searchActs_ = acts;
    QStringList names;
    for (auto* a : acts) {
        QString n = plainText(a->text());
        if (!n.isEmpty() && !names.contains(n)) names << n;
    }
    names.sort(Qt::CaseInsensitive);
    model_->setStringList(names);
}

void TitleBar::focusSearch() {
    search_->setFocus();
    search_->selectAll();
}

void TitleBar::runSearch(const QString& text) {
    QString t = text.trimmed();
    if (t.isEmpty()) return;
    QAction* best = nullptr;
    int bestScore = -1;
    for (auto* a : searchActs_) {
        if (!a->isEnabled()) continue;
        QString n = plainText(a->text());
        QString d = a->property("screentip").toString();
        int score = -1;
        if (n.compare(t, Qt::CaseInsensitive) == 0) score = 100;
        else if (n.startsWith(t, Qt::CaseInsensitive)) score = 80;
        else if (n.contains(t, Qt::CaseInsensitive)) score = 60;
        else if (d.contains(t, Qt::CaseInsensitive)) score = 30;
        if (score > bestScore) { bestScore = score; best = a; }
    }
    if (!best || bestScore < 0) { QApplication::beep(); return; }
    search_->clear();
    search_->clearFocus();
    best->trigger();
}

bool TitleBar::eventFilter(QObject* o, QEvent* e) {
    if (o == win_ && e->type() == QEvent::WindowStateChange) syncMaximized();
    if (o == search_ && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Escape) { search_->clear(); search_->clearFocus(); return true; }
    }
    return QWidget::eventFilter(o, e);
}

void TitleBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && win_->windowHandle() && (win_->windowFlags() & Qt::FramelessWindowHint)) {
        win_->windowHandle()->startSystemMove();
        return;
    }
    QWidget::mousePressEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && (win_->windowFlags() & Qt::FramelessWindowHint)) {
        if (win_->isMaximized()) win_->showNormal(); else win_->showMaximized();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

// ------------------------------------------------------------------ FramelessResizer

FramelessResizer::FramelessResizer(QWidget* window) : QObject(window), win_(window) {
    qApp->installEventFilter(this);
    // most widgets do not track the mouse, so the edge cursor is also refreshed on a timer
    auto* t = new QTimer(this);
    t->setInterval(80);
    QObject::connect(t, &QTimer::timeout, this, [this] { update(QCursor::pos()); });
    t->start();
}

void FramelessResizer::update(const QPoint& g) {
    Qt::Edges ed = (QApplication::mouseButtons() == Qt::NoButton && win_->isActiveWindow()) ? edgesAt(g) : Qt::Edges();
    if (ed == hover_) return;
    hover_ = ed;
    if (overriding_) { QApplication::restoreOverrideCursor(); overriding_ = false; }
    if (ed) {
        Qt::CursorShape c;
        if (ed == Qt::LeftEdge || ed == Qt::RightEdge) c = Qt::SizeHorCursor;
        else if (ed == Qt::TopEdge || ed == Qt::BottomEdge) c = Qt::SizeVerCursor;
        else if (ed == (Qt::LeftEdge | Qt::TopEdge) || ed == (Qt::RightEdge | Qt::BottomEdge)) c = Qt::SizeFDiagCursor;
        else c = Qt::SizeBDiagCursor;
        QApplication::setOverrideCursor(c);
        overriding_ = true;
    }
}

Qt::Edges FramelessResizer::edgesAt(const QPoint& g) const {
    Qt::Edges ed;
    if (!enabled || !(win_->windowFlags() & Qt::FramelessWindowHint) || win_->isMaximized() || win_->isFullScreen()) return ed;
    QRect r = win_->frameGeometry();
    const int m = 5;
    if (!r.adjusted(-1, -1, 1, 1).contains(g)) return ed;
    if (g.x() <= r.left() + m) ed |= Qt::LeftEdge;
    if (g.x() >= r.right() - m) ed |= Qt::RightEdge;
    if (g.y() <= r.top() + m - 2) ed |= Qt::TopEdge;
    if (g.y() >= r.bottom() - m) ed |= Qt::BottomEdge;
    return ed;
}

bool FramelessResizer::eventFilter(QObject* o, QEvent* e) {
    if (e->type() != QEvent::MouseMove && e->type() != QEvent::MouseButtonPress && e->type() != QEvent::HoverMove) return false;
    auto* w = qobject_cast<QWidget*>(o);
    if (!w || w->window() != win_) return false;
    if (QApplication::mouseButtons() & ~Qt::LeftButton) return false;
    QPoint g = QCursor::pos();
    Qt::Edges ed = edgesAt(g);
    if (e->type() == QEvent::MouseButtonPress) {
        auto* me = static_cast<QMouseEvent*>(e);
        if (ed && me->button() == Qt::LeftButton && win_->windowHandle()) {
            win_->windowHandle()->startSystemResize(ed);
            return true;
        }
        return false;
    }
    update(g);
    return false;
}

}  // namespace gable
