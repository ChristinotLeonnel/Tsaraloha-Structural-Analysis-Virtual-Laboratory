#pragma once

// Blueprints d'exemple de TSALab (menu Blueprint ▸ Exemples, tests du laboratoire).
// Construits avec la bibliothèque standard (TSA::Blueprint::NodeLibrary) : uniquement des nœuds réels,
// dont les commandes du registre central — exécutés, ils créent de vrais objets du modèle.

#include "Blueprint/BlueprintGraph.h"

#include <string>
#include <vector>

namespace TSALab::Research::BlueprintExamples
{

struct Info
{
    std::string id;
    std::string title;
    std::string description;
};

const std::vector<Info>& catalog();

/// Remplit `graph` avec l'exemple `id` (faux si inconnu).
bool build(const std::string& id, TSA::Blueprint::Graph& graph);

} // namespace TSALab::Research::BlueprintExamples
