// Tests de l'application TSALab (exécutable TSALab_TestSuite) : exemples, format .tsalab et import des
// modèles TSA, Blueprints, analyse (contrôleur partagé), SOLVER LAB et plugins (DLL d'exemple). Le cœur scientifique a ses propres tests (science/tests, tsalab_science_tests) ; la base
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
#include "Plugins/PluginManager.h"
#include "Project/ProjectSession.h"
#include "Research/Examples/BlueprintExamples.h"
#include "Research/Examples/ExampleModels.h"
#include "Research/Solver/SolverExperiment.h"
#include "Research/Validation/ValidationRunner.h"
#include "Research/Experiments/ExperimentJournal.h"
#include "Blueprint/BlueprintScript.h"

#include <QApplication>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>

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

constexpr int kLabTests = 7;

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
    // TEST L8 : plugin d'exemple (DLL) — commande composée (console, Blueprint, IA) et nœud pur
    {
        auto& plugins = TSA::Plugins::PluginManager::instance();
        QString error;
        TEST_CHECK(plugins.loadFile(QStringLiteral(TSALAB_SAMPLE_PLUGIN), &error), "Test L8: plugin chargé (" << error.toStdString() << ")");
        TEST_CHECK(plugins.loadFile(QStringLiteral(TSALAB_SAMPLE_PLUGIN)) && plugins.plugins().size() == 1, "Test L8: chargé une seule fois");
        const auto& rec = plugins.plugins().front();
        TEST_CHECK(rec.info.id == "tsalab.sample" && rec.commands.size() == 1 && rec.nodes.size() == 1 && !rec.log.isEmpty(),
                   "Test L8: informations, commande, nœud et journal du plugin");
        const auto& reg = TSA::Automation::CommandRegistry::builtIn();
        const auto& lib = TSA::Blueprint::NodeLibrary::standard();
        TEST_CHECK(reg.find("sample.portal") && lib.find("cmd.sample.portal") && lib.find("sample.golden"),
                   "Test L8: commande au registre, nœud de commande et nœud pur dans la bibliothèque");

        // Commande composée : chaque étape garde son entrée Annuler.
        TSA::Project::ProjectSession session;
        const auto r = TSA::Automation::executeCommandLine(reg, session, "sample.portal span=8 height=4");
        TEST_CHECK(r.ok && session.model().nodes().size() == 4 && session.model().beams().size() == 3
                       && session.model().loadManager().memberLoads().size() == 1,
                   "Test L8: portique du plugin (" << r.message << ")");
        TEST_CHECK(session.model().getNode(3) && approxEqual(session.model().getNode(3)->x(), 8.0), "Test L8: portée imposée");
        int undo = 0;
        while (session.undo()) ++undo;
        TEST_CHECK(undo >= 8 && session.model().nodes().empty(), "Test L8: étapes annulables une par une");

        // Blueprint : script → nœud de commande du plugin ; nœud pur du plugin relié à « Afficher ».
        TSA::Blueprint::Graph g;
        std::string why;
        TEST_CHECK(TSA::Blueprint::fromCommandScript("p = sample.portal span=5\nquery.model_summary", lib, reg, g, &why),
                   "Test L8: commande du plugin dans un script (" << why << ")");
        const int s = g.addNode("event.start", 0, 300);
        const int golden = g.addNode("sample.golden");
        g.setValue(golden, "x", 2.0);
        const int print = g.addNode("debug.print");
        TEST_CHECK(lib.connect(g, { s, "then", print, "exec" }, &why) && lib.connect(g, { golden, "y", print, "value" }, &why),
                   "Test L8: liens vers le nœud du plugin");
        TSA::Project::ProjectSession bp;
        const auto report = TSA::Blueprint::Runner(lib, g, &bp).run();
        bool goldenPrinted = false;
        for (const auto& line : report.log) goldenPrinted |= line.rfind("3.236", 0) == 0;
        TEST_CHECK(report.ok && goldenPrinted && bp.model().nodes().size() == 4, "Test L8: Blueprint exécuté (portique + 2φ affiché)");

        TEST_CHECK(!plugins.loadFile(QStringLiteral("C:/nulle/part/plugin.dll"), &error) && !error.isEmpty(),
                   "Test L8: DLL introuvable refusée avec un message");
        std::cout << "[PASS] Test L8: plugin d'exemple (commande composée, nœud pur)" << std::endl;
        ++passed;
    }
    // -------------------------------------------------------------------------
    // TEST L9 : laboratoire de validation (banc partagé avec TSA)
    // -------------------------------------------------------------------------
    {
        using namespace TSALab::Research;
        const QString dir = defaultBenchmarkDirectory();
        const auto cases = loadCases(dir);
        int valid = 0;
        for (const auto& c : cases) valid += c.valid();
        TEST_CHECK(QDir(dir).exists() && cases.size() == 27 && valid == 27, "Test L9: 27 cas du banc partagé chargés et valides (" << cases.size() << ", " << valid << ")");

        // Cas invalide : erreurs listées, pas d'exécution ni de plantage
        QTemporaryDir tmp;
        {
            QFile f(tmp.filePath("BAD.json"));
            f.open(QIODevice::WriteOnly);
            f.write(R"({"format":"tsa.benchmark/1","id":"BAD","title":"Cas invalide","checks":[{"quantity":"node:1:quoi","formula":"1/"}]})");
        }
        const auto bad = loadCases(tmp.filePath("BAD.json"));
        TEST_CHECK(bad.size() == 1 && !bad[0].valid() && !bad[0].errors.isEmpty(), "Test L9: cas invalide signalé avec ses erreurs");

        // Exécution réelle : barre en traction (RDM-01) et portique (RDM-06), OpenSees et Custom2D
        std::vector<TSA::Validation::BenchmarkCase> run;
        for (const auto& c : cases)
            if (c.benchmark.id == "RDM-01" || c.benchmark.id == "RDM-06") run.push_back(c.benchmark);
        int progressCalls = 0;
        const auto report = runValidation(run, {}, [&](const ValidationProgress&) { ++progressCalls; });
        int checks = 0;
        for (const auto& c : report.cases) checks += static_cast<int>(c.checks.size());
        std::cout << "  " << report.checksPassed() << " réussies, " << report.checksFailed() << " en échec, " << report.checksUnavailable()
                  << " non disponibles ; moteurs : " << report.engineVersions << std::endl;
        TEST_CHECK(run.size() == 2 && report.cases.size() == 4 && checks > 0 && report.checksPassed() == checks && report.checksFailed() == 0,
                   "Test L9: RDM-01 et RDM-06 sur OpenSees et Custom2D — toutes les vérifications réussies (" << report.checksPassed() << " / " << checks << ")");
        TEST_CHECK(report.tsaVersion.rfind("TSALab", 0) == 0 && report.engineVersions.find("OpenSees 3.8") != std::string::npos &&
                       report.engineVersions.find("Custom2D") != std::string::npos && report.engineVersions.find("OpenSees 3.8.0 (version inconnue)") == std::string::npos && !report.timestamp.empty() && progressCalls == 3,
                   "Test L9: en-tête du rapport — version de TSALab, versions réelles des moteurs, date, progression");

        // Arrêt demandé avant le premier cas : rien n'est exécuté
        const auto cancelled = runValidation(run, {}, {}, [] { return true; });
        TEST_CHECK(cancelled.cases.empty(), "Test L9: arrêt demandé — aucun cas exécuté");

        // Références externes : valeur fausse détectée ; référence nulle → écart relatif non défini, tolérance absolue
        {
            QFile f(tmp.filePath("ref.csv"));
            f.open(QIODevice::WriteOnly);
            f.write("case,quantity,value,unit,source,rel_tol,abs_tol\n"
                    "RDM-01,node:2:ux,1.0,m,logiciel fictif (essai d'erreur),0.01,0\n"
                    "RDM-01,node:2:uz,0,m,symétrie (référence nulle),0,1e-9\n");
        }
        TSA::Validation::RunOptions options;
        std::vector<std::string> errors;
        options.external = TSA::Validation::loadExternalReferences(tmp.filePath("ref.csv"), &errors);
        options.engines = { "opensees" };
        const auto ext = runValidation({ run[0].id == "RDM-01" ? run[0] : run[1] }, options);
        const TSA::Validation::CheckOutcome* wrong = nullptr;
        const TSA::Validation::CheckOutcome* zero = nullptr;
        for (const auto& c : ext.cases)
            for (const auto& k : c.checks)
            {
                if (k.quantity == "node:2:ux" && k.source.find("fictif") != std::string::npos) wrong = &k;
                if (k.quantity == "node:2:uz") zero = &k;
            }
        TEST_CHECK(errors.empty() && options.external.size() == 2 && wrong && !wrong->passed && checkStatusText(*wrong) == "ÉCHEC",
                   "Test L9: référence externe erronée détectée (ÉCHEC), provenance conservée");
        TEST_CHECK(zero && std::isnan(zero->relError) && relativeErrorText(*zero) == "réf. ≈ 0" && zero->passed,
                   "Test L9: référence nulle — écart relatif non défini (« réf. ≈ 0 »), jugée sur la tolérance absolue");
        std::cout << "[PASS] Test L9: laboratoire de validation (cas, exécution réelle, écarts, références externes)" << std::endl;
        ++passed;
    }
    // -------------------------------------------------------------------------
    // TEST L10 : journal d'expériences (traçabilité et reproduction)
    // -------------------------------------------------------------------------
    {
        using namespace TSALab::Research;
        QTemporaryDir tmp;
        ExperimentJournal journal(tmp.filePath("Essai.experiments"));
        const auto& reg = TSA::Automation::CommandRegistry::builtIn();
        TSA::Project::ProjectSession session;
        TEST_CHECK(Examples::build("frame-3d", session.model()), "Test L10: portique spatial construit");
        int completed = 0;
        std::string completedEngine;
        QObject::connect(&session.analysis(), &TSA::Analysis::AnalysisController::runCompleted,
                         [&](const TSA::Analysis::AnalysisContext& c, bool ok, const QString&) { ++completed, completedEngine = ok ? c.engineId : std::string(); });
        const auto r = TSA::Automation::executeCommandLine(reg, session, "analysis.run engine=opensees");
        const auto results = session.analysis().results();
        TEST_CHECK(r.ok && results && results->isValid(), "Test L10: calcul OpenSees (" << r.message << ")");
        TEST_CHECK(completed == 1 && completedEngine == "opensees", "Test L10: fin de calcul signalée aussi pour un calcul en console (runCompleted, contexte calculé)");
        const auto context = session.analysis().context();
        TEST_CHECK(context.engineId == "opensees", "Test L10: contexte du calcul relu sur le contrôleur (" << context.engineId << ")");
        QString err;
        const auto rec = journal.recordAnalysis(session.model(), &session.grids(), context, true, QString::fromStdString(r.message), results.get(),
                                                session.analysis().registry(), tmp.filePath("Essai.tsalab"), nullptr, &err);
        TEST_CHECK(err.isEmpty() && rec.status == "réussi" && rec.labVersion.startsWith("TSALab") && rec.engineId == "opensees" && rec.engineVersion == "3.8.0" &&
                       rec.modelHash.size() == 64 && QFile::exists(QDir(rec.folder).filePath(rec.modelSnapshot)) && rec.units.value("force").toString() == "kN" &&
                       rec.results.contains("displacement.max_translation") && rec.results.contains("reaction.sum_fz") && rec.provenance.contains("OpenSees 3.8.0") &&
                       !rec.analysisContext.isEmpty(),
                   "Test L10: enregistrement — version, moteur et version lue, modèle figé + SHA-256, contexte, unités, résultats, provenance (" << err.toStdString() << ")");
        const auto list = journal.records();
        TEST_CHECK(list.size() == 1 && list[0].id == rec.id && list[0].results == rec.results && list[0].analysisContext == rec.analysisContext,
                   "Test L10: expérience relue depuis le disque à l'identique");
        const auto rep = ExperimentJournal::reproduce(list[0]);
        for (const auto& c : rep.comparisons)
            if (!c.equal) std::cout << "  écart " << c.quantity.toStdString() << " : " << c.original << " / " << c.reproduced << std::endl;
        TEST_CHECK(rep.ran && rep.identical() && rep.comparisons.size() >= 6, "Test L10: rejeu — mêmes résultats (" << rep.comparisons.size() << " grandeurs ; " << rep.message.toStdString() << ")");

        // Copie figée modifiée : rejeu refusé (empreinte)
        {
            QFile f(QDir(rec.folder).filePath(rec.modelSnapshot));
            f.open(QIODevice::Append);
            f.write("x");
        }
        const auto tampered = ExperimentJournal::reproduce(list[0]);
        TEST_CHECK(!tampered.ran && tampered.message.contains("SHA-256"), "Test L10: copie figée modifiée — rejeu refusé (" << tampered.message.toStdString() << ")");

        // Calcul non abouti : statut « échec », erreur conservée, aucun résultat
        const auto failed = journal.recordAnalysis(session.model(), &session.grids(), context, false, "moteur indisponible", nullptr, session.analysis().registry(), QString());
        TEST_CHECK(failed.status == "échec" && failed.errors.contains("moteur indisponible") && failed.results.isEmpty(), "Test L10: calcul non abouti enregistré avec son erreur");

        // Exécution du banc : bilan et cas
        std::vector<TSA::Validation::BenchmarkCase> one;
        for (const auto& c : loadCases(defaultBenchmarkDirectory()))
            if (c.benchmark.id == "RDM-01") one.push_back(c.benchmark);
        const auto vrec = journal.recordValidation(runValidation(one, {}));
        TEST_CHECK(vrec.kind == "validation" && vrec.status == "réussi" && vrec.results.value("checks.passed").toInt() > 0 && vrec.provenance.contains("banc de validation"),
                   "Test L10: exécution du banc enregistrée (bilan, moteurs, cas)");
        TEST_CHECK(!ExperimentJournal::reproduce(vrec).ran, "Test L10: une exécution du banc ne se rejoue pas comme un calcul (message explicite)");

        // Enregistrement illisible : ignoré, les autres restent lisibles
        QDir(journal.directory()).mkpath("corrompu");
        {
            QFile f(QDir(journal.directory()).filePath("corrompu/record.json"));
            f.open(QIODevice::WriteOnly);
            f.write("{pas du json");
        }
        TEST_CHECK(journal.records().size() == 3, "Test L10: enregistrement illisible ignoré (" << journal.records().size() << " lus)");
        TEST_CHECK(ExperimentJournal::directoryForProject("C:/Etudes/Pont.tsalab").endsWith("Pont.experiments"), "Test L10: journal à côté du projet");
        std::cout << "[PASS] Test L10: journal d'expériences (enregistrement, relecture, rejeu, intégrité)" << std::endl;
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
