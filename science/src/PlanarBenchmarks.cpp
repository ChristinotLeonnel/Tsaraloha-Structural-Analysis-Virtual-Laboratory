#include "tsalab/validation/PlanarBenchmarks.h"

#include "tsalab/numerics/SolverComparison.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <limits>
#include <sstream>

namespace tsalab::validation
{

using namespace tsalab::planar;

namespace
{
// Profilé de type IPE 300 en acier : E = 210 GPa (kPa), A (m²), I (m⁴).
constexpr double kE = 210.0e6;
constexpr double kA = 5.381e-3;
constexpr double kI = 8.356e-5;
constexpr double kEI = kE * kI;

Node node(int index, double x, double y, bool fx, bool fy, bool frz)
{
    Node n;
    n.index = index;
    n.x = x;
    n.y = y;
    n.fixX = fx;
    n.fixY = fy;
    n.fixRz = frz;
    return n;
}

Element bar(int index, int i, int j, double length, ElementType type = ElementType::Frame)
{
    Element e;
    e.index = index;
    e.nodeI = i;
    e.nodeJ = j;
    e.type = type;
    e.E = kE;
    e.A = kA;
    e.I = type == ElementType::Frame ? kI : 0.0;
    e.length = length;
    return e;
}

MemberLoad uniform(int element, double length, double qy)
{
    MemberLoad l;
    l.loadCaseId = 1;
    l.element = element;
    l.kind = MemberLoadKind::Distributed;
    l.a = 0.0;
    l.b = length;
    l.py1 = l.py2 = qy;
    return l;
}

Input base()
{
    Input in;
    in.loadCases.push_back({ 1, "Charge", false, 1.0 });
    in.request.includeSelfWeight = false;
    return in;
}

const NodeDisplacement* displacement(const Output& o, int nodeIndex)
{
    for (const auto& d : o.displacements)
        if (d.node == nodeIndex) return &d;
    return nullptr;
}

const NodeReaction* reaction(const Output& o, int nodeIndex)
{
    for (const auto& r : o.reactions)
        if (r.node == nodeIndex) return &r;
    return nullptr;
}

const ElementForces* forces(const Output& o, int element)
{
    for (const auto& f : o.elementForces)
        if (f.element == element) return &f;
    return nullptr;
}

// Accesseurs de grandeurs (NaN si absente : le contrôle échoue explicitement).
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
auto uy(int n) { return [n](const Output& o) { const auto* d = displacement(o, n); return d ? d->uy : kNaN; }; }
auto rz(int n) { return [n](const Output& o) { const auto* d = displacement(o, n); return d ? d->rz : kNaN; }; }
auto Ry(int n) { return [n](const Output& o) { const auto* r = reaction(o, n); return r ? r->fy : kNaN; }; }
auto Mz(int n) { return [n](const Output& o) { const auto* r = reaction(o, n); return r ? r->mz : kNaN; }; }
template <typename F>
auto summary(int e, F field)
{
    return [e, field](const Output& o) {
        const auto* f = forces(o, e);
        return f && f->hasSummary ? field(f->summary) : kNaN;
    };
}

std::vector<PlanarBenchmark> buildCatalog()
{
    std::vector<PlanarBenchmark> list;
    const double q = 10.0;

    {   // Console, force en bout
        const double L = 4.0, P = 10.0;
        PlanarBenchmark b { "cantilever-point", "Console — force en bout",
                            "Console de 4 m encastrée à gauche, P = 10 kN vers le bas à l'extrémité libre.", base(), {} };
        b.input.nodes = { node(1, 0, 0, true, true, true), node(2, L, 0, false, false, false) };
        b.input.elements = { bar(1, 1, 2, L) };
        b.input.nodalLoads = { { 1, 2, 0.0, -P, 0.0 } };
        b.checks = {
            { "flèche en bout", "−PL³/3EI", -P * L * L * L / (3 * kEI), uy(2) },
            { "rotation en bout", "−PL²/2EI", -P * L * L / (2 * kEI), rz(2) },
            { "réaction verticale", "P", P, Ry(1) },
            { "moment d'encastrement", "PL", P * L, Mz(1) },
        };
        list.push_back(std::move(b));
    }
    {   // Poutre sur deux appuis, charge uniforme
        const double L = 6.0;
        PlanarBenchmark b { "simply-supported-udl", "Poutre sur deux appuis — charge uniforme",
                            "Portée 6 m, articulation à gauche, appui simple à droite, q = 10 kN/m.", base(), {} };
        b.input.nodes = { node(1, 0, 0, true, true, false), node(2, L, 0, false, true, false) };
        b.input.elements = { bar(1, 1, 2, L) };
        b.input.memberLoads = { uniform(1, L, -q) };
        b.checks = {
            { "réaction gauche", "qL/2", q * L / 2, Ry(1) },
            { "réaction droite", "qL/2", q * L / 2, Ry(2) },
            { "rotation d'appui", "−qL³/24EI", -q * L * L * L / (24 * kEI), rz(1) },
            { "moment en travée", "qL²/8", q * L * L / 8, summary(1, [](const MemberSummary& s) { return s.MSpanExtremum; }) },
            { "flèche maximale", "−5qL⁴/384EI", -5 * q * std::pow(L, 4) / (384 * kEI),
              summary(1, [](const MemberSummary& s) { return s.deflectionMax; }) },
        };
        list.push_back(std::move(b));
    }
    {   // Poutre bi-encastrée (nœud à mi-portée : une barre unique encastrée aux deux bouts n'aurait aucun
        // DDL libre, cas refusé par MetDeDeplacement — limite connue, voir .claude/known-issues.md)
        const double L = 6.0;
        PlanarBenchmark b { "fixed-fixed-udl", "Poutre bi-encastrée — charge uniforme",
                            "Portée 6 m encastrée aux deux extrémités (nœud à mi-portée), q = 10 kN/m.", base(), {} };
        b.input.nodes = { node(1, 0, 0, true, true, true), node(2, L / 2, 0, false, false, false), node(3, L, 0, true, true, true) };
        b.input.elements = { bar(1, 1, 2, L / 2), bar(2, 2, 3, L / 2) };
        b.input.memberLoads = { uniform(1, L / 2, -q), uniform(2, L / 2, -q) };
        b.checks = {
            { "moment sur appui", "−qL²/12", -q * L * L / 12, summary(1, [](const MemberSummary& s) { return s.Mi; }) },
            { "moment à mi-portée", "qL²/24", q * L * L / 24, summary(1, [](const MemberSummary& s) { return s.Mj; }) },
            { "moment d'encastrement", "qL²/12", q * L * L / 12, Mz(1) },
            { "flèche à mi-portée", "−qL⁴/384EI", -q * std::pow(L, 4) / (384 * kEI), uy(2) },
        };
        list.push_back(std::move(b));
    }
    {   // Encastrée – appuyée
        const double L = 6.0;
        PlanarBenchmark b { "propped-cantilever-udl", "Poutre encastrée-appuyée — charge uniforme",
                            "Portée 6 m encastrée à gauche, appui simple à droite, q = 10 kN/m.", base(), {} };
        b.input.nodes = { node(1, 0, 0, true, true, true), node(2, L, 0, false, true, false) };
        b.input.elements = { bar(1, 1, 2, L) };
        b.input.memberLoads = { uniform(1, L, -q) };
        b.checks = {
            { "réaction appui simple", "3qL/8", 3 * q * L / 8, Ry(2) },
            { "moment d'encastrement", "−qL²/8", -q * L * L / 8, summary(1, [](const MemberSummary& s) { return s.Mi; }) },
            { "moment en travée", "9qL²/128", 9 * q * L * L / 128, summary(1, [](const MemberSummary& s) { return s.MSpanExtremum; }) },
        };
        list.push_back(std::move(b));
    }
    {   // Poutre continue à deux travées égales
        const double L = 5.0;
        PlanarBenchmark b { "two-span-continuous", "Poutre continue — deux travées égales",
                            "Deux travées de 5 m sur trois appuis, q = 10 kN/m sur toute la longueur.", base(), {} };
        b.input.nodes = { node(1, 0, 0, true, true, false), node(2, L, 0, false, true, false), node(3, 2 * L, 0, false, true, false) };
        b.input.elements = { bar(1, 1, 2, L), bar(2, 2, 3, L) };
        b.input.memberLoads = { uniform(1, L, -q), uniform(2, L, -q) };
        b.checks = {
            { "réaction appui central", "5qL/4", 5 * q * L / 4, Ry(2) },
            { "réaction appui de rive", "3qL/8", 3 * q * L / 8, Ry(1) },
            { "moment sur appui central", "−qL²/8", -q * L * L / 8, summary(1, [](const MemberSummary& s) { return s.Mj; }) },
        };
        list.push_back(std::move(b));
    }
    {   // Treillis triangulaire isostatique
        const double P = 10.0, w = 4.0, h = 3.0;
        const double Li = std::hypot(w / 2, h);
        const double sinT = h / Li, tanT = h / (w / 2);
        PlanarBenchmark b { "truss-triangle", "Treillis triangulaire — charge au sommet",
                            "Base 4 m, hauteur 3 m, P = 10 kN vers le bas au sommet (barres articulées).", base(), {} };
        b.input.nodes = { node(1, 0, 0, true, true, false), node(2, w, 0, false, true, false), node(3, w / 2, h, false, false, false) };
        b.input.elements = { bar(1, 1, 3, Li, ElementType::Truss), bar(2, 3, 2, Li, ElementType::Truss),
                             bar(3, 1, 2, w, ElementType::Truss) };
        b.input.nodalLoads = { { 1, 3, 0.0, -P, 0.0 } };
        b.checks = {
            { "effort dans une diagonale", "−P/(2 sin θ)", -P / (2 * sinT), summary(1, [](const MemberSummary& s) { return s.Nmax; }) },
            { "effort dans le tirant", "P/(2 tan θ)", P / (2 * tanT), summary(3, [](const MemberSummary& s) { return s.Nmax; }) },
            { "réaction d'appui", "P/2", P / 2, Ry(1) },
        };
        list.push_back(std::move(b));
    }
    return list;
}
} // namespace

const std::vector<PlanarBenchmark>& planarBenchmarks()
{
    static const std::vector<PlanarBenchmark> catalog = buildCatalog();
    return catalog;
}

BenchmarkReport runBenchmark(const PlanarBenchmark& benchmark, ISolver& solver)
{
    BenchmarkReport rep;
    rep.id = benchmark.id;
    rep.title = benchmark.title;
    rep.solver = solver.name();

    Input input = benchmark.input;
    input.options.exportSystem = solver.features().linearSystem;
    const Output out = solver.solve(input);
    rep.solved = out.success;
    rep.message = out.message;
    if (!out.success) return rep;

    bool allPassed = true;
    for (const auto& c : benchmark.checks)
    {
        CheckResult r;
        r.label = c.label;
        r.formula = c.formula;
        r.expected = c.expected;
        r.computed = c.computed(out);
        const double scale = std::max(std::abs(c.expected), 1e-12);
        r.relativeError = std::isfinite(r.computed) ? std::abs(r.computed - c.expected) / scale : 1.0;
        r.passed = std::isfinite(r.computed) && r.relativeError <= c.relativeTolerance;
        allPassed &= r.passed;
        rep.checks.push_back(r);
    }

    // Validation croisée : système exporté résolu par le laboratoire (Cholesky).
    if (out.system.available && out.system.equations > 0)
    {
        numerics::LinearProblem p;
        p.K = numerics::Matrix(out.system.equations, out.system.equations);
        for (const auto& e : out.system.stiffness) p.K(e.row, e.col) = e.value;
        p.f = out.system.loads;
        p.reference = out.system.displacements;
        const auto run = numerics::runSolver(p, numerics::SolverMethod::Cholesky);
        rep.crossChecked = run.report.success;
        rep.crossDeviation = run.deviationFromReference;
        allPassed &= rep.crossChecked && rep.crossDeviation >= 0.0 && rep.crossDeviation < 1e-8;
    }
    rep.passed = allPassed;
    return rep;
}

std::string formatReport(const std::vector<BenchmarkReport>& reports)
{
    std::ostringstream o;
    o << std::setprecision(6);
    int ok = 0;
    for (const auto& r : reports)
    {
        o << (r.passed ? "[OK]   " : "[ÉCHEC] ") << r.title << " — " << r.solver << "\n";
        if (!r.solved)
        {
            o << "         calcul impossible : " << r.message << "\n";
            continue;
        }
        for (const auto& c : r.checks)
            o << "         " << (c.passed ? "✓ " : "✗ ") << c.label << " (" << c.formula << ") : calculé " << c.computed
              << ", attendu " << c.expected << ", écart relatif " << std::scientific << c.relativeError
              << std::defaultfloat << "\n";
        if (r.crossChecked)
            o << "         validation croisée K·U = F (Cholesky du laboratoire) : écart " << std::scientific
              << r.crossDeviation << std::defaultfloat << "\n";
        ok += r.passed ? 1 : 0;
    }
    o << ok << " / " << reports.size() << " benchmark(s) validé(s)\n";
    return o.str();
}

} // namespace tsalab::validation
