// Dynamique des portiques plans : masse, analyse modale, intégration de Newmark, analyse spectrale (voir Dynamics2D.h).

#include "tsalab/structural/Dynamics2D.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace tsalab::structural
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
/// Taille maximale stockée d'un calcul temporel : (pas + 1) × DDL valeurs par historique.
constexpr double kMaxStoredValues = 5e6;

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

const char* massModelName(MassModel m)
{
    return m == MassModel::Lumped ? "concentrée" : "cohérente";
}

const char* directionLabel(int direction)
{
    return direction == 0 ? "X" : "Y";
}

Matrix subMatrix(const Matrix& a, const std::vector<int>& rows, const std::vector<int>& cols)
{
    Matrix s(static_cast<int>(rows.size()), static_cast<int>(cols.size()));
    for (std::size_t i = 0; i < rows.size(); ++i)
        for (std::size_t j = 0; j < cols.size(); ++j) s(static_cast<int>(i), static_cast<int>(j)) = a(rows[i], cols[j]);
    return s;
}

Vector subVector(const Vector& v, const std::vector<int>& idx)
{
    Vector s(idx.size());
    for (std::size_t i = 0; i < idx.size(); ++i) s[i] = v[static_cast<std::size_t>(idx[i])];
    return s;
}

/// Vecteur d'influence ι d'une translation d'ensemble suivant X (0) ou Y (1), restreint aux DDL libres.
Vector influence(const std::vector<int>& freeDofs, int direction)
{
    Vector iota(freeDofs.size(), 0.0);
    for (std::size_t i = 0; i < freeDofs.size(); ++i)
        if (freeDofs[i] % 3 == direction) iota[i] = 1.0;
    return iota;
}

/// Structure stable : K_ff définie positive, sinon diagnostic du mécanisme (même analyse que le calcul statique).
bool checkStable(const Matrix& K, const BoundaryConditions& bc, const DofNumbering& dofs, const Model2D& model, std::string& message,
                 std::vector<std::string>& warnings)
{
    ReducedSystem rs;
    if (!reduce(K, Vector(static_cast<std::size_t>(K.rows), 0.0), bc, rs, &message)) return false;
    const DisplacementSolve s = solveDisplacements(rs, bc, dofs, &model);
    warnings = s.warnings;
    if (!s.success) message = s.message;
    return s.success;
}

/// Masses nodales sans effet : composante portée par un DDL bloqué (elle suit l'appui) ou une rotation éliminée.
void reportIgnoredMasses(const Model2D& model, const DofNumbering& dofs, const BoundaryConditions& bc, std::vector<std::string>& log)
{
    for (const auto& m : model.nodalMasses)
    {
        const double v[3] = { m.mx, m.my, m.jz };
        for (int c = 0; c < 3; ++c)
        {
            const int d = dofs.dof(m.node, c);
            if (v[c] == 0.0 || d < 0 || !bc.isConstrained(d)) continue;
            log.push_back("Masse nodale " + std::string(c == 2 ? "jz" : (c == 0 ? "mx" : "my")) + " du nœud " + std::to_string(m.node) + " sans effet : "
                          + (bc.isEliminated(d) ? "rotation éliminée (toutes les barres y sont articulées)" : "DDL bloqué (la masse suit l'appui)"));
        }
    }
}

/// Interpolation linéaire dans une table croissante en abscisse ; faux hors de [x_premier, x_dernier] (tolérance relative).
bool interpolate(const std::vector<std::pair<double, double>>& pts, double x, double& y)
{
    if (pts.size() < 2) return false;
    const double x0 = pts.front().first, x1 = pts.back().first;
    const double tol = 1e-9 * std::max({ 1.0, std::abs(x0), std::abs(x1) });
    if (x < x0 - tol || x > x1 + tol) return false;
    x = std::clamp(x, x0, x1);
    auto it = std::upper_bound(pts.begin(), pts.end(), x, [](double v, const std::pair<double, double>& p) { return v < p.first; });
    if (it == pts.begin()) it = std::next(it);
    if (it == pts.end()) it = std::prev(pts.end());
    const auto& b = *it;
    const auto& a = *std::prev(it);
    const double w = (x - a.first) / (b.first - a.first);
    y = a.second + w * (b.second - a.second);
    return true;
}

bool validTable(const std::vector<std::pair<double, double>>& pts, bool nonNegative, const char* xName, std::string* error)
{
    if (pts.size() < 2) return fail(error, "au moins deux points requis");
    for (std::size_t k = 0; k < pts.size(); ++k)
    {
        if (!std::isfinite(pts[k].first) || !std::isfinite(pts[k].second)) return fail(error, "valeur non finie au point " + std::to_string(k + 1));
        if (nonNegative && (pts[k].first < 0.0 || pts[k].second < 0.0))
            return fail(error, "valeur négative au point " + std::to_string(k + 1));
        if (k > 0 && !(pts[k].first > pts[k - 1].first))
            return fail(error, std::string(xName) + " non strictement croissant au point " + std::to_string(k + 1));
    }
    return true;
}
} // namespace

// -----------------------------------------------------------------------------
// Masse
// -----------------------------------------------------------------------------

