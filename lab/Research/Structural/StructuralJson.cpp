// Persistance JSON du portique plan et de ses résultats (voir StructuralJson.h).

#include "StructuralJson.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <cmath>
#include <limits>

namespace TSALab::Research::Structural
{

namespace
{
namespace S = tsalab::structural;
namespace N = tsalab::numerics;

constexpr char kModelFormat[] = "tsalab-model2d";
constexpr char kResultFormat[] = "tsalab-result2d";

// ---------------------------------------------------------------------------------------------
// Énumérations : identifiants stables
// ---------------------------------------------------------------------------------------------

const char* directionId(S::LoadDirection d)
{
    switch (d)
    {
    case S::LoadDirection::LocalX: return "local-x";
    case S::LoadDirection::LocalY: return "local-y";
    case S::LoadDirection::GlobalX: return "global-x";
    case S::LoadDirection::GlobalY: return "global-y";
    }
    return "local-y";
}

bool directionFromId(const QString& id, S::LoadDirection& out)
{
    for (auto d : { S::LoadDirection::LocalX, S::LoadDirection::LocalY, S::LoadDirection::GlobalX, S::LoadDirection::GlobalY })
        if (id == QLatin1String(directionId(d)))
        {
            out = d;
            return true;
        }
    return false;
}

const char* loadTypeId(S::ElementLoadType t)
{
    switch (t)
    {
    case S::ElementLoadType::Distributed: return "distributed";
    case S::ElementLoadType::PointForce: return "point-force";
    case S::ElementLoadType::PointMoment: return "point-moment";
    case S::ElementLoadType::Thermal: return "thermal";
    case S::ElementLoadType::InitialStrain: return "initial-strain";
    case S::ElementLoadType::SelfWeight: return "self-weight";
    }
    return "distributed";
}

bool loadTypeFromId(const QString& id, S::ElementLoadType& out)
{
    for (auto t : { S::ElementLoadType::Distributed, S::ElementLoadType::PointForce, S::ElementLoadType::PointMoment, S::ElementLoadType::Thermal,
                    S::ElementLoadType::InitialStrain, S::ElementLoadType::SelfWeight })
        if (id == QLatin1String(loadTypeId(t)))
        {
            out = t;
            return true;
        }
    return false;
}

bool methodFromId(const QString& id, N::SolverMethod& out)
{
    for (auto m : { N::SolverMethod::GaussLU, N::SolverMethod::Cholesky, N::SolverMethod::ConjugateGradient })
        if (id == QLatin1String(N::solverMethodId(m)))
        {
            out = m;
            return true;
        }
    return false;
}

// ---------------------------------------------------------------------------------------------
// Écriture
// ---------------------------------------------------------------------------------------------

QJsonValue num(double v)
{
    if (std::isfinite(v)) return v;
    return QString::fromLatin1(std::isnan(v) ? "nan" : v > 0 ? "inf" : "-inf");
}

QJsonArray vec(const N::Vector& v)
{
    QJsonArray a;
    for (double x : v) a.append(num(x));
    return a;
}

QJsonObject mat(const N::DenseMatrix& m)
{
    return { { "rows", m.rows }, { "cols", m.cols }, { "data", vec(m.data) } };
}

QJsonArray ints(const std::vector<int>& v)
{
    QJsonArray a;
    for (int x : v) a.append(x);
    return a;
}

QJsonArray texts(const std::vector<std::string>& v)
{
    QJsonArray a;
    for (const auto& x : v) a.append(QString::fromStdString(x));
    return a;
}

QString str(const std::string& s)
{
    return QString::fromStdString(s);
}

QJsonObject loadCaseJson(const S::LoadCase& c)
{
    QJsonArray nodal, element, imposed;
    for (const auto& l : c.nodalLoads) nodal.append(QJsonObject { { "node", l.node }, { "fx", num(l.fx) }, { "fy", num(l.fy) }, { "mz", num(l.mz) } });
    for (const auto& l : c.elementLoads)
        element.append(QJsonObject { { "element", l.element },
                                     { "type", loadTypeId(l.type) },
                                     { "direction", directionId(l.direction) },
                                     { "projected", l.projected },
                                     { "relative", l.relative },
                                     { "a", num(l.a) },
                                     { "b", num(l.b) },
                                     { "q1", num(l.q1) },
                                     { "q2", num(l.q2) },
                                     { "deltaT", num(l.deltaT) },
                                     { "gradientT", num(l.gradientT) },
                                     { "epsilon0", num(l.epsilon0) },
                                     { "kappa0", num(l.kappa0) },
                                     { "factor", num(l.factor) } });
    for (const auto& d : c.imposedDisplacements)
        imposed.append(QJsonObject { { "node", d.node }, { "component", d.component }, { "value", num(d.value) } });
    return { { "id", c.id }, { "name", str(c.name) }, { "nodalLoads", nodal }, { "elementLoads", element }, { "imposedDisplacements", imposed } };
}

QJsonObject modelJson(const S::Model2D& m)
{
    QJsonArray nodes, elements, supports, cases, combinations, masses, metadata;
    for (const auto& n : m.nodes) nodes.append(QJsonObject { { "id", n.id }, { "x", num(n.x) }, { "y", num(n.y) }, { "name", str(n.name) } });
    for (const auto& e : m.elements)
    {
        const QJsonObject material { { "name", str(e.material.name) },     { "E", num(e.material.E) },
                                     { "alpha", num(e.material.alpha) }, { "unitWeight", num(e.material.unitWeight) },
                                     { "density", num(e.material.density) } };
        const QJsonObject section { { "name", str(e.section.name) }, { "A", num(e.section.A) }, { "Iz", num(e.section.Iz) }, { "h", num(e.section.h) } };
        elements.append(QJsonObject { { "id", e.id },
                                      { "nodeI", e.nodeI },
                                      { "nodeJ", e.nodeJ },
                                      { "name", str(e.name) },
                                      { "releaseI", e.releaseI },
                                      { "releaseJ", e.releaseJ },
                                      { "material", material },
                                      { "section", section } });
    }
    for (const auto& s : m.supports)
        supports.append(QJsonObject { { "node", s.node },       { "fixUx", s.fixUx },     { "fixUy", s.fixUy },     { "fixRz", s.fixRz },
                                      { "kUx", num(s.kUx) }, { "kUy", num(s.kUy) }, { "kRz", num(s.kRz) } });
    for (const auto& c : m.loadCases) cases.append(loadCaseJson(c));
    for (const auto& c : m.combinations)
    {
        QJsonArray factors;
        for (const auto& [id, f] : c.factors) factors.append(QJsonObject { { "case", id }, { "factor", num(f) } });
        combinations.append(QJsonObject { { "id", c.id }, { "name", str(c.name) }, { "factors", factors } });
    }
    for (const auto& n : m.nodalMasses) masses.append(QJsonObject { { "node", n.node }, { "mx", num(n.mx) }, { "my", num(n.my) }, { "jz", num(n.jz) } });
    for (const auto& [k, v] : m.metadata) metadata.append(QJsonObject { { "key", str(k) }, { "value", str(v) } });
    return { { "name", str(m.name) },         { "nodes", nodes },   { "elements", elements },       { "supports", supports },
             { "loadCases", cases },          { "combinations", combinations }, { "nodalMasses", masses }, { "metadata", metadata } };
}

QJsonObject unitsJson()
{
    return { { "force", "kN" }, { "length", "m" }, { "E", "kN/m²" }, { "A", "m²" }, { "Iz", "m⁴" }, { "temperature", "°C" }, { "angle", "rad" },
             { "mass", "t" }, { "density", "t/m³" }, { "unitWeight", "kN/m³" }, { "time", "s" } };
}

QJsonObject reportJson(const N::SolverReport& r)
{
    return { { "success", r.success },
             { "message", str(r.message) },
             { "method", N::solverMethodId(r.method) },
             { "equations", r.equations },
             { "iterations", r.iterations },
             { "residualHistory", vec(r.residualHistory) },
             { "relativeResidual", num(r.relativeResidual) },
             { "minPivot", num(r.minPivot) },
             { "maxPivot", num(r.maxPivot) },
             { "failedEquation", r.failedEquation },
             { "elapsedMs", num(r.elapsedMs) } };
}

QJsonObject diagramJson(const S::InternalForceDiagram& g)
{
    QJsonArray points;
    for (const auto& p : g.points)
        points.append(QJsonObject { { "x", num(p.x) }, { "N", num(p.N) }, { "V", num(p.V) }, { "M", num(p.M) }, { "u", num(p.u) }, { "v", num(p.v) },
                                    { "rotation", num(p.rotation) } });
    return { { "element", g.element },       { "length", num(g.length) },   { "points", points },
             { "Nmin", num(g.Nmin) },         { "Nmax", num(g.Nmax) },       { "Vmin", num(g.Vmin) },
             { "Vmax", num(g.Vmax) },         { "Mmin", num(g.Mmin) },       { "Mmax", num(g.Mmax) },
             { "xMmin", num(g.xMmin) },       { "xMmax", num(g.xMmax) },     { "maxDeflection", num(g.maxDeflection) },
             { "xMaxDeflection", num(g.xMaxDeflection) }, { "closureError", num(g.closureError) } };
}

QJsonObject resultJson(const S::AnalysisResult2D& r)
{
    QJsonArray elements;
    for (const auto& e : r.elements)
        elements.append(QJsonObject { { "element", e.element },          { "dGlobal", vec(e.dGlobal) },     { "dLocal", vec(e.dLocal) },
                                      { "fixedEnd", vec(e.fixedEnd) },   { "endForces", vec(e.endForces) }, { "endForcesGlobal", vec(e.endForcesGlobal) },
                                      { "diagram", diagramJson(e.diagram) } });
    const auto& q = r.equilibrium;
    const QJsonObject equilibrium { { "loadFx", num(q.loadFx) },           { "loadFy", num(q.loadFy) },           { "loadMz", num(q.loadMz) },
                                    { "reactionFx", num(q.reactionFx) },   { "reactionFy", num(q.reactionFy) },   { "reactionMz", num(q.reactionMz) },
                                    { "residualFx", num(q.residualFx) },   { "residualFy", num(q.residualFy) },   { "residualMz", num(q.residualMz) },
                                    { "forceScale", num(q.forceScale) },   { "momentScale", num(q.momentScale) },
                                    { "relativeForceResidual", num(q.relativeForceResidual) },
                                    { "relativeMomentResidual", num(q.relativeMomentResidual) } };
    const auto& s = r.solve;
    const QJsonObject solve { { "success", s.success },
                              { "message", str(s.message) },
                              { "warnings", texts(s.warnings) },
                              { "df", vec(s.df) },
                              { "report", reportJson(s.report) },
                              { "conditionNumber", num(s.conditionNumber) },
                              { "failedDof", str(s.failedDof) },
                              { "mechanismDofs", texts(s.mechanismDofs) },
                              { "mechanismMode", vec(s.mechanismMode) },
                              { "mechanismKind", str(s.mechanismKind) } };
    const auto& rs = r.reduced;
    const QJsonObject reduced { { "Kff", mat(rs.Kff) }, { "Kfc", mat(rs.Kfc) }, { "Kcf", mat(rs.Kcf) }, { "Kcc", mat(rs.Kcc) },
                                { "Ff", vec(rs.Ff) },   { "Fc", vec(rs.Fc) },   { "dc", vec(rs.dc) },   { "rhs", vec(rs.rhs) } };
    const QJsonObject bc { { "freeDofs", ints(r.bc.freeDofs) },
                           { "constrainedDofs", ints(r.bc.constrainedDofs) },
                           { "imposed", vec(r.bc.imposed) },
                           { "eliminatedDofs", ints(r.bc.eliminatedDofs) } };
    return { { "success", r.success },         { "message", str(r.message) },         { "log", texts(r.log) },
             { "title", str(r.title) },        { "model", modelJson(r.model) },       { "loads", loadCaseJson(r.loads) },
             { "dofs", QJsonObject { { "nodeIds", ints(r.dofs.nodeIds) } } },          { "bc", bc },
             { "K", mat(r.K) },                { "F", vec(r.F) },                     { "reduced", reduced },
             { "solve", solve },               { "d", vec(r.d) },                     { "R", vec(r.R) },
             { "springs", vec(r.springs) },    { "elements", elements },              { "equilibrium", equilibrium } };
}

// ---------------------------------------------------------------------------------------------
// Lecture stricte
// ---------------------------------------------------------------------------------------------

class Reader
{
public:
    std::string error;

