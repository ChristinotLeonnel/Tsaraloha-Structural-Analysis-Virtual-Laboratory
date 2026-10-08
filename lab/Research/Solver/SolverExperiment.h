#pragma once

// Expérience « SOLVER LAB » : rejouer la résolution K·U = F du dernier calcul avec les solveurs
// instrumentés du cœur scientifique (tsalab::numerics : Gauss LU, Cholesky, gradient conjugué) et les
// comparer à la solution du moteur (OpenSees).
//
// Ce fichier n'est que l'ADAPTATEUR entre les résultats d'un calcul du modèle partagé
// (TSA::Analysis::ResultsModel) et le problème linéaire du cœur scientifique ; la science est dans
// TSALab/science (numerics/SolverComparison.h).
//
// Données : matrice de rigidité globale K (DDL libres) et déplacements U extraits par OpenSees en
// mode ADVANCED. Le second membre est reconstruit : F = K·U, soit les efforts nodaux équivalents
// réellement vus par le solveur. Aucune donnée n'est inventée : sans K extraite, l'expérience est
// indisponible.

#include "tsalab/numerics/SolverComparison.h"

#include <string>

namespace TSA::Analysis
{
class ResultsModel;
}

namespace TSALab::Research
{

namespace LinAlg = tsalab::numerics::LinAlg;
using tsalab::numerics::Matrix;
using tsalab::numerics::SolverMethod;
using tsalab::numerics::SolverReport;
using tsalab::numerics::SolverRun;
using tsalab::numerics::SolverSettings;
using tsalab::numerics::Vector;
using tsalab::numerics::conditionNumber;
using tsalab::numerics::runSolver;
using tsalab::numerics::solverMethodName;

struct SolverExperimentInput : tsalab::numerics::LinearProblem
{
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

} // namespace TSALab::Research
