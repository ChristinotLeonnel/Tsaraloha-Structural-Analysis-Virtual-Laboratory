#pragma once

// TSALab — banc de validation des solveurs d'ossatures planes (C++ pur).
//
// Chaque benchmark est un problème à solution analytique connue (RDM). Il est vérifié de trois façons :
//   A. résultat du solveur (déplacements, réactions, moments) ;
//   C. référence analytique (formule) ;
//   B. validation croisée : le système K·U = F exporté par le solveur est résolu à nouveau par les
//      solveurs instrumentés du laboratoire (Cholesky) ; U doit coïncider avec celui du solveur.
// Un solveur est validé si A ≈ C pour toutes les grandeurs et A ≈ B sur tout le système.
// Unités : kN, m, kPa.

#include "tsalab/planar/PlanarSolver.h"

#include <functional>
#include <string>
#include <vector>

namespace tsalab::validation
{

struct Check
{
    std::string label;                                  ///< « flèche en bout »
    std::string formula;                                ///< « −PL³/3EI »
    double expected = 0.0;
    std::function<double(const planar::Output&)> computed;
    double relativeTolerance = 1e-6;
};

struct PlanarBenchmark
{
    std::string id;
    std::string title;
    std::string description;
    planar::Input input;
    std::vector<Check> checks;
};

/// Problèmes de référence, dans l'ordre de présentation.
const std::vector<PlanarBenchmark>& planarBenchmarks();

struct CheckResult
{
    std::string label;
    std::string formula;
    double expected = 0.0;
    double computed = 0.0;
    double relativeError = 0.0;
    bool passed = false;
};

struct BenchmarkReport
{
    std::string id;
    std::string title;
    std::string solver;
    bool solved = false;
    bool skipped = false;               ///< solveur indisponible sur ce poste (ex. OpenSees absent) : ni validé ni en échec
    std::string message;
    std::vector<CheckResult> checks;
    bool crossChecked = false;          ///< système exporté et résolu à nouveau
    double crossDeviation = -1.0;       ///< max|U_labo − U_solveur| / max|U_solveur|
    bool passed = false;
};

/// Exécute un benchmark sur un solveur (le système est demandé si le solveur sait l'exporter).
BenchmarkReport runBenchmark(const PlanarBenchmark& benchmark, planar::ISolver& solver);

/// Rapport lisible (console, journal).
std::string formatReport(const std::vector<BenchmarkReport>& reports);

} // namespace tsalab::validation
