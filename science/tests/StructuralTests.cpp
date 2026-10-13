// Tests de la programmation structurelle (tsalab::structural) : élément FrameElement2D à 6 DDL, charges,
// méthode des déplacements pas à pas, diagnostic des mécanismes.
// Références : solutions analytiques de la RDM (Euler-Bernoulli), solution fermée du portique encastré,
// et MetDeDeplacement (implémentation indépendante du même problème) pour la validation croisée.
// Tolérances explicites : 1e-9 en relatif pour les cas exacts de la formulation (éléments prismatiques,
// efforts d'encastrement exacts), 1e-6 pour le portique comparé à la solution à barres inextensibles.
#include "tsalab/structural/DirectStiffness2D.h"
#include "mdd/MetDeDeplacement.h"

#include <cmath>
#include <iostream>
#include <map>
#include <string>

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << #cond << ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

#define CHECK_REL(value, expected, tol, msg) \
    do { \
        const double v_ = (value), e_ = (expected); \
        if (!(std::abs(v_ - e_) <= (tol) * std::max(std::abs(e_), 1e-300))) { \
            std::cerr << "[FAIL] " << msg << " : " << v_ << " au lieu de " << e_ << " (écart relatif " \
                      << std::abs(v_ - e_) / std::max(std::abs(e_), 1e-300) << ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

#define CHECK_ABS(value, expected, tol, msg) \
    do { \
        const double v_ = (value), e_ = (expected); \
        if (!(std::abs(v_ - e_) <= (tol))) { \
            std::cerr << "[FAIL] " << msg << " : " << v_ << " au lieu de " << e_ << " at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace tsalab::structural;
namespace numerics = tsalab::numerics;

namespace
{

constexpr double kExact = 1e-9;   // cas exacts de la formulation
const double kPi = std::acos(-1.0);

Material steel() { return { "S235", 210e6, 1.2e-5, 78.5 }; }
Section2D ipe300() { return { "IPE 300", 53.8e-4, 8356e-8, 0.30 }; }
Section2D heb200() { return { "HEB 200", 78.1e-4, 5696e-8, 0.20 }; }
const double EI = 210e6 * 8356e-8;    // IPE 300 : 17 547,6 kN·m²
const double EA = 210e6 * 53.8e-4;    // IPE 300 : 1 129 800 kN

struct Builder
{
    Model2D m;
    int node(double x, double y)
    {
        const int id = static_cast<int>(m.nodes.size()) + 1;
        m.nodes.push_back({ id, x, y, {} });
        return id;
    }
    int element(int i, int j, Section2D s = ipe300(), Material mat = steel())
    {
        const int id = static_cast<int>(m.elements.size()) + 1;
        m.elements.push_back({ id, i, j, mat, s, {} });
        return id;
    }
    void support(int n, bool x, bool y, bool r) { m.supports.push_back({ n, x, y, r }); }
    LoadCase& lc(int id = 1)
    {
        for (auto& c : m.loadCases)
            if (c.id == id) return c;
        m.loadCases.push_back({ id, "cas " + std::to_string(id), {}, {}, {} });
        return m.loadCases.back();
    }
};

AnalysisResult2D run(const Model2D& m, int caseId = 1, int points = 61)
{
    SolveOptions o;
    o.diagramPoints = points;
    return solveLinearStatic(m, { caseId, 0 }, o);
}

/// Point du diagramme en x (premier, ou dernier si `right` : à droite d'une discontinuité).
const DiagramPoint* at(const InternalForceDiagram& d, double x, bool right = false)
{
    const DiagramPoint* found = nullptr;
    for (const auto& p : d.points)
        if (std::abs(p.x - x) <= 1e-9 * std::max(1.0, d.length))
        {
            if (!right) return &p;
            found = &p;
        }
    return found;
}

/// Contrôles communs à tout calcul réussi : équilibre global, cohérence des diagrammes.
bool commonChecks(const AnalysisResult2D& r, const char* what)
{
    TEST_CHECK(r.success, what << " : calcul réussi — " << r.message);
    TEST_CHECK(r.equilibrium.balanced(1e-9), what << " : équilibre global (" << r.equilibrium.text() << ")");
    for (const auto& e : r.elements)
        TEST_CHECK(e.diagram.closureError <= 1e-9 * std::max(1.0, e.diagram.length),
                   what << " : déformée cohérente avec les déplacements de l'élément " << e.element << " (écart " << e.diagram.closureError << ")");
    return true;
}

/// Simplement appuyée horizontale (appui double en 1, appui simple en dernier nœud), n éléments égaux.
Builder simplySupported(double L, int n = 1)
{
    Builder b;
    for (int k = 0; k <= n; ++k) b.node(L * k / n, 0.0);
    for (int k = 1; k <= n; ++k) b.element(k, k + 1);
    b.support(1, true, true, false);
    b.support(n + 1, false, true, false);
    return b;
}

// --- MetDeDeplacement : implémentation indépendante ----------------------------------------------

bool toMdd(const Model2D& m, const LoadCase& loads, mdd::Model& out)
{
    std::map<int, int> index;
    for (const auto& n : m.nodes)
    {
        index[n.id] = static_cast<int>(out.nodes.size());
        mdd::Node x;
        x.x = n.x, x.y = n.y;
        if (const auto* s = m.support(n.id)) x.fixX = s->fixUx, x.fixY = s->fixUy, x.fixRz = s->fixRz;
        out.nodes.push_back(x);
    }
    for (const auto& e : m.elements)
    {
        mdd::Member b;
        b.i = index[e.nodeI], b.j = index[e.nodeJ];
        b.E = e.material.E, b.A = e.section.A, b.I = e.section.Iz;
        out.members.push_back(b);
    }
    for (const auto& p : loads.nodalLoads) out.nodalLoads.push_back({ index[p.node], p.fx, p.fy, p.mz });
    for (std::size_t k = 0; k < m.elements.size(); ++k)
    {
        FrameElement2D el;
        if (!makeElement(m, m.elements[k], el)) return false;
        for (const auto& l : el.localLoads(loads.elementLoads))
        {
            if (l.kind == LocalLoad::Kind::Distributed)
                out.distributedLoads.push_back({ static_cast<int>(k), l.a, l.b, l.px1, l.py1, l.px2, l.py2 });
            else if (l.kind == LocalLoad::Kind::Force)
                out.pointLoads.push_back({ static_cast<int>(k), l.a, l.px1, l.py1 });
            else
                return false;   // moment concentré, déformation initiale : hors du périmètre de MetDeDeplacement
        }
    }
    return true;
}

bool compareWithMdd(const Model2D& m, int caseId, const char* what)
{
    const auto r = run(m, caseId);
    TEST_CHECK(commonChecks(r, what), what);
    mdd::Model x;
    TEST_CHECK(toMdd(m, r.loads, x), what << " : modèle convertible pour MetDeDeplacement");
    const auto ref = mdd::solve(x);
    TEST_CHECK(ref.success, what << " : MetDeDeplacement — " << ref.message);
    double dScale = 0.0, rScale = 0.0, fScale = 0.0, dDiff = 0.0, rDiff = 0.0, fDiff = 0.0;
    for (std::size_t k = 0; k < m.nodes.size(); ++k)
    {
        const auto d = r.nodeDisplacement(m.nodes[k].id);
        const auto rr = r.nodeReaction(m.nodes[k].id);
        for (int c = 0; c < 3; ++c)
        {
            dScale = std::max(dScale, std::abs(ref.displacements[k][static_cast<std::size_t>(c)]));
            dDiff = std::max(dDiff, std::abs(d[static_cast<std::size_t>(c)] - ref.displacements[k][static_cast<std::size_t>(c)]));
            rScale = std::max(rScale, std::abs(ref.reactions[k][static_cast<std::size_t>(c)]));
            rDiff = std::max(rDiff, std::abs(rr[static_cast<std::size_t>(c)] - ref.reactions[k][static_cast<std::size_t>(c)]));
        }
    }
    for (std::size_t k = 0; k < m.elements.size(); ++k)
    {
        const auto& e = ref.members[k].end;
        const double mf[6] = { e.fxI, e.fyI, e.mzI, e.fxJ, e.fyJ, e.mzJ };
        for (int c = 0; c < 6; ++c)
        {
            fScale = std::max(fScale, std::abs(mf[c]));
            fDiff = std::max(fDiff, std::abs(r.elements[k].endForces[static_cast<std::size_t>(c)] - mf[c]));
        }
    }
    TEST_CHECK(dDiff <= kExact * dScale && rDiff <= kExact * rScale && fDiff <= kExact * fScale,
               what << " : écarts à MetDeDeplacement " << dDiff / dScale << " (déplacements), " << rDiff / rScale << " (réactions), "
                    << fDiff / fScale << " (efforts)");
    return true;
}

// =================================================================================================
// S7 — classe FrameElement2D : matrices, transformation, charges équivalentes, validation
// =================================================================================================
bool testElement(int& passed)
{
    using numerics::LinAlg::multiply;
    using numerics::LinAlg::transpose;
    const Node2D a { 1, 1.0, 2.0, {} }, b { 2, 4.0, 6.0, {} };   // L = 5, c = 0,6, s = 0,8
    const FrameElement2D el(7, a, b, steel(), ipe300());
    TEST_CHECK(el.isValid(), "S7: élément valide");
    CHECK_REL(el.length(), 5.0, kExact, "S7: longueur");
    CHECK_REL(el.cosine(), 0.6, kExact, "S7: cosinus");
    CHECK_REL(el.sine(), 0.8, kExact, "S7: sinus");
    CHECK_REL(el.angle(), std::atan2(4.0, 3.0), kExact, "S7: angle");
    const auto k = el.localStiffness();
    CHECK_REL(k(0, 0), EA / 5.0, kExact, "S7: k'(1,1) = EA/L");
    CHECK_REL(k(1, 1), 12.0 * EI / 125.0, kExact, "S7: k'(2,2) = 12EI/L³");
    CHECK_REL(k(1, 2), 6.0 * EI / 25.0, kExact, "S7: k'(2,3) = 6EI/L²");
    CHECK_REL(k(2, 2), 4.0 * EI / 5.0, kExact, "S7: k'(3,3) = 4EI/L");
    CHECK_REL(k(2, 5), 2.0 * EI / 5.0, kExact, "S7: k'(3,6) = 2EI/L");
    const auto T = el.transformation();
    const auto TtT = multiply(transpose(T), T);
    for (int i = 0; i < 6; ++i)
        for (int j = 0; j < 6; ++j) CHECK_ABS(TtT(i, j), i == j ? 1.0 : 0.0, 1e-14, "S7: T orthogonale (TᵀT = I)");
    const auto kg = el.globalStiffness();
    TEST_CHECK(numerics::LinAlg::asymmetry(kg) < 1e-14, "S7: k = TᵀK'T symétrique");
    // Modes de corps rigide globaux : translation X, translation Y, rotation autour de l'origine → k·r = 0
    const numerics::Vector modes[3] = { { 1, 0, 0, 1, 0, 0 }, { 0, 1, 0, 0, 1, 0 }, { -a.y, a.x, 1, -b.y, b.x, 1 } };
    for (const auto& r : modes)
        TEST_CHECK(numerics::LinAlg::normInf(multiply(kg, r)) <= 1e-9 * numerics::LinAlg::maxAbs(kg), "S7: k·r = 0 pour un mouvement de corps rigide");
    const numerics::Vector g { 0.1, -0.2, 0.3, 0.4, 0.5, -0.6 };
    const auto back = el.toGlobal(el.toLocal(g));
    for (std::size_t i = 0; i < 6; ++i) CHECK_ABS(back[i], g[i], 1e-15, "S7: Tᵀ·T·d = d");

    // Efforts d'encastrement parfait (élément horizontal de 6 m) : valeurs des tables
    const FrameElement2D h(1, { 1, 0, 0, {} }, { 2, 6, 0, {} }, steel(), ipe300());
    auto f0 = h.fixedEndForces(h.localLoads({ uniformLoad(1, -10.0) }));
    CHECK_REL(f0[1], 30.0, kExact, "S7: charge uniforme — Fy_i = qL/2");
    CHECK_REL(f0[2], 30.0, kExact, "S7: charge uniforme — Mz_i = qL²/12");
    CHECK_REL(f0[5], -30.0, kExact, "S7: charge uniforme — Mz_j = −qL²/12");
    f0 = h.fixedEndForces(h.localLoads({ pointForce(1, -40.0, 3.0, LoadDirection::LocalY) }));
    CHECK_REL(f0[1], 20.0, kExact, "S7: force centrée — Fy_i = P/2");
    CHECK_REL(f0[2], 30.0, kExact, "S7: force centrée — Mz_i = PL/8");
    f0 = h.fixedEndForces(h.localLoads({ pointForce(1, -60.0, 2.0, LoadDirection::LocalY) }));
    CHECK_REL(f0[2], 60.0 * 2.0 * 16.0 / 36.0, kExact, "S7: force excentrée — Mz_i = P·a·b²/L²");
    CHECK_REL(f0[5], -60.0 * 4.0 * 4.0 / 36.0, kExact, "S7: force excentrée — Mz_j = −P·a²·b/L²");
    f0 = h.fixedEndForces(h.localLoads({ pointMoment(1, 24.0, 3.0) }));
    CHECK_REL(f0[1], 1.5 * 24.0 / 6.0, kExact, "S7: moment centré — Fy_i = 3M/(2L)");
    CHECK_REL(f0[2], 6.0, kExact, "S7: moment centré — Mz_i = M/4");
    CHECK_REL(f0[5], 6.0, kExact, "S7: moment centré — Mz_j = M/4");
    f0 = h.fixedEndForces(h.localLoads({ thermalLoad(1, 30.0, 20.0) }));
    CHECK_REL(f0[0], EA * 1.2e-5 * 30.0, kExact, "S7: ΔT uniforme — Fx_i = EAαΔT");
    CHECK_REL(f0[2], EI * 1.2e-5 * 20.0 / 0.3, kExact, "S7: gradient — Mz_i = EIαΔT/h");
    // Charges équivalentes statiquement équivalentes aux charges (résultante et moment)
    const auto mixed = el.localLoads({ linearLoad(7, -3.0, -9.0, 0.5, 4.0, LoadDirection::GlobalY), pointForce(7, 12.0, 0.4, LoadDirection::GlobalX, true),
                                       pointMoment(7, 5.0, 2.5) });
    const auto feq = el.equivalentNodalLoadsGlobal(mixed);
    const auto res = el.loadResultant(mixed);
    CHECK_REL(feq[0] + feq[3], res.fx, kExact, "S7: Σ charges équivalentes X = résultante");
    CHECK_REL(feq[1] + feq[4], res.fy, kExact, "S7: Σ charges équivalentes Y = résultante");
    CHECK_REL(feq[2] + feq[5] + a.x * feq[1] - a.y * feq[0] + b.x * feq[4] - b.y * feq[3], res.mz, kExact, "S7: moment des charges équivalentes");
    // Données invalides détectées
    TEST_CHECK(!FrameElement2D(9, a, a, steel(), ipe300()).validate().empty(), "S7: longueur nulle refusée");
    TEST_CHECK(!FrameElement2D(9, a, b, { "x", 0.0, 0, 0 }, ipe300()).validate().empty(), "S7: E nul refusé");
    TEST_CHECK(!FrameElement2D(9, a, b, steel(), { "x", 0.01, -1.0, 0.1 }).validate().empty(), "S7: Iz négatif refusé");
    std::vector<std::string> errors;
    h.localLoads({ pointForce(1, 10.0, 7.0, LoadDirection::LocalY) }, &errors);
    TEST_CHECK(errors.size() == 1, "S7: force hors de l'élément refusée");
    errors.clear();
    h.localLoads({ thermalLoad(1, 0.0, 10.0) }, &errors);
    const FrameElement2D noH(1, { 1, 0, 0, {} }, { 2, 6, 0, {} }, steel(), { "x", 0.01, 1e-4, 0.0 });
    noH.localLoads({ thermalLoad(1, 0.0, 10.0) }, &errors);
    TEST_CHECK(errors.size() == 1, "S7: gradient thermique sans hauteur de section refusé");
    std::cout << "[PASS] S7: FrameElement2D — k', T, k = TᵀK'T, corps rigides, efforts d'encastrement des tables, validation" << std::endl;
    ++passed;
    return true;
}

// =================================================================================================
// S8 — barre axiale (mission § 9, cas 1)
// =================================================================================================
bool testAxialBar(int& passed)
{
    const double L = 4.0;
    {   // force en bout
        Builder b;
        b.node(0, 0), b.node(L, 0);
        b.element(1, 2);
        b.support(1, true, true, true);
        b.lc().nodalLoads.push_back({ 2, 100.0, 0.0, 0.0 });
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S8 force axiale"), "S8");
        CHECK_REL(r.nodeDisplacement(2)[0], 100.0 * L / EA, kExact, "S8: u = PL/(EA)");
        CHECK_REL(r.nodeReaction(1)[0], -100.0, kExact, "S8: réaction −P");
        CHECK_REL(r.elements[0].diagram.Nmax, 100.0, kExact, "S8: N = +P (traction)");
        CHECK_REL(r.elements[0].diagram.Nmin, 100.0, kExact, "S8: N constant");
    }
    {   // charge axiale répartie (x local) sur une console
        Builder b;
        b.node(0, 0), b.node(L, 0);
        b.element(1, 2);
        b.support(1, true, true, true);
        b.lc().elementLoads.push_back(uniformLoad(1, 10.0, LoadDirection::LocalX));
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S8 axiale répartie"), "S8");
        CHECK_REL(r.nodeDisplacement(2)[0], 10.0 * L * L / (2.0 * EA), kExact, "S8: u(L) = pL²/(2EA)");
        CHECK_REL(at(r.elements[0].diagram, 0.0)->N, 10.0 * L, kExact, "S8: N(0) = pL");
        CHECK_REL(r.nodeReaction(1)[0], -10.0 * L, kExact, "S8: réaction −pL");
    }
    {   // force axiale ponctuelle sur barre bloquée aux deux bouts
        Builder b;
        b.node(0, 0), b.node(L, 0);
        b.element(1, 2);
        b.support(1, true, true, true);
        b.support(2, true, true, true);
        b.lc().elementLoads.push_back(pointForce(1, 60.0, 1.0, LoadDirection::LocalX));
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S8 force axiale ponctuelle"), "S8");
        CHECK_REL(r.nodeReaction(1)[0], -60.0 * 3.0 / L, kExact, "S8: R1 = −P·b/L");
        CHECK_REL(r.nodeReaction(2)[0], -60.0 * 1.0 / L, kExact, "S8: R2 = −P·a/L");
    }
    {   // ΔT uniforme : barre bloquée (effort) et barre libre (allongement) ; ε0 équivalent
        for (int variant = 0; variant < 3; ++variant)
        {
            Builder b;
            b.node(0, 0), b.node(L, 0);
            b.element(1, 2);
            b.support(1, true, true, true);
            b.support(2, variant != 1, true, true);
            b.lc().elementLoads.push_back(variant == 2 ? initialStrain(1, 1.2e-5 * 30.0, 0.0) : thermalLoad(1, 30.0, 0.0));
            const auto r = run(b.m);
            TEST_CHECK(commonChecks(r, "S8 thermique"), "S8");
            if (variant == 1)
            {
                CHECK_REL(r.nodeDisplacement(2)[0], 1.2e-5 * 30.0 * L, kExact, "S8: barre libre — allongement αΔT·L");
                CHECK_ABS(r.elements[0].diagram.Nmax, 0.0, 1e-9, "S8: barre libre — N = 0");
            }
            else
            {
                CHECK_REL(r.elements[0].diagram.Nmin, -EA * 1.2e-5 * 30.0, kExact, "S8: barre bloquée — N = −EAαΔT (ou −EAε0)");
                CHECK_REL(r.nodeReaction(1)[0], EA * 1.2e-5 * 30.0, kExact, "S8: réaction EAαΔT");
                CHECK_ABS(r.nodeDisplacement(2)[0], 0.0, 1e-15, "S8: aucun déplacement");
            }
        }
    }
    std::cout << "[PASS] S8: barre axiale — force, charge répartie, force ponctuelle, ΔT uniforme, déformation initiale" << std::endl;
    ++passed;
    return true;
}

// =================================================================================================
// S9 — poutre bi-appuyée sous charge ponctuelle (cas 2)
// =================================================================================================
bool testPointLoadBeam(int& passed)
{
    const double L = 6.0, P = 50.0;
    {   // force nodale au nœud central (2 éléments)
        auto b = simplySupported(L, 2);
        b.lc().nodalLoads.push_back({ 2, 0.0, -P, 0.0 });
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S9 force nodale"), "S9");
        CHECK_REL(r.nodeDisplacement(2)[1], -P * L * L * L / (48.0 * EI), kExact, "S9: flèche PL³/(48EI)");
        CHECK_REL(r.nodeReaction(1)[1], P / 2, kExact, "S9: réaction P/2");
        CHECK_REL(r.nodeReaction(3)[1], P / 2, kExact, "S9: réaction P/2 (B)");
        CHECK_REL(r.nodeDisplacement(1)[2], -P * L * L / (16.0 * EI), kExact, "S9: rotation d'appui −PL²/(16EI)");
        CHECK_REL(r.elements[0].diagram.Mmax, P * L / 4, kExact, "S9: moment maximal PL/4");
    }
    for (auto dir : { LoadDirection::LocalY, LoadDirection::GlobalY })
    {   // force sur l'élément au milieu (1 élément) : même solution
        auto b = simplySupported(L, 1);
        b.lc().elementLoads.push_back(pointForce(1, -P, 0.5, dir, true));
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S9 force sur élément"), "S9");
        CHECK_REL(at(r.elements[0].diagram, 3.0)->v, -P * L * L * L / (48.0 * EI), kExact, "S9: flèche sous la force (élément)");
        CHECK_REL(r.elements[0].diagram.Mmax, P * L / 4, kExact, "S9: PL/4 (élément)");
        CHECK_REL(r.elements[0].diagram.xMmax, 3.0, kExact, "S9: abscisse du moment maximal");
        CHECK_REL(at(r.elements[0].diagram, 3.0)->V, P / 2, kExact, "S9: V(L/2⁻) = P/2");
        CHECK_REL(at(r.elements[0].diagram, 3.0, true)->V, -P / 2, kExact, "S9: V(L/2⁺) = −P/2 (discontinuité)");
    }
    {   // force excentrée a = 2, b = 4
        const double a = 2.0, bb = 4.0, Q = 60.0;
        auto b = simplySupported(L, 1);
        b.lc().elementLoads.push_back(pointForce(1, -Q, a, LoadDirection::LocalY));
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S9 force excentrée"), "S9");
        CHECK_REL(r.nodeReaction(1)[1], Q * bb / L, kExact, "S9: R_A = P·b/L");
        CHECK_REL(r.nodeReaction(2)[1], Q * a / L, kExact, "S9: R_B = P·a/L");
        CHECK_REL(r.elements[0].diagram.Mmax, Q * a * bb / L, kExact, "S9: M = P·a·b/L");
        CHECK_REL(at(r.elements[0].diagram, a)->v, -Q * a * a * bb * bb / (3.0 * EI * L), kExact, "S9: flèche sous la force P·a²·b²/(3EIL)");
    }
    std::cout << "[PASS] S9: poutre bi-appuyée, charge ponctuelle — nodale, sur l'élément (local et global), excentrée" << std::endl;
    ++passed;
    return true;
}

// =================================================================================================
// S10 — charges réparties (cas 3) et autres charges d'élément
// =================================================================================================
bool testDistributedLoads(int& passed)
{
    const double L = 6.0, q = 10.0;
    {   // bi-appuyée, charge uniforme
        auto b = simplySupported(L);
        b.lc().elementLoads.push_back(uniformLoad(1, -q));
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S10 uniforme"), "S10");
        CHECK_REL(at(r.elements[0].diagram, 3.0)->v, -5.0 * q * std::pow(L, 4) / (384.0 * EI), kExact, "S10: flèche 5qL⁴/(384EI)");
        CHECK_REL(r.elements[0].diagram.Mmax, q * L * L / 8, kExact, "S10: M = qL²/8");
        CHECK_REL(r.nodeReaction(1)[1], q * L / 2, kExact, "S10: R = qL/2");
        CHECK_REL(r.nodeDisplacement(1)[2], -q * L * L * L / (24.0 * EI), kExact, "S10: rotation qL³/(24EI)");
        CHECK_REL(r.elements[0].diagram.maxDeflection, -5.0 * q * std::pow(L, 4) / (384.0 * EI), 1e-8, "S10: flèche maximale (recherche)");
    }
    {   // bi-encastrée (aucun DDL libre : solution par les efforts d'encastrement)
        Builder b;
        b.node(0, 0), b.node(L, 0);
        b.element(1, 2);
        b.support(1, true, true, true), b.support(2, true, true, true);
        b.lc().elementLoads.push_back(uniformLoad(1, -q));
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S10 bi-encastrée"), "S10");
        TEST_CHECK(r.bc.freeDofs.empty(), "S10: aucun DDL libre");
        CHECK_REL(at(r.elements[0].diagram, 0.0)->M, -q * L * L / 12, kExact, "S10: moment d'encastrement −qL²/12");
        CHECK_REL(at(r.elements[0].diagram, 3.0)->M, q * L * L / 24, kExact, "S10: moment en travée qL²/24");
        CHECK_REL(at(r.elements[0].diagram, 3.0)->v, -q * std::pow(L, 4) / (384.0 * EI), kExact, "S10: flèche qL⁴/(384EI)");
        CHECK_REL(r.nodeReaction(1)[2], q * L * L / 12, kExact, "S10: réaction moment qL²/12");
    }
    {   // console, charge uniforme
        const double Lc = 3.0;
        Builder b;
        b.node(0, 0), b.node(Lc, 0);
        b.element(1, 2);
        b.support(1, true, true, true);
        b.lc().elementLoads.push_back(uniformLoad(1, -q));
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S10 console"), "S10");
        CHECK_REL(r.nodeDisplacement(2)[1], -q * std::pow(Lc, 4) / (8.0 * EI), kExact, "S10: console qL⁴/(8EI)");
        CHECK_REL(r.nodeDisplacement(2)[2], -q * std::pow(Lc, 3) / (6.0 * EI), kExact, "S10: rotation qL³/(6EI)");
        CHECK_REL(r.nodeReaction(1)[2], q * Lc * Lc / 2, kExact, "S10: moment d'encastrement qL²/2");
    }
    {   // triangulaire 0 → q
        const double qt = 12.0;
        auto b = simplySupported(L);
        b.lc().elementLoads.push_back(linearLoad(1, 0.0, -qt, 0.0, -1.0, LoadDirection::LocalY));
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S10 triangulaire"), "S10");
        CHECK_REL(r.nodeReaction(1)[1], qt * L / 6, kExact, "S10: triangulaire R_A = qL/6");
        CHECK_REL(r.nodeReaction(2)[1], qt * L / 3, kExact, "S10: triangulaire R_B = qL/3");
        CHECK_REL(r.elements[0].diagram.Mmax, qt * L * L / (9.0 * std::sqrt(3.0)), kExact, "S10: M max = qL²/(9√3)");
        CHECK_REL(r.elements[0].diagram.xMmax, L / std::sqrt(3.0), kExact, "S10: en x = L/√3");
        CHECK_REL(at(r.elements[0].diagram, 3.0)->v, -5.0 * qt * std::pow(L, 4) / (768.0 * EI), kExact, "S10: flèche 5qL⁴/(768EI)");
    }
    {   // trapézoïdale 5 → 15 = uniforme 5 + triangulaire 10
        auto b = simplySupported(L);
        b.lc().elementLoads.push_back(linearLoad(1, -5.0, -15.0, 0.0, -1.0, LoadDirection::LocalY));
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S10 trapézoïdale"), "S10");
        CHECK_REL(r.nodeReaction(1)[1], 5.0 * L / 2 + 10.0 * L / 6, kExact, "S10: trapézoïdale R_A");
        CHECK_REL(r.nodeReaction(2)[1], 5.0 * L / 2 + 10.0 * L / 3, kExact, "S10: trapézoïdale R_B");
        CHECK_REL(at(r.elements[0].diagram, 3.0)->v, -5.0 * 10.0 * std::pow(L, 4) / (384.0 * EI), kExact, "S10: flèche = celle de la charge moyenne");
    }
    {   // partielle de 1 à 3 m
        auto b = simplySupported(L);
        b.lc().elementLoads.push_back(linearLoad(1, -8.0, -8.0, 1.0, 3.0, LoadDirection::LocalY));
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S10 partielle"), "S10");
        CHECK_REL(r.nodeReaction(1)[1], 8.0 * 2.0 * (L - 2.0) / L, kExact, "S10: partielle R_A = q·c·(L − x̄)/L");
        CHECK_REL(r.nodeReaction(2)[1], 8.0 * 2.0 * 2.0 / L, kExact, "S10: partielle R_B = q·c·x̄/L");
    }
    {   // multi-segments = somme de segments linéaires
        std::string error;
        const auto pieces = piecewiseLinearLoad(1, { { 0.0, -4.0 }, { 2.0, -10.0 }, { 6.0, -2.0 } }, LoadDirection::LocalY, false, &error);
        TEST_CHECK(pieces.size() == 2 && error.empty(), "S10: charge multi-segments découpée en 2 segments");
        auto b1 = simplySupported(L), b2 = simplySupported(L);
        b1.lc().elementLoads = pieces;
        b2.lc().elementLoads = { linearLoad(1, -4.0, -10.0, 0.0, 2.0, LoadDirection::LocalY), linearLoad(1, -10.0, -2.0, 2.0, 6.0, LoadDirection::LocalY) };
        const auto r1 = run(b1.m), r2 = run(b2.m);
        TEST_CHECK(commonChecks(r1, "S10 multi-segments"), "S10");
        CHECK_REL(r1.nodeReaction(1)[1] + r1.nodeReaction(2)[1], 14.0 + 24.0, kExact, "S10: multi-segments Σ R = aire de la charge");
        TEST_CHECK(compareResults(r1, r2).maxDeviation() == 0.0, "S10: multi-segments identique aux segments séparés");
        TEST_CHECK(piecewiseLinearLoad(1, { { 0.0, 1.0 }, { 0.0, 2.0 } }, LoadDirection::LocalY, false, &error).empty(), "S10: positions non croissantes refusées");
    }
    {   // charges globales sur élément incliné (L = 5, c = 0,8, s = 0,6) : projetées ou non
        struct Case
        {
            LoadDirection dir;
            bool projected;
            double q, fx, fy;
        };
        const Case cases[4] = { { LoadDirection::GlobalY, false, -2.0, 0.0, -10.0 }, { LoadDirection::GlobalY, true, -2.0, 0.0, -8.0 },
                                { LoadDirection::GlobalX, false, 1.0, 5.0, 0.0 }, { LoadDirection::GlobalX, true, 1.0, 3.0, 0.0 } };
        for (const auto& c : cases)
        {
            Builder b;
            b.node(0, 0), b.node(4, 3);
            b.element(1, 2);
            b.support(1, true, true, false), b.support(2, false, true, false);
            auto load = uniformLoad(1, c.q, c.dir);
            load.projected = c.projected;
            b.lc().elementLoads.push_back(load);
            const auto r = run(b.m);
            TEST_CHECK(commonChecks(r, "S10 incliné"), "S10");
            CHECK_REL(r.equilibrium.loadFx + 1.0, c.fx + 1.0, kExact, "S10: résultante X des charges globales (projection)");
            CHECK_REL(r.equilibrium.loadFy + 1.0, c.fy + 1.0, kExact, "S10: résultante Y des charges globales (projection)");
            if (c.dir == LoadDirection::GlobalY && !c.projected) CHECK_REL(r.nodeReaction(2)[1], 5.0, kExact, "S10: incliné R_B = q·L·(x̄ = 2)/4");
        }
    }
    {   // poids propre (tous les éléments)
        auto b = simplySupported(L, 2);
        b.lc().elementLoads.push_back(selfWeight());
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S10 poids propre"), "S10");
        const double w = 78.5 * 53.8e-4;
        CHECK_REL(r.nodeReaction(1)[1], w * L / 2, kExact, "S10: poids propre R = γA·L/2");
        CHECK_REL(r.nodeDisplacement(2)[1], -5.0 * w * std::pow(L, 4) / (384.0 * EI), kExact, "S10: flèche de poids propre");
    }
    {   // moment concentré en travée
        const double M0 = 30.0, a = 2.0;
        auto b = simplySupported(L);
        b.lc().elementLoads.push_back(pointMoment(1, M0, a));
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S10 moment concentré"), "S10");
        CHECK_REL(r.nodeReaction(1)[1], M0 / L, kExact, "S10: moment concentré R_A = M/L");
        CHECK_REL(r.nodeReaction(2)[1], -M0 / L, kExact, "S10: moment concentré R_B = −M/L");
        CHECK_REL(at(r.elements[0].diagram, a)->M, M0 * a / L, kExact, "S10: M(a⁻) = M·a/L");
        CHECK_REL(at(r.elements[0].diagram, a, true)->M, -M0 * (L - a) / L, kExact, "S10: M(a⁺) = −M·b/L (saut de M)");
    }
    {   // gradient thermique : bi-encastrée (moment constant) et bi-appuyée (courbure libre) ; κ0 équivalent
        const double kappa = 1.2e-5 * 20.0 / 0.3;
        for (int variant = 0; variant < 3; ++variant)
        {
            Builder b;
            b.node(0, 0), b.node(L, 0);
            b.element(1, 2);
            if (variant == 0) b.support(1, true, true, true), b.support(2, true, true, true);
            else b.support(1, true, true, false), b.support(2, false, true, false);
            b.lc().elementLoads.push_back(variant == 2 ? initialStrain(1, 0.0, kappa) : thermalLoad(1, 0.0, 20.0));
            const auto r = run(b.m);
            TEST_CHECK(commonChecks(r, "S10 gradient"), "S10");
            if (variant == 0)
            {
                CHECK_REL(r.elements[0].diagram.Mmin, -EI * kappa, kExact, "S10: bi-encastrée — M = −EIαΔT/h");
                CHECK_REL(r.elements[0].diagram.Mmax, -EI * kappa, kExact, "S10: bi-encastrée — M constant");
            }
            else
            {
                CHECK_REL(at(r.elements[0].diagram, 3.0)->v, -kappa * L * L / 8.0, kExact, "S10: bi-appuyée — flèche −κ0·L²/8");
                CHECK_REL(r.nodeDisplacement(1)[2], -kappa * L / 2.0, kExact, "S10: bi-appuyée — rotation −κ0·L/2");
                CHECK_ABS(r.elements[0].diagram.Mmax, 0.0, 1e-9, "S10: bi-appuyée — aucun moment");
            }
        }
    }
    std::cout << "[PASS] S10: charges d'élément — uniforme, bi-encastrée, console, triangulaire, trapézoïdale, partielle, multi-segments, "
                 "globales projetées ou non, poids propre, moment concentré, gradient thermique, courbure initiale" << std::endl;
    ++passed;
    return true;
}

// =================================================================================================
// S11 — portique élémentaire (cas 4)
// =================================================================================================
bool testPortal(int& passed)
{
    const double h = 4.0, L = 6.0, H = 20.0;
    // Barres quasi inextensibles (A × 10⁴ : effet de l'allongement des barres < 1e-7 en relatif, conditionnement
    // encore modéré) : solution fermée du portique encastré sous force horizontale en tête
    Section2D col = heb200(), beam = ipe300();
    col.A *= 1e4, beam.A *= 1e4;
    Builder b;
    b.node(0, 0), b.node(0, h), b.node(L, h), b.node(L, 0);
    b.element(1, 2, col), b.element(2, 3, beam), b.element(4, 3, col);
    b.support(1, true, true, true), b.support(4, true, true, true);
    b.lc().nodalLoads.push_back({ 2, H, 0.0, 0.0 });
    const auto r = run(b.m);
    TEST_CHECK(commonChecks(r, "S11 portique"), "S11");
    const double EIc = 210e6 * 5696e-8;
    const double k = (8356e-8 * h) / (5696e-8 * L);
    const double Mbase = H * h * (1 + 3 * k) / (2 * (1 + 6 * k));
    const double Mtop = 3 * k * H * h / (2 * (1 + 6 * k));
    const double sway = H * h * h * h * (2 + 3 * k) / (12 * EIc * (1 + 6 * k));
    CHECK_REL(std::abs(r.nodeReaction(1)[2]), Mbase, 1e-6, "S11: moment en pied Hh(1+3k)/(2(1+6k))");
    CHECK_REL(std::abs(r.nodeReaction(4)[2]), Mbase, 1e-6, "S11: moment en pied (poteau droit)");
    CHECK_REL(std::abs(r.elements[0].endForces[5]), Mtop, 1e-6, "S11: moment en tête 3kHh/(2(1+6k))");
    CHECK_REL(r.nodeDisplacement(2)[0], sway, 1e-6, "S11: déplacement en tête Hh³(2+3k)/(12EIc(1+6k))");
    CHECK_REL(r.nodeReaction(1)[0], -H / 2, 1e-6, "S11: effort tranchant en pied H/2");
    // Sections réelles, charge répartie sur la traverse et force horizontale : MetDeDeplacement
    Builder real;
    real.node(0, 0), real.node(0, h), real.node(L, h), real.node(L, 0);
    real.element(1, 2, heb200()), real.element(2, 3), real.element(4, 3, heb200());
    real.support(1, true, true, true), real.support(4, true, true, true);
    real.lc().nodalLoads.push_back({ 2, H, 0.0, 0.0 });
    real.lc().elementLoads.push_back(uniformLoad(2, -15.0));
    TEST_CHECK(compareWithMdd(real.m, 1, "S11 portique réel"), "S11");
    std::cout << "[PASS] S11: portique encastré — solution fermée (moments en pied et en tête, déplacement) à 1e-6, MetDeDeplacement à 1e-9" << std::endl;
    ++passed;
    return true;
}

// =================================================================================================
// S12 — élément incliné, transformation local/global (cas 5)
// =================================================================================================
bool testInclined(int& passed)
{
    const double L = 3.0, alpha = kPi / 6.0, c = std::cos(alpha), s = std::sin(alpha), P = 10.0;
    Builder b;
    b.node(0, 0), b.node(L * c, L * s);
    b.element(1, 2);
    b.support(1, true, true, true);
    b.lc().nodalLoads.push_back({ 2, 0.0, -P, 0.0 });
    const auto r = run(b.m);
    TEST_CHECK(commonChecks(r, "S12 incliné"), "S12");
    const double pAxial = -P * s, pTrans = -P * c;   // composantes locales de la force
    const double uAx = pAxial * L / EA, vTr = pTrans * L * L * L / (3.0 * EI);
    CHECK_REL(r.nodeDisplacement(2)[0], c * uAx - s * vTr, kExact, "S12: ux en bout (transformation)");
    CHECK_REL(r.nodeDisplacement(2)[1], s * uAx + c * vTr, kExact, "S12: uy en bout (transformation)");
    CHECK_REL(r.nodeDisplacement(2)[2], pTrans * L * L / (2.0 * EI), kExact, "S12: rotation en bout PL²/(2EI)");
    CHECK_REL(r.nodeReaction(1)[1], P, kExact, "S12: réaction verticale");
    CHECK_REL(r.nodeReaction(1)[2], P * L * c, kExact, "S12: moment d'encastrement P × bras de levier");
    CHECK_REL(r.elements[0].endForces[0], -pAxial, kExact, "S12: effort axial local à l'encastrement");
    // Invariance : même structure et même charge tournées de 47° → mêmes efforts locaux et mêmes normes
    const double phi = 47.0 * kPi / 180.0, cp = std::cos(phi), sp = std::sin(phi);
    Builder t;
    t.node(0, 0), t.node(L * std::cos(alpha + phi), L * std::sin(alpha + phi));
    t.element(1, 2);
    t.support(1, true, true, true);
    t.lc().nodalLoads.push_back({ 2, P * sp, -P * cp, 0.0 });
    const auto rt = run(t.m);
    TEST_CHECK(commonChecks(rt, "S12 tourné"), "S12");
    for (std::size_t k = 0; k < 6; ++k) CHECK_ABS(rt.elements[0].endForces[k], r.elements[0].endForces[k], 1e-9, "S12: efforts locaux invariants par rotation");
    CHECK_REL(std::hypot(rt.nodeDisplacement(2)[0], rt.nodeDisplacement(2)[1]), std::hypot(r.nodeDisplacement(2)[0], r.nodeDisplacement(2)[1]), kExact,
              "S12: norme du déplacement invariante");
    std::cout << "[PASS] S12: élément incliné — déplacements par transformation, réactions, invariance par rotation du modèle" << std::endl;
    ++passed;
    return true;
}

// =================================================================================================
// S13 — déplacements imposés non nuls (cas 6)
// =================================================================================================
bool testImposedDisplacements(int& passed)
{
    {   // bi-encastrée, tassement Δ en j
        const double L = 6.0, D = 0.01;
        Builder b;
        b.node(0, 0), b.node(L, 0);
        b.element(1, 2);
        b.support(1, true, true, true), b.support(2, true, true, true);
        b.lc().imposedDisplacements.push_back({ 2, 1, -D });
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S13 tassement"), "S13");
        CHECK_REL(r.nodeReaction(1)[1], 12.0 * EI * D / (L * L * L), kExact, "S13: tassement — R = 12EIΔ/L³");
        CHECK_REL(r.nodeReaction(1)[2], 6.0 * EI * D / (L * L), kExact, "S13: tassement — M = 6EIΔ/L²");
        CHECK_REL(r.nodeReaction(2)[2], 6.0 * EI * D / (L * L), kExact, "S13: tassement — M = 6EIΔ/L² (j)");
        TEST_CHECK(numerics::LinAlg::normInf(r.F) == 0.0, "S13: le déplacement imposé n'est pas une force nodale (F = 0)");
        CHECK_REL(r.nodeDisplacement(2)[1], -D, kExact, "S13: déplacement imposé restitué dans d");
    }
    {   // rotation imposée à l'encastrement : 4EIθ/L et 2EIθ/L
        const double L = 5.0, th = 0.002;
        Builder b;
        b.node(0, 0), b.node(L, 0);
        b.element(1, 2);
        b.support(1, true, true, true), b.support(2, true, true, true);
        b.lc().imposedDisplacements.push_back({ 1, 2, th });
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S13 rotation"), "S13");
        CHECK_REL(r.nodeReaction(1)[2], 4.0 * EI * th / L, kExact, "S13: rotation imposée — 4EIθ/L");
        CHECK_REL(r.nodeReaction(2)[2], 2.0 * EI * th / L, kExact, "S13: rotation imposée — 2EIθ/L");
        CHECK_REL(r.nodeReaction(1)[1], 6.0 * EI * th / (L * L), kExact, "S13: rotation imposée — 6EIθ/L²");
    }
    {   // poutre continue à deux travées, tassement de l'appui central
        const double L = 5.0, D = 0.005;
        Builder b;
        b.node(0, 0), b.node(L, 0), b.node(2 * L, 0);
        b.element(1, 2), b.element(2, 3);
        b.support(1, true, true, false), b.support(2, false, true, false), b.support(3, false, true, false);
        b.lc().imposedDisplacements.push_back({ 2, 1, -D });
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S13 continue"), "S13");
        CHECK_REL(r.nodeReaction(2)[1], -6.0 * EI * D / (L * L * L), kExact, "S13: appui central R = −6EIΔ/L³");
        CHECK_REL(r.nodeReaction(1)[1], 3.0 * EI * D / (L * L * L), kExact, "S13: appuis d'extrémité 3EIΔ/L³");
        double rhs = 0.0;
        for (double v : r.reduced.rhs) rhs = std::max(rhs, std::abs(v));
        TEST_CHECK(rhs > 0.0 && numerics::LinAlg::normInf(r.reduced.Ff) == 0.0, "S13: second membre = −K_fc·d_c (F_f nul)");
    }
    {   // déplacement imposé sur un DDL libre : refusé
        Builder b;
        b.node(0, 0), b.node(4, 0);
        b.element(1, 2);
        b.support(1, true, true, true);
        b.lc().imposedDisplacements.push_back({ 2, 1, -0.01 });
        TEST_CHECK(!validate(b.m).ok(), "S13: validation — déplacement imposé sur DDL libre refusé");
        BoundaryConditions bc;
        std::string error;
        TEST_CHECK(!boundaryConditions(b.m, numberDofs(b.m), b.m.loadCases[0], bc, &error) && error.find("libre") != std::string::npos,
                   "S13: conditions aux limites — refus motivé");
        TEST_CHECK(!run(b.m).success, "S13: calcul refusé");
    }
    std::cout << "[PASS] S13: déplacements imposés — tassement, rotation d'appui, poutre continue, partition K_ff·d_f = F_f − K_fc·d_c, refus sur DDL libre" << std::endl;
    ++passed;
    return true;
}

// =================================================================================================
// S14 — système instable, détection du mécanisme (cas 7)
// =================================================================================================
bool testMechanisms(int& passed)
{
    {   // deux appuis simples verticaux : translation X libre
        Builder b;
        b.node(0, 0), b.node(6, 0);
        b.element(1, 2);
        b.support(1, false, true, false), b.support(2, false, true, false);
        b.lc().elementLoads.push_back(uniformLoad(1, -10.0));
        const auto r = run(b.m);
        TEST_CHECK(!r.success, "S14: appuis insuffisants — calcul refusé");
        TEST_CHECK(r.solve.mechanismKind == "rigid-body", "S14: mode de corps rigide identifié (" << r.solve.mechanismKind << ")");
        TEST_CHECK(r.message.find("suivant X") != std::string::npos && !r.solve.failedDof.empty(),
                   "S14: message — translation suivant X et DDL fautif (" << r.message << ")");
    }
    {   // une seule articulation : rotation autour de l'appui
        Builder b;
        b.node(2, 1), b.node(6, 1);
        b.element(1, 2);
        b.support(1, true, true, false);
        b.lc().nodalLoads.push_back({ 2, 0.0, -5.0, 0.0 });
        const auto r = run(b.m);
        TEST_CHECK(!r.success && r.solve.mechanismKind == "rigid-body", "S14: rotation libre détectée");
        TEST_CHECK(r.message.find("rotation") != std::string::npos && r.message.find("(2 ; 1)") != std::string::npos,
                   "S14: centre de rotation = appui (" << r.message << ")");
    }
    {   // nœud isolé
        Builder b;
        b.node(0, 0), b.node(6, 0), b.node(3, 4);
        b.element(1, 2);
        b.support(1, true, true, true);
        b.lc().nodalLoads.push_back({ 2, 0.0, -5.0, 0.0 });
        TEST_CHECK(!validate(b.m).warnings.empty(), "S14: nœud isolé signalé par la validation");
        const auto r = run(b.m);
        TEST_CHECK(!r.success && r.solve.mechanismKind == "isolated-node" && r.message.find("N3") != std::string::npos,
                   "S14: nœud isolé N3 désigné (" << r.message << ")");
    }
    {   // système mal conditionné (EA/EI ≈ 2,4e10 : κ ≈ 3e10) : signalé, calcul effectué
        Builder b;
        Section2D stiff = ipe300();
        stiff.A = 2e6;
        b.node(0, 0), b.node(1, 0);
        b.element(1, 2, stiff);
        b.support(1, true, true, true);
        b.lc().nodalLoads.push_back({ 2, 0.0, -1.0, 0.0 });
        const auto r = run(b.m);
        TEST_CHECK(r.success && !r.solve.warnings.empty() && r.solve.conditionNumber > kIllConditioned,
                   "S14: mauvais conditionnement signalé (κ = " << r.solve.conditionNumber << ")");
        // EA/EI ≈ 1e13 : pivot relatif sous la tolérance — déclaré quasi singulier, sans mode nul (pas un mécanisme)
        b.m.elements[0].section.A = 1e9;
        const auto q = run(b.m);
        TEST_CHECK(!q.success && q.solve.mechanismKind == "ill-conditioned" && q.message.find("quasi singulier") != std::string::npos,
                   "S14: système quasi singulier distingué d'un mécanisme (" << q.message << ")");
    }
    std::cout << "[PASS] S14: mécanismes — translation, rotation autour de l'appui, nœud isolé (DDL et causes) ; mauvais conditionnement signalé" << std::endl;
    ++passed;
    return true;
}

// =================================================================================================
// S15 — nœud partagé par plusieurs éléments (cas 8), poutre continue
// =================================================================================================
bool testSharedNode(int& passed)
{
    {   // nœud 2 commun à trois éléments
        Builder b;
        b.node(0, 0), b.node(4, 0), b.node(8, 0), b.node(4, -3);
        b.element(1, 2), b.element(2, 3), b.element(4, 2, heb200());
        b.support(1, true, true, false), b.support(3, false, true, false), b.support(4, true, true, true);
        b.lc().nodalLoads.push_back({ 2, 10.0, -40.0, 5.0 });
        b.lc().elementLoads.push_back(uniformLoad(1, -6.0));
        b.lc().elementLoads.push_back(pointForce(2, -20.0, 1.5, LoadDirection::GlobalY));
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S15 nœud partagé"), "S15");
        double sx = 0.0, sy = 0.0, sm = 0.0;
        for (const auto& e : r.elements)
        {
            const auto* def = b.m.element(e.element);
            const int o = def->nodeI == 2 ? 0 : def->nodeJ == 2 ? 3 : -1;
            if (o < 0) continue;
            sx += e.endForcesGlobal[static_cast<std::size_t>(o)], sy += e.endForcesGlobal[static_cast<std::size_t>(o + 1)], sm += e.endForcesGlobal[static_cast<std::size_t>(o + 2)];
        }
        CHECK_REL(sx, 10.0, kExact, "S15: équilibre du nœud partagé (X)");
        CHECK_REL(sy, -40.0, kExact, "S15: équilibre du nœud partagé (Y)");
        CHECK_REL(sm, 5.0, kExact, "S15: équilibre du nœud partagé (moment)");
        TEST_CHECK(compareWithMdd(b.m, 1, "S15 nœud partagé"), "S15");
    }
    {   // poutre continue à deux travées égales, charge uniforme
        const double L = 5.0, q = 10.0;
        Builder b;
        b.node(0, 0), b.node(L, 0), b.node(2 * L, 0);
        b.element(1, 2), b.element(2, 3);
        b.support(1, true, true, false), b.support(2, false, true, false), b.support(3, false, true, false);
        b.lc().elementLoads.push_back(uniformLoad(1, -q));
        b.lc().elementLoads.push_back(uniformLoad(2, -q));
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S15 continue"), "S15");
        CHECK_REL(r.nodeReaction(2)[1], 5.0 * q * L / 4.0, kExact, "S15: appui central 5qL/4");
        CHECK_REL(r.nodeReaction(1)[1], 3.0 * q * L / 8.0, kExact, "S15: appui d'extrémité 3qL/8");
        CHECK_REL(at(r.elements[0].diagram, L)->M, -q * L * L / 8.0, kExact, "S15: moment sur appui −qL²/8");
    }
    std::cout << "[PASS] S15: nœud commun à trois éléments (équilibre du nœud, MetDeDeplacement), poutre continue (5qL/4, −qL²/8)" << std::endl;
    ++passed;
    return true;
}

// =================================================================================================
// S16 — charges nodales, combinaisons, validation croisée MetDeDeplacement
// =================================================================================================
bool testCombinationsAndCrossCheck(int& passed)
{
    {   // moment nodal en bout de console
        const double L = 3.0, M = 12.0;
        Builder b;
        b.node(0, 0), b.node(L, 0);
        b.element(1, 2);
        b.support(1, true, true, true);
        b.lc().nodalLoads.push_back({ 2, 0.0, 0.0, M });
        const auto r = run(b.m);
        TEST_CHECK(commonChecks(r, "S16 moment nodal"), "S16");
        CHECK_REL(r.nodeDisplacement(2)[2], M * L / EI, kExact, "S16: rotation ML/EI");
        CHECK_REL(r.nodeDisplacement(2)[1], M * L * L / (2.0 * EI), kExact, "S16: flèche ML²/(2EI)");
    }
    {   // combinaison 1,35 G + 1,5 Q = superposition des calculs séparés
        auto b = simplySupported(6.0, 2);
        b.lc(1).elementLoads.push_back(uniformLoad(1, -5.0));
        b.lc(1).elementLoads.push_back(uniformLoad(2, -5.0));
        b.lc(2).nodalLoads.push_back({ 2, 0.0, -30.0, 0.0 });
        b.m.combinations.push_back({ 1, "ELU", { { 1, 1.35 }, { 2, 1.5 } } });
        const auto g = run(b.m, 1), q = run(b.m, 2);
        const auto c = solveLinearStatic(b.m, { 0, 1 });
        TEST_CHECK(commonChecks(c, "S16 combinaison"), "S16");
        for (std::size_t k = 0; k < c.d.size(); ++k)
            CHECK_ABS(c.d[k], 1.35 * g.d[k] + 1.5 * q.d[k], 1e-12 * (1.0 + std::abs(c.d[k])), "S16: d(ELU) = 1,35 d(G) + 1,5 d(Q)");
        TEST_CHECK(c.title.find("ELU") != std::string::npos, "S16: titre de la combinaison");
    }
    {   // portique à versants (éléments inclinés), charges trapézoïdales partielles et ponctuelles globales
        Builder b;
        b.node(0, 0), b.node(0, 5), b.node(6, 7), b.node(12, 5), b.node(12, 0);
        b.element(1, 2, heb200()), b.element(2, 3), b.element(3, 4), b.element(5, 4, heb200());
        b.support(1, true, true, true), b.support(5, true, true, false);
        b.lc().nodalLoads.push_back({ 2, 8.0, 0.0, 0.0 });
        b.lc().elementLoads.push_back(linearLoad(2, -4.0, -9.0, 0.5, 5.0, LoadDirection::GlobalY));
        b.lc().elementLoads.push_back(uniformLoad(3, -6.0, LoadDirection::GlobalY));
        b.lc().elementLoads.push_back(pointForce(3, -15.0, 0.3, LoadDirection::GlobalY, true));
        b.lc().elementLoads.push_back(uniformLoad(1, 2.5, LoadDirection::GlobalX));
        TEST_CHECK(compareWithMdd(b.m, 1, "S16 portique à versants"), "S16");
    }
    std::cout << "[PASS] S16: moment nodal, combinaison = superposition, portique à versants identique à MetDeDeplacement (1e-9)" << std::endl;
    ++passed;
    return true;
}

} // namespace

/// Point d'entrée des tests de la programmation structurelle (appelé par ScienceTests.cpp).
int structuralTestCount() { return 10; }

bool testStructural(int& passed)
{
    return testElement(passed) && testAxialBar(passed) && testPointLoadBeam(passed) && testDistributedLoads(passed) && testPortal(passed)
           && testInclined(passed) && testImposedDisplacements(passed) && testMechanisms(passed) && testSharedNode(passed)
           && testCombinationsAndCrossCheck(passed);
}
