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

/// OpenSees en ossature plane (processus externe : script Tcl 2D, elasticBeamColumn / truss / zeroLength),
/// second solveur de référence du banc de validation (validation croisée entre logiciels indépendants).
/// Les efforts d'extrémité viennent d'OpenSees ; courbes et valeurs caractéristiques sont calculées par le
/// même post-traitement que MetDeDeplacement (équilibre exact du tronçon, ligne élastique intégrée).
/// Non pris en charge (refus explicite) : rotules d'extrémité de barres fléchies, charges réparties partielles
/// ou trapézoïdales sur une barre fléchie. Pas d'export du système K·U = F.
class OpenSeesPlanarSolver final : public ISolver
{
public:
    explicit OpenSeesPlanarSolver(std::string executable = std::string());
    std::string name() const override;
    std::string version() const override;
    Output solve(const Input& input) override;
    Features features() const override { return { true, true, false, false }; }
    bool available(std::string* why = nullptr) const override;

private:
    std::string m_executable;
    mutable std::string m_version;
    mutable bool m_versionProbed = false;
};

/// Exécutable OpenSees utilisé par défaut : setOpenSeesExecutable(), sinon variable d'environnement
/// TSALAB_OPENSEES, sinon chemin trouvé à la compilation (dépôts de développement) ; vide si inconnu.
std::string openSeesExecutable();
/// Fixé par l'application (ex. TSA : chemin configuré dans OpenSeesManager).
void setOpenSeesExecutable(const std::string& path);

/// Solveurs disponibles, dans l'ordre de préférence (le premier est le solveur par défaut).
std::vector<std::unique_ptr<ISolver>> createBuiltInSolvers();

} // namespace tsalab::planar
