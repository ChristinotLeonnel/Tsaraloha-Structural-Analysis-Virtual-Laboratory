// Tests du cœur scientifique TSALab (sans Qt) : numerics, solveurs plans, export du système,
// banc de validation (solution analytique + validation croisée).
#include "tsalab/numerics/SolverComparison.h"
#include "tsalab/planar/PlanarSolvers.h"
#include "tsalab/validation/PlanarBenchmarks.h"

#include <algorithm>
#include <cmath>
#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << #cond << ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

using namespace tsalab;
using numerics::Matrix;
using numerics::Vector;

namespace
{

bool approx(double a, double b, double eps) { return std::abs(a - b) <= eps; }

// Matrice SDP n×n de type « barre discrétisée » (tridiagonale 2, −1) + couplage faible, bien posée.
Matrix springChain(int n)
{
    Matrix K(n, n);
    for (int i = 0; i < n; ++i)
    {
        K(i, i) = 2.0 + 0.01 * i;
        if (i + 1 < n) K(i, i + 1) = K(i + 1, i) = -1.0;
        if (i + 3 < n) K(i, i + 3) = K(i + 3, i) = 0.05;
    }
    return K;
}

bool testSolvers(int& passed)
{
    // S1 : les trois solveurs retrouvent la solution connue d'un système SDP
    {
        const int n = 40;
        Vector reference(n);
        for (int i = 0; i < n; ++i) reference[std::size_t(i)] = std::sin(0.3 * i) + 0.1 * i;
        const auto problem = numerics::makeProblem(springChain(n), reference);
        for (auto m : { numerics::SolverMethod::GaussLU, numerics::SolverMethod::Cholesky, numerics::SolverMethod::ConjugateGradient })
        {
            const auto run = numerics::runSolver(problem, m);
            TEST_CHECK(run.report.success, "S1: résolution réussie");
            TEST_CHECK(run.deviationFromReference >= 0.0 && run.deviationFromReference < 1e-8, "S1: écart à la référence < 1e-8");
            TEST_CHECK(run.report.relativeResidual < 1e-9, "S1: résidu relatif < 1e-9");
        }
        const auto cg = numerics::runSolver(problem, numerics::SolverMethod::ConjugateGradient);
        TEST_CHECK(cg.report.iterations > 1 && cg.report.iterations <= n, "S1: gradient conjugué en ≤ n itérations");
        std::cout << "[PASS] S1: Gauss LU, Cholesky et gradient conjugué concordent" << std::endl;
        ++passed;
    }
    // S2 : mécanisme détecté par les pivots de Cholesky, sans résultat inventé
    {
        Matrix K(3, 3);
        const double k = 1000.0;
        K(0, 0) = k;  K(0, 1) = -k;
        K(1, 0) = -k; K(1, 1) = 2 * k; K(1, 2) = -k;
        K(2, 1) = -k; K(2, 2) = k;
        Vector f = { 1.0, 0.0, -1.0 }, x;
        numerics::SolverSettings s;
        s.method = numerics::SolverMethod::Cholesky;
        const auto r = numerics::solve(K, f, x, s);
        TEST_CHECK(!r.success && r.failedEquation >= 0, "S2: système singulier refusé, équation identifiée");
        std::cout << "[PASS] S2: mécanisme détecté (pivot nul)" << std::endl;
        ++passed;
    }
    // S3 : valeurs propres et conditionnement
    {
        Matrix A(2, 2);
        A(0, 0) = 2.0; A(0, 1) = 1.0; A(1, 0) = 1.0; A(1, 1) = 2.0;
        const auto e = numerics::symmetricEigen(A);
        TEST_CHECK(e.success && approx(e.values[0], 1.0, 1e-10) && approx(e.values[1], 3.0, 1e-10), "S3: λ = {1, 3}");
        TEST_CHECK(approx(numerics::conditionNumber(A), 3.0, 1e-9), "S3: κ = 3");
        std::cout << "[PASS] S3: valeurs propres de Jacobi et conditionnement" << std::endl;
        ++passed;
    }
    return true;
}

bool testPlanar(int& passed)
{
    // S4 : solveurs intégrés et export du système K·U = F
    {
        auto solvers = planar::createBuiltInSolvers();
        TEST_CHECK(!solvers.empty(), "S4: au moins un solveur intégré");
        auto& mdd = *solvers.front();
        TEST_CHECK(!mdd.version().empty() && mdd.features().linearSystem, "S4: version réelle, export du système déclaré");

        const auto& b = validation::planarBenchmarks().front();
        planar::Input in = b.input;
        const auto noSystem = mdd.solve(in);
        TEST_CHECK(noSystem.success && !noSystem.system.available, "S4: pas d'export sans demande");
        in.options.exportSystem = true;
        const auto out = mdd.solve(in);
        TEST_CHECK(out.success && out.system.available, "S4: système exporté sur demande");
        TEST_CHECK(out.system.equations == static_cast<int>(out.system.dofs.size())
                       && out.system.loads.size() == out.system.displacements.size(),
                   "S4: dimensions cohérentes");
        // K·U = F sur le système exporté
        Matrix K(out.system.equations, out.system.equations);
        for (const auto& e : out.system.stiffness) K(e.row, e.col) = e.value;
        TEST_CHECK(numerics::LinAlg::asymmetry(K) < 1e-12, "S4: K symétrique");
        const Vector KU = numerics::LinAlg::multiply(K, out.system.displacements);
        const double res = numerics::LinAlg::norm2(numerics::LinAlg::subtract(KU, out.system.loads))
                           / std::max(numerics::LinAlg::norm2(out.system.loads), 1e-300);
        TEST_CHECK(res < 1e-10, "S4: K·U = F (résidu relatif < 1e-10)");
        // Équation ↔ DDL : le déplacement exporté est celui du nœud correspondant
        for (std::size_t e = 0; e < out.system.dofs.size(); ++e)
        {
            const auto [node, dof] = out.system.dofs[e];
            const auto it = std::find_if(out.displacements.begin(), out.displacements.end(), [node = node](const auto& d) { return d.node == node; });
            TEST_CHECK(it != out.displacements.end(), "S4: nœud de l'équation connu");
            const double u = dof == 0 ? it->ux : dof == 1 ? it->uy : it->rz;
            TEST_CHECK(approx(u, out.system.displacements[e], 1e-15), "S4: numérotation équation ↔ (nœud, DDL)");
        }
        std::cout << "[PASS] S4: export du système K·U = F de MetDeDeplacement" << std::endl;
        ++passed;
    }
    // S5 : banc de validation complet (solution analytique + validation croisée)
    {
        auto solvers = planar::createBuiltInSolvers();
        std::vector<validation::BenchmarkReport> reports;
        for (const auto& b : validation::planarBenchmarks()) reports.push_back(validation::runBenchmark(b, *solvers.front()));
        const std::string text = validation::formatReport(reports);
        for (const auto& r : reports)
        {
            if (!r.passed) std::cerr << text;
            TEST_CHECK(r.passed, "S5: benchmark " << r.id);
            // Sans DDL libre, il n'y a pas de système à résoudre : pas de validation croisée.
            TEST_CHECK(r.crossChecked || r.id == "fixed-fixed-single-bar", "S5: validation croisée effectuée (" << r.id << ")");
        }
        TEST_CHECK(reports.size() >= 7, "S5: au moins 7 benchmarks");
        std::cout << "[PASS] S5: " << reports.size() << " benchmarks validés (A ≈ B ≈ C)" << std::endl;
        ++passed;
    }
    // S6 : deux logiciels indépendants — MetDeDeplacement et OpenSees valident chaque benchmark et donnent les
    // mêmes déplacements, réactions et efforts d'extrémité (ignoré, et signalé, si OpenSees est absent).
    {
        auto solvers = planar::createBuiltInSolvers();
        TEST_CHECK(solvers.size() >= 2 && solvers[1]->name().find("OpenSees") != std::string::npos,
                   "S6: OpenSees est le second solveur intégré");
        planar::ISolver& mdd = *solvers[0];
        planar::ISolver& ops = *solvers[1];
        std::string why;
        if (!ops.available(&why))
        {
            std::cout << "[PASS] S6: ignoré — " << why << std::endl;
            ++passed;
            return true;
        }
        int compared = 0;
        for (const auto& b : validation::planarBenchmarks())
        {
            const auto rep = validation::runBenchmark(b, ops);
            if (!rep.passed) std::cerr << validation::formatReport({ rep });
            TEST_CHECK(rep.passed, "S6: OpenSees valide le benchmark " << b.id);
            const planar::Output a = mdd.solve(b.input), o = ops.solve(b.input);
            TEST_CHECK(a.success && o.success, "S6: deux calculs réussis (" << b.id << ")");
            double scale = 1e-12, gap = 0.0;
            for (const auto& d : a.displacements) scale = std::max({ scale, std::abs(d.ux), std::abs(d.uy) });
            for (const auto& d : a.displacements)
                for (const auto& e : o.displacements)
                    if (d.node == e.node) gap = std::max({ gap, std::abs(d.ux - e.ux), std::abs(d.uy - e.uy) });
            TEST_CHECK(gap <= 1e-6 * scale, "S6: déplacements identiques (" << b.id << ", écart " << gap / scale << ")");
            double rScale = 1e-12, rGap = 0.0;
            for (const auto& r : a.reactions) rScale = std::max({ rScale, std::abs(r.fx), std::abs(r.fy), std::abs(r.mz) });
            for (const auto& r : a.reactions)
                for (const auto& q : o.reactions)
                    if (r.node == q.node) rGap = std::max({ rGap, std::abs(r.fx - q.fx), std::abs(r.fy - q.fy), std::abs(r.mz - q.mz) });
            TEST_CHECK(rGap <= 1e-6 * rScale, "S6: réactions identiques (" << b.id << ")");
            double fScale = 1e-12, fGap = 0.0;
            for (const auto& f : a.elementForces) fScale = std::max({ fScale, std::abs(f.mzI), std::abs(f.mzJ), std::abs(f.fyI), std::abs(f.fxI) });
            for (const auto& f : a.elementForces)
                for (const auto& g : o.elementForces)
                    if (f.element == g.element)
                        fGap = std::max({ fGap, std::abs(f.fxI - g.fxI), std::abs(f.fyI - g.fyI), std::abs(f.mzI - g.mzI),
                                          std::abs(f.fxJ - g.fxJ), std::abs(f.fyJ - g.fyJ), std::abs(f.mzJ - g.mzJ) });
            if (fGap > 1e-6 * fScale)
                for (const auto* set : { &a, &o })
                    for (const auto& f : set->elementForces)
                        std::cerr << (set == &a ? "MDD " : "OPS ") << f.element << " : " << f.fxI << ' ' << f.fyI << ' ' << f.mzI
                                  << " | " << f.fxJ << ' ' << f.fyJ << ' ' << f.mzJ << '\n';
            TEST_CHECK(fGap <= 1e-6 * fScale, "S6: efforts d'extrémité identiques (" << b.id << ", écart " << fGap / fScale << ")");
            ++compared;
        }
        std::cout << "[PASS] S6: OpenSees " << ops.version() << " ≈ MetDeDeplacement sur " << compared << " benchmarks" << std::endl;
        ++passed;
    }
    return true;
}

} // namespace

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    constexpr int kExpected = 6;
    int passed = 0;
    const bool ok = testSolvers(passed) && testPlanar(passed);
    std::cout << "\nRESULTS: " << passed << " / " << kExpected << " tests passed" << (ok ? " successfully!" : " (FAILURE)") << std::endl;
    return ok && passed == kExpected ? 0 : 1;
}
