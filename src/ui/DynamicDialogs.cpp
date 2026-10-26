// Folio Gable — Component Options and Component Attributes.
#include "ui/DynamicDialogs.h"
#include "core/Dynamic.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace gable {

static const struct { const char* code; const char* text; } kUnits[] = {
    {"LENGTH", "Length"}, {"FLOAT", "Decimal number"}, {"INTEGER", "Whole number"},
    {"DEGREES", "Degrees"}, {"STRING", "Text"}, {"BOOLEAN", "TRUE or FALSE"},
};

QString formatAttrValue(const DynAttr& a, const UnitSettings& u) {
    QString un = a.units.toUpper();
    bool ok = false;
    double d = a.value.trimmed().toDouble(&ok);
    if (un == "BOOLEAN") {
        bool t = ok ? d != 0 : a.value.trimmed().compare("true", Qt::CaseInsensitive) == 0;
        return t ? "TRUE" : "FALSE";
    }
    if (!ok) return a.value;
    if (un == "LENGTH") return formatLength(d, u);
    if (un == "DEGREES") return dyn::Value(d).toText() + "°";
    if (un == "INTEGER") return QString::number((qint64)std::llround(d));
    return dyn::Value(d).toText();
}

bool parseAttrValue(const QString& textIn, const QString& units, const UnitSettings& u, QString& out) {
    QString text = textIn.trimmed();
    QString un = units.toUpper();
    if (un == "LENGTH") {
        double mm;
        if (!parseLength(text, u, mm)) return false;
        out = dyn::Value(mm).toText();
        return true;
    }
    if (un == "DEGREES" || un == "FLOAT" || un == "INTEGER") {
        if (text.endsWith("°")) text.chop(1);
        bool ok;
        double d = text.toDouble(&ok);
        if (!ok) return false;
        out = dyn::Value(un == "INTEGER" ? std::round(d) : d).toText();
        return true;
    }
    if (un == "BOOLEAN") {
        QString t = text.toLower();
        if (t == "true" || t == "yes" || t == "1") { out = "TRUE"; return true; }
        if (t == "false" || t == "no" || t == "0") { out = "FALSE"; return true; }
        return false;
    }
    out = textIn;
    return true;
}

static int selectedInstance(Document* d) {
    auto is = d->selectedInstances();
    if (is.size() != 1 || d->sel.size() != 1) return -1;
    return is[0];
}

static QString instName(const Model& m, const Instance& in) {
    return in.name.isEmpty() ? m.defs[in.def].name : in.name;
}

static void clearLayout(QLayout* l) {
    while (l->count() > 0) {
        QLayoutItem* it = l->takeAt(0);
        if (QWidget* w = it->widget()) {
            // out of the window now (a search for it no longer finds it), deleted once its signals are done
            w->hide();
            w->setParent(nullptr);
            w->deleteLater();
        }
        if (it->layout()) clearLayout(it->layout());
        delete it;
    }
}

// ======================================================================= Component Options

ComponentOptionsDialog::ComponentOptionsDialog(Document* d, QWidget* parent) : QDialog(parent), doc_(d) {
    setWindowTitle("Component Options");
    setObjectName("componentOptions");
    resize(340, 380);
    auto* lay = new QVBoxLayout(this);
    title_ = new QLabel;
    QFont f = title_->font();
    f.setPointSizeF(f.pointSizeF() * 1.25);
    f.setBold(true);
    title_->setFont(f);
    lay->addWidget(title_);
    summary_ = new QLabel;
    summary_->setWordWrap(true);
    lay->addWidget(summary_);
    empty_ = new QLabel;
    empty_->setWordWrap(true);
    empty_->setAlignment(Qt::AlignCenter);
    lay->addWidget(empty_);
    form_ = new QWidget;
    new QFormLayout(form_);
    lay->addWidget(form_);
    lay->addStretch(1);
    apply_ = new QPushButton("Apply");
    apply_->setDefault(true);
    auto* row = new QHBoxLayout;
    row->addStretch(1);
    row->addWidget(apply_);
    lay->addLayout(row);
    connect(apply_, &QPushButton::clicked, this, [this] { apply(); });
    auto later = [this] { if (isVisible() && !updating_) QTimer::singleShot(0, this, [this] { refresh(); }); };
    connect(d, &Document::selectionChanged, this, later);
    connect(d, &Document::contextChanged, this, later);
    connect(d, &Document::undoStateChanged, this, later);
    refresh();
}

