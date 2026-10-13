#pragma once

// TSALab — élément filaire de portique plan à 6 DDL (Euler-Bernoulli, élastique linéaire, section constante).
//
// Classe pré-codée de TSALab Blueprint : longueur et orientation, rigidité locale, transformation local/global,
// rigidité globale, charges nodales équivalentes, efforts d'extrémité, diagrammes N, V, M et déformée.
//
// Formulation (conventions de Frame2D.h) :
//  - k' : termes axiaux EA/L, termes de flexion 12EI/L³, 6EI/L², 4EI/L, 2EI/L (exacts pour une barre prismatique) ;
//  - T  : d' = T·d avec, par nœud, [c s 0 ; −s c 0 ; 0 0 1], c = cos α, s = sin α (α : angle de X vers x') ;
//  - k  = Tᵀ·k'·T ;
//  - efforts d'encastrement parfait f0' : efforts EXERCÉS SUR l'élément par ses extrémités bloquées ; ils valent
//    −∫ Nᵀ·p dx avec les fonctions de forme exactes de la barre prismatique (linéaires en axial, cubiques
//    d'Hermite en flexion), ce qui donne les valeurs exactes des tables (qL/2, qL²/12…) pour toute charge ;
//    déformation initiale : f0' = [EAε0, 0, EIκ0, −EAε0, 0, −EIκ0] ;
//  - charges nodales équivalentes : −f0' (local), Tᵀ·(−f0') (global) ;
//  - efforts d'extrémité : f' = k'·T·d + f0' (forces exercées sur l'élément, repère local
//    [Fx_i, Fy_i, Mz_i, Fx_j, Fy_j, Mz_j]) ;
//  - diagrammes (convention RDM, coupure en x, tronçon [0, x]) : N(x) = −Fx_i − Px(x), N > 0 en traction ;
//    V(x) = Fy_i + Py(x) = dM/dx ; M(x) = −Mz_i + x·Fy_i + MPy(x) − ΣM0, M > 0 tend la fibre y' < 0 ;
//    M(0) = −Mz_i, M(L) = Mz_j ; déformée : EA(u' − ε0) = N, EI(v'' − κ0) = M.

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
    /// Contrôle de cohérence : écart entre u, v, v' intégrés en x = L et les déplacements de l'extrémité j
    /// (|Δu| + |Δv| + L·|Δθ|, m). Proche de zéro si efforts, charges et déplacements sont cohérents.
    double closureError = 0.0;

    bool operator==(const InternalForceDiagram&) const = default;
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

    /// Données invalides : longueur nulle, E, A ou Iz non positifs ou non finis, coordonnées non finies. Vide = valide.
    std::vector<std::string> validate() const;
    bool isValid() const { return validate().empty(); }

    double length() const;
    double angle() const;          ///< rad, de X vers x' (sens trigonométrique), dans ]−π, π]
    double cosine() const;         ///< c = (xj − xi) / L
    double sine() const;           ///< s = (yj − yi) / L

    Matrix localStiffness() const;     ///< k' (6×6)
    Matrix transformation() const;     ///< T (6×6), d' = T·d
    Matrix globalStiffness() const;    ///< k = Tᵀ·k'·T

    Vector toLocal(const Vector& global6) const;   ///< T·v
    Vector toGlobal(const Vector& local6) const;   ///< Tᵀ·v

    /// Charges de CET élément (element == id(), ou poids propre « tous les éléments ») converties en repère local.
    /// Une charge invalide est écartée et décrite dans *errors (position hors de l'élément, hauteur de section ou
    /// coefficient de dilatation manquant, poids volumique nul, projection d'une direction locale…).
    std::vector<LocalLoad> localLoads(const std::vector<ElementLoad>& loads, std::vector<std::string>* errors = nullptr) const;

    Vector fixedEndForces(const std::vector<LocalLoad>& loads) const;              ///< f0' (local)
    Vector equivalentNodalLoadsLocal(const std::vector<LocalLoad>& loads) const;   ///< −f0'
    Vector equivalentNodalLoadsGlobal(const std::vector<LocalLoad>& loads) const;  ///< Tᵀ·(−f0')

    /// f' = k'·T·d + f0' (d : déplacements globaux des 6 DDL de l'élément).
    Vector endForces(const Vector& globalDisplacements, const std::vector<LocalLoad>& loads) const;

    /// N, V, M, u, v, v' en `points` abscisses régulières (au moins 2) plus les points singuliers des charges.
    InternalForceDiagram diagram(const Vector& globalDisplacements, const std::vector<LocalLoad>& loads, int points = 21) const;

    /// Résultante des charges dans le repère global et moment par rapport à l'origine (contrôle d'équilibre,
    /// calculés directement depuis les charges, indépendamment des charges nodales équivalentes).
    struct Resultant
    {
        double fx = 0.0, fy = 0.0, mz = 0.0;
    };
    Resultant loadResultant(const std::vector<LocalLoad>& loads) const;

    bool operator==(const FrameElement2D&) const = default;

private:
    int m_id = 0;
    std::string m_name;
    Node2D m_i, m_j;
    Material m_material;
    Section2D m_section;
};

} // namespace tsalab::structural
