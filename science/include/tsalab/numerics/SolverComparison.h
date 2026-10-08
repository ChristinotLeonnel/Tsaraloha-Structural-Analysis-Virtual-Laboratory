#pragma once

// TSALab — comparaison de solveurs sur un même système K·x = f (C++ pur).
// Sert au SOLVER LAB (système d'un calcul réel) et à la validation croisée des moteurs (système exporté
// par un moteur, résolu à nouveau par les solveurs instrumentés du laboratoire).

#include "tsalab/numerics/LinearAlgebra.h"

#include <string>
#include <vector>

namespace tsalab::numerics
{

struct LinearProblem
{
    Matrix K;                         ///< matrice du système
    Vector f;                         ///< second membre
    Vector reference;                 ///< solution de référence (celle du moteur), vide si inconnue
    std::vector<std::string> labels;  ///< libellé par équation (« N12.UZ »)
};

/// Problème de référence : f = K·reference.
LinearProblem makeProblem(Matrix K, Vector reference);

struct SolverRun
{
    SolverReport report;
    Vector x;
    /// max|x − référence| / max|référence| (0 si la référence est nulle) ; -1 si échec ou pas de référence.
    double deviationFromReference = -1.0;
};

/// Résout le problème avec la méthode demandée et compare à la référence.
SolverRun runSolver(const LinearProblem& problem, SolverMethod method, const SolverSettings& base = {});

} // namespace tsalab::numerics
