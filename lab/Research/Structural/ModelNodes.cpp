// Catégorie A — modèle structurel : nœuds, matériaux, sections, appuis, éléments FrameElement2D, modèle, charges,
// cas de charge, combinaisons, chargement à calculer.

#include "NodeHelpers.h"
#include "StructuralModels.h"

#include <cmath>

namespace TSALab::Research::Structural::detail
{

namespace
{
const char* const kUnits = "Unités : kN, m, kN/m² (E), m², m⁴, °C, rad. Repère global (X, Y), rotations positives dans le sens trigonométrique.";

/// Copie du modèle d'entrée, modifiée puis rendue (les objets sont immuables).
bool withModel(ExecutionContext& c, S::Model2D& out)
{
    const S::Model2D* m = modelIn(c);
    if (!m) return false;
    out = *m;
    return true;
}

/// Ajoute une charge (groupe Load) issue d'un bloc de charge.
bool emitLoad(ExecutionContext& c, S::LoadCase loads)
{
    if (c.failed()) return false;
    c.setOutput("load", make<LoadObject>(std::move(loads)));
    return true;
}

bool direction(ExecutionContext& c, S::LoadDirection& d)
{
    const std::string t = c.text("direction");
    if (c.failed()) return false;
    if (!parseDirection(t, d)) return c.fail("direction inconnue « " + t + " » (y local, x local, X global, Y global)");
    return true;
}

const std::string kLoadDoc = std::string(
    "Charges d'élément : positions mesurées depuis le nœud i le long de l'élément, en mètres (ou en fraction de L si « relative »).\n"
    "Directions : x local (axe de l'élément, de i vers j), y local (perpendiculaire, x' tourné de +90°), X global, Y global.\n"
    "Une charge globale est décomposée dans le repère local : (px, py) = (c·qX + s·qY, −s·qX + c·qY), c = cos α, s = sin α.\n"
    "« Projetée » (charge répartie globale uniquement) : intensité donnée par mètre de projection perpendiculaire à la\n"
    "direction (neige par mètre horizontal) : q_élément = q·|c| pour Y global, q·|s| pour X global.\n"
    "Conversion en charges nodales équivalentes : efforts d'encastrement parfait f0' = −∫ Nᵀ·p dx avec les fonctions de forme\n"
    "exactes de la barre prismatique (Hermite en flexion, linéaires en axial) : valeurs exactes des tables pour toute charge\n"
    "polynomiale par morceaux ; charges nodales équivalentes −f0' (local), Tᵀ·(−f0') (global).\n"
    "Limites : élément prismatique d'Euler-Bernoulli (sans déformation d'effort tranchant), élastique linéaire, petits\n"
    "déplacements ; une charge hors de l'élément, de longueur nulle ou mal définie est refusée avec la raison.\n")
    + kUnits;
} // namespace

void registerModelNodes(NodeLibrary& lib)
{
    // --- Nœud, matériau, section, appui -----------------------------------------------------------
    lib.add(pureBlock("struct.node", "Node2D", kModel, "Nœud du portique plan : identifiant et coordonnées (m).",
                      { data("id", "Identifiant", VT::Integer, 1LL), data("x", "X (m)", VT::Real, 0.0), data("y", "Y (m)", VT::Real, 0.0),
                        data("name", "Nom", VT::Text, std::string()) },
                      { object("node", "Nœud", Type::Node), data("id", "Identifiant", VT::Integer) },
                      std::string("Nœud à 3 DDL : ux, uy (m) et θz (rad), repère global.\n") + kUnits, kSrcFrame2D),
            [](ExecutionContext& c) {
                const long long id = c.integer("id");
                S::Node2D n { static_cast<int>(id), c.real("x"), c.real("y"), c.text("name") };
                if (c.failed()) return false;
                if (id <= 0) return c.fail("l'identifiant d'un nœud doit être positif");
                if (!std::isfinite(n.x) || !std::isfinite(n.y)) return c.fail("coordonnées non finies");
                c.setOutput("node", make<NodeObject>(n));
                c.setOutput("id", id);
                return true;
            });

    lib.add(pureBlock("struct.material", "Material", kModel, "Matériau élastique linéaire : module d'Young, dilatation thermique, poids volumique.",
                      { data("name", "Nom", VT::Text, std::string("Acier")), data("E", "E (kN/m²)", VT::Real, 2.1e8),
                        data("alpha", "α (1/°C)", VT::Real, 1.2e-5), data("gamma", "γ (kN/m³)", VT::Real, 78.5) },
                      { object("material", "Matériau", Type::Material) },
                      std::string("E en kN/m² (210 GPa = 2,1e8 kN/m²) ; α sert aux actions thermiques ; γ au poids propre (γ·A, kN/m).\n"
                                  "Contrôle : E > 0 (vérifié à la création de l'élément).\n")
                          + kUnits,
                      kSrcFrame2D),
            [](ExecutionContext& c) {
                S::Material m { c.text("name"), c.real("E"), c.real("alpha"), c.real("gamma") };
                if (c.failed()) return false;
                if (!(m.E > 0.0)) return c.fail("module d'Young E non positif");
                if (m.alpha < 0.0 || m.unitWeight < 0.0) return c.fail("α et γ doivent être positifs ou nuls");
                c.setOutput("material", make<MaterialObject>(m));
                return true;
            });

    lib.add(pureBlock("struct.material_catalog", "Matériau du catalogue", kModel,
                      "Matériau du catalogue de TSA (S235, S355, C25/30, C30/37, C24) converti en kN/m², 1/°C et kN/m³.",
                      { data("name", "Matériau", VT::Text, std::string("S235")) },
                      { object("material", "Matériau", Type::Material), data("E", "E (kN/m²)", VT::Real), data("alpha", "α (1/°C)", VT::Real),
                        data("gamma", "γ (kN/m³)", VT::Real) },
                      "Valeurs du catalogue de TSA (Model/Material) : E (Pa → kN/m²), α, γ = ρ·g avec g = 9,81 m/s² (valeur du poids propre "
                      "des calculs de TSA). Un nom inconnu est refusé.",
                      "TSALab/lab/Research/Structural/StructuralModels.cpp"),
            [](ExecutionContext& c) {
                S::Material m;
                std::string error;
                const std::string name = c.text("name");
                if (c.failed()) return false;
                if (!catalogMaterial(name, m, &error)) return c.fail(error);
                c.setOutput("material", make<MaterialObject>(m));
                c.setOutput("E", m.E);
                c.setOutput("alpha", m.alpha);
                c.setOutput("gamma", m.unitWeight);
                return true;
            });

    lib.add(pureBlock("struct.section", "Section2D", kModel, "Section : aire, moment quadratique de flexion dans le plan, hauteur.",
                      { data("name", "Nom", VT::Text, std::string()), data("A", "A (m²)", VT::Real, 53.8e-4), data("Iz", "Iz (m⁴)", VT::Real, 8356e-8),
                        data("h", "h (m)", VT::Real, 0.30) },
                      { object("section", "Section", Type::Section) },
                      std::string("A en m² (53,8 cm² = 53,8e-4 m²), Iz en m⁴ (8 356 cm⁴ = 8,356e-5 m⁴), h en m (gradient thermique : κ0 = α·ΔT/h).\n"
                                  "Contrôle : A > 0, Iz > 0 (vérifié à la création de l'élément).\n")
                          + kUnits,
                      kSrcFrame2D),
            [](ExecutionContext& c) {
                S::Section2D s { c.text("name"), c.real("A"), c.real("Iz"), c.real("h") };
                if (c.failed()) return false;
                if (!(s.A > 0.0) || !(s.Iz > 0.0)) return c.fail("A et Iz doivent être positifs");
                if (s.h < 0.0) return c.fail("hauteur négative");
                c.setOutput("section", make<SectionObject>(s));
                return true;
            });

    lib.add(pureBlock("struct.section_catalog", "Section du catalogue", kModel,
                      "Profil du catalogue de TSA (IPE, HEA, HEB, UPN, RECT bxh, CIRC d) : A, Iz (axe fort), h.",
                      { data("profile", "Profil", VT::Text, std::string("IPE 300")) },
                      { object("section", "Section", Type::Section), data("A", "A (m²)", VT::Real), data("Iz", "Iz (m⁴)", VT::Real), data("h", "h (m)", VT::Real) },
                      "Dimensions EN 10365 du catalogue de TSA (Model/Section) ; Iz = inertie de flexion autour de l'axe fort (profil dans le "
                      "plan du portique). Une taille absente du catalogue est refusée avec la liste des tailles (jamais remplacée).",
                      "TSALab/lab/Research/Structural/StructuralModels.cpp"),
            [](ExecutionContext& c) {
                S::Section2D s;
                std::string error;
                const std::string profile = c.text("profile");
                if (c.failed()) return false;
                if (!catalogSection(profile, s, &error)) return c.fail(error);
                c.setOutput("section", make<SectionObject>(s));
                c.setOutput("A", s.A);
                c.setOutput("Iz", s.Iz);
                c.setOutput("h", s.h);
                return true;
            });

    lib.add(pureBlock("struct.support", "SupportCondition", kModel, "Appui d'un nœud : DDL bloqués (repère global).",
                      { data("node", "Nœud", VT::Integer, 1LL), data("ux", "ux bloqué", VT::Bool, true), data("uy", "uy bloqué", VT::Bool, true),
                        data("rz", "θz bloqué", VT::Bool, false) },
                      { object("support", "Appui", Type::Support) },
                      "Encastrement : ux, uy, θz ; articulation : ux, uy ; appui simple vertical : uy. Un DDL bloqué a un déplacement nul, ou "
                      "imposé par le chargement (bloc « Déplacement imposé ») : il passe dans les DDL imposés c de la partition.",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const long long node = c.integer("node");
                S::SupportCondition s { static_cast<int>(node), c.boolean("ux"), c.boolean("uy"), c.boolean("rz") };
                if (c.failed()) return false;
                if (node <= 0) return c.fail("nœud d'appui invalide");
                c.setOutput("support", make<SupportObject>(s));
                return true;
            });

    // --- Classe FrameElement2D -------------------------------------------------------------------
    lib.add(pureBlock("struct.frame_element", "FrameElement2D", kModel,
                      "Classe d'élément filaire 2D à 6 DDL (portique plan) : créée depuis deux nœuds, un matériau et une section ; rend ses "
                      "matrices.",
                      { data("id", "Identifiant", VT::Integer, 1LL), object("node_i", "Nœud i", Type::Node), object("node_j", "Nœud j", Type::Node),
                        object("material", "Matériau", Type::Material), object("section", "Section", Type::Section), data("name", "Nom", VT::Text, std::string()) },
                      { object("element", "Élément", Type::Element), data("length", "Longueur (m)", VT::Real), data("angle", "Angle (°)", VT::Real),
                        object("k_local", "k' local", Type::Matrix), object("T", "T", Type::Matrix), object("k_global", "k global", Type::Matrix) },
                      "FrameElement2D (Euler-Bernoulli, élastique linéaire, section constante), DDL [ux_i, uy_i, θz_i, ux_j, uy_j, θz_j].\n"
                      "Contrôles à la création : longueur non nulle, E, A, Iz positifs et finis (sinon le bloc échoue avec la raison).\n"
                      "Longueur L = √(Δx² + Δy²) ; angle α = atan2(Δy, Δx) de X vers x'.\n"
                      "k' : EA/L (axial), 12EI/L³, 6EI/L², 4EI/L, 2EI/L (flexion).\n"
                      "T : d' = T·d avec, par nœud, [c s 0 ; −s c 0 ; 0 0 1].\n"
                      "k = Tᵀ·k'·T (repère global).\n"
                      "Fonctions de la classe exposées par les blocs de la catégorie B : géométrie, rigidités, charges équivalentes, "
                      "déplacements et efforts d'extrémité, diagrammes N, V, M et déformée.",
                      kSrcElement),
            [](ExecutionContext& c) {
                const long long id = c.integer("id");
                const auto ni = c.objectAs<NodeObject>("node_i");
                const auto nj = c.objectAs<NodeObject>("node_j");
                const auto mat = c.objectAs<MaterialObject>("material");
                const auto sec = c.objectAs<SectionObject>("section");
                const std::string name = c.text("name");
                if (!ni || !nj || !mat || !sec || c.failed()) return false;
                if (id <= 0) return c.fail("l'identifiant d'un élément doit être positif");
                const S::FrameElement2D e(static_cast<int>(id), ni->value(), nj->value(), mat->value(), sec->value(), name);
                const auto issues = e.validate();
                if (!issues.empty()) return c.fail(issues.front());
                const auto labels = elementDofLabels(e);
                const std::vector<std::string> local { "u'i", "v'i", "θi", "u'j", "v'j", "θj" };
                c.setOutput("element", make<ElementObject>(e));
                c.setOutput("length", e.length());
                c.setOutput("angle", e.angle() * 180.0 / std::acos(-1.0));
                c.setOutput("k_local", matrixOut(e.localStiffness(), local, local, "k' (élément " + std::to_string(id) + ")"));
                c.setOutput("T", matrixOut(e.transformation(), local, labels, "T (élément " + std::to_string(id) + ")"));
                c.setOutput("k_global", matrixOut(e.globalStiffness(), labels, labels, "k = TᵀK'T (élément " + std::to_string(id) + ")"));
                return true;
            });

    // --- Modèle -----------------------------------------------------------------------------------
    lib.add(pureBlock("struct.model_empty", "StructuralModel (vide)", kModel, "Modèle vide, à compléter par les blocs « Ajouter … ».",
                      { data("name", "Nom", VT::Text, std::string("Modèle")) }, { object("model", "Modèle", Type::Model) },
                      "Point de départ de la construction d'un modèle par blocs : chaque bloc « Ajouter » rend un nouveau modèle (les "
                      "objets sont immuables).",
                      kSrcFrame2D),
            [](ExecutionContext& c) {
                S::Model2D m;
                m.name = c.text("name");
                if (c.failed()) return false;
                c.setOutput("model", make<ModelObject>(std::move(m)));
                return true;
            });

    lib.add(pureBlock("struct.model_add_node", "Ajouter un nœud", kModel, "Ajoute un nœud au modèle (identifiant unique).",
                      { object("model", "Modèle", Type::Model), object("node", "Nœud", Type::Node) }, { object("model", "Modèle", Type::Model) },
                      "Refus : identifiant déjà utilisé par un autre nœud (coordonnées différentes). Un nœud identique déjà présent est accepté sans doublon.",
                      kSrcFrame2D),
            [](ExecutionContext& c) {
                S::Model2D m;
                const auto n = c.objectAs<NodeObject>("node");
                if (!withModel(c, m) || !n) return false;
                if (const S::Node2D* existing = m.node(n->value().id))
                {
                    if (!(*existing == n->value()))
                        return c.fail("nœud " + std::to_string(n->value().id) + " déjà présent avec d'autres coordonnées");
                }
                else
                    m.nodes.push_back(n->value());
                c.setOutput("model", make<ModelObject>(std::move(m)));
                return true;
            });

    lib.add(pureBlock("struct.model_add_element", "Ajouter un élément", kModel,
                      "Ajoute un élément FrameElement2D au modèle ; ses nœuds d'extrémité sont ajoutés s'ils manquent.",
                      { object("model", "Modèle", Type::Model), object("element", "Élément", Type::Element) }, { object("model", "Modèle", Type::Model) },
                      "Refus : identifiant d'élément déjà utilisé ; nœud d'extrémité présent dans le modèle avec d'autres coordonnées.",
                      kSrcFrame2D),
            [](ExecutionContext& c) {
                S::Model2D m;
                const auto e = c.objectAs<ElementObject>("element");
                if (!withModel(c, m) || !e) return false;
                const S::FrameElement2D& el = e->value();
                if (m.element(el.id())) return c.fail("élément " + std::to_string(el.id()) + " déjà présent dans le modèle");
                for (const S::Node2D* n : { &el.nodeI(), &el.nodeJ() })
                {
                    if (const S::Node2D* existing = m.node(n->id))
                    {
                        if (existing->x != n->x || existing->y != n->y)
                            return c.fail("nœud " + std::to_string(n->id) + " de l'élément " + std::to_string(el.id()) + " : coordonnées différentes de celles du modèle");
                    }
                    else
                        m.nodes.push_back(*n);
                }
                m.elements.push_back({ el.id(), el.nodeI().id, el.nodeJ().id, el.material(), el.section(), el.name() });
                c.setOutput("model", make<ModelObject>(std::move(m)));
                return true;
            });

    lib.add(pureBlock("struct.model_add_support", "Ajouter un appui", kModel, "Ajoute un appui (un seul par nœud).",
                      { object("model", "Modèle", Type::Model), object("support", "Appui", Type::Support) }, { object("model", "Modèle", Type::Model) },
                      "Refus : nœud inexistant ou déjà muni d'un appui.", kSrcFrame2D),
            [](ExecutionContext& c) {
                S::Model2D m;
                const auto s = c.objectAs<SupportObject>("support");
                if (!withModel(c, m) || !s) return false;
                if (!m.node(s->value().node)) return c.fail("appui sur le nœud " + std::to_string(s->value().node) + " absent du modèle");
                if (m.support(s->value().node)) return c.fail("le nœud " + std::to_string(s->value().node) + " a déjà un appui");
                m.supports.push_back(s->value());
                c.setOutput("model", make<ModelObject>(std::move(m)));
                return true;
            });

    lib.add(pureBlock("struct.model_add_case", "Ajouter un cas de charge", kModel, "Ajoute un cas de charge (identifiant unique).",
                      { object("model", "Modèle", Type::Model), object("case", "Cas", Type::LoadCase) }, { object("model", "Modèle", Type::Model) },
                      "Refus : identifiant de cas déjà utilisé. Les références (nœuds, éléments) sont contrôlées par « Valider le modèle » et au calcul.",
                      kSrcFrame2D),
            [](ExecutionContext& c) {
                S::Model2D m;
                const auto k = c.objectAs<LoadCaseObject>("case");
                if (!withModel(c, m) || !k) return false;
                if (m.loadCase(k->value().id)) return c.fail("cas " + std::to_string(k->value().id) + " déjà présent");
                m.loadCases.push_back(k->value());
                c.setOutput("model", make<ModelObject>(std::move(m)));
                return true;
            });

    lib.add(pureBlock("struct.model_add_combination", "Ajouter une combinaison", kModel, "Ajoute une combinaison de cas.",
                      { object("model", "Modèle", Type::Model), object("combination", "Combinaison", Type::Combination) },
                      { object("model", "Modèle", Type::Model) }, "Refus : identifiant déjà utilisé ou cas absent du modèle.", kSrcFrame2D),
            [](ExecutionContext& c) {
                S::Model2D m;
                const auto k = c.objectAs<CombinationObject>("combination");
                if (!withModel(c, m) || !k) return false;
                if (m.combination(k->value().id)) return c.fail("combinaison " + std::to_string(k->value().id) + " déjà présente");
                for (const auto& [cid, f] : k->value().factors)
                    if (!m.loadCase(cid)) return c.fail("combinaison : cas " + std::to_string(cid) + " absent du modèle");
                m.combinations.push_back(k->value());
                c.setOutput("model", make<ModelObject>(std::move(m)));
                return true;
            });

    lib.add(pureBlock("struct.model_validate", "Valider le modèle", kModel,
                      "Contrôle des données avant le calcul : erreurs bloquantes et avertissements (le bloc n'échoue pas).",
                      { object("model", "Modèle", Type::Model) },
                      { data("valid", "Valide", VT::Bool), data("errors", "Erreurs", VT::Integer), data("warnings", "Avertissements", VT::Integer),
                        data("report", "Rapport", VT::Text) },
                      "Identifiants uniques ; références existantes ; longueurs non nulles ; E, A, Iz positifs et finis ; positions des charges "
                      "dans l'élément ; données thermiques et de poids propre présentes ; déplacements imposés sur des DDL bloqués ; "
                      "combinaisons formées de cas existants. Avertissements : nœud relié à aucun élément, appui sans DDL bloqué.",
                      kSrcFrame2D),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                if (!m) return false;
                const auto r = S::validate(*m);
                c.setOutput("valid", r.ok());
                c.setOutput("errors", static_cast<long long>(r.errors.size()));
                c.setOutput("warnings", static_cast<long long>(r.warnings.size()));
                c.setOutput("report", r.text());
                return true;
            });

