// Catégorie E — contrôle et affichage : boucles sur les éléments et les identifiants, sélection et filtre d'éléments,
// comparaison de vecteurs, courbes, visualisation (tableau des résultats), export des résultats.
// Composants expérimentaux (laboratoire de l'étudiant) : itérations de Jacobi et de Gauss-Seidel pour écrire son
// propre solveur et le comparer à la solution de référence ; séparés des composants validés.

#include "NodeHelpers.h"
#include "ResultBoard.h"
#include "StructuralJson.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <cmath>
#include <sstream>

namespace TSALab::Research::Structural::detail
{

namespace
{
namespace LA = tsalab::numerics::LinAlg;

/// Éléments retenus par un filtre : « tous », « horizontaux », « verticaux », « inclinés », « ids:1,3 », « longueur>5 »,
/// « longueur<5 ».
bool selectElements(const S::Model2D& m, const std::string& filter, std::vector<int>& out, std::string* error)
{
    out.clear();
    std::string f = filter;
    for (char& ch : f)
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    auto angleOf = [&](const S::ElementDefinition& e, double& c, double& s) {
        S::FrameElement2D el;
        if (!S::makeElement(m, e, el)) return false;
        c = el.cosine(), s = el.sine();
        return true;
    };
    const double tol = 1e-9;
    if (f.rfind("ids:", 0) == 0)
    {
        std::stringstream ss(f.substr(4));
        for (std::string t; std::getline(ss, t, ',');)
        {
            try
            {
                const int id = std::stoi(t);
                if (!m.element(id))
                {
                    if (error) *error = "élément " + std::to_string(id) + " absent du modèle";
                    return false;
                }
                out.push_back(id);
            }
            catch (const std::exception&)
            {
                if (error) *error = "identifiant invalide « " + t + " »";
                return false;
            }
        }
        return true;
    }
    double limit = 0.0;
    const bool longer = f.rfind("longueur>", 0) == 0, shorter = f.rfind("longueur<", 0) == 0;
    if (longer || shorter)
    {
        try
        {
            limit = std::stod(f.substr(9));
        }
        catch (const std::exception&)
        {
            if (error) *error = "longueur invalide dans « " + filter + " »";
            return false;
        }
    }
    else if (f != "tous" && f != "horizontaux" && f != "verticaux" && f != "inclinés" && f != "inclines")
    {
        if (error) *error = "filtre inconnu « " + filter + " » (tous, horizontaux, verticaux, inclinés, ids:1,3, longueur>5, longueur<5)";
        return false;
    }
    for (const auto& e : m.elements)
    {
        double c = 1.0, s = 0.0;
        if (!angleOf(e, c, s)) continue;
        S::FrameElement2D el;
        S::makeElement(m, e, el);
        bool keep = true;
        if (f == "horizontaux") keep = std::abs(s) <= tol;
        else if (f == "verticaux") keep = std::abs(c) <= tol;
        else if (f == "inclinés" || f == "inclines") keep = std::abs(s) > tol && std::abs(c) > tol;
        else if (longer) keep = el.length() > limit;
        else if (shorter) keep = el.length() < limit;
        if (keep) out.push_back(e.id);
    }
    return true;
}

std::string csv(double v)
{
    std::ostringstream o;
    o.precision(17);
    o << v;
    return o.str();
}

bool writeFile(ExecutionContext& c, const QString& path, const std::string& content)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(QByteArray::fromStdString(content)) < 0 || !f.commit())
        return c.fail("écriture impossible : " + path.toStdString() + " (" + f.errorString().toStdString() + ")");
    c.log("Fichier écrit : " + QFileInfo(path).absoluteFilePath().toStdString());
    return true;
}

/// Contrôles communs des itérations : K carrée, f et x de même taille, diagonale non nulle.
bool iterationInputs(ExecutionContext& c, const LabeledMatrix*& K, const LabeledVector*& f, const LabeledVector*& x)
{
    K = matrixIn(c, "K");
    f = vectorIn(c, "f");
    x = vectorIn(c, "x");
    if (!K || !f || !x) return false;
    const int n = K->matrix.rows;
    if (K->matrix.cols != n || static_cast<int>(f->vector.size()) != n || static_cast<int>(x->vector.size()) != n)
        return c.fail("dimensions incompatibles : K " + std::to_string(K->matrix.rows) + "×" + std::to_string(K->matrix.cols) + ", f "
                      + std::to_string(f->vector.size()) + ", x " + std::to_string(x->vector.size()));
    for (int i = 0; i < n; ++i)
        if (K->matrix(i, i) == 0.0) return c.fail("coefficient diagonal nul à la ligne " + std::to_string(i) + " : itération impossible");
    return true;
}

