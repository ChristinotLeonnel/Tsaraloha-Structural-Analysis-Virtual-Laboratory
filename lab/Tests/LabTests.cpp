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
#include "Project/ProjectManager.h"
#include "Research/Examples/BlueprintExamples.h"
#include "Research/Examples/ExampleModels.h"
#include "Research/Solver/SolverExperiment.h"
#include "Research/Validation/ValidationRunner.h"
#include "Research/Experiments/ExperimentJournal.h"
#include "Research/Reference/AnalyticalReference.h"
#include "Research/Validation/MeshConvergence.h"
#include "Mesh/MeshGenerator.h"
#include "Mesh/MeshProject.h"
#include "Mesh/Solvers/OpenSeesMeshAdapter.h"
#include "Model/Slab.h"
#include "Analysis/ResultsExport.h"
#include "Blueprint/BlueprintScript.h"

#include <QApplication>
#include <QTemporaryDir>
#include <variant>
#include <QImage>
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

constexpr int kLabTests = 11;

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
            else if (ex.id == "simply-supported-experiment")
            {
                // Chaîne complète en nœuds : modèle, propriétés, calcul, extraction, comparaison
                for (const auto& line : r.log) std::cout << "    " << line << std::endl;
                int compared = 0;
                for (const auto& line : r.log) compared += line.find("dans la tolérance") != std::string::npos;
                TEST_CHECK(m.nodes().size() == 3 && m.beams().size() == 2 && m.beams().begin()->second.section().name == "IPE 300" && compared == 3 &&
                               r.log.back().find("référence (flèche, réaction, moment) : vrai") != std::string::npos,
                           "Test L6: expérience en nœuds — 3 comparaisons dans la tolérance (flèche, réaction, moment)");
                const auto ref = TSALab::Research::simplySupportedCentralLoad(m, session.analysis().context(), session.analysis().results().get());
                TEST_CHECK(ref.applicable && ref.passed(), "Test L6: le modèle produit par le graphe est reconnu par la référence analytique du laboratoire");
                TSA::Project::ProjectSession other;
                const auto r2 = TSA::Blueprint::Runner(lib, back, &other).run({ { "section", std::string("IPE 999") } });
                TEST_CHECK(!r2.ok && r2.message.find("catalogue") != std::string::npos, "Test L6: section hors catalogue — exécution arrêtée, raison donnée");
            }
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
            if (ex.id == "frame-3d" || ex.id == "square-plate") continue;   // portique spatial, plaque : hors du moteur 2D
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
        TEST_CHECK(planar == 6, "Test L7: 6 exemples plans calculés");
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
        TEST_CHECK(QDir(dir).exists() && cases.size() == 28 && valid == 28, "Test L9: 28 cas du banc partagé chargés et valides (" << cases.size() << ", " << valid << ")");

        // Cas invalide : erreurs listées, pas d'exécution ni de plantage
        QTemporaryDir tmp;
        {
            QFile f(tmp.filePath("BAD.json"));
            TEST_CHECK(f.open(QIODevice::WriteOnly), "fichier d'essai ouvert en écriture");
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
            TEST_CHECK(f.open(QIODevice::WriteOnly), "fichier d'essai ouvert en écriture");
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
            TEST_CHECK(f.open(QIODevice::Append), "fichier d'essai ouvert en écriture");
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
            TEST_CHECK(f.open(QIODevice::WriteOnly), "fichier d'essai ouvert en écriture");
            f.write("{pas du json");
        }
        TEST_CHECK(journal.records().size() == 3, "Test L10: enregistrement illisible ignoré (" << journal.records().size() << " lus)");
        TEST_CHECK(ExperimentJournal::directoryForProject("C:/Etudes/Pont.tsalab").endsWith("Pont.experiments"), "Test L10: journal à côté du projet");
        std::cout << "[PASS] Test L10: journal d'expériences (enregistrement, relecture, rejeu, intégrité)" << std::endl;
        ++passed;
    }
    // -------------------------------------------------------------------------
    // TEST L11 : première expérience complète — poutre bi-appuyée, force ponctuelle centrale, référence d'Euler-Bernoulli
    // -------------------------------------------------------------------------
    {
        using namespace TSALab::Research;
        const auto& reg = TSA::Automation::CommandRegistry::builtIn();
        QTemporaryDir tmp;

        // 1-3. Modèle créé par les commandes du registre (chemin de l'utilisateur : console, Blueprint, MCP)
        TSA::Project::ProjectSession user;
        const char* script[] = { "model.create_node position=0,0,0", "model.create_node position=3,0,0", "model.create_node position=6,0,0",
                                 "model.create_beam start=1 end=2 section=\"IPE 300\" material=S235", "model.create_beam start=2 end=3 section=\"IPE 300\" material=S235",
                                 "model.set_support node=1 type=PINNED", "model.set_support node=3 type=ROLLER" };
        for (const char* line : script)
        {
            const auto r = TSA::Automation::executeCommandLine(reg, user, line);
            TEST_CHECK(r.ok, "Test L11: commande « " << line << " » (" << r.message << ")");
        }
        const auto lc = TSA::Automation::executeCommandLine(reg, user, "loads.create_case name=P category=live");
        TEST_CHECK(lc.ok, "Test L11: cas de charge (" << lc.message << ")");
        const int caseId = static_cast<int>(std::get<long long>(lc.outputs.at("id")));
        TEST_CHECK(TSA::Automation::executeCommandLine(reg, user, ("loads.add_nodal node=2 case=" + std::to_string(caseId) + " fz=-50").c_str()).ok, "Test L11: force de 50 kN à mi-portée");

        // 4-7. Calcul par chaque moteur disponible, comparaison à la référence analytique
        for (const char* engine : { "custom2d", "opensees" })
        {
            auto ctx = user.analysis().context();
            ctx.common.includeSelfWeight = false;   // hypothèse de la formule
            ctx.loadCaseIds = { caseId };
            user.analysis().setContext(ctx);
            const std::string cmd = std::string("analysis.run engine=") + engine + (std::string(engine) == "custom2d" ? " axis=plan" : "");
            const auto run = TSA::Automation::executeCommandLine(reg, user, cmd.c_str());
            TEST_CHECK(run.ok, "Test L11: calcul " << engine << " (" << run.message << ")");
            const auto results = user.analysis().results();
            const auto ref = simplySupportedCentralLoad(user.model(), user.analysis().context(), results.get());
            for (const auto& v : ref.violations) std::cout << "  hypothèse non remplie : " << v.toStdString() << std::endl;
            for (const auto& c : ref.checks)
                std::cout << "  " << engine << " — " << c.label.toStdString() << " : " << c.computed << " / " << c.reference << " " << c.unit.toStdString()
                          << " (écart " << c.absError << ")" << (c.passed ? "" : "  ÉCHEC") << std::endl;
            TEST_CHECK(ref.applicable && ref.checks.size() == 5 && ref.passed() && ref.warnings.isEmpty(),
                       "Test L11: " << engine << " — réactions P/2, flèche P·L³/(48EI), M = P·L/4, V = P/2 dans la tolérance 1e-6");
        }
        // Valeur de contrôle indépendante : δ = 50·6³/(48·210e6·Iy) avec Iy de l'IPE 300
        const double iy = TSA::Model::Section::ipe(300).iy();
        const auto refNow = simplySupportedCentralLoad(user.model(), user.analysis().context(), user.analysis().results().get());
        TEST_CHECK(std::abs(refNow.checks[2].reference - (-50.0 * 216.0 / (48.0 * 210e6 * iy))) < 1e-15 && std::abs(refNow.checks[3].reference - 75.0) < 1e-12,
                   "Test L11: valeurs de référence (δ = " << refNow.checks[2].reference << " m, M = " << refNow.checks[3].reference << " kN·m)");

        // Hypothèse non remplie : nommée, aucune comparaison
        auto withWeight = user.analysis().context();
        withWeight.common.includeSelfWeight = true;
        const auto refused = simplySupportedCentralLoad(user.model(), withWeight, user.analysis().results().get());
        TEST_CHECK(!refused.applicable && refused.checks.empty() && refused.violations.join(" ").contains("poids propre"),
                   "Test L11: poids propre inclus — référence non appliquée, raison nommée");
        TSA::Project::ProjectSession portal;
        Examples::build("portal", portal.model());
        const auto notThis = simplySupportedCentralLoad(portal.model(), portal.analysis().context(), nullptr);
        TEST_CHECK(!notThis.applicable && notThis.violations.size() >= 1, "Test L11: portique — reconnu comme hors du domaine de la référence");

        // 1 et 8. Exemple de référence : enregistrer, rouvrir (modèle, réglages), expérience journalisée puis relue
        TSA::Project::ProjectSession ex;
        TEST_CHECK(Examples::build("simply-supported-point", ex.model()), "Test L11: exemple de référence construit");
        TEST_CHECK(ex.analysis().restoreContextFromModel() && !ex.analysis().context().common.includeSelfWeight, "Test L11: réglages de l'exemple (poids propre exclu) relus");
        const QString file = tmp.filePath(QStringLiteral("Poutre%1").arg(TSA::Product::projectExtension()));
        QString err;
        TEST_CHECK(ex.project().saveProject(file, ex.model(), &ex.grids(), QImage(), &err), "Test L11: projet enregistré (" << err.toStdString() << ")");
        TSA::Project::ProjectSession reopened;
        TEST_CHECK(reopened.project().openProject(file, reopened.model(), &reopened.grids(), &err), "Test L11: projet rouvert (" << err.toStdString() << ")");
        TEST_CHECK(reopened.model().nodes().size() == 3 && reopened.model().beams().size() == 2 && reopened.analysis().restoreContextFromModel() &&
                       !reopened.analysis().context().common.includeSelfWeight,
                   "Test L11: modèle et réglages de calcul identiques après réouverture");
        const auto runEx = TSA::Automation::executeCommandLine(reg, reopened, "analysis.run engine=custom2d axis=plan");
        TEST_CHECK(runEx.ok, "Test L11: exemple rouvert calculé (" << runEx.message << ")");
        const auto refEx = simplySupportedCentralLoad(reopened.model(), reopened.analysis().context(), reopened.analysis().results().get());
        ExperimentJournal journal(ExperimentJournal::directoryForProject(file));
        const auto rec = journal.recordAnalysis(reopened.model(), &reopened.grids(), reopened.analysis().context(), true, QString(),
                                                reopened.analysis().results().get(), reopened.analysis().registry(), file, nullptr, &err, refEx.toJson());
        TEST_CHECK(refEx.passed() && rec.reference.value("applicable").toBool() && rec.warnings.isEmpty(), "Test L11: expérience journalisée avec sa référence, sans avertissement");
        const auto again = ExperimentJournal(ExperimentJournal::directoryForProject(file)).records();
        const auto back = again.empty() ? ReferenceComparison {} : ReferenceComparison::fromJson(again.back().reference);
        TEST_CHECK(again.size() == 1 && back.passed() && back.checks.size() == 5 && std::abs(back.checks[2].computed - refEx.checks[2].computed) == 0.0 &&
                       back.parameters == refEx.parameters,
                   "Test L11: expérience rouverte depuis le disque — référence, écarts et paramètres intacts");
        std::cout << "[PASS] Test L11: première expérience complète (modèle, calcul sur 2 moteurs, référence analytique, enregistrement, réouverture)" << std::endl;
        ++passed;
    }
    // -------------------------------------------------------------------------
    // TEST L12 : laboratoire matriciel — données réellement exportées par le moteur, export CSV, aucune valeur inventée
    // -------------------------------------------------------------------------
    {
        using namespace TSA::Analysis;
        const auto& reg = TSA::Automation::CommandRegistry::builtIn();
        TSA::Project::ProjectSession s;
        TEST_CHECK(Examples::build("simply-supported-point", s.model()) && s.analysis().restoreContextFromModel(), "Test L12: expérience de référence");
        TEST_CHECK(TSA::Automation::executeCommandLine(reg, s, "analysis.run engine=custom2d axis=plan export_system=true").ok, "Test L12: calcul Custom2D, système exporté");
        const auto r = s.analysis().results();
        const auto& adv = r->advanced();
        const std::size_t n = adv.dofMap.equations.size();
        TEST_CHECK(n > 0 && adv.hasGlobalStiffness && adv.loadVector.size() == n && adv.displacementVector.size() == n,
                   "Test L12: K, F, U et numérotation des DDL exportés par Custom2D (" << n << " équations)");
        // K·U = F à partir des seules données exportées
        std::vector<double> ku(n, 0.0);
        for (std::size_t q = 0; q < adv.kGlobal.values.size(); ++q) ku[std::size_t(adv.kGlobal.rowIndex[q])] += adv.kGlobal.values[q] * adv.displacementVector[std::size_t(adv.kGlobal.colIndex[q])];
        double res = 0, fmax = 0, fsum = 0;
        for (std::size_t i = 0; i < n; ++i) res = std::max(res, std::abs(ku[i] - adv.loadVector[i])), fmax = std::max(fmax, std::abs(adv.loadVector[i])), fsum += adv.loadVector[i];
        TEST_CHECK(res <= 1e-9 * fmax && std::abs(fsum + 50.0) < 1e-9, "Test L12: K·U = F vérifié sur les données exportées (résidu " << res << ", ΣF = " << fsum << " kN)");
        std::string err;
        const std::string fu = ResultsExport::render(*r, ResultsDataset::SystemVectors, ResultsExportFormat::Csv, &err);
        TEST_CHECK(err.empty() && fu.find("equation,node,dof,F,U,U_source") != std::string::npos && fu.find("-50") != std::string::npos && fu.find(",moteur") != std::string::npos,
                   "Test L12: export CSV des vecteurs F et U (provenance « moteur ») (" << err << ")");
        // Custom2D n'exporte pas de matrices élémentaires : transformations refusées avec la raison, jamais inventées
        std::string errT;
        const std::string trC = ResultsExport::render(*r, ResultsDataset::Transformations, ResultsExportFormat::Csv, &errT);
        TEST_CHECK(trC.empty() && errT.find("indisponibles") != std::string::npos, "Test L12: Custom2D — transformations non exportées, refus explicite (" << errT << ")");
        const std::string kg = ResultsExport::render(*r, ResultsDataset::GlobalStiffness, ResultsExportFormat::Csv, &err);
        TEST_CHECK(err.empty() && kg.find("row,col,row_dof,col_dof,value") != std::string::npos, "Test L12: export CSV de K globale");

        // OpenSees en extraction avancée : matrices élémentaires (rotation comprise) et K globale exportées
        {
            auto ctx = s.analysis().context();
            QJsonObject os = ctx.settingsFor("opensees");
            os.insert("extractionLevel", static_cast<int>(ExtractionLevel::Advanced));
            ctx.engineSettings["opensees"] = os;
            s.analysis().setContext(ctx);
        }
        TEST_CHECK(TSA::Automation::executeCommandLine(reg, s, "analysis.run engine=opensees").ok, "Test L12: calcul OpenSees (extraction avancée)");
        const auto adv2 = s.analysis().results();
        std::string errA;
        const std::string trO = ResultsExport::render(*adv2, ResultsDataset::Transformations, ResultsExportFormat::Csv, &errA);
        int rows = 0;
        for (char ch : trO) rows += ch == '\n';
        TEST_CHECK(errA.empty() && rows == 3 && trO.find("x_X,x_Y,x_Z") != std::string::npos && trO.find("elasticBeamColumn") != std::string::npos,
                   "Test L12: OpenSees — transformations local/global des 2 barres exportées (" << errA << ")");
        const auto& em = adv2->advanced().elementMatrices;
        TEST_CHECK(!em.empty() && em.begin()->second.available && em.begin()->second.kLocal.rows == 12 && adv2->advanced().hasGlobalStiffness,
                   "Test L12: OpenSees — matrices locales 12×12 et K globale exportées");
        const std::string fuO = ResultsExport::render(*adv2, ResultsDataset::SystemVectors, ResultsExportFormat::Csv, &errA);
        TEST_CHECK(fuO.find("non exporté par ce moteur") != std::string::npos && fuO.find("déplacements nodaux") != std::string::npos,
                   "Test L12: OpenSees — F non exporté (colonne vide, signalé), U déduit des déplacements et marqué comme tel");

        // OpenSees en mode léger : aucune matrice exportée → refus explicite, aucune valeur inventée
        {
            auto ctx = s.analysis().context();
            QJsonObject os = ctx.settingsFor("opensees");
            os.insert("extractionLevel", static_cast<int>(ExtractionLevel::Light));
            ctx.engineSettings["opensees"] = os;
            s.analysis().setContext(ctx);
        }
        TEST_CHECK(TSA::Automation::executeCommandLine(reg, s, "analysis.run engine=opensees").ok, "Test L12: calcul OpenSees (mode léger)");
        const auto o = s.analysis().results();
        std::string e1, e2, e3;
        const std::string a1 = ResultsExport::render(*o, ResultsDataset::GlobalStiffness, ResultsExportFormat::Csv, &e1);
        const std::string a2 = ResultsExport::render(*o, ResultsDataset::SystemVectors, ResultsExportFormat::Csv, &e2);
        const std::string a3 = ResultsExport::render(*o, ResultsDataset::Transformations, ResultsExportFormat::Csv, &e3);
        std::cout << "  OpenSees léger — K : " << e1 << " | F, U : " << e2 << " | T : " << e3 << std::endl;
        TEST_CHECK(!o->advanced().hasGlobalStiffness && a1.empty() && !e1.empty(), "Test L12: K globale absente — refus explicite avec la raison du moteur");
        TEST_CHECK(o->advanced().loadVector.empty() && (a2.empty() ? !e2.empty() : a2.find(",,") != std::string::npos), "Test L12: F non exporté par OpenSees — jamais reconstitué");
        std::cout << "[PASS] Test L12: laboratoire matriciel (K, F, U, DDL, transformations ; données absentes refusées)" << std::endl;
        ++passed;
    }
    // -------------------------------------------------------------------------
    // TEST L13 : convergence en maillage — série structurée du banc, ordre observé, extrapolation, chaîne Gmsh → OpenSees
    // -------------------------------------------------------------------------
    {
        using namespace TSALab::Research;
        using TSA::Validation::CheckOutcome;
        using TSA::Validation::ConvergencePoint;
        // Série synthétique f(h) = 1 + 0,1·h² : ordre 2, Richardson exact
        CheckOutcome k;
        k.caseId = "SYN", k.quantity = "surface:uz:absmax", k.reference = 1.0;
        for (double h : { 1.0, 0.5, 0.25 }) k.convergence.push_back(ConvergencePoint { h, "maillage", true, 1.0 + 0.1 * h * h });
        auto st = analyseMeshConvergence(k);
        TEST_CHECK(st.applicable && st.errorDecreasing && std::abs(st.refinementRatio - 2.0) < 1e-12 && std::abs(st.observedOrder - 2.0) < 1e-9 &&
                       std::abs(st.extrapolated - 1.0) < 1e-12 && st.extrapolatedRelError < 1e-12,
                   "Test L13: ordre observé 2 et extrapolation de Richardson exacts sur f = 1 + 0,1·h² (p = " << st.observedOrder << ")");
        const QString csv = meshConvergenceCsv(k, st);
        TEST_CHECK(csv.contains("mesh_size,mesh,value,rel_error_to_reference,rel_change") && csv.count('\n') == 6 && csv.contains("non calculée par le moteur"),
                   "Test L13: CSV de convergence (en-tête, 3 maillages, extrapolation signalée comme non issue du moteur)");
        // Rapport non constant, oscillation, maillage manquant : ordre non calculé, raison donnée
        CheckOutcome nc = k;
        nc.convergence[2].meshSize = 0.2;
        st = analyseMeshConvergence(nc);
        TEST_CHECK(st.applicable && std::isnan(st.observedOrder) && std::isnan(st.extrapolated) && st.orderNote.contains("non constant"), "Test L13: rapport non constant — ordre non calculé (" << st.orderNote.toStdString() << ")");
        CheckOutcome osc = k;
        osc.convergence[2].value = 1.04;
        st = analyseMeshConvergence(osc);
        TEST_CHECK(std::isnan(st.observedOrder) && st.orderNote.contains("oscillante") && !st.errorDecreasing, "Test L13: convergence oscillante — ordre non défini, écart non monotone signalé");
        CheckOutcome miss = k;
        miss.convergence[1].available = false;
        st = analyseMeshConvergence(miss);
        TEST_CHECK(st.applicable && std::isnan(st.observedOrder) && std::isnan(st.rows[1].relErrorToReference) && std::isnan(st.rows[2].relChange),
                   "Test L13: maillage non calculé — aucune valeur inventée, ordre non calculé");
        CheckOutcome single;
        single.reference = 1.0;
        TEST_CHECK(!analyseMeshConvergence(single).applicable && meshConvergenceCsv(single, analyseMeshConvergence(single)).isEmpty(), "Test L13: calcul unique — pas d'étude");

        // Chaîne réelle : FEA-01 (plaque, Gmsh → coques OpenSees) sur 3 maillages
        std::vector<TSA::Validation::BenchmarkCase> run;
        for (const auto& c : loadCases(defaultBenchmarkDirectory()))
            if (c.benchmark.id == "FEA-01") run.push_back(c.benchmark);
        TEST_CHECK(run.size() == 1, "Test L13: cas FEA-01 présent");
        const auto report = runValidation(run, {});
        const CheckOutcome* uz = nullptr;
        for (const auto& c : report.cases)
            for (const auto& ck : c.checks)
                if (ck.quantity == "surface:uz:absmax") uz = &ck;
        TEST_CHECK(uz && uz->convergence.size() == 3, "Test L13: FEA-01 — série de 3 maillages enregistrée par le banc");
        if (uz)
        {
            const auto fea = analyseMeshConvergence(*uz);
            for (const auto& r : fea.rows)
                std::cout << "  FEA-01 h=" << r.meshSize << " (" << r.mesh.toStdString() << ") uz=" << r.value << " écart réf. " << r.relErrorToReference * 100 << " %" << std::endl;
            std::cout << "  ordre observé : " << fea.observedOrder << " (" << fea.orderNote.toStdString() << ") ; extrapolé : " << fea.extrapolated << " (écart "
                      << fea.extrapolatedRelError * 100 << " %)" << std::endl;
            bool meshes = true;
            for (const auto& r : fea.rows) meshes = meshes && r.available && !r.mesh.isEmpty();
            TEST_CHECK(fea.applicable && meshes && uz->passed && fea.rows.back().value == uz->value, "Test L13: FEA-01 — 3 maillages générés et calculés, vérification sur le plus fin réussie");
        }
        // FEA-07 : MITC4 avec appui dur contre la référence de Mindlin — convergence d'ordre 2, Richardson sur la référence
        std::vector<TSA::Validation::BenchmarkCase> hard;
        for (const auto& c : loadCases(defaultBenchmarkDirectory()))
            if (c.benchmark.id == "FEA-07") hard.push_back(c.benchmark);
        const auto rep7 = runValidation(hard, {});
        const CheckOutcome* uz7 = nullptr;
        for (const auto& c : rep7.cases)
            for (const auto& ck : c.checks)
                if (ck.quantity == "surface:uz:absmax") uz7 = &ck;
        TEST_CHECK(uz7 && uz7->convergence.size() == 4, "Test L13: FEA-07 — série de 4 maillages");
        if (uz7)
        {
            const auto st7 = analyseMeshConvergence(*uz7);
            std::cout << "  FEA-07 : ordre observé " << st7.observedOrder << ", écart du maillage le plus fin " << st7.rows.back().relErrorToReference * 100
                      << " %, extrapolé " << st7.extrapolatedRelError * 100 << " %" << std::endl;
            TEST_CHECK(uz7->passed && st7.errorDecreasing && std::abs(st7.observedOrder - 2.0) < 0.2 && st7.extrapolatedRelError < 1e-4,
                       "Test L13: FEA-07 — écart décroissant, ordre observé ≈ 2, extrapolation de Richardson sur la référence de Mindlin");
        }
        std::cout << "[PASS] Test L13: convergence en maillage (série du banc, ordre observé, Richardson, chaîne Gmsh → OpenSees)" << std::endl;
        ++passed;
    }
    // -------------------------------------------------------------------------
    // TEST L14 : plaque dans TSALab — exemple maillé (Gmsh, paramètres enregistrés), coques OpenSees, référence de Mindlin
    // -------------------------------------------------------------------------
    {
        using namespace TSALab::Research;
        TSA::Project::ProjectSession s;
        TEST_CHECK(Examples::build("square-plate", s.model()) && s.analysis().restoreContextFromModel(), "Test L14: exemple de plaque construit, réglages relus");
        const auto mp = TSA::Mesh::MeshProject::load(s.model());
        TEST_CHECK(!mp.hasMesh && mp.settings.global.recombine && std::abs(mp.settings.global.effectiveSize() - 0.25) < 1e-9 &&
                       s.analysis().context().common.useGeneratedMesh && s.analysis().context().engineId == "opensees",
                   "Test L14: paramètres de maillage (quadrangles, h = 0,25 m) et de calcul enregistrés, sans maillage");
        // Sans maillage transmis : la référence refuse (dalle non calculée)
        const auto noMesh = analyticalReference(s.model(), s.analysis().context(), nullptr);
        TEST_CHECK(!noMesh.applicable && noMesh.violations.join(" ").contains("maillage non transmis"), "Test L14: sans maillage — référence non appliquée, raison nommée");

        const auto g = TSA::Mesh::MeshGeometry::extract(s.model());
        const auto mesh = TSA::Mesh::MeshGenerator::generate(g, mp.settings);
        const auto* engine = s.analysis().registry().engine("opensees");
        TEST_CHECK(engine && engine->availability().available, "Test L14: OpenSees disponible");
        const auto compat = TSA::Mesh::OpenSeesMeshAdapter::check(mesh.mesh, g, "OpenSees", engine->capabilities(), 3000000, &s.model());
        TEST_CHECK(!compat.blocking() && compat.surfaces.mesh.quads == 576, "Test L14: 576 quadrangles transmissibles aux coques OpenSees");
        auto ctx = s.analysis().context();
        ctx.surfaces = compat.surfaces;
        ctx.surfaces.meshStamp = "L14";
        ctx.meshLabel = "maillage Gmsh h = 0,25 m (test L14)";
        const auto prepared = s.analysis().prepare(ctx);
        TEST_CHECK(prepared.canRun() && !prepared.validation.hasWarnings(), "Test L14: modèle maillé valide, sans avertissement");
        const auto run = s.analysis().runBlocking(ctx, prepared);
        TEST_CHECK(run.success, "Test L14: calcul des coques");
        const auto ref = analyticalReference(s.model(), ctx, s.analysis().results().get());
        for (const auto& c : ref.checks)
            std::cout << "  plaque — " << c.label.toStdString() << " : " << c.computed << " / " << c.reference << " " << c.unit.toStdString() << " (écart relatif "
                      << c.relError * 100 << " %)" << std::endl;
        TEST_CHECK(ref.problemId == "square-plate-hard-support" && ref.applicable && ref.checks.size() == 2 && ref.passed() && ref.checks[0].relError < 1e-3,
                   "Test L14: flèche et Σ réactions conformes à la référence de Mindlin (écart < 0,1 % à h = 0,25 m)");

        // Hors domaine : appui mou (rotation tangentielle libre) et poids propre — raisons nommées
        auto soft = s.model().getSlab(s.model().slabs().begin()->first)->edgeSupports();
        for (auto& sp : soft) sp.fixed[3] = sp.fixed[4] = false;
        s.model().getSlab(s.model().slabs().begin()->first)->setEdgeSupports(soft);
        TEST_CHECK(analyticalReference(s.model(), ctx, nullptr).violations.join(" ").contains("rotation tangentielle libre"), "Test L14: appui mou — hors domaine");
        auto weighted = ctx;
        weighted.common.includeSelfWeight = true;
        TEST_CHECK(analyticalReference(s.model(), weighted, nullptr).violations.join(" ").contains("poids propre"), "Test L14: poids propre — hors domaine");
        std::cout << "[PASS] Test L14: plaque maillée dans TSALab (Gmsh → coques OpenSees), référence de Mindlin" << std::endl;
        ++passed;
    }
    return true;
}

} // namespace

// Programmation structurelle (StructuralLabTests.cpp) : tests L15 à L22.
bool runSuite_Structural(int& passed);
int structuralLabTestCount();

int main(int argc, char* argv[])
{
    QApplication app(argc, argv); // aperçu des projets (QPainter, polices) : application graphique requise
    std::cout << "=================================================" << std::endl;
    std::cout << "TSALab Unit Tests" << std::endl;
    std::cout << "=================================================" << std::endl;
    int passed = 0;
    // --structural : seulement la programmation structurelle (L15 à L22), sans les calculs OpenSees de L4 à L14
    const bool structuralOnly = argc > 1 && std::strcmp(argv[1], "--structural") == 0;
    const int expected = (structuralOnly ? 0 : kLabTests) + structuralLabTestCount();
    const bool ok = (structuralOnly || runSuite_Lab(passed)) && runSuite_Structural(passed);
    std::cout << "\nRESULTS: " << passed << " / " << expected << " tests passed" << (ok ? " successfully!" : " (FAILURE)") << std::endl;
    return ok && passed == expected ? 0 : 1;
}
