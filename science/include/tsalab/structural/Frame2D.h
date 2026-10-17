#pragma once

// TSALab — modèle de portique plan de la programmation structurelle (TSALab Blueprint), cœur scientifique C++ pur.
//
// Modèle explicite destiné à l'enseignement et à la recherche : chaque donnée a son unité et son repère, chaque
// étape de la méthode des déplacements est une fonction séparée (DirectStiffness2D.h) que les blocs Blueprint
// exposent un par un. Les calculs de production restent ceux de MetDeDeplacement et d'OpenSees (tsalab::planar),
// qui servent ici d'implémentations indépendantes de comparaison.
//
// Unités : kN, m, kN/m² (kPa) pour E, m², m⁴, °C, rad ; masses en tonnes (t = kN·s²/m), masses volumiques en t/m³,
// temps en s (kN = t·m/s² : unités cohérentes pour la dynamique).
// Repère global (X, Y) du plan ; rotations et moments positifs dans le sens trigonométrique (autour de +Z).
// Repère local d'un élément : x' du nœud i vers le nœud j, y' = x' tourné de +90°.
// DDL d'un nœud : ux, uy, θz ; DDL d'un élément : [ux_i, uy_i, θz_i, ux_j, uy_j, θz_j].

#include <string>
#include <utility>
#include <vector>

namespace tsalab::structural
{

struct Node2D
{
    int id = 0;
    double x = 0.0, y = 0.0;   ///< m
    std::string name;

    bool operator==(const Node2D&) const = default;
};

struct Material
{
    std::string name;
    double E = 0.0;            ///< module d'Young, kN/m²
    double alpha = 0.0;        ///< coefficient de dilatation thermique, 1/°C (actions thermiques)
    double unitWeight = 0.0;   ///< poids volumique γ, kN/m³ (poids propre)
    double density = 0.0;      ///< masse volumique ρ, t/m³ (dynamique : masse linéique ρ·A en t/m)

    bool operator==(const Material&) const = default;
};

struct Section2D
{
    std::string name;
    double A = 0.0;            ///< aire, m²
    double Iz = 0.0;           ///< moment quadratique de flexion dans le plan, m⁴
    double h = 0.0;            ///< hauteur, m (gradient thermique) ; 0 = non renseignée

    bool operator==(const Section2D&) const = default;
};

/// Appui d'un nœud, repère global : DDL bloqués et appuis élastiques (ressorts). Un DDL bloqué a un déplacement nul,
/// ou imposé par le chargement (ImposedDisplacement : action d'un cas). Un ressort agit sur un DDL libre : sa rigidité
/// s'ajoute à K, il exerce sur la structure la force −k·u (réaction de l'appui élastique). Un ressort sur un DDL bloqué
/// est sans effet (signalé à la validation).
struct SupportCondition
{
    int node = 0;
    bool fixUx = false, fixUy = false, fixRz = false;
    double kUx = 0.0, kUy = 0.0;   ///< ressorts de translation, kN/m (0 : aucun)
    double kRz = 0.0;              ///< ressort de rotation, kN·m/rad (0 : aucun)

    bool hasSpring() const { return kUx > 0.0 || kUy > 0.0 || kRz > 0.0; }
    bool operator==(const SupportCondition&) const = default;
};

/// Élément filaire du modèle : extrémités désignées par identifiant de nœud, matériau et section portés par l'élément.
/// Rotule d'extrémité (releaseI, releaseJ) : moment fléchissant nul à cette extrémité ; la rotation propre de l'élément
/// y est indépendante de celle du nœud (condensation statique, FrameElement2D).
struct ElementDefinition
{
    int id = 0;
    int nodeI = 0, nodeJ = 0;
    Material material;
    Section2D section;
    std::string name;
    bool releaseI = false, releaseJ = false;

    bool operator==(const ElementDefinition&) const = default;
};

/// Masse concentrée en un nœud (dynamique) : translations mx, my (t), inertie de rotation jz (t·m²).
struct NodalMass
{
    int node = 0;
    double mx = 0.0, my = 0.0, jz = 0.0;