double relativeResidual(const Matrix& K, const Vector& x, const Vector& f)
{
    const Vector r = LA::subtract(LA::multiply(K, x), f);
    const double nf = LA::norm2(f);
    return nf > 0.0 ? LA::norm2(r) / nf : LA::norm2(r);
}

/// UNE itération de Gauss-Seidel pour K·x = f, x mis à jour sur place (itéré k à l'entrée, k + 1 à la sortie) :
///   x_i ← (f_i − Σ_{j<i} K_ij·x_j(k+1) − Σ_{j>i} K_ij·x_j(k)) / K_ii
/// Mise à jour sur place : quand x_i est calculé, x_j pour j < i contient déjà l'itéré k + 1 (c'est la différence avec
/// Jacobi, qui n'utilise que l'itéré k). Dimensions et diagonale non nulle contrôlées par l'appelant (iterationInputs).
bool gaussSeidelSweep(const Matrix& K, const Vector& f, Vector& x)
{
    const int n = K.rows;
    for (int i = 0; i < n; ++i)
    {
        double s = f[static_cast<std::size_t>(i)];
        for (int j = 0; j < n; ++j)
            if (j != i) s -= K(i, j) * x[static_cast<std::size_t>(j)];
        x[static_cast<std::size_t>(i)] = s / K(i, i);
    }
    return true;
}
} // namespace