void ComponentOptionsDialog::refresh() {
    if (updating_) return;
    auto* form = static_cast<QFormLayout*>(form_->layout());
    clearLayout(form);
    rows_.clear();
    inst_ = selectedInstance(doc_);
    const Model& m = doc_->model;
    const Instance* in = inst_ >= 0 ? &doc_->active().insts[inst_] : nullptr;
    if (!in || !dyn::isDynamic(m, *in)) {
        inst_ = in ? inst_ : -1;
        title_->setText(in ? instName(m, *in) : QString());
        summary_->clear();
        empty_->setText(in ? "This component has no options." : "Select a dynamic component to see its options.");
        empty_->show();
        apply_->setEnabled(false);
        return;
    }
    title_->setText(instName(m, *in));
    QStringList about;
    for (const char* k : {"Summary", "Description"})
        if (auto* a = dyn::find(in->attrs, k)) if (!a->value.isEmpty()) about << a->value;
    summary_->setText(about.join("\n"));
    summary_->setVisible(!about.isEmpty());
    bool editable = false;
    for (const DynAttr& a : in->attrs) {
        if (a.access < 1 || a.name.compare("onClick", Qt::CaseInsensitive) == 0) continue;
        if (a.name.compare("Summary", Qt::CaseInsensitive) == 0 || a.name.compare("Description", Qt::CaseInsensitive) == 0) continue;   // in the heading
        QString label = a.label.isEmpty() ? a.name : a.label;
        QWidget* w;
        if (a.access == 3 && !a.options.empty()) {
            auto* cb = new QComboBox;
            int cur = -1;
            for (size_t i = 0; i < a.options.size(); ++i) {
                cb->addItem(a.options[i].first, a.options[i].second);
                bool okA, okB;
                double x = a.options[i].second.toDouble(&okA), y = a.value.toDouble(&okB);
                if ((okA && okB && std::fabs(x - y) < 1e-6) || a.options[i].second.compare(a.value, Qt::CaseInsensitive) == 0) cur = (int)i;
            }
            if (cur < 0) { cb->insertItem(0, formatAttrValue(a, m.units), a.value); cur = 0; }
            cb->setCurrentIndex(cur);
            w = cb;
            editable = true;
        } else if (a.access >= 2) {
            auto* le = new QLineEdit(formatAttrValue(a, m.units));
            connect(le, &QLineEdit::returnPressed, this, [this] { apply(); });
            w = le;
            editable = true;
        } else {
            auto* l = new QLabel(formatAttrValue(a, m.units));
            l->setTextInteractionFlags(Qt::TextSelectableByMouse);
            w = l;
        }
        w->setObjectName("opt_" + a.name);
        form->addRow(label, w);
        w->show();
        if (QWidget* l = form->labelForField(w)) l->show();
        rows_.push_back({a.name, w, a.units});
    }
    empty_->setText(rows_.empty() ? "This component has no options." : QString());
    empty_->setVisible(rows_.empty());
    apply_->setEnabled(editable);
}

bool ComponentOptionsDialog::apply() {
    if (inst_ < 0 || inst_ >= (int)doc_->active().insts.size()) return false;
    const UnitSettings& u = doc_->model.units;
    std::vector<std::pair<QString, QString>> vals;
    for (auto& r : rows_) {
        if (auto* cb = qobject_cast<QComboBox*>(r.w)) {
            vals.push_back({r.attr, cb->currentData().toString()});
        } else if (auto* le = qobject_cast<QLineEdit*>(r.w)) {
            QString v;
            if (!parseAttrValue(le->text(), r.units, u, v)) {
                le->setStyleSheet("background:#fbebeb;");
                le->setToolTip("This isn't a valid value.");
                le->setFocus();
                le->selectAll();
                return false;
            }
            le->setStyleSheet(QString());
            vals.push_back({r.attr, v});
        }
    }
    updating_ = true;
    doc_->begin("Component Options");
    Instance& in = doc_->active().insts[inst_];
    for (auto& [name, v] : vals) {
        DynAttr* a = dyn::find(in.attrs, name);
        if (!a) continue;
        a->formula.clear();
        a->value = v;
    }
    dyn::redraw(doc_->model, doc_->active(), inst_);
    doc_->active().touch();
    doc_->commit();
    doc_->notifyChanged();
    updating_ = false;
    emit redrawn();
    refresh();
    return true;
}