    bool operator==(const NodalMass&) const = default;
};

// ---------------------------------------------------------------------------------------------
// Charges
// ---------------------------------------------------------------------------------------------

/// Force et moment appliqués à un nœud, repère global (kN, kN·m).
struct NodalLoad
{
    int node = 0;
    double fx = 0.0, fy = 0.0, mz = 0.0;

    bool operator==(const NodalLoad&) const = default;
};

/// Direction d'une force ou d'une charge répartie appliquée à un élément.
enum class LoadDirection
{
    LocalX,    ///< axe de l'élément x'
    LocalY,    ///< perpendiculaire à l'élément, y'
    GlobalX,   ///< X global
    GlobalY    ///< Y global
};

enum class ElementLoadType
{
    Distributed,    ///< linéairement variable de a à b (q1 en a, q2 en b) : uniforme, triangulaire, trapézoïdale, partielle
    PointForce,     ///< force concentrée P = q1 en a
    PointMoment,    ///< moment concentré M = q1 en a (autour de z, sens trigonométrique)
    Thermal,        ///< variation uniforme deltaT et gradient gradientT sur la hauteur h de la section
    InitialStrain,  ///< déformation axiale initiale epsilon0 et courbure initiale kappa0 (défaut de longueur, précontrainte par déformation)
    SelfWeight      ///< poids propre γ·A selon −Y global, sur toute la longueur ; element = 0 : tous les éléments
};

/// Charge appliquée à un élément. Les positions sont mesurées depuis le nœud i, le long de l'élément.
struct ElementLoad
{
    int element = 0;
    ElementLoadType type = ElementLoadType::Distributed;
    LoadDirection direction = LoadDirection::LocalY;
    /// Charge répartie de direction globale donnée par mètre de PROJECTION perpendiculaire à sa direction (neige :
    /// par mètre horizontal). Sans effet pour une direction locale (refusé).
    bool projected = false;
    bool relative = false;      ///< a et b en fraction de la longueur (0 à 1)
    double a = 0.0;             ///< début (répartie) ou position (force, moment)
    double b = -1.0;            ///< fin (répartie) ; < 0 : jusqu'au nœud j
    double q1 = 0.0;            ///< répartie : kN/m en a ; force : P (kN) ; moment : M (kN·m)
    double q2 = 0.0;            ///< répartie : kN/m en b
    double deltaT = 0.0;        ///< Thermal : variation uniforme de température (°C)
    double gradientT = 0.0;     ///< Thermal : T(fibre y' < 0) − T(fibre y' > 0) (°C) sur la hauteur h
    double epsilon0 = 0.0;      ///< InitialStrain : allongement relatif initial (> 0 : l'élément libre s'allonge)
    double kappa0 = 0.0;        ///< InitialStrain : courbure initiale (1/m), > 0 si la fibre y' < 0 s'allonge
    double factor = 1.0;        ///< SelfWeight : multiplicateur de γ·A

    bool operator==(const ElementLoad&) const = default;
};

/// Déplacement imposé à un DDL bloqué par un appui (tassement, rotation d'appui) : traité par la partition
/// K_ff·d_f = F_f − K_fc·d_c, jamais comme une force nodale.
struct ImposedDisplacement
{
    int node = 0;
    int component = 0;          ///< 0 = ux, 1 = uy, 2 = θz
    double value = 0.0;         ///< m ou rad

    bool operator==(const ImposedDisplacement&) const = default;
};

struct LoadCase
{
    int id = 0;
    std::string name;
    std::vector<NodalLoad> nodalLoads;
    std::vector<ElementLoad> elementLoads;
    std::vector<ImposedDisplacement> imposedDisplacements;

    bool empty() const { return nodalLoads.empty() && elementLoads.empty() && imposedDisplacements.empty(); }
    /// Ajoute les charges et déplacements imposés de `other` multipliés par `factor` (superposition linéaire).
    void append(const LoadCase& other, double factor = 1.0);
    bool operator==(const LoadCase&) const = default;
};

struct LoadCombination
{
    int id = 0;
    std::string name;
    std::vector<std::pair<int, double>> factors;   ///< (identifiant de cas, coefficient)

