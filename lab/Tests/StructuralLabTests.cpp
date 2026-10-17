// Tests de TSALab Blueprint — programmation structurelle (tests L15 à L22) : bibliothèque de blocs pré-codés, graphe
// Methode_Des_Deplacements_2D comparé au calcul direct (mission, cas 9), sauvegarde et réouverture du graphe (cas 10),
// mécanisme (cas 7), solveur de l'étudiant (mode 2), classe FrameElement2D et charges en blocs (mode 1), tassement
// d'appui (cas 6), blocs mathématiques et exports ; L23 à L26 : rotules et appuis élastiques, dynamique pédagogique
// (modal, Newmark, accélérogramme fourni, spectral), charges roulantes, comparaison de solveurs, enregistrement des
// graphes ; L27 : import de projets TSA réels ; L28 : méthode des déplacements en sous-graphes ; L29 : graphe d'analyse
// d'un projet TSA importé ; L30 : persistance JSON du modèle et des résultats. La science (solutions analytiques) est testée par tsalab_science_tests
// (S7 à S22).
#include "Blueprint/BlueprintFile.h"
#include "Blueprint/BlueprintRuntime.h"
#include "IO/TSAFile.h"
#include "IO/TSAFileFormat.h"
#include "Model/Load/LoadCase.h"
#include "Model/Load/LoadEnums.h"
#include "Model/Load/LoadManager.h"
#include "Model/Load/NodalLoad.h"
#include "Model/Model.h"
#include "Model/SupportDefinition.h"
#include "Research/Examples/StructuralGraphs.h"
#include "Research/Structural/ResultBoard.h"
#include "Research/Structural/StructuralLibrary.h"
#include "Research/Structural/StructuralModels.h"
#include "Research/Structural/StructuralJson.h"
#include "Research/Structural/StructuralObjects.h"
#include "Research/Structural/TsaImport.h"
#include "tsalab/planar/PlanarSolvers.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <atomic>

#include <cmath>
#include <functional>
#include <limits>
#include <iostream>
#include <map>
#include <set>

#define TEST_CHECK(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "[FAIL] " << msg << " (" << #cond << ") at line " << __LINE__ << std::endl; \
            return false; \
        } \
    } while (0)

namespace
{
using namespace TSA::Blueprint;
namespace SG = TSALab::Research::StructuralGraphs;
namespace RS = TSALab::Research::Structural;
namespace S = tsalab::structural;

int nodeOfType(const Graph& g, const std::string& type, int nth = 0)
{
    for (const auto& [id, n] : g.nodes())
        if (n.type == type && nth-- == 0) return id;
    return 0;
}

template <typename O>
const O* outputObject(const ExecutionReport& rep, int node, const std::string& pin)
{
    const auto it = rep.values.find(node);
    if (it == rep.values.end() || !it->second.count(pin)) return nullptr;
    const auto* ref = std::get_if<ObjectRef>(&it->second.at(pin));
    return ref ? dynamic_cast<const O*>(ref->get()) : nullptr;
}

double outputReal(const ExecutionReport& rep, int node, const std::string& pin)
{
    const auto it = rep.values.find(node);
    if (it == rep.values.end() || !it->second.count(pin)) return std::nan("");
    const Value& v = it->second.at(pin);
    if (const auto* d = std::get_if<double>(&v)) return *d;
    if (const auto* i = std::get_if<long long>(&v)) return static_cast<double>(*i);
    return std::nan("");
}

bool logContains(const ExecutionReport& rep, const std::string& text)
{
    for (const auto& l : rep.log)
        if (l.find(text) != std::string::npos) return true;
    return false;
}

/// Construction contrôlée d'un graphe de test.
struct Builder
{
    Graph& g;
    const NodeLibrary& lib;
    std::string error;
    int node(const std::string& type, std::map<std::string, Value> values = {})
    {
        if (!lib.find(type) && error.empty()) error = "bloc inconnu " + type;
        const int id = g.addNode(type, 0, 0);
        for (const auto& [pin, v] : values) g.setValue(id, pin, v);
        return id;
    }
    void link(int a, const std::string& out, int b, const std::string& in)
    {
        std::string why;
        if (!lib.connect(g, { a, out, b, in }, &why) && error.empty()) error = "lien " + out + " → " + in + " : " + why;
    }
    void then(int a, int b, const std::string& out = "then") { link(a, out, b, "exec"); }
};

double maxDiff(const tsalab::numerics::Matrix& a, const tsalab::numerics::Matrix& b)
{
    if (a.rows != b.rows || a.cols != b.cols) return 1e300;
    double d = 0.0;
    for (std::size_t k = 0; k < a.data.size(); ++k) d = std::max(d, std::abs(a.data[k] - b.data[k]));
    return d;
}

const S::AnalysisResult2D* graphResult(const Graph& g, const ExecutionReport& rep)
{
    const auto* r = outputObject<RS::ResultObject>(rep, nodeOfType(g, "struct.result_assemble"), "result");
    return r ? &r->value() : nullptr;
}
} // namespace

int structuralLabTestCount() { return 16; }

