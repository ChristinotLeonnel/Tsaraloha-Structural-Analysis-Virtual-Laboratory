#pragma once

// TSALab — méthode des déplacements (rigidité directe) pour portiques plans, étape par étape.
//
// Chaque étape est une fonction séparée, exposée telle quelle par un bloc de TSALab Blueprint ; solveLinearStatic
// les enchaîne (« calcul direct », référence des graphes) :
//   1. numérotation globale des DDL (3 par nœud)                                  numberDofs
//   2. éléments : géométrie, k', T, k = Tᵀk'T, charges nodales équivalentes         FrameElement2D
//   3. assemblage K = Σ A_eᵀ·k_e·A_e + K_ressorts et F = charges nodales + Σ A_eᵀ·f_eq,e   assembleStiffness, assembleLoads
//   4. conditions aux limites : DDL libres f, imposés c, d_c                         boundaryConditions
//      système réduit K_ff·d_f = F_f − K_fc·d_c                                      reduce
//   5. résolution (factorisation, jamais d'inverse explicite), diagnostic des
//      systèmes singuliers (DDL fautif, mode de mécanisme, causes) et du
//      conditionnement                                                            solveDisplacements
//   6. déplacements complets d = [d_f ; d_c]                                        fullDisplacements
//   7. réactions R = K·d − F, bilan d'équilibre global (forces et moment)           reactions, checkEquilibrium
//   8. efforts d'extrémité et diagrammes par élément                                elementResults

#include "tsalab/structural/FrameElement2D.h"

#include <array>
#include <string>
#include <vector>

namespace tsalab::structural
{

/// Numérotation globale : le k-ième nœud du modèle porte les DDL 3k (ux), 3k+1 (uy), 3k+2 (θz).
struct DofNumbering
{
    std::vector<int> nodeIds;

    int size() const { return 3 * static_cast<int>(nodeIds.size()); }
    int nodePosition(int nodeId) const;                   ///< −1 si absent
    int dof(int nodeId, int component) const;             ///< −1 si absent
    std::array<int, 6> elementDofs(int nodeI, int nodeJ) const;
    std::string label(int dof) const;                     ///< « N3.uy »
    bool operator==(const DofNumbering&) const = default;
};

DofNumbering numberDofs(const Model2D& model);

/// Partition des DDL : libres (f) et imposés (c, bloqués par un appui), valeurs imposées d_c.
struct BoundaryConditions
{
    std::vector<int> freeDofs;          ///< croissants
    std::vector<int> constrainedDofs;   ///< croissants
    Vector imposed;                     ///< d_c, même ordre que constrainedDofs (0 pour un appui fixe)
    /// Rotations éliminées (sous-ensemble de constrainedDofs) : nœud entouré de rotules, sans appui ni ressort en
    /// rotation — rotation sans rigidité, indéterminée et sans effet ; ce n'est pas un appui (aucune réaction).
    std::vector<int> eliminatedDofs;

