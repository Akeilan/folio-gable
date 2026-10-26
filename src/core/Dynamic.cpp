// Folio Gable — dynamic components.
#include "core/Dynamic.h"
#include "core/Geometry.h"
#include <QColor>
#include <cmath>
#include <map>
#include <memory>
#include <set>

namespace gable {
namespace dyn {

static const double kDeg = M_PI / 180.0;

// ------------------------------------------------------------------ values

double Value::toNum(bool* ok) const {
    if (ok) *ok = true;
    if (!isText) return num;
    QString t = text.trimmed();
    if (t.compare("true", Qt::CaseInsensitive) == 0) return 1;
    if (t.compare("false", Qt::CaseInsensitive) == 0 || t.isEmpty()) return 0;
    bool good = false;
    double d = t.toDouble(&good);
    if (ok) *ok = good;
    return good ? d : 0;
}

QString Value::toText() const {
    if (isText) return text;
    if (std::isnan(num) || std::isinf(num)) return "0";
    double r = std::round(num);
    if (std::fabs(num - r) < 1e-9 && std::fabs(r) < 1e15) return QString::number((qint64)r);
    return QString::number(num, 'g', 10);
}

static Value parseStored(const QString& s) {
    bool ok = false;
    double d = s.trimmed().toDouble(&ok);
    if (ok) return Value(d);
    return Value(s);
}

// ------------------------------------------------------------------ formula parser

namespace {

enum class Tk { End, Num, Str, Name, Op, LP, RP, Comma };
struct Token { Tk k; QString s; double n = 0; };

bool tokenize(const QString& f, std::vector<Token>& out, QString& err) {
    int i = 0, n = f.size();
    while (i < n) {
        QChar c = f[i];
        if (c.isSpace()) { ++i; continue; }
        if (c.isDigit() || (c == '.' && i + 1 < n && f[i + 1].isDigit())) {
            int j = i;
            while (j < n && (f[j].isDigit() || f[j] == '.')) ++j;
            if (j < n && (f[j] == 'e' || f[j] == 'E') && j + 1 < n && (f[j + 1].isDigit() || ((f[j + 1] == '-' || f[j + 1] == '+') && j + 2 < n && f[j + 2].isDigit()))) {
                j += 2;
                while (j < n && f[j].isDigit()) ++j;
            }
            Token t{Tk::Num, f.mid(i, j - i)};
            t.n = t.s.toDouble();
            out.push_back(t);
            i = j;
            continue;
        }
        if (c == '"') {
            QString s;
            ++i;
            for (;;) {
                if (i >= n) { err = "A text in quotes isn't closed."; return false; }
                if (f[i] == '"') {
                    if (i + 1 < n && f[i + 1] == '"') { s += '"'; i += 2; continue; }
                    ++i;
                    break;
                }
                s += f[i++];
            }
            out.push_back({Tk::Str, s});
            continue;
        }
        if (c.isLetter() || c == '_') {
            int j = i;
            while (j < n && (f[j].isLetterOrNumber() || f[j] == '_' || f[j] == '!' || f[j] == '#')) ++j;
            out.push_back({Tk::Name, f.mid(i, j - i)});
            i = j;
            continue;
        }
        if (c == '(') { out.push_back({Tk::LP, "("}); ++i; continue; }
        if (c == ')') { out.push_back({Tk::RP, ")"}); ++i; continue; }
        if (c == ',') { out.push_back({Tk::Comma, ","}); ++i; continue; }
        if (c == '<' && i + 1 < n && (f[i + 1] == '=' || f[i + 1] == '>')) { out.push_back({Tk::Op, f.mid(i, 2)}); i += 2; continue; }
        if (c == '>' && i + 1 < n && f[i + 1] == '=') { out.push_back({Tk::Op, ">="}); i += 2; continue; }
        if (QString("+-*/^&=<>").contains(c)) { out.push_back({Tk::Op, QString(c)}); ++i; continue; }
        err = QString("Unexpected \"%1\".").arg(c);
        return false;
    }
    out.push_back({Tk::End, QString()});
    return true;
}

struct Ast {
    enum K { Num, Str, Ref, Call, Bin, Neg } k;
    double n = 0;
    QString s;   // text, name, function or operator
    std::vector<std::unique_ptr<Ast>> a;
};
using P = std::unique_ptr<Ast>;

struct Parser {
    std::vector<Token> t;
    size_t i = 0;
    QString err;
    const Token& peek() const { return t[i]; }
    bool isOp(const char* o) const { return t[i].k == Tk::Op && t[i].s == o; }
    P bin(const QString& op, P l, P r) {
        P p(new Ast{Ast::Bin});
        p->s = op;
        p->a.push_back(std::move(l));
        p->a.push_back(std::move(r));
        return p;
    }
    P expr() { return cmp(); }
    P cmp() {
        P l = cat();
        while (l && t[i].k == Tk::Op && (t[i].s == "=" || t[i].s == "<>" || t[i].s == "<" || t[i].s == ">" || t[i].s == "<=" || t[i].s == ">=")) {
            QString o = t[i++].s;
            P r = cat();
            if (!r) return nullptr;
            l = bin(o, std::move(l), std::move(r));
        }
        return l;
    }
    P cat() {
        P l = add();
        while (l && isOp("&")) { ++i; P r = add(); if (!r) return nullptr; l = bin("&", std::move(l), std::move(r)); }
        return l;
    }
    P add() {
        P l = mul();
        while (l && (isOp("+") || isOp("-"))) { QString o = t[i++].s; P r = mul(); if (!r) return nullptr; l = bin(o, std::move(l), std::move(r)); }
        return l;
    }
    P mul() {
        P l = pow();
        while (l && (isOp("*") || isOp("/"))) { QString o = t[i++].s; P r = pow(); if (!r) return nullptr; l = bin(o, std::move(l), std::move(r)); }
        return l;
    }
    P pow() {
        P l = unary();
        while (l && isOp("^")) { ++i; P r = unary(); if (!r) return nullptr; l = bin("^", std::move(l), std::move(r)); }
        return l;
    }
    P unary() {
        if (isOp("-")) { ++i; P x = unary(); if (!x) return nullptr; P p(new Ast{Ast::Neg}); p->a.push_back(std::move(x)); return p; }
        if (isOp("+")) { ++i; return unary(); }
        return primary();
    }
    P primary() {
        const Token& k = t[i];
        if (k.k == Tk::Num) { ++i; P p(new Ast{Ast::Num}); p->n = k.n; return p; }
        if (k.k == Tk::Str) { ++i; P p(new Ast{Ast::Str}); p->s = k.s; return p; }
        if (k.k == Tk::LP) {
            ++i;
            P x = expr();
            if (!x) return nullptr;
            if (t[i].k != Tk::RP) { err = "A bracket isn't closed."; return nullptr; }
            ++i;
            return x;
        }
        if (k.k == Tk::Name) {
            ++i;
            if (t[i].k == Tk::LP) {
                ++i;
                P p(new Ast{Ast::Call});
                p->s = k.s.toUpper();
                if (t[i].k != Tk::RP) {
                    for (;;) {
                        P x = expr();
                        if (!x) return nullptr;
                        p->a.push_back(std::move(x));
                        if (t[i].k == Tk::Comma) { ++i; continue; }
                        break;
                    }
                }
                if (t[i].k != Tk::RP) { err = QString("%1( isn't closed.").arg(k.s); return nullptr; }
                ++i;
                return p;
            }
            P p(new Ast{Ast::Ref});
            p->s = k.s;
            return p;
        }
        err = k.k == Tk::End ? "The formula ends too soon." : QString("Unexpected \"%1\".").arg(k.s);
        return nullptr;
    }
};

bool truthy(const Value& v) { return v.isText ? (v.toNum() != 0) : v.num != 0; }

bool equalValues(const Value& a, const Value& b) {
    bool oa = true, ob = true;
    double x = a.toNum(&oa), y = b.toNum(&ob);
    if (oa && ob && !(a.isText && b.isText)) return std::fabs(x - y) < 1e-6;
    return a.toText().compare(b.toText(), Qt::CaseInsensitive) == 0;
}

struct Eval {
    const Lookup& lookup;
    QString err;
    bool num(const Ast* a, double& d) {
        Value v;
        if (!run(a, v)) return false;
        bool ok = true;
        d = v.toNum(&ok);
        if (!ok) { err = QString("\"%1\" isn't a number.").arg(v.text); return false; }
        return true;
    }
    bool run(const Ast* a, Value& out) {
        switch (a->k) {
        case Ast::Num: out = Value(a->n); return true;
        case Ast::Str: out = Value(a->s); return true;
        case Ast::Ref: {
            QString u = a->s.toUpper();
            if (u == "TRUE") { out = Value(1.0); return true; }
            if (u == "FALSE") { out = Value(0.0); return true; }
            QString e;
            if (!lookup(a->s, out, e)) { err = e.isEmpty() ? QString("\"%1\" isn't an attribute.").arg(a->s) : e; return false; }
            return true;
        }
        case Ast::Neg: { double d; if (!num(a->a[0].get(), d)) return false; out = Value(-d); return true; }
        case Ast::Bin: {
            const QString& o = a->s;
            if (o == "&") {
                Value l, r;
                if (!run(a->a[0].get(), l) || !run(a->a[1].get(), r)) return false;
                out = Value(l.toText() + r.toText());
                return true;
            }
            if (o == "=" || o == "<>" || o == "<" || o == ">" || o == "<=" || o == ">=") {
                Value l, r;
                if (!run(a->a[0].get(), l) || !run(a->a[1].get(), r)) return false;
                bool eq = equalValues(l, r);
                if (o == "=") { out = Value(eq ? 1.0 : 0.0); return true; }
                if (o == "<>") { out = Value(eq ? 0.0 : 1.0); return true; }
                bool ol = true, orr = true;
                double x = l.toNum(&ol), y = r.toNum(&orr);
                int c = (ol && orr) ? (eq ? 0 : (x < y ? -1 : 1)) : l.toText().compare(r.toText(), Qt::CaseInsensitive);
                bool res = o == "<" ? c < 0 : o == ">" ? c > 0 : o == "<=" ? c <= 0 : c >= 0;
                out = Value(res ? 1.0 : 0.0);
                return true;
            }
            double x, y;
            if (!num(a->a[0].get(), x) || !num(a->a[1].get(), y)) return false;
            if (o == "+") out = Value(x + y);
            else if (o == "-") out = Value(x - y);
            else if (o == "*") out = Value(x * y);
            else if (o == "/") {
                if (y == 0) { err = "Division by zero."; return false; }
                out = Value(x / y);
            } else out = Value(std::pow(x, y));
            return true;
        }
        case Ast::Call: return call(a, out);
        }
        return false;
    }
    bool call(const Ast* a, Value& out) {
        const QString& f = a->s;
        size_t n = a->a.size();
        auto need = [&](size_t lo, size_t hi) {
            if (n < lo || n > hi) { err = QString("%1 takes %2 values.").arg(f).arg(lo == hi ? QString::number(lo) : QString("%1 to %2").arg(lo).arg(hi)); return false; }
            return true;
        };
        auto arg = [&](size_t k, double& d) { return num(a->a[k].get(), d); };
        if (f == "IF") {
            if (!need(2, 3)) return false;
            Value c;
            if (!run(a->a[0].get(), c)) return false;
            if (truthy(c)) return run(a->a[1].get(), out);
            if (n == 3) return run(a->a[2].get(), out);
            out = Value(0.0);
            return true;
        }
        if (f == "AND" || f == "OR") {
            if (!need(1, 99)) return false;
            bool r = f == "AND";
            for (auto& x : a->a) {
                Value v;
                if (!run(x.get(), v)) return false;
                if (f == "AND") r = r && truthy(v); else r = r || truthy(v);
            }
            out = Value(r ? 1.0 : 0.0);
            return true;
        }
        if (f == "NOT") { if (!need(1, 1)) return false; Value v; if (!run(a->a[0].get(), v)) return false; out = Value(truthy(v) ? 0.0 : 1.0); return true; }
        if (f == "MIN" || f == "MAX" || f == "SMALLEST" || f == "LARGEST" || f == "SUM" || f == "AVERAGE") {
            if (!need(1, 99)) return false;
            double r = 0, sum = 0;
            for (size_t k = 0; k < n; ++k) {
                double d;
                if (!arg(k, d)) return false;
                sum += d;
                if (k == 0) r = d;
                else if (f == "MIN" || f == "SMALLEST") r = std::min(r, d);
                else r = std::max(r, d);
            }
            out = Value(f == "SUM" ? sum : f == "AVERAGE" ? sum / n : r);
            return true;
        }
        if (f == "CHOOSE") {
            if (!need(2, 99)) return false;
            double d;
            if (!arg(0, d)) return false;
            int k = (int)d;
            if (k < 1 || k >= (int)n) { err = "CHOOSE: the index is out of range."; return false; }
            return run(a->a[k].get(), out);
        }
        if (f == "CONCATENATE") {
            QString s;
            for (auto& x : a->a) { Value v; if (!run(x.get(), v)) return false; s += v.toText(); }
            out = Value(s);
            return true;
        }
        if (f == "PI") { if (!need(0, 0)) return false; out = Value(M_PI); return true; }
        if (f == "ROUND" || f == "CEILING" || f == "FLOOR" || f == "MOD" || f == "POWER" || f == "ATAN2") {
            if (!need(f == "ROUND" || f == "CEILING" || f == "FLOOR" ? 1 : 2, 2)) return false;
            double x, y = (f == "ROUND") ? 0 : 1;
            if (!arg(0, x) || (n > 1 && !arg(1, y))) return false;
            if (f == "ROUND") { double p = std::pow(10.0, std::round(y)); out = Value(std::round(x * p) / p); }
            else if (f == "CEILING") out = Value(y == 0 ? 0 : std::ceil(x / y) * y);
            else if (f == "FLOOR") out = Value(y == 0 ? 0 : std::floor(x / y) * y);
            else if (f == "MOD") {
                if (y == 0) { err = "Division by zero."; return false; }
                out = Value(x - y * std::floor(x / y));
            } else if (f == "POWER") out = Value(std::pow(x, y));
            else out = Value(std::atan2(y, x) / kDeg);
            return true;
        }
        static const std::map<QString, double (*)(double)> one = {
            {"ABS", [](double x) { return std::fabs(x); }},
            {"INT", [](double x) { return std::floor(x); }},
            {"SQRT", [](double x) { return std::sqrt(x); }},
            {"EXP", [](double x) { return std::exp(x); }},
            {"LN", [](double x) { return std::log(x); }},
            {"LOG10", [](double x) { return std::log10(x); }},
            {"SIN", [](double x) { return std::sin(x * kDeg); }},
            {"COS", [](double x) { return std::cos(x * kDeg); }},
            {"TAN", [](double x) { return std::tan(x * kDeg); }},
            {"ASIN", [](double x) { return std::asin(x) / kDeg; }},
            {"ACOS", [](double x) { return std::acos(x) / kDeg; }},
            {"ATAN", [](double x) { return std::atan(x) / kDeg; }},
            {"RADIANS", [](double x) { return x * kDeg; }},
            {"DEGREES", [](double x) { return x / kDeg; }},
            {"SIGN", [](double x) { return x > 0 ? 1.0 : x < 0 ? -1.0 : 0.0; }},
            {"EVEN", [](double x) { double r = std::ceil(std::fabs(x) / 2) * 2; return x < 0 ? -r : r; }},
            {"ODD", [](double x) { double r = std::ceil((std::fabs(x) + 1) / 2) * 2 - 1; return x < 0 ? -r : r; }},
        };
        auto it = one.find(f);
        if (it != one.end()) {
            if (!need(1, 1)) return false;
            double x;
            if (!arg(0, x)) return false;
            double r = it->second(x);
            if (std::isnan(r)) { err = QString("%1 can't take that value.").arg(f); return false; }
            out = Value(r);
            return true;
        }
        err = QString("%1 isn't a function a formula can use.").arg(f);
        return false;
    }
};

}  // namespace

bool evaluate(const QString& formula, const Lookup& lookup, Value& out, QString* err) {
    QString f = formula.trimmed();
    if (f.startsWith('=')) f = f.mid(1);
    Parser p;
    QString e;
    if (!tokenize(f, p.t, e)) { if (err) *err = e; return false; }
    P ast = p.expr();
    if (ast && p.peek().k != Tk::End) { p.err = QString("Unexpected \"%1\".").arg(p.peek().s); ast.reset(); }
    if (!ast) { if (err) *err = p.err; return false; }
    Eval ev{lookup};
    if (!ev.run(ast.get(), out)) { if (err) *err = ev.err; return false; }
    return true;
}

// ------------------------------------------------------------------ attributes

const std::vector<StdAttr>& standardAttributes() {
    static const std::vector<StdAttr> v = {
        {"Position", "X", "LENGTH", "Distance of the component's origin along its parent's red axis."},
        {"Position", "Y", "LENGTH", "Distance of the component's origin along its parent's green axis."},
        {"Position", "Z", "LENGTH", "Distance of the component's origin along its parent's blue axis."},
        {"Size", "LenX", "LENGTH", "Size along the component's red axis."},
        {"Size", "LenY", "LENGTH", "Size along the component's green axis."},
        {"Size", "LenZ", "LENGTH", "Size along the component's blue axis."},
        {"Rotation", "RotX", "DEGREES", "Rotation about the red axis, in degrees."},
        {"Rotation", "RotY", "DEGREES", "Rotation about the green axis, in degrees."},
        {"Rotation", "RotZ", "DEGREES", "Rotation about the blue axis, in degrees."},
        {"Behaviors", "Material", "STRING", "The material the component is painted with, by name."},
        {"Behaviors", "Hidden", "BOOLEAN", "TRUE hides the component."},
        {"Behaviors", "onClick", "STRING", "What the Interact tool does on a click: SET(\"attribute\", value1, value2, ...) or ANIMATE(...)."},
        {"Behaviors", "Copies", "INTEGER", "How many copies to make; their formulas can use COPY (1, 2, ...)."},
        {"Component Info", "Name", "STRING", "The instance's name."},
        {"Component Info", "Summary", "STRING", "A line about the component, shown in Component Options."},
        {"Component Info", "Description", "STRING", "A longer description."},
        {"Component Info", "ItemCode", "STRING", "A product or part code."},
    };
    return v;
}

QString defaultUnits(const QString& name) {
    for (auto& s : standardAttributes())
        if (name.compare(s.name, Qt::CaseInsensitive) == 0) return s.units;
    return "LENGTH";
}

const DynAttr* find(const std::vector<DynAttr>& a, const QString& name) {
    for (auto& x : a)
        if (x.name.compare(name, Qt::CaseInsensitive) == 0) return &x;
    return nullptr;
}
DynAttr* find(std::vector<DynAttr>& a, const QString& name) {
    for (auto& x : a)
        if (x.name.compare(name, Qt::CaseInsensitive) == 0) return &x;
    return nullptr;
}

static bool defHasDynamic(const Model& m, int def, int depth = 0) {
    if (def < 0 || def >= (int)m.defs.size() || depth > 32) return false;
    for (auto& c : m.defs[def].ents.insts)
        if (c.alive && (!c.attrs.empty() || defHasDynamic(m, c.def, depth + 1))) return true;
    return false;
}

bool isDynamic(const Model& m, const Instance& in) { return !in.attrs.empty() || defHasDynamic(m, in.def); }

// ------------------------------------------------------------------ evaluation tree

namespace {

int defOf(const Model& m, const Entities& E) {
    if (&E == &m.root) return -1;
    for (size_t i = 0; i < m.defs.size(); ++i)
        if (&m.defs[i].ents == &E) return (int)i;
    return -2;
}

void eulerOf(const Vec3 r[3], double& rx, double& ry, double& rz) {
    // R = Rz * Ry * Rx; columns r[0..2]
    double r20 = r[0].z;
    ry = std::asin(std::max(-1.0, std::min(1.0, -r20))) / kDeg;
    if (std::fabs(r20) < 0.999999) {
        rz = std::atan2(r[0].y, r[0].x) / kDeg;
        rx = std::atan2(r[1].z, r[2].z) / kDeg;
    } else {
        rz = std::atan2(-r[1].x, r[1].y) / kDeg;
        rx = 0;
    }
}

struct Node {
    Model* m;
    int cdef;          // definition holding the instance (-1 root)
    int inst;
    Node* parent;
    int copy;
    QStringList* errors;
    std::map<QString, Value> cache;
    std::set<QString> busy;

