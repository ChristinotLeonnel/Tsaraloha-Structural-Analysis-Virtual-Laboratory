// Méthode des déplacements pour portiques plans, étape par étape (voir DirectStiffness2D.h).

#include "tsalab/structural/DirectStiffness2D.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <sstream>

namespace tsalab::structural
{

namespace
{
std::string num(double v, int precision = 6)
{
    std::ostringstream o;
    o.precision(precision);
    o << v;
    return o.str();
}

bool fail(std::string* error, const std::string& message)
{
    if (error) *error = message;
    return false;
}
} // namespace

// -----------------------------------------------------------------------------
// 1. Numérotation
// -----------------------------------------------------------------------------

int DofNumbering::nodePosition(int nodeId) const
{
    for (std::size_t k = 0; k < nodeIds.size(); ++k)
        if (nodeIds[k] == nodeId) return static_cast<int>(k);
    return -1;
}

int DofNumbering::dof(int nodeId, int component) const
{
    const int p = nodePosition(nodeId);
    return p < 0 || component < 0 || component > 2 ? -1 : 3 * p + component;
}

std::array<int, 6> DofNumbering::elementDofs(int nodeI, int nodeJ) const
{
    return { dof(nodeI, 0), dof(nodeI, 1), dof(nodeI, 2), dof(nodeJ, 0), dof(nodeJ, 1), dof(nodeJ, 2) };
}

std::string DofNumbering::label(int d) const
{
    if (d < 0 || d >= size()) return "DDL " + std::to_string(d);
    return "N" + std::to_string(nodeIds[static_cast<std::size_t>(d / 3)]) + "." + componentName(d % 3);
}

DofNumbering numberDofs(const Model2D& model)
{
    DofNumbering n;
    for (const auto& node : model.nodes) n.nodeIds.push_back(node.id);
    return n;
}

// -----------------------------------------------------------------------------
// 4. Conditions aux limites
// -----------------------------------------------------------------------------

bool BoundaryConditions::isConstrained(int dof) const
{
    return std::binary_search(constrainedDofs.begin(), constrainedDofs.end(), dof);
}

bool BoundaryConditions::isEliminated(int dof) const
{
    return std::binary_search(eliminatedDofs.begin(), eliminatedDofs.end(), dof);
}

bool boundaryConditions(const Model2D& model, const DofNumbering& dofs, const LoadCase& loads, BoundaryConditions& out, std::string* error)
{
    out = BoundaryConditions {};
    const int n = dofs.size();
    std::vector<char> fixed(static_cast<std::size_t>(n), 0);
    std::vector<double> value(static_cast<std::size_t>(n), 0.0);
    for (const auto& s : model.supports)
    {
        const int p = dofs.nodePosition(s.node);
        if (p < 0) return fail(error, "appui sur le nœud " + std::to_string(s.node) + " absent de la numérotation");
        if (s.fixUx) fixed[static_cast<std::size_t>(3 * p)] = 1;
        if (s.fixUy) fixed[static_cast<std::size_t>(3 * p + 1)] = 1;
        if (s.fixRz) fixed[static_cast<std::size_t>(3 * p + 2)] = 1;
    }
    // Rotations sans rigidité (toutes les barres articulées au nœud, ni appui ni ressort en rotation) : éliminées
    std::vector<char> eliminated(static_cast<std::size_t>(n), 0);
    for (std::size_t k = 0; k < dofs.nodeIds.size(); ++k)
        if (model.rotationFree(dofs.nodeIds[k]))
        {
            fixed[3 * k + 2] = 1;
            eliminated[3 * k + 2] = 1;
        }
    for (const auto& d : loads.imposedDisplacements)
    {
        const int k = dofs.dof(d.node, d.component);
        if (k < 0) return fail(error, "déplacement imposé au nœud " + std::to_string(d.node) + " absent de la numérotation");
        if (eliminated[static_cast<std::size_t>(k)])
            return fail(error, "rotation imposée au nœud " + std::to_string(d.node) + " : rotation éliminée (toutes les barres y sont articulées)");
        if (!fixed[static_cast<std::size_t>(k)])
            return fail(error, "déplacement imposé " + std::string(componentName(d.component)) + " au nœud " + std::to_string(d.node)
                                   + " : ce DDL est libre (un déplacement imposé s'applique à un DDL bloqué par un appui)");
        value[static_cast<std::size_t>(k)] += d.value;
    }
    for (int k = 0; k < n; ++k)
    {
        if (fixed[static_cast<std::size_t>(k)])
        {
            out.constrainedDofs.push_back(k);
            out.imposed.push_back(value[static_cast<std::size_t>(k)]);
            if (eliminated[static_cast<std::size_t>(k)]) out.eliminatedDofs.push_back(k);
        }
        else
            out.freeDofs.push_back(k);
    }
    return true;
}

// -----------------------------------------------------------------------------
// 2-3. Éléments et assemblage
// -----------------------------------------------------------------------------

bool makeElement(const Model2D& model, const ElementDefinition& e, FrameElement2D& out, std::string* error)
{
    const Node2D* ni = model.node(e.nodeI);
    const Node2D* nj = model.node(e.nodeJ);
    if (!ni || !nj)
        return fail(error, "élément " + std::to_string(e.id) + " : nœud " + std::to_string(!ni ? e.nodeI : e.nodeJ) + " inexistant");
    out = FrameElement2D(e.id, *ni, *nj, e.material, e.section, e.name);
    out.setReleases(e.releaseI, e.releaseJ);
    return true;
}

Matrix assemblyOperator(const DofNumbering& dofs, int nodeI, int nodeJ)
{
    Matrix A(6, dofs.size());
    const auto d = dofs.elementDofs(nodeI, nodeJ);
    for (int k = 0; k < 6; ++k)
        if (d[static_cast<std::size_t>(k)] >= 0) A(k, d[static_cast<std::size_t>(k)]) = 1.0;
    return A;
}

bool assembleStiffness(const Model2D& model, const DofNumbering& dofs, Matrix& K, std::string* error)
{
    const int n = dofs.size();
    K = Matrix(n, n);
    for (const auto& def : model.elements)
    {
        FrameElement2D el;
        if (!makeElement(model, def, el, error)) return false;
        const auto issues = el.validate();
        if (!issues.empty()) return fail(error, issues.front());
        const Matrix ke = el.globalStiffness();
        const auto d = dofs.elementDofs(def.nodeI, def.nodeJ);
        for (int a = 0; a < 6; ++a)
        {
            if (d[static_cast<std::size_t>(a)] < 0) return fail(error, "élément " + std::to_string(def.id) + " : nœud absent de la numérotation");
            for (int b = 0; b < 6; ++b) K(d[static_cast<std::size_t>(a)], d[static_cast<std::size_t>(b)]) += ke(a, b);
        }
    }
    const Matrix Ks = springStiffness(model, dofs);
    for (int k = 0; k < n; ++k) K(k, k) += Ks(k, k);
    return true;
}

Matrix springStiffness(const Model2D& model, const DofNumbering& dofs)
{
    Matrix Ks(dofs.size(), dofs.size());
    for (const auto& s : model.supports)
    {
        const int p = dofs.nodePosition(s.node);
        if (p < 0) continue;
        const bool fix[3] = { s.fixUx, s.fixUy, s.fixRz };
        const double k[3] = { s.kUx, s.kUy, s.kRz };
        for (int c = 0; c < 3; ++c)
            if (!fix[c] && k[c] > 0.0) Ks(3 * p + c, 3 * p + c) += k[c];
    }
    return Ks;
}

Vector springForces(const Model2D& model, const DofNumbering& dofs, const Vector& d)
{
    Vector f(static_cast<std::size_t>(dofs.size()), 0.0);
    if (static_cast<int>(d.size()) != dofs.size()) return f;
    const Matrix Ks = springStiffness(model, dofs);
    for (int k = 0; k < dofs.size(); ++k) f[static_cast<std::size_t>(k)] = -Ks(k, k) * d[static_cast<std::size_t>(k)];
    return f;
}

Vector nodalLoadVector(const DofNumbering& dofs, const LoadCase& loads)
{
    Vector F(static_cast<std::size_t>(dofs.size()), 0.0);
    for (const auto& p : loads.nodalLoads)
    {
        const int k = dofs.nodePosition(p.node);
        if (k < 0) continue;
        F[static_cast<std::size_t>(3 * k)] += p.fx;
        F[static_cast<std::size_t>(3 * k + 1)] += p.fy;
        F[static_cast<std::size_t>(3 * k + 2)] += p.mz;
    }
    return F;
}

bool assembleLoads(const Model2D& model, const DofNumbering& dofs, const LoadCase& loads, Vector& F, std::string* error)
{
    for (const auto& p : loads.nodalLoads)
        if (dofs.nodePosition(p.node) < 0) return fail(error, "charge nodale sur le nœud " + std::to_string(p.node) + " inexistant");
    for (const auto& l : loads.elementLoads)
        if (!(l.element == 0 && l.type == ElementLoadType::SelfWeight) && !model.element(l.element))
            return fail(error, std::string("charge ") + loadTypeName(l.type) + " sur l'élément " + std::to_string(l.element) + " inexistant");
    F = nodalLoadVector(dofs, loads);
    for (const auto& def : model.elements)
    {
        FrameElement2D el;
        if (!makeElement(model, def, el, error)) return false;
        std::vector<std::string> issues;
        const auto local = el.localLoads(loads.elementLoads, &issues);
        if (!issues.empty()) return fail(error, issues.front());
        if (local.empty()) continue;
        const Vector feq = el.equivalentNodalLoadsGlobal(local);
        const auto d = dofs.elementDofs(def.nodeI, def.nodeJ);
        for (int a = 0; a < 6; ++a)
            if (d[static_cast<std::size_t>(a)] >= 0) F[static_cast<std::size_t>(d[static_cast<std::size_t>(a)])] += feq[static_cast<std::size_t>(a)];
    }
    return true;
}

// -----------------------------------------------------------------------------
// 4. Système réduit
// -----------------------------------------------------------------------------

bool reduce(const Matrix& K, const Vector& F, const BoundaryConditions& bc, ReducedSystem& out, std::string* error)
{
    const int n = K.rows;
    const int nf = static_cast<int>(bc.freeDofs.size()), nc = static_cast<int>(bc.constrainedDofs.size());
    if (K.cols != n) return fail(error, "K n'est pas carrée (" + std::to_string(K.rows) + "×" + std::to_string(K.cols) + ")");
    if (static_cast<int>(F.size()) != n)
        return fail(error, "dimensions incompatibles : K est " + std::to_string(n) + "×" + std::to_string(n) + ", F a " + std::to_string(F.size()) + " composantes");
    if (nf + nc != n || static_cast<int>(bc.imposed.size()) != nc)
        return fail(error, "partition des DDL incompatible avec K (" + std::to_string(nf) + " libres + " + std::to_string(nc) + " imposés ≠ " + std::to_string(n) + ")");
    out = ReducedSystem {};
    auto sub = [&](const std::vector<int>& r, const std::vector<int>& c) {
        Matrix m(static_cast<int>(r.size()), static_cast<int>(c.size()));
        for (std::size_t i = 0; i < r.size(); ++i)
            for (std::size_t j = 0; j < c.size(); ++j) m(static_cast<int>(i), static_cast<int>(j)) = K(r[i], c[j]);
        return m;
    };
    out.Kff = sub(bc.freeDofs, bc.freeDofs);
    out.Kfc = sub(bc.freeDofs, bc.constrainedDofs);
    out.Kcf = sub(bc.constrainedDofs, bc.freeDofs);
    out.Kcc = sub(bc.constrainedDofs, bc.constrainedDofs);
    for (int k : bc.freeDofs) out.Ff.push_back(F[static_cast<std::size_t>(k)]);
    for (int k : bc.constrainedDofs) out.Fc.push_back(F[static_cast<std::size_t>(k)]);
    out.dc = bc.imposed;
    out.rhs = out.Ff;
    if (nf > 0 && nc > 0)
    {
        const Vector kd = numerics::LinAlg::multiply(out.Kfc, out.dc);
        for (int i = 0; i < nf; ++i) out.rhs[static_cast<std::size_t>(i)] -= kd[static_cast<std::size_t>(i)];
    }
    return true;
}

// -----------------------------------------------------------------------------
// 5. Résolution et diagnostic
// -----------------------------------------------------------------------------

namespace
{
/// Le mode (DDL libres, nuls ailleurs) est-il un mouvement de corps rigide ux = a − θ·y, uy = b + θ·x, θz = θ ?
bool rigidBodyFit(const Vector& full, const Model2D& model, const DofNumbering& dofs, double& a, double& b, double& theta)
{
    numerics::Matrix AtA(3, 3);
    Vector Atu(3, 0.0);
    double norm2 = 0.0;
    for (std::size_t k = 0; k < dofs.nodeIds.size(); ++k)
    {
        const Node2D* n = model.node(dofs.nodeIds[k]);
        if (!n) return false;
        const double rows[3][3] = { { 1.0, 0.0, -n->y }, { 0.0, 1.0, n->x }, { 0.0, 0.0, 1.0 } };
        for (int c = 0; c < 3; ++c)
        {
            const double u = full[3 * k + static_cast<std::size_t>(c)];
            norm2 += u * u;
            for (int i = 0; i < 3; ++i)
            {
                Atu[static_cast<std::size_t>(i)] += rows[c][i] * u;
                for (int j = 0; j < 3; ++j) AtA(i, j) += rows[c][i] * rows[c][j];
            }
        }
    }
    Vector p;
    numerics::SolverSettings s;
    s.method = numerics::SolverMethod::GaussLU;
    if (!numerics::solve(AtA, Atu, p, s).success) return false;
    a = p[0], b = p[1], theta = p[2];
    double res2 = 0.0;
    for (std::size_t k = 0; k < dofs.nodeIds.size(); ++k)
    {
        const Node2D* n = model.node(dofs.nodeIds[k]);
        const double fit[3] = { a - theta * n->y, b + theta * n->x, theta };
        for (int c = 0; c < 3; ++c)
        {
            const double r = full[3 * k + static_cast<std::size_t>(c)] - fit[c];
            res2 += r * r;
        }
    }
    return norm2 > 0.0 && std::sqrt(res2) <= 1e-6 * std::sqrt(norm2);
}
} // namespace

DisplacementSolve solveDisplacements(const ReducedSystem& system, const BoundaryConditions& bc, const DofNumbering& dofs,
                                     const Model2D* model, numerics::SolverMethod method)
{
    DisplacementSolve s;
    const int nf = system.Kff.rows;
    if (nf == 0)
    {
        s.success = true;
        s.message = "Aucun DDL libre : tous les déplacements sont imposés.";
        return s;
    }
    if (system.Kff.cols != nf || static_cast<int>(system.rhs.size()) != nf || static_cast<int>(bc.freeDofs.size()) != nf)
    {
        s.message = "Système réduit incohérent : K_ff est " + std::to_string(system.Kff.rows) + "×" + std::to_string(system.Kff.cols)
                    + ", second membre de " + std::to_string(system.rhs.size()) + " composantes, " + std::to_string(bc.freeDofs.size()) + " DDL libres.";
        return s;
    }
    numerics::SolverSettings settings;
    settings.method = method;
    s.report = numerics::solve(system.Kff, system.rhs, s.df, settings);
    if (s.report.success)
    {
        s.success = true;
        if (nf <= kConditionLimit) s.conditionNumber = numerics::conditionNumber(system.Kff);
        if (s.conditionNumber > kIllConditioned)
            s.warnings.push_back("système mal conditionné (κ(K_ff) ≈ " + num(s.conditionNumber, 3) + ", environ "
                                 + std::to_string(static_cast<int>(16.0 - std::log10(s.conditionNumber))) + " chiffres significatifs fiables) : "
                                 + "rigidités d'ordres de grandeur très différents ou éléments très courts ; résultats à vérifier");
        if (s.report.relativeResidual > 1e-8)
            s.warnings.push_back("résidu relatif élevé après résolution : " + num(s.report.relativeResidual, 3));
        s.message = std::string("Résolution par ") + numerics::solverMethodName(method) + " : " + std::to_string(nf) + " équation(s), résidu relatif "
                    + num(s.report.relativeResidual, 3) + (s.conditionNumber > 0.0 ? ", conditionnement " + num(s.conditionNumber, 3) : std::string()) + ".";
        return s;
    }

    // Échec : système singulier (mécanisme) ou non défini positif. Pivot fautif, puis mode à énergie nulle.
    const int e = s.report.failedEquation;
    if (e >= 0 && e < nf) s.failedDof = dofs.label(bc.freeDofs[static_cast<std::size_t>(e)]);
    std::ostringstream m;
    m << "Système singulier : K_ff n'est pas définie positive (" << s.report.message << ")";
    if (!s.failedDof.empty()) m << " — DDL " << s.failedDof;
    m << ".";
    if (nf <= 2 * kConditionLimit)
    {
        const auto eig = numerics::symmetricEigen(system.Kff);
        if (eig.success && !eig.values.empty())
        {
            int k0 = 0;
            double lambdaMax = 0.0;
            for (int k = 0; k < static_cast<int>(eig.values.size()); ++k)
            {
                lambdaMax = std::max(lambdaMax, std::abs(eig.values[static_cast<std::size_t>(k)]));
                if (std::abs(eig.values[static_cast<std::size_t>(k)]) < std::abs(eig.values[static_cast<std::size_t>(k0)])) k0 = k;
            }
            const double lambdaMin = std::abs(eig.values[static_cast<std::size_t>(k0)]);
            s.mechanismMode.assign(static_cast<std::size_t>(nf), 0.0);
            double peak = 0.0;
            for (int i = 0; i < nf; ++i) peak = std::max(peak, std::abs(eig.vectors(i, k0)));
            Vector full(static_cast<std::size_t>(dofs.size()), 0.0);
            for (int i = 0; i < nf; ++i)
            {
                const double phi = peak > 0.0 ? eig.vectors(i, k0) / peak : 0.0;
                s.mechanismMode[static_cast<std::size_t>(i)] = phi;
                full[static_cast<std::size_t>(bc.freeDofs[static_cast<std::size_t>(i)])] = phi;
                if (std::abs(phi) >= 0.1) s.mechanismDofs.push_back(dofs.label(bc.freeDofs[static_cast<std::size_t>(i)]));
            }
            double a = 0.0, b = 0.0, theta = 0.0;
            if (lambdaMax > 0.0 && lambdaMin > 1e-13 * lambdaMax)
            {
                // Plus petite valeur propre non nulle à la précision du calcul : pas de mécanisme, mais un système
                // si mal conditionné que la factorisation le juge singulier (pivot relatif < tolérance).
                s.mechanismKind = "ill-conditioned";
                s.conditionNumber = lambdaMax / lambdaMin;
                m << " Aucun mode à énergie nulle : système quasi singulier (κ(K_ff) ≈ " << num(s.conditionNumber, 3)
                  << ") — rigidités d'ordres de grandeur très différents (sections, longueurs ou modules à vérifier).";
            }
            else if (model && rigidBodyFit(full, *model, dofs, a, b, theta))
            {
                s.mechanismKind = "rigid-body";
                m << " Mode de corps rigide : ";
                if (std::abs(theta) * std::max(1.0, model->extent()) < 1e-6 * std::max(std::abs(a), std::abs(b)))
                    m << (std::abs(a) >= std::abs(b) ? "translation de toute la structure suivant X" : "translation de toute la structure suivant Y");
                else
                    m << "rotation de toute la structure autour du point (" << num(-b / theta, 4) << " ; " << num(a / theta, 4) << ") m";
                m << " — appuis insuffisants : ajouter un appui qui bloque ce mouvement.";
            }
            else
            {
                std::map<int, int> perNode;
                for (int i = 0; i < nf; ++i)
                    if (std::abs(s.mechanismMode[static_cast<std::size_t>(i)]) >= 0.1)
                        ++perNode[dofs.nodeIds[static_cast<std::size_t>(bc.freeDofs[static_cast<std::size_t>(i)] / 3)]];
                if (model && perNode.size() == 1 && model->elementCount(perNode.begin()->first) == 0)
                {
                    s.mechanismKind = "isolated-node";
                    m << " Le nœud N" << perNode.begin()->first << " n'est relié à aucun élément : ses DDL n'ont aucune rigidité (le relier ou le supprimer).";
                }
                else
                {
                    s.mechanismKind = "internal";
                    m << " Mécanisme interne.";
                }
            }
            if (!s.mechanismDofs.empty())
            {
                m << " DDL du mode :";
                for (std::size_t i = 0; i < s.mechanismDofs.size() && i < 12; ++i) m << " " << s.mechanismDofs[i];
                if (s.mechanismDofs.size() > 12) m << " …";
                m << ".";
            }
        }
    }
    m << " Causes possibles : appuis insuffisants, nœud non relié, élément de rigidité nulle ou données incohérentes.";
    s.message = m.str();
    return s;
}

// -----------------------------------------------------------------------------
// 6-7. Déplacements, réactions, équilibre
// -----------------------------------------------------------------------------

bool fullDisplacements(const BoundaryConditions& bc, const Vector& df, int size, Vector& d, std::string* error)
{
    if (df.size() != bc.freeDofs.size())
        return fail(error, "d_f a " + std::to_string(df.size()) + " composantes pour " + std::to_string(bc.freeDofs.size()) + " DDL libres");
    if (static_cast<int>(bc.freeDofs.size() + bc.constrainedDofs.size()) != size || bc.imposed.size() != bc.constrainedDofs.size())
        return fail(error, "partition des DDL incompatible avec la numérotation (" + std::to_string(size) + " DDL)");
    d.assign(static_cast<std::size_t>(size), 0.0);
    for (std::size_t k = 0; k < bc.freeDofs.size(); ++k) d[static_cast<std::size_t>(bc.freeDofs[k])] = df[k];
    for (std::size_t k = 0; k < bc.constrainedDofs.size(); ++k) d[static_cast<std::size_t>(bc.constrainedDofs[k])] = bc.imposed[k];
    return true;
}

bool reactions(const Matrix& K, const Vector& d, const Vector& F, Vector& R, std::string* error)
{
    if (K.rows != K.cols || static_cast<int>(d.size()) != K.cols || static_cast<int>(F.size()) != K.rows)
        return fail(error, "dimensions incompatibles : K " + std::to_string(K.rows) + "×" + std::to_string(K.cols) + ", d "
                               + std::to_string(d.size()) + ", F " + std::to_string(F.size()));
    R = numerics::LinAlg::multiply(K, d);
    for (std::size_t k = 0; k < R.size(); ++k) R[k] -= F[k];
    return true;
}

bool EquilibriumCheck::balanced(double tolerance) const
{
    return relativeForceResidual <= tolerance && relativeMomentResidual <= tolerance;
}

std::string EquilibriumCheck::text() const
{
    std::ostringstream o;
    o.precision(6);
    o << "Charges : ΣFx = " << loadFx << " kN, ΣFy = " << loadFy << " kN, ΣM/O = " << loadMz << " kN·m\n"
      << "Réactions : ΣRx = " << reactionFx << " kN, ΣRy = " << reactionFy << " kN, ΣM/O = " << reactionMz << " kN·m\n"
      << "Écarts : " << residualFx << " kN, " << residualFy << " kN, " << residualMz << " kN·m (relatifs "
      << relativeForceResidual << " et " << relativeMomentResidual << ")";
    return o.str();
}

double referenceForce(const Matrix& K, const Vector& d, const Vector& F)
{
    double ref = F.empty() ? 0.0 : numerics::LinAlg::normInf(F);
    if (K.rows == K.cols && static_cast<int>(d.size()) == K.cols && K.cols > 0)
        ref = std::max(ref, numerics::LinAlg::normInf(numerics::LinAlg::multiply(K, d)));
    return ref;
}

EquilibriumCheck checkEquilibrium(const Model2D& model, const DofNumbering& dofs, const BoundaryConditions& bc, const LoadCase& loads, const Vector& R,
                                  double reference, const Vector& d)
{
    EquilibriumCheck q;
    double loadAbs = 0.0, reactionAbs = 0.0, momentAbs = 0.0;
    for (const auto& p : loads.nodalLoads)
    {
        const Node2D* n = model.node(p.node);
        if (!n) continue;
        q.loadFx += p.fx, q.loadFy += p.fy;
        q.loadMz += p.mz + n->x * p.fy - n->y * p.fx;
        loadAbs += std::abs(p.fx) + std::abs(p.fy);
        momentAbs += std::abs(p.mz) + std::abs(n->x * p.fy) + std::abs(n->y * p.fx);
    }
    for (const auto& def : model.elements)
    {
        FrameElement2D el;
        if (!makeElement(model, def, el)) continue;
        const auto r = el.loadResultant(el.localLoads(loads.elementLoads));
        q.loadFx += r.fx, q.loadFy += r.fy, q.loadMz += r.mz;
        loadAbs += std::abs(r.fx) + std::abs(r.fy);
        momentAbs += std::abs(r.mz);
    }
    // Réactions : appuis (DDL imposés, hors rotations éliminées) et ressorts (−k·u sur les DDL libres)
    std::vector<std::pair<int, double>> forces;
    for (int c : bc.constrainedDofs)
        if (c >= 0 && c < static_cast<int>(R.size()) && !bc.isEliminated(c)) forces.emplace_back(c, R[static_cast<std::size_t>(c)]);
    if (static_cast<int>(d.size()) == dofs.size())
    {
        const Vector fs = springForces(model, dofs, d);
        for (int k = 0; k < dofs.size(); ++k)
            if (fs[static_cast<std::size_t>(k)] != 0.0) forces.emplace_back(k, fs[static_cast<std::size_t>(k)]);
    }
    for (const auto& [c, r] : forces)
    {
        const Node2D* n = model.node(dofs.nodeIds[static_cast<std::size_t>(c / 3)]);
        if (!n) continue;
        switch (c % 3)
        {
        case 0:
            q.reactionFx += r, q.reactionMz -= n->y * r;
            reactionAbs += std::abs(r), momentAbs += std::abs(n->y * r);
            break;
        case 1:
            q.reactionFy += r, q.reactionMz += n->x * r;
            reactionAbs += std::abs(r), momentAbs += std::abs(n->x * r);
            break;
        default:
            q.reactionMz += r;
            momentAbs += std::abs(r);
            break;
        }
    }
    q.residualFx = q.loadFx + q.reactionFx;
    q.residualFy = q.loadFy + q.reactionFy;
    q.residualMz = q.loadMz + q.reactionMz;
    const double tiny = std::numeric_limits<double>::min();
    q.forceScale = std::max({ loadAbs, reactionAbs, reference, tiny });
    q.momentScale = std::max({ momentAbs, q.forceScale * std::max(1.0, model.extent()), tiny });
    q.relativeForceResidual = std::hypot(q.residualFx, q.residualFy) / q.forceScale;
    q.relativeMomentResidual = std::abs(q.residualMz) / q.momentScale;
    return q;
}

// -----------------------------------------------------------------------------
// 8. Résultats par élément
// -----------------------------------------------------------------------------

bool elementResults(const Model2D& model, const DofNumbering& dofs, const LoadCase& loads, const Vector& d, int points,
                    std::vector<ElementResult>& out, std::string* error)
{
    out.clear();
    if (static_cast<int>(d.size()) != dofs.size())
        return fail(error, "d a " + std::to_string(d.size()) + " composantes pour " + std::to_string(dofs.size()) + " DDL");
    for (const auto& def : model.elements)
    {
        FrameElement2D el;
        if (!makeElement(model, def, el, error)) return false;
        ElementResult r;
        r.element = def.id;
        const auto k = dofs.elementDofs(def.nodeI, def.nodeJ);
        r.dGlobal.assign(6, 0.0);
        for (std::size_t a = 0; a < 6; ++a)
        {
            if (k[a] < 0) return fail(error, "élément " + std::to_string(def.id) + " : nœud absent de la numérotation");
            r.dGlobal[a] = d[static_cast<std::size_t>(k[a])];
        }
        std::vector<std::string> issues;
        const auto local = el.localLoads(loads.elementLoads, &issues);
        if (!issues.empty()) return fail(error, issues.front());
        r.dLocal = el.toLocal(r.dGlobal);
        r.fixedEnd = el.fixedEndForces(local);
        r.endForces = el.endForces(r.dGlobal, local);
        r.endForcesGlobal = el.toGlobal(r.endForces);
        r.diagram = el.diagram(r.dGlobal, local, points);
        out.push_back(std::move(r));
    }
    return true;
}

// -----------------------------------------------------------------------------
// Calcul complet
// -----------------------------------------------------------------------------

std::array<double, 3> AnalysisResult2D::nodeDisplacement(int nodeId) const
{
    const int p = dofs.nodePosition(nodeId);
    if (p < 0 || d.size() < static_cast<std::size_t>(3 * p + 3)) return { 0.0, 0.0, 0.0 };
    return { d[static_cast<std::size_t>(3 * p)], d[static_cast<std::size_t>(3 * p + 1)], d[static_cast<std::size_t>(3 * p + 2)] };
}

std::array<double, 3> AnalysisResult2D::nodeReaction(int nodeId) const
{
    std::array<double, 3> r { 0.0, 0.0, 0.0 };
    const int p = dofs.nodePosition(nodeId);
    if (p < 0 || R.size() < static_cast<std::size_t>(3 * p + 3)) return r;
    for (int c = 0; c < 3; ++c)
        if (bc.isConstrained(3 * p + c) && !bc.isEliminated(3 * p + c)) r[static_cast<std::size_t>(c)] = R[static_cast<std::size_t>(3 * p + c)];
    return r;
}

std::array<double, 3> AnalysisResult2D::nodeSpringForce(int nodeId) const
{
    std::array<double, 3> r { 0.0, 0.0, 0.0 };
    const int p = dofs.nodePosition(nodeId);
    if (p < 0 || springs.size() < static_cast<std::size_t>(3 * p + 3)) return r;
    for (int c = 0; c < 3; ++c) r[static_cast<std::size_t>(c)] = springs[static_cast<std::size_t>(3 * p + c)];
    return r;
}

const ElementResult* AnalysisResult2D::element(int id) const
{
    for (const auto& e : elements)
        if (e.element == id) return &e;
    return nullptr;
}

bool assembleResult(const Model2D& model, const LoadCase& loads, const DofNumbering& dofs, const BoundaryConditions& bc,
                    const Matrix& K, const Vector& F, const Vector& d, int diagramPoints, AnalysisResult2D& out, std::string* error)
{
    out.model = model;
    out.loads = loads;
    out.dofs = dofs;
    out.bc = bc;
    out.K = K;
    out.F = F;
    out.d = d;
    if (!reactions(K, d, F, out.R, error)) return false;
    if (!elementResults(model, dofs, loads, d, diagramPoints, out.elements, error)) return false;
    out.springs = springForces(model, dofs, d);
    out.equilibrium = checkEquilibrium(model, dofs, bc, loads, out.R, referenceForce(K, d, F), d);
    out.success = true;
    return true;
}

AnalysisResult2D solveLinearStatic(const Model2D& model, const LoadSelection& selection, const SolveOptions& options)
{
    AnalysisResult2D r;
    r.model = model;
    auto stop = [&](const std::string& m) {
        r.success = false;
        r.message = m;
        r.log.push_back("Échec : " + m);
        return r;
    };
    const ValidationReport check = validate(model);
    for (const auto& w : check.warnings) r.log.push_back("Avertissement : " + w);
    if (!check.ok())
    {
        std::string all;
        for (const auto& e : check.errors) all += (all.empty() ? "" : " ; ") + e;
        return stop("modèle invalide : " + all);
    }
    std::string error;
    if (!resolveLoads(model, selection, r.loads, &error)) return stop(error);
    r.title = selection.combinationId > 0 ? "combinaison « " + r.loads.name + " »"
                                          : "cas " + std::to_string(r.loads.id) + (r.loads.name.empty() ? std::string() : " « " + r.loads.name + " »");
    r.dofs = numberDofs(model);
    r.log.push_back("1. Numérotation : " + std::to_string(model.nodes.size()) + " nœud(s), " + std::to_string(r.dofs.size()) + " DDL.");
    if (!assembleStiffness(model, r.dofs, r.K, &error)) return stop(error);
    if (!assembleLoads(model, r.dofs, r.loads, r.F, &error)) return stop(error);
    r.log.push_back("2-3. Assemblage : K " + std::to_string(r.K.rows) + "×" + std::to_string(r.K.cols) + " (" + std::to_string(model.elements.size())
                    + " élément(s)), F (" + r.title + ").");
    if (!boundaryConditions(model, r.dofs, r.loads, r.bc, &error)) return stop(error);
    if (!reduce(r.K, r.F, r.bc, r.reduced, &error)) return stop(error);
    r.log.push_back("4. Conditions aux limites : " + std::to_string(r.bc.freeDofs.size()) + " DDL libre(s), " + std::to_string(r.bc.constrainedDofs.size())
                    + " imposé(s).");
    for (int e : r.bc.eliminatedDofs)
        r.log.push_back("   " + r.dofs.label(e) + " éliminé : rotation sans rigidité (toutes les barres articulées au nœud).");
    r.solve = solveDisplacements(r.reduced, r.bc, r.dofs, &model, options.method);
    r.log.push_back("5. " + r.solve.message);
    for (const auto& w : r.solve.warnings) r.log.push_back("Avertissement : " + w);
    if (!r.solve.success) return stop(r.solve.message);
    if (!fullDisplacements(r.bc, r.solve.df, r.dofs.size(), r.d, &error)) return stop(error);
    if (!reactions(r.K, r.d, r.F, r.R, &error)) return stop(error);
    if (!elementResults(model, r.dofs, r.loads, r.d, options.diagramPoints, r.elements, &error)) return stop(error);
    r.springs = springForces(model, r.dofs, r.d);
    r.equilibrium = checkEquilibrium(model, r.dofs, r.bc, r.loads, r.R, referenceForce(r.K, r.d, r.F), r.d);
    r.log.push_back("6-8. Déplacements, réactions et efforts de " + std::to_string(r.elements.size()) + " élément(s) ; équilibre : écarts relatifs "
                    + num(r.equilibrium.relativeForceResidual, 3) + " (forces) et " + num(r.equilibrium.relativeMomentResidual, 3) + " (moments).");
    r.success = true;
    r.message = "Calcul terminé (" + r.title + ").";
    return r;
}

double ResultComparison::maxDeviation() const
{
    return std::max({ displacementDeviation, reactionDeviation, forceDeviation });
}

ResultComparison compareResults(const AnalysisResult2D& a, const AnalysisResult2D& ref)
{
    ResultComparison c;
    if (!a.success || !ref.success)
    {
        c.message = "résultat absent ou en échec";
        return c;
    }
    if (a.d.size() != ref.d.size() || a.R.size() != ref.R.size() || a.elements.size() != ref.elements.size())
    {
        c.message = "résultats non comparables (DDL ou éléments différents)";
        return c;
    }
    auto deviation = [](const Vector& x, const Vector& r) {
        double diff = 0.0, scale = 0.0;
        for (std::size_t k = 0; k < r.size(); ++k)
        {
            diff = std::max(diff, std::abs(x[k] - r[k]));
            scale = std::max(scale, std::abs(r[k]));
        }
        return scale > 0.0 ? diff / scale : diff;
    };
    c.displacementDeviation = deviation(a.d, ref.d);
    Vector ra, rr;
    for (int k : ref.bc.constrainedDofs)
    {
        ra.push_back(a.R[static_cast<std::size_t>(k)]);
        rr.push_back(ref.R[static_cast<std::size_t>(k)]);
    }
    c.reactionDeviation = deviation(ra, rr);
    Vector fa, fr;
    for (std::size_t e = 0; e < ref.elements.size(); ++e)
    {
        fa.insert(fa.end(), a.elements[e].endForces.begin(), a.elements[e].endForces.end());
        fr.insert(fr.end(), ref.elements[e].endForces.begin(), ref.elements[e].endForces.end());
    }
    c.forceDeviation = deviation(fa, fr);
    c.comparable = true;
    c.message = "écart maximal relatif " + num(c.maxDeviation(), 3);
    return c;
}

} // namespace tsalab::structural
