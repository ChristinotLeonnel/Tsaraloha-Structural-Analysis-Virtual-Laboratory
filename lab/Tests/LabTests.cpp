// Tests propres à TSALab (exécutable TSALab_TestSuite) : solveurs du laboratoire, comparaison de
// solveurs (SOLVER LAB), exemples, format .tsalab et import des modèles TSA.
// La base commune (modèle, viewport, IO…) est couverte par les 212 tests de TSA.
#include "App/ProductInfo.h"
#include "IO/TSAFile.h"
#include "IO/TSAFileFormat.h"
#include "Model/Model.h"
#include "Research/Examples/ExampleModels.h"
#include "Research/Numerics/LinearAlgebra.h"
#include "Research/Solver/SolverExperiment.h"

#include <QApplication>
#include <QTemporaryDir>

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << #cond << ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

inline bool approxEqual(double a, double b, double eps = 1e-4) { return std::abs(a - b) <= eps; }

using namespace TSALab::Research;

namespace
{

constexpr int kLabTests = 5;

// Matrice SDP nÃ—n de type Â« barre discrÃ©tisÃ©e Â» (tridiagonale 2, âˆ’1) + couplage faible, bien posÃ©e.
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

bool runSuite_Lab(int& passed)
{
    // TEST L1 : les trois solveurs retrouvent la solution connue d'un systÃ¨me SDP
    {
        const int n = 40;
        Vector reference(n);
        for (int i = 0; i < n; ++i) reference[std::size_t(i)] = std::sin(0.3 * i) + 0.1 * i;
        const SolverExperimentInput input = makeSolverExperiment(springChain(n), reference);
        for (SolverMethod m : { SolverMethod::GaussLU, SolverMethod::Cholesky, SolverMethod::ConjugateGradient })
        {
            const SolverRun run = runSolver(input, m);
            TEST_CHECK(run.report.success, "Test L1: rÃ©solution rÃ©ussie");
            TEST_CHECK(run.deviationFromReference >= 0.0 && run.deviationFromReference < 1e-8, "Test L1: Ã©cart Ã  la rÃ©fÃ©rence < 1e-8");
            TEST_CHECK(run.report.relativeResidual < 1e-9, "Test L1: rÃ©sidu relatif < 1e-9");
        }
        const SolverRun cg = runSolver(input, SolverMethod::ConjugateGradient);
        TEST_CHECK(cg.report.iterations > 1 && cg.report.iterations <= n, "Test L1: gradient conjuguÃ© converge en â‰¤ n itÃ©rations");
        TEST_CHECK(cg.report.residualHistory.size() >= 2, "Test L1: historique de convergence disponible");
        std::cout << "[PASS] Test L1: Gauss LU, Cholesky et gradient conjuguÃ© concordent" << std::endl;
        ++passed;
    }

    // TEST L2 : mÃ©canisme (K singuliÃ¨re) dÃ©tectÃ© par les pivots de Cholesky, sans rÃ©sultat inventÃ©
    {
        Matrix K(3, 3); // trois ressorts en sÃ©rie sans appui : mode rigide
        const double k = 1000.0;
        K(0, 0) = k;  K(0, 1) = -k;
        K(1, 0) = -k; K(1, 1) = 2 * k; K(1, 2) = -k;
        K(2, 1) = -k; K(2, 2) = k;
        Vector f = { 1.0, 0.0, -1.0 }, x;
        SolverSettings s;
        s.method = SolverMethod::Cholesky;
        const SolverReport r = solve(K, f, x, s);
        TEST_CHECK(!r.success, "Test L2: systÃ¨me singulier refusÃ©");
        TEST_CHECK(r.failedEquation >= 0, "Test L2: Ã©quation du pivot nul identifiÃ©e");
        TEST_CHECK(!std::isfinite(conditionNumber(K)) || conditionNumber(K) > 1e12, "Test L2: conditionnement infini");
        std::cout << "[PASS] Test L2: mÃ©canisme dÃ©tectÃ© (pivot nul, Ã©quation " << r.failedEquation << ")" << std::endl;
        ++passed;
    }

    // TEST L3 : valeurs propres et conditionnement d'une matrice connue
    {
        Matrix A(2, 2);
        A(0, 0) = 2.0; A(0, 1) = 1.0; A(1, 0) = 1.0; A(1, 1) = 2.0; // Î» = 1 et 3
        const EigenResult e = symmetricEigen(A);
        TEST_CHECK(e.success && e.values.size() == 2, "Test L3: dÃ©composition rÃ©ussie");
        TEST_CHECK(approxEqual(e.values[0], 1.0, 1e-10) && approxEqual(e.values[1], 3.0, 1e-10), "Test L3: Î» = {1, 3}");
        TEST_CHECK(approxEqual(conditionNumber(A), 3.0, 1e-9), "Test L3: Îº = 3");
        std::cout << "[PASS] Test L3: valeurs propres de Jacobi et conditionnement" << std::endl;
        ++passed;
    }

    // TEST L4 : chaque exemple du catalogue est un vrai modÃ¨le, enregistrÃ© au format natif .tsalab
    {
        QTemporaryDir dir;
        TEST_CHECK(dir.isValid(), "Test L4: dossier temporaire");
        TEST_CHECK(!Examples::catalog().empty(), "Test L4: catalogue non vide");
        for (const auto& ex : Examples::catalog())
        {
            TSA::Model::Model model;
            TEST_CHECK(Examples::build(ex.id, model), "Test L4: exemple construit");
            TEST_CHECK(model.nodes().size() >= 2, "Test L4: nÅ“uds prÃ©sents");
            TEST_CHECK(!model.loadManager().loadCases().empty(), "Test L4: cas de charge prÃ©sent");
            const QString path = dir.filePath(QString::fromStdString(ex.id) + TSA::Product::projectExtension());
            QString error;
            TEST_CHECK(TSA::IO::TSAProjectIO::saveProject(path, model, nullptr, QString::fromStdString(ex.title), QString(),
                                                          true, QImage(), &error), "Test L4: enregistrement .tsalab");
            TSA::IO::TSAFileHeader header;
            TEST_CHECK(TSA::IO::TSAFileReader::readHeader(path.toStdString(), header), "Test L4: en-tÃªte lisible");
            TEST_CHECK(header.magic == 0x424C5354, "Test L4: signature TSLB (TSALab)");
            TSA::Model::Model reloaded;
            TEST_CHECK(TSA::IO::TSAProjectIO::loadFromFile(path, reloaded, nullptr), "Test L4: relecture");
            TEST_CHECK(reloaded.nodes().size() == model.nodes().size(), "Test L4: nÅ“uds conservÃ©s");
        }
        TEST_CHECK(!Examples::build("inconnu", *std::make_unique<TSA::Model::Model>()), "Test L4: identifiant inconnu refusÃ©");
        std::cout << "[PASS] Test L4: " << Examples::catalog().size() << " exemples construits, enregistrÃ©s et relus" << std::endl;
        ++passed;
    }

    // TEST L5 : un modÃ¨le TSA (.tsa, signature TSAF) s'ouvre dans TSALab (import), une signature
    //           Ã©trangÃ¨re est refusÃ©e
    {
        QTemporaryDir dir;
        TSA::Model::Model model;
        TEST_CHECK(Examples::build("cantilever", model), "Test L5: modÃ¨le source");
        const QString tsaPath = dir.filePath("modele_tsa.tsa");
        TEST_CHECK(TSA::IO::TSAProjectIO::saveProject(tsaPath, model), "Test L5: Ã©criture");
        auto patchMagic = [](const QString& path, uint32_t magic) {
            std::fstream f(path.toStdString(), std::ios::in | std::ios::out | std::ios::binary);
            f.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
            return bool(f);
        };
        TEST_CHECK(patchMagic(tsaPath, TSA::IO::TSA_FILE_MAGIC), "Test L5: signature TSAF (fichier de TSA)");
        TSA::Model::Model imported;
        TEST_CHECK(TSA::IO::TSAProjectIO::loadFromFile(tsaPath, imported, nullptr), "Test L5: modÃ¨le TSA importÃ©");
        TEST_CHECK(imported.nodes().size() == model.nodes().size(), "Test L5: contenu importÃ©");
        TEST_CHECK(TSA::Product::isOpenableProjectFile(tsaPath) && !TSA::Product::isNativeProjectFile(tsaPath),
                   "Test L5: .tsa ouvrable mais non natif (jamais rÃ©Ã©crit)");
        TEST_CHECK(TSA::Product::withProjectExtension(tsaPath).endsWith(".tsalab"), "Test L5: enregistrement redirigÃ© vers .tsalab");
        TEST_CHECK(patchMagic(tsaPath, 0x58585858), "Test L5: signature Ã©trangÃ¨re");
        TSA::Model::Model rejected;
        TEST_CHECK(!TSA::IO::TSAProjectIO::loadFromFile(tsaPath, rejected, nullptr), "Test L5: signature inconnue refusÃ©e");
        std::cout << "[PASS] Test L5: import des modÃ¨les TSA (.tsa) et refus des signatures inconnues" << std::endl;
        ++passed;
    }
    return true;
}

} // namespace

int main(int argc, char* argv[])
{
    QApplication app(argc, argv); // aperçu des projets (QPainter, polices) : application graphique requise
    std::cout << "=================================================" << std::endl;
    std::cout << "TSALab Unit Tests" << std::endl;
    std::cout << "=================================================" << std::endl;
    int passed = 0;
    const bool ok = runSuite_Lab(passed);
    std::cout << "\nRESULTS: " << passed << " / " << kLabTests << " tests passed" << (ok ? " successfully!" : " (FAILURE)") << std::endl;
    return ok && passed == kLabTests ? 0 : 1;
}