    Entities& E() const { return m->ents(cdef); }
    Instance& I() const { return E().insts[inst]; }
    QString who() const {
        const Instance& in = I();
        QString n = in.name.isEmpty() ? m->defs[in.def].name : in.name;
        return copy ? QString("%1 (copy %2)").arg(n).arg(copy) : n;
    }

    bool builtin(const QString& key, Value& out) const {
        const Instance& in = I();
        Vec3 r[3] = {in.xf.axis(0), in.xf.axis(1), in.xf.axis(2)};
        if (key == "x") { out = Value(in.xf.origin().x); return true; }
        if (key == "y") { out = Value(in.xf.origin().y); return true; }
        if (key == "z") { out = Value(in.xf.origin().z); return true; }
        if (key == "lenx" || key == "leny" || key == "lenz") {
            int i = key[3].unicode() - 'x';
            BBox b = m->defBBox(in.def);
            double s = b.valid() ? (i == 0 ? b.size().x : i == 1 ? b.size().y : b.size().z) : 0;
            out = Value(s * r[i].length());
            return true;
        }
        if (key == "rotx" || key == "roty" || key == "rotz") {
            Vec3 n[3] = {r[0].normalized(), r[1].normalized(), r[2].normalized()};
            double rx, ry, rz;
            eulerOf(n, rx, ry, rz);
            out = Value(key == "rotx" ? rx : key == "roty" ? ry : rz);
            return true;
        }
        if (key == "name") { out = Value(in.name); return true; }
        if (key == "hidden") { out = Value(in.hidden ? 1.0 : 0.0); return true; }
        if (key == "material") { out = Value(in.material >= 0 ? m->materials[in.material].name : QString()); return true; }
        if (key == "copies") { out = Value(0.0); return true; }
        if (key == "copy") { out = Value((double)copy); return true; }
        return false;
    }

