#pragma once

// TSALab — solveurs d'ossatures planes fournis par le laboratoire.
// Ajouter un solveur : implémenter tsalab::planar::ISolver (PlanarSolver.h) et l'ajouter à
// createBuiltInSolvers() ; TSA (moteur « custom2d ») et TSALab le proposent sans autre modification.

#include "tsalab/planar/PlanarSolver.h"

#include <memory>
#include <vector>

namespace tsalab::planar
{

/// Méthode des déplacements (bibliothèque MetDeDeplacement) : combine les cas de charge selon la
/// demande, ajoute le poids propre, résout, convertit les résultats au contrat (indices, signes).
class MetDeDeplacementSolver final : public ISolver
{
public:
    std::string name() const override;
    std::string version() const override;
    Output solve(const Input& input) override;
    Features features() const override { return { true, true, true, true }; }
};

/// Solveurs disponibles, dans l'ordre de préférence (le premier est le solveur par défaut).
std::vector<std::unique_ptr<ISolver>> createBuiltInSolvers();

} // namespace tsalab::planar