    lib.add(pureBlock("struct.model_info", "Informations du modèle", kModel, "Nombres de nœuds, d'éléments, de cas et de DDL ; résumé.",
                      { object("model", "Modèle", Type::Model) },
                      { data("nodes", "Nœuds", VT::Integer), data("elements", "Éléments", VT::Integer), data("cases", "Cas", VT::Integer),
                        data("dofs", "DDL", VT::Integer), data("summary", "Résumé", VT::Text) },
                      "DDL = 3 × nombre de nœuds (ux, uy, θz).", kSrcFrame2D),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                if (!m) return false;
                c.setOutput("nodes", static_cast<long long>(m->nodes.size()));
                c.setOutput("elements", static_cast<long long>(m->elements.size()));
                c.setOutput("cases", static_cast<long long>(m->loadCases.size()));
                c.setOutput("dofs", static_cast<long long>(3 * m->nodes.size()));
                c.setOutput("summary", summary(*m));
                return true;
            });

    lib.add(pureBlock("struct.model_demo_portal", "Portique de démonstration", kModel,
                      "Portique encastré (deux poteaux, une traverse) avec cas G, W, tassement T et combinaison 1,35 G + 1,5 W.",
                      { data("span", "Portée (m)", VT::Real, 6.0), data("height", "Hauteur (m)", VT::Real, 4.0),
                        data("q", "q traverse (kN/m)", VT::Real, -15.0), data("H", "H en tête (kN)", VT::Real, 20.0),
                        data("settlement", "Tassement (m)", VT::Real, 0.01), data("column", "Poteaux", VT::Text, std::string("HEB 200")),
                        data("beam", "Traverse", VT::Text, std::string("IPE 300")), data("material", "Matériau", VT::Text, std::string("S235")) },
                      { object("model", "Modèle", Type::Model) },
                      "Nœuds N1 (0 ; 0), N2 (0 ; h), N3 (L ; h), N4 (L ; 0) ; éléments 1 (N1 → N2), 2 (N2 → N3), 3 (N4 → N3) ; pieds encastrés.\n"
                      "Cas 1 « G » : q répartie sur la traverse (y local) + poids propre de tous les éléments (γ·A).\n"
                      "Cas 2 « W » : force H en N2 (X global) + charge répartie 0,2·H/h kN/m sur le poteau gauche (X global) — valeurs "
                      "de démonstration, non issues d'un calcul de vent.\n"
                      "Cas 3 « T » : tassement du pied droit (uy imposé = −tassement en N4, déplacement imposé non nul).\n"
                      "Combinaison 1 « ELU 6.10 » : 1,35 G + 1,5 W (EN 1990, expression 6.10 avec γG = 1,35 et γQ = 1,5 ; exemple à deux actions).",
                      "TSALab/lab/Research/Structural/StructuralModels.cpp"),
            [](ExecutionContext& c) {
                PortalParameters p;
                p.span = c.real("span");
                p.height = c.real("height");
                p.q = c.real("q");
                p.H = c.real("H");
                p.settlement = c.real("settlement");
                p.column = c.text("column");
                p.beam = c.text("beam");
                p.material = c.text("material");
                if (c.failed()) return false;
                S::Model2D m;
                std::string error;
                if (!demoPortal(p, m, &error)) return c.fail(error);
                c.setOutput("model", make<ModelObject>(std::move(m)));
                return true;
            });

    lib.add(pureBlock("struct.model_example", "Modèle d'exemple", kModel,
                      "Modèles d'exemple du laboratoire : « poutre instable » (deux appuis simples verticaux : mécanisme), « poutre continue » "
                      "(deux travées, cas 1 charge répartie, cas 2 tassement de l'appui central).",
                      { data("name", "Exemple", VT::Text, std::string("poutre continue")) }, { object("model", "Modèle", Type::Model) },
                      "Poutre instable : IPE 300 S235 de 6 m, appuis uy seuls (translation X libre), charge 10 kN/m.\nPoutre continue : deux travées "
                      "de 5 m, IPE 300 S235, articulation en N1, appuis simples en N2 et N3 ; cas 1 « q » : 10 kN/m sur les deux travées ; cas 2 "
                      "« tassement » : uy imposé = −5 mm en N2 (réaction centrale −6EIΔ/L³).",
                      "TSALab/lab/Research/Structural/StructuralModels.cpp"),
            [](ExecutionContext& c) {
                const std::string name = c.text("name");
                if (c.failed()) return false;
                if (name == "poutre instable") c.setOutput("model", make<ModelObject>(unstableBeam()));
                else if (name == "poutre continue") c.setOutput("model", make<ModelObject>(settlementBeam()));
                else return c.fail("exemple inconnu « " + name + " » (poutre instable, poutre continue)");
                return true;
            });

    lib.add(pureBlock("struct.model_node", "Nœud du modèle", kModel, "Nœud d'identifiant donné : objet et coordonnées.",
                      { object("model", "Modèle", Type::Model), data("id", "Identifiant", VT::Integer, 1LL) },
                      { object("node", "Nœud", Type::Node), data("x", "X (m)", VT::Real), data("y", "Y (m)", VT::Real) }, "Refus : nœud absent du modèle.",
                      kSrcFrame2D),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const long long id = c.integer("id");
                if (!m || c.failed()) return false;
                const S::Node2D* n = m->node(static_cast<int>(id));
                if (!n) return c.fail("nœud " + std::to_string(id) + " absent du modèle");
                c.setOutput("node", make<NodeObject>(*n));
                c.setOutput("x", n->x);
                c.setOutput("y", n->y);
                return true;
            });

    lib.add(pureBlock("struct.model_element", "Élément du modèle", kModel,
                      "k-ième élément du modèle (index à partir de 0, pour une boucle « Pour » de 0 à nombre − 1) : objet FrameElement2D.",
                      { object("model", "Modèle", Type::Model), data("index", "Index (0…)", VT::Integer, 0LL) },
                      { object("element", "Élément", Type::Element), data("id", "Identifiant", VT::Integer), data("count", "Nombre d'éléments", VT::Integer) },
                      "L'élément est construit avec les nœuds, le matériau et la section du modèle, puis contrôlé (longueur, E, A, Iz).",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const long long index = c.integer("index");
                if (!m || c.failed()) return false;
                c.setOutput("count", static_cast<long long>(m->elements.size()));
                if (index < 0 || index >= static_cast<long long>(m->elements.size()))
                    return c.fail("index d'élément " + std::to_string(index) + " hors de [0 ; " + std::to_string(m->elements.size()) + "[");
                S::FrameElement2D e;
                std::string error;
                if (!S::makeElement(*m, m->elements[static_cast<std::size_t>(index)], e, &error)) return c.fail(error);
                const auto issues = e.validate();
                if (!issues.empty()) return c.fail(issues.front());
                c.setOutput("element", make<ElementObject>(e));
                c.setOutput("id", static_cast<long long>(e.id()));
                return true;
            });

    // --- Charges ----------------------------------------------------------------------------------
    lib.add(pureBlock("struct.load_nodal", "Charge nodale", kModel, "Forces Fx, Fy (kN) et moment Mz (kN·m) appliqués à un nœud, repère global.",
                      { data("node", "Nœud", VT::Integer, 1LL), data("fx", "Fx (kN)", VT::Real, 0.0), data("fy", "Fy (kN)", VT::Real, 0.0),
                        data("mz", "Mz (kN·m)", VT::Real, 0.0) },
                      { object("load", "Charge", Type::Load) },
                      std::string("Charge nodale : ajoutée directement au vecteur F aux DDL du nœud (plusieurs composantes simultanées admises).\n") + kUnits,
                      kSrcStiffness),
            [](ExecutionContext& c) {
                S::LoadCase l;
                l.nodalLoads.push_back({ static_cast<int>(c.integer("node")), c.real("fx"), c.real("fy"), c.real("mz") });
                return emitLoad(c, std::move(l));
            });

    lib.add(pureBlock("struct.load_distributed", "Charge répartie", kModel,
                      "Charge linéairement variable de a à b (uniforme, triangulaire, trapézoïdale, partielle) sur un élément.",
                      { data("element", "Élément", VT::Integer, 1LL), data("direction", "Direction", VT::Text, std::string("y local")),
                        data("q1", "q en a (kN/m)", VT::Real, -10.0), data("q2", "q en b (kN/m)", VT::Real, -10.0), data("a", "a", VT::Real, 0.0),
                        data("b", "b (−1 : jusqu'à j)", VT::Real, -1.0), data("relative", "Positions relatives (0…1)", VT::Bool, false),
                        data("projected", "Projetée (direction globale)", VT::Bool, false) },
                      { object("load", "Charge", Type::Load) }, kLoadDoc, kSrcElement),
            [](ExecutionContext& c) {
                S::LoadDirection d;
                if (!direction(c, d)) return false;
                auto l = S::linearLoad(static_cast<int>(c.integer("element")), c.real("q1"), c.real("q2"), c.real("a"), c.real("b"), d, c.boolean("relative"));
                l.projected = c.boolean("projected");
                S::LoadCase g;
                g.elementLoads.push_back(l);
                return emitLoad(c, std::move(g));
            });

    lib.add(pureBlock("struct.load_uniform", "Charge uniforme", kModel, "Charge uniforme q (kN/m) sur toute la longueur d'un élément.",
                      { data("element", "Élément", VT::Integer, 1LL), data("q", "q (kN/m)", VT::Real, -10.0),
                        data("direction", "Direction", VT::Text, std::string("y local")) },
                      { object("load", "Charge", Type::Load) }, kLoadDoc, kSrcElement),
            [](ExecutionContext& c) {
                S::LoadDirection d;
                if (!direction(c, d)) return false;
                S::LoadCase g;
                g.elementLoads.push_back(S::uniformLoad(static_cast<int>(c.integer("element")), c.real("q"), d));
                return emitLoad(c, std::move(g));
            });

    lib.add(pureBlock("struct.load_point", "Force ponctuelle sur élément", kModel, "Force concentrée P (kN) en position a sur un élément.",
                      { data("element", "Élément", VT::Integer, 1LL), data("P", "P (kN)", VT::Real, -10.0), data("a", "a", VT::Real, 0.5),
                        data("direction", "Direction", VT::Text, std::string("y local")), data("relative", "Position relative (0…1)", VT::Bool, true) },
                      { object("load", "Charge", Type::Load) },
                      kLoadDoc + "\nForce en x local : effort axial ponctuel ; en y local : effort transversal (saut de V en a).", kSrcElement),
            [](ExecutionContext& c) {
                S::LoadDirection d;
                if (!direction(c, d)) return false;
                S::LoadCase g;
                g.elementLoads.push_back(S::pointForce(static_cast<int>(c.integer("element")), c.real("P"), c.real("a"), d, c.boolean("relative")));
                return emitLoad(c, std::move(g));
            });

    lib.add(pureBlock("struct.load_moment", "Moment ponctuel sur élément", kModel, "Moment concentré M (kN·m) en position a sur un élément.",
                      { data("element", "Élément", VT::Integer, 1LL), data("M", "M (kN·m)", VT::Real, 10.0), data("a", "a", VT::Real, 0.5),
                        data("relative", "Position relative (0…1)", VT::Bool, true) },
                      { object("load", "Charge", Type::Load) },
                      kLoadDoc + "\nMoment positif dans le sens trigonométrique ; charges équivalentes = M·N'(a) (travail M·v'(a)) ; saut de M en a.",
                      kSrcElement),
            [](ExecutionContext& c) {
                S::LoadCase g;
                g.elementLoads.push_back(S::pointMoment(static_cast<int>(c.integer("element")), c.real("M"), c.real("a"), c.boolean("relative")));
                return emitLoad(c, std::move(g));
            });

    lib.add(pureBlock("struct.load_piecewise", "Charge multi-segments", kModel,
                      "Charge définie par des points (position:intensité) reliés linéairement : un segment par intervalle.",
                      { data("element", "Élément", VT::Integer, 1LL), data("points", "Points (a:q ; …)", VT::Text, std::string("0:-4 ; 2:-10 ; 6:-2")),
                        data("direction", "Direction", VT::Text, std::string("y local")), data("relative", "Positions relatives (0…1)", VT::Bool, false) },
                      { object("load", "Charge", Type::Load) }, kLoadDoc + "\nPositions strictement croissantes (sinon refus).", kSrcFrame2D),
            [](ExecutionContext& c) {
                S::LoadDirection d;
                if (!direction(c, d)) return false;
                std::vector<std::pair<double, double>> points;
                std::string error;
                const std::string text = c.text("points");
                if (c.failed()) return false;
                if (!parsePairs(text, points, &error)) return c.fail(error);
                S::LoadCase g;
                g.elementLoads = S::piecewiseLinearLoad(static_cast<int>(c.integer("element")), points, d, c.boolean("relative"), &error);
                if (g.elementLoads.empty()) return c.fail(error);
                return emitLoad(c, std::move(g));
            });

    lib.add(pureBlock("struct.load_thermal", "Action thermique", kModel, "Variation uniforme ΔT et gradient de température sur la hauteur de la section.",
                      { data("element", "Élément", VT::Integer, 1LL), data("deltaT", "ΔT uniforme (°C)", VT::Real, 30.0),
                        data("gradient", "T(y'<0) − T(y'>0) (°C)", VT::Real, 0.0) },
                      { object("load", "Charge", Type::Load) },
                      "Déformation initiale ε0 = α·ΔT (allongement) et courbure κ0 = α·gradient/h (fibre y' < 0 plus chaude : courbure positive, "
                      "même signe que M).\nEfforts d'encastrement parfait : f0' = [EAε0, 0, EIκ0, −EAε0, 0, −EIκ0] ; déformée : u' = N/EA + ε0, "
                      "v'' = M/EI + κ0.\nNécessite α > 0 (matériau) et, pour le gradient, h > 0 (section) : sinon refus.\nLimites : variation "
                      "linéaire de la température dans la hauteur, matériau élastique linéaire, α constant.",
                      kSrcElement),
            [](ExecutionContext& c) {
                S::LoadCase g;
                g.elementLoads.push_back(S::thermalLoad(static_cast<int>(c.integer("element")), c.real("deltaT"), c.real("gradient")));
                return emitLoad(c, std::move(g));
            });

    lib.add(pureBlock("struct.load_initial_strain", "Déformation initiale", kModel,
                      "Allongement initial ε0 et courbure initiale κ0 (défaut de longueur, précontrainte par déformation imposée).",
                      { data("element", "Élément", VT::Integer, 1LL), data("eps0", "ε0", VT::Real, 1e-4), data("kappa0", "κ0 (1/m)", VT::Real, 0.0) },
                      { object("load", "Charge", Type::Load) },
                      "ε0 > 0 : l'élément libre s'allongerait ; κ0 > 0 : la fibre y' < 0 s'allongerait. Un élément bloqué développe N = −EA·ε0 et "
                      "M = −EI·κ0 (efforts initiaux). Mêmes efforts d'encastrement que l'action thermique.",
                      kSrcElement),
            [](ExecutionContext& c) {
                S::LoadCase g;
                g.elementLoads.push_back(S::initialStrain(static_cast<int>(c.integer("element")), c.real("eps0"), c.real("kappa0")));
                return emitLoad(c, std::move(g));
            });

    lib.add(pureBlock("struct.load_self_weight", "Poids propre", kModel, "Poids propre γ·A (kN/m) selon −Y global ; élément 0 : tous les éléments.",
                      { data("element", "Élément (0 : tous)", VT::Integer, 0LL), data("factor", "Coefficient", VT::Real, 1.0) },
                      { object("load", "Charge", Type::Load) },
                      "Charge répartie uniforme q = −γ·A·coefficient (kN/m) selon Y global, par mètre de longueur réelle de l'élément. Nécessite "
                      "γ > 0 (matériau). Développée élément par élément au calcul (« tous les éléments »).",
                      kSrcFrame2D),
            [](ExecutionContext& c) {
                S::LoadCase g;
                g.elementLoads.push_back(S::selfWeight(static_cast<int>(c.integer("element")), c.real("factor")));
                return emitLoad(c, std::move(g));
            });

    lib.add(pureBlock("struct.imposed_displacement", "Déplacement imposé", kModel,
                      "Déplacement ou rotation imposé à un DDL bloqué (tassement, rotation d'appui) : jamais traité comme une force nodale.",
                      { data("node", "Nœud", VT::Integer, 1LL), data("component", "Composante (ux, uy, rz)", VT::Text, std::string("uy")),
                        data("value", "Valeur (m ou rad)", VT::Real, -0.01) },
                      { object("load", "Charge", Type::Load) },
                      "Le DDL doit être bloqué par un appui (sinon refus au calcul). Il entre dans d_c, et le système réduit devient "
                      "K_ff·d_f = F_f − K_fc·d_c ; les réactions R = K·d − F en tiennent compte. Combinaison : valeur multipliée par le coefficient.",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                int comp = 0;
                const std::string t = c.text("component");
                if (c.failed()) return false;
                if (!parseComponent(t, comp)) return c.fail("composante inconnue « " + t + " » (ux, uy, rz)");
                S::LoadCase g;
                g.imposedDisplacements.push_back({ static_cast<int>(c.integer("node")), comp, c.real("value") });
                return emitLoad(c, std::move(g));
            });

    lib.add(pureBlock("struct.load_merge", "Regrouper des charges", kModel, "Réunit deux groupes de charges (à chaîner pour en réunir davantage).",
                      { object("a", "A", Type::Load), object("b", "B", Type::Load) }, { object("load", "Charges", Type::Load) },
                      "Superposition : les listes de charges sont concaténées.", kSrcFrame2D),
            [](ExecutionContext& c) {
                const auto a = c.objectAs<LoadObject>("a");
                const auto b = c.objectAs<LoadObject>("b");
                if (!a || !b) return false;
                S::LoadCase g = a->value();
                g.append(b->value());
                return emitLoad(c, std::move(g));
            });

    lib.add(pureBlock("struct.load_case", "LoadCase", kModel, "Cas de charge : identifiant, nom et charges.",
                      { data("id", "Identifiant", VT::Integer, 1LL), data("name", "Nom", VT::Text, std::string("G")), object("loads", "Charges", Type::Load) },
                      { object("case", "Cas", Type::LoadCase) }, "Un cas regroupe des charges nodales, d'élément et des déplacements imposés.", kSrcFrame2D),
            [](ExecutionContext& c) {
                const auto l = c.objectAs<LoadObject>("loads");
                const long long id = c.integer("id");
                const std::string name = c.text("name");
                if (!l || c.failed()) return false;
                if (id <= 0) return c.fail("l'identifiant d'un cas doit être positif");
                S::LoadCase k = l->value();
                k.id = static_cast<int>(id);
                k.name = name;
                c.setOutput("case", make<LoadCaseObject>(std::move(k)));
                return true;
            });

    lib.add(pureBlock("struct.load_combination", "LoadCombination", kModel, "Combinaison Σ γk × cas k (coefficients saisis « cas:γ ; … »).",
                      { data("id", "Identifiant", VT::Integer, 1LL), data("name", "Nom", VT::Text, std::string("ELU")),
                        data("factors", "Coefficients (cas:γ ; …)", VT::Text, std::string("1:1.35 ; 2:1.5")) },
                      { object("combination", "Combinaison", Type::Combination) },
                      "Superposition linéaire : charges ET déplacements imposés de chaque cas multipliés par leur coefficient. Les coefficients "
                      "sont ceux saisis : aucun coefficient normatif n'est ajouté par le bloc.",
                      kSrcFrame2D),
            [](ExecutionContext& c) {
                std::vector<std::pair<double, double>> pairs;
                std::string error;
                const std::string text = c.text("factors");
                const long long id = c.integer("id");
                const std::string name = c.text("name");
                if (c.failed()) return false;
                if (!parsePairs(text, pairs, &error)) return c.fail(error);
                S::LoadCombination k;
                k.id = static_cast<int>(id);
                k.name = name;
                for (const auto& [cid, f] : pairs)
                {
                    if (cid != std::floor(cid) || cid <= 0) return c.fail("identifiant de cas invalide : " + num(cid));
                    k.factors.emplace_back(static_cast<int>(cid), f);
                }
                c.setOutput("combination", make<CombinationObject>(std::move(k)));
                return true;
            });

    lib.add(pureBlock("struct.loading", "Chargement à calculer", kModel,
                      "Charges effectives d'un cas, ou d'une combinaison (prioritaire si son identifiant est > 0).",
                      { object("model", "Modèle", Type::Model), data("case", "Cas", VT::Integer, 1LL), data("combination", "Combinaison (0 : aucune)", VT::Integer, 0LL) },
                      { object("loads", "Chargement", Type::LoadCase), data("title", "Titre", VT::Text) },
                      "Combinaison : Σ γk × cas k (charges et déplacements imposés). Le poids propre « tous les éléments » est développé élément "
                      "par élément. Refus : cas ou combinaison absents.",
                      kSrcFrame2D),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const long long caseId = c.integer("case"), combo = c.integer("combination");
                if (!m || c.failed()) return false;
                S::LoadCase loads;
                std::string error;
                if (!S::resolveLoads(*m, { static_cast<int>(caseId), static_cast<int>(combo) }, loads, &error)) return c.fail(error);
                const std::string title = combo > 0 ? "combinaison « " + loads.name + " »" : "cas " + std::to_string(loads.id) + " « " + loads.name + " »";
                c.setOutput("loads", make<LoadCaseObject>(std::move(loads)));
                c.setOutput("title", title);
                return true;
            });
}

} // namespace TSALab::Research::Structural::detail