// ======================================================================= Component Attributes

ComponentAttributesDialog::ComponentAttributesDialog(Document* d, QWidget* parent) : QDialog(parent), doc_(d) {
    setWindowTitle("Component Attributes");
    setObjectName("componentAttributes");
    resize(560, 520);
    auto* lay = new QVBoxLayout(this);
    empty_ = new QLabel;
    empty_->setWordWrap(true);
    empty_->setAlignment(Qt::AlignCenter);
    lay->addWidget(empty_);
    scroll_ = new QScrollArea;
    scroll_->setWidgetResizable(true);
    body_ = new QWidget;
    bodyLay_ = new QVBoxLayout(body_);
    scroll_->setWidget(body_);
    lay->addWidget(scroll_, 1);
    auto* help = new QLabel("Type a value, or a formula starting with = (for example =Parent!LenX - 36). "
                            "Formulas can use the attributes of this part, Parent!Name, PartName!Name and COPY.");
    help->setWordWrap(true);
    help->setStyleSheet("color: palette(mid);");
    lay->addWidget(help);
    auto later = [this] { if (isVisible() && !updating_) QTimer::singleShot(0, this, [this] { refresh(); }); };
    connect(d, &Document::selectionChanged, this, later);
    connect(d, &Document::contextChanged, this, later);
    connect(d, &Document::undoStateChanged, this, later);
    refresh();
}

Instance* ComponentAttributesDialog::part(int p) {
    Entities& A = doc_->active();
    if (inst_ < 0 || inst_ >= (int)A.insts.size() || !A.insts[inst_].alive) return nullptr;
    if (p < 0) return &A.insts[inst_];
    Entities& D = doc_->model.defs[A.insts[inst_].def].ents;
    if (p >= (int)D.insts.size() || !D.insts[p].alive) return nullptr;
    return &D.insts[p];
}

void ComponentAttributesDialog::refresh() {
    if (updating_) return;
    clearLayout(bodyLay_);
    inst_ = selectedInstance(doc_);
    if (inst_ < 0) {
        empty_->setText("Select one component to see and add its attributes.");
        empty_->show();
        scroll_->hide();
        return;
    }
    empty_->hide();
    scroll_->show();
    const Model& m = doc_->model;
    Instance* top = part(-1);
    addSection(-1, instName(m, *top));
    const Entities& D = m.defs[top->def].ents;
    for (size_t i = 0; i < D.insts.size(); ++i) {
        const Instance& c = D.insts[i];
        if (!c.alive || c.copyOf >= 0) continue;
        addSection((int)i, instName(m, c));
    }
    if (!errors_.isEmpty()) {
        auto* e = new QLabel(errors_.join("\n"));
        e->setObjectName("attrErrors");
        e->setWordWrap(true);
        e->setStyleSheet("color:#b3261e;");
        bodyLay_->addWidget(e);
    }
    bodyLay_->addStretch(1);
    for (int i = 0; i < bodyLay_->count(); ++i)
        if (QWidget* w = bodyLay_->itemAt(i)->widget()) w->show();
}