    int childIndex(const QString& name) const {
        const Entities& D = m->defs[I().def].ents;
        for (size_t i = 0; i < D.insts.size(); ++i) {
            const Instance& c = D.insts[i];
            if (!c.alive || c.copyOf >= 0) continue;
            if (c.name.compare(name, Qt::CaseInsensitive) == 0) return (int)i;
        }
        for (size_t i = 0; i < D.insts.size(); ++i) {
            const Instance& c = D.insts[i];
            if (!c.alive || c.copyOf >= 0) continue;
            QString dn = m->defs[c.def].name;
            if (dn.compare(name, Qt::CaseInsensitive) == 0) return (int)i;
            int hash = dn.indexOf('#');   // "Board#2" answers to Board
            if (hash > 0 && dn.left(hash).compare(name, Qt::CaseInsensitive) == 0) return (int)i;
        }
        return -1;
    }

    bool resolve(const QString& ref, Value& out, QString& err) {
        int bang = ref.indexOf('!');
        if (bang < 0) {
            if (ref.compare("copy", Qt::CaseInsensitive) == 0) { out = Value((double)copy); return true; }
            return get(ref, out, err);
        }
        QString who = ref.left(bang), attr = ref.mid(bang + 1);
        if (who.compare("parent", Qt::CaseInsensitive) == 0) {
            if (!parent) { err = QString("%1 has no parent component.").arg(this->who()); return false; }
            return parent->get(attr, out, err);
        }
        int c = childIndex(who);
        if (c < 0) { err = QString("There is no part called \"%1\".").arg(who); return false; }
        Node child{m, I().def, c, this, 0, nullptr, {}, {}};
        return child.get(attr, out, err);
    }

