// Tests de TSALab Blueprint — programmation structurelle (tests L15 à L22) : bibliothèque de blocs pré-codés, graphe
// Methode_Des_Deplacements_2D comparé au calcul direct (mission, cas 9), sauvegarde et réouverture du graphe (cas 10),
// mécanisme (cas 7), solveur de l'étudiant (mode 2), classe FrameElement2D et charges en blocs (mode 1), tassement
// d'appui (cas 6), blocs mathématiques et exports. La science (cas 1 à 8 analytiques) est testée par
// tsalab_science_tests (S7 à S16).
#include "Blueprint/BlueprintFile.h"
#include "Blueprint/BlueprintRuntime.h"
#include "Research/Examples/StructuralGraphs.h"
#include "Research/Structural/ResultBoard.h"
#include "Research/Structural/StructuralLibrary.h"
#include "Research/Structural/StructuralModels.h"
#include "Research/Structural/StructuralObjects.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
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

int structuralLabTestCount() { return 8; }

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
            if (d->pure && d->status == NodeStatus::Validated) TEST_CHECK(d->cacheable, "Test L15: bloc pur réutilisable (" << id << ")");
        }
        TEST_CHECK(perCategory.size() == 6 && ids.size() >= 90, "Test L15: 6 catégories, " << ids.size() << " blocs");
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
        std::cout << "[PASS] Test L15: bibliothèque structurelle — " << ids.size() << " blocs en 6 catégories, documentés, sources présentes, typés"
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
    return true;
}
