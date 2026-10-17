#pragma once

// TSALab — élément filaire de portique plan à 6 DDL (Euler-Bernoulli, élastique linéaire, section constante).
//
// Classe pré-codée de TSALab Blueprint : longueur et orientation, rigidité locale, transformation local/global,
// rigidité globale, charges nodales équivalentes, efforts d'extrémité, diagrammes N, V, M et déformée, rotules
// d'extrémité, matrices de masse (dynamique).
//
// Formulation (conventions de Frame2D.h) :
//  - k' : termes axiaux EA/L, termes de flexion 12EI/L³, 6EI/L², 4EI/L, 2EI/L (exacts pour une barre prismatique) ;
//  - T  : d' = T·d avec, par nœud, [c s 0 ; −s c 0 ; 0 0 1], c = cos α, s = sin α (α : angle de X vers x') ;
//  - k  = Tᵀ·k'·T ;
//  - efforts d'encastrement parfait f0' : efforts EXERCÉS SUR l'élément par ses extrémités bloquées ; ils valent
//    −∫ Nᵀ·p dx avec les fonctions de forme exactes de la barre prismatique (linéaires en axial, cubiques
//    d'Hermite en flexion), ce qui donne les valeurs exactes des tables (qL/2, qL²/12…) pour toute charge ;
//    déformation initiale : f0' = [EAε0, 0, EIκ0, −EAε0, 0, −EIκ0] ;
//  - rotules d'extrémité : la rotation propre de l'élément à une extrémité articulée (R) n'est plus celle du nœud ;
//    elle découle de la condition de moment nul : θ_R = −k'_RR⁻¹·(k'_RO·d'_O + f0'_R). Sous la forme
//    d'_élément = C·d' + c0, la rigidité devient Cᵀ·k'·C (condensation statique), les efforts d'encastrement
//    Cᵀ·(k'·c0 + f0') (qL²/8 côté encastré d'une barre articulée-encastrée), la masse cohérente Cᵀ·m'·C ;
//  - charges nodales équivalentes : −f0' (local), Tᵀ·(−f0') (global) ;
//  - efforts d'extrémité : f' = k'·T·d + f0' (forces exercées sur l'élément, repère local
//    [Fx_i, Fy_i, Mz_i, Fx_j, Fy_j, Mz_j]) ; moment nul à une extrémité articulée ;
//  - diagrammes (convention RDM, coupure en x, tronçon [0, x]) : N(x) = −Fx_i − Px(x), N > 0 en traction ;
//    V(x) = Fy_i + Py(x) = dM/dx ; M(x) = −Mz_i + x·Fy_i + MPy(x) − ΣM0, M > 0 tend la fibre y' < 0 ;
//    M(0) = −Mz_i, M(L) = Mz_j ; déformée : EA(u' − ε0) = N, EI(v'' − κ0) = M, intégrée depuis les déplacements
//    propres de l'élément (rotations d'extrémité de l'élément aux rotules) ;
//  - masses (t, avec ρ en t/m³) : cohérente m̄L/6·[2 1 ; 1 2] (axial), m̄L/420·[156 22L 54 −13L ; …] (flexion) ;
//    concentrée m̄L/2 sur les translations de chaque nœud, inertie de rotation nulle ; m̄ = ρ·A.

#include "tsalab/numerics/LinearAlgebra.h"
#include "tsalab/structural/Frame2D.h"

#include <string>
#include <vector>

namespace tsalab::structural
{

using numerics::Matrix;
using numerics::Vector;

/// Charge d'un élément exprimée dans son repère local, positions en mètres depuis le nœud i.
struct LocalLoad
{
    enum class Kind
    {
        Distributed,   ///< px, py linéaires de a à b
        Force,         ///< force concentrée (px1, py1) en a
        Moment,        ///< moment concentré m en a
        Strain         ///< déformation initiale ε0, κ0 sur toute la longueur
    };
    Kind kind = Kind::Distributed;
    double a = 0.0, b = 0.0;
    double px1 = 0.0, py1 = 0.0;   ///< répartie : kN/m en a ; force : kN
    double px2 = 0.0, py2 = 0.0;   ///< répartie : kN/m en b
    double m = 0.0;                ///< kN·m
    double epsilon0 = 0.0, kappa0 = 0.0;

    bool operator==(const LocalLoad&) const = default;
};

struct DiagramPoint
{
    double x = 0.0;                ///< m depuis le nœud i
    double N = 0.0, V = 0.0, M = 0.0;
    double u = 0.0, v = 0.0;       ///< déplacements locaux : axial, transversal (m)
    double rotation = 0.0;         ///< v'(x) (rad)

    bool operator==(const DiagramPoint&) const = default;
};

/// Diagrammes d'efforts intérieurs et déformée d'un élément.
struct InternalForceDiagram
{
    int element = 0;
    double length = 0.0;
    /// Abscisses croissantes ; une discontinuité (force ou moment concentré) donne deux points de même x.
    std::vector<DiagramPoint> points;
    double Nmin = 0.0, Nmax = 0.0, Vmin = 0.0, Vmax = 0.0, Mmin = 0.0, Mmax = 0.0;
    double xMmin = 0.0, xMmax = 0.0;
    double maxDeflection = 0.0;    ///< écart signé de v(x) à la corde de plus grande valeur absolue (m)
    double xMaxDeflection = 0.0;
    /// Contrôle de cohérence : écart entre u, v, v' intégrés en x = L et les déplacements propres de l'extrémité j
    /// (|Δu| + |Δv| + L·|Δθ|, m). Proche de zéro si efforts, charges et déplacements sont cohérents.
    double closureError = 0.0;