    bool isConstrained(int dof) const;
    bool isEliminated(int dof) const;
    bool operator==(const BoundaryConditions&) const = default;
};

/// Appuis du modèle, rotations sans rigidité éliminées, déplacements imposés du chargement (refusés sur un DDL libre).
bool boundaryConditions(const Model2D& model, const DofNumbering& dofs, const LoadCase& loads, BoundaryConditions& out,
                        std::string* error = nullptr);

/// Élément du modèle (nœuds et propriétés). Faux si un nœud manque ; l'élément peut être invalide (validate()).
bool makeElement(const Model2D& model, const ElementDefinition& definition, FrameElement2D& out, std::string* error = nullptr);

/// Opérateur d'assemblage booléen A_e (6 × n) : d_e = A_e·d.
Matrix assemblyOperator(const DofNumbering& dofs, int nodeI, int nodeJ);

/// K (n × n) = Σ A_eᵀ·k_e·A_e + K_ressorts, calculé par adressage direct (même résultat que la somme des produits).
bool assembleStiffness(const Model2D& model, const DofNumbering& dofs, Matrix& K, std::string* error = nullptr);
/// Rigidité des appuis élastiques (diagonale, n × n) : k sur les DDL libres munis d'un ressort.
Matrix springStiffness(const Model2D& model, const DofNumbering& dofs);
/// Forces exercées par les ressorts sur la structure : −k·u (n, nulles hors ressorts).
Vector springForces(const Model2D& model, const DofNumbering& dofs, const Vector& d);
/// Vecteur des charges nodales seules (repère global).
Vector nodalLoadVector(const DofNumbering& dofs, const LoadCase& loads);
/// F = charges nodales + Σ A_eᵀ·(charges nodales équivalentes globales). Les déplacements imposés n'y figurent pas.
bool assembleLoads(const Model2D& model, const DofNumbering& dofs, const LoadCase& loads, Vector& F, std::string* error = nullptr);

struct ReducedSystem
{
    Matrix Kff, Kfc, Kcf, Kcc;
    Vector Ff, Fc, dc;
    Vector rhs;                         ///< F_f − K_fc·d_c
    bool operator==(const ReducedSystem&) const = default;
};

/// Faux si les dimensions de K, F et de la partition ne concordent pas.
bool reduce(const Matrix& K, const Vector& F, const BoundaryConditions& bc, ReducedSystem& out, std::string* error = nullptr);

/// Résolution de K_ff·d_f = F_f − K_fc·d_c.
struct DisplacementSolve
{
    bool success = false;
    std::string message;                     ///< réussite, ou diagnostic d'un système singulier avec causes possibles
    std::vector<std::string> warnings;       ///< conditionnement élevé, résidu élevé
    Vector df;
    numerics::SolverReport report;
    double conditionNumber = -1.0;           ///< λmax/λmin de K_ff (si n ≤ kConditionLimit) ; −1 : non calculé
    std::string failedDof;                   ///< DDL au pivot nul (« N2.ux »)
    std::vector<std::string> mechanismDofs;  ///< DDL du mode de mécanisme (|φ| ≥ 10 % du maximum)
    Vector mechanismMode;                    ///< mode à énergie nulle sur les DDL libres (normé : max |φ| = 1)
    /// « rigid-body » (appuis insuffisants), « isolated-node », « internal », ou « ill-conditioned » (pas de mode nul :
    /// système quasi singulier par écart de rigidités)
    std::string mechanismKind;
    bool operator==(const DisplacementSolve&) const = default;
};

inline constexpr int kConditionLimit = 150;   ///< taille maximale de K_ff pour le calcul du conditionnement et des modes
/// Conditionnement au-delà duquel le calcul est signalé (≈ 6 chiffres significatifs fiables en double précision). Au-delà
/// d'environ 1e11, le pivot relatif passe sous la tolérance du solveur et le système est déclaré quasi singulier.
inline constexpr double kIllConditioned = 1e10;

DisplacementSolve solveDisplacements(const ReducedSystem& system, const BoundaryConditions& bc, const DofNumbering& dofs,
                                     const Model2D* model = nullptr,
                                     numerics::SolverMethod method = numerics::SolverMethod::Cholesky);

/// d complet (n) : d_f et d_c remis à leur place. Faux si les tailles ne concordent pas.
bool fullDisplacements(const BoundaryConditions& bc, const Vector& df, int size, Vector& d, std::string* error = nullptr);
/// R = K·d − F (forces exercées par les appuis ; ≈ 0 aux DDL libres).
bool reactions(const Matrix& K, const Vector& d, const Vector& F, Vector& R, std::string* error = nullptr);

struct EquilibriumCheck
{
    double loadFx = 0.0, loadFy = 0.0, loadMz = 0.0;              ///< charges appliquées (moment / origine)
    double reactionFx = 0.0, reactionFy = 0.0, reactionMz = 0.0;  ///< réactions aux DDL imposés
    double residualFx = 0.0, residualFy = 0.0, residualMz = 0.0;  ///< charges + réactions
    double forceScale = 0.0, momentScale = 0.0;                   ///< grandeurs de référence des écarts relatifs
    double relativeForceResidual = 0.0, relativeMomentResidual = 0.0;
    bool balanced(double tolerance = 1e-9) const;
    std::string text() const;
    bool operator==(const EquilibriumCheck&) const = default;
};

/// Bilan global : résultante des charges calculée depuis leur définition (pas depuis F), des réactions d'appui et des
/// forces des ressorts (−k·u, si d est fourni). referenceForce : force de référence des écarts relatifs quand charges
/// et réactions sont nulles ou presque (dilatation libre, déplacement imposé sans effort) ; referenceForce(K, d, F).
EquilibriumCheck checkEquilibrium(const Model2D& model, const DofNumbering& dofs, const BoundaryConditions& bc,
                                  const LoadCase& loads, const Vector& R, double referenceForce = 0.0, const Vector& d = {});
/// max(‖K·d‖∞, ‖F‖∞) : ordre de grandeur des forces nodales élastiques du calcul.
double referenceForce(const Matrix& K, const Vector& d, const Vector& F);

struct ElementResult
{
    int element = 0;
    Vector dGlobal, dLocal;          ///< 6 DDL
    Vector fixedEnd;                 ///< f0' (local)
    Vector endForces;                ///< f' (local)
    Vector endForcesGlobal;          ///< Tᵀ·f'
    InternalForceDiagram diagram;
    bool operator==(const ElementResult&) const = default;
};

bool elementResults(const Model2D& model, const DofNumbering& dofs, const LoadCase& loads, const Vector& d, int points,
                    std::vector<ElementResult>& out, std::string* error = nullptr);

struct AnalysisResult2D
{
    bool success = false;
    std::string message;
    std::vector<std::string> log;
    std::string title;               ///< chargement calculé (« cas 1 — G », « combinaison ELU »)
    Model2D model;
    LoadCase loads;                  ///< chargement effectif
    DofNumbering dofs;
    BoundaryConditions bc;
    Matrix K;
    Vector F;
    ReducedSystem reduced;
    DisplacementSolve solve;
    Vector d, R;
    Vector springs;                  ///< forces des ressorts sur la structure, −k·u (n)
    std::vector<ElementResult> elements;
    EquilibriumCheck equilibrium;