void registerControlNodes(NodeLibrary& lib)
{
    // --- Boucles ----------------------------------------------------------------------------------
    {
        NodeDefinition d = actionBlock("struct.for_each_element", "Pour chaque élément", kControl,
                                       "Exécute « Corps » pour chaque élément du modèle (objet FrameElement2D, index, identifiant), puis « Terminé ».",
                                       { object("model", "Modèle", Type::Model) }, {},
                                       "Boucle sur les éléments dans l'ordre du modèle : index 0 … n − 1. L'élément est construit et contrôlé "
                                       "(longueur, E, A, Iz) avant le corps. Accumulation : « Affecter une variable » dans le corps (K = K + A_eᵀ·k_e·A_e).",
                                       "TSALab/lab/Research/Structural/ControlNodes.cpp");
        d.outputs = { execOut("body", "Corps"), object("element", "Élément", Type::Element), data("index", "Index", VT::Integer), data("id", "Identifiant", VT::Integer),
                      execOut("completed", "Terminé") };
        d.orderedOutputs = true;
        lib.add(std::move(d), [](ExecutionContext& c) {
            const S::Model2D* m = modelIn(c);
            if (!m) return false;
            if (static_cast<int>(m->elements.size()) > c.maxIterations()) return c.fail("trop d'éléments pour la boucle");
            for (std::size_t k = 0; k < m->elements.size(); ++k)
            {
                S::FrameElement2D e;
                std::string error;
                if (!S::makeElement(*m, m->elements[k], e, &error)) return c.fail(error);
                const auto issues = e.validate();
                if (!issues.empty()) return c.fail(issues.front());
                c.setOutput("element", make<ElementObject>(e));
                c.setOutput("index", static_cast<long long>(k));
                c.setOutput("id", static_cast<long long>(e.id()));
                if (!c.fire("body")) return false;
            }
            return c.fire("completed");
        });
    }
    {
        NodeDefinition d = actionBlock("flow.for_each_id", "Pour chaque identifiant", kControl,
                                       "Exécute « Corps » pour chaque identifiant d'une liste (par exemple une sélection d'éléments), puis « Terminé ».",
                                       { data("ids", "Identifiants", VT::IdList, std::vector<int> { 1 }) }, {}, "Liste parcourue dans l'ordre.",
                                       "TSALab/lab/Research/Structural/ControlNodes.cpp");
        d.status = NodeStatus::Standard;
        d.outputs = { execOut("body", "Corps"), data("id", "Identifiant", VT::Integer), data("index", "Index", VT::Integer), execOut("completed", "Terminé") };
        d.orderedOutputs = true;
        lib.add(std::move(d), [](ExecutionContext& c) {
            const Value v = c.input("ids");
            if (c.failed()) return false;
            std::vector<int> ids;
            if (const auto* list = std::get_if<std::vector<int>>(&v)) ids = *list;
            else if (const auto* i = std::get_if<long long>(&v)) ids = { static_cast<int>(*i) };
            if (static_cast<int>(ids.size()) > c.maxIterations()) return c.fail("liste trop longue");
            for (std::size_t k = 0; k < ids.size(); ++k)
            {
                c.setOutput("id", static_cast<long long>(ids[k]));
                c.setOutput("index", static_cast<long long>(k));
                if (!c.fire("body")) return false;
            }
            return c.fire("completed");
        });
    }

    // --- Sélection, filtre, comparaison ---------------------------------------------------------------
    lib.add(pureBlock("struct.select_elements", "Sélection d'éléments", kControl,
                      "Identifiants des éléments retenus par un filtre : tous, horizontaux, verticaux, inclinés, ids:1,3, longueur>5, longueur<5.",
                      { object("model", "Modèle", Type::Model), data("filter", "Filtre", VT::Text, std::string("tous")) },
                      { data("ids", "Identifiants", VT::IdList), data("count", "Nombre", VT::Integer) },
                      "Horizontal : |sin α| ≤ 1e-9 ; vertical : |cos α| ≤ 1e-9 ; incliné : ni l'un ni l'autre. Longueurs en m. Refus : filtre inconnu "
                      "ou identifiant absent.",
                      "TSALab/lab/Research/Structural/ControlNodes.cpp"),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const std::string filter = c.text("filter");
                if (!m || c.failed()) return false;
                std::vector<int> ids;
                std::string error;
                if (!selectElements(*m, filter, ids, &error)) return c.fail(error);
                c.setOutput("count", static_cast<long long>(ids.size()));
                c.setOutput("ids", ids);
                return true;
            });

    lib.add(pureBlock("vector.compare", "Comparer deux vecteurs", kControl, "Écart relatif max|x − ref| / max|ref| et conformité à une tolérance.",
                      { object("x", "x", Type::Vector), object("reference", "Référence", Type::Vector), data("tolerance", "Tolérance relative", VT::Real, 1e-9) },
                      { data("deviation", "Écart relatif", VT::Real), data("passed", "Conforme", VT::Bool), data("report", "Rapport", VT::Text) },
                      "Écart absolu si la référence est nulle. Contrôle : même taille (sinon refus). Sert à comparer un algorithme de l'étudiant à "
                      "la solution de référence.",
                      "TSALab/lab/Research/Structural/ControlNodes.cpp"),
            [](ExecutionContext& c) {
                const LabeledVector* x = vectorIn(c, "x");
                const LabeledVector* r = vectorIn(c, "reference");
                const double tol = c.real("tolerance");
                if (!x || !r || c.failed()) return false;
                if (x->vector.size() != r->vector.size())
                    return c.fail("tailles différentes : " + std::to_string(x->vector.size()) + " et " + std::to_string(r->vector.size()));
                double diff = 0.0, scale = 0.0;
                for (std::size_t k = 0; k < x->vector.size(); ++k)
                {
                    diff = std::max(diff, std::abs(x->vector[k] - r->vector[k]));
                    scale = std::max(scale, std::abs(r->vector[k]));
                }
                const double dev = scale > 0.0 ? diff / scale : diff;
                c.setOutput("deviation", dev);
                c.setOutput("passed", dev <= tol);
                c.setOutput("report", "écart relatif " + num(dev, 3) + (dev <= tol ? " ≤ " : " > ") + "tolérance " + num(tol, 3));
                return true;
            });

    lib.add(pureBlock("struct.compare_values", "Comparer deux valeurs", kControl,
                      "Écarts absolu et relatif d'une valeur à une référence, conformité à une tolérance mixte (sans projet ouvert).",
                      { data("value", "Valeur", VT::Real, 0.0), data("reference", "Référence", VT::Real, 0.0), data("rel_tol", "Tolérance relative", VT::Real, 1e-6),
                        data("abs_tol", "Tolérance absolue", VT::Real, 1e-12) },
                      { data("abs_error", "Écart absolu", VT::Real), data("rel_error", "Écart relatif", VT::Real), data("passed", "Conforme", VT::Bool),
                        data("report", "Rapport", VT::Text) },
                      "Conforme si |v − r| ≤ max(tolérance relative × |r|, tolérance absolue). Écart relatif = |v − r| / |r| (−1 si |r| ≤ tolérance "
                      "absolue : non défini). Même règle que la commande compare.values de TSA, utilisable dans un graphe sans projet.",
                      "TSALab/lab/Research/Structural/ControlNodes.cpp"),
            [](ExecutionContext& c) {
                const double v = c.real("value"), r = c.real("reference"), rel = c.real("rel_tol"), abs = c.real("abs_tol");
                if (c.failed()) return false;
                if (rel < 0.0 || abs < 0.0) return c.fail("tolérances négatives");
                const double e = std::abs(v - r);
                const bool ok = e <= std::max(rel * std::abs(r), abs);
                const double relErr = std::abs(r) > abs ? e / std::abs(r) : -1.0;
                c.setOutput("abs_error", e);
                c.setOutput("rel_error", relErr);
                c.setOutput("passed", ok);
                c.setOutput("report", "valeur " + num(v, 10) + ", référence " + num(r, 10) + ", écart relatif " + (relErr >= 0.0 ? num(relErr, 3) : std::string("non défini"))
                                          + (ok ? " : conforme" : " : NON conforme"));
                return true;
            });

    // --- Courbes, visualisation, export ------------------------------------------------------------------
    lib.add(pureBlock("plot.curve", "Courbe (x, y)", kControl, "Courbe y(x) à partir de deux vecteurs de même taille (tracé : double-clic sur la valeur).",
                      { object("x", "x", Type::Vector), object("y", "y", Type::Vector), data("title", "Titre", VT::Text, std::string()),
                        data("x_label", "Axe x", VT::Text, std::string("x")), data("y_label", "Axe y", VT::Text, std::string("y")) },
                      { object("curve", "Courbe", Type::Curve) }, "Contrôle : même taille, au moins deux points.", "TSALab/lab/Research/Structural/ControlNodes.cpp"),
            [](ExecutionContext& c) {
                const LabeledVector* x = vectorIn(c, "x");
                const LabeledVector* y = vectorIn(c, "y");
                CurveData d { {}, {}, c.text("title"), c.text("x_label"), c.text("y_label") };
                if (!x || !y || c.failed()) return false;
                if (x->vector.size() != y->vector.size() || x->vector.size() < 2) return c.fail("x et y doivent avoir la même taille (au moins 2 points)");
                d.x = x->vector, d.y = y->vector;
                c.setOutput("curve", make<CurveObject>(std::move(d)));
                return true;
            });

    lib.add(pureBlock("struct.diagram_curve", "Courbe d'un diagramme", kControl, "Courbe N(x), V(x), M(x), u(x) ou v(x) d'un élément.",
                      { object("diagram", "Diagrammes", Type::Diagram), data("quantity", "Grandeur (N, V, M, u, v)", VT::Text, std::string("M")) },
                      { object("curve", "Courbe", Type::Curve), object("x", "x", Type::Vector), object("y", "valeurs", Type::Vector) },
                      "Valeurs aux points du diagramme (deux points de même x aux discontinuités). Convention RDM : N > 0 en traction, M > 0 tend la "
                      "fibre y' < 0.",
                      "TSALab/lab/Research/Structural/ControlNodes.cpp"),
            [](ExecutionContext& c) {
                const auto g = c.objectAs<DiagramObject>("diagram");
                const std::string q = c.text("quantity");
                if (!g || c.failed()) return false;
                Vector xs, ys;
                const char* unit = "";
                for (const auto& p : g->value().diagram.points)
                {
                    xs.push_back(p.x);
                    if (q == "N") ys.push_back(p.N), unit = "N (kN)";
                    else if (q == "V") ys.push_back(p.V), unit = "V (kN)";
                    else if (q == "M") ys.push_back(p.M), unit = "M (kN·m)";
                    else if (q == "u") ys.push_back(p.u), unit = "u (m)";
                    else if (q == "v") ys.push_back(p.v), unit = "v (m)";
                    else return c.fail("grandeur inconnue « " + q + " » (N, V, M, u, v)");
                }
                CurveData d { xs, ys, q + " — élément " + std::to_string(g->value().diagram.element), "x (m)", unit };
                c.setOutput("x", vectorOut(xs, {}, "x"));
                c.setOutput("y", vectorOut(ys, {}, q));
                c.setOutput("curve", make<CurveObject>(std::move(d)));
                return true;
            });

    lib.add(actionBlock("struct.show", "Visualiser", kControl,
                        "Affiche un objet dans la fenêtre de TSALab : portique et déformée d'un résultat, tableau d'une matrice, tracé d'une courbe ou d'un "
                        "diagramme ; écrit son résumé dans le journal.",
                        { any("value", "Objet"), data("title", "Titre", VT::Text, std::string()),
                          data("scale", "Amplification de la déformée (0 : auto)", VT::Real, 0.0) },
                        {},
                        "Résultat (SolverResult) : modèle initial, déformée amplifiée (facteur affiché et réglable), déplacements nodaux, réactions, "
                        "diagrammes N, V, M, bilan d'équilibre. Le bloc dépose l'objet dans le tableau des résultats (lu par la fenêtre) ; sans "
                        "fenêtre (tests, console), seul le journal est écrit.",
                        "TSALab/lab/Research/Structural/ResultBoard.cpp"),
            [](ExecutionContext& c) {
                const Value v = c.input("value");
                const std::string title = c.text("title");
                const double scale = c.real("scale");
                if (c.failed()) return false;
                const auto* o = std::get_if<ObjectRef>(&v);
                if (!o || !*o) return c.fail("objet attendu (résultat, matrice, vecteur, diagramme, courbe)");
                if (scale < 0.0) return c.fail("amplification négative");
                c.log((title.empty() ? std::string() : title + " : ") + (*o)->summary());
                ResultBoard::instance().publish(*o, QString::fromStdString(title.empty() ? (*o)->typeName() : title), scale);
                return c.fire("then");
            });

    lib.add(actionBlock("struct.export_results", "Exporter les résultats (CSV)", kControl,
                        "Écrit trois fichiers CSV : déplacements nodaux, réactions, efforts d'extrémité et extremums par élément.",
                        { object("result", "Résultat", Type::Result), data("prefix", "Préfixe des fichiers", VT::Text, std::string("resultats")) }, {},
                        "Fichiers <préfixe>_deplacements.csv, <préfixe>_reactions.csv, <préfixe>_elements.csv ; séparateur « ; », 17 chiffres "
                        "significatifs ; unités dans les en-têtes. Refus : résultat en échec.",
                        "TSALab/lab/Research/Structural/ControlNodes.cpp"),
            [](ExecutionContext& c) {
                const S::AnalysisResult2D* r = resultIn(c);
                const std::string prefix = c.text("prefix");
                if (!r || c.failed()) return false;
                if (!r->success) return c.fail("résultat en échec : " + r->message);
                if (prefix.empty()) return c.fail("préfixe vide");
                std::ostringstream d, re, el;
                d << "noeud;ux (m);uy (m);thetaz (rad)\n";
                for (std::size_t p = 0; p < r->dofs.nodeIds.size(); ++p)
                    d << r->dofs.nodeIds[p] << ";" << csv(r->d[3 * p]) << ";" << csv(r->d[3 * p + 1]) << ";" << csv(r->d[3 * p + 2]) << "\n";
                re << "noeud;Rx (kN);Ry (kN);Mz (kN.m)\n";
                for (const auto& s : r->model.supports)
                {
                    const auto R = r->nodeReaction(s.node);
                    re << s.node << ";" << csv(R[0]) << ";" << csv(R[1]) << ";" << csv(R[2]) << "\n";
                }
                el << "element;Fx_i (kN);Fy_i (kN);Mz_i (kN.m);Fx_j (kN);Fy_j (kN);Mz_j (kN.m);N min;N max;V min;V max;M min;M max;fleche (m)\n";
                for (const auto& e : r->elements)
                {
                    el << e.element;
                    for (double v : e.endForces) el << ";" << csv(v);
                    const auto& g = e.diagram;
                    for (double v : { g.Nmin, g.Nmax, g.Vmin, g.Vmax, g.Mmin, g.Mmax, g.maxDeflection }) el << ";" << csv(v);
                    el << "\n";
                }
                const QString base = QString::fromStdString(prefix);
                return writeFile(c, base + QStringLiteral("_deplacements.csv"), d.str()) && writeFile(c, base + QStringLiteral("_reactions.csv"), re.str())
                       && writeFile(c, base + QStringLiteral("_elements.csv"), el.str()) && c.fire("then");
            });

    // --- Persistance JSON du modèle et des résultats (StructuralJson.h) ----------------------------------------------
    constexpr char kSrcJson[] = "TSALab/lab/Research/Structural/StructuralJson.cpp";
    const std::string jsonFormat = "Format versionné (version 1), unités du cœur scientifique rappelées dans le fichier, réels écrits au plus court "
                                   "qui se relit exactement (NaN et ±∞ en texte) : relu, le contenu est identique à l'enregistré. Lecture stricte : "
                                   "autre format, version future, champ manquant ou de mauvais type refusés avec le chemin du champ ; aucune "
                                   "valeur par défaut substituée. Écriture d'un bloc : un échec ne laisse pas de fichier tronqué.";
    lib.add(actionBlock("struct.model_save_json", "Enregistrer le modèle (JSON)", kModel,
                        "Écrit le modèle de portique (nœuds, éléments, appuis, ressorts, rotules, cas, combinaisons, masses, traçabilité) en JSON.",
                        { object("model", "Modèle", Type::Model), data("path", "Fichier .json", VT::Text, std::string()) }, {},
                        "Format « tsalab-model2d ». " + jsonFormat, kSrcJson),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const std::string path = c.text("path");
                if (!m || c.failed()) return false;
                if (path.empty()) return c.fail("fichier non renseigné");
                std::string error;
                if (!saveModelJson(*m, path, &error)) return c.fail(error);
                c.log("Modèle enregistré : " + path);
                return c.fire("then");
            });
    {
        NodeDefinition def = pureBlock("struct.model_load_json", "Lire un modèle (JSON)", kModel, "Relit un modèle de portique enregistré en JSON.",
                                       { data("path", "Fichier .json", VT::Text, std::string()) }, { object("model", "Modèle", Type::Model) },
                                       "Format « tsalab-model2d ». " + jsonFormat + " Le fichier est relu à chaque exécution.", kSrcJson);
        def.cacheable = false;   // le fichier peut changer entre deux exécutions
        lib.add(def, [](ExecutionContext& c) {
            const std::string path = c.text("path");
            if (c.failed()) return false;
            S::Model2D m;
            std::string error;
            if (!loadModelJson(path, m, &error)) return c.fail(error);
            c.setOutput("model", make<ModelObject>(std::move(m)));
            return true;
        });
    }
    lib.add(actionBlock("struct.result_save_json", "Enregistrer le résultat (JSON)", kControl,
                        "Archive un résultat complet en JSON : modèle, chargement, DDL, K, F, système réduit, résolution, d, R, ressorts, "
                        "efforts et diagrammes, équilibre.",
                        { object("result", "Résultat", Type::Result), data("path", "Fichier .json", VT::Text, std::string()) }, {},
                        "Format « tsalab-result2d ». " + jsonFormat + " Un résultat en échec s'enregistre aussi (diagnostic du mécanisme).", kSrcJson),
            [](ExecutionContext& c) {
                const S::AnalysisResult2D* r = resultIn(c);
                const std::string path = c.text("path");
                if (!r || c.failed()) return false;
                if (path.empty()) return c.fail("fichier non renseigné");
                std::string error;
                if (!saveResultJson(*r, path, &error)) return c.fail(error);
                c.log("Résultat enregistré : " + path);
                return c.fire("then");
            });
    {
        NodeDefinition def = pureBlock("struct.result_load_json", "Lire un résultat (JSON)", kControl,
                                       "Relit un résultat archivé en JSON (visualisation, comparaison à un nouveau calcul).",
                                       { data("path", "Fichier .json", VT::Text, std::string()) }, { object("result", "Résultat", Type::Result) },
                                       "Format « tsalab-result2d ». " + jsonFormat + " Le fichier est relu à chaque exécution.", kSrcJson);
        def.cacheable = false;
        lib.add(def, [](ExecutionContext& c) {
            const std::string path = c.text("path");
            if (c.failed()) return false;
            S::AnalysisResult2D r;
            std::string error;
            if (!loadResultJson(path, r, &error)) return c.fail(error);
            c.setOutput("result", make<ResultObject>(std::move(r)));
            return true;
        });
    }

    // --- Laboratoire de l'étudiant (composants expérimentaux, séparés des composants validés) ---------------
    {
        NodeDefinition d = pureBlock("exp.jacobi_sweep", "Itération de Jacobi", kExperimental,
                                     "Une itération de Jacobi pour K·x = f : x_suivant = D⁻¹·(f − (K − D)·x). Brique d'un solveur itératif écrit par l'étudiant.",
                                     { object("K", "K", Type::Matrix), object("f", "f", Type::Vector), object("x", "x", Type::Vector) },
                                     { object("x_next", "x suivant", Type::Vector), data("residual", "Résidu relatif de x suivant", VT::Real) },
                                     "x_i(k+1) = (f_i − Σ_{j≠i} K_ij·x_j(k)) / K_ii : toutes les composantes calculées avec l'itéré précédent. Converge si K est "
                                     "à diagonale strictement dominante (souvent lentement pour une rigidité de portique) ; à boucler avec « Tant que » jusqu'à "
                                     "un résidu ‖K·x − f‖/‖f‖ assez petit, puis à comparer à « Résoudre K·x = f » (« Comparer deux vecteurs »).\n"
                                     "Composant expérimental : laboratoire de l'étudiant, à valider contre la solution de référence avant tout usage.",
                                     "TSALab/lab/Research/Structural/ControlNodes.cpp");
        d.status = NodeStatus::Experimental;
        lib.add(std::move(d), [](ExecutionContext& c) {
            const LabeledMatrix* K = nullptr;
            const LabeledVector *f = nullptr, *x = nullptr;
            if (!iterationInputs(c, K, f, x)) return false;
            const int n = K->matrix.rows;
            Vector next(static_cast<std::size_t>(n));
            for (int i = 0; i < n; ++i)
            {
                double s = f->vector[static_cast<std::size_t>(i)];
                for (int j = 0; j < n; ++j)
                    if (j != i) s -= K->matrix(i, j) * x->vector[static_cast<std::size_t>(j)];
                next[static_cast<std::size_t>(i)] = s / K->matrix(i, i);
            }
            c.setOutput("residual", relativeResidual(K->matrix, next, f->vector));
            c.setOutput("x_next", vectorOut(std::move(next), x->labels, "x (Jacobi)"));
            return true;
        });
    }
    {
        NodeDefinition d = pureBlock("exp.gauss_seidel_sweep", "Itération de Gauss-Seidel", kExperimental,
                                     "Une itération de Gauss-Seidel pour K·x = f : chaque composante utilise les composantes précédentes "
                                     "déjà mises à jour. Brique d'un solveur itératif écrit par l'étudiant.",
                                     { object("K", "K", Type::Matrix), object("f", "f", Type::Vector), object("x", "x", Type::Vector) },
                                     { object("x_next", "x suivant", Type::Vector), data("residual", "Résidu relatif de x suivant", VT::Real) },
                                     "x_i(k+1) = (f_i − Σ_{j<i} K_ij·x_j(k+1) − Σ_{j>i} K_ij·x_j(k)) / K_ii : contrairement à Jacobi, les "
                                     "composantes déjà calculées dans l'itération sont utilisées aussitôt (mise à jour sur place). Converge pour K "
                                     "symétrique définie positive (rigidité d'une structure stable), en général plus vite que Jacobi ; la vitesse "
                                     "dépend du conditionnement. À boucler avec « Tant que » jusqu'à un résidu ‖K·x − f‖/‖f‖ assez petit, puis à "
                                     "comparer à « Résoudre K·x = f ».\nÉtat : implémenté (fonction gaussSeidelSweep), convergence vérifiée par le "
                                     "test L19 et par l'exemple « Solveur de l'étudiant (Gauss-Seidel) ». Composant du laboratoire de l'étudiant : "
                                     "non utilisé par les blocs validés, convergence non garantie pour une matrice quelconque.",
                                     "TSALab/lab/Research/Structural/ControlNodes.cpp");
        d.status = NodeStatus::Experimental;
        lib.add(std::move(d), [](ExecutionContext& c) {
            const LabeledMatrix* K = nullptr;
            const LabeledVector *f = nullptr, *x = nullptr;
            if (!iterationInputs(c, K, f, x)) return false;
            Vector next = x->vector;
            if (!gaussSeidelSweep(K->matrix, f->vector, next)) return c.fail("itération de Gauss-Seidel impossible");
            c.setOutput("residual", relativeResidual(K->matrix, next, f->vector));
            c.setOutput("x_next", vectorOut(std::move(next), x->labels, "x (Gauss-Seidel)"));
            return true;
        });
    }
}

} // namespace TSALab::Research::Structural::detail
