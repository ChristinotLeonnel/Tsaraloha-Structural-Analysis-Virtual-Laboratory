#pragma once

// Modèles d'exemple de TSALab (Start Center ▸ Exemples, tests du laboratoire).
//
// Chaque exemple est un vrai modèle TSA::Model::Model (nœuds, barres, appuis, cas de charge),
// construit par l'API du modèle : il s'ouvre dans le workspace comme n'importe quel projet et sert
// de support aux expériences (solution analytique connue indiquée dans `reference`).
// Unités : m, kN, kN/m ; matériaux et sections de la bibliothèque (Material, Section).

#include <string>
#include <vector>

namespace TSA::Model
{
class Model;
}

namespace TSALab::Research::Examples
{

struct ExampleInfo
{
    std::string id;          ///< identifiant stable (« cantilever », « simply-supported »…)
    std::string title;       ///< libellé affiché
    std::string description;
    std::string reference;   ///< solution de référence (formule)
};

/// Exemples disponibles, dans l'ordre d'affichage.
const std::vector<ExampleInfo>& catalog();

/// Remplit `model` (supposé vide) avec l'exemple `id`. Faux si l'identifiant est inconnu.
bool build(const std::string& id, TSA::Model::Model& model);

} // namespace TSALab::Research::Examples