void ComponentAttributesDialog::addSection(int p, const QString& title) {
    Instance* in = part(p);
    if (!in) return;
    const UnitSettings& u = doc_->model.units;
    auto* box = new QGroupBox(p < 0 ? title : QString("%1   (part)").arg(title));
    box->setObjectName(p < 0 ? "attrTop" : QString("attrPart%1").arg(p));
    auto* g = new QGridLayout(box);
    g->setColumnStretch(1, 1);
    int r = 0;
    for (const DynAttr& a : in->attrs) {
        auto* name = new QLabel(a.name);
        name->setToolTip(a.label.isEmpty() ? QString() : QString("Shown to users as \"%1\"").arg(a.label));
        auto* ed = new QLineEdit(a.formula.isEmpty() ? formatAttrValue(a, u) : "=" + a.formula);
        ed->setObjectName(QString("attr_%1_%2").arg(p).arg(a.name));
        QString nm = a.name;
        connect(ed, &QLineEdit::editingFinished, this, [this, p, nm, ed] {
            if (!ed->isModified()) return;
            ed->setModified(false);
            QString err;
            if (!setAttribute(p, nm, ed->text(), &err)) {
                ed->setStyleSheet("background:#fbebeb;");
                ed->setToolTip(err);
            }
        });
        auto* res = new QLabel(a.formula.isEmpty() ? QString() : "= " + formatAttrValue(a, u));
        res->setStyleSheet("color: palette(mid);");
        auto* opt = new QToolButton;
        opt->setText("Options…");
        opt->setToolTip("Who can see or change this attribute, its label, units and list of choices.");
        connect(opt, &QToolButton::clicked, this, [this, p, nm] { optionsDialog(p, nm); });
        auto* del = new QToolButton;
        del->setText("✕");
        del->setToolTip("Delete this attribute");
        connect(del, &QToolButton::clicked, this, [this, p, nm] { removeAttribute(p, nm); });
        g->addWidget(name, r, 0);
        g->addWidget(ed, r, 1);
        g->addWidget(res, r, 2);
        g->addWidget(opt, r, 3);
        g->addWidget(del, r, 4);
        ++r;
    }
    auto* add = new QToolButton;
    add->setText("Add attribute");
    add->setPopupMode(QToolButton::InstantPopup);
    add->setObjectName(QString("attrAdd%1").arg(p));
    auto* menu = new QMenu(add);
    QString group;
    QMenu* sub = nullptr;
    for (auto& s : dyn::standardAttributes()) {
        if (group != s.group) { group = s.group; sub = menu->addMenu(group); }
        QString nm = s.name;
        QAction* a = sub->addAction(nm, this, [this, p, nm] {
            Instance* in = part(p);
            if (!in || dyn::find(in->attrs, nm)) return;
            dyn::Value v;
            Entities& E = p < 0 ? doc_->active() : doc_->model.defs[part(-1)->def].ents;
            int idx = p < 0 ? inst_ : p;
            QString val = dyn::currentValue(doc_->model, E, idx, nm, v) ? v.toText() : QString();
            if (nm == "onClick") val = "ANIMATE(\"RotZ\", 0, 90)";
            setAttribute(p, nm, val);
        });
        a->setToolTip(s.tip);
        if (dyn::find(in->attrs, nm)) a->setEnabled(false);
    }
    menu->addSeparator();
    menu->addAction("Custom attribute…", this, [this, p] {
        bool ok;
        QString nm = QInputDialog::getText(this, "Add Attribute", "Name (letters, digits and _):", QLineEdit::Normal, QString(), &ok).trimmed();
        if (!ok || nm.isEmpty()) return;
        static const QRegularExpression valid("^[A-Za-z_][A-Za-z0-9_]*$");
        if (!valid.match(nm).hasMatch() || nm.compare("copy", Qt::CaseInsensitive) == 0 || nm.compare("parent", Qt::CaseInsensitive) == 0) return;
        setAttribute(p, nm, "0");
    });
    add->setMenu(menu);
    g->addWidget(add, r, 0, 1, 2, Qt::AlignLeft);
    bodyLay_->addWidget(box);
}

void ComponentAttributesDialog::commitEdit(const QString& what) {
    (void)what;
    errors_.clear();
    Instance* top = part(-1);
    if (top) {
        dyn::redraw(doc_->model, doc_->active(), inst_, &errors_);
        top = part(-1);
        // new copies of the component start with its attributes
        doc_->model.defs[top->def].attrs = top->attrs;
    }
    doc_->active().touch();
    doc_->commit();
    doc_->notifyChanged();
    updating_ = false;
    emit redrawn();
    QTimer::singleShot(0, this, [this] { refresh(); });
}

