#pragma once

// Pont interne contrat tsalab::planar ↔ bibliothèque MetDeDeplacement (mdd), partagé par les solveurs du
// laboratoire : le chargement demandé (cas, combinaison, poids propre) est réduit UNE fois en charges
// locales de la bibliothèque, et les courbes / valeurs caractéristiques d'une barre sont calculées par le
// même post-traitement (mdd::computeCurves) quel que soit le solveur qui a fourni les efforts d'extrémité.
// Deux solveurs différents sont donc comparés sur leurs seules différences de résolution.

#include "tsalab/planar/PlanarSolver.h"

#include <mdd/MetDeDeplacement.h>

#include <vector>

namespace tsalab::planar::detail
{

struct MddBridge
{
    mdd::Model model;                 ///< charges combinées (facteurs appliqués), poids propre compris
    std::vector<int> nodeIndex;       ///< indice du contrat (1..N) → nœud de la bibliothèque (0-based), -1 sinon
    std::vector<int> memberOf;        ///< barre de la bibliothèque → indice du contrat
    std::vector<int> memberIndex;     ///< indice du contrat → barre de la bibliothèque, -1 sinon
    std::vector<ElementType> memberType;   ///< type de chaque barre de la bibliothèque
};

/// Construit le modèle de la bibliothèque à partir de l'entrée (charges de la demande seulement).
MddBridge buildMddModel(const Input& in);

/// Efforts d'extrémité, stations et valeurs caractéristiques d'une barre, au format du contrat.
ElementForces toContract(int contractIndex, const mdd::MemberResult& mr);

} // namespace tsalab::planar::detail
