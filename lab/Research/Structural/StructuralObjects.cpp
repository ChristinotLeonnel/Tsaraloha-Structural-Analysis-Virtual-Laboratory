// Descriptions des objets de la programmation structurelle (journal, valeurs produites, vues).

#include "StructuralObjects.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace TSALab::Research::Structural
{

namespace
{
std::string num(double v, int precision = 6)
{
    std::ostringstream o;
    o.precision(precision);
    o << (v == 0.0 ? 0.0 : v);   // pas de « -0 »
    return o.str();
}

/// Tableau à colonnes alignées : en-tête de colonnes facultatif, libellés de lignes facultatifs.
std::string table(const Matrix& m, const std::vector<std::string>& rows, const std::vector<std::string>& cols)
{
    std::ostringstream o;
    const int width = 12;
    std::size_t rowWidth = 0;
    for (const auto& r : rows) rowWidth = std::max(rowWidth, r.size());
    rowWidth = std::min<std::size_t>(rowWidth, 12);
    auto cell = [&](const std::string& s) {
        std::string t = s.size() > static_cast<std::size_t>(width - 1) ? s.substr(0, static_cast<std::size_t>(width - 1)) : s;
        o << std::setw(width) << t;
    };
    if (!cols.empty())
    {
        o << std::string(rowWidth + (rowWidth ? 1 : 0), ' ');
        for (int j = 0; j < m.cols; ++j) cell(j < static_cast<int>(cols.size()) ? cols[static_cast<std::size_t>(j)] : std::to_string(j));
        o << "\n";
    }
    for (int i = 0; i < m.rows; ++i)
    {
        if (rowWidth)
        {
            std::string r = i < static_cast<int>(rows.size()) ? rows[static_cast<std::size_t>(i)] : std::to_string(i);
            if (r.size() > rowWidth) r = r.substr(0, rowWidth);
            o << std::left << std::setw(static_cast<int>(rowWidth)) << r << std::right << " ";
        }
        for (int j = 0; j < m.cols; ++j) cell(num(m(i, j), 5));
        o << "\n";
    }
    return o.str();
}

std::string nodeName(int id) { return "N" + std::to_string(id); }

std::string dofsText(const S::SupportCondition& s)
{
    std::string t;
    if (s.fixUx) t += "ux ";
    if (s.fixUy) t += "uy ";
    if (s.fixRz) t += "θz ";
    if (t.empty()) return "aucun DDL bloqué";
    t.pop_back();
    return t + " bloqué(s)";
}
} // namespace

std::vector<std::string> elementDofLabels(const S::FrameElement2D& e)
{
    const std::string i = nodeName(e.nodeI().id), j = nodeName(e.nodeJ().id);
    return { i + ".ux", i + ".uy", i + ".θz", j + ".ux", j + ".uy", j + ".θz" };
}

std::vector<std::string> dofLabels(const S::DofNumbering& numbering, const std::vector<int>* dofs)
{
    std::vector<std::string> out;
    if (dofs)
        for (int d : *dofs) out.push_back(numbering.label(d));
    else
        for (int d = 0; d < numbering.size(); ++d) out.push_back(numbering.label(d));
    return out;
}

// --- Matrices et vecteurs ------------------------------------------------------------------------

std::string summary(const LabeledMatrix& m)
{
    std::ostringstream o;
    o << "Matrice " << m.matrix.rows << "×" << m.matrix.cols;
    if (!m.title.empty()) o << " « " << m.title << " »";
    if (m.matrix.rows == m.matrix.cols && m.matrix.rows > 0)
        o << (tsalab::numerics::LinAlg::asymmetry(m.matrix) <= 1e-12 ? ", symétrique" : ", non symétrique");
    return o.str();
}

std::string details(const LabeledMatrix& m)
{
    return summary(m) + "\n" + table(m.matrix, m.rows, m.cols);
}

std::string summary(const LabeledVector& v)
{
    std::ostringstream o;
    o << "Vecteur de " << v.vector.size() << " composante(s)";
    if (!v.title.empty()) o << " « " << v.title << " »";
    if (!v.vector.empty()) o << ", max |x| = " << num(tsalab::numerics::LinAlg::normInf(v.vector), 5);
    return o.str();
}

std::string details(const LabeledVector& v)
{
    Matrix column(static_cast<int>(v.vector.size()), 1);
    for (std::size_t k = 0; k < v.vector.size(); ++k) column(static_cast<int>(k), 0) = v.vector[k];
    std::vector<std::string> rows = v.labels;
    if (rows.empty())
        for (std::size_t k = 0; k < v.vector.size(); ++k) rows.push_back(std::to_string(k));
    return summary(v) + "\n" + table(column, rows, {});
}

// --- Modèle ------------------------------------------------------------------------------------

std::string summary(const S::Model2D& m)
{
    std::ostringstream o;
    o << "Modèle" << (m.name.empty() ? "" : " « " + m.name + " »") << " : " << m.nodes.size() << " nœud(s), " << m.elements.size() << " élément(s), "
      << m.supports.size() << " appui(s), " << m.loadCases.size() << " cas, " << m.combinations.size() << " combinaison(s)";
    return o.str();
}

std::string details(const S::Model2D& m)
{
    std::ostringstream o;
    o << summary(m) << "\n\nNœuds (m) :\n";
    for (const auto& n : m.nodes) o << "  " << nodeName(n.id) << "  x = " << num(n.x) << "  y = " << num(n.y) << (n.name.empty() ? "" : "  " + n.name) << "\n";
    o << "\nÉléments :\n";
    for (const auto& e : m.elements)
        o << "  " << e.id << "  " << nodeName(e.nodeI) << " → " << nodeName(e.nodeJ) << "  E = " << num(e.material.E) << " kN/m²  A = " << num(e.section.A)
          << " m²  Iz = " << num(e.section.Iz) << " m⁴" << (e.section.name.empty() ? "" : "  (" + e.section.name + ")") << "\n";
    o << "\nAppuis :\n";
    for (const auto& s : m.supports) o << "  " << nodeName(s.node) << " : " << dofsText(s) << "\n";
    for (const auto& c : m.loadCases) o << "\n" << details(c) << "\n";
    for (const auto& k : m.combinations) o << "\n" << details(k) << "\n";
    return o.str();
}

std::string summary(const S::Node2D& n)
{
    return "Nœud " + nodeName(n.id) + " (" + num(n.x) + " ; " + num(n.y) + ") m" + (n.name.empty() ? "" : " « " + n.name + " »");
}

std::string details(const S::Node2D& n)
{
    return summary(n);
}

std::string summary(const S::Material& m)
{
    return "Matériau" + (m.name.empty() ? std::string() : " « " + m.name + " »") + " : E = " + num(m.E) + " kN/m²";
}

std::string details(const S::Material& m)
{
    return summary(m) + "\nα = " + num(m.alpha) + " 1/°C\nγ = " + num(m.unitWeight) + " kN/m³";
}

std::string summary(const S::Section2D& s)
{
    return "Section" + (s.name.empty() ? std::string() : " « " + s.name + " »") + " : A = " + num(s.A) + " m², Iz = " + num(s.Iz) + " m⁴";
}

std::string details(const S::Section2D& s)
{
    return summary(s) + "\nh = " + num(s.h) + " m";
}

std::string summary(const S::SupportCondition& s)
{
    return "Appui " + nodeName(s.node) + " : " + dofsText(s);
}

std::string details(const S::SupportCondition& s)
{
    return summary(s);
}

std::string summary(const S::FrameElement2D& e)
{
    return "Élément " + std::to_string(e.id()) + " : " + nodeName(e.nodeI().id) + " → " + nodeName(e.nodeJ().id) + ", L = " + num(e.length())
           + " m, α = " + num(e.angle() * 180.0 / std::acos(-1.0)) + "°";
}

std::string details(const S::FrameElement2D& e)
{
    std::ostringstream o;
    o << summary(e) << "\n" << details(e.material()) << "\n" << details(e.section()) << "\n";
    const auto issues = e.validate();
    if (!issues.empty())
        for (const auto& i : issues) o << "Invalide : " << i << "\n";
    else
    {
        const auto labels = elementDofLabels(e);
        o << "\nRigidité locale k' :\n" << table(e.localStiffness(), {}, {}) << "\nTransformation T :\n" << table(e.transformation(), {}, {})
          << "\nRigidité globale k = Tᵀ·k'·T :\n" << table(e.globalStiffness(), labels, labels);
    }
    return o.str();
}

// --- Charges ------------------------------------------------------------------------------------

std::string describe(const S::ElementLoad& l)
{
    std::ostringstream o;
    const std::string where = "élément " + std::to_string(l.element == 0 ? 0 : l.element);
    auto pos = [&](double v) { return num(v) + (l.relative ? " L" : " m"); };
    switch (l.type)
    {
    case S::ElementLoadType::Distributed:
        o << "répartie " << S::directionName(l.direction) << (l.projected ? " (projetée)" : "") << " sur l'" << where << " de " << pos(l.a) << " à "
          << (l.b < 0 ? std::string("L") : pos(l.b)) << " : " << num(l.q1) << " → " << num(l.q2) << " kN/m";
        break;
    case S::ElementLoadType::PointForce:
        o << "force " << S::directionName(l.direction) << " sur l'" << where << " en " << pos(l.a) << " : " << num(l.q1) << " kN";
        break;
    case S::ElementLoadType::PointMoment: o << "moment sur l'" << where << " en " << pos(l.a) << " : " << num(l.q1) << " kN·m"; break;
    case S::ElementLoadType::Thermal:
        o << "thermique sur l'" << where << " : ΔT = " << num(l.deltaT) << " °C, gradient = " << num(l.gradientT) << " °C";
        break;
    case S::ElementLoadType::InitialStrain:
        o << "déformation initiale sur l'" << where << " : ε0 = " << num(l.epsilon0) << ", κ0 = " << num(l.kappa0) << " 1/m";
        break;
    case S::ElementLoadType::SelfWeight:
        o << "poids propre " << (l.element == 0 ? std::string("de tous les éléments") : "de l'" + where) << " (× " << num(l.factor) << ")";
        break;
    }
    return o.str();
}

std::string summary(const S::LoadCase& c)
{
    std::ostringstream o;
    if (c.id != 0 || !c.name.empty()) o << "Cas " << c.id << (c.name.empty() ? "" : " « " + c.name + " »") << " : ";
    else o << "Charges : ";
    o << c.nodalLoads.size() << " nodale(s), " << c.elementLoads.size() << " d'élément, " << c.imposedDisplacements.size() << " déplacement(s) imposé(s)";
    return o.str();
}

std::string details(const S::LoadCase& c)
{
    std::ostringstream o;
    o << summary(c) << "\n";
    for (const auto& p : c.nodalLoads)
        o << "  nodale " << nodeName(p.node) << " : Fx = " << num(p.fx) << " kN, Fy = " << num(p.fy) << " kN, Mz = " << num(p.mz) << " kN·m\n";
    for (const auto& l : c.elementLoads) o << "  " << describe(l) << "\n";
    for (const auto& d : c.imposedDisplacements)
        o << "  déplacement imposé " << nodeName(d.node) << "." << S::componentName(d.component) << " = " << num(d.value) << (d.component == 2 ? " rad" : " m") << "\n";
    std::string s = o.str();
    if (!s.empty() && s.back() == '\n') s.pop_back();
    return s;
}

std::string summary(const S::LoadCombination& c)
{
    std::ostringstream o;
    o << "Combinaison " << c.id << (c.name.empty() ? "" : " « " + c.name + " »") << " :";
    bool first = true;
    for (const auto& [id, f] : c.factors)
    {
        o << (first ? " " : " + ") << num(f) << " × cas " << id;
        first = false;
    }
    return o.str();
}

std::string details(const S::LoadCombination& c)
{
    return summary(c);
}

// --- DDL, système, résultats ----------------------------------------------------------------------

std::string summary(const DofSet& d)
{
    std::ostringstream o;
    o << d.numbering.size() << " DDL (" << d.numbering.nodeIds.size() << " nœuds × 3)";
    if (d.partitioned) o << " : " << d.conditions.freeDofs.size() << " libre(s), " << d.conditions.constrainedDofs.size() << " imposé(s)";
    return o.str();
}

std::string details(const DofSet& d)
{
    std::ostringstream o;
    o << summary(d) << "\n\nNumérotation globale (DDL 3k, 3k+1, 3k+2 du k-ième nœud) :\n";
    for (std::size_t k = 0; k < d.numbering.nodeIds.size(); ++k)
        o << "  " << nodeName(d.numbering.nodeIds[k]) << " : ux → " << 3 * k << ", uy → " << 3 * k + 1 << ", θz → " << 3 * k + 2 << "\n";
    if (d.partitioned)
    {
        o << "\nLibres (f) :";
        for (int f : d.conditions.freeDofs) o << " " << d.numbering.label(f);
        o << "\nImposés (c) :";
        for (std::size_t k = 0; k < d.conditions.constrainedDofs.size(); ++k)
            o << " " << d.numbering.label(d.conditions.constrainedDofs[k]) << (d.conditions.imposed[k] != 0.0 ? " = " + num(d.conditions.imposed[k]) : std::string());
        o << "\n";
    }
    return o.str();
}

std::string summary(const ReducedData& r)
{
    std::ostringstream o;
    o << "Système réduit K_ff·d_f = F_f − K_fc·d_c : " << r.system.Kff.rows << " équation(s), " << r.system.dc.size() << " DDL imposé(s)";
    double imposed = 0.0;
    for (double v : r.system.dc) imposed = std::max(imposed, std::abs(v));
    if (imposed > 0.0) o << " (dont déplacements imposés non nuls)";
    return o.str();
}

std::string details(const ReducedData& r)
{
    const auto free = dofLabels(r.numbering, &r.conditions.freeDofs);
    const auto fixed = dofLabels(r.numbering, &r.conditions.constrainedDofs);
    std::ostringstream o;
    o << summary(r) << "\n\nK_ff :\n" << table(r.system.Kff, free, free) << "\nK_fc :\n" << table(r.system.Kfc, free, fixed);
    Matrix rhs(static_cast<int>(r.system.rhs.size()), 3);
    for (std::size_t k = 0; k < r.system.rhs.size(); ++k)
    {
        rhs(static_cast<int>(k), 0) = r.system.Ff[k];
        rhs(static_cast<int>(k), 1) = r.system.Ff[k] - r.system.rhs[k];
        rhs(static_cast<int>(k), 2) = r.system.rhs[k];
    }
    o << "\nSecond membre :\n" << table(rhs, free, { "F_f", "K_fc·d_c", "F_f−K_fc·d_c" });
    return o.str();
}

std::string summary(const S::AnalysisResult2D& r)
{
    if (!r.success) return "Résultat en échec : " + r.message;
    double umax = 0.0;
    for (std::size_t p = 0; p < r.dofs.nodeIds.size(); ++p)
        umax = std::max(umax, std::hypot(r.d[3 * p], r.d[3 * p + 1]));
    std::ostringstream o;
    o << "Résultat (" << r.title << ") : " << r.dofs.size() << " DDL, déplacement maximal " << num(umax * 1000.0, 5) << " mm, équilibre "
      << (r.equilibrium.balanced(1e-9) ? "vérifié" : "NON vérifié") << " (écart " << num(std::max(r.equilibrium.relativeForceResidual, r.equilibrium.relativeMomentResidual), 2)
      << ")";
    return o.str();
}

std::string details(const S::AnalysisResult2D& r)
{
    std::ostringstream o;
    o << summary(r) << "\n";
    for (const auto& l : r.log) o << "  " << l << "\n";
    if (!r.success) return o.str();
    o << "\nDéplacements nodaux (repère global) :\n";
    Matrix d(static_cast<int>(r.dofs.nodeIds.size()), 3);
    std::vector<std::string> nodes;
    for (std::size_t p = 0; p < r.dofs.nodeIds.size(); ++p)
    {
        nodes.push_back(nodeName(r.dofs.nodeIds[p]));
        for (int c = 0; c < 3; ++c) d(static_cast<int>(p), c) = r.d[3 * p + static_cast<std::size_t>(c)];
    }
    o << table(d, nodes, { "ux (m)", "uy (m)", "θz (rad)" });
    o << "\nRéactions d'appui (forces exercées par les appuis) :\n";
    std::vector<std::string> supported;
    std::vector<std::array<double, 3>> reactions;
    for (const auto& s : r.model.supports)
    {
        supported.push_back(nodeName(s.node));
        reactions.push_back(r.nodeReaction(s.node));
    }
    Matrix R(static_cast<int>(supported.size()), 3);
    for (std::size_t k = 0; k < supported.size(); ++k)
        for (int c = 0; c < 3; ++c) R(static_cast<int>(k), c) = reactions[k][static_cast<std::size_t>(c)];
    o << table(R, supported, { "Rx (kN)", "Ry (kN)", "Mz (kN·m)" });
    o << "\nEfforts d'extrémité (repère local, forces exercées sur l'élément) et extremums :\n";
    Matrix f(static_cast<int>(r.elements.size()), 9);
    std::vector<std::string> names;
    for (std::size_t k = 0; k < r.elements.size(); ++k)
    {
        const auto& e = r.elements[k];
        names.push_back("élément " + std::to_string(e.element));
        for (int c = 0; c < 6; ++c) f(static_cast<int>(k), c) = e.endForces[static_cast<std::size_t>(c)];
        f(static_cast<int>(k), 6) = e.diagram.Mmin;
        f(static_cast<int>(k), 7) = e.diagram.Mmax;
        f(static_cast<int>(k), 8) = e.diagram.maxDeflection;
    }
    o << table(f, names, { "Fx_i", "Fy_i", "Mz_i", "Fx_j", "Fy_j", "Mz_j", "M min", "M max", "flèche" });
    o << "\nBilan d'équilibre :\n" << r.equilibrium.text() << "\n";
    return o.str();
}

std::string summary(const DiagramData& d)
{
    const auto& g = d.diagram;
    std::ostringstream o;
    o << (d.title.empty() ? "Diagrammes de l'élément " + std::to_string(g.element) : d.title) << " : N ∈ [" << num(g.Nmin, 4) << " ; " << num(g.Nmax, 4)
      << "] kN, V ∈ [" << num(g.Vmin, 4) << " ; " << num(g.Vmax, 4) << "] kN, M ∈ [" << num(g.Mmin, 4) << " ; " << num(g.Mmax, 4) << "] kN·m";
    return o.str();
}

std::string details(const DiagramData& d)
{
    const auto& g = d.diagram;
    Matrix t(static_cast<int>(g.points.size()), 6);
    for (std::size_t k = 0; k < g.points.size(); ++k)
    {
        const auto& p = g.points[k];
        const double row[6] = { p.x, p.N, p.V, p.M, p.u, p.v };
        for (int c = 0; c < 6; ++c) t(static_cast<int>(k), c) = row[c];
    }
    std::ostringstream o;
    o << summary(d) << "\nM max = " << num(g.Mmax) << " kN·m en x = " << num(g.xMmax) << " m ; M min = " << num(g.Mmin) << " kN·m en x = " << num(g.xMmin)
      << " m ; flèche maximale = " << num(g.maxDeflection) << " m en x = " << num(g.xMaxDeflection) << " m\n"
      << "Convention RDM : N > 0 en traction, M > 0 tend la fibre y' < 0, V = dM/dx.\n\n"
      << table(t, {}, { "x (m)", "N (kN)", "V (kN)", "M (kN·m)", "u (m)", "v (m)" });
    return o.str();
}

std::string summary(const CurveData& c)
{
    return "Courbe" + (c.title.empty() ? std::string() : " « " + c.title + " »") + " : " + std::to_string(c.x.size()) + " point(s)";
}

std::string details(const CurveData& c)
{
    Matrix t(static_cast<int>(c.x.size()), 2);
    for (std::size_t k = 0; k < c.x.size() && k < c.y.size(); ++k)
    {
        t(static_cast<int>(k), 0) = c.x[k];
        t(static_cast<int>(k), 1) = c.y[k];
    }
    return summary(c) + "\n" + table(t, {}, { c.xLabel.empty() ? "x" : c.xLabel, c.yLabel.empty() ? "y" : c.yLabel });
}

// --- Dynamique (pédagogique) et charges roulantes ------------------------------------------------------

namespace
{
/// Tableau limité aux premières et dernières lignes au-delà de `maxRows` (journal lisible).
std::string longTable(const Matrix& m, const std::vector<std::string>& cols, int maxRows = 400)
{
    if (m.rows <= maxRows) return table(m, {}, cols);
    const int half = maxRows / 2;
    Matrix head(half, m.cols), tail(half, m.cols);
    for (int i = 0; i < half; ++i)
        for (int j = 0; j < m.cols; ++j) head(i, j) = m(i, j), tail(i, j) = m(m.rows - half + i, j);
    return table(head, {}, cols) + "  … " + std::to_string(m.rows - 2 * half) + " lignes non affichées …\n" + table(tail, {}, {});
}

Matrix pairsTable(const std::vector<std::pair<double, double>>& pts)
{
    Matrix t(static_cast<int>(pts.size()), 2);
    for (std::size_t k = 0; k < pts.size(); ++k) t(static_cast<int>(k), 0) = pts[k].first, t(static_cast<int>(k), 1) = pts[k].second;
    return t;
}

std::string logText(const std::vector<std::string>& log)
{
    std::string o;
    for (const auto& l : log) o += "  " + l + "\n";
    return o;
}

/// Déplacements nodaux (3 composantes par nœud) en tableau.
std::string nodalTable(const S::DofNumbering& dofs, const Vector& v, const std::vector<std::string>& cols)
{
    Matrix t(static_cast<int>(dofs.nodeIds.size()), 3);
    std::vector<std::string> nodes;
    for (std::size_t p = 0; p < dofs.nodeIds.size(); ++p)
    {
        nodes.push_back(nodeName(dofs.nodeIds[p]));
        for (int c = 0; c < 3; ++c)
            if (3 * p + static_cast<std::size_t>(c) < v.size()) t(static_cast<int>(p), c) = v[3 * p + static_cast<std::size_t>(c)];
    }
    return table(t, nodes, cols);
}
} // namespace

std::string summary(const S::ModalResult& r)
{
    return r.success ? r.message : "Analyse modale en échec : " + r.message;
}

std::string details(const S::ModalResult& r)
{
    std::string o = summary(r) + "\n" + logText(r.log);
    if (!r.success) return o;
    Matrix t(static_cast<int>(r.omega.size()), 6);
    std::vector<std::string> rows;
    for (std::size_t k = 0; k < r.omega.size(); ++k)
    {
        const int i = static_cast<int>(k);
        rows.push_back("mode " + std::to_string(k + 1));
        t(i, 0) = r.omega[k], t(i, 1) = r.frequency[k], t(i, 2) = r.period[k];
        t(i, 3) = r.effectiveMassX[k], t(i, 4) = r.effectiveMassY[k], t(i, 5) = r.participationX[k];
    }
    return o + "\n" + table(t, rows, { "ω (rad/s)", "f (Hz)", "T (s)", "Meff X (t)", "Meff Y (t)", "Γ X" });
}

std::string summary(const S::TimeFunction& f)
{
    if (f.points.empty()) return "Fonction du temps vide";
    double peak = 0.0;
    for (const auto& p : f.points) peak = std::max(peak, std::abs(p.second));
    return "Fonction du temps : " + std::to_string(f.points.size()) + " points de " + num(f.points.front().first) + " à " + num(f.points.back().first)
           + " s, max |valeur| " + num(peak) + " (nulle hors de l'intervalle)";
}

std::string details(const S::TimeFunction& f)
{
    return summary(f) + "\n" + longTable(pairsTable(f.points), { "t (s)", "valeur" });
}

std::string summary(const S::GroundMotion& g)
{
    double peak = 0.0;
    for (const auto& p : g.acceleration.points) peak = std::max(peak, std::abs(p.second));
    return std::string("Mouvement du sol suivant ") + (g.direction == 0 ? "X" : "Y") + " : " + std::to_string(g.acceleration.points.size())
           + " points, pic |a_g| = " + num(peak) + " m/s² — " + (g.source.empty() ? std::string("source non précisée") : g.source);
}

std::string details(const S::GroundMotion& g)
{
    return summary(g) + "\n" + longTable(pairsTable(g.acceleration.points), { "t (s)", "a_g (m/s²)" });
}

std::string summary(const S::TimeHistoryResult& r)
{
    return r.success ? r.message : "Analyse temporelle en échec : " + r.message;
}

std::string details(const S::TimeHistoryResult& r)
{
    std::string o = summary(r) + "\n" + logText(r.log);
    if (!r.success) return o;
    return o + "\nMaxima de |u| sur la durée (relatifs au sol pour un mouvement du sol) :\n"
           + nodalTable(r.dofs, r.peakAbsDisplacement, { "|ux| (m)", "|uy| (m)", "|θz| (rad)" });
}

std::string summary(const S::ResponseSpectrum& s)
{
    if (s.points.empty()) return "Spectre vide";
    return "Spectre de réponse : " + std::to_string(s.points.size()) + " points, T de " + num(s.points.front().first) + " à " + num(s.points.back().first)
           + " s — " + (s.source.empty() ? std::string("fourni par l'utilisateur") : s.source);
}

std::string details(const S::ResponseSpectrum& s)
{
    return summary(s) + "\n" + longTable(pairsTable(s.points), { "T (s)", "Sa (m/s²)" });
}

std::string summary(const S::SpectralResult& r)
{
    return r.success ? r.message : "Analyse spectrale en échec : " + r.message;
}

std::string details(const S::SpectralResult& r)
{
    std::string o = summary(r) + "\n" + logText(r.log);
    if (!r.success) return o;
    o += "\nDéplacements de pointe combinés (valeurs absolues) :\n" + nodalTable(r.dofs, r.peakDisplacement, { "ux (m)", "uy (m)", "θz (rad)" });
    Matrix f(static_cast<int>(r.elementPeaks.size()), 6);
    std::vector<std::string> els;
    for (std::size_t e = 0; e < r.elementPeaks.size(); ++e)
    {
        els.push_back("E" + std::to_string(r.elementPeaks[e].element));
        for (int c = 0; c < 6; ++c) f(static_cast<int>(e), c) = r.elementPeaks[e].endForces[static_cast<std::size_t>(c)];
    }
    return o + "\nEfforts d'extrémité de pointe combinés (repère local, valeurs absolues) :\n"
           + table(f, els, { "|Fx i|", "|Fy i|", "|Mz i|", "|Fx j|", "|Fy j|", "|Mz j|" });
}

std::string summary(const S::Vehicle& v)
{
    double total = 0.0;
    for (const auto& a : v.axles) total += a.load;
    return "Convoi" + (v.name.empty() ? std::string() : " « " + v.name + " »") + " : " + std::to_string(v.axles.size()) + " essieu(x), longueur "
           + num(v.length()) + " m, charge totale " + num(total) + " kN (suivant −Y)";
}

std::string details(const S::Vehicle& v)
{
    Matrix t(static_cast<int>(v.axles.size()), 2);
    for (std::size_t k = 0; k < v.axles.size(); ++k) t(static_cast<int>(k), 0) = v.axles[k].offset, t(static_cast<int>(k), 1) = v.axles[k].load;
    return summary(v) + "\n" + table(t, {}, { "décalage (m)", "charge (kN)" });
}

std::string summary(const S::MovingLoadResult& r)
{
    return r.success ? r.message : "Charges roulantes en échec : " + r.message;
}

std::string details(const S::MovingLoadResult& r)
{
    std::string o = summary(r) + "\n" + logText(r.log);
    if (!r.success) return o;
    Matrix t(static_cast<int>(r.stations.size()), 7);
    std::vector<std::string> rows;
    for (std::size_t k = 0; k < r.stations.size(); ++k)
    {
        const auto& s = r.stations[k];
        rows.push_back("E" + std::to_string(s.element));
        const double v[7] = { s.x, s.Nmin, s.Nmax, s.Vmin, s.Vmax, s.Mmin, s.Mmax };
        for (int c = 0; c < 7; ++c) t(static_cast<int>(k), c) = v[c];
    }
    return o + "\nEnveloppes (kN, kN·m) :\n" + table(t, rows, { "x (m)", "N min", "N max", "V min", "V max", "M min", "M max" });
}

} // namespace TSALab::Research::Structural