    bool operator==(const LoadCombination&) const = default;
};

struct Model2D
{
    std::string name;
    std::vector<Node2D> nodes;
    std::vector<ElementDefinition> elements;
    std::vector<SupportCondition> supports;
    std::vector<LoadCase> loadCases;
    std::vector<LoadCombination> combinations;
    std::vector<NodalMass> nodalMasses;                           ///< dynamique
    /// Traçabilité (clé, valeur) : fichier d'origine, empreinte, plan, date d'import… (vide pour un modèle construit).
    std::vector<std::pair<std::string, std::string>> metadata;

    const Node2D* node(int id) const;
    const ElementDefinition* element(int id) const;
    const SupportCondition* support(int nodeId) const;
    const LoadCase* loadCase(int id) const;
    const LoadCombination* combination(int id) const;
    /// Éléments reliés à un nœud.
    int elementCount(int nodeId) const;
    /// Nœud sans rigidité de rotation : relié uniquement à des éléments articulés à ce nœud (rotules), sans appui ni
    /// ressort en rotation. Sa rotation est indéterminée et sans effet : elle est éliminée du système (signalé).
    bool rotationFree(int nodeId) const;
    std::string metadataValue(const std::string& key) const;
    double extent() const;   ///< plus grande dimension du modèle (m), pour les tolérances géométriques

    bool operator==(const Model2D&) const = default;
};

// --- Construction des charges (aides) ------------------------------------------------------------

ElementLoad uniformLoad(int element, double q, LoadDirection direction = LoadDirection::LocalY);
ElementLoad linearLoad(int element, double q1, double q2, double a, double b, LoadDirection direction, bool relative = false);
ElementLoad pointForce(int element, double P, double a, LoadDirection direction, bool relative = false);
ElementLoad pointMoment(int element, double M, double a, bool relative = false);
ElementLoad thermalLoad(int element, double deltaT, double gradientT);
ElementLoad initialStrain(int element, double epsilon0, double kappa0);
ElementLoad selfWeight(int element = 0, double factor = 1.0);
/// Charge définie par plusieurs segments : points (position, intensité) croissants, reliés linéairement ;
/// un segment par intervalle. Positions en m (ou en fraction si relative).
std::vector<ElementLoad> piecewiseLinearLoad(int element, const std::vector<std::pair<double, double>>& points,
                                             LoadDirection direction, bool relative = false, std::string* error = nullptr);

const char* directionName(LoadDirection d);           ///< « x local », « Y global »…
const char* loadTypeName(ElementLoadType t);          ///< « répartie », « force ponctuelle »…
const char* componentName(int component);             ///< « ux », « uy », « θz »

// --- Contrôle et chargement effectif -------------------------------------------------------------

struct ValidationReport
{
    std::vector<std::string> errors;     ///< bloquantes : le calcul est refusé
    std::vector<std::string> warnings;   ///< signalées (nœud isolé, charge nulle…)
    bool ok() const { return errors.empty(); }
    std::string text() const;
};

/// Contrôle des données : identifiants uniques, références existantes, longueurs non nulles, E, A, Iz positifs
/// et finis, positions de charge dans l'élément, données thermiques et de poids propre présentes, déplacements
/// imposés sur des DDL bloqués, combinaisons formées de cas existants.
ValidationReport validate(const Model2D& model);

/// Chargement à calculer : un cas (caseId) ou une combinaison (combinationId > 0, prioritaire).
struct LoadSelection
{
    int caseId = 0;
    int combinationId = 0;
};

/// Charges effectives : le cas seul, ou Σ γ_k × cas_k pour une combinaison (charges ET déplacements imposés
/// multipliés : superposition linéaire). Le poids propre « tous les éléments » (element = 0) est développé.
bool resolveLoads(const Model2D& model, const LoadSelection& selection, LoadCase& out, std::string* error = nullptr);

} // namespace tsalab::structural