bool ComponentAttributesDialog::setAttribute(int p, const QString& name, const QString& text, QString* err) {
    Instance* in = part(p);
    if (!in) return false;
    QString t = text.trimmed();
    QString units = dyn::find(in->attrs, name) ? dyn::find(in->attrs, name)->units : dyn::defaultUnits(name);
    QString value, formula;
    if (t.startsWith('=')) {
        formula = t.mid(1).trimmed();
        dyn::Value dummy;
        QString e;
        // check the formula reads, whatever its names turn out to be
        bool ok = dyn::evaluate(formula, [](const QString&, dyn::Value& o, QString&) { o = dyn::Value(1.0); return true; }, dummy, &e);
        if (!ok && !e.contains("zero")) { if (err) *err = e; return false; }
    } else if (!parseAttrValue(t, units, doc_->model.units, value)) {
        value = text;   // text in a number attribute: kept as typed (a formula may compare it)
    }
    updating_ = true;
    doc_->begin("Component Attributes");
    in = part(p);
    DynAttr* a = dyn::find(in->attrs, name);
    if (!a) {
        DynAttr na;
        na.name = name;
        na.units = units;
        in->attrs.push_back(na);
        a = &in->attrs.back();
    }
    a->formula = formula;
    if (formula.isEmpty()) a->value = value;
    commitEdit("set");
    return true;
}

bool ComponentAttributesDialog::removeAttribute(int p, const QString& name) {
    Instance* in = part(p);
    if (!in || !dyn::find(in->attrs, name)) return false;
    updating_ = true;
    doc_->begin("Delete Attribute");
    in = part(p);
    for (size_t i = 0; i < in->attrs.size(); ++i)
        if (in->attrs[i].name.compare(name, Qt::CaseInsensitive) == 0) { in->attrs.erase(in->attrs.begin() + i); break; }
    commitEdit("remove");
    return true;
}

bool ComponentAttributesDialog::setOptions(int p, const QString& name, int access, const QString& label, const QString& units,
                                           const std::vector<std::pair<QString, QString>>& options) {
    Instance* in = part(p);
    if (!in || !dyn::find(in->attrs, name)) return false;
    updating_ = true;
    doc_->begin("Attribute Options");
    DynAttr* a = dyn::find(part(p)->attrs, name);
    a->access = std::max(0, std::min(3, access));
    a->label = label;
    a->units = units;
    a->options = options;
    commitEdit("options");
    return true;
}

void ComponentAttributesDialog::optionsDialog(int p, const QString& name) {
    Instance* in = part(p);
    if (!in) return;
    const DynAttr* a = dyn::find(in->attrs, name);
    if (!a) return;
    QDialog dlg(this);
    dlg.setWindowTitle(QString("Options for %1").arg(name));
    auto* form = new QFormLayout(&dlg);
    auto* rule = new QComboBox;
    rule->addItems({"Users cannot see this attribute", "Users can see this attribute", "Users can edit as a textbox",
                    "Users can select from a list"});
    rule->setCurrentIndex(a->access);
    form->addRow("Display rule:", rule);
    auto* label = new QLineEdit(a->label);
    label->setPlaceholderText(name);
    form->addRow("Display label:", label);
    auto* units = new QComboBox;
    for (auto& u : kUnits) units->addItem(u.text, u.code);
    int ui = units->findData(a->units.toUpper());
    units->setCurrentIndex(ui < 0 ? 0 : ui);
    form->addRow("Units:", units);
    auto* list = new QPlainTextEdit;
    QStringList lines;
    for (auto& o : a->options) lines << QString("%1 = %2").arg(o.first, o.second);
    list->setPlainText(lines.join("\n"));
    list->setPlaceholderText("One choice per line: Label = value\nfor example  Oak = Oak  or  Wide = 900");
    form->addRow("List options:", list);
    auto sync = [rule, list] { list->setEnabled(rule->currentIndex() == 3); };
    connect(rule, &QComboBox::currentIndexChanged, &dlg, sync);
    sync();
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    form->addRow(bb);
    if (dlg.exec() != QDialog::Accepted) return;
    std::vector<std::pair<QString, QString>> opts;
    QString un = units->currentData().toString();
    for (const QString& line : list->toPlainText().split('\n', Qt::SkipEmptyParts)) {
        int eq = line.indexOf('=');
        QString l = (eq < 0 ? line : line.left(eq)).trimmed();
        QString v = (eq < 0 ? line : line.mid(eq + 1)).trimmed();
        QString parsed;
        if (parseAttrValue(v, un, doc_->model.units, parsed)) v = parsed;
        if (!l.isEmpty()) opts.push_back({l, v});
    }
    setOptions(p, name, rule->currentIndex(), label->text().trimmed(), un, opts);
}

}  // namespace gable