    std::array<double, 3> nodeDisplacement(int nodeId) const;
    /// Réaction d'appui (composantes nulles sur les DDL libres et les rotations éliminées).
    std::array<double, 3> nodeReaction(int nodeId) const;
    /// Force des ressorts du nœud (Fx, Fy en kN, Mz en kN·m), nulle sans ressort.
    std::array<double, 3> nodeSpringForce(int nodeId) const;
    const ElementResult* element(int id) const;
    bool operator==(const AnalysisResult2D&) const = default;
};

struct SolveOptions
{
    numerics::SolverMethod method = numerics::SolverMethod::Cholesky;
    int diagramPoints = 21;
};

/// Méthode des déplacements complète (étapes 1 à 8), sans exception : échec → success = false et message.
AnalysisResult2D solveLinearStatic(const Model2D& model, const LoadSelection& selection, const SolveOptions& options = {});

/// Assemble un résultat à partir des étapes déjà calculées (chemin pas à pas des graphes) : éléments, bilan.
bool assembleResult(const Model2D& model, const LoadCase& loads, const DofNumbering& dofs, const BoundaryConditions& bc,
                    const Matrix& K, const Vector& F, const Vector& d, int diagramPoints, AnalysisResult2D& out,
                    std::string* error = nullptr);

/// Écarts entre deux résultats (même modèle) : max|a − b| / max|référence| sur déplacements, réactions, efforts.
struct ResultComparison
{
    bool comparable = false;
    std::string message;
    double displacementDeviation = 0.0, reactionDeviation = 0.0, forceDeviation = 0.0;
    double maxDeviation() const;
};
ResultComparison compareResults(const AnalysisResult2D& a, const AnalysisResult2D& reference);

} // namespace tsalab::structural
