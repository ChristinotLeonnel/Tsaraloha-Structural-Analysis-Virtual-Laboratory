// Tests de l'application TSALab (exécutable TSALab_TestSuite) : exemples, format .tsalab et import des
// modèles TSA, Blueprints, analyse (contrôleur partagé) et SOLVER LAB. Le cœur scientifique a ses propres tests (science/tests, tsalab_science_tests) ; la base
// commune (modèle, viewport, IO…) est couverte par les 212 tests de TSA.
#include "Analysis/AnalysisController.h"
#include "Analysis/ResultsModel.h"
#include "App/ProductInfo.h"
#include "Automation/CommandRegistry.h"
#include "IO/TSAFile.h"
#include "IO/TSAFileFormat.h"
#include "Model/Model.h"
#include "Blueprint/BlueprintFile.h"
#include "Blueprint/BlueprintRuntime.h"
#include "Model/Beam.h"
#include "Model/Load/LoadManager.h"
#include "Project/ProjectSession.h"
#include "Research/Examples/BlueprintExamples.h"
#include "Research/Examples/ExampleModels.h"
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

constexpr int kLabTests = 4;

bool runSuite_Lab(int& passed)
{
    // TEST L4 : chaque exemple du catalogue est un vrai modèle, enregistré au format natif .tsalab
    {
        QTemporaryDir dir;
        TEST_CHECK(dir.isValid(), "Test L4: dossier temporaire");
        TEST_CHECK(!Examples::catalog().empty(), "Test L4: catalogue non vide");
        for (const auto& ex : Examples::catalog())
        {
            TSA::Model::Model model;
            TEST_CHECK(Examples::build(ex.id, model), "Test L4: exemple construit");
            TEST_CHECK(model.nodes().size() >= 2, "Test L4: nœuds présents");
            TEST_CHECK(!model.loadManager().loadCases().empty(), "Test L4: cas de charge présent");
            const QString path = dir.filePath(QString::fromStdString(ex.id) + TSA::Product::projectExtension());
            QString error;
            TEST_CHECK(TSA::IO::TSAProjectIO::saveProject(path, model, nullptr, QString::fromStdString(ex.title), QString(),
                                                          true, QImage(), &error), "Test L4: enregistrement .tsalab");
            TSA::IO::TSAFileHeader header;
            TEST_CHECK(TSA::IO::TSAFileReader::readHeader(path.toStdString(), header), "Test L4: en-tête lisible");
            TEST_CHECK(header.magic == 0x424C5354, "Test L4: signature TSLB (TSALab)");
            TSA::Model::Model reloaded;
            TEST_CHECK(TSA::IO::TSAProjectIO::loadFromFile(path, reloaded, nullptr), "Test L4: relecture");
            TEST_CHECK(reloaded.nodes().size() == model.nodes().size(), "Test L4: nœuds conservés");
        }
        TEST_CHECK(!Examples::build("inconnu", *std::make_unique<TSA::Model::Model>()), "Test L4: identifiant inconnu refusé");
        std::cout << "[PASS] Test L4: " << Examples::catalog().size() << " exemples construits, enregistrés et relus" << std::endl;
        ++passed;
    }

    // TEST L5 : un modèle TSA (.tsa, signature TSAF) s'ouvre dans TSALab (import), une signature
    //           étrangère est refusée
    {
        QTemporaryDir dir;
        TSA::Model::Model model;
        TEST_CHECK(Examples::build("cantilever", model), "Test L5: modèle source");
        const QString tsaPath = dir.filePath("modele_tsa.tsa");
        TEST_CHECK(TSA::IO::TSAProjectIO::saveProject(tsaPath, model), "Test L5: écriture");
        auto patchMagic = [](const QString& path, uint32_t magic) {
            std::fstream f(path.toStdString(), std::ios::in | std::ios::out | std::ios::binary);
            f.write(reinterpret_cast<const char*>(&magic), sizeof(magic));
            return bool(f);
        };
        TEST_CHECK(patchMagic(tsaPath, TSA::IO::TSA_FILE_MAGIC), "Test L5: signature TSAF (fichier de TSA)");
        TSA::Model::Model imported;
        TEST_CHECK(TSA::IO::TSAProjectIO::loadFromFile(tsaPath, imported, nullptr), "Test L5: modèle TSA importé");
        TEST_CHECK(imported.nodes().size() == model.nodes().size(), "Test L5: contenu importé");
        TEST_CHECK(TSA::Product::isOpenableProjectFile(tsaPath) && !TSA::Product::isNativeProjectFile(tsaPath),
                   "Test L5: .tsa ouvrable mais non natif (jamais réécrit)");
        TEST_CHECK(TSA::Product::withProjectExtension(tsaPath).endsWith(".tsalab"), "Test L5: enregistrement redirigé vers .tsalab");
        TEST_CHECK(patchMagic(tsaPath, 0x58585858), "Test L5: signature étrangère");
        TSA::Model::Model rejected;
        TEST_CHECK(!TSA::IO::TSAProjectIO::loadFromFile(tsaPath, rejected, nullptr), "Test L5: signature inconnue refusée");
        std::cout << "[PASS] Test L5: import des modèles TSA (.tsa) et refus des signatures inconnues" << std::endl;
        ++passed;
    }
    // TEST L6 : Blueprints d'exemple — exécutés sur un vrai projet, après aller-retour .tsbp
    {
        const auto& lib = TSA::Blueprint::NodeLibrary::standard();
        QTemporaryDir dir;
        for (const auto& ex : BlueprintExamples::catalog())
        {
            TSA::Blueprint::Graph g;
            TEST_CHECK(BlueprintExamples::build(ex.id, g), "Test L6: exemple construit (" << ex.id << ")");
            TEST_CHECK(lib.validate(g).empty(), "Test L6: exemple valide (" << ex.id << ")");
            const QString path = dir.filePath(QString::fromStdString(ex.id) + ".tsbp");
            TSA::Blueprint::Graph back;
            TEST_CHECK(TSA::Blueprint::saveFile(g, path) && TSA::Blueprint::loadFile(path, back), "Test L6: aller-retour .tsbp");

            TSA::Project::ProjectSession session;
            const auto r = TSA::Blueprint::Runner(lib, back, &session).run();
            TEST_CHECK(r.ok, "Test L6: exécution de " << ex.id << " (" << r.message << ")");
            const auto& m = session.model();
            if (ex.id == "parametric-portal")
            {
                int columns = 0, beams = 0;
                for (const auto& [id, b] : m.beams()) (b.role() == TSA::Model::BarRole::Column ? columns : beams) += 1;
                TEST_CHECK(m.nodes().size() == 4 && columns == 2 && beams == 1 && m.loadManager().memberLoads().size() == 1,
                           "Test L6: portique (4 nœuds, 2 poteaux, 1 poutre, 1 charge)");
                TEST_CHECK(m.getNode(3) && approxEqual(m.getNode(3)->x(), 6.0) && approxEqual(m.getNode(3)->z(), 3.0),
                           "Test L6: géométrie issue des paramètres (portée 6, hauteur 3)");
                TSA::Project::ProjectSession wide;
                TEST_CHECK(TSA::Blueprint::Runner(lib, back, &wide).run({ { "portée", 9.0 } }).ok
                               && approxEqual(wide.model().getNode(3)->x(), 9.0),
                           "Test L6: reconstruction paramétrique (portée 9 m)");
            }
            else if (ex.id == "node-row")
                TEST_CHECK(m.nodes().size() == 5 && approxEqual(m.getNode(5)->x(), 10.0), "Test L6: 5 nœuds au pas de 2,5 m");
            else if (ex.id == "validation-bench")
                TEST_CHECK(!r.log.empty() && r.log.back().find("benchmark(s) validé(s)") != std::string::npos,
                           "Test L6: rapport du banc de validation dans le journal");
        }
        std::cout << "[PASS] Test L6: " << BlueprintExamples::catalog().size() << " Blueprints d'exemple exécutés" << std::endl;
        ++passed;
    }
    // TEST L7 : analyse dans TSALab — exemples plans calculés sans grille (portée « plan du modèle »), système
    // K·U = F exporté, rejoué par SOLVER LAB (Gauss LU, Cholesky, gradient conjugué)
    {
        const auto& reg = TSA::Automation::CommandRegistry::builtIn();
        int planar = 0;
        for (const auto& ex : Examples::catalog())
        {
            if (ex.id == "frame-3d") continue;   // portique spatial : non plan
            TSA::Project::ProjectSession session;
            TEST_CHECK(Examples::build(ex.id, session.model()), "Test L7: exemple construit (" << ex.id << ")");
            const auto r = TSA::Automation::executeCommandLine(reg, session, "analysis.run engine=custom2d axis=plan export_system=true");
            TEST_CHECK(r.ok, "Test L7: " << ex.id << " calculé (" << r.message << ")");
            const auto results = session.analysis().results();
            SolverExperimentInput input;
            std::string why;
            if (ex.id == "fixed-fixed")
            {
                // Deux nœuds encastrés : aucun DDL libre, K vide — signalé, jamais inventé.
                TEST_CHECK(results && !results->advanced().hasGlobalStiffness && !results->advanced().kGlobalUnavailableReason.empty()
                               && !buildSolverExperiment(*results, input, &why) && why.find("aucun degré de liberté") != std::string::npos,
                           "Test L7: poutre bi-encastrée sans DDL libre signalée");
                ++planar;
                continue;
            }
            TEST_CHECK(results && results->advanced().hasGlobalStiffness, "Test L7: K exportée (" << ex.id << ")");
            TEST_CHECK(buildSolverExperiment(*results, input, &why), "Test L7: expérience SOLVER LAB (" << ex.id << ", " << why << ")");
            for (SolverMethod m : { SolverMethod::GaussLU, SolverMethod::Cholesky, SolverMethod::ConjugateGradient })
            {
                const SolverRun run = runSolver(input, m, SolverSettings {});
                TEST_CHECK(run.report.success && run.deviationFromReference >= 0.0 && run.deviationFromReference < 1e-6,
                           "Test L7: " << solverMethodName(m) << " retrouve U du moteur (" << ex.id << ")");
            }
            ++planar;
        }
        TEST_CHECK(planar == 5, "Test L7: 5 exemples plans calculés");
        // Portique spatial : aucun plan, le moteur 2D refuse explicitement.
        TSA::Project::ProjectSession space;
        TEST_CHECK(Examples::build("frame-3d", space.model()), "Test L7: portique spatial construit");
        TEST_CHECK(!TSA::Automation::executeCommandLine(reg, space, "analysis.run engine=custom2d axis=plan").ok,
                   "Test L7: portique spatial refusé en 2D (modèle non plan)");
        std::cout << "[PASS] Test L7: analyse et SOLVER LAB sur les exemples" << std::endl;
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