bool runSuite_Structural(int& passed)
{
    NodeLibrary& global = NodeLibrary::global();
    RS::registerStructuralNodes(global);   // idempotent
    const NodeLibrary& lib = NodeLibrary::standard();

    // -------------------------------------------------------------------------
    // TEST L15 : bibliothèque — catégories A à E et expérimentale, statut, documentation et source réelles, typage
    // -------------------------------------------------------------------------
    {
        TEST_CHECK(!RS::registerStructuralNodes(global), "Test L15: enregistrement unique (second appel sans effet)");
        const auto ids = RS::structuralNodeIds(lib);
        std::map<std::string, int> perCategory;
        const QDir root = QDir(QStringLiteral(TSALAB_TSA_BENCHMARKS)).absoluteFilePath(QStringLiteral("../../.."));
        for (const auto& id : ids)
        {
            const auto* d = lib.find(id);
            perCategory[d->category] += 1;
            const bool experimental = d->category.rfind("Expérimental", 0) == 0;
            TEST_CHECK(experimental ? d->status == NodeStatus::Experimental : d->status != NodeStatus::Experimental,
                       "Test L15: composant expérimental séparé des composants validés (" << id << ")");
            TEST_CHECK(!d->documentation.empty() && !d->description.empty(), "Test L15: documentation du bloc " << id);
            TEST_CHECK(!d->source.empty() && QFileInfo::exists(root.filePath(QString::fromStdString(d->source))),
                       "Test L15: source du bloc " << id << " présente (" << d->source << ")");
            // Bloc qui lit un fichier de l'utilisateur : ses sorties ne dépendent pas que de ses entrées, il n'est jamais réutilisé
            const bool readsFile = id == "struct.accelerogram_file" || id == "struct.model_import_tsa" || id == "struct.model_load_json"
                                   || id == "struct.result_load_json";
            if (d->pure && d->status == NodeStatus::Validated)
                TEST_CHECK(d->cacheable != readsFile, "Test L15: bloc pur réutilisable, sauf lecture de fichier (" << id << ")");
        }
        TEST_CHECK(perCategory.size() == 8 && ids.size() >= 110, "Test L15: 8 catégories (A à G, expérimental), " << ids.size() << " blocs");
        for (const char* required : { "struct.frame_element", "struct.local_stiffness", "struct.transformation", "struct.global_element_stiffness",
                                      "struct.equivalent_loads", "struct.element_displacement", "struct.element_end_forces", "struct.internal_forces",
                                      "struct.dof_numbering", "struct.global_stiffness", "struct.global_loads", "struct.boundary_conditions",
                                      "struct.reduced_system", "struct.displacement_recovery", "struct.reaction_recovery", "struct.load_case",
                                      "struct.load_combination", "matrix.solve", "matrix.lu", "matrix.inverse", "struct.for_each_element" })
            TEST_CHECK(lib.find(required), "Test L15: bloc " << required);
        // Typage des objets
        Graph g;
        Builder b { g, lib };
        const int el = b.node("struct.frame_element"), kl = b.node("struct.local_stiffness"), fe = b.node("struct.equivalent_loads");
        const int vadd = b.node("vector.add"), node = b.node("struct.node");
        std::string why;
        TEST_CHECK(!lib.canConnect(g, { kl, "k", vadd, "x" }, &why) && why.find("Matrix → Vector") != std::string::npos,
                   "Test L15: Matrix → Vector refusé (" << why << ")");
        TEST_CHECK(!lib.canConnect(g, { el, "element", el, "node_i" }, &why), "Test L15: lien d'un bloc vers lui-même refusé");
        TEST_CHECK(!lib.canConnect(g, { el, "element", fe, "loads" }, &why) && why.find("FrameElement2D → LoadCase") != std::string::npos,
                   "Test L15: FrameElement2D → LoadCase refusé");
        TEST_CHECK(lib.canConnect(g, { node, "node", el, "node_i" }, &why), "Test L15: Node2D → Node2D admis");
        bool missing = false;
        for (const auto& i : lib.validate(g)) missing = missing || (i.node == el && i.message.find("Matériau") != std::string::npos);
        TEST_CHECK(missing, "Test L15: entrée objet obligatoire non reliée signalée");
        std::cout << "[PASS] Test L15: bibliothèque structurelle — " << ids.size() << " blocs en 8 catégories, documentés, sources présentes, typés"
                  << std::endl;
        ++passed;
    }

    // -------------------------------------------------------------------------
    // TEST L16 : Methode_Des_Deplacements_2D — exécution du graphe, comparaison au calcul direct (mission, cas 9)
    // -------------------------------------------------------------------------
    Graph method;
    {
        std::string error;
        TEST_CHECK(SG::build("methode-deplacements-2d", method, &error), "Test L16: graphe construit (" << error << ")");
        TEST_CHECK(lib.validate(method).empty(), "Test L16: graphe valide");
        TEST_CHECK(lib.warnings(method).empty(), "Test L16: aucune dépendance mal ordonnée");
        TEST_CHECK(!lib.usesProject(method), "Test L16: sans accès au projet (exécutable en tâche de fond)");
        TEST_CHECK(method.comments().size() == 7, "Test L16: 7 cadres d'étapes commentés");
        RS::ResultBoard::instance().take();
        Runner runner(lib, method, nullptr);
        const auto rep = runner.run();
        TEST_CHECK(rep.ok, "Test L16: exécution (" << rep.message << ")");
        TEST_CHECK(logContains(rep, "Graphe conforme au calcul direct : vrai"), "Test L16: le graphe se déclare conforme au calcul direct");
        const S::AnalysisResult2D* r = graphResult(method, rep);
        TEST_CHECK(r && r->success, "Test L16: résultat assemblé par le graphe");
        S::Model2D portal;
        TEST_CHECK(RS::demoPortal({}, portal), "Test L16: portique de démonstration");
        const auto direct = S::solveLinearStatic(portal, { 1, 0 });
        const auto cmp = S::compareResults(*r, direct);
        TEST_CHECK(cmp.comparable && cmp.maxDeviation() <= 1e-12, "Test L16: graphe ≡ calcul direct (écart " << cmp.maxDeviation() << ")");
        TEST_CHECK(r->equilibrium.balanced(1e-9), "Test L16: équilibre global du résultat du graphe");
        const auto* K = rep.variables.count("K") ? std::get_if<ObjectRef>(&rep.variables.at("K")) : nullptr;
        const auto* Km = K ? dynamic_cast<const RS::MatrixObject*>(K->get()) : nullptr;
        TEST_CHECK(Km && Km->value().matrix.rows == 12 && tsalab::numerics::LinAlg::asymmetry(Km->value().matrix) <= 1e-12
                       && maxDiff(Km->value().matrix, direct.K) <= 1e-9 * tsalab::numerics::LinAlg::maxAbs(direct.K),
                   "Test L16: K = Σ A_eᵀ·k_e·A_e (boucle) identique à l'assemblage direct, 12×12, symétrique");
        bool shown = false;
        for (const auto& item : RS::ResultBoard::instance().take()) shown = shown || dynamic_cast<const RS::ResultObject*>(item.object.get());
        TEST_CHECK(shown, "Test L16: résultat déposé dans le tableau des résultats (bloc « Visualiser »)");
        // Combinaison 1,35 G + 1,5 W et tassement (déplacement imposé) : mêmes contrôles par surcharge des paramètres
        for (const auto& [name, value, caseId, combo] : std::vector<std::tuple<std::string, long long, int, int>> { { "combinaison", 1, 0, 1 }, { "cas", 3, 3, 0 } })
        {
            Runner other(lib, method, nullptr);
            const auto rep2 = other.run({ { name, Value(value) } });
            const S::AnalysisResult2D* r2 = graphResult(method, rep2);
            TEST_CHECK(rep2.ok && r2 && logContains(rep2, "Graphe conforme au calcul direct : vrai"), "Test L16: " << name << " = " << value << " (" << rep2.message << ")");
            const auto d2 = S::solveLinearStatic(portal, { caseId, combo });
            TEST_CHECK(S::compareResults(*r2, d2).maxDeviation() <= 1e-12 && r2->equilibrium.balanced(1e-9), "Test L16: " << name << " — graphe ≡ calcul direct");
            if (caseId == 3)
                TEST_CHECK(std::abs(r2->nodeDisplacement(4)[1] + 0.01) <= 1e-15 && r2->loads.imposedDisplacements.size() == 1,
                           "Test L16: tassement imposé restitué (uy N4 = −10 mm)");
        }
        std::cout << "[PASS] Test L16: Methode_Des_Deplacements_2D — K assemblée dans une boucle, résultat ≡ calcul direct (cas 1, ELU, tassement)" << std::endl;
        ++passed;
    }

    // -------------------------------------------------------------------------
    // TEST L17 : sauvegarde et réouverture du graphe (cas 10), réexécution, réutilisation des résultats
    // -------------------------------------------------------------------------
    {
        QTemporaryDir dir;
        TEST_CHECK(dir.isValid(), "Test L17: dossier temporaire");
        const QString path = dir.filePath(QStringLiteral("Methode_Des_Deplacements_2D.tsbp"));
        QString error;
        TEST_CHECK(saveFile(method, path, &error), "Test L17: enregistrement (" << error.toStdString() << ")");
        Graph back;
        TEST_CHECK(loadFile(path, back, &error), "Test L17: réouverture (" << error.toStdString() << ")");
        bool same = back.name == method.name && back.description == method.description && back.nodes().size() == method.nodes().size()
                    && back.links().size() == method.links().size() && back.comments() == method.comments();
        for (const auto& [id, n] : method.nodes())
        {
            const auto* m = back.node(id);
            same = same && m && m->type == n.type && m->x == n.x && m->y == n.y && m->values == n.values && m->version == lib.find(n.type)->version;
        }
        for (const auto& l : method.links()) same = same && std::find(back.links().begin(), back.links().end(), l) != back.links().end();
        TEST_CHECK(same, "Test L17: nœuds, versions, valeurs, liens et commentaires relus à l'identique");
        Graph again;
        TEST_CHECK(fromJson(toJson(back), again) && again.sameContent(back), "Test L17: second aller-retour identique");
        // Réexécution du graphe rouvert, avec réutilisation des résultats d'une exécution à l'autre
        EvaluationCache cache;
        Runner first(lib, back, nullptr);
        first.setCache(&cache);
        const auto rep1 = first.run();
        Runner reference(lib, method, nullptr);
        const auto repRef = reference.run();
        TEST_CHECK(rep1.ok && repRef.ok && S::compareResults(*graphResult(back, rep1), *graphResult(method, repRef)).maxDeviation() == 0.0,
                   "Test L17: graphe rouvert exécuté, résultat identique à l'original");
        Runner second(lib, back, nullptr);
        second.setCache(&cache);
        const auto rep2 = second.run({ { "amplification", Value(25.0) } });
        const int res = nodeOfType(back, "struct.result_assemble"), sol = nodeOfType(back, "struct.solve_displacements");
        TEST_CHECK(rep2.ok && rep2.reused > 0 && rep2.profile.count(res) && rep2.profile.at(res).executions == 0 && rep2.profile.at(res).reused >= 1
                       && rep2.profile.at(sol).executions == 0,
                   "Test L17: paramètre d'affichage modifié — calculs réutilisés sans réévaluation (" << rep2.reused << " réutilisations)");
        TEST_CHECK(S::compareResults(*graphResult(back, rep2), *graphResult(back, rep1)).maxDeviation() == 0.0, "Test L17: résultat réutilisé identique");
        // Linéarité : cas W seul, H doublé → déplacements doublés (la chaîne dépendant de H est recalculée)
        Runner w1(lib, back, nullptr), w2(lib, back, nullptr);
        w1.setCache(&cache);
        w2.setCache(&cache);
        const auto a = w1.run({ { "cas", Value(2LL) } });
        const auto c = w2.run({ { "cas", Value(2LL) }, { "H", Value(40.0) } });
        const auto* ra = graphResult(back, a);
        const auto* rc = graphResult(back, c);
        TEST_CHECK(a.ok && c.ok && ra && rc && std::abs(rc->nodeDisplacement(2)[0] - 2.0 * ra->nodeDisplacement(2)[0]) <= 1e-12 * std::abs(ra->nodeDisplacement(2)[0])
                       && c.profile.at(sol).executions == 1,
                   "Test L17: H doublé → déplacement en tête doublé, résolution recalculée");
        std::cout << "[PASS] Test L17: graphe enregistré (.tsbp v2) et rouvert à l'identique, réexécuté ; résultats réutilisés quand rien ne change" << std::endl;
        ++passed;
    }

    // -------------------------------------------------------------------------
    // TEST L18 : système instable — diagnostic du mécanisme dans le graphe (cas 7)
    // -------------------------------------------------------------------------
    {
        Graph g;
        std::string error;
        TEST_CHECK(SG::build("mecanisme", g, &error), "Test L18: graphe construit (" << error << ")");
        const auto rep = Runner(lib, g, nullptr).run();
        TEST_CHECK(!rep.ok && rep.failedNode == nodeOfType(g, "struct.solve_displacements"), "Test L18: le bloc de résolution s'arrête (" << rep.message << ")");
        TEST_CHECK(rep.message.find("translation de toute la structure suivant X") != std::string::npos && rep.message.find("appuis insuffisants") != std::string::npos
                       && rep.message.find("DDL") != std::string::npos,
                   "Test L18: diagnostic — DDL, translation suivant X, appuis insuffisants");
        TEST_CHECK(logContains(rep, "Calcul direct : Système singulier"), "Test L18: le calcul direct rend le même diagnostic sans s'arrêter");
        std::cout << "[PASS] Test L18: mécanisme détecté dans le graphe (DDL, mode de corps rigide, causes)" << std::endl;
        ++passed;
    }

    // -------------------------------------------------------------------------
    // TEST L19 : mode 2 — solveur de l'étudiant (gradient conjugué en blocs) comparé à la référence ; exercice Gauss-Seidel
    // -------------------------------------------------------------------------
    {
        Graph g;
        std::string error;
        TEST_CHECK(SG::build("solveur-etudiant", g, &error), "Test L19: graphe construit (" << error << ")");
        TEST_CHECK(lib.validate(g).empty() && lib.warnings(g).empty(), "Test L19: graphe valide, dépendances ordonnées");
        const auto rep = Runner(lib, g, nullptr).run();
        TEST_CHECK(rep.ok && logContains(rep, "Solveur de l'étudiant conforme : vrai"), "Test L19: gradient conjugué conforme à Cholesky (" << rep.message << ")");
        const Value itValue = rep.variables.count("it") ? rep.variables.at("it") : Value {};
        const double it = std::holds_alternative<double>(itValue) ? std::get<double>(itValue) : -1.0;
        TEST_CHECK(it >= 1.0 && it <= 20.0, "Test L19: convergence en " << it << " itérations (6 DDL libres)");
        // Exercice : itération de Gauss-Seidel (composant expérimental). Non écrite : refus explicite ; écrite : convergence vérifiée.
        Graph gs;
        Builder b { gs, lib };
        const int K = b.node("matrix.from_text", { { "text", std::string("4 1 0 ; 1 3 1 ; 0 1 2") } });
        const int f = b.node("vector.from_text", { { "text", std::string("1 2 3") } });
        const int zeros = b.node("vector.zeros", { { "n", 3LL } });
        const int start = b.node("event.start"), set0 = b.node("var.set", { { "name", std::string("x") } });
        const int loop = b.node("flow.for", { { "first", 0LL }, { "last", 59LL } });
        const int getX = b.node("var.get", { { "name", std::string("x") } }), sweep = b.node("exp.gauss_seidel_sweep");
        const int set = b.node("var.set", { { "name", std::string("x") } });
        const int ref = b.node("matrix.solve", { { "method", std::string("lu") } }), getX2 = b.node("var.get", { { "name", std::string("x") } });
        const int cmp = b.node("vector.compare", { { "tolerance", 1e-10 } }), print = b.node("debug.print");
        b.link(zeros, "v", set0, "value");
        b.then(start, set0);
        b.then(set0, loop);
        b.link(K, "M", sweep, "K");
        b.link(f, "v", sweep, "f");
        b.link(getX, "value", sweep, "x");
        b.link(sweep, "x_next", set, "value");
        b.link(loop, "body", set, "exec");
        b.link(K, "M", ref, "K");
        b.link(f, "v", ref, "f");
        b.link(getX2, "value", cmp, "x");
        b.link(ref, "x", cmp, "reference");
        b.link(cmp, "passed", print, "value");
        b.link(loop, "completed", print, "exec");
        TEST_CHECK(b.error.empty(), "Test L19: graphe Gauss-Seidel (" << b.error << ")");
        TEST_CHECK(lib.find("exp.gauss_seidel_sweep")->status == NodeStatus::Experimental, "Test L19: Gauss-Seidel est un composant expérimental");
        const auto gsRep = Runner(lib, gs, nullptr).run();
        TEST_CHECK(gsRep.ok && !gsRep.log.empty() && gsRep.log.back() == "vrai",
                   "Test L19: Gauss-Seidel — 60 itérations convergent vers la solution LU à 1e-10 (" << gsRep.message << ")");
        // Une itération à la main : x1 = (f1 − 0)/4, x2 = (f2 − 1·x1)/3, x3 = (f3 − 1·x2)/2 (x0 = 0)
        {
            Graph one;
            Builder ob { one, lib };
            const int k1 = ob.node("matrix.from_text", { { "text", std::string("4 1 0 ; 1 3 1 ; 0 1 2") } });
            const int f1 = ob.node("vector.from_text", { { "text", std::string("1 2 3") } });
            const int z1 = ob.node("vector.zeros", { { "n", 3LL } });
            const int sw = ob.node("exp.gauss_seidel_sweep"), st = ob.node("event.start"), pr = ob.node("debug.print");
            ob.link(k1, "M", sw, "K");
            ob.link(f1, "v", sw, "f");
            ob.link(z1, "v", sw, "x");
            ob.link(sw, "residual", pr, "value");
            ob.then(st, pr);
            const auto r1 = Runner(lib, one, nullptr).run();
            const auto* x1 = outputObject<RS::VectorObject>(r1, sw, "x_next");
            const double e1 = 0.25, e2 = (2.0 - e1) / 3.0, e3 = (3.0 - e2) / 2.0;
            TEST_CHECK(r1.ok && x1 && x1->value().vector.size() == 3 && std::abs(x1->value().vector[0] - e1) <= 1e-15
                           && std::abs(x1->value().vector[1] - e2) <= 1e-15 && std::abs(x1->value().vector[2] - e3) <= 1e-15,
                       "Test L19: une itération de Gauss-Seidel utilise les composantes déjà mises à jour");
        }
        // Exemple : Gauss-Seidel sur le système réduit du portique, comparé à Cholesky
        Graph gse;
        TEST_CHECK(SG::build("solveur-etudiant-gs", gse, &error), "Test L19: exemple Gauss-Seidel construit (" << error << ")");
        const auto gseRep = Runner(lib, gse, nullptr).run();
        const Value gsIt = gseRep.variables.count("it") ? gseRep.variables.at("it") : Value {};
        const double gsIterations = std::holds_alternative<double>(gsIt) ? std::get<double>(gsIt) : -1.0;
        TEST_CHECK(gseRep.ok && logContains(gseRep, "Solveur de l'étudiant (Gauss-Seidel) conforme : vrai") && gsIterations > 0.0 && gsIterations < 20000.0,
                   "Test L19: Gauss-Seidel sur le portique conforme à Cholesky en " << gsIterations << " itérations (" << gseRep.message << ")");
        std::cout << "[PASS] Test L19: mode 2 — gradient conjugué en blocs (" << it << " itérations) et Gauss-Seidel (" << gsIterations
                  << " itérations) ≡ Cholesky" << std::endl;
        ++passed;
    }

    // -------------------------------------------------------------------------
    // TEST L20 : mode 1 — classe FrameElement2D, modèle et charges construits par blocs ≡ modèle C++, calcul direct
    // -------------------------------------------------------------------------
    {
        Graph g;
        std::string error;
        TEST_CHECK(SG::build("classe-frame-element", g, &error), "Test L20: exemple construit (" << error << ")");
        const auto rep = Runner(lib, g, nullptr).run();
        TEST_CHECK(rep.ok, "Test L20: exemple exécuté (" << rep.message << ")");
        S::Section2D ipe;
        S::Material s235;
        TEST_CHECK(RS::catalogSection("IPE 300", ipe) && RS::catalogMaterial("S235", s235), "Test L20: catalogue");
        const S::FrameElement2D ref(1, { 1, 0.0, 0.0, {} }, { 2, 4.0, 3.0, {} }, s235, ipe);
        const int el = nodeOfType(g, "struct.frame_element");
        const auto* kg = outputObject<RS::MatrixObject>(rep, el, "k_global");
        TEST_CHECK(kg && maxDiff(kg->value().matrix, ref.globalStiffness()) == 0.0, "Test L20: k global du bloc = FrameElement2D::globalStiffness");
        TEST_CHECK(std::abs(outputReal(rep, el, "angle") - std::atan2(3.0, 4.0) * 180.0 / std::acos(-1.0)) <= 1e-12 && outputReal(rep, el, "length") == 5.0,
                   "Test L20: longueur 5 m et angle atan2(3, 4)");
        const auto* feq = outputObject<RS::VectorObject>(rep, nodeOfType(g, "struct.equivalent_loads"), "f_global");
        TEST_CHECK(feq && std::abs(feq->value().vector[1] + feq->value().vector[4] + 50.0) <= 1e-12, "Test L20: charges équivalentes verticales Σ = −10 × 5 kN");

        // Modèle construit par blocs, toutes les charges, comparé au même modèle écrit en C++
        Graph m;
        Builder b { m, lib };
        const int mat = b.node("struct.material_catalog"), sec = b.node("struct.section_catalog"), col = b.node("struct.section_catalog", { { "profile", std::string("HEB 200") } });
        const int n1 = b.node("struct.node", { { "id", 1LL } }), n2 = b.node("struct.node", { { "id", 2LL }, { "x", 6.0 } });
        const int n3 = b.node("struct.node", { { "id", 3LL }, { "x", 6.0 }, { "y", -4.0 } });
        const int e1 = b.node("struct.frame_element", { { "id", 1LL } }), e2 = b.node("struct.frame_element", { { "id", 2LL } });
        b.link(n1, "node", e1, "node_i");
        b.link(n2, "node", e1, "node_j");
        b.link(mat, "material", e1, "material");
        b.link(sec, "section", e1, "section");
        b.link(n3, "node", e2, "node_i");
        b.link(n2, "node", e2, "node_j");
        b.link(mat, "material", e2, "material");
        b.link(col, "section", e2, "section");
        const int s1 = b.node("struct.support", { { "node", 1LL }, { "ux", true }, { "uy", true }, { "rz", false } });
        const int s3 = b.node("struct.support", { { "node", 3LL }, { "ux", true }, { "uy", true }, { "rz", true } });
        const std::vector<int> loads = {
            b.node("struct.load_nodal", { { "node", 2LL }, { "fx", 5.0 }, { "fy", -10.0 }, { "mz", 2.0 } }),
            b.node("struct.load_distributed", { { "element", 1LL }, { "q1", -4.0 }, { "q2", -8.0 }, { "a", 0.5 }, { "b", 5.5 } }),
            b.node("struct.load_distributed", { { "element", 1LL }, { "direction", std::string("Y global") }, { "q1", -1.5 }, { "q2", -1.5 }, { "projected", true } }),
            b.node("struct.load_point", { { "element", 1LL }, { "P", 3.0 }, { "a", 0.25 }, { "direction", std::string("X global") } }),
            b.node("struct.load_moment", { { "element", 1LL }, { "M", 4.0 }, { "a", 0.5 } }),
            b.node("struct.load_piecewise", { { "element", 2LL }, { "points", std::string("0:1 ; 2:3 ; 4:0") }, { "direction", std::string("X global") } }),
            b.node("struct.load_thermal", { { "element", 1LL }, { "deltaT", 15.0 }, { "gradient", 10.0 } }),
            b.node("struct.load_initial_strain", { { "element", 2LL }, { "eps0", 1e-4 }, { "kappa0", 0.0 } }),
            b.node("struct.load_self_weight"),
            b.node("struct.imposed_displacement", { { "node", 3LL }, { "component", std::string("uy") }, { "value", -0.002 } }),
        };
        int merged = loads[0];
        for (std::size_t k = 1; k < loads.size(); ++k)
        {
            const int mg = b.node("struct.load_merge");
            b.link(merged, "load", mg, "a");
            b.link(loads[k], "load", mg, "b");
            merged = mg;
        }
        const int lc = b.node("struct.load_case", { { "id", 1LL }, { "name", std::string("toutes les charges") } });
        b.link(merged, "load", lc, "loads");
        const int m0 = b.node("struct.model_empty", { { "name", std::string("Modèle par blocs") } });
        const int ma = b.node("struct.model_add_element"), mb = b.node("struct.model_add_element"), mc = b.node("struct.model_add_support");
        const int md = b.node("struct.model_add_support"), me = b.node("struct.model_add_case");
        b.link(m0, "model", ma, "model");
        b.link(e1, "element", ma, "element");
        b.link(ma, "model", mb, "model");
        b.link(e2, "element", mb, "element");
        b.link(mb, "model", mc, "model");
        b.link(s1, "support", mc, "support");
        b.link(mc, "model", md, "model");
        b.link(s3, "support", md, "support");
        b.link(md, "model", me, "model");
        b.link(lc, "case", me, "case");
        const int valid = b.node("struct.model_validate"), solve = b.node("struct.solve_direct"), info = b.node("struct.model_info");
        b.link(me, "model", valid, "model");
        b.link(me, "model", solve, "model");
        b.link(me, "model", info, "model");
        const int selV = b.node("struct.select_elements", { { "filter", std::string("verticaux") } });
        const int selL = b.node("struct.select_elements", { { "filter", std::string("longueur>5") } });
        b.link(me, "model", selV, "model");
        b.link(me, "model", selL, "model");
        const int start = b.node("event.start"), print = b.node("debug.print");
        b.link(valid, "report", print, "value");
        b.then(start, print);
        // Les blocs purs ne sont évalués que lus par un bloc d'action : un affichage par valeur contrôlée
        int last = print;
        for (const auto& [node, pin] : std::vector<std::pair<int, std::string>> { { info, "dofs" }, { solve, "success" }, { selV, "ids" }, { selL, "ids" } })
        {
            const int p = b.node("debug.print");
            b.link(node, pin, p, "value");
            b.then(last, p);
            last = p;
        }
        TEST_CHECK(b.error.empty(), "Test L20: graphe du modèle par blocs (" << b.error << ")");
        const auto rm = Runner(lib, m, nullptr).run();
        TEST_CHECK(rm.ok, "Test L20: modèle par blocs exécuté (" << rm.message << ")");
        // Le même modèle en C++
        S::Section2D heb;
        RS::catalogSection("HEB 200", heb);
        S::Model2D cpp;
        cpp.name = "Modèle par blocs";
        cpp.nodes = { { 1, 0.0, 0.0, {} }, { 2, 6.0, 0.0, {} }, { 3, 6.0, -4.0, {} } };
        cpp.elements = { { 1, 1, 2, s235, ipe, {} }, { 2, 3, 2, s235, heb, {} } };
        cpp.supports = { { 1, true, true, false }, { 3, true, true, true } };
        S::LoadCase all { 1, "toutes les charges", { { 2, 5.0, -10.0, 2.0 } }, {}, { { 3, 1, -0.002 } } };
        auto projected = S::uniformLoad(1, -1.5, S::LoadDirection::GlobalY);
        projected.projected = true;
        const auto pieces = S::piecewiseLinearLoad(2, { { 0.0, 1.0 }, { 2.0, 3.0 }, { 4.0, 0.0 } }, S::LoadDirection::GlobalX);
        all.elementLoads = { S::linearLoad(1, -4.0, -8.0, 0.5, 5.5, S::LoadDirection::LocalY), projected, S::pointForce(1, 3.0, 0.25, S::LoadDirection::GlobalX, true),
                             S::pointMoment(1, 4.0, 0.5, true) };
        all.elementLoads.insert(all.elementLoads.end(), pieces.begin(), pieces.end());
        all.elementLoads.push_back(S::thermalLoad(1, 15.0, 10.0));
        all.elementLoads.push_back(S::initialStrain(2, 1e-4, 0.0));
        all.elementLoads.push_back(S::selfWeight());
        cpp.loadCases = { all };
        const auto* built = outputObject<RS::ModelObject>(rm, me, "model");
        TEST_CHECK(built && built->value() == cpp, "Test L20: modèle et charges construits par blocs ≡ modèle C++ (directions, positions, projection, déplacement imposé)");
        TEST_CHECK(rm.values.at(valid).at("valid") == Value(true) && outputReal(rm, info, "dofs") == 9.0,
                   "Test L20: modèle valide, 9 DDL (" << (rm.log.empty() ? std::string() : rm.log.front()) << ", " << outputReal(rm, info, "dofs") << " DDL)");
        const auto* res = outputObject<RS::ResultObject>(rm, solve, "result");
        const auto direct = S::solveLinearStatic(cpp, { 1, 0 });
        TEST_CHECK(res && res->value().success && direct.success && S::compareResults(res->value(), direct).maxDeviation() == 0.0 && direct.equilibrium.balanced(1e-9),
                   "Test L20: calcul direct du modèle par blocs ≡ calcul C++, équilibre vérifié");
        TEST_CHECK(rm.values.at(selV).at("ids") == Value(std::vector<int> { 2 }) && rm.values.at(selL).at("ids") == Value(std::vector<int> { 1 }),
                   "Test L20: sélection d'éléments (verticaux, longueur > 5 m)");
        std::cout << "[PASS] Test L20: mode 1 — classe FrameElement2D en bloc, modèle et 10 types de charges par blocs ≡ C++, calcul direct" << std::endl;
        ++passed;
    }

    // -------------------------------------------------------------------------
    // TEST L21 : tassement d'appui dans un graphe (cas 6) — réaction comparée à −6EIΔ/L³ calculée par blocs
    // -------------------------------------------------------------------------
    {
        Graph g;
        std::string error;
        TEST_CHECK(SG::build("tassement", g, &error), "Test L21: graphe construit (" << error << ")");
        RS::ResultBoard::instance().take();
        const auto rep = Runner(lib, g, nullptr).run();
        TEST_CHECK(rep.ok && logContains(rep, "Conforme à −6EIΔ/L³ : vrai"), "Test L21: réaction centrale = −6EIΔ/L³ (" << rep.message << ")");
        TEST_CHECK(!RS::ResultBoard::instance().take().empty(), "Test L21: résultat visualisé");
        std::cout << "[PASS] Test L21: tassement d'appui — K_ff·d_f = F_f − K_fc·d_c, réaction conforme à la référence analytique" << std::endl;
        ++passed;
    }

    // -------------------------------------------------------------------------
    // TEST L22 : blocs mathématiques (dimensions, LU, inversion contrôlée, résolution, résidu, extraction), exports, courbes
    // -------------------------------------------------------------------------
    {
        QTemporaryDir dir;
        Graph g;
        Builder b { g, lib };
        const int A = b.node("matrix.from_text", { { "text", std::string("4 1 2 ; 1 5 3 ; 2 3 6") }, { "title", std::string("A") } });
        const int v = b.node("vector.from_text", { { "text", std::string("1 ; 2 ; 3") } });
        const int inv = b.node("matrix.inverse"), prod = b.node("matrix.multiply"), lu = b.node("matrix.lu");
        const int pa = b.node("matrix.multiply"), luProd = b.node("matrix.multiply"), sol = b.node("matrix.solve", { { "method", std::string("lu") } });
        const int res = b.node("matrix.residual"), sub = b.node("matrix.submatrix", { { "rows", std::vector<int> { 0, 2 } }, { "cols", std::vector<int> { 1, 2 } } });
        const int sym = b.node("matrix.symmetry"), cond = b.node("matrix.condition");
        b.link(A, "M", inv, "A");
        b.link(A, "M", prod, "A");
        b.link(inv, "Ainv", prod, "B");
        b.link(A, "M", lu, "A");
        b.link(lu, "P", pa, "A");
        b.link(A, "M", pa, "B");
        b.link(lu, "L", luProd, "A");
        b.link(lu, "U", luProd, "B");
        b.link(A, "M", sol, "K");
        b.link(v, "v", sol, "f");
        b.link(A, "M", res, "K");
        b.link(sol, "x", res, "x");
        b.link(v, "v", res, "f");
        b.link(A, "M", sub, "A");
        b.link(A, "M", sym, "A");
        b.link(A, "M", cond, "A");
        const QString csv = dir.filePath(QStringLiteral("A.csv"));
        const int exportA = b.node("matrix.export_csv", { { "path", csv.toStdString() } });
        b.link(A, "M", exportA, "A");
        // Résultat d'un modèle : export CSV et courbe du moment
        const int model = b.node("struct.model_example", { { "name", std::string("poutre continue") } });
        const int direct = b.node("struct.solve_direct");
        b.link(model, "model", direct, "model");
        const int elementRes = b.node("struct.result_element", { { "element", 1LL } });
        b.link(direct, "result", elementRes, "result");
        const int curve = b.node("struct.diagram_curve", { { "quantity", std::string("M") } });
        b.link(elementRes, "diagram", curve, "diagram");
        const QString prefix = dir.filePath(QStringLiteral("poutre"));
        const int exportR = b.node("struct.export_results", { { "prefix", prefix.toStdString() } });
        b.link(direct, "result", exportR, "result");
        const int start = b.node("event.start"), show = b.node("debug.print_details", { { "title", std::string("Courbe") } });
        b.link(curve, "curve", show, "value");
        b.then(start, exportA);
        b.then(exportA, exportR);
        b.then(exportR, show);
        // Forcer l'évaluation des blocs purs contrôlés (lecture par un affichage)
        int last = show;
        for (const auto& [node, pin] : std::vector<std::pair<int, std::string>> { { prod, "C" }, { pa, "C" }, { luProd, "C" }, { res, "residual" }, { sub, "B" },
                                                                                 { sym, "symmetric" }, { cond, "condition" } })
        {
            const int p = b.node("debug.print");
            b.link(node, pin, p, "value");
            b.then(last, p);
            last = p;
        }
        TEST_CHECK(b.error.empty(), "Test L22: graphe des blocs mathématiques (" << b.error << ")");
        const auto rep = Runner(lib, g, nullptr).run();
        TEST_CHECK(rep.ok, "Test L22: exécution (" << rep.message << ")");
        const auto* I = outputObject<RS::MatrixObject>(rep, prod, "C");
        TEST_CHECK(I && maxDiff(I->value().matrix, tsalab::numerics::LinAlg::identity(3)) <= 1e-14, "Test L22: A·A⁻¹ = I (inversion contrôlée)");
        const auto* PA = outputObject<RS::MatrixObject>(rep, pa, "C");
        const auto* LU = outputObject<RS::MatrixObject>(rep, luProd, "C");
        TEST_CHECK(PA && LU && maxDiff(PA->value().matrix, LU->value().matrix) <= 1e-14, "Test L22: P·A = L·U");
        TEST_CHECK(outputReal(rep, res, "residual") <= 1e-15, "Test L22: résidu de la résolution ‖A·x − f‖/‖f‖ ≤ 1e-15");
        const auto* B = outputObject<RS::MatrixObject>(rep, sub, "B");
        TEST_CHECK(B && B->value().matrix.rows == 2 && B->value().matrix(0, 0) == 1.0 && B->value().matrix(1, 1) == 6.0, "Test L22: sous-matrice A[{0,2}, {1,2}]");
        TEST_CHECK(rep.values.at(sym).at("symmetric") == Value(true) && outputReal(rep, cond, "condition") > 1.0, "Test L22: symétrie et conditionnement");
        QFile f(csv);
        TEST_CHECK(f.open(QIODevice::ReadOnly) && f.readLine().trimmed() == "DDL;0;1;2", "Test L22: export CSV de la matrice (en-tête)");
        for (const char* suffix : { "_deplacements.csv", "_reactions.csv", "_elements.csv" })
            TEST_CHECK(QFileInfo::exists(prefix + QString::fromLatin1(suffix)), "Test L22: export des résultats " << suffix);
        const auto* cv = outputObject<RS::CurveObject>(rep, curve, "curve");
        TEST_CHECK(cv && cv->value().x.size() >= 21 && cv->value().x.size() == cv->value().y.size(), "Test L22: courbe M(x) de l'élément 1");
        // Erreurs de dimension et matrice singulière : refus motivés
        auto failing = [&](const std::string& text1, const std::string& text2, const std::string& op, const std::string& expected) -> std::string {
            Graph e;
            Builder eb { e, lib };
            const int a = eb.node("matrix.from_text", { { "text", text1 } }), c = eb.node("matrix.from_text", { { "text", text2 } });
            const int o = eb.node(op);
            eb.link(a, "M", o, "A");
            if (op == "matrix.multiply") eb.link(c, "M", o, "B");
            const int st = eb.node("event.start"), p = eb.node("debug.print");
            eb.link(o, op == "matrix.multiply" ? "C" : "Ainv", p, "value");
            eb.then(st, p);
            const auto r = Runner(lib, e, nullptr).run();
            return !r.ok && r.message.find(expected) != std::string::npos ? std::string() : r.message;
        };
        std::string why = failing("1 2 3 ; 4 5 6", "1 2 ; 3 4", "matrix.multiply", "dimensions incompatibles pour A·B : A est 2×3, B est 2×2");
        TEST_CHECK(why.empty(), "Test L22: produit de dimensions incompatibles refusé (" << why << ")");
        why = failing("1 2 ; 2 4", "1", "matrix.inverse", "matrice singulière");
        TEST_CHECK(why.empty(), "Test L22: inversion d'une matrice singulière refusée (" << why << ")");
        std::cout << "[PASS] Test L22: blocs mathématiques (A·A⁻¹ = I, P·A = L·U, résidu, sous-matrice, dimensions, singularité), exports CSV, courbe" << std::endl;
        ++passed;
    }

    // -------------------------------------------------------------------------
    // TEST L23 : exemples des nouveaux modules (rotule et ressort, modal, Newmark, spectral, charges roulantes) exécutés
    // et conformes aux références analytiques calculées dans les graphes
    // -------------------------------------------------------------------------
    {
        const std::map<std::string, std::vector<std::string>> expected = {
            { "rotules-ressorts",
              { "Réaction R1 conforme (70 kN) : vrai", "Moment d'encastrement conforme (200 kN·m) : vrai", "Flèche de l'appui élastique conforme (−6 mm) : vrai" } },
            { "analyse-modale", { "ω² conforme à 3EI/(mH³) : vrai", "Masse effective conforme (10 t) : vrai" } },
            { "newmark-echelon", { "Pointe conforme à 2P/k : vrai" } },
            { "analyse-spectrale", { "Effort tranchant conforme à m·Sa(T1) : vrai" } },
            { "charges-roulantes", { "Moment maximal conforme au théorème de Barré (405 kN·m) : vrai", "Ligne d'influence conforme (L/4) : vrai" } },
        };
        for (const auto& [id, lines] : expected)
        {
            Graph g;
            std::string error;
            TEST_CHECK(SG::build(id, g, &error), "Test L23: exemple " << id << " construit (" << error << ")");
            TEST_CHECK(lib.validate(g).empty() && lib.warnings(g).empty(), "Test L23: exemple " << id << " valide");
            const auto rep = Runner(lib, g, nullptr).run();
            TEST_CHECK(rep.ok, "Test L23: exemple " << id << " exécuté (" << rep.message << ")");
            for (const auto& l : lines) TEST_CHECK(logContains(rep, l), "Test L23: " << id << " — « " << l << " »");
            if (id == "rotules-ressorts")
            {   // rotule et ressort transmis par les blocs jusqu'au modèle calculé
                const auto* r = outputObject<RS::ResultObject>(rep, nodeOfType(g, "struct.solve_direct"), "result");
                TEST_CHECK(r && r->value().model.elements.size() == 2 && r->value().model.elements[0].releaseJ && !r->value().model.elements[0].releaseI,
                           "Test L23: rotule de l'élément 1 portée par le modèle");
                const S::SupportCondition* sp = r->value().model.support(3);
                TEST_CHECK(sp && sp->kUy == 5000.0 && !sp->fixUy, "Test L23: appui élastique du nœud 3 porté par le modèle");
                TEST_CHECK(std::abs(r->value().nodeSpringForce(3)[1] - 30.0) <= 1e-9 * 30.0, "Test L23: force du ressort qL/2 = 30 kN");
                TEST_CHECK(r->value().equilibrium.balanced(1e-9), "Test L23: équilibre global avec la force du ressort");
            }
            if (id == "charges-roulantes")
                TEST_CHECK(logContains(rep, "aucune interaction véhicule-pont"), "Test L23: le journal précise la nature statique du calcul");
        }
        std::cout << "[PASS] Test L23: exemples — rotule et appui élastique, analyse modale, Newmark, spectral, charges roulantes conformes" << std::endl;
        ++passed;
    }

    // -------------------------------------------------------------------------
    // TEST L24 : méthode pas à pas avec appui élastique — K = Σ A_eᵀ·k_e·A_e + K_ressorts, résolution, équilibre avec d
    // -------------------------------------------------------------------------
    {
        Graph g;
        std::string error;
        TEST_CHECK(SG::build("rotules-ressorts", g, &error), "Test L24: modèle par blocs (" << error << ")");
        const int model = nodeOfType(g, "struct.model_add_case");
        Builder b { g, lib };
        const int loads = b.node("struct.loading"), dofs = b.node("struct.dof_numbering"), K = b.node("struct.global_stiffness");
        const int Ks = b.node("struct.spring_stiffness"), F = b.node("struct.global_loads"), bc = b.node("struct.boundary_conditions");
        const int rs = b.node("struct.reduced_system"), sol = b.node("struct.solve_displacements"), disp = b.node("struct.displacement_recovery");
        const int reac = b.node("struct.reaction_recovery"), eq = b.node("struct.equilibrium"), eqNoD = b.node("struct.equilibrium");
        b.link(model, "model", loads, "model");
        b.link(model, "model", dofs, "model");
        for (int n : { K, Ks })
        {
            b.link(model, "model", n, "model");
            b.link(dofs, "dofs", n, "dofs");
        }
        for (int n : { F, bc })
        {
            b.link(model, "model", n, "model");
            b.link(dofs, "dofs", n, "dofs");
            b.link(loads, "loads", n, "loads");
        }
        b.link(K, "K", rs, "K");
        b.link(F, "F", rs, "F");
        b.link(bc, "dofs", rs, "dofs");
        b.link(rs, "system", sol, "system");
        b.link(model, "model", sol, "model");
        b.link(sol, "d_f", disp, "d_f");
        b.link(bc, "dofs", disp, "dofs");
        b.link(K, "K", reac, "K");
        b.link(disp, "d", reac, "d");
        b.link(F, "F", reac, "F");
        b.link(bc, "dofs", reac, "dofs");
        for (int n : { eq, eqNoD })
        {
            b.link(model, "model", n, "model");
            b.link(bc, "dofs", n, "dofs");
            b.link(loads, "loads", n, "loads");
            b.link(reac, "R", n, "R");
        }
        b.link(disp, "d", eq, "d");
        const int start = b.node("event.start"), p1 = b.node("debug.print"), p2 = b.node("debug.print");
        b.link(eq, "balanced", p1, "value");
        b.link(Ks, "count", p2, "value");
        b.then(start, p1);
        b.then(p1, p2);
        TEST_CHECK(b.error.empty(), "Test L24: graphe pas à pas (" << b.error << ")");
        const auto rep = Runner(lib, g, nullptr).run();
        TEST_CHECK(rep.ok, "Test L24: exécution (" << rep.message << ")");
        TEST_CHECK(rep.values.at(eq).at("balanced") == Value(true), "Test L24: équilibre vérifié avec les forces des ressorts");
        TEST_CHECK(outputReal(rep, Ks, "count") == 1.0, "Test L24: un ressort actif");
        const auto* k = outputObject<RS::MatrixObject>(rep, K, "K");
        const auto* ks = outputObject<RS::MatrixObject>(rep, Ks, "Ks");
        TEST_CHECK(k && ks, "Test L24: K et K_ressorts produits");
        const auto* res = outputObject<RS::ResultObject>(rep, nodeOfType(g, "struct.solve_direct"), "result");
        TEST_CHECK(res, "Test L24: calcul direct du même graphe");
        // K du bloc = Σ A_eᵀ·k_e·A_e (élément par élément) + K_ressorts
        tsalab::numerics::Matrix sum(k->value().matrix.rows, k->value().matrix.cols);
        for (const auto& def : res->value().model.elements)
        {
            S::FrameElement2D el;
            TEST_CHECK(S::makeElement(res->value().model, def, el), "Test L24: élément");
            const auto A = S::assemblyOperator(res->value().dofs, def.nodeI, def.nodeJ);
            const auto ke = tsalab::numerics::LinAlg::congruence(A, el.globalStiffness());
            for (std::size_t q = 0; q < sum.data.size(); ++q) sum.data[q] += ke.data[q];
        }
        for (std::size_t q = 0; q < sum.data.size(); ++q) sum.data[q] += ks->value().matrix.data[q];
        TEST_CHECK(maxDiff(sum, k->value().matrix) <= 1e-12 * tsalab::numerics::LinAlg::maxAbs(sum), "Test L24: K = Σ A_eᵀ·k_e·A_e + K_ressorts");
        // Sans d, le bilan d'un modèle à ressorts est refusé (forces des ressorts inconnues)
        Graph g2 = g;
        Builder b2 { g2, lib };
        const int p3 = b2.node("debug.print");
        b2.link(eqNoD, "balanced", p3, "value");
        b2.then(nodeOfType(g2, "event.start"), p3);
        const auto rep2 = Runner(lib, g2, nullptr).run();
        TEST_CHECK(!rep2.ok && rep2.message.find("appuis élastiques") != std::string::npos, "Test L24: bilan sans d refusé (" << rep2.message << ")");
        std::cout << "[PASS] Test L24: méthode pas à pas avec appui élastique — K = Σ A_eᵀk_eA_e + K_ressorts, équilibre avec −k·u" << std::endl;
        ++passed;
    }

    // -------------------------------------------------------------------------
    // TEST L25 : blocs de dynamique (accélérogramme lu dans un fichier, spectre, Newmark) ≡ cœur scientifique ;
    // comparaison de deux solveurs
    // -------------------------------------------------------------------------
    {
        QTemporaryDir tmp;
        TEST_CHECK(tmp.isValid(), "Test L25: dossier temporaire");
        // Signal de vérification (harmonique, analytique) écrit pour l'essai : ce n'est pas un séisme
        const QString path = tmp.filePath(QStringLiteral("signal_verification.txt"));
        {
            QFile f(path);
            TEST_CHECK(f.open(QIODevice::WriteOnly | QIODevice::Text), "Test L25: écriture du signal d'essai");
            std::string text = "# signal harmonique de verification (analytique, pas un seisme)\nt a\n";
            for (int k = 0; k <= 400; ++k)
            {
                const double t = 0.005 * k;
                text += std::to_string(t) + " " + std::to_string(0.2 * std::sin(2.0 * std::acos(-1.0) * 1.5 * t)) + "\n";
            }
            f.write(text.c_str());
        }
        const QByteArray before = [&] {
            QFile f(path);
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        }();
        Graph g;
        std::string error;
        TEST_CHECK(SG::build("analyse-modale", g, &error), "Test L25: oscillateur (" << error << ")");
        const int model = nodeOfType(g, "struct.model_add_mass");
        Builder b { g, lib };
        const int acc = b.node("struct.accelerogram_file", { { "path", path.toStdString() }, { "scale", 9.80665 } });
        const int th = b.node("struct.time_history", { { "dt", 0.005 }, { "duration", 2.0 } });
        const int spec = b.node("struct.accelerogram_spectrum", { { "periods", std::string("0.3 0.6928") }, { "xi", 0.0 } });
        b.link(model, "model", th, "model");
        b.link(acc, "motion", th, "ground");
        b.link(acc, "motion", spec, "motion");
        const int cmpSolvers = b.node("matrix.compare_solvers");
        const int Kt = b.node("matrix.from_text", { { "text", std::string("4 1 0 ; 1 3 1 ; 0 1 2") } });
        const int ft = b.node("vector.from_text", { { "text", std::string("1 2 3") } });
        b.link(Kt, "M", cmpSolvers, "K");
        b.link(ft, "v", cmpSolvers, "f");
        // Affichages insérés en tête de la chaîne d'exécution de l'exemple (les blocs purs sont évalués par leurs lecteurs)
        const int start = nodeOfType(g, "event.start");
        const auto first = g.linksFrom(start, "then");
        TEST_CHECK(first.size() == 1, "Test L25: chaîne d'exécution de l'exemple");
        const Link next = first.front();
        g.removeLink(next);
        int last = start;
        for (const auto& [n, pin] : std::vector<std::pair<int, std::string>> { { th, "summary" }, { spec, "curve" }, { cmpSolvers, "report" }, { acc, "skipped" } })
        {
            const int p = b.node("debug.print");
            b.link(n, pin, p, "value");
            b.then(last, p);
            last = p;
        }
        b.link(last, "then", next.toNode, next.toPin);
        TEST_CHECK(b.error.empty(), "Test L25: graphe (" << b.error << ")");
        const auto rep = Runner(lib, g, nullptr).run();
        TEST_CHECK(rep.ok, "Test L25: exécution (" << rep.message << ")");
        TEST_CHECK(outputReal(rep, acc, "points") == 401.0 && outputReal(rep, acc, "skipped") == 2.0, "Test L25: 401 points lus, 2 lignes d'en-tête ignorées");
        TEST_CHECK(std::abs(outputReal(rep, acc, "peak") - 0.2 * 9.80665) <= 1e-3 * 0.2 * 9.80665, "Test L25: pic du signal × facteur d'unité");
        // Même calcul par le cœur scientifique, mêmes données lues : résultats identiques
        const auto* motion = outputObject<RS::GroundMotionObject>(rep, acc, "motion");
        const auto* hist = outputObject<RS::TimeHistoryObject>(rep, th, "history");
        const auto* modelObj = outputObject<RS::ModelObject>(rep, model, "model");
        TEST_CHECK(motion && hist && modelObj && motion->value().source.find("signal_verification.txt") != std::string::npos, "Test L25: provenance du signal");
        S::TimeHistoryOptions o;
        o.dt = 0.005;
        o.duration = 2.0;
        const S::TimeHistoryResult direct = S::timeHistory(modelObj->value(), o, nullptr, &motion->value());
        TEST_CHECK(direct.success && direct == hist->value(), "Test L25: bloc Newmark ≡ tsalab::structural::timeHistory");
        S::ResponseSpectrum ref;
        TEST_CHECK(S::accelerogramSpectrum(motion->value().acceleration, { 0.3, 0.6928 }, 0.0, ref), "Test L25: spectre de référence");
        const auto* sp = outputObject<RS::SpectrumObject>(rep, spec, "spectrum");
        TEST_CHECK(sp && sp->value().points == ref.points, "Test L25: spectre du bloc ≡ accelerogramSpectrum");
        TEST_CHECK(outputReal(rep, cmpSolvers, "deviation") <= 1e-9 && outputReal(rep, cmpSolvers, "residual_b") <= 1e-9,
                   "Test L25: Cholesky et gradient conjugué concordent (écart " << outputReal(rep, cmpSolvers, "deviation") << ")");
        QFile f(path);
        TEST_CHECK(f.open(QIODevice::ReadOnly) && f.readAll() == before, "Test L25: fichier d'accélérogramme non modifié");
        // Fichier absent : refus motivé
        Graph e;
        Builder eb { e, lib };
        const int bad = eb.node("struct.accelerogram_file", { { "path", tmp.filePath(QStringLiteral("absent.txt")).toStdString() } });
        const int es = eb.node("event.start"), ep = eb.node("debug.print");
        eb.link(bad, "points", ep, "value");
        eb.then(es, ep);
        const auto r2 = Runner(lib, e, nullptr).run();
        TEST_CHECK(!r2.ok && r2.message.find("illisible") != std::string::npos, "Test L25: fichier absent refusé (" << r2.message << ")");
        std::cout << "[PASS] Test L25: accélérogramme fourni (fichier non modifié), Newmark et spectre ≡ cœur scientifique, comparaison de solveurs" << std::endl;
        ++passed;
    }

    // -------------------------------------------------------------------------
    // TEST L26 : enregistrement et réouverture des graphes des nouveaux modules (versions de blocs, résultats identiques)
    // -------------------------------------------------------------------------
    {
        QTemporaryDir tmp;
        TEST_CHECK(tmp.isValid(), "Test L26: dossier temporaire");
        for (const char* id : { "rotules-ressorts", "charges-roulantes", "analyse-spectrale" })
        {
            Graph g, back;
            std::string error;
            TEST_CHECK(SG::build(id, g, &error), "Test L26: " << id);
            const QString path = tmp.filePath(QString::fromLatin1(id) + QStringLiteral(".tsbp"));
            QString ferr;
            TEST_CHECK(saveFile(g, path, &ferr), "Test L26: enregistrement de " << id << " (" << ferr.toStdString() << ")");
            TEST_CHECK(loadFile(path, back, &ferr), "Test L26: réouverture de " << id << " (" << ferr.toStdString() << ")");
            TEST_CHECK(back.nodes().size() == g.nodes().size() && back.links().size() == g.links().size(), "Test L26: nœuds et liens restaurés (" << id << ")");
            const auto r1 = Runner(lib, g, nullptr).run(), r2 = Runner(lib, back, nullptr).run();
            TEST_CHECK(r1.ok && r2.ok && r1.log == r2.log, "Test L26: même journal après réouverture (" << id << ")");
        }
        QFile f(tmp.filePath(QStringLiteral("rotules-ressorts.tsbp")));
        TEST_CHECK(f.open(QIODevice::ReadOnly), "Test L26: lecture du fichier");
        const QByteArray json = f.readAll();
        TEST_CHECK(json.contains("\"struct.frame_element\"") && json.contains("\"struct.elastic_support\""), "Test L26: blocs enregistrés");
        std::cout << "[PASS] Test L26: graphes rotule/ressort, charges roulantes et spectral enregistrés et rouverts à l'identique" << std::endl;
        ++passed;
    }

    // -------------------------------------------------------------------------
    // TEST L27 : import de projets TSA réels (exemples du dépôt TSA, copiés dans un dossier temporaire) : portées planes,
    // rapport, traçabilité, validation croisée avec le solveur 2D de TSA (MetDeDeplacement sur l'entrée Custom2D), fichiers
    // jamais modifiés ; refus motivés : fichier non TSA, tronqué, de version future, câble, portée inconnue, annulation
    // -------------------------------------------------------------------------
    {
        const QDir root = QDir(QStringLiteral(TSALAB_TSA_BENCHMARKS)).absoluteFilePath(QStringLiteral("../../.."));
        QTemporaryDir tmp;
        TEST_CHECK(tmp.isValid(), "Test L27: dossier temporaire");
        int importedPlanes = 0, refusedPlanes = 0, compared = 0;
        std::size_t trussBars = 0;
        for (const char* file : { "TSA_Demo_Hangar_60x40m.tsa", "TSA_Demo_Pont_Treillis_3x48m.tsa", "TSA_Projet_Batiment_BA_R+4.tsa" })
        {
            const QString original = root.filePath(QStringLiteral("TSA/examples/") + QString::fromLatin1(file));
            TEST_CHECK(QFileInfo::exists(original), "Test L27: exemple TSA présent (" << file << ")");
            const std::string h0 = RS::fileSha256(original.toStdString());
            const QString copy = tmp.filePath(QString::fromLatin1(file));
            TEST_CHECK(!h0.empty() && QFile::copy(original, copy), "Test L27: copie de " << file);
            std::vector<std::string> scopes;
            std::string err;
            TEST_CHECK(RS::listTsaScopes(copy.toStdString(), scopes, &err) && !scopes.empty(), "Test L27: portées de " << file << " (" << err << ")");
            for (std::size_t k = 0; k < scopes.size() && k < 6; ++k)
            {
                S::Model2D m;
                RS::TsaImportReport rep;
                tsalab::planar::Input in;
                if (!RS::importTsaProject(copy.toStdString(), { scopes[k] }, m, rep, nullptr, &in))
                {
                    TEST_CHECK(!rep.errors.empty() && rep.message.rfind("Import refusé", 0) == 0 && m.nodes.empty(),
                               "Test L27: refus motivé (" << file << ", « " << scopes[k] << " ») : " << rep.message);
                    ++refusedPlanes;
                    continue;
                }
                ++importedPlanes;
                trussBars += rep.trussBars;
                TEST_CHECK(S::validate(m).ok(), "Test L27: modèle importé valide (" << file << ", " << scopes[k] << ")");
                TEST_CHECK(m.metadataValue("sha256") == h0 && m.metadataValue("fichier") == copy.toStdString() && !m.metadataValue("portée").empty()
                               && !m.metadataValue("format").empty(),
                           "Test L27: traçabilité (fichier, empreinte, portée, format)");
                TEST_CHECK(m.nodes.front().name.rfind("TSA N", 0) == 0 && !m.elements.front().name.empty(), "Test L27: correspondance des nœuds et éléments TSA");
                TEST_CHECK(rep.text().find("Importé") != std::string::npos && rep.nodes == m.nodes.size(), "Test L27: rapport d'import");
                // Validation croisée, un cas chargé par portée : Model2D (rigidité directe) ≡ solveur 2D de TSA (MetDeDeplacement)
                for (const auto& lc : m.loadCases)
                {
                    if (lc.empty()) continue;
                    const auto res = S::solveLinearStatic(m, { lc.id, 0 });
                    tsalab::planar::Input one = in;
                    one.request.loadCaseIds = { lc.id };
                    one.request.combinationId = 0;
                    const auto out = tsalab::planar::MetDeDeplacementSolver().solve(one);
                    TEST_CHECK(res.success == out.success, "Test L27: même issue que le solveur 2D de TSA (" << file << ", cas " << lc.id << ") : " << res.message
                                                                                                               << " / " << out.message);
                    if (!res.success) break;
                    TEST_CHECK(res.equilibrium.balanced(1e-9), "Test L27: équilibre (" << res.equilibrium.text() << ")");
                    double scale = 0.0, diff = 0.0;
                    for (const auto& d : out.displacements)
                    {
                        const auto u = res.nodeDisplacement(d.node);
                        const double ref[3] = { d.ux, d.uy, d.rz };
                        for (int c = 0; c < 3; ++c)
                        {
                            scale = std::max(scale, std::abs(ref[c]));
                            diff = std::max(diff, std::abs(u[static_cast<std::size_t>(c)] - ref[c]));
                        }
                    }
                    TEST_CHECK(scale > 0.0 && diff <= 1e-9 * scale,
                               "Test L27: déplacements ≡ MetDeDeplacement (" << file << ", « " << scopes[k] << " », cas " << lc.id << ", écart relatif " << diff / scale << ")");
                    ++compared;
                    break;
                }
            }
            TEST_CHECK(RS::fileSha256(original.toStdString()) == h0 && RS::fileSha256(copy.toStdString()) == h0, "Test L27: projet source jamais modifié (" << file << ")");
        }
        TEST_CHECK(importedPlanes >= 2 && compared >= 2, "Test L27: au moins deux portées importées et comparées (" << importedPlanes << ", " << compared << ")");
        // Refus motivés
        auto refusal = [&](const QString& path, const std::string& scope, const std::atomic<bool>* cancel = nullptr) {
            S::Model2D m;
            RS::TsaImportReport rep;
            const bool ok = RS::importTsaProject(path.toStdString(), { scope }, m, rep, cancel);
            return ok ? std::string("(accepté)") : rep.text();
        };
        const QString junk = tmp.filePath(QStringLiteral("non_tsa.tsa"));
        {
            QFile f(junk);
            TEST_CHECK(f.open(QIODevice::WriteOnly) && f.write(QByteArray(5000, '\x5A')) == 5000, "Test L27: fichier non TSA");
        }
        TEST_CHECK(refusal(junk, "").find("en-tête TSA invalide") != std::string::npos, "Test L27: fichier non TSA refusé");
        const QString truncated = tmp.filePath(QStringLiteral("tronque.tsa"));
        TEST_CHECK(QFile::copy(tmp.filePath(QStringLiteral("TSA_Demo_Hangar_60x40m.tsa")), truncated), "Test L27: copie à tronquer");
        {
            QFile f(truncated);
            TEST_CHECK(f.resize(f.size() / 2), "Test L27: fichier tronqué");
        }
        TEST_CHECK(refusal(truncated, "").find("Import refusé") != std::string::npos, "Test L27: fichier tronqué refusé");
        const QString future = tmp.filePath(QStringLiteral("version_future.tsa"));
        {
            TSA::IO::TSAFileHeader h;   // en-tête du format de TSA, version majeure future
            h.versionMajor = static_cast<uint16_t>(TSA::IO::TSA_FORMAT_VERSION_MAJOR + 1);
            QFile f(future);
            TEST_CHECK(f.open(QIODevice::WriteOnly) && f.write(reinterpret_cast<const char*>(&h), sizeof(h)) == static_cast<qint64>(sizeof(h)), "Test L27: en-tête futur");
        }
        TEST_CHECK(refusal(future, "").find("plus récente") != std::string::npos, "Test L27: version future refusée (" << refusal(future, "") << ")");
        const QString cablePath = tmp.filePath(QStringLiteral("cable.tsa"));
        {
            TSA::Model::Model cm;
            const int n1 = cm.addNode(0, 0, 0), n2 = cm.addNode(10, 0, 0), n3 = cm.addNode(0, 0, 5);
            cm.addBeam(n1, n2);
            cm.addColumn(n1, n3);
            cm.addCable(n3, n2, TSA::Model::CableType::StayCable);
            cm.getNode(n1)->setSupportType(TSA::Model::SupportType::Fixed);
            cm.getNode(n2)->setSupportType(TSA::Model::SupportType::Pinned);
            TEST_CHECK(TSA::IO::TSAProjectIO::saveProject(cablePath, cm), "Test L27: projet à câble enregistré par TSA");
        }
        const std::string cableText = refusal(cablePath, "");
        TEST_CHECK(cableText.find("Import refusé") != std::string::npos && (cableText.find("câble") != std::string::npos || cableText.find("Câble") != std::string::npos),
                   "Test L27: câble refusé, sans conversion silencieuse (" << cableText << ")");
        // Barres de treillis (projet enregistré par TSA) : converties en barres bi-articulées, signalées, ≡ solveur 2D de TSA
        const QString trussPath = tmp.filePath(QStringLiteral("treillis.tsa"));
        {
            TSA::Model::Model tm;
            const int na = tm.addNode(0, 0, 0), nb = tm.addNode(4, 0, 0), nc = tm.addNode(2, 0, 2);
            tm.addTrussMember(na, nb);
            tm.addTrussMember(na, nc);
            tm.addTrussMember(nc, nb);
            tm.getNode(na)->setSupportType(TSA::Model::SupportType::Pinned);
            tm.getNode(nb)->setSupportType(TSA::Model::SupportType::Pinned);
            const int lc = tm.loadManager().addLoadCase(TSA::Model::LoadCase(0, "P", TSA::Model::LoadCaseCategory::Live));
            tm.loadManager().addNodalLoad(TSA::Model::NodalLoad(0, nc, lc, 0.0, 0.0, -50.0, 0.0, 0.0, 0.0, TSA::Model::LoadCoordSystem::Global, "P"));
            TEST_CHECK(TSA::IO::TSAProjectIO::saveProject(trussPath, tm), "Test L27: projet de treillis enregistré par TSA");
        }
        {
            S::Model2D m;
            RS::TsaImportReport rep;
            tsalab::planar::Input in;
            TEST_CHECK(RS::importTsaProject(trussPath.toStdString(), {}, m, rep, nullptr, &in), "Test L27: treillis importé (" << rep.text() << ")");
            TEST_CHECK(rep.trussBars == 3 && rep.text().find("bi-articulée") != std::string::npos, "Test L27: 3 barres de treillis converties et signalées");
            for (const auto& e : m.elements) TEST_CHECK(e.releaseI && e.releaseJ, "Test L27: barre de treillis bi-articulée");
            const auto res = S::solveLinearStatic(m, { m.loadCases.front().id, 0 });
            TEST_CHECK(res.success && res.bc.eliminatedDofs.size() == 3, "Test L27: treillis calculé, 3 rotations éliminées (" << res.message << ")");
            tsalab::planar::Input one = in;
            one.request.loadCaseIds = { m.loadCases.front().id };
            const auto out = tsalab::planar::MetDeDeplacementSolver().solve(one);
            TEST_CHECK(out.success, "Test L27: solveur 2D de TSA sur le treillis (" << out.message << ")");
            double scale = 0.0, diff = 0.0;
            for (const auto& d : out.displacements)
            {
                const auto u = res.nodeDisplacement(d.node);
                scale = std::max({ scale, std::abs(d.ux), std::abs(d.uy) });
                diff = std::max({ diff, std::abs(u[0] - d.ux), std::abs(u[1] - d.uy) });
            }
            TEST_CHECK(scale > 0.0 && diff <= 1e-9 * scale, "Test L27: treillis ≡ MetDeDeplacement (écart relatif " << diff / scale << ")");
            trussBars += rep.trussBars;
        }
        const std::string unknown = refusal(tmp.filePath(QStringLiteral("TSA_Demo_Hangar_60x40m.tsa")), "portée inexistante");
        TEST_CHECK(unknown.find("inconnue") != std::string::npos && unknown.find("Plan du modèle") != std::string::npos, "Test L27: portée inconnue refusée, portées listées");
        const std::atomic<bool> stop { true };
        TEST_CHECK(refusal(tmp.filePath(QStringLiteral("TSA_Demo_Hangar_60x40m.tsa")), "", &stop).find("annulé") != std::string::npos, "Test L27: import annulable");
        std::cout << "[PASS] Test L27: import TSA — " << importedPlanes << " portée(s) importée(s), " << refusedPlanes << " refusée(s) avec la raison, " << compared
                  << " comparée(s) au solveur 2D de TSA (1e-9), " << trussBars << " barre(s) de treillis converties ; sources intactes ; non TSA, tronqué, "
                  << "version future, câble, portée inconnue, annulation refusés" << std::endl;
        ++passed;
    }
    // -------------------------------------------------------------------------
    // TEST L28 : méthode des déplacements en six sous-graphes (étapes encapsulées) ≡ graphe pas à pas ≡ calcul direct ;
    // interfaces typées et documentées, journal des corps, enregistrement en version 3 et réouverture
    // -------------------------------------------------------------------------
    {
        std::string error;
        Graph flat, sub;
        TEST_CHECK(SG::build("methode-deplacements-2d", flat, &error), "Test L28: graphe pas à pas (" << error << ")");
        TEST_CHECK(SG::build("methode-sous-graphes", sub, &error), "Test L28: graphe en sous-graphes (" << error << ")");
        const std::set<std::string> expected = { "construire_modele", "assembler_k_f", "conditions_limites", "resoudre_deplacements",
                                                 "reactions_efforts", "verifier_equilibre" };
        TEST_CHECK(sub.subgraphs.size() == expected.size(), "Test L28: six sous-graphes");
        for (const auto& d : sub.subgraphs)
            TEST_CHECK(expected.count(d.id) && d.version == 1 && !d.documentation.empty() && !d.description.empty(),
                       "Test L28: définition documentée « " << d.id << " »");
        std::unique_ptr<NodeLibrary> holder;
        const NodeLibrary& L = lib.forGraph(sub, holder);
        const NodeDefinition* assembly = L.find("sub.assembler_k_f");
        TEST_CHECK(assembly && !assembly->pure && assembly->output("K") && assembly->output("K")->objectType == "Matrix" && assembly->output("F")
                       && assembly->output("numerotation") && assembly->input("model") && assembly->input("loads"),
                   "Test L28: interface « Assembler K et F » (entrées modèle et charges ; sorties K, F, numérotation)");
        const NodeDefinition* model = L.find("sub.construire_modele");
        TEST_CHECK(model && model->input("q") && model->input("H") && model->input("cas") && model->input("combinaison") && model->output("model"),
                   "Test L28: entrées q, H, cas, combinaison du modèle");
        TEST_CHECK(L.validate(sub).empty() && L.warnings(sub).empty(), "Test L28: graphe valide, sans avertissement");
        const auto ref = Runner(lib, flat, nullptr).run();
        const auto rep = Runner(lib, sub, nullptr).run();
        TEST_CHECK(ref.ok && rep.ok, "Test L28: exécutions (" << ref.message << " ; " << rep.message << ")");
        TEST_CHECK(logContains(rep, "Graphe conforme au calcul direct : vrai") && logContains(rep, "[Assembler K et F] Étape 3"),
                   "Test L28: conforme au calcul direct, journal des corps préfixé du nom du sous-graphe");
        const auto* a = graphResult(flat, ref);
        const auto* b = graphResult(sub, rep);
        TEST_CHECK(a && b && a->K == b->K && a->F == b->F && a->d == b->d && a->R == b->R && a->elements.size() == b->elements.size(),
                   "Test L28: K, F, d et R identiques au graphe pas à pas (mêmes opérations dans le même ordre)");
        // Lignes de résultats imprimées (déplacements, réactions, équilibre, efforts internes) : identiques
        auto unprefixed = [](const std::string& l) {
            return l.rfind("[", 0) == 0 && l.find("] ") != std::string::npos ? l.substr(l.find("] ") + 2) : l;
        };
        std::multiset<std::string> printed;
        for (const auto& l : rep.log) printed.insert(unprefixed(l));
        int compared = 0;
        for (const auto& l : ref.log)
            if (l.rfind("Étape 6", 0) == 0 || l.rfind("Étape 7", 0) == 0 || l.rfind("Étape 8", 0) == 0)
            {
                TEST_CHECK(printed.count(l) > 0, "Test L28: ligne « " << l.substr(0, 60) << " » identique");
                ++compared;
            }
        TEST_CHECK(compared >= 4, "Test L28: lignes de résultats comparées (" << compared << ")");
        // Enregistrement (version 3) et réouverture
        QTemporaryDir dir;
        const QString file = dir.filePath(QStringLiteral("methode_sous_graphes.tsbp"));
        Graph back;
        QString ioError;
        TEST_CHECK(saveFile(sub, file, &ioError) && loadFile(file, back, &ioError) && back.subgraphs.size() == 6,
                   "Test L28: enregistré et relu (" << ioError.toStdString() << ")");
        QFile f(file);
        TEST_CHECK(f.open(QIODevice::ReadOnly) && f.readAll().contains("\"version\": 3"), "Test L28: fichier en version 3");
        const auto again = Runner(lib, back, nullptr).run();
        const auto* c = graphResult(back, again);
        TEST_CHECK(again.ok && c && c->d == b->d && c->R == b->R, "Test L28: graphe relu exécuté, mêmes résultats");
        std::cout << "[PASS] Test L28: méthode des déplacements en six sous-graphes ≡ graphe pas à pas (K, F, d, R à l'identique, " << compared
                  << " lignes de résultats), conforme au calcul direct, version 3 relue" << std::endl;
        ++passed;
    }
    // -------------------------------------------------------------------------
    // TEST L29 : graphe d'analyse d'un projet TSA importé (boîte « Importer un portique d'un projet TSA ») : projet lu une
    // seule fois par exécution, résultat ≡ import et calcul direct en C++, projet non modifié
    // -------------------------------------------------------------------------
    {
        const QDir root = QDir(QStringLiteral(TSALAB_TSA_BENCHMARKS)).absoluteFilePath(QStringLiteral("../../.."));
        QTemporaryDir tmp;
        const QString original = root.filePath(QStringLiteral("TSA/examples/TSA_Demo_Hangar_60x40m.tsa"));
        const QString copy = tmp.filePath(QStringLiteral("TSA_Demo_Hangar_60x40m.tsa"));
        TEST_CHECK(tmp.isValid() && QFile::copy(original, copy), "Test L29: copie du projet");
        const std::string h0 = RS::fileSha256(copy.toStdString());
        std::vector<std::string> scopes;
        TEST_CHECK(RS::listTsaScopes(copy.toStdString(), scopes) && !scopes.empty(), "Test L29: portées du projet");
        S::Model2D ref;
        std::string scope;
        for (const auto& candidate : scopes)
        {
            S::Model2D m;
            RS::TsaImportReport r;
            if (RS::importTsaProject(copy.toStdString(), { candidate }, m, r) && !m.loadCases.empty())
            {
                ref = std::move(m);
                scope = candidate;
                break;
            }
        }
        TEST_CHECK(!scope.empty(), "Test L29: portée importable avec un cas de charge");
        const int caseId = ref.loadCases.front().id;
        Graph g;
        std::string error;
        TEST_CHECK(SG::buildTsaImport(copy.toStdString(), scope, caseId, 0, g, &error), "Test L29: graphe d'analyse (" << error << ")");
        TEST_CHECK(lib.validate(g).empty() && lib.warnings(g).empty(), "Test L29: graphe valide, sans avertissement");
        const auto run = Runner(lib, g, nullptr).run();
        TEST_CHECK(run.ok, "Test L29: graphe exécuté (" << run.message << ")");
        const int importNode = nodeOfType(g, "struct.model_import_tsa");
        TEST_CHECK(run.profile.count(importNode) && run.profile.at(importNode).executions == 1, "Test L29: projet lu une seule fois par exécution");
        const auto* r = outputObject<RS::ResultObject>(run, nodeOfType(g, "struct.solve_direct"), "result");
        const auto direct = S::solveLinearStatic(ref, { caseId, 0 });
        TEST_CHECK(r && direct.success && r->value().d == direct.d && r->value().R == direct.R,
                   "Test L29: résultat du graphe ≡ import et calcul direct en C++ (" << direct.message << ")");
        TEST_CHECK(logContains(run, "Contrôle des données") && logContains(run, "Calcul : "), "Test L29: contrôle et calcul imprimés");
        TEST_CHECK(RS::fileSha256(copy.toStdString()) == h0, "Test L29: projet non modifié");
        std::cout << "[PASS] Test L29: graphe d'analyse d'un projet TSA (portée « " << scope << " », cas " << caseId
                  << ") — lu une fois, ≡ calcul direct, projet intact" << std::endl;
        ++passed;
    }
    // -------------------------------------------------------------------------
    // TEST L30 : persistance JSON du modèle et des résultats — relecture à l'identique (portique de démonstration avec
    // combinaison, poutre continue avec tassement, variante à rotule, ressort, masse et tous les types de charge, modèle
    // importé de TSA avec traçabilité), résultats complets, blocs d'un graphe, valeurs non finies, refus motivés
    // -------------------------------------------------------------------------
    {
        QTemporaryDir dir;
        std::string error;
        S::Model2D portal;
        TEST_CHECK(RS::demoPortal({}, portal, &error), "Test L30: portique de démonstration (" << error << ")");
        S::Model2D beam = RS::settlementBeam();
        S::Model2D hinge = RS::settlementBeam();
        hinge.name = "Variante à rotule et ressort (essai JSON)";
        hinge.elements.front().releaseJ = true;
        hinge.supports.back() = { 3, false, false, false, 0.0, 5000.0, 0.0 };
        hinge.nodalMasses = { { 2, 1.5, 1.5, 0.25 } };
        hinge.metadata = { { "source", "essai" }, { "remarque", "accents : é, °, ², « guillemets »" } };
        S::LoadCase all;
        all.id = 9;
        all.name = "toutes les charges";
        auto linear = S::linearLoad(1, -1.0, -2.0, 0.5, 2.5, S::LoadDirection::GlobalY);
        linear.projected = true;
        all.elementLoads = { S::uniformLoad(1, -3.0), linear, S::pointForce(1, 4.0, 0.3, S::LoadDirection::LocalX, true), S::pointMoment(2, 2.0, 1.0),
                             S::thermalLoad(2, 15.0, 5.0), S::initialStrain(1, 1e-4, 2e-5), S::selfWeight(0, 1.35) };
        all.nodalLoads = { { 3, 1.0, -2.0, 0.5 } };
        hinge.loadCases.push_back(all);
        hinge.combinations = { { 5, "1,35 q + 1,5 tout", { { 1, 1.35 }, { 9, 1.5 } } } };
        TEST_CHECK(S::validate(hinge).ok(), "Test L30: variante valide (" << S::validate(hinge).text() << ")");
        // Modèle importé d'un projet TSA (traçabilité : fichier, empreinte, date)
        const QDir root = QDir(QStringLiteral(TSALAB_TSA_BENCHMARKS)).absoluteFilePath(QStringLiteral("../../.."));
        const QString tsa = dir.filePath(QStringLiteral("hangar.tsa"));
        TEST_CHECK(QFile::copy(root.filePath(QStringLiteral("TSA/examples/TSA_Demo_Hangar_60x40m.tsa")), tsa), "Test L30: copie du projet TSA");
        S::Model2D imported;
        std::vector<std::string> tsaScopes;
        TEST_CHECK(RS::listTsaScopes(tsa.toStdString(), tsaScopes), "Test L30: portées du projet TSA");
        for (const auto& scope : tsaScopes)   // le hangar n'est pas plan : première portée plane importable
        {
            RS::TsaImportReport importReport;
            if (RS::importTsaProject(tsa.toStdString(), { scope }, imported, importReport)) break;
            imported = {};
        }
        TEST_CHECK(!imported.nodes.empty() && !imported.metadata.empty(), "Test L30: modèle importé d'une portée plane, avec traçabilité");
        for (const S::Model2D* m : { &portal, &beam, &hinge, &imported })
        {
            S::Model2D back;
            TEST_CHECK(RS::modelFromJson(RS::modelToJson(*m), back, &error) && back == *m, "Test L30: modèle « " << m->name << " » relu à l'identique (" << error << ")");
        }
        // Résultats complets : K, système réduit, résolution, diagrammes, équilibre
        const std::pair<const S::Model2D*, S::LoadSelection> runs[] = { { &portal, { 1, 1 } }, { &beam, { 2, 0 } }, { &hinge, { 9, 0 } }, { &hinge, { 1, 5 } } };
        for (const auto& [m, selection] : runs)
        {
            const auto res = S::solveLinearStatic(*m, selection);
            TEST_CHECK(res.success && !res.elements.empty(), "Test L30: calcul de « " << m->name << " » (" << res.message << ")");
            S::AnalysisResult2D back;
            TEST_CHECK(RS::resultFromJson(RS::resultToJson(res), back, &error) && back == res, "Test L30: résultat de « " << m->name << " » relu à l'identique (" << error << ")");
        }
        const auto mechanism = S::solveLinearStatic(RS::unstableBeam(), { 1, 0 });
        S::AnalysisResult2D mechanismBack;
        TEST_CHECK(!mechanism.success && RS::resultFromJson(RS::resultToJson(mechanism), mechanismBack, &error) && mechanismBack == mechanism,
                   "Test L30: résultat en échec (diagnostic du mécanisme) relu à l'identique (" << error << ")");
        // Valeurs non finies
        S::Model2D odd = portal;
        odd.nodes[0].x = std::numeric_limits<double>::infinity();
        odd.nodes[1].x = -std::numeric_limits<double>::infinity();
        odd.nodes[1].y = std::numeric_limits<double>::quiet_NaN();
        S::Model2D oddBack;
        TEST_CHECK(RS::modelFromJson(RS::modelToJson(odd), oddBack, &error) && std::isinf(oddBack.nodes[0].x) && oddBack.nodes[0].x > 0
                       && std::isinf(oddBack.nodes[1].x) && oddBack.nodes[1].x < 0 && std::isnan(oddBack.nodes[1].y),
                   "Test L30: NaN et ±∞ conservés");
        // Fichiers
        const std::string modelPath = dir.filePath(QStringLiteral("portique.json")).toStdString();
        S::Model2D fromFile;
        TEST_CHECK(RS::saveModelJson(portal, modelPath, &error) && RS::loadModelJson(modelPath, fromFile, &error) && fromFile == portal,
                   "Test L30: modèle enregistré puis relu (" << error << ")");
        TEST_CHECK(!RS::saveModelJson(portal, dir.filePath(QStringLiteral("absent/portique.json")).toStdString(), &error) && error.find("écriture impossible") != std::string::npos,
                   "Test L30: écriture impossible signalée");
        // Refus motivés
        const QByteArray good = RS::modelToJson(portal);
        auto refusal = [&](const QByteArray& json) {
            S::Model2D m;
            std::string why;
            return RS::modelFromJson(json, m, &why) ? std::string() : why;
        };
        auto edited = [&](const std::function<void(QJsonObject&)>& change) {
            QJsonObject top = QJsonDocument::fromJson(good).object();
            QJsonObject model = top.value(QStringLiteral("model")).toObject();
            change(model);
            top[QStringLiteral("model")] = model;
            return QJsonDocument(top).toJson();
        };
        TEST_CHECK(refusal("pas du JSON").find("JSON illisible") != std::string::npos, "Test L30: document illisible refusé");
        TEST_CHECK(refusal(RS::resultToJson(S::solveLinearStatic(portal, { 1, 0 }))).find("n'est pas au format") != std::string::npos,
                   "Test L30: résultat lu comme modèle refusé");
        QByteArray future = good;
        future.replace("\"version\": 1", "\"version\": 2");
        TEST_CHECK(refusal(future).find("plus récente") != std::string::npos, "Test L30: version future refusée");
        const std::string missing = refusal(edited([](QJsonObject& m) {
            QJsonArray supports = m[QStringLiteral("supports")].toArray();
            QJsonObject first = supports[0].toObject();
            first.remove(QStringLiteral("fixUx"));
            supports[0] = first;
            m[QStringLiteral("supports")] = supports;
        }));
        TEST_CHECK(missing == "model.supports[0].fixUx : champ manquant", "Test L30: champ manquant signalé avec son chemin (" << missing << ")");
        const std::string type = refusal(edited([](QJsonObject& m) {
            QJsonArray elements = m[QStringLiteral("elements")].toArray();
            QJsonObject first = elements[0].toObject();
            QJsonObject section = first[QStringLiteral("section")].toObject();
            section[QStringLiteral("A")] = QStringLiteral("grande");
            first[QStringLiteral("section")] = section;
            elements[0] = first;
            m[QStringLiteral("elements")] = elements;
        }));
        TEST_CHECK(type == "model.elements[0].section.A : nombre attendu", "Test L30: mauvais type signalé avec son chemin (" << type << ")");
        const std::string enumeration = refusal(edited([](QJsonObject& m) {
            QJsonArray cases = m[QStringLiteral("loadCases")].toArray();
            QJsonObject first = cases[0].toObject();
            QJsonArray loads = first[QStringLiteral("elementLoads")].toArray();
            QJsonObject load = loads[0].toObject();
            load[QStringLiteral("type")] = QStringLiteral("vent");
            loads[0] = load;
            first[QStringLiteral("elementLoads")] = loads;
            cases[0] = first;
            m[QStringLiteral("loadCases")] = cases;
        }));
        TEST_CHECK(enumeration.find("type de charge inconnu « vent »") != std::string::npos, "Test L30: énumération inconnue refusée (" << enumeration << ")");
        // Blocs d'un graphe : modèle enregistré puis relu, résultat archivé puis relu, comparés au calcul d'origine
        Graph g;
        Builder b { g, lib };
        const QString modelFile = dir.filePath(QStringLiteral("graphe_modele.json")), resultFile = dir.filePath(QStringLiteral("graphe_resultat.json"));
        const int start = b.node("event.start");
        const int demo = b.node("struct.model_demo_portal");
        const int save = b.node("struct.model_save_json", { { "path", modelFile.toStdString() } });
        const int load = b.node("struct.model_load_json", { { "path", modelFile.toStdString() } });
        const int solved = b.node("struct.solve_direct", { { "case", 1LL } });
        const int reloaded = b.node("struct.solve_direct", { { "case", 1LL } });
        const int archive = b.node("struct.result_save_json", { { "path", resultFile.toStdString() } });
        const int read = b.node("struct.result_load_json", { { "path", resultFile.toStdString() } });
        const int cmpModel = b.node("struct.compare_results"), cmpArchive = b.node("struct.compare_results");
        const int p1 = b.node("debug.print"), p2 = b.node("debug.print");
        b.link(demo, "model", save, "model");
        b.link(load, "model", reloaded, "model");
        b.link(demo, "model", solved, "model");
        b.link(solved, "result", archive, "result");
        b.link(reloaded, "result", cmpModel, "result");
        b.link(solved, "result", cmpModel, "reference");
        b.link(read, "result", cmpArchive, "result");
        b.link(solved, "result", cmpArchive, "reference");
        b.link(cmpModel, "passed", p1, "value");
        b.link(cmpArchive, "passed", p2, "value");
        b.then(start, save);
        b.then(save, archive);
        b.then(archive, p1);
        b.then(p1, p2);
        TEST_CHECK(b.error.empty(), "Test L30: graphe des blocs JSON (" << b.error << ")");
        const auto run = Runner(lib, g, nullptr).run();
        TEST_CHECK(run.ok && run.log.size() >= 4, "Test L30: graphe exécuté (" << run.message << ")");
        TEST_CHECK(run.values.at(cmpModel).at("passed") == Value(true) && run.values.at(cmpArchive).at("passed") == Value(true),
                   "Test L30: modèle relu et résultat archivé ≡ calcul d'origine");
        std::cout << "[PASS] Test L30: persistance JSON — 4 modèles et 5 résultats relus à l'identique, NaN et ±∞, blocs d'un graphe, "
                     "refus motivés (illisible, autre format, version future, champ manquant, mauvais type, énumération inconnue)"
                  << std::endl;
        ++passed;
    }
    return true;
}
