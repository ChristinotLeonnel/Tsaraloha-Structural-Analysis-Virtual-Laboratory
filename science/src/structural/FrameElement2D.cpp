// Élément de portique plan à 6 DDL : rigidités, transformation, charges, efforts, diagrammes et déformée, rotules
// d'extrémité (condensation statique), matrices de masse. Formulation et conventions : FrameElement2D.h.

#include "tsalab/structural/FrameElement2D.h"

#include <algorithm>
#include <cmath>
#include <sstream>

namespace tsalab::structural
{

namespace
{
constexpr double kGaussX[3] = { -0.77459666924148337704, 0.0, 0.77459666924148337704 };   // ±√(3/5)
constexpr double kGaussW[3] = { 5.0 / 9.0, 8.0 / 9.0, 5.0 / 9.0 };

std::string num(double v)
{
    std::ostringstream o;
    o.precision(6);
    o << v;
    return o.str();
}

/// Fonctions de forme de la barre prismatique (flexion, Hermite) en x ∈ [0, L] : v(x) = N1·v_i + N2·θ_i + N3·v_j + N4·θ_j.
struct Hermite
{
    double n1, n2, n3, n4;
};

Hermite shape(double x, double L)
{
    const double t = x / L, t2 = t * t, t3 = t2 * t;
    return { 1.0 - 3.0 * t2 + 2.0 * t3, L * (t - 2.0 * t2 + t3), 3.0 * t2 - 2.0 * t3, L * (t3 - t2) };
}

/// Dérivées dN/dx (travail d'un moment concentré : M·v'(a)).
Hermite shapeDerivative(double x, double L)
{
    const double t = x / L, t2 = t * t;
    return { (6.0 * t2 - 6.0 * t) / L, 1.0 - 4.0 * t + 3.0 * t2, (6.0 * t - 6.0 * t2) / L, 3.0 * t2 - 2.0 * t };
}

/// ∫ f sur [x0, x1], découpé aux abscisses singulières, Gauss-Legendre à 3 points par tronçon (exact jusqu'au
/// degré 5 : les intégrandes de la formulation sont polynomiaux de degré ≤ 4 par tronçon).
template <typename F>
double integrate(F&& f, double x0, double x1, const std::vector<double>& breaks)
{
    double sum = 0.0, a = x0;
    auto piece = [&](double lo, double hi) {
        if (hi - lo <= 0.0) return;
        const double c = 0.5 * (lo + hi), h = 0.5 * (hi - lo);
        for (int g = 0; g < 3; ++g) sum += h * kGaussW[g] * f(c + h * kGaussX[g]);
    };
    for (double b : breaks)
    {
        if (b <= a) continue;
        if (b >= x1) break;
        piece(a, b);
        a = b;
    }
    piece(a, x1);
    return sum;
}

/// Sommes des charges du tronçon [0, x] (convention de l'en-tête) ; includeAt : charges concentrées situées en x comprises.
struct LoadSums
{
    double Px = 0.0, Py = 0.0, MPy = 0.0, Mc = 0.0;
};

LoadSums loadSums(const std::vector<LocalLoad>& loads, double x, bool includeAt)
{
    LoadSums s;
    for (const auto& l : loads)
    {
        switch (l.kind)
        {
        case LocalLoad::Kind::Distributed:
        {
            const double lb = l.b - l.a;
            if (x <= l.a || lb <= 0.0) break;
            const double kx = (l.px2 - l.px1) / lb, ky = (l.py2 - l.py1) / lb;
            const double t = std::min(x, l.b) - l.a;
            s.Px += l.px1 * t + kx * t * t / 2.0;
            s.Py += l.py1 * t + ky * t * t / 2.0;
            if (x <= l.b)
                s.MPy += l.py1 * t * t / 2.0 + ky * t * t * t / 6.0;   // ∫0^t (t − τ)(py1 + k·τ) dτ
            else
            {
                const double F = l.py1 * lb + ky * lb * lb / 2.0;                                  // résultante
                const double S = l.a * F + l.py1 * lb * lb / 2.0 + ky * lb * lb * lb / 3.0;        // ∫ s·py ds
                s.MPy += x * F - S;
            }
            break;
        }
        case LocalLoad::Kind::Force:
            if (l.a < x || (includeAt && l.a <= x))
            {
                s.Px += l.px1;
                s.Py += l.py1;
                s.MPy += (x - l.a) * l.py1;
            }
            break;
        case LocalLoad::Kind::Moment:
            if (l.a < x || (includeAt && l.a <= x)) s.Mc += l.m;
            break;
        case LocalLoad::Kind::Strain: break;
        }
    }
    return s;
}
} // namespace

FrameElement2D::FrameElement2D(int id, const Node2D& nodeI, const Node2D& nodeJ, const Material& material, const Section2D& section,
                               std::string name)
    : m_id(id), m_name(std::move(name)), m_i(nodeI), m_j(nodeJ), m_material(material), m_section(section)
{
}

void FrameElement2D::setReleases(bool releaseI, bool releaseJ)
{
    m_releaseI = releaseI;
    m_releaseJ = releaseJ;
}

std::vector<std::string> FrameElement2D::validate() const
{
    std::vector<std::string> e;
    const std::string who = "élément " + std::to_string(m_id);
    if (!std::isfinite(m_i.x) || !std::isfinite(m_i.y) || !std::isfinite(m_j.x) || !std::isfinite(m_j.y))
        e.push_back(who + " : coordonnées non finies");
    const double scale = std::max({ 1.0, std::abs(m_i.x), std::abs(m_i.y), std::abs(m_j.x), std::abs(m_j.y) });
    if (!(length() > 1e-9 * scale))
        e.push_back(who + " : longueur nulle (nœuds " + std::to_string(m_i.id) + " et " + std::to_string(m_j.id) + " confondus)");
    if (!(m_material.E > 0.0) || !std::isfinite(m_material.E)) e.push_back(who + " : module d'Young E non positif (" + num(m_material.E) + " kN/m²)");
    if (!(m_section.A > 0.0) || !std::isfinite(m_section.A)) e.push_back(who + " : aire A non positive (" + num(m_section.A) + " m²)");
    if (!(m_section.Iz > 0.0) || !std::isfinite(m_section.Iz)) e.push_back(who + " : moment quadratique Iz non positif (" + num(m_section.Iz) + " m⁴)");
    return e;
}

double FrameElement2D::length() const
{
    return std::hypot(m_j.x - m_i.x, m_j.y - m_i.y);
}

double FrameElement2D::angle() const
{
    return std::atan2(m_j.y - m_i.y, m_j.x - m_i.x);
}

double FrameElement2D::cosine() const
{
    const double L = length();
    return L > 0.0 ? (m_j.x - m_i.x) / L : 1.0;
}

double FrameElement2D::sine() const
{
    const double L = length();
    return L > 0.0 ? (m_j.y - m_i.y) / L : 0.0;
}

Matrix FrameElement2D::baseLocalStiffness() const
{
    Matrix k(6, 6);
    const double L = length();
    if (!(L > 0.0)) return k;
    const double L2 = L * L, L3 = L2 * L;
    const double ea = m_material.E * m_section.A / L;
    const double ei = m_material.E * m_section.Iz;
    k(0, 0) = k(3, 3) = ea;
    k(0, 3) = k(3, 0) = -ea;
    k(1, 1) = k(4, 4) = 12.0 * ei / L3;
    k(1, 4) = k(4, 1) = -12.0 * ei / L3;
    k(1, 2) = k(2, 1) = k(1, 5) = k(5, 1) = 6.0 * ei / L2;
    k(2, 4) = k(4, 2) = k(4, 5) = k(5, 4) = -6.0 * ei / L2;
    k(2, 2) = k(5, 5) = 4.0 * ei / L;
    k(2, 5) = k(5, 2) = 2.0 * ei / L;
    return k;
}

void FrameElement2D::condensation(const Vector& f0, Matrix& C, Vector& c0) const
{
    // DDL locaux libérés R (rotations aux rotules) et conservés O ; θ_R = −k'_RR⁻¹·(k'_RO·d'_O + f0'_R)
    C = numerics::LinAlg::identity(6);
    c0.assign(6, 0.0);
    std::vector<int> R;
    if (m_releaseI) R.push_back(2);
    if (m_releaseJ) R.push_back(5);
    if (R.empty()) return;
    const Matrix k = baseLocalStiffness();
    double inv[2][2] = { { 0.0, 0.0 }, { 0.0, 0.0 } };
    if (R.size() == 1)
    {
        const double krr = k(R[0], R[0]);
        if (!(krr > 0.0)) return;   // élément invalide (EI nul) : signalé par validate()
        inv[0][0] = 1.0 / krr;
    }
    else
    {
        const double a = k(2, 2), b = k(2, 5), c = k(5, 2), d = k(5, 5), det = a * d - b * c;
        if (!(std::abs(det) > 0.0)) return;
        inv[0][0] = d / det, inv[0][1] = -b / det, inv[1][0] = -c / det, inv[1][1] = a / det;
    }
    for (std::size_t r = 0; r < R.size(); ++r)
    {
        const int row = R[r];
        for (int col = 0; col < 6; ++col)
        {
            C(row, col) = 0.0;
            if (col == 2 && m_releaseI) continue;
            if (col == 5 && m_releaseJ) continue;
            double v = 0.0;
            for (std::size_t q = 0; q < R.size(); ++q) v -= inv[r][q] * k(R[q], col);
            C(row, col) = v;
        }
        double v0 = 0.0;
        for (std::size_t q = 0; q < R.size(); ++q) v0 -= inv[r][q] * f0[static_cast<std::size_t>(R[q])];
        c0[static_cast<std::size_t>(row)] = v0;
    }
}

Matrix FrameElement2D::localStiffness() const
{
    if (!hasRelease()) return baseLocalStiffness();
    Matrix C;
    Vector c0;
    condensation(Vector(6, 0.0), C, c0);
    Matrix k = numerics::LinAlg::congruence(C, baseLocalStiffness());
    // Lignes et colonnes des rotations libérées : nulles par construction (arrondis effacés)
    for (int r : { 2, 5 })
        if ((r == 2 && m_releaseI) || (r == 5 && m_releaseJ))
            for (int k2 = 0; k2 < 6; ++k2) k(r, k2) = k(k2, r) = 0.0;
    return k;
}

Matrix FrameElement2D::transformation() const
{
    Matrix T(6, 6);
    const double c = cosine(), s = sine();
    for (int n = 0; n < 2; ++n)
    {
        const int o = 3 * n;
        T(o, o) = c;
        T(o, o + 1) = s;
        T(o + 1, o) = -s;
        T(o + 1, o + 1) = c;
        T(o + 2, o + 2) = 1.0;
    }
    return T;
}

Matrix FrameElement2D::globalStiffness() const
{
    return numerics::LinAlg::congruence(transformation(), localStiffness());
}

Vector FrameElement2D::toLocal(const Vector& g) const
{
    if (g.size() != 6) return {};
    const double c = cosine(), s = sine();
    Vector l(6);
    for (int n = 0; n < 2; ++n)
    {
        const std::size_t o = static_cast<std::size_t>(3 * n);
        l[o] = c * g[o] + s * g[o + 1];
        l[o + 1] = -s * g[o] + c * g[o + 1];
        l[o + 2] = g[o + 2];
    }
    return l;
}

Vector FrameElement2D::toGlobal(const Vector& l) const
{
    if (l.size() != 6) return {};
    const double c = cosine(), s = sine();
    Vector g(6);
    for (int n = 0; n < 2; ++n)
    {
        const std::size_t o = static_cast<std::size_t>(3 * n);
        g[o] = c * l[o] - s * l[o + 1];
        g[o + 1] = s * l[o] + c * l[o + 1];
        g[o + 2] = l[o + 2];
    }
    return g;
}

std::vector<LocalLoad> FrameElement2D::localLoads(const std::vector<ElementLoad>& loads, std::vector<std::string>* errors) const
{
    std::vector<LocalLoad> out;
    const double L = length(), c = cosine(), s = sine();
    const double tol = 1e-9 * std::max(L, 1e-12);
    auto err = [&](const std::string& m) {
        if (errors) errors->push_back("élément " + std::to_string(m_id) + " : " + m);
    };
    for (const auto& l : loads)
    {
        if (l.element != m_id && !(l.element == 0 && l.type == ElementLoadType::SelfWeight)) continue;
        if (!(L > 0.0))
        {
            err("charge sur un élément de longueur nulle");
            continue;
        }
        // Direction de la charge en composantes locales (x', y')
        double ux = 0.0, uy = 0.0;
        switch (l.direction)
        {
        case LoadDirection::LocalX: ux = 1.0; break;
        case LoadDirection::LocalY: uy = 1.0; break;
        case LoadDirection::GlobalX: ux = c, uy = -s; break;
        case LoadDirection::GlobalY: ux = s, uy = c; break;
        }
        double projection = 1.0;
        if (l.projected)
        {
            if (l.type != ElementLoadType::Distributed)
            {
                err(std::string("projection sans objet pour une charge ") + loadTypeName(l.type));
                continue;
            }
            if (l.direction == LoadDirection::LocalX || l.direction == LoadDirection::LocalY)
            {
                err("projection demandée pour une direction locale");
                continue;
            }
            projection = l.direction == LoadDirection::GlobalY ? std::abs(c) : std::abs(s);
        }
        auto position = [&](double v) { return l.relative ? v * L : v; };
        LocalLoad x;
        switch (l.type)
        {
        case ElementLoadType::Distributed:
        {
            double a = position(l.a), b = l.b < 0.0 ? L : position(l.b);
            if (a < -tol || b > L + tol || b < a - tol)
            {
                err("charge répartie de " + num(a) + " à " + num(b) + " m hors de l'élément (L = " + num(L) + " m)");
                continue;
            }
            a = std::clamp(a, 0.0, L), b = std::clamp(b, 0.0, L);
            if (b - a <= tol)
            {
                err("charge répartie de longueur nulle (a = b = " + num(a) + " m)");
                continue;
            }
            x.kind = LocalLoad::Kind::Distributed;
            x.a = a, x.b = b;
            x.px1 = l.q1 * ux * projection, x.py1 = l.q1 * uy * projection;
            x.px2 = l.q2 * ux * projection, x.py2 = l.q2 * uy * projection;
            break;
        }
        case ElementLoadType::PointForce:
        case ElementLoadType::PointMoment:
        {
            const double a = position(l.a);
            if (a < -tol || a > L + tol)
            {
                err(std::string(loadTypeName(l.type)) + " en " + num(a) + " m hors de l'élément (L = " + num(L) + " m)");
                continue;
            }
            x.a = x.b = std::clamp(a, 0.0, L);
            if (l.type == ElementLoadType::PointForce)
            {
                x.kind = LocalLoad::Kind::Force;
                x.px1 = l.q1 * ux, x.py1 = l.q1 * uy;
            }
            else
            {
                x.kind = LocalLoad::Kind::Moment;
                x.m = l.q1;
            }
            break;
        }
        case ElementLoadType::Thermal:
            if ((l.deltaT != 0.0 || l.gradientT != 0.0) && !(m_material.alpha > 0.0))
            {
                err("action thermique sans coefficient de dilatation α");
                continue;
            }
            if (l.gradientT != 0.0 && !(m_section.h > 0.0))
            {
                err("gradient thermique sans hauteur de section h");
                continue;
            }
            x.kind = LocalLoad::Kind::Strain;
            x.a = 0.0, x.b = L;
            x.epsilon0 = m_material.alpha * l.deltaT;
            x.kappa0 = l.gradientT != 0.0 ? m_material.alpha * l.gradientT / m_section.h : 0.0;
            break;
        case ElementLoadType::InitialStrain:
            x.kind = LocalLoad::Kind::Strain;
            x.a = 0.0, x.b = L;
            x.epsilon0 = l.epsilon0;
            x.kappa0 = l.kappa0;
            break;
        case ElementLoadType::SelfWeight:
        {
            if (!(m_material.unitWeight > 0.0))
            {
                err("poids propre sans poids volumique γ");
                continue;
            }
            const double q = -m_material.unitWeight * m_section.A * l.factor;   // kN/m selon Y global (pesanteur vers −Y)
            x.kind = LocalLoad::Kind::Distributed;
            x.a = 0.0, x.b = L;
            x.px1 = x.px2 = q * s;
            x.py1 = x.py2 = q * c;
            break;
        }
        }
        out.push_back(x);
    }
    return out;
}

Vector FrameElement2D::fixedEndForces(const std::vector<LocalLoad>& loads) const
{
    const Vector f0 = baseFixedEndForces(loads);
    if (!hasRelease()) return f0;
    // Cᵀ·(k'·c0 + f0') : efforts d'encastrement de la barre articulée (moment nul à la rotule)
    Matrix C;
    Vector c0;
    condensation(f0, C, c0);
    Vector g = numerics::LinAlg::multiply(baseLocalStiffness(), c0);
    for (std::size_t k = 0; k < 6; ++k) g[k] += f0[k];
    Vector f = numerics::LinAlg::multiply(numerics::LinAlg::transpose(C), g);
    if (m_releaseI) f[2] = 0.0;
    if (m_releaseJ) f[5] = 0.0;
    return f;
}

Vector FrameElement2D::baseFixedEndForces(const std::vector<LocalLoad>& loads) const
{
    Vector f(6, 0.0);
    const double L = length();
    if (!(L > 0.0)) return f;
    const double EA = m_material.E * m_section.A, EI = m_material.E * m_section.Iz;
    // Charge (px, py) en x : f0 = −Nᵀ·p (axial linéaire, flexion Hermite)
    auto addForce = [&](double x, double px, double py, double w) {
        const Hermite N = shape(x, L);
        f[0] -= w * (1.0 - x / L) * px;
        f[3] -= w * (x / L) * px;
        f[1] -= w * N.n1 * py;
        f[2] -= w * N.n2 * py;
        f[4] -= w * N.n3 * py;
        f[5] -= w * N.n4 * py;
    };
    for (const auto& l : loads)
    {
        switch (l.kind)
        {
        case LocalLoad::Kind::Distributed:
        {
            const double h = 0.5 * (l.b - l.a), mid = 0.5 * (l.a + l.b);
            if (h <= 0.0) break;
            for (int g = 0; g < 3; ++g)
            {
                const double x = mid + h * kGaussX[g];
                const double t = (x - l.a) / (l.b - l.a);
                addForce(x, l.px1 + (l.px2 - l.px1) * t, l.py1 + (l.py2 - l.py1) * t, h * kGaussW[g]);
            }
            break;
        }
        case LocalLoad::Kind::Force: addForce(l.a, l.px1, l.py1, 1.0); break;
        case LocalLoad::Kind::Moment:
        {
            const Hermite d = shapeDerivative(l.a, L);
            f[1] -= l.m * d.n1;
            f[2] -= l.m * d.n2;
            f[4] -= l.m * d.n3;
            f[5] -= l.m * d.n4;
            break;
        }
        case LocalLoad::Kind::Strain:
            f[0] += EA * l.epsilon0;
            f[3] -= EA * l.epsilon0;
            f[2] += EI * l.kappa0;
            f[5] -= EI * l.kappa0;
            break;
        }
    }
    return f;
}

Vector FrameElement2D::equivalentNodalLoadsLocal(const std::vector<LocalLoad>& loads) const
{
    Vector f = fixedEndForces(loads);
    for (double& v : f) v = -v;
    return f;
}

Vector FrameElement2D::equivalentNodalLoadsGlobal(const std::vector<LocalLoad>& loads) const
{
    return toGlobal(equivalentNodalLoadsLocal(loads));
}

Vector FrameElement2D::elementLocalDisplacements(const Vector& d, const std::vector<LocalLoad>& loads) const
{
    if (d.size() != 6) return {};
    Vector dl = toLocal(d);
    if (!hasRelease()) return dl;
    Matrix C;
    Vector c0;
    condensation(baseFixedEndForces(loads), C, c0);
    Vector de = numerics::LinAlg::multiply(C, dl);
    for (std::size_t k = 0; k < 6; ++k) de[k] += c0[k];
    return de;
}

Vector FrameElement2D::endForces(const Vector& d, const std::vector<LocalLoad>& loads) const
{
    if (d.size() != 6) return {};
    // f' = k'·d'_élément + f0' (barre bi-encastrée, déplacements propres) ; nul aux rotules par construction
    Vector f = numerics::LinAlg::multiply(baseLocalStiffness(), elementLocalDisplacements(d, loads));
    const Vector f0 = baseFixedEndForces(loads);
    for (std::size_t k = 0; k < 6; ++k) f[k] += f0[k];
    if (m_releaseI) f[2] = 0.0;
    if (m_releaseJ) f[5] = 0.0;
    return f;
}

FrameElement2D::SectionForces FrameElement2D::internalForcesAt(const Vector& d, const std::vector<LocalLoad>& loads, double x, bool includeAt) const
{
    SectionForces s;
    const Vector f = endForces(d, loads);
    if (f.size() != 6) return s;
    const LoadSums q = loadSums(loads, x, includeAt);
    s.N = -f[0] - q.Px;
    s.V = f[1] + q.Py;
    s.M = -f[2] + x * f[1] + q.MPy - q.Mc;
    return s;
}

double FrameElement2D::massPerLength() const
{
    return m_material.density * m_section.A;
}

Matrix FrameElement2D::localMass(MassModel model) const
{
    Matrix m(6, 6);
    const double L = length(), mb = massPerLength();
    if (!(L > 0.0) || !(mb > 0.0)) return m;
    if (model == MassModel::Lumped)
    {
        // Masse concentrée : m̄L/2 sur les translations de chaque nœud, pas d'inertie de rotation
        for (int k : { 0, 1, 3, 4 }) m(k, k) = mb * L / 2.0;
        return m;
    }
    const double a = mb * L / 6.0, b = mb * L / 420.0, L2 = L * L;
    m(0, 0) = m(3, 3) = 2.0 * a;
    m(0, 3) = m(3, 0) = a;
    const int idx[4] = { 1, 2, 4, 5 };
    const double bend[4][4] = { { 156.0, 22.0 * L, 54.0, -13.0 * L },
                                { 22.0 * L, 4.0 * L2, 13.0 * L, -3.0 * L2 },
                                { 54.0, 13.0 * L, 156.0, -22.0 * L },
                                { -13.0 * L, -3.0 * L2, -22.0 * L, 4.0 * L2 } };
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) m(idx[r], idx[c]) = b * bend[r][c];
    if (!hasRelease()) return m;
    // Rotules : masse cohérente réduite par la même transformation que la rigidité (Cᵀ·m'·C)
    Matrix C;
    Vector c0;
    condensation(Vector(6, 0.0), C, c0);
    Matrix mc = numerics::LinAlg::congruence(C, m);
    for (int r : { 2, 5 })
        if ((r == 2 && m_releaseI) || (r == 5 && m_releaseJ))
            for (int k = 0; k < 6; ++k) mc(r, k) = mc(k, r) = 0.0;
    return mc;
}