    bool failed() const { return !error.empty(); }
    void fail(const std::string& path, const std::string& what)
    {
        if (error.empty()) error = path + " : " + what;
    }

    QJsonValue field(const QJsonObject& o, const char* key, const std::string& path)
    {
        if (!o.contains(QLatin1String(key)))
        {
            fail(path + "." + key, "champ manquant");
            return {};
        }
        return o.value(QLatin1String(key));
    }
    double number(const QJsonValue& v, const std::string& path)
    {
        if (v.isDouble()) return v.toDouble();
        if (v.isString())
        {
            const QString s = v.toString();
            if (s == QLatin1String("nan")) return std::numeric_limits<double>::quiet_NaN();
            if (s == QLatin1String("inf")) return std::numeric_limits<double>::infinity();
            if (s == QLatin1String("-inf")) return -std::numeric_limits<double>::infinity();
        }
        fail(path, "nombre attendu");
        return 0.0;
    }
    double real(const QJsonObject& o, const char* key, const std::string& path) { return number(field(o, key, path), path + "." + key); }
    int integer(const QJsonObject& o, const char* key, const std::string& path)
    {
        const QJsonValue v = field(o, key, path);
        if (failed()) return 0;
        const double d = v.toDouble();
        if (!v.isDouble() || d != std::floor(d) || std::abs(d) > 2147483647.0)
        {
            fail(path + "." + key, "entier attendu");
            return 0;
        }
        return static_cast<int>(d);
    }
    bool boolean(const QJsonObject& o, const char* key, const std::string& path)
    {
        const QJsonValue v = field(o, key, path);
        if (!failed() && !v.isBool()) fail(path + "." + key, "booléen attendu");
        return v.toBool();
    }
    std::string text(const QJsonObject& o, const char* key, const std::string& path)
    {
        const QJsonValue v = field(o, key, path);
        if (!failed() && !v.isString()) fail(path + "." + key, "texte attendu");
        return v.toString().toStdString();
    }
    QJsonObject object(const QJsonObject& o, const char* key, const std::string& path)
    {
        const QJsonValue v = field(o, key, path);
        if (!failed() && !v.isObject()) fail(path + "." + key, "objet attendu");
        return v.toObject();
    }
    QJsonArray array(const QJsonObject& o, const char* key, const std::string& path)
    {
        const QJsonValue v = field(o, key, path);
        if (!failed() && !v.isArray()) fail(path + "." + key, "tableau attendu");
        return v.toArray();
    }
    /// Éléments d'un tableau d'objets : f(objet, chemin de l'élément).
    template <typename F>
    void each(const QJsonObject& o, const char* key, const std::string& path, F f)
    {
        const QJsonArray a = array(o, key, path);
        for (int i = 0; i < a.size() && !failed(); ++i)
        {
            const std::string at = path + "." + key + "[" + std::to_string(i) + "]";
            if (!a[i].isObject()) fail(at, "objet attendu");
            else f(a[i].toObject(), at);
        }
    }
    N::Vector vector(const QJsonObject& o, const char* key, const std::string& path)
    {
        const QJsonArray a = array(o, key, path);
        N::Vector v;
        v.reserve(static_cast<std::size_t>(a.size()));
        for (int i = 0; i < a.size() && !failed(); ++i) v.push_back(number(a[i], path + "." + key + "[" + std::to_string(i) + "]"));
        return v;
    }
    std::vector<int> ints(const QJsonObject& o, const char* key, const std::string& path)
    {
        const QJsonArray a = array(o, key, path);
        std::vector<int> v;
        for (int i = 0; i < a.size() && !failed(); ++i)
        {
            const double d = a[i].toDouble();
            if (!a[i].isDouble() || d != std::floor(d)) fail(path + "." + key + "[" + std::to_string(i) + "]", "entier attendu");
            v.push_back(static_cast<int>(d));
        }
        return v;
    }
    std::vector<std::string> texts(const QJsonObject& o, const char* key, const std::string& path)
    {
        const QJsonArray a = array(o, key, path);
        std::vector<std::string> v;
        for (int i = 0; i < a.size() && !failed(); ++i)
        {
            if (!a[i].isString()) fail(path + "." + key + "[" + std::to_string(i) + "]", "texte attendu");
            v.push_back(a[i].toString().toStdString());
        }
        return v;
    }
    N::DenseMatrix matrix(const QJsonObject& o, const char* key, const std::string& path)
    {
        const QJsonObject m = object(o, key, path);
        const std::string at = path + "." + key;
        N::DenseMatrix out;
        out.rows = integer(m, "rows", at);
        out.cols = integer(m, "cols", at);
        out.data = vector(m, "data", at);
        if (!failed() && (out.rows < 0 || out.cols < 0 || out.data.size() != static_cast<std::size_t>(out.rows) * static_cast<std::size_t>(out.cols)))
            fail(at, "dimensions incohérentes (" + std::to_string(out.rows) + " × " + std::to_string(out.cols) + ", " + std::to_string(out.data.size())
                         + " coefficient(s))");
        return out;
    }
};

S::LoadCase readLoadCase(Reader& r, const QJsonObject& o, const std::string& path)
{
    S::LoadCase c;
    c.id = r.integer(o, "id", path);
    c.name = r.text(o, "name", path);
    r.each(o, "nodalLoads", path, [&](const QJsonObject& l, const std::string& at) {
        c.nodalLoads.push_back({ r.integer(l, "node", at), r.real(l, "fx", at), r.real(l, "fy", at), r.real(l, "mz", at) });
    });
    r.each(o, "elementLoads", path, [&](const QJsonObject& l, const std::string& at) {
        S::ElementLoad e;
        e.element = r.integer(l, "element", at);
        const std::string type = r.text(l, "type", at), direction = r.text(l, "direction", at);
        if (!r.failed() && !loadTypeFromId(QString::fromStdString(type), e.type)) r.fail(at + ".type", "type de charge inconnu « " + type + " »");
        if (!r.failed() && !directionFromId(QString::fromStdString(direction), e.direction))
            r.fail(at + ".direction", "direction inconnue « " + direction + " »");
        e.projected = r.boolean(l, "projected", at);
        e.relative = r.boolean(l, "relative", at);
        e.a = r.real(l, "a", at);
        e.b = r.real(l, "b", at);
        e.q1 = r.real(l, "q1", at);
        e.q2 = r.real(l, "q2", at);
        e.deltaT = r.real(l, "deltaT", at);
        e.gradientT = r.real(l, "gradientT", at);
        e.epsilon0 = r.real(l, "epsilon0", at);
        e.kappa0 = r.real(l, "kappa0", at);
        e.factor = r.real(l, "factor", at);
        c.elementLoads.push_back(e);
    });
    r.each(o, "imposedDisplacements", path, [&](const QJsonObject& l, const std::string& at) {
        c.imposedDisplacements.push_back({ r.integer(l, "node", at), r.integer(l, "component", at), r.real(l, "value", at) });
    });
    return c;
}

S::Model2D readModel(Reader& r, const QJsonObject& o, const std::string& path)
{
    S::Model2D m;
    m.name = r.text(o, "name", path);
    r.each(o, "nodes", path, [&](const QJsonObject& n, const std::string& at) {
        m.nodes.push_back({ r.integer(n, "id", at), r.real(n, "x", at), r.real(n, "y", at), r.text(n, "name", at) });
    });
    r.each(o, "elements", path, [&](const QJsonObject& e, const std::string& at) {
        S::ElementDefinition d;
        d.id = r.integer(e, "id", at);
        d.nodeI = r.integer(e, "nodeI", at);
        d.nodeJ = r.integer(e, "nodeJ", at);
        d.name = r.text(e, "name", at);
        d.releaseI = r.boolean(e, "releaseI", at);
        d.releaseJ = r.boolean(e, "releaseJ", at);
        const QJsonObject mat = r.object(e, "material", at), sec = r.object(e, "section", at);
        const std::string ma = at + ".material", se = at + ".section";
        d.material = { r.text(mat, "name", ma), r.real(mat, "E", ma), r.real(mat, "alpha", ma), r.real(mat, "unitWeight", ma), r.real(mat, "density", ma) };
        d.section = { r.text(sec, "name", se), r.real(sec, "A", se), r.real(sec, "Iz", se), r.real(sec, "h", se) };
        m.elements.push_back(d);
    });
    r.each(o, "supports", path, [&](const QJsonObject& s, const std::string& at) {
        S::SupportCondition c;
        c.node = r.integer(s, "node", at);
        c.fixUx = r.boolean(s, "fixUx", at);
        c.fixUy = r.boolean(s, "fixUy", at);
        c.fixRz = r.boolean(s, "fixRz", at);
        c.kUx = r.real(s, "kUx", at);
        c.kUy = r.real(s, "kUy", at);
        c.kRz = r.real(s, "kRz", at);
        m.supports.push_back(c);
    });
    r.each(o, "loadCases", path, [&](const QJsonObject& c, const std::string& at) { m.loadCases.push_back(readLoadCase(r, c, at)); });
    r.each(o, "combinations", path, [&](const QJsonObject& c, const std::string& at) {
        S::LoadCombination k;
        k.id = r.integer(c, "id", at);
        k.name = r.text(c, "name", at);
        r.each(c, "factors", at, [&](const QJsonObject& f, const std::string& fat) { k.factors.emplace_back(r.integer(f, "case", fat), r.real(f, "factor", fat)); });
        m.combinations.push_back(k);
    });
    r.each(o, "nodalMasses", path, [&](const QJsonObject& n, const std::string& at) {
        m.nodalMasses.push_back({ r.integer(n, "node", at), r.real(n, "mx", at), r.real(n, "my", at), r.real(n, "jz", at) });
    });
    r.each(o, "metadata", path, [&](const QJsonObject& k, const std::string& at) { m.metadata.emplace_back(r.text(k, "key", at), r.text(k, "value", at)); });
    return m;
}

S::InternalForceDiagram readDiagram(Reader& r, const QJsonObject& g, const std::string& at)
{
    S::InternalForceDiagram d;
    d.element = r.integer(g, "element", at);
    d.length = r.real(g, "length", at);
    r.each(g, "points", at, [&](const QJsonObject& p, const std::string& pat) {
        d.points.push_back({ r.real(p, "x", pat), r.real(p, "N", pat), r.real(p, "V", pat), r.real(p, "M", pat), r.real(p, "u", pat), r.real(p, "v", pat),
                             r.real(p, "rotation", pat) });
    });
    d.Nmin = r.real(g, "Nmin", at), d.Nmax = r.real(g, "Nmax", at);
    d.Vmin = r.real(g, "Vmin", at), d.Vmax = r.real(g, "Vmax", at);
    d.Mmin = r.real(g, "Mmin", at), d.Mmax = r.real(g, "Mmax", at);
    d.xMmin = r.real(g, "xMmin", at), d.xMmax = r.real(g, "xMmax", at);
    d.maxDeflection = r.real(g, "maxDeflection", at), d.xMaxDeflection = r.real(g, "xMaxDeflection", at);
    d.closureError = r.real(g, "closureError", at);
    return d;
}

S::AnalysisResult2D readResult(Reader& r, const QJsonObject& o, const std::string& path)
{
    S::AnalysisResult2D res;
    res.success = r.boolean(o, "success", path);
    res.message = r.text(o, "message", path);
    res.log = r.texts(o, "log", path);
    res.title = r.text(o, "title", path);
    res.model = readModel(r, r.object(o, "model", path), path + ".model");
    res.loads = readLoadCase(r, r.object(o, "loads", path), path + ".loads");
    res.dofs.nodeIds = r.ints(r.object(o, "dofs", path), "nodeIds", path + ".dofs");
    const QJsonObject bc = r.object(o, "bc", path);
    res.bc.freeDofs = r.ints(bc, "freeDofs", path + ".bc");
    res.bc.constrainedDofs = r.ints(bc, "constrainedDofs", path + ".bc");
    res.bc.imposed = r.vector(bc, "imposed", path + ".bc");
    res.bc.eliminatedDofs = r.ints(bc, "eliminatedDofs", path + ".bc");
    res.K = r.matrix(o, "K", path);
    res.F = r.vector(o, "F", path);
    const QJsonObject red = r.object(o, "reduced", path);
    const std::string rp = path + ".reduced";
    res.reduced.Kff = r.matrix(red, "Kff", rp);
    res.reduced.Kfc = r.matrix(red, "Kfc", rp);
    res.reduced.Kcf = r.matrix(red, "Kcf", rp);
    res.reduced.Kcc = r.matrix(red, "Kcc", rp);
    res.reduced.Ff = r.vector(red, "Ff", rp);
    res.reduced.Fc = r.vector(red, "Fc", rp);
    res.reduced.dc = r.vector(red, "dc", rp);
    res.reduced.rhs = r.vector(red, "rhs", rp);
    const QJsonObject solve = r.object(o, "solve", path);
    const std::string sp = path + ".solve";
    res.solve.success = r.boolean(solve, "success", sp);
    res.solve.message = r.text(solve, "message", sp);
    res.solve.warnings = r.texts(solve, "warnings", sp);
    res.solve.df = r.vector(solve, "df", sp);
    const QJsonObject rep = r.object(solve, "report", sp);
    const std::string rpp = sp + ".report";
    auto& sr = res.solve.report;
    sr.success = r.boolean(rep, "success", rpp);
    sr.message = r.text(rep, "message", rpp);
    const std::string method = r.text(rep, "method", rpp);
    if (!r.failed() && !methodFromId(QString::fromStdString(method), sr.method)) r.fail(rpp + ".method", "méthode de résolution inconnue « " + method + " »");
    sr.equations = r.integer(rep, "equations", rpp);
    sr.iterations = r.integer(rep, "iterations", rpp);
    sr.residualHistory = r.vector(rep, "residualHistory", rpp);
    sr.relativeResidual = r.real(rep, "relativeResidual", rpp);
    sr.minPivot = r.real(rep, "minPivot", rpp);
    sr.maxPivot = r.real(rep, "maxPivot", rpp);
    sr.failedEquation = r.integer(rep, "failedEquation", rpp);
    sr.elapsedMs = r.real(rep, "elapsedMs", rpp);
    res.solve.conditionNumber = r.real(solve, "conditionNumber", sp);
    res.solve.failedDof = r.text(solve, "failedDof", sp);
    res.solve.mechanismDofs = r.texts(solve, "mechanismDofs", sp);
    res.solve.mechanismMode = r.vector(solve, "mechanismMode", sp);
    res.solve.mechanismKind = r.text(solve, "mechanismKind", sp);
    res.d = r.vector(o, "d", path);
    res.R = r.vector(o, "R", path);
    res.springs = r.vector(o, "springs", path);
    r.each(o, "elements", path, [&](const QJsonObject& e, const std::string& at) {
        S::ElementResult er;
        er.element = r.integer(e, "element", at);
        er.dGlobal = r.vector(e, "dGlobal", at);
        er.dLocal = r.vector(e, "dLocal", at);
        er.fixedEnd = r.vector(e, "fixedEnd", at);
        er.endForces = r.vector(e, "endForces", at);
        er.endForcesGlobal = r.vector(e, "endForcesGlobal", at);
        er.diagram = readDiagram(r, r.object(e, "diagram", at), at + ".diagram");
        res.elements.push_back(std::move(er));
    });
    const QJsonObject q = r.object(o, "equilibrium", path);
    const std::string qp = path + ".equilibrium";
    auto& eq = res.equilibrium;
    eq.loadFx = r.real(q, "loadFx", qp), eq.loadFy = r.real(q, "loadFy", qp), eq.loadMz = r.real(q, "loadMz", qp);
    eq.reactionFx = r.real(q, "reactionFx", qp), eq.reactionFy = r.real(q, "reactionFy", qp), eq.reactionMz = r.real(q, "reactionMz", qp);
    eq.residualFx = r.real(q, "residualFx", qp), eq.residualFy = r.real(q, "residualFy", qp), eq.residualMz = r.real(q, "residualMz", qp);
    eq.forceScale = r.real(q, "forceScale", qp), eq.momentScale = r.real(q, "momentScale", qp);
    eq.relativeForceResidual = r.real(q, "relativeForceResidual", qp);
    eq.relativeMomentResidual = r.real(q, "relativeMomentResidual", qp);
    return res;
}

/// Document versionné : objet racine, format attendu, version 1 à `maxVersion` ; contenu sous la clé `key`.
bool openDocument(const QByteArray& json, const char* format, int maxVersion, const char* key, QJsonObject& content, std::string* error)
{
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &pe);
    auto fail = [error](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    if (doc.isNull()) return fail("JSON illisible : " + pe.errorString().toStdString() + " (position " + std::to_string(pe.offset) + ")");
    const QJsonObject root = doc.object();
    if (!doc.isObject() || root.value(QLatin1String("format")).toString() != QLatin1String(format))
        return fail(std::string("ce fichier n'est pas au format « ") + format + " »");
    const QJsonValue v = root.value(QLatin1String("version"));
    const int version = v.toInt(0);
    if (!v.isDouble() || version < 1) return fail("version du format absente ou invalide");
    if (version > maxVersion)
        return fail("fichier de version " + std::to_string(version) + ", plus récente que celle prise en charge (" + std::to_string(maxVersion) + ")");
    if (!root.value(QLatin1String(key)).isObject()) return fail(std::string(key) + " : objet attendu");
    content = root.value(QLatin1String(key)).toObject();
    return true;
}

bool writeFile(const std::string& path, const QByteArray& data, std::string* error)
{
    QSaveFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::WriteOnly) || f.write(data) != data.size() || !f.commit())
    {
        if (error) *error = "écriture impossible de « " + path + " » : " + f.errorString().toStdString();
        return false;
    }
    return true;
}

