#pragma once

// Expérience « SOLVER LAB » : rejouer la résolution K·U = F du dernier calcul avec les solveurs
// instrumentés du laboratoire (Gauss LU, Cholesky, gradient conjugué) et les comparer à la solution
// du moteur (OpenSees).
//
// Données : matrice de rigidité globale K (DDL libres) et déplacements U extraits par OpenSees en
// mode ADVANCED (TSA::Analysis::AdvancedResults). Le second membre est reconstruit : F = K·U, soit
// les efforts nodaux équivalents réellement vus par le solveur (charges nodales + charges réparties
// ramenées aux nœuds). Aucune donnée n'est inventée : sans K extraite, l'expérience est indisponible.

#include "Research/Numerics/LinearAlgebra.h"

#include <string>
#include <vector>

namespace TSA::Analysis
{
class ResultsModel;
}

namespace TSALab::Research
{

struct SolverExperimentInput
{
    Matrix K;                         ///< rigidité globale, DDL libres (ordre des équations OpenSees)
    Vector f;                         ///< F = K·U
    Vector reference;                 ///< U du moteur
    std::vector<std::string> labels;  ///< « N12.UZ » par équation
    std::string caseName;             ///< cas ou combinaison calculé(e)
    std::string units;                ///< unités de K (traçabilité)
};

/// Taille maximale traitée en matrice dense (n×n doubles) par le laboratoire.
inline constexpr int kMaxDenseEquations = 2000;
/// Taille maximale pour le conditionnement spectral (Jacobi, O(n³) par balayage).
inline constexpr int kMaxSpectralEquations = 400;

/// Construit l'expérience à partir des résultats d'un calcul. Faux, avec la raison, si la matrice
/// globale n'a pas été extraite (calcul en mode LIGHT, moteur sans export) ou si le système est trop grand.
bool buildSolverExperiment(const TSA::Analysis::ResultsModel& results, SolverExperimentInput& out, std::string* why);

/// Construit l'expérience directement (tests, systèmes saisis) : f = K·reference.
SolverExperimentInput makeSolverExperiment(Matrix K, Vector reference);

struct SolverRun
{
    SolverReport report;
    Vector x;
    /// max|x − U_moteur| / max|U_moteur| (0 si U nul) ; -1 si la résolution a échoué.
    double deviationFromReference = -1.0;
};

/// Résout K·x = f avec la méthode demandée et compare x à la solution du moteur.
SolverRun runSolver(const SolverExperimentInput& input, SolverMethod method, const SolverSettings& base = {});

} // namespace TSALab::Research