bool assembleMass(const Model2D& model, const DofNumbering& dofs, MassModel massModel, Matrix& M, std::string* error)
{
    const int n = dofs.size();
    M = Matrix(n, n);
    for (const auto& def : model.elements)
    {
        FrameElement2D el;
        if (!makeElement(model, def, el, error)) return false;
        const auto issues = el.validate();
        if (!issues.empty()) return fail(error, issues.front());
        if (!std::isfinite(def.material.density) || def.material.density < 0.0)
            return fail(error, "élément " + std::to_string(def.id) + " : masse volumique ρ négative ou non finie");
        const Matrix me = el.globalMass(massModel);
        const auto d = dofs.elementDofs(def.nodeI, def.nodeJ);
        for (int a = 0; a < 6; ++a)
        {
            if (d[static_cast<std::size_t>(a)] < 0) return fail(error, "élément " + std::to_string(def.id) + " : nœud absent de la numérotation");
            for (int b = 0; b < 6; ++b) M(d[static_cast<std::size_t>(a)], d[static_cast<std::size_t>(b)]) += me(a, b);
        }
    }
    for (const auto& m : model.nodalMasses)
    {
        const int p = dofs.nodePosition(m.node);
        if (p < 0) return fail(error, "masse nodale au nœud " + std::to_string(m.node) + " absent de la numérotation");
        if (!std::isfinite(m.mx) || !std::isfinite(m.my) || !std::isfinite(m.jz) || m.mx < 0.0 || m.my < 0.0 || m.jz < 0.0)
            return fail(error, "masse nodale du nœud " + std::to_string(m.node) + " négative ou non finie");
        M(3 * p, 3 * p) += m.mx;
        M(3 * p + 1, 3 * p + 1) += m.my;
        M(3 * p + 2, 3 * p + 2) += m.jz;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Analyse modale
// -----------------------------------------------------------------------------

Vector ModalResult::mode(int k) const
{
    if (k < 0 || k >= modes.cols) return {};
    Vector v(static_cast<std::size_t>(modes.rows));
    for (int i = 0; i < modes.rows; ++i) v[static_cast<std::size_t>(i)] = modes(i, k);
    return v;
}

ModalResult modalAnalysis(const Model2D& model, int modeCount, MassModel massModel)
{
    ModalResult r;
    r.model = model;
    r.massModel = massModel;
    const ValidationReport v = validate(model);
    if (!v.ok())
    {
        r.message = "Modèle invalide : " + v.errors.front();
        return r;
    }
    for (const auto& w : v.warnings) r.log.push_back("Avertissement : " + w);
    r.dofs = numberDofs(model);
    std::string err;
    if (!boundaryConditions(model, r.dofs, LoadCase {}, r.bc, &err))
    {
        r.message = "Conditions aux limites : " + err;
        return r;
    }
    Matrix K, M;
    if (!assembleStiffness(model, r.dofs, K, &err) || !assembleMass(model, r.dofs, massModel, M, &err))
    {
        r.message = err;
        return r;
    }
    const auto& f = r.bc.freeDofs;
    if (f.empty())
    {
        r.message = "Aucun DDL libre : tous les déplacements sont bloqués.";
        return r;
    }
    std::vector<std::string> warnings;
    if (!checkStable(K, r.bc, r.dofs, model, err, warnings))
    {
        r.message = "Analyse modale impossible : " + err;
        return r;
    }
    for (const auto& w : warnings) r.log.push_back("Avertissement : " + w);
    reportIgnoredMasses(model, r.dofs, r.bc, r.log);

    const Matrix Kff = subMatrix(K, f, f), Mff = subMatrix(M, f, f);
    if (!(numerics::LinAlg::maxAbs(Mff) > 0.0))
    {
        r.message = "Aucune masse sur les DDL libres : renseigner la masse volumique ρ des matériaux (t/m³) ou des masses nodales.";
        return r;
    }
    const numerics::EigenResult e = numerics::generalizedEigen(Kff, Mff, modeCount);
    if (!e.success || e.values.empty())
    {
        r.message = "Analyse modale impossible : " + (e.message.empty() ? std::string("aucun mode de masse non nulle") : e.message);
        return r;
    }

    const int n = r.dofs.size(), m = static_cast<int>(e.values.size()), nf = static_cast<int>(f.size());
    r.modes = Matrix(n, m);
    const Vector ix = influence(f, 0), iy = influence(f, 1);
    const Vector Mix = numerics::LinAlg::multiply(Mff, ix), Miy = numerics::LinAlg::multiply(Mff, iy);
    r.totalMassX = numerics::LinAlg::dot(ix, Mix);
    r.totalMassY = numerics::LinAlg::dot(iy, Miy);
    r.log.push_back("Analyse modale : " + std::to_string(nf) + " DDL libres, masse " + massModelName(massModel) + ", " + std::to_string(m)
                    + " mode(s) ; masse mobilisable X = " + num(r.totalMassX) + " t, Y = " + num(r.totalMassY) + " t");
    for (int k = 0; k < m; ++k)
    {
        // Signe conventionnel : la plus grande composante (la première en cas d'égalité) est positive
        double big = 0.0;
        for (int i = 0; i < nf; ++i) big = std::max(big, std::abs(e.vectors(i, k)));
        double sign = 1.0;
        for (int i = 0; i < nf; ++i)
            if (std::abs(e.vectors(i, k)) >= (1.0 - 1e-9) * big)
            {
                sign = e.vectors(i, k) < 0.0 ? -1.0 : 1.0;
                break;
            }
        Vector phi(static_cast<std::size_t>(nf));
        for (int i = 0; i < nf; ++i)
        {
            phi[static_cast<std::size_t>(i)] = sign * e.vectors(i, k);
            r.modes(f[static_cast<std::size_t>(i)], k) = phi[static_cast<std::size_t>(i)];
        }
        const double omega = std::sqrt(std::max(e.values[static_cast<std::size_t>(k)], 0.0));
        r.omega.push_back(omega);
        r.frequency.push_back(omega / (2.0 * kPi));
        r.period.push_back(omega > 0.0 ? 2.0 * kPi / omega : std::numeric_limits<double>::infinity());
        const double gx = numerics::LinAlg::dot(phi, Mix), gy = numerics::LinAlg::dot(phi, Miy);
        r.participationX.push_back(gx);
        r.participationY.push_back(gy);
        r.effectiveMassX.push_back(gx * gx);
        r.effectiveMassY.push_back(gy * gy);
        auto pct = [](double a, double total) { return total > 0.0 ? num(100.0 * a / total, 4) + " %" : std::string("—"); };
        r.log.push_back("Mode " + std::to_string(k + 1) + " : ω = " + num(r.omega.back()) + " rad/s, f = " + num(r.frequency.back()) + " Hz, T = "
                        + num(r.period.back()) + " s ; masse effective X " + pct(gx * gx, r.totalMassX) + ", Y " + pct(gy * gy, r.totalMassY));
    }
    double sx = 0.0, sy = 0.0;
    for (int k = 0; k < m; ++k) sx += r.effectiveMassX[static_cast<std::size_t>(k)], sy += r.effectiveMassY[static_cast<std::size_t>(k)];
    r.log.push_back("Masses effectives cumulées : X " + (r.totalMassX > 0.0 ? num(100.0 * sx / r.totalMassX, 4) + " %" : std::string("—")) + ", Y "
                    + (r.totalMassY > 0.0 ? num(100.0 * sy / r.totalMassY, 4) + " %" : std::string("—")));
    r.success = true;
    r.message = "Analyse modale : " + std::to_string(m) + " mode(s), f1 = " + num(r.frequency.front()) + " Hz (T1 = " + num(r.period.front()) + " s).";
    return r;
}

// -----------------------------------------------------------------------------
// Fonctions du temps, spectres
// -----------------------------------------------------------------------------

double TimeFunction::at(double t) const
{
    double y = 0.0;
    return interpolate(points, t, y) ? y : 0.0;
}

bool TimeFunction::valid(std::string* error) const
{
    return validTable(points, false, "temps", error);
}

bool ResponseSpectrum::at(double period, double& sa) const
{
    return interpolate(points, period, sa);
}

bool ResponseSpectrum::valid(std::string* error) const
{
    return validTable(points, true, "période", error);
}

DampingSpec rayleighDamping(double xi, double omega1, double omega2)
{
    DampingSpec d;
    const double s = omega1 + omega2;
    if (!(s > 0.0) || omega1 == omega2) return d;
    d.a0 = 2.0 * xi * omega1 * omega2 / s;
    d.a1 = 2.0 * xi / s;
    return d;
}

bool parseTimeSeries(const std::string& text, double dt, double scale, TimeFunction& out, std::string* error, int* skippedLines)
{
    out = TimeFunction {};
    if (skippedLines) *skippedLines = 0;
    if (!std::isfinite(scale) || scale == 0.0) return fail(error, "facteur d'unité nul ou non fini");
    if (dt < 0.0 || !std::isfinite(dt)) return fail(error, "pas de temps négatif ou non fini");
    std::istringstream in(text);
    std::string line;
    std::vector<double> values;
    int lineNo = 0;
    while (std::getline(in, line))
    {
        ++lineNo;
        for (char& c : line)
            if (c == ',' || c == ';' || c == '\t' || c == '\r') c = ' ';
        std::istringstream ls(line);
        std::vector<double> row;
        std::string tok;
        bool numeric = true;
        while (ls >> tok)
        {
            std::size_t used = 0;
            double v = 0.0;
            try
            {
                v = std::stod(tok, &used);
            }
            catch (...)
            {
                used = 0;
            }
            if (used != tok.size() || !std::isfinite(v))
            {
                numeric = false;
                break;
            }
            row.push_back(v);
        }
        if (row.empty() && numeric) continue;   // ligne vide
        if (!numeric)
        {
            if (skippedLines) ++*skippedLines;
            continue;
        }
        if (dt > 0.0)
            values.insert(values.end(), row.begin(), row.end());
        else
        {
            if (row.size() != 2)
                return fail(error, "ligne " + std::to_string(lineNo) + " : deux colonnes (t, valeur) attendues, " + std::to_string(row.size())
                                       + " trouvée(s) ; indiquer le pas dt pour une série à valeurs successives");
            out.points.emplace_back(row[0], row[1] * scale);
        }
    }
    if (dt > 0.0)
        for (std::size_t k = 0; k < values.size(); ++k) out.points.emplace_back(static_cast<double>(k) * dt, values[k] * scale);
    std::string why;
    if (!out.valid(&why)) return fail(error, "série temporelle invalide : " + why);
    return true;
}

// -----------------------------------------------------------------------------
// Analyse temporelle (Newmark)
// -----------------------------------------------------------------------------

Vector TimeHistoryResult::history(int dof) const
{
    Vector h;
    if (dof < 0 || dof >= dofs.size()) return h;
    h.reserve(displacement.size());
    for (const auto& d : displacement) h.push_back(d[static_cast<std::size_t>(dof)]);
    return h;
}

TimeHistoryResult timeHistory(const Model2D& model, const TimeHistoryOptions& o, const DynamicLoad* load, const GroundMotion* ground,
                              const Vector* initialDisplacement, const Vector* initialVelocity)
{
    TimeHistoryResult r;
    r.model = model;
    auto refuse = [&r](const std::string& message) -> TimeHistoryResult& {
        r.message = message;
        return r;
    };
    const ValidationReport v = validate(model);
    if (!v.ok()) return refuse("Modèle invalide : " + v.errors.front());
    for (const auto& w : v.warnings) r.log.push_back("Avertissement : " + w);
    if (!(o.dt > 0.0) || !std::isfinite(o.dt)) return refuse("pas de temps Δt nul, négatif ou non fini");
    if (!(o.duration > 0.0) || !std::isfinite(o.duration)) return refuse("durée nulle, négative ou non finie");
    if (!(o.beta > 0.0) || !(o.gamma >= 0.5) || !std::isfinite(o.beta) || !std::isfinite(o.gamma))
        return refuse("paramètres de Newmark : β > 0 et γ ≥ 1/2 requis (γ < 1/2 introduit un amortissement numérique négatif)");
    if (!(o.damping.a0 >= 0.0) || !(o.damping.a1 >= 0.0) || !std::isfinite(o.damping.a0) || !std::isfinite(o.damping.a1))
        return refuse("coefficients de Rayleigh a0, a1 négatifs ou non finis");
    const double stepsD = std::round(o.duration / o.dt);
    if (stepsD < 1.0) return refuse("durée inférieure à un pas de temps");
    r.dofs = numberDofs(model);
    const int n = r.dofs.size();
    if ((stepsD + 1.0) * n > kMaxStoredValues)
        return refuse("calcul trop long à stocker : " + num(stepsD, 10) + " pas × " + std::to_string(n) + " DDL dépassent " + num(kMaxStoredValues)
                      + " valeurs par historique (augmenter Δt ou réduire la durée)");
    const int steps = static_cast<int>(stepsD);
    if (std::abs(steps * o.dt - o.duration) > 1e-9 * o.duration)
        r.log.push_back("Durée arrondie à " + std::to_string(steps) + " pas : " + num(steps * o.dt) + " s");

    std::string err;
    Vector F(static_cast<std::size_t>(n), 0.0);
    if (load)
    {
        if (!load->factor.valid(&err)) return refuse("fonction du temps de la charge : " + err);
        if (!resolveLoads(model, LoadSelection { load->caseId, 0 }, r.loads, &err)) return refuse(err);
        if (!r.loads.imposedDisplacements.empty())
            return refuse("déplacements imposés (tassements) non pris en charge en dynamique : l'excitation multi-appuis n'est pas implémentée");
        if (!assembleLoads(model, r.dofs, r.loads, F, &err)) return refuse(err);
    }
    if (ground)
    {
        if (ground->direction != 0 && ground->direction != 1) return refuse("direction du mouvement du sol : 0 (X) ou 1 (Y)");
        if (!ground->acceleration.valid(&err)) return refuse("accélérogramme : " + err);
        double minStep = std::numeric_limits<double>::max();
        const auto& p = ground->acceleration.points;
        for (std::size_t k = 1; k < p.size(); ++k) minStep = std::min(minStep, p[k].first - p[k - 1].first);
        if (o.dt > minStep * (1.0 + 1e-9))
            r.log.push_back("Avertissement : Δt = " + num(o.dt) + " s supérieur au pas de l'accélérogramme (" + num(minStep)
                            + " s) : signal lu par interpolation au pas de calcul, pics possiblement écrêtés");
        r.log.push_back("Mouvement du sol suivant " + std::string(directionLabel(ground->direction)) + " : "
                        + (ground->source.empty() ? std::string("source non précisée") : ground->source) + " ; déplacements relatifs au sol");
    }
    auto nonZero = [](const Vector* x) {
        return x && std::any_of(x->begin(), x->end(), [](double a) { return a != 0.0; });
    };
    if (!load && !ground && !nonZero(initialDisplacement) && !nonZero(initialVelocity))
        return refuse("aucune excitation : charge dynamique, mouvement du sol ou conditions initiales à fournir");

    if (!boundaryConditions(model, r.dofs, LoadCase {}, r.bc, &err)) return refuse("Conditions aux limites : " + err);
    Matrix K, M;
    if (!assembleStiffness(model, r.dofs, K, &err) || !assembleMass(model, r.dofs, o.mass, M, &err)) return refuse(err);
    const auto& f = r.bc.freeDofs;
    const int nf = static_cast<int>(f.size());
    if (nf == 0) return refuse("Aucun DDL libre : tous les déplacements sont bloqués.");
    std::vector<std::string> warnings;
    if (!checkStable(K, r.bc, r.dofs, model, err, warnings)) return refuse("Analyse temporelle impossible : " + err);
    for (const auto& w : warnings) r.log.push_back("Avertissement : " + w);
    reportIgnoredMasses(model, r.dofs, r.bc, r.log);

    const Matrix Kff = subMatrix(K, f, f), Mff = subMatrix(M, f, f);
    const Matrix Cff = numerics::LinAlg::add(numerics::LinAlg::scaled(Mff, o.damping.a0), Kff, o.damping.a1);
    const double mMax = numerics::LinAlg::maxAbs(Mff);
    if (!(mMax > 0.0)) return refuse("Aucune masse sur les DDL libres : renseigner la masse volumique ρ des matériaux (t/m³) ou des masses nodales.");
    std::vector<int> massive, massless;   // indices dans la numérotation libre
    for (int i = 0; i < nf; ++i)
    {
        double row = 0.0;
        for (int j = 0; j < nf; ++j) row = std::max(row, std::abs(Mff(i, j)));
        (row > 1e-12 * mMax ? massive : massless).push_back(i);
    }
    if (!massless.empty() && o.damping.a1 > 0.0)
        return refuse("amortissement proportionnel à la rigidité (a1 > 0) avec " + std::to_string(massless.size())
                      + " DDL sans masse (rotations en masse concentrée…) : équations mixtes non traitées ; utiliser la masse cohérente, "
                        "des inerties de rotation, ou a1 = 0");

    // Conditions initiales (DDL libres ; nulles exigées sur les DDL bloqués)
    Vector u(static_cast<std::size_t>(nf), 0.0), vel(static_cast<std::size_t>(nf), 0.0), acc(static_cast<std::size_t>(nf), 0.0);
    for (int which = 0; which < 2; ++which)
    {
        const Vector* x = which == 0 ? initialDisplacement : initialVelocity;
        if (!x) continue;
        if (static_cast<int>(x->size()) != n)
            return refuse(std::string(which == 0 ? "déplacements" : "vitesses") + " initiaux : " + std::to_string(n) + " composantes attendues");
        for (int k : r.bc.constrainedDofs)
            if ((*x)[static_cast<std::size_t>(k)] != 0.0)
                return refuse(std::string(which == 0 ? "déplacement" : "vitesse") + " initial non nul sur le DDL bloqué " + r.dofs.label(k));
        Vector& dst = which == 0 ? u : vel;
        for (int i = 0; i < nf; ++i) dst[static_cast<std::size_t>(i)] = (*x)[static_cast<std::size_t>(f[static_cast<std::size_t>(i)])];
    }

    const Vector Ff = subVector(F, f);
    const Vector iota = ground ? influence(f, ground->direction) : Vector(static_cast<std::size_t>(nf), 0.0);
    const Vector Miota = numerics::LinAlg::multiply(Mff, iota);
    auto loadAt = [&](double t, double& fac, double& ag) {
        fac = load ? load->factor.at(t) : 0.0;
        ag = ground ? ground->acceleration.at(t) : 0.0;
        Vector p(static_cast<std::size_t>(nf));
        for (int i = 0; i < nf; ++i) p[static_cast<std::size_t>(i)] = fac * Ff[static_cast<std::size_t>(i)] - ag * Miota[static_cast<std::size_t>(i)];
        return p;
    };
    double fac0 = 0.0, ag0 = 0.0;
    const Vector p0 = loadAt(0.0, fac0, ag0);

    // DDL sans masse (a1 = 0 ici) : relation statique K_00·u_0 = p_0 − K_0m·u_m, à chaque pas comme à t = 0
    if (!massless.empty())
    {
        const Matrix K00 = subMatrix(Kff, massless, massless), K0m = subMatrix(Kff, massless, massive);
        Vector rhs = subVector(p0, massless);
        const Vector km = numerics::LinAlg::multiply(K0m, subVector(u, massive));
        for (std::size_t i = 0; i < rhs.size(); ++i) rhs[i] -= km[i];
        numerics::CholeskyFactorization c;
        if (!c.factor(K00)) return refuse("condensation des DDL sans masse impossible : " + c.message);
        const Vector u0 = c.solve(rhs);
        for (std::size_t i = 0; i < massless.size(); ++i)
        {
            u[static_cast<std::size_t>(massless[i])] = u0[i];
            vel[static_cast<std::size_t>(massless[i])] = 0.0;
        }
        r.log.push_back(std::to_string(massless.size()) + " DDL sans masse : condensation statique (vitesses et accélérations non calculées, "
                        "affichées nulles ; état initial recalculé)");
    }
    // Accélération initiale des DDL massiques : M_mm·a_m = (p0 − C·v0 − K·u0)_m
    {
        Vector r0 = p0;
        const Vector cv = numerics::LinAlg::multiply(Cff, vel), ku = numerics::LinAlg::multiply(Kff, u);
        for (int i = 0; i < nf; ++i) r0[static_cast<std::size_t>(i)] -= cv[static_cast<std::size_t>(i)] + ku[static_cast<std::size_t>(i)];
        numerics::CholeskyFactorization c;
        if (!c.factor(subMatrix(Mff, massive, massive))) return refuse("matrice de masse des DDL massiques non définie positive : " + c.message);
        const Vector am = c.solve(subVector(r0, massive));
        for (std::size_t i = 0; i < massive.size(); ++i) acc[static_cast<std::size_t>(massive[i])] = am[i];
    }

    const double dt = o.dt, b = o.beta, g = o.gamma;
    const double c2 = 1.0 / (b * dt * dt), c3 = 1.0 / (b * dt), c4 = 1.0 / (2.0 * b) - 1.0;
    const double c5 = g / (b * dt), c6 = g / b - 1.0, c7 = dt * (g / (2.0 * b) - 1.0);
    if (2.0 * b < g)
    {
        // Schéma conditionnellement stable : Δt·ω_max ≤ 1/√(γ/2 − β) (sans amortissement)
        const numerics::EigenResult all = numerics::generalizedEigen(Kff, Mff, 0);
        if (!all.success || all.values.empty()) return refuse("contrôle de stabilité impossible : " + all.message);
        const double omegaMax = std::sqrt(all.values.back()), limit = 1.0 / std::sqrt(g / 2.0 - b);
        if (omegaMax * dt > limit)
            return refuse("schéma de Newmark conditionnellement stable (2β < γ) : Δt = " + num(dt) + " s dépasse la limite " + num(limit / omegaMax)
                          + " s (ω_max = " + num(omegaMax) + " rad/s)");
        r.log.push_back("Schéma conditionnellement stable : Δt limite = " + num(limit / omegaMax) + " s");
    }
    const Matrix Khat = numerics::LinAlg::add(numerics::LinAlg::add(Kff, Cff, c5), Mff, c2);
    numerics::CholeskyFactorization solver;
    if (!solver.factor(Khat)) return refuse("rigidité effective non définie positive : " + solver.message);

    const std::size_t total = static_cast<std::size_t>(steps) + 1;
    r.time.reserve(total);
    r.displacement.reserve(total);
    r.velocity.reserve(total);
    r.acceleration.reserve(total);
    r.peakAbsDisplacement.assign(static_cast<std::size_t>(n), 0.0);
    auto record = [&](double t, double fac, double ag) {
        Vector du(static_cast<std::size_t>(n), 0.0), dv(static_cast<std::size_t>(n), 0.0), da(static_cast<std::size_t>(n), 0.0);
        for (int i = 0; i < nf; ++i)
        {
            const auto k = static_cast<std::size_t>(f[static_cast<std::size_t>(i)]);
            du[k] = u[static_cast<std::size_t>(i)], dv[k] = vel[static_cast<std::size_t>(i)], da[k] = acc[static_cast<std::size_t>(i)];
            r.peakAbsDisplacement[k] = std::max(r.peakAbsDisplacement[k], std::abs(du[k]));
        }
        r.time.push_back(t);
        r.displacement.push_back(std::move(du));
        r.velocity.push_back(std::move(dv));
        r.acceleration.push_back(std::move(da));
        r.groundAcceleration.push_back(ag);
        r.loadFactor.push_back(fac);
        r.kineticEnergy.push_back(0.5 * numerics::LinAlg::dot(vel, numerics::LinAlg::multiply(Mff, vel)));
        r.strainEnergy.push_back(0.5 * numerics::LinAlg::dot(u, numerics::LinAlg::multiply(Kff, u)));
    };
    record(0.0, fac0, ag0);
    Vector ma(static_cast<std::size_t>(nf)), cb(static_cast<std::size_t>(nf));
    for (int s = 1; s <= steps; ++s)
    {
        const double t = s * dt;
        double fac = 0.0, ag = 0.0;
        Vector p = loadAt(t, fac, ag);
        for (int i = 0; i < nf; ++i)
        {
            const auto k = static_cast<std::size_t>(i);
            ma[k] = c2 * u[k] + c3 * vel[k] + c4 * acc[k];
            cb[k] = c5 * u[k] + c6 * vel[k] + c7 * acc[k];
        }
        const Vector mterm = numerics::LinAlg::multiply(Mff, ma), cterm = numerics::LinAlg::multiply(Cff, cb);
        for (int i = 0; i < nf; ++i) p[static_cast<std::size_t>(i)] += mterm[static_cast<std::size_t>(i)] + cterm[static_cast<std::size_t>(i)];
        const Vector un = solver.solve(p);
        for (int i = 0; i < nf; ++i)
        {
            const auto k = static_cast<std::size_t>(i);
            const double an = c2 * (un[k] - u[k]) - c3 * vel[k] - c4 * acc[k];
            vel[k] += dt * ((1.0 - g) * acc[k] + g * an);
            acc[k] = an;
            u[k] = un[k];
        }
        for (int i : massless) vel[static_cast<std::size_t>(i)] = acc[static_cast<std::size_t>(i)] = 0.0;
        record(t, fac, ag);
    }
    r.log.push_back("Newmark (β = " + num(b) + ", γ = " + num(g) + ") : " + std::to_string(steps) + " pas de " + num(dt) + " s, masse " + massModelName(o.mass)
                    + ", Rayleigh a0 = " + num(o.damping.a0) + " 1/s, a1 = " + num(o.damping.a1) + " s");
    double peak = 0.0;
    int peakDof = -1;
    for (int k = 0; k < n; ++k)
        if (r.peakAbsDisplacement[static_cast<std::size_t>(k)] > peak) peak = r.peakAbsDisplacement[static_cast<std::size_t>(k)], peakDof = k;
    r.success = true;
    r.message = "Analyse temporelle : " + std::to_string(steps) + " pas" + (peakDof >= 0 ? ", max |u| = " + num(peak) + " (" + r.dofs.label(peakDof) + ")" : std::string())
                + ".";
    return r;
}

bool elementResultsAt(const TimeHistoryResult& r, int step, int points, std::vector<ElementResult>& out, std::string* error)
{
    if (!r.success) return fail(error, "analyse temporelle non réussie");
    if (step < 0 || step >= static_cast<int>(r.displacement.size())) return fail(error, "pas " + std::to_string(step) + " hors du calcul");
    LoadCase scaled;
    scaled.id = r.loads.id;
    scaled.name = r.loads.name;
    if (!r.loads.empty()) scaled.append(r.loads, r.loadFactor[static_cast<std::size_t>(step)]);
    return elementResults(r.model, r.dofs, scaled, r.displacement[static_cast<std::size_t>(step)], points, out, error);
}

bool accelerogramSpectrum(const TimeFunction& ag, const std::vector<double>& periods, double xi, ResponseSpectrum& out, std::string* error)
{
    out = ResponseSpectrum {};
    std::string why;
    if (!ag.valid(&why)) return fail(error, "accélérogramme : " + why);
    if (!(xi >= 0.0) || !(xi < 1.0)) return fail(error, "taux d'amortissement ξ hors de [0, 1[");
    if (periods.empty()) return fail(error, "aucune période demandée");
    double signalStep = std::numeric_limits<double>::max();
    for (std::size_t k = 1; k < ag.points.size(); ++k) signalStep = std::min(signalStep, ag.points[k].first - ag.points[k - 1].first);
    const double t0 = ag.points.front().first, t1 = ag.points.back().first;
    for (std::size_t k = 0; k < periods.size(); ++k)
    {
        const double T = periods[k];
        if (!(T > 0.0) || !std::isfinite(T) || (k > 0 && !(T > periods[k - 1])))
            return fail(error, "périodes strictement positives et croissantes requises (point " + std::to_string(k + 1) + ")");
        const double w = 2.0 * kPi / T;
        const int steps = static_cast<int>(std::ceil((t1 - t0) / std::min(signalStep, T / 50.0)));
        const double h = (t1 - t0) / steps;
        // Newmark accélération moyenne sur l'oscillateur à 1 DDL (masse unitaire), départ au repos
        const double kh = w * w + 2.0 * xi * w * 2.0 / h + 4.0 / (h * h);
        double u = 0.0, v = 0.0, a = -ag.at(t0), umax = 0.0;
        for (int s = 1; s <= steps; ++s)
        {
            const double p = -ag.at(t0 + s * h);
            const double ph = p + (4.0 / (h * h) * u + 4.0 / h * v + a) + 2.0 * xi * w * (2.0 / h * u + v);
            const double un = ph / kh;
            const double an = 4.0 / (h * h) * (un - u) - 4.0 / h * v - a;
            v += h / 2.0 * (a + an);
            a = an;
            u = un;
            umax = std::max(umax, std::abs(u));
        }
        out.points.emplace_back(T, w * w * umax);
    }
    out.source = "spectre élastique (ξ = " + num(xi) + ") de l'accélérogramme fourni";
    return true;
}

// -----------------------------------------------------------------------------
// Analyse spectrale
// -----------------------------------------------------------------------------

double cqcCoefficient(double omegaI, double omegaJ, double xi)
{
    if (!(omegaI > 0.0) || !(omegaJ > 0.0)) return 0.0;
    const double r = omegaJ / omegaI;
    const double num = 8.0 * xi * xi * (1.0 + r) * std::pow(r, 1.5);
    const double den = (1.0 - r * r) * (1.0 - r * r) + 4.0 * xi * xi * r * (1.0 + r) * (1.0 + r);
    return den > 0.0 ? num / den : 1.0;
}

SpectralResult spectralAnalysis(const ModalResult& modal, const ResponseSpectrum& spectrum, int direction, ModalCombination combination, double xi)
{
    SpectralResult r;
    r.direction = direction;
    r.combination = combination;
    r.damping = xi;
    r.dofs = modal.dofs;
    if (!modal.success)
    {
        r.message = "analyse modale non réussie : " + modal.message;
        return r;
    }
    if (direction != 0 && direction != 1)
    {
        r.message = "direction du séisme : 0 (X) ou 1 (Y)";
        return r;
    }
    std::string err;
    if (!spectrum.valid(&err))
    {
        r.message = "spectre de réponse : " + err;
        return r;
    }
    if (combination == ModalCombination::CQC && !(xi > 0.0 && xi < 1.0))
    {
        r.message = "combinaison CQC : taux d'amortissement ξ dans ]0, 1[ requis";
        return r;
    }
    const auto& G = direction == 0 ? modal.participationX : modal.participationY;
    const auto& Me = direction == 0 ? modal.effectiveMassX : modal.effectiveMassY;
    const double totalMass = direction == 0 ? modal.totalMassX : modal.totalMassY;
    if (!(totalMass > 0.0))
    {
        r.message = std::string("aucune masse mobilisable suivant ") + directionLabel(direction);
        return r;
    }
    const int m = static_cast<int>(modal.omega.size()), n = modal.dofs.size();
    r.log.push_back("Spectre : " + (spectrum.source.empty() ? std::string("fourni par l'utilisateur") : spectrum.source) + " ; "
                    + std::to_string(spectrum.points.size()) + " points, T de " + num(spectrum.points.front().first) + " à "
                    + num(spectrum.points.back().first) + " s");
    for (int k = 0; k < m; ++k)
    {
        const auto kk = static_cast<std::size_t>(k);
        double sa = 0.0;
        if (!spectrum.at(modal.period[kk], sa))
        {
            r.message = "période du mode " + std::to_string(k + 1) + " (T = " + num(modal.period[kk]) + " s) hors du spectre fourni ["
                        + num(spectrum.points.front().first) + " ; " + num(spectrum.points.back().first) + "] s : compléter le spectre (pas d'extrapolation)";
            return r;
        }
        const double w = modal.omega[kk], sd = sa / (w * w);
        r.period.push_back(modal.period[kk]);
        r.Sa.push_back(sa);
        r.Sd.push_back(sd);
        r.participation.push_back(G[kk]);
        r.effectiveMass.push_back(Me[kk]);
        r.baseShear.push_back(Me[kk] * sa);
        Vector uk = modal.mode(k);
        for (double& x : uk) x *= G[kk] * sd;
        r.modalDisplacement.push_back(std::move(uk));
        r.log.push_back("Mode " + std::to_string(k + 1) + " : T = " + num(modal.period[kk]) + " s, Sa = " + num(sa) + " m/s², Sd = " + num(sd) + " m, Γ = "
                        + num(G[kk]) + ", masse effective " + num(Me[kk]) + " t, effort tranchant " + num(Me[kk] * sa) + " kN");
    }
    // Corrélations : SRSS (modes indépendants) ou CQC
    Matrix rho = numerics::LinAlg::identity(m);
    if (combination == ModalCombination::CQC)
        for (int i = 0; i < m; ++i)
            for (int j = 0; j < m; ++j) rho(i, j) = cqcCoefficient(modal.omega[static_cast<std::size_t>(i)], modal.omega[static_cast<std::size_t>(j)], xi);
    auto combine = [&](const std::vector<double>& x) {
        double s = 0.0;
        for (int i = 0; i < m; ++i)
            for (int j = 0; j < m; ++j) s += rho(i, j) * x[static_cast<std::size_t>(i)] * x[static_cast<std::size_t>(j)];
        return std::sqrt(std::max(s, 0.0));
    };
    std::vector<double> x(static_cast<std::size_t>(m));
    r.peakDisplacement.assign(static_cast<std::size_t>(n), 0.0);
    for (int d = 0; d < n; ++d)
    {
        for (int k = 0; k < m; ++k) x[static_cast<std::size_t>(k)] = r.modalDisplacement[static_cast<std::size_t>(k)][static_cast<std::size_t>(d)];
        r.peakDisplacement[static_cast<std::size_t>(d)] = combine(x);
    }
    r.combinedBaseShear = combine(r.baseShear);
    // Efforts d'extrémité : combinaison composante par composante des efforts modaux
    std::vector<std::vector<ElementResult>> modalForces(static_cast<std::size_t>(m));
    for (int k = 0; k < m; ++k)
        if (!elementResults(modal.model, modal.dofs, LoadCase {}, r.modalDisplacement[static_cast<std::size_t>(k)], 2, modalForces[static_cast<std::size_t>(k)], &err))
        {
            r.message = "efforts modaux : " + err;
            return r;
        }
    for (std::size_t e = 0; e < modal.model.elements.size(); ++e)
    {
        ElementPeak pk;
        pk.element = modal.model.elements[e].id;
        pk.endForces.assign(6, 0.0);
        for (int c = 0; c < 6; ++c)
        {
            for (int k = 0; k < m; ++k)
                x[static_cast<std::size_t>(k)] = modalForces[static_cast<std::size_t>(k)][e].endForces[static_cast<std::size_t>(c)];
            pk.endForces[static_cast<std::size_t>(c)] = combine(x);
        }
        r.elementPeaks.push_back(std::move(pk));
    }
    double sum = 0.0;
    for (double v : r.effectiveMass) sum += v;
    r.massRatio = sum / totalMass;
    r.log.push_back(std::string("Combinaison ") + (combination == ModalCombination::CQC ? "CQC (ξ = " + num(xi) + ")" : std::string("SRSS")) + " de "
                    + std::to_string(m) + " mode(s) ; masse effective cumulée " + num(100.0 * r.massRatio, 4) + " % ; effort tranchant à la base "
                    + num(r.combinedBaseShear) + " kN (valeurs de pointe, signes perdus)");
    if (r.massRatio < 0.9)
        r.log.push_back("Avertissement : masse effective cumulée inférieure au seuil usuel de 90 % : retenir davantage de modes");
    r.success = true;
    r.message = "Analyse spectrale suivant " + std::string(directionLabel(direction)) + " : effort tranchant à la base " + num(r.combinedBaseShear) + " kN.";
    return r;
}

} // namespace tsalab::structural