    bool get(const QString& name, Value& out, QString& err) {
        QString key = name.toLower();
        auto it = cache.find(key);
        if (it != cache.end()) { out = it->second; return true; }
        DynAttr* a = find(I().attrs, name);
        if (!a) {
            if (builtin(key, out)) return true;
            err = QString("\"%1\" isn't an attribute of %2.").arg(name, who());
            return false;
        }
        if (a->formula.trimmed().isEmpty() || key == "onclick") {
            out = parseStored(a->value);
            cache[key] = out;
            return true;
        }
        if (busy.count(key)) { err = QString("%1 of %2 refers to itself.").arg(a->name, who()); return false; }
        busy.insert(key);
        QString formula = a->formula;
        bool ok = evaluate(formula, [this](const QString& r, Value& v, QString& e) { return resolve(r, v, e); }, out, &err);
        busy.erase(key);
        a = find(I().attrs, name);   // (the vector is not resized during evaluation, but be safe)
        if (!ok) {
            out = parseStored(a ? a->value : QString());
            return false;
        }
        if (a) a->value = out.toText();
        cache[key] = out;
        return true;
    }

    bool num(const char* name, double& d) {
        if (!find(I().attrs, name)) return false;
        Value v;
        QString e;
        if (!get(name, v, e)) { if (errors) errors->append(QString("%1: %2").arg(who(), e)); return false; }
        bool ok = true;
        d = v.toNum(&ok);
        if (!ok && errors) errors->append(QString("%1: %2 isn't a number.").arg(who(), name));
        return ok;
    }
};

struct Redrawer {
    Model& m;
    QStringList* errors;
    bool ok = true;

