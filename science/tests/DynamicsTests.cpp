// Tests de la dynamique des portiques plans (Dynamics2D) et des charges roulantes (MovingLoads2D).
// Références : fréquences propres d'Euler-Bernoulli (poutre sur deux appuis (nπ/L)²·√(EI/m̄) ; console, β1·L racine
// de 1 + cos x·ch x = 0), oscillateur à 1 DDL (solution discrète exacte du schéma de Newmark à accélération moyenne
// u_n = u0·cos(n·θ), θ = 2·atan(ωΔt/2) ; vibrations amorties ; sollicitation harmonique du sol), spectre Sa/ω²,
// lignes d'influence et théorème de Barré (poutre sur deux appuis), poutre continue à deux travées égales.
// Tolérances : 1e-9 pour les relations exactes ; pour les approximations (masse cohérente O(h⁴), Newmark O(Δt²)),
// tolérance déduite de l'erreur théorique et rappelée à chaque contrôle.
#include "tsalab/structural/Dynamics2D.h"
#include "tsalab/structural/MovingLoads2D.h"

#include <cmath>
#include <iostream>
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

constexpr double kExact = 1e-9;
const double kPi = std::acos(-1.0);

Material steel(double density = 7.85) { return { "S235", 210e6, 1.2e-5, 78.5, density }; }
Section2D ipe300() { return { "IPE 300", 53.8e-4, 8356e-8, 0.30 }; }
const double EI = 210e6 * 8356e-8;
const double mbar = 7.85 * 53.8e-4;   // t/m