Matrix FrameElement2D::globalMass(MassModel model) const
{
    return numerics::LinAlg::congruence(transformation(), localMass(model));
}

InternalForceDiagram FrameElement2D::diagram(const Vector& d, const std::vector<LocalLoad>& loads, int points) const
{
    InternalForceDiagram D;
    D.element = m_id;
    const double L = length();
    D.length = L;
    if (!(L > 0.0) || d.size() != 6) return D;
    points = std::max(points, 2);
    const Vector dl = elementLocalDisplacements(d, loads);   // rotations propres de l'élément aux rotules
    const Vector f = endForces(d, loads);
    const double Fxi = f[0], Fyi = f[1], Mzi = f[2];
    const double EA = m_material.E * m_section.A, EI = m_material.E * m_section.Iz;
    double eps0 = 0.0, kap0 = 0.0;
    std::vector<double> breaks, concentrated;
    for (const auto& l : loads)
    {
        if (l.kind == LocalLoad::Kind::Strain) eps0 += l.epsilon0, kap0 += l.kappa0;
        else if (l.kind == LocalLoad::Kind::Distributed) breaks.push_back(l.a), breaks.push_back(l.b);
        else breaks.push_back(l.a), concentrated.push_back(l.a);
    }
    const double tol = 1e-12 * L;
    auto tidy = [&](std::vector<double>& v, bool interiorOnly) {
        if (interiorOnly) v.erase(std::remove_if(v.begin(), v.end(), [&](double x) { return x <= tol || x >= L - tol; }), v.end());
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end(), [&](double a, double b) { return std::abs(a - b) <= tol; }), v.end());
    };
    tidy(breaks, true);
    tidy(concentrated, false);

    auto N = [&](double x, bool at) { return -Fxi - loadSums(loads, x, at).Px; };
    auto V = [&](double x, bool at) { return Fyi + loadSums(loads, x, at).Py; };
    auto M = [&](double x, bool at) {
        const LoadSums s = loadSums(loads, x, at);
        return -Mzi + x * Fyi + s.MPy - s.Mc;
    };
    // Déformée : u' = N/EA + ε0, v'' = M/EI + κ0 (intégrandes évalués hors des points singuliers)
    auto curvature = [&](double s) { return M(s, false) / EI + kap0; };
    auto u = [&](double x) { return dl[0] + integrate([&](double s) { return N(s, false) / EA + eps0; }, 0.0, x, breaks); };
    auto rot = [&](double x) { return dl[2] + integrate(curvature, 0.0, x, breaks); };
    auto v = [&](double x) { return dl[1] + dl[2] * x + integrate([&](double s) { return (x - s) * curvature(s); }, 0.0, x, breaks); };

    // Abscisses : régulières, points singuliers, extremums exacts de M (V = 0) dans chaque tronçon
    std::vector<double> xs;
    for (int k = 0; k < points; ++k) xs.push_back(L * k / (points - 1));
    xs.insert(xs.end(), breaks.begin(), breaks.end());
    std::vector<double> pieces { 0.0 };
    pieces.insert(pieces.end(), breaks.begin(), breaks.end());
    pieces.push_back(L);
    for (std::size_t p = 1; p < pieces.size(); ++p)
    {
        const double x0 = pieces[p - 1], x1 = pieces[p];
        if (x1 - x0 <= tol) continue;
        // V est un polynôme de degré ≤ 2 dans le tronçon : interpolé en trois points intérieurs
        const double xa = x0 + 0.25 * (x1 - x0), xb = 0.5 * (x0 + x1), xc = x0 + 0.75 * (x1 - x0);
        const double va = V(xa, false), vb = V(xb, false), vc = V(xc, false);
        const double h = 0.25 * (x1 - x0);
        const double A2 = (va - 2.0 * vb + vc) / (2.0 * h * h), A1 = (vc - va) / (2.0 * h);   // V ≈ A2·t² + A1·t + vb, t = x − xb
        std::vector<double> roots;
        if (std::abs(A2) > 1e-14 * (std::abs(A1) + std::abs(vb) + 1e-300))
        {
            const double disc = A1 * A1 - 4.0 * A2 * vb;
            if (disc >= 0.0)
            {
                const double sq = std::sqrt(disc);
                roots.push_back((-A1 + sq) / (2.0 * A2));
                roots.push_back((-A1 - sq) / (2.0 * A2));
            }
        }
        else if (std::abs(A1) > 1e-300)
            roots.push_back(-vb / A1);
        for (double t : roots)
        {
            const double x = xb + t;
            if (x > x0 + tol && x < x1 - tol) xs.push_back(x);
        }
    }
    tidy(xs, false);

    for (double x : xs)
    {
        const bool jump = std::any_of(concentrated.begin(), concentrated.end(), [&](double a) { return std::abs(a - x) <= tol; });
        const double ux = u(x), vx = v(x), rx = rot(x);
        D.points.push_back({ x, N(x, false), V(x, false), M(x, false), ux, vx, rx });
        if (jump) D.points.push_back({ x, N(x, true), V(x, true), M(x, true), ux, vx, rx });
    }
    D.Nmin = D.Nmax = D.points.front().N;
    D.Vmin = D.Vmax = D.points.front().V;
    D.Mmin = D.Mmax = D.points.front().M;
    for (const auto& p : D.points)
    {
        D.Nmin = std::min(D.Nmin, p.N), D.Nmax = std::max(D.Nmax, p.N);
        D.Vmin = std::min(D.Vmin, p.V), D.Vmax = std::max(D.Vmax, p.V);
        if (p.M < D.Mmin) D.Mmin = p.M, D.xMmin = p.x;
        if (p.M > D.Mmax) D.Mmax = p.M, D.xMmax = p.x;
    }
    // Flèche : écart à la corde, maximum affiné par section dorée autour du meilleur échantillon
    auto deflection = [&](double x) { return v(x) - (dl[1] + (dl[4] - dl[1]) * x / L); };
    std::size_t best = 0;
    for (std::size_t k = 0; k < D.points.size(); ++k)
        if (std::abs(deflection(D.points[k].x)) > std::abs(deflection(D.points[best].x))) best = k;
    double lo = D.points[best > 0 ? best - 1 : 0].x, hi = D.points[std::min(best + 1, D.points.size() - 1)].x;
    const double sign = deflection(D.points[best].x) >= 0.0 ? 1.0 : -1.0;
    const double gr = 0.5 * (std::sqrt(5.0) - 1.0);
    for (int it = 0; it < 60 && hi - lo > tol; ++it)
    {
        const double c1 = hi - gr * (hi - lo), c2 = lo + gr * (hi - lo);
        if (sign * deflection(c1) > sign * deflection(c2)) hi = c2;
        else lo = c1;
    }
    const double xBest = 0.5 * (lo + hi);
    const double candidate = deflection(xBest), sampled = deflection(D.points[best].x);
    D.maxDeflection = std::abs(candidate) >= std::abs(sampled) ? candidate : sampled;
    D.xMaxDeflection = std::abs(candidate) >= std::abs(sampled) ? xBest : D.points[best].x;
    D.closureError = std::abs(u(L) - dl[3]) + std::abs(v(L) - dl[4]) + L * std::abs(rot(L) - dl[5]);
    return D;
}