    void note(const QString& e) {
        ok = false;
        if (errors) errors->append(e);
    }

    int cloneDef(int d) {
        ComponentDef copy = m.defs[d];
        copy.name = m.uniqueDefName(copy.name);
        m.defs.push_back(std::move(copy));
        return (int)m.defs.size() - 1;
    }

    // Scale put on a component with dynamic parts (by the Scale tool) becomes its LenX/LenY/LenZ.
    void foldScale(Node& n) {
        Instance& in = n.I();
        static const char* lens[3] = {"LenX", "LenY", "LenZ"};
        double sc[3];
        bool any = false;
        for (int i = 0; i < 3; ++i) {
            sc[i] = in.xf.axis(i).length();
            if (std::fabs(sc[i] - 1) > 1e-6) {
                any = true;
                if (!find(in.attrs, lens[i])) return;
            }
        }
        if (!any) return;
        for (int i = 0; i < 3; ++i) {
            if (std::fabs(sc[i] - 1) <= 1e-6) continue;
            DynAttr* a = find(in.attrs, lens[i]);
            if (a->formula.trimmed().isEmpty()) {
                bool okn = false;
                double v = a->value.toDouble(&okn);
                if (!okn) { Value b; n.builtin(QString(lens[i]).toLower(), b); v = b.num / sc[i]; }
                a->value = Value(v * sc[i]).toText();
            }
        }
        for (int i = 0; i < 3; ++i) {
            Vec3 ax = in.xf.axis(i) * (1.0 / sc[i]);
            in.xf.at(0, i) = ax.x; in.xf.at(1, i) = ax.y; in.xf.at(2, i) = ax.z;
        }
    }

    void apply(Node& n, bool hasKids) {
        Instance& in = n.I();
        double d;
        Vec3 o = in.xf.origin();
        Vec3 r[3];
        double sc[3];
        for (int i = 0; i < 3; ++i) { r[i] = in.xf.axis(i); sc[i] = r[i].length(); r[i] = sc[i] > 1e-12 ? r[i] * (1.0 / sc[i]) : Vec3(i == 0, i == 1, i == 2); }
        bool changed = false;
        double rx, ry, rz;
        eulerOf(r, rx, ry, rz);
        bool rot = false;
        if (n.num("RotX", d)) { rx = d; rot = true; }
        if (n.num("RotY", d)) { ry = d; rot = true; }
        if (n.num("RotZ", d)) { rz = d; rot = true; }
        if (rot) {
            Mat4 R = Mat4::rotation({0, 0, 1}, rz * kDeg) * Mat4::rotation({0, 1, 0}, ry * kDeg) * Mat4::rotation({1, 0, 0}, rx * kDeg);
            for (int i = 0; i < 3; ++i) r[i] = R.axis(i);
            changed = true;
        }
        if (!hasKids) {
            BBox b = m.defBBox(in.def);
            static const char* lens[3] = {"LenX", "LenY", "LenZ"};
            for (int i = 0; i < 3; ++i) {
                double s = b.valid() ? (i == 0 ? b.size().x : i == 1 ? b.size().y : b.size().z) : 0;
                if (n.num(lens[i], d) && s > 1e-9) {
                    sc[i] = std::max(d, 1e-3) / s;
                    changed = true;
                }
            }
        }
        if (n.num("X", d)) { o.x = d; changed = true; }
        if (n.num("Y", d)) { o.y = d; changed = true; }
        if (n.num("Z", d)) { o.z = d; changed = true; }
        if (changed) {
            Mat4 x;
            for (int i = 0; i < 3; ++i) {
                Vec3 c = r[i] * sc[i];
                x.at(0, i) = c.x; x.at(1, i) = c.y; x.at(2, i) = c.z;
            }
            x.at(0, 3) = o.x; x.at(1, 3) = o.y; x.at(2, 3) = o.z;
            in.xf = x;
        }
        Value v;
        QString e;
        if (find(in.attrs, "Hidden")) {
            if (n.get("Hidden", v, e)) in.hidden = truthy(v);
            else note(QString("%1: %2").arg(n.who(), e));
        }
        if (find(in.attrs, "Material")) {
            if (n.get("Material", v, e)) {
                QString name = v.toText().trimmed();
                int mat = name.isEmpty() ? -1 : m.findMaterial(name);
                if (mat < 0 && !name.isEmpty() && QColor(name).isValid()) mat = m.addMaterial(name, QColor(name));
                if (mat >= 0 || name.isEmpty()) in.material = mat;
                else note(QString("%1: there is no material called \"%2\".").arg(n.who(), name));
            } else note(QString("%1: %2").arg(n.who(), e));
        }
        if (find(in.attrs, "Name")) {
            if (n.get("Name", v, e)) in.name = v.toText();
        }
    }