struct Builder
{
    Model2D m;
    int node(double x, double y)
    {
        const int id = static_cast<int>(m.nodes.size()) + 1;
        m.nodes.push_back({ id, x, y, {} });
        return id;
    }
    int element(int i, int j, Material mat = steel())
    {
        const int id = static_cast<int>(m.elements.size()) + 1;
        m.elements.push_back({ id, i, j, mat, ipe300(), {} });
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

Builder simplySupported(double L, int n, Material mat = steel())
{
    Builder b;
    for (int k = 0; k <= n; ++k) b.node(L * k / n, 0.0);
    for (int k = 1; k <= n; ++k) b.element(k, k + 1, mat);
    b.support(1, true, true, false);
    b.support(n + 1, false, true, false);
    return b;
}

/// Oscillateur à 1 DDL : poteau encastré sans masse (hauteur H), masse m concentrée en tête suivant X.
/// Rigidité latérale 3EI/H³ (rotation et translation verticale de tête sans masse : condensées).
const double kH = 4.0, kMass = 10.0;
const double kSdofK = 3.0 * EI / (kH * kH * kH);
const double kSdofOmega = std::sqrt(kSdofK / kMass);
const double kSdofT = 2.0 * kPi / kSdofOmega;
Builder sdof()
{
    Builder b;
    b.node(0, 0), b.node(0, kH);
    b.element(1, 2, steel(0.0));
    b.support(1, true, true, true);
    b.m.nodalMasses.push_back({ 2, kMass, 0.0, 0.0 });
    return b;
}
constexpr int kTopUx = 3;   // DDL ux du nœud 2

Vector initialTop(double u0)
{
    Vector d(6, 0.0);
    d[kTopUx] = u0;
    return d;
}

// =================================================================================================
// S19 — matrices de masse et analyse modale
// =================================================================================================
bool testModal(int& passed)
{
    {   // masse totale d'un élément incliné : rᵀ·M·r = m̄L pour une translation d'ensemble (cohérente, concentrée, rotule)
        FrameElement2D el(1, { 1, 0.0, 0.0, {} }, { 2, 3.0, 4.0, {} }, steel(), ipe300());
        for (int pass = 0; pass < 3; ++pass)
        {
            if (pass == 2) el.setReleases(true, false);
            const Matrix M = el.globalMass(pass == 1 ? MassModel::Lumped : MassModel::Consistent);
            TEST_CHECK(numerics::LinAlg::asymmetry(M) < 1e-14, "S19: matrice de masse symétrique");
            for (int dir = 0; dir < 2; ++dir)
            {
                Vector r(6, 0.0);
                r[static_cast<std::size_t>(dir)] = r[static_cast<std::size_t>(dir + 3)] = 1.0;
                CHECK_REL(numerics::LinAlg::dot(r, numerics::LinAlg::multiply(M, r)), mbar * 5.0, kExact, "S19: masse de translation m̄L (cas " << pass << ")");
            }
        }
    }
    const double L = 10.0;
    auto exactSS = [&](int k) { return std::pow(k * kPi / L, 2) * std::sqrt(EI / mbar); };
    double err10 = 0.0;
    for (int n : { 10, 20 })
    {   // poutre sur deux appuis, masse cohérente : convergence par excès en O(h⁴)
        const auto b = simplySupported(L, n);
        const ModalResult r = modalAnalysis(b.m, 3, MassModel::Consistent);
        TEST_CHECK(r.success && r.omega.size() == 3, "S19: analyse modale — " << r.message);
        for (int k = 1; k <= 3; ++k) TEST_CHECK(r.omega[static_cast<std::size_t>(k - 1)] > exactSS(k), "S19: masse cohérente — borne supérieure, mode " << k);
        const double err = r.omega[0] / exactSS(1) - 1.0;
        if (n == 10)
            err10 = err;
        else
        {
            // Erreur O(h⁴) : (h/L)⁴ = 6e-6 pour 20 éléments ; rapport des erreurs ≈ 2⁴
            TEST_CHECK(err < 1e-5, "S19: mode 1 à " << err << " près (20 éléments)");
            TEST_CHECK(err10 / err > 14.0 && err10 / err < 18.0, "S19: convergence d'ordre 4 (rapport " << err10 / err << ")");
            CHECK_REL(r.omega[2], exactSS(3), 1e-4, "S19: mode 3 (erreur O((3h/L)⁴) ≈ 5e-5)");
            const ModalResult lumped = modalAnalysis(b.m, 1, MassModel::Lumped);
            TEST_CHECK(lumped.success && lumped.omega[0] < exactSS(1), "S19: masse concentrée — borne inférieure du mode 1");
        }
    }
    {   // orthogonalité : φᵀ·M·φ = I, φᵀ·K·φ = diag(ω²)
        const auto b = simplySupported(L, 8);
        const ModalResult r = modalAnalysis(b.m, 0, MassModel::Consistent);
        TEST_CHECK(r.success, "S19: tous les modes — " << r.message);
        Matrix K, M;
        TEST_CHECK(assembleStiffness(b.m, r.dofs, K) && assembleMass(b.m, r.dofs, MassModel::Consistent, M), "S19: assemblage K, M");
        const Matrix PtMP = numerics::LinAlg::congruence(r.modes, M), PtKP = numerics::LinAlg::congruence(r.modes, K);
        for (int i = 0; i < PtMP.rows; ++i)
            for (int j = 0; j < PtMP.cols; ++j)
            {
                CHECK_ABS(PtMP(i, j), i == j ? 1.0 : 0.0, 1e-9, "S19: φᵀMφ = I (" << i << ", " << j << ")");
                const double w2 = r.omega[static_cast<std::size_t>(i)] * r.omega[static_cast<std::size_t>(i)];
                CHECK_ABS(PtKP(i, j), i == j ? w2 : 0.0, 1e-9 * r.omega.back() * r.omega.back(), "S19: φᵀKφ = diag(ω²)");
            }
        double sumY = 0.0;
        for (double v : r.effectiveMassY) sumY += v;
        CHECK_REL(sumY, r.totalMassY, 1e-9, "S19: somme des masses effectives (tous les modes) = masse mobilisable");
    }
    {   // console : β1·L racine de 1 + cos x·ch x = 0 (bissection)
        double a = 1.8, c = 1.9;
        for (int it = 0; it < 200; ++it)
        {
            const double mid = 0.5 * (a + c);
            ((1.0 + std::cos(a) * std::cosh(a)) * (1.0 + std::cos(mid) * std::cosh(mid)) <= 0.0 ? c : a) = mid;
        }
        const double beta1 = 0.5 * (a + c), Lc = 4.0;
        Builder b;
        for (int k = 0; k <= 20; ++k) b.node(Lc * k / 20.0, 0.0);
        for (int k = 1; k <= 20; ++k) b.element(k, k + 1);
        b.support(1, true, true, true);
        const ModalResult r = modalAnalysis(b.m, 2, MassModel::Consistent);
        TEST_CHECK(r.success, "S19: console — " << r.message);
        CHECK_REL(r.omega[0], std::pow(beta1 / Lc, 2) * std::sqrt(EI / mbar), 1e-5, "S19: console, mode 1 (β1L = " << beta1 << ", erreur O(h⁴))");
        TEST_CHECK(r.effectiveMassY[0] / r.totalMassY > 0.5 && r.effectiveMassY[0] / r.totalMassY < 0.7,
                   "S19: masse effective du mode 1 d'une console ≈ 61 % (" << r.effectiveMassY[0] / r.totalMassY << ")");
    }
    {   // oscillateur à 1 DDL : ω = √(3EI/(mH³)), masse effective = masse totale
        const auto b = sdof();
        const ModalResult r = modalAnalysis(b.m, 0, MassModel::Consistent);
        TEST_CHECK(r.success && r.omega.size() == 1, "S19: 1 DDL — un seul mode de masse non nulle (" << r.message << ")");
        CHECK_REL(r.omega[0], kSdofOmega, kExact, "S19: ω = √(3EI/(mH³))");
        CHECK_REL(r.totalMassX, kMass, kExact, "S19: masse mobilisable X");
        CHECK_REL(r.effectiveMassX[0], kMass, kExact, "S19: masse effective = m");
        CHECK_REL(r.mode(0)[kTopUx], 1.0 / std::sqrt(kMass), kExact, "S19: mode normé φ = 1/√m (signe positif)");
    }
    {   // refus motivés
        Builder b = simplySupported(L, 2, steel(0.0));
        const ModalResult none = modalAnalysis(b.m, 0, MassModel::Consistent);
        TEST_CHECK(!none.success && none.message.find("masse") != std::string::npos, "S19: sans masse — refus (" << none.message << ")");
        Builder c = simplySupported(L, 2);
        c.m.supports[0].fixUx = false;
        const ModalResult mech = modalAnalysis(c.m, 0, MassModel::Consistent);
        TEST_CHECK(!mech.success && mech.message.find("suivant X") != std::string::npos, "S19: mécanisme diagnostiqué (" << mech.message << ")");
    }
    std::cout << "[PASS] S19: masses (m̄L), modes de la poutre sur deux appuis (O(h⁴), bornes), orthogonalité, console β1L, 1 DDL, refus" << std::endl;
    ++passed;
    return true;
}

// =================================================================================================
// S20 — analyse temporelle (Newmark), fonctions du temps, spectre d'un accélérogramme
// =================================================================================================
bool testTimeHistory(int& passed)
{
    const auto b = sdof();
    const double theta = [](double dt) { return 2.0 * std::atan(kSdofOmega * dt / 2.0); }(kSdofT / 100.0);
    {   // vibrations libres : solution discrète exacte u_n = u0·cos(nθ) et conservation de l'énergie
        TimeHistoryOptions o;
        o.dt = kSdofT / 100.0;
        o.duration = 3.0 * kSdofT;
        const double u0 = 0.01;
        const Vector d0 = initialTop(u0);
        const TimeHistoryResult r = timeHistory(b.m, o, nullptr, nullptr, &d0);
        TEST_CHECK(r.success, "S20: vibrations libres — " << r.message);
        TEST_CHECK(r.time.size() == 301, "S20: 300 pas (" << r.time.size() << ")");
        const double E0 = r.kineticEnergy[0] + r.strainEnergy[0];
        CHECK_REL(E0, 0.5 * kSdofK * u0 * u0, kExact, "S20: énergie initiale ½ku0² (rotation de tête condensée)");
        for (std::size_t n = 0; n < r.time.size(); ++n)
        {
            CHECK_ABS(r.displacement[n][kTopUx], u0 * std::cos(static_cast<double>(n) * theta), 1e-9 * u0, "S20: u_n = u0·cos(nθ), n = " << n);
            CHECK_ABS(r.kineticEnergy[n] + r.strainEnergy[n], E0, 1e-10 * E0, "S20: énergie conservée, n = " << n);
        }
    }
    {   // charge échelon : u_n = (P/k)(1 − cos nθ), facteur d'amplification dynamique 2 ; efforts au pas de pointe
        Builder c = sdof();
        const double P = 5.0;
        c.lc().nodalLoads.push_back({ 2, P, 0.0, 0.0 });
        TimeHistoryOptions o;
        o.dt = kSdofT / 100.0;
        o.duration = 1.0 * kSdofT;
        DynamicLoad load { 1, { { { 0.0, 1.0 }, { 10.0, 1.0 } } } };
        const TimeHistoryResult r = timeHistory(c.m, o, &load, nullptr);
        TEST_CHECK(r.success, "S20: charge échelon — " << r.message);
        for (std::size_t n = 0; n < r.time.size(); ++n)
            CHECK_ABS(r.displacement[n][kTopUx], P / kSdofK * (1.0 - std::cos(static_cast<double>(n) * theta)), 1e-9 * P / kSdofK, "S20: échelon, n = " << n);
        CHECK_REL(r.peakAbsDisplacement[kTopUx], P / kSdofK * (1.0 - std::cos(50.0 * theta)), kExact, "S20: pointe ≈ 2P/k au pas 50");
        std::vector<ElementResult> er;
        TEST_CHECK(elementResultsAt(r, 50, 5, er), "S20: efforts au pas 50");
        CHECK_REL(std::abs(er[0].endForces[2]), kSdofK * r.displacement[50][kTopUx] * kH, kExact, "S20: moment en pied = k·u·H (efforts élastiques)");
    }
    {   // vibrations amorties (Rayleigh a0 = 2ξω, exact pour 1 DDL) : erreur de phase ≈ (ωΔt)²/12·ωt ≈ 1e-5 attendue
        const double xi = 0.05, u0 = 0.01;
        TimeHistoryOptions o;
        o.dt = kSdofT / 2000.0;
        o.duration = 2.0 * kSdofT;
        o.damping = { 2.0 * xi * kSdofOmega, 0.0 };
        const Vector d0 = initialTop(u0);
        const TimeHistoryResult r = timeHistory(b.m, o, nullptr, nullptr, &d0);
        TEST_CHECK(r.success, "S20: amorti — " << r.message);
        const double wd = kSdofOmega * std::sqrt(1.0 - xi * xi);
        for (std::size_t n = 0; n < r.time.size(); n += 50)
        {
            const double t = r.time[n];
            const double exact = u0 * std::exp(-xi * kSdofOmega * t) * (std::cos(wd * t) + xi / std::sqrt(1.0 - xi * xi) * std::sin(wd * t));
            CHECK_ABS(r.displacement[n][kTopUx], exact, 1e-4 * u0, "S20: vibrations amorties, t = " << t);
        }
        DampingSpec ray = rayleighDamping(xi, 10.0, 30.0);
        CHECK_REL(ray.a0 / 20.0 + ray.a1 * 10.0 / 2.0, xi, kExact, "S20: Rayleigh — ξ(ω1) = ξ");
        CHECK_REL(ray.a0 / 60.0 + ray.a1 * 30.0 / 2.0, xi, kExact, "S20: Rayleigh — ξ(ω2) = ξ");
    }
    // Accélération harmonique du sol a_g = A·sin(Ωt) depuis le repos, sans amortissement :
    // u(t) = −A/(ω² − Ω²)·[sin Ωt − (Ω/ω)·sin ωt] (déplacement relatif)
    const double A = 2.0, Omega = 0.5 * kSdofOmega, duration = 3.0 * kSdofT;
    auto exactGround = [&](double t, double w) { return -A / (w * w - Omega * Omega) * (std::sin(Omega * t) - Omega / w * std::sin(w * t)); };
    GroundMotion ground;
    ground.direction = 0;
    ground.source = "signal harmonique de vérification (analytique, pas un séisme)";
    for (int k = 0; k <= 6000; ++k)
    {
        const double t = duration * k / 6000.0;
        ground.acceleration.points.emplace_back(t, A * std::sin(Omega * t));
    }
    {
        double err[2] = { 0.0, 0.0 }, peak = 0.0;
        for (int pass = 0; pass < 2; ++pass)
        {
            TimeHistoryOptions o;
            o.dt = kSdofT / (pass == 0 ? 500.0 : 1000.0);
            o.duration = duration;
            const TimeHistoryResult r = timeHistory(b.m, o, nullptr, &ground);
            TEST_CHECK(r.success, "S20: mouvement du sol — " << r.message);
            for (std::size_t n = 0; n < r.time.size(); ++n)
            {
                const double e = exactGround(r.time[n], kSdofOmega);
                peak = std::max(peak, std::abs(e));
                err[pass] = std::max(err[pass], std::abs(r.displacement[n][kTopUx] - e));
            }
        }
        // Erreur de phase ≈ (ωΔt)²/12·ωt ≈ 6e-5 attendue à T/1000 sur 3 périodes ; ordre 2 : rapport ≈ 4
        TEST_CHECK(err[1] <= 5e-4 * peak, "S20: mouvement du sol — écart " << err[1] / peak << " de la pointe");
        TEST_CHECK(err[0] / err[1] > 3.5 && err[0] / err[1] < 4.5, "S20: Newmark d'ordre 2 (rapport " << err[0] / err[1] << ")");
    }
    {   // spectre élastique de l'accélérogramme (ξ = 0) : Sa = ω²·max|u| de la solution fermée (erreur O((ωh)²) ≈ 2e-5)
        ResponseSpectrum spec;
        std::string error;
        const std::vector<double> periods { 0.5 * kSdofT, kSdofT };
        TEST_CHECK(accelerogramSpectrum(ground.acceleration, periods, 0.0, spec, &error), "S20: spectre de l'accélérogramme — " << error);
        for (std::size_t k = 0; k < periods.size(); ++k)
        {
            const double w = 2.0 * kPi / periods[k];
            double umax = 0.0;
            for (int i = 0; i <= 200000; ++i) umax = std::max(umax, std::abs(exactGround(duration * i / 200000.0, w)));
            CHECK_REL(spec.points[k].second, w * w * umax, 1e-4, "S20: Sa(T = " << periods[k] << " s)");
        }
    }
    {   // refus motivés et schéma conditionnellement stable
        TimeHistoryOptions o;
        o.dt = kSdofT / 100.0;
        o.duration = kSdofT;
        o.damping = { 0.0, 0.01 };
        const Vector d0 = initialTop(0.01);
        const TimeHistoryResult stiff = timeHistory(b.m, o, nullptr, nullptr, &d0);
        TEST_CHECK(!stiff.success && stiff.message.find("sans masse") != std::string::npos, "S20: a1 > 0 avec DDL sans masse refusé (" << stiff.message << ")");
        o.damping = {};
        o.gamma = 0.4;
        TEST_CHECK(!timeHistory(b.m, o, nullptr, nullptr, &d0).success, "S20: γ < 1/2 refusé");
        o.gamma = 0.5;
        o.beta = 1.0 / 6.0;   // accélération linéaire : Δt ≤ √12/ω ≈ 0,551·T
        o.dt = 0.6 * kSdofT;
        o.duration = 3.0 * kSdofT;
        const TimeHistoryResult unstable = timeHistory(b.m, o, nullptr, nullptr, &d0);
        TEST_CHECK(!unstable.success && unstable.message.find("conditionnellement") != std::string::npos, "S20: Δt au-delà de la limite de stabilité refusé");
        o.dt = kSdofT / 100.0;
        TEST_CHECK(timeHistory(b.m, o, nullptr, nullptr, &d0).success, "S20: accélération linéaire sous la limite acceptée");
        o.beta = 0.25;
        TEST_CHECK(!timeHistory(b.m, o, nullptr, nullptr).success, "S20: aucune excitation refusée");
    }
    {   // lecture de séries temporelles fournies
        TimeFunction f;
        std::string error;
        int skipped = 0;
        TEST_CHECK(parseTimeSeries("# accélérogramme\nUNITES: g\n0 0\n0.01 0.5\n0.02 -0.25\n", 0.0, 9.81, f, &error, &skipped), "S20: deux colonnes — " << error);
        TEST_CHECK(f.points.size() == 3 && skipped == 2, "S20: 3 points, 2 lignes d'en-tête ignorées");
        CHECK_REL(f.points[1].second, 0.5 * 9.81, kExact, "S20: facteur d'unité appliqué");
        CHECK_REL(f.at(0.015), 0.125 * 9.81, kExact, "S20: interpolation linéaire");
        CHECK_ABS(f.at(0.03), 0.0, 0.0, "S20: nulle après la fin du signal");
        TEST_CHECK(parseTimeSeries("0.1 0.2\n0.3\n", 0.005, 1.0, f, &error) && f.points.size() == 3, "S20: valeurs successives au pas dt");
        CHECK_REL(f.points[2].first, 0.01, kExact, "S20: temps k·dt");
        TEST_CHECK(!parseTimeSeries("0 1 2\n", 0.0, 1.0, f, &error), "S20: trois colonnes sans pas refusées");
        TEST_CHECK(!parseTimeSeries("0 1\n0 2\n", 0.0, 1.0, f, &error), "S20: temps non croissants refusés");
    }
    std::cout << "[PASS] S20: Newmark — solution discrète exacte, énergie, échelon (×2), amortissement, sol harmonique (ordre 2), spectre, refus, lecture"
              << std::endl;
    ++passed;
    return true;
}

// =================================================================================================
// S21 — analyse spectrale (spectre fourni), SRSS / CQC
// =================================================================================================
bool testSpectral(int& passed)
{
    ResponseSpectrum spectrum;
    spectrum.points = { { 0.0, 2.0 }, { 2.0, 6.0 } };   // Sa = 2 + 2T (m/s²), spectre de vérification
    spectrum.source = "spectre de vérification";
    {   // 1 DDL : u = Sa/ω², effort tranchant m·Sa, moment en pied m·Sa·H
        const auto b = sdof();
        const ModalResult modal = modalAnalysis(b.m, 0, MassModel::Consistent);
        const SpectralResult r = spectralAnalysis(modal, spectrum, 0, ModalCombination::SRSS);
        TEST_CHECK(r.success, "S21: spectral 1 DDL — " << r.message);
        const double sa = 2.0 + 2.0 * kSdofT;
        CHECK_REL(r.Sa[0], sa, kExact, "S21: Sa(T1) interpolé");
        CHECK_REL(r.peakDisplacement[kTopUx], sa / (kSdofOmega * kSdofOmega), kExact, "S21: u = Sa/ω²");
        CHECK_REL(r.combinedBaseShear, kMass * sa, kExact, "S21: effort tranchant à la base m·Sa");
        CHECK_REL(r.massRatio, 1.0, kExact, "S21: 100 % de la masse");
        CHECK_REL(r.elementPeaks[0].endForces[2], kMass * sa * kH, kExact, "S21: moment en pied m·Sa·H");
        const SpectralResult cqc = spectralAnalysis(modal, spectrum, 0, ModalCombination::CQC, 0.05);
        CHECK_REL(cqc.peakDisplacement[kTopUx], r.peakDisplacement[kTopUx], kExact, "S21: CQC = SRSS pour un seul mode");
    }
    {   // coefficients CQC : ρ(ω, ω) = 1, symétrie, valeur de la formule de Der Kiureghian, modes éloignés → 0
        CHECK_REL(cqcCoefficient(10.0, 10.0, 0.05), 1.0, kExact, "S21: ρ_ii = 1");
        CHECK_REL(cqcCoefficient(10.0, 9.0, 0.05), cqcCoefficient(9.0, 10.0, 0.05), kExact, "S21: ρ symétrique");
        CHECK_REL(cqcCoefficient(10.0, 9.0, 0.05), 0.4730276832384835, 1e-12, "S21: ρ(r = 0,9 ; ξ = 5 %)");
        TEST_CHECK(cqcCoefficient(10.0, 100.0, 0.05) < 1e-3, "S21: modes éloignés quasi indépendants");
    }
    {   // poteau en console avec masse répartie : combinaisons SRSS et CQC recalculées depuis les réponses modales
        Builder b;
        for (int k = 0; k <= 8; ++k) b.node(0.0, 4.0 * k / 8.0);
        for (int k = 1; k <= 8; ++k) b.element(k, k + 1);
        b.support(1, true, true, true);
        ResponseSpectrum wide;
        wide.points = { { 0.0, 3.0 }, { 0.5, 8.0 }, { 5.0, 1.0 } };
        const ModalResult modal = modalAnalysis(b.m, 3, MassModel::Consistent);
        TEST_CHECK(modal.success, "S21: modes du poteau — " << modal.message);
        for (auto comb : { ModalCombination::SRSS, ModalCombination::CQC })
        {
            const SpectralResult r = spectralAnalysis(modal, wide, 0, comb, 0.05);
            TEST_CHECK(r.success, "S21: poteau — " << r.message);
            const int top = r.dofs.dof(9, 0);
            double s = 0.0;
            for (std::size_t i = 0; i < r.modalDisplacement.size(); ++i)
                for (std::size_t j = 0; j < r.modalDisplacement.size(); ++j)
                {
                    const double rho = comb == ModalCombination::SRSS ? (i == j ? 1.0 : 0.0) : cqcCoefficient(modal.omega[i], modal.omega[j], 0.05);
                    s += rho * r.modalDisplacement[i][static_cast<std::size_t>(top)] * r.modalDisplacement[j][static_cast<std::size_t>(top)];
                }
            CHECK_REL(r.peakDisplacement[static_cast<std::size_t>(top)], std::sqrt(s), kExact, "S21: combinaison des déplacements de tête");
            for (std::size_t k = 0; k < r.modalDisplacement.size(); ++k)
                CHECK_REL(r.Sd[k], r.Sa[k] / (modal.omega[k] * modal.omega[k]), kExact, "S21: Sd = Sa/ω²");
        }
        const ModalResult one = modalAnalysis(b.m, 1, MassModel::Consistent);
        const SpectralResult partial = spectralAnalysis(one, wide, 0, ModalCombination::SRSS);
        TEST_CHECK(partial.success && partial.massRatio < 0.9, "S21: un seul mode — masse effective partielle");
        bool warned = false;
        for (const auto& l : partial.log) warned = warned || l.find("90 %") != std::string::npos;
        TEST_CHECK(warned, "S21: avertissement masse effective < 90 %");
        ResponseSpectrum narrow;
        narrow.points = { { 0.1, 3.0 }, { 0.5, 8.0 } };
        const SpectralResult out = spectralAnalysis(modal, narrow, 0, ModalCombination::SRSS);
        TEST_CHECK(!out.success && out.message.find("hors du spectre") != std::string::npos, "S21: période hors du spectre refusée (pas d'extrapolation)");
    }
    std::cout << "[PASS] S21: spectral — 1 DDL (Sa/ω², m·Sa), coefficients CQC, combinaisons SRSS/CQC, masse effective, spectre incomplet refusé" << std::endl;
    ++passed;
    return true;
}

// =================================================================================================
// S22 — charges roulantes : lignes d'influence, théorème de Barré, superposition, chemins
// =================================================================================================
bool testMovingLoads(int& passed)
{
    const double L = 10.0;
    {   // lignes d'influence d'une poutre sur deux appuis : M(L/2) = s/2 puis (L − s)/2, R1 = (L − s)/L
        const auto b = simplySupported(L, 1);
        ResponseQuantity M { ResponseQuantity::Kind::BendingMoment, 0, 0, 1, L / 2.0 };
        const InfluenceLine im = influenceLine(b.m, { 1 }, 0.5, M);
        TEST_CHECK(im.success && im.s.size() == 21, "S22: ligne d'influence — " << im.message);
        for (std::size_t k = 0; k < im.s.size(); ++k)
            CHECK_ABS(im.value[k], im.s[k] <= L / 2.0 ? im.s[k] / 2.0 : (L - im.s[k]) / 2.0, 1e-9, "S22: LI de M(L/2), s = " << im.s[k]);
        CHECK_REL(im.maximum, L / 4.0, kExact, "S22: maximum L/4 en s = L/2");
        CHECK_REL(im.sMaximum, L / 2.0, kExact, "S22: position du maximum");
        ResponseQuantity R { ResponseQuantity::Kind::Reaction, 1, 1, 0, 0.0 };
        const InfluenceLine ir = influenceLine(b.m, { 1 }, 0.5, R);
        TEST_CHECK(ir.success, "S22: LI de réaction — " << ir.message);
        for (std::size_t k = 0; k < ir.s.size(); ++k) CHECK_ABS(ir.value[k], (L - ir.s[k]) / L, 1e-9, "S22: LI de R1, s = " << ir.s[k]);
        ResponseQuantity bad { ResponseQuantity::Kind::Reaction, 2, 0, 0, 0.0 };
        TEST_CHECK(!influenceLine(b.m, { 1 }, 0.5, bad).success, "S22: réaction sur un DDL libre refusée");
    }
    {   // théorème de Barré : deux essieux de 100 kN distants de 2 m, M max = 2P(L/2 − a/4)²/L = 405 kN·m en x = 4,5 m
        auto b = simplySupported(L, 1);
        Vehicle v { "deux essieux", { { 0.0, 100.0 }, { 2.0, 100.0 } } };
        MovingLoadOptions o;
        o.path = { 1 };
        o.step = 0.5;
        o.stations = 21;
        const MovingLoadResult r = movingLoadAnalysis(b.m, v, o);
        TEST_CHECK(r.success, "S22: convoi — " << r.message);
        const EnvelopeStation* mx = r.maxMoment();
        TEST_CHECK(mx != nullptr, "S22: enveloppe");
        CHECK_REL(mx->Mmax, 405.0, kExact, "S22: Barré — M max");
        TEST_CHECK(std::abs(mx->x - 4.5) < 1e-9 || std::abs(mx->x - 5.5) < 1e-9, "S22: Barré — section x = 4,5 ou 5,5 m (" << mx->x << ")");
        CHECK_REL(r.reactionMax[static_cast<std::size_t>(r.dofs.dof(1, 1))], 180.0, kExact, "S22: réaction max P + P·(L − 2)/L");
        bool honest = false;
        for (const auto& l : r.log) honest = honest || l.find("aucune interaction véhicule-pont") != std::string::npos;
        TEST_CHECK(honest, "S22: le journal précise qu'il s'agit d'une succession de calculs statiques");
        // superposition d'un cas permanent q = 5 kN/m : M max = q·x(L − x)/2 + 405 en x = 4,5 m
        b.lc(2).elementLoads.push_back(uniformLoad(1, -5.0));
        o.baseCase = 2;
        const MovingLoadResult g = movingLoadAnalysis(b.m, v, o);
        TEST_CHECK(g.success, "S22: avec cas permanent — " << g.message);
        CHECK_REL(g.maxMoment()->Mmax, 5.0 * 4.5 * 5.5 / 2.0 + 405.0, kExact, "S22: superposition G + convoi");
    }
    {   // élément décrit à rebours (nœud j → i) : même ligne d'influence que la poutre d'un seul tenant
        Builder b;
        b.node(0, 0), b.node(5, 0), b.node(10, 0);
        b.element(1, 2), b.element(3, 2);
        b.support(1, true, true, false), b.support(3, false, true, false);
        ResponseQuantity M { ResponseQuantity::Kind::BendingMoment, 0, 0, 1, 5.0 };
        const InfluenceLine im = influenceLine(b.m, { 1, 2 }, 0.5, M);
        TEST_CHECK(im.success, "S22: chemin à deux éléments — " << im.message);
        for (std::size_t k = 0; k < im.s.size(); ++k)
            CHECK_ABS(im.value[k], im.s[k] <= 5.0 ? im.s[k] / 2.0 : (L - im.s[k]) / 2.0, 1e-9, "S22: sens de parcours de l'élément 2, s = " << im.s[k]);
        TEST_CHECK(!influenceLine(b.m, { 1, 1 }, 0.5, M).success, "S22: élément répété refusé");
    }
    {   // poutre continue à deux travées égales : M sur appui intermédiaire −a(L² − a²)/(4L²) ; chemin discontinu refusé
        Builder b;
        b.node(0, 0), b.node(L, 0), b.node(2 * L, 0), b.node(3 * L, 0);
        b.element(1, 2), b.element(2, 3), b.element(3, 4);
        b.support(1, true, true, false), b.support(2, false, true, false), b.support(3, false, true, false);
        b.m.elements.pop_back();
        b.m.nodes.pop_back();
        ResponseQuantity MB { ResponseQuantity::Kind::BendingMoment, 0, 0, 1, L };
        const InfluenceLine im = influenceLine(b.m, { 1, 2 }, 0.5, MB);
        TEST_CHECK(im.success, "S22: poutre continue — " << im.message);
        for (std::size_t k = 0; k < im.s.size(); ++k)
        {
            const double a = im.s[k] <= L ? im.s[k] : 2.0 * L - im.s[k];
            CHECK_ABS(im.value[k], -a * (L * L - a * a) / (4.0 * L * L), 1e-9, "S22: LI de M_B, s = " << im.s[k]);
        }
        Builder c = b;
        c.node(3 * L, 0);
        c.element(3, 4);
        c.node(4 * L, 0);
        c.element(4, 5);
        TEST_CHECK(!influenceLine(c.m, { 1, 4 }, 0.5, MB).success, "S22: chemin discontinu refusé");
    }
    std::cout << "[PASS] S22: charges roulantes — lignes d'influence exactes, théorème de Barré (405 kN·m), superposition, sens de parcours, poutre continue"
              << std::endl;
    ++passed;
    return true;
}

} // namespace

int dynamicsTestCount() { return 4; }

bool testDynamics(int& passed)
{
    return testModal(passed) && testTimeHistory(passed) && testSpectral(passed) && testMovingLoads(passed);
}