FrameElement2D::Resultant FrameElement2D::loadResultant(const std::vector<LocalLoad>& loads) const
{
    Resultant r;
    const double c = cosine(), s = sine();
    auto add = [&](double x, double px, double py, double w) {
        const double gx = c * px - s * py, gy = s * px + c * py;
        const double X = m_i.x + c * x, Y = m_i.y + s * x;
        r.fx += w * gx;
        r.fy += w * gy;
        r.mz += w * (X * gy - Y * gx);
    };
    for (const auto& l : loads)
    {
        switch (l.kind)
        {
        case LocalLoad::Kind::Distributed:
        {
            const double h = 0.5 * (l.b - l.a), mid = 0.5 * (l.a + l.b);
            if (h <= 0.0) break;
            for (int g = 0; g < 3; ++g)
            {
                const double x = mid + h * kGaussX[g];
                const double t = (x - l.a) / (l.b - l.a);
                add(x, l.px1 + (l.px2 - l.px1) * t, l.py1 + (l.py2 - l.py1) * t, h * kGaussW[g]);
            }
            break;
        }
        case LocalLoad::Kind::Force: add(l.a, l.px1, l.py1, 1.0); break;
        case LocalLoad::Kind::Moment: r.mz += l.m; break;
        case LocalLoad::Kind::Strain: break;
        }
    }
    return r;
}

} // namespace tsalab::structural