    bool operator==(const InternalForceDiagram&) const = default;
};

/// Modèle de masse d'un élément.
enum class MassModel
{
    Consistent,   ///< masse cohérente (fonctions de forme de la rigidité)
    Lumped        ///< masse concentrée aux nœuds (translations seulement)
};

class FrameElement2D
{
public:
    FrameElement2D() = default;
    FrameElement2D(int id, const Node2D& nodeI, const Node2D& nodeJ, const Material& material, const Section2D& section,
                   std::string name = {});

    int id() const { return m_id; }
    const std::string& name() const { return m_name; }
    const Node2D& nodeI() const { return m_i; }
    const Node2D& nodeJ() const { return m_j; }
    const Material& material() const { return m_material; }
    const Section2D& section() const { return m_section; }

    /// Rotules d'extrémité (moment nul à l'extrémité i et/ou j).
    void setReleases(bool releaseI, bool releaseJ);
    bool releaseI() const { return m_releaseI; }
    bool releaseJ() const { return m_releaseJ; }
    bool hasRelease() const { return m_releaseI || m_releaseJ; }

    /// Données invalides : longueur nulle, E, A ou Iz non positifs ou non finis, coordonnées non finies. Vide = valide.
    std::vector<std::string> validate() const;
    bool isValid() const { return validate().empty(); }

    double length() const;
    double angle() const;          ///< rad, de X vers x' (sens trigonométrique), dans ]−π, π]
    double cosine() const;         ///< c = (xj − xi) / L
    double sine() const;           ///< s = (yj − yi) / L

    /// k' (6×6), condensé aux rotules (lignes et colonnes des rotations libérées nulles).
    Matrix localStiffness() const;
    /// k' sans rotule (barre bi-encastrée), pour comparaison et pédagogie.
    Matrix baseLocalStiffness() const;
    Matrix transformation() const;     ///< T (6×6), d' = T·d
    Matrix globalStiffness() const;    ///< k = Tᵀ·k'·T

    Vector toLocal(const Vector& global6) const;   ///< T·v
    Vector toGlobal(const Vector& local6) const;   ///< Tᵀ·v

    /// Charges de CET élément (element == id(), ou poids propre « tous les éléments ») converties en repère local.
    /// Une charge invalide est écartée et décrite dans *errors (position hors de l'élément, hauteur de section ou
    /// coefficient de dilatation manquant, poids volumique nul, projection d'une direction locale…).
    std::vector<LocalLoad> localLoads(const std::vector<ElementLoad>& loads, std::vector<std::string>* errors = nullptr) const;

    Vector fixedEndForces(const std::vector<LocalLoad>& loads) const;              ///< f0' (local, condensé aux rotules)
    Vector equivalentNodalLoadsLocal(const std::vector<LocalLoad>& loads) const;   ///< −f0'
    Vector equivalentNodalLoadsGlobal(const std::vector<LocalLoad>& loads) const;  ///< Tᵀ·(−f0')

    /// f' = k'·T·d + f0' (d : déplacements globaux des 6 DDL de l'élément).
    Vector endForces(const Vector& globalDisplacements, const std::vector<LocalLoad>& loads) const;
    /// Déplacements locaux PROPRES de l'élément : T·d, avec, aux rotules, la rotation de l'élément (et non du nœud).
    Vector elementLocalDisplacements(const Vector& globalDisplacements, const std::vector<LocalLoad>& loads) const;

    /// N, V, M, u, v, v' en `points` abscisses régulières (au moins 2) plus les points singuliers des charges.
    InternalForceDiagram diagram(const Vector& globalDisplacements, const std::vector<LocalLoad>& loads, int points = 21) const;

    /// Efforts intérieurs en une abscisse x (limite à gauche d'une discontinuité ; includeAt : à droite).
    struct SectionForces
    {
        double N = 0.0, V = 0.0, M = 0.0;
    };
    SectionForces internalForcesAt(const Vector& globalDisplacements, const std::vector<LocalLoad>& loads, double x, bool includeAt = false) const;

    /// Résultante des charges dans le repère global et moment par rapport à l'origine (contrôle d'équilibre,
    /// calculés directement depuis les charges, indépendamment des charges nodales équivalentes).
    struct Resultant
    {
        double fx = 0.0, fy = 0.0, mz = 0.0;
    };
    Resultant loadResultant(const std::vector<LocalLoad>& loads) const;

    /// Masse linéique m̄ = ρ·A (t/m).
    double massPerLength() const;
    Matrix localMass(MassModel model) const;    ///< m' (6×6, t), condensée aux rotules pour la masse cohérente
    Matrix globalMass(MassModel model) const;   ///< Tᵀ·m'·T

    bool operator==(const FrameElement2D&) const = default;

private:
    /// d'_élément = C·d' + c0 (rotules) ; sans rotule : C = I, c0 = 0.
    void condensation(const Vector& baseFixedEnd, Matrix& C, Vector& c0) const;
    Vector baseFixedEndForces(const std::vector<LocalLoad>& loads) const;   ///< f0' de la barre bi-encastrée

    int m_id = 0;
    std::string m_name;
    Node2D m_i, m_j;
    Material m_material;
    Section2D m_section;
    bool m_releaseI = false, m_releaseJ = false;
};

} // namespace tsalab::structural