    void process(Node& n, int depth = 0) {
        if (depth > 32) { note("Components are nested too deeply."); return; }
        bool kids = defHasDynamic(m, n.I().def);
        if (kids) foldScale(n);
        // every attribute, so each one's value is up to date and its errors are reported
        std::vector<QString> names;
        for (auto& a : n.I().attrs) names.push_back(a.name);
        for (auto& nm : names) {
            if (nm.compare("onClick", Qt::CaseInsensitive) == 0) continue;
            Value v;
            QString e;
            if (!n.get(nm, v, e)) note(QString("%1: %2").arg(n.who(), e));
        }
        apply(n, kids);
        n.E().touch();
        if (!kids) return;
        int def = n.I().def;
        if (m.instanceCount(def) > 1) {
            def = cloneDef(def);
            n.I().def = def;
        }
        size_t count = m.defs[def].ents.insts.size();
        std::vector<bool> isOriginal(count, false);
        for (size_t i = 0; i < count; ++i) {
            const Instance& c = m.defs[def].ents.insts[i];
            if (!c.alive || c.copyOf >= 0 || !isDynamic(m, c)) continue;
            isOriginal[i] = true;
        }
        for (size_t i = 0; i < count; ++i) {
            if (!isOriginal[i]) continue;
            Node cn{&m, def, (int)i, &n, 0, errors, {}, {}};
            process(cn, depth + 1);
            int copies = 0;
            {
                Node cq{&m, def, (int)i, &n, 0, errors, {}, {}};
                double d;
                if (cq.num("Copies", d)) copies = std::max(0, std::min(1000, (int)std::lround(d)));
            }
            std::vector<int> have;
            {
                const auto& insts = m.defs[def].ents.insts;
                std::vector<std::pair<int, int>> ord;
                for (size_t j = 0; j < insts.size(); ++j)
                    if (insts[j].alive && insts[j].copyOf == (int)i) ord.push_back({insts[j].copyIndex, (int)j});
                std::sort(ord.begin(), ord.end());
                for (auto& p : ord) have.push_back(p.second);
            }
            for (int k = 1; k <= copies; ++k) {
                Instance c = m.defs[def].ents.insts[i];
                c.copyOf = (int)i;
                c.copyIndex = k;
                int j;
                if (k - 1 < (int)have.size()) {
                    j = have[k - 1];
                    c.hidden = false;
                    m.defs[def].ents.insts[j] = c;
                } else {
                    m.defs[def].ents.insts.push_back(c);
                    j = (int)m.defs[def].ents.insts.size() - 1;
                }
                Node cc{&m, def, j, &n, k, errors, {}, {}};
                process(cc, depth + 1);
            }
            for (size_t k = copies; k < have.size(); ++k) m.defs[def].ents.insts[have[k]].alive = false;
        }
        // copies whose original is gone or no longer makes them
        auto& insts = m.defs[def].ents.insts;
        for (auto& c : insts)
            if (c.alive && c.copyOf >= 0 && (c.copyOf >= (int)count || !isOriginal[c.copyOf])) c.alive = false;
        m.defs[def].ents.touch();
    }
};

}  // namespace

bool currentValue(Model& m, Entities& E, int inst, const QString& name, Value& out) {
    int cdef = defOf(m, E);
    if (cdef < -1 || inst < 0 || inst >= (int)E.insts.size()) return false;
    Node n{&m, cdef, inst, nullptr, 0, nullptr, {}, {}};
    return n.builtin(name.toLower(), out);
}

bool redraw(Model& m, Entities& E, int inst, QStringList* errors) {
    int cdef = defOf(m, E);
    if (cdef < -1 || inst < 0 || inst >= (int)E.insts.size() || !E.insts[inst].alive) return false;
    Redrawer r{m, errors};
    Node n{&m, cdef, inst, nullptr, E.insts[inst].copyIndex, errors, {}, {}};
    r.process(n);
    return r.ok;
}

int clickTarget(const Model& m, const Entities& E, const std::vector<int>& path) {
    const Entities* cur = &E;
    int best = -1;
    for (size_t i = 0; i < path.size(); ++i) {
        if (path[i] < 0 || path[i] >= (int)cur->insts.size()) break;
        const Instance& in = cur->insts[path[i]];
        const DynAttr* a = find(in.attrs, "onClick");
        if (a && !(a->formula.trimmed().isEmpty() && a->value.trimmed().isEmpty())) best = (int)i;
        if (in.def < 0 || in.def >= (int)m.defs.size()) break;
        cur = &m.defs[in.def].ents;
    }
    return best;
}

// Splits "SET(...); ANIMATE(...)" into calls: name and the text of each argument.
static bool splitActions(const QString& s, std::vector<std::pair<QString, QStringList>>& out, QString& err) {
    int i = 0, n = s.size();
    while (i < n) {
        while (i < n && (s[i].isSpace() || s[i] == ';')) ++i;
        if (i >= n) break;
        int j = i;
        while (j < n && (s[j].isLetterOrNumber() || s[j] == '_')) ++j;
        QString name = s.mid(i, j - i).toUpper();
        while (j < n && s[j].isSpace()) ++j;
        if (name.isEmpty() || j >= n || s[j] != '(') { err = "onClick should be like SET(\"attribute\", value1, value2)."; return false; }
        ++j;
        QStringList args;
        QString cur;
        int depth = 0;
        bool quote = false;
        for (; j < n; ++j) {
            QChar c = s[j];
            if (quote) { cur += c; if (c == '"') quote = false; continue; }
            if (c == '"') { quote = true; cur += c; continue; }
            if (c == '(') ++depth;
            if (c == ')') { if (depth == 0) break; --depth; }
            if (c == ',' && depth == 0) { args << cur.trimmed(); cur.clear(); continue; }
            cur += c;
        }
        if (j >= n) { err = QString("%1( isn't closed.").arg(name); return false; }
        if (!cur.trimmed().isEmpty() || !args.isEmpty()) args << cur.trimmed();
        out.push_back({name, args});
        i = j + 1;
    }
    return true;
}

bool clickChanges(Model& m, Entities& E, const std::vector<int>& path, int depth, std::vector<ClickChange>& out, QStringList* errors) {
    int cdef = defOf(m, E);
    if (cdef < -1 || depth < 0 || depth >= (int)path.size()) return false;
    std::vector<std::unique_ptr<Node>> chain;
    int d = cdef;
    for (int i = 0; i <= depth; ++i) {
        chain.emplace_back(new Node{&m, d, path[i], i ? chain.back().get() : nullptr, 0, errors, {}, {}});
        d = chain.back()->I().def;
    }
    Node& n = *chain.back();
    const DynAttr* a = find(n.I().attrs, "onClick");
    if (!a) return false;
    QString text = a->formula.trimmed().isEmpty() ? a->value : a->formula;
    if (text.startsWith('=')) text = text.mid(1);
    std::vector<std::pair<QString, QStringList>> calls;
    QString err;
    if (!splitActions(text, calls, err)) { if (errors) errors->append(QString("%1: %2").arg(n.who(), err)); return false; }
    bool ok = true;
    for (auto& [name, args] : calls) {
        bool animate = name.startsWith("ANIMATE");
        if (name == "REDRAW" || name == "ALERT") continue;
        if (name != "SET" && !animate) {
            if (errors) errors->append(QString("%1: onClick can't run %2.").arg(n.who(), name));
            ok = false;
            continue;
        }
        if (args.size() < 2) { if (errors) errors->append(QString("%1: %2 needs an attribute and values.").arg(n.who(), name)); ok = false; continue; }
        QString target = args[0];
        if (target.startsWith('"') && target.endsWith('"') && target.size() >= 2) target = target.mid(1, target.size() - 2);
        int first = name == "ANIMATECUSTOM" ? 4 : 1;   // ANIMATECUSTOM("attr", time, easein, easeout, values...)
        Node* tn = &n;
        std::vector<int> tpath(path.begin(), path.begin() + depth + 1);
        QString attr = target;
        std::unique_ptr<Node> child;
        int bang = target.indexOf('!');
        if (bang >= 0) {
            QString who = target.left(bang);
            attr = target.mid(bang + 1);
            if (who.compare("parent", Qt::CaseInsensitive) == 0) {
                if (!n.parent) { if (errors) errors->append(QString("%1 has no parent component.").arg(n.who())); ok = false; continue; }
                tn = n.parent;
                tpath.pop_back();
            } else {
                int c = n.childIndex(who);
                if (c < 0) { if (errors) errors->append(QString("%1: there is no part called \"%2\".").arg(n.who(), who)); ok = false; continue; }
                child.reset(new Node{&m, n.I().def, c, &n, 0, errors, {}, {}});
                tn = child.get();
                tpath.push_back(c);
            }
        }
        std::vector<Value> vals;
        bool good = true;
        for (int k = first; k < args.size(); ++k) {
            Value v;
            QString e;
            if (!evaluate(args[k], [&](const QString& r, Value& o, QString& e2) { return n.resolve(r, o, e2); }, v, &e)) {
                if (errors) errors->append(QString("%1: %2").arg(n.who(), e));
                good = false;
                break;
            }
            vals.push_back(v);
        }
        if (!good || vals.empty()) { ok = false; continue; }
        Value cur;
        QString e;
        if (!tn->get(attr, cur, e)) cur = vals.back();
        size_t next = 0;
        for (size_t k = 0; k < vals.size(); ++k)
            if (equalValues(cur, vals[k])) { next = (k + 1) % vals.size(); break; }
        ClickChange c;
        c.path = tpath;
        c.attr = attr;
        c.from = cur;
        c.to = vals[next];
        c.animate = animate;
        out.push_back(c);
    }
    return ok;
}

void makePathUnique(Model& m, Entities& E, const std::vector<int>& path) {
    int cdef = defOf(m, E);
    if (cdef < -1) return;
    for (size_t i = 0; i + 1 < path.size(); ++i) {
        Entities& C = m.ents(cdef);
        if (path[i] < 0 || path[i] >= (int)C.insts.size()) return;
        int def = C.insts[path[i]].def;
        if (m.instanceCount(def) > 1) {
            ComponentDef copy = m.defs[def];
            copy.name = m.uniqueDefName(copy.name);
            m.defs.push_back(std::move(copy));
            def = (int)m.defs.size() - 1;
            m.ents(cdef).insts[path[i]].def = def;
            m.ents(cdef).touch();
        }
        cdef = def;
    }
}

// ------------------------------------------------------------------ samples

QStringList sampleNames() { return {"Bookshelf", "Door", "Fence"}; }

QString sampleDescription(int which) {
    switch (which) {
    case 0: return "Shelves are added or taken away as you make it taller; width, depth, spacing and finish in Component Options.";
    case 1: return "Click it with Interact to open and close; width and height in Component Options.";
    case 2: return "Posts and rails repeat along its length; stretch it with the Scale tool or set the length.";
    }
    return QString();
}

static void boxInto(Entities& E, double sx, double sy, double sz) {
    geom::addPolyline(E, {Vec3(0, 0, 0), Vec3(sx, 0, 0), Vec3(sx, sy, 0), Vec3(0, sy, 0)}, true);
    for (int f = 0; f < (int)E.faces.size(); ++f) {
        if (!E.faces[f].alive) continue;
        geom::pushPull(E, f, E.faces[f].plane.n.z < 0 ? -sz : sz, false);
        break;
    }
}

static DynAttr A(const char* n, const QString& formula, const QString& value = QString(), int access = 0,
                 const QString& label = QString(), std::vector<std::pair<QString, QString>> options = {}) {
    DynAttr a;
    a.name = n;
    a.formula = formula;
    a.value = value;
    a.access = access;
    a.label = label;
    a.units = defaultUnits(n);
    a.options = std::move(options);
    return a;
}

static int material(Model& m, const QString& name, QColor c) {
    int i = m.findMaterial(name);
    return i >= 0 ? i : m.addMaterial(name, c);
}

static int part(Model& m, const QString& name, double sx, double sy, double sz) {
    int d = m.addDef(m.uniqueDefName(name), false);
    boxInto(m.defs[d].ents, sx, sy, sz);
    return d;
}

static Instance child(int def, const QString& name, std::vector<DynAttr> attrs) {
    Instance in;
    in.def = def;
    in.name = name;
    in.attrs = std::move(attrs);
    return in;
}

int addSample(Model& m, int which) {
    int top = -1;
    if (which == 0) {
        material(m, "Oak", QColor(196, 152, 98));
        material(m, "Walnut", QColor(104, 72, 48));
        material(m, "White", QColor(240, 240, 236));
        int side = part(m, "Shelf side", 18, 300, 1000), board = part(m, "Shelf board", 1000, 300, 18);
        top = m.addDef(m.uniqueDefName("Bookshelf"), false);
        auto& E = m.defs[top].ents;
        E.insts.push_back(child(side, "Left side", {A("LenY", "Parent!LenY"), A("LenZ", "Parent!LenZ"), A("Material", "Parent!Finish")}));
        E.insts.push_back(child(side, "Right side", {A("X", "Parent!LenX - 18"), A("LenY", "Parent!LenY"), A("LenZ", "Parent!LenZ"),
                                                     A("Material", "Parent!Finish")}));
        E.insts.push_back(child(board, "Shelf", {A("X", "18"), A("LenX", "Parent!LenX - 36"), A("LenY", "Parent!LenY"),
                                                 A("Copies", "MAX(1, ROUND((Parent!LenZ - 18) / Parent!Spacing))"),
                                                 A("Z", "COPY * (Parent!LenZ - 18) / Copies"), A("Material", "Parent!Finish")}));
        DynAttr fin = A("Finish", "", "Oak", 3, "Finish", {{"Oak", "Oak"}, {"Walnut", "Walnut"}, {"White", "White"}});
        fin.units = "STRING";
        m.defs[top].attrs = {A("Summary", "", "Freestanding bookshelf", 1, "About"),
                             A("LenX", "", "800", 2, "Width"), A("LenY", "", "300", 2, "Depth"), A("LenZ", "", "1800", 2, "Height"),
                             A("Spacing", "", "350", 3, "Shelf spacing", {{"300 mm", "300"}, {"350 mm", "350"}, {"400 mm", "400"}}),
                             fin};
        m.defs[top].description = sampleDescription(0);
    } else if (which == 1) {
        material(m, "Door paint", QColor(236, 232, 222));
        material(m, "Frame", QColor(120, 120, 124));
        int leaf = part(m, "Door leaf", 900, 40, 2100), jamb = part(m, "Door jamb", 40, 120, 2140), head = part(m, "Door head", 980, 120, 40);
        top = m.addDef(m.uniqueDefName("Door"), false);
        auto& E = m.defs[top].ents;
        E.insts.push_back(child(leaf, "Leaf", {A("X", "0"), A("Y", "40"), A("LenX", "Parent!Width"), A("LenZ", "Parent!Height"),
                                               A("Material", "\"Door paint\""), A("RotZ", "", "0"),
                                               A("onClick", "", "ANIMATE(\"RotZ\", 0, 90)")}));
        E.insts.push_back(child(jamb, "Left jamb", {A("X", "-40"), A("LenZ", "Parent!Height + 40"), A("Material", "\"Frame\"")}));
        E.insts.push_back(child(jamb, "Right jamb", {A("X", "Parent!Width"), A("LenZ", "Parent!Height + 40"), A("Material", "\"Frame\"")}));
        E.insts.push_back(child(head, "Head", {A("X", "-40"), A("Z", "Parent!Height"), A("LenX", "Parent!Width + 80"), A("Material", "\"Frame\"")}));
        m.defs[top].attrs = {A("Summary", "", "Hinged door; click with Interact to open", 1, "About"),
                             A("Width", "", "900", 3, "Width", {{"800 mm", "800"}, {"900 mm", "900"}, {"1000 mm", "1000"}}),
                             A("Height", "", "2100", 2, "Height")};
        m.defs[top].description = sampleDescription(1);
    } else if (which == 2) {
        material(m, "Fence wood", QColor(150, 118, 80));
        int post = part(m, "Fence post", 90, 90, 1200), rail = part(m, "Fence rail", 1000, 40, 90);
        top = m.addDef(m.uniqueDefName("Fence"), false);
        auto& E = m.defs[top].ents;
        E.insts.push_back(child(post, "Post", {A("Copies", "CEILING(Parent!LenX / Parent!PostSpacing, 1)"),
                                               A("X", "COPY * (Parent!LenX - 90) / Copies"), A("LenZ", "Parent!Height"),
                                               A("Material", "\"Fence wood\"")}));
        E.insts.push_back(child(rail, "Rail", {A("Copies", "1"), A("Y", "90"), A("LenX", "Parent!LenX"),
                                               A("Z", "IF(COPY = 0, 250, Parent!Height - 250)"), A("Material", "\"Fence wood\"")}));
        m.defs[top].attrs = {A("LenX", "", "4000", 2, "Length"), A("Height", "", "1200", 2, "Height"),
                             A("PostSpacing", "", "1800", 3, "Post spacing", {{"1.2 m", "1200"}, {"1.8 m", "1800"}, {"2.4 m", "2400"}})};
        m.defs[top].description = sampleDescription(2);
    }
    if (top < 0) return -1;
    // shape the definition once, as a first copy placed would look
    Instance probe;
    probe.def = top;
    probe.attrs = m.defs[top].attrs;
    m.root.insts.push_back(probe);
    int idx = (int)m.root.insts.size() - 1;
    redraw(m, m.root, idx);
    top = m.root.insts[idx].def;
    m.root.insts.pop_back();   // the probe was the last instance
    m.root.touch();
    return top;
}

void setAttr(Model& m, Entities& E, const std::vector<int>& path, const QString& attr, const Value& v) {
    int cdef = defOf(m, E);
    if (cdef < -1 || path.empty()) return;
    for (size_t i = 0; i + 1 < path.size(); ++i) cdef = m.ents(cdef).insts[path[i]].def;
    Entities& C = m.ents(cdef);
    Instance& in = C.insts[path.back()];
    DynAttr* a = find(in.attrs, attr);
    if (!a) {
        DynAttr na;
        na.name = attr;
        na.units = defaultUnits(attr);
        in.attrs.push_back(na);
        a = &in.attrs.back();
    }
    a->formula.clear();
    a->value = v.toText();
    C.touch();
}

}  // namespace dyn
}  // namespace gable