bool readFile(const std::string& path, QByteArray& data, std::string* error)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
    {
        if (error) *error = "lecture impossible de « " + path + " » : " + f.errorString().toStdString();
        return false;
    }
    data = f.readAll();
    return true;
}
} // namespace

QByteArray modelToJson(const S::Model2D& model)
{
    const QJsonObject root { { "format", kModelFormat }, { "version", kModelJsonVersion }, { "units", unitsJson() }, { "model", modelJson(model) } };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool modelFromJson(const QByteArray& json, S::Model2D& model, std::string* error)
{
    QJsonObject content;
    if (!openDocument(json, kModelFormat, kModelJsonVersion, "model", content, error)) return false;
    Reader r;
    S::Model2D m = readModel(r, content, "model");
    if (r.failed())
    {
        if (error) *error = r.error;
        return false;
    }
    model = std::move(m);
    return true;
}

QByteArray resultToJson(const S::AnalysisResult2D& result)
{
    const QJsonObject root { { "format", kResultFormat }, { "version", kResultJsonVersion }, { "units", unitsJson() }, { "result", resultJson(result) } };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

bool resultFromJson(const QByteArray& json, S::AnalysisResult2D& result, std::string* error)
{
    QJsonObject content;
    if (!openDocument(json, kResultFormat, kResultJsonVersion, "result", content, error)) return false;
    Reader r;
    S::AnalysisResult2D res = readResult(r, content, "result");
    if (r.failed())
    {
        if (error) *error = r.error;
        return false;
    }
    result = std::move(res);
    return true;
}

bool saveModelJson(const S::Model2D& model, const std::string& path, std::string* error)
{
    return writeFile(path, modelToJson(model), error);
}

bool loadModelJson(const std::string& path, S::Model2D& model, std::string* error)
{
    QByteArray data;
    return readFile(path, data, error) && modelFromJson(data, model, error);
}

bool saveResultJson(const S::AnalysisResult2D& result, const std::string& path, std::string* error)
{
    return writeFile(path, resultToJson(result), error);
}

bool loadResultJson(const std::string& path, S::AnalysisResult2D& result, std::string* error)
{
    QByteArray data;
    return readFile(path, data, error) && resultFromJson(data, result, error);
}

} // namespace TSALab::Research::Structural
