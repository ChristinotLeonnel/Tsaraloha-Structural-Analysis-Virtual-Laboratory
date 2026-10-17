#pragma once

// TSALab Blueprint — graphes d'exemple de la programmation structurelle (menu Blueprint ▸ Exemples, tests).
// Construits avec les blocs pré-codés (Research/Structural) : à enregistrer dans la bibliothèque standard
// (registerStructuralNodes) avant de les construire.
//
//   Methode_Des_Deplacements_2D : méthode de rigidité directe pas à pas sur le portique de démonstration (étapes 1 à 9,
//                                 K = Σ A_eᵀ·k_e·A_e calculée dans une boucle), comparée au calcul direct ;
//   méthode en sous-graphes     : la même méthode encapsulée en six sous-graphes réutilisables (modèle, assemblage,
//                                 conditions aux limites, résolution, réactions et efforts, équilibre) ;
//   classe FrameElement2D       : utilisation directe de la classe (mode 1) : matrices et charges équivalentes ;
//   solveur de l'étudiant       : gradient conjugué écrit avec des blocs mathématiques (mode 2), comparé à la solution
//                                 de référence ;
//   mécanisme                   : système instable, diagnostic du mécanisme ;
//   tassement                   : déplacement imposé non nul, réaction comparée à −6EIΔ/L³ ;
//   rotules-ressorts            : poutre Gerber construite par blocs, rotule d'extrémité et appui élastique ;
//   analyse-modale, newmark-echelon, analyse-spectrale : dynamique pédagogique sur un oscillateur à 1 DDL, comparée
//                                 aux solutions analytiques ;
//   charges-roulantes           : convoi d'essieux (théorème de Barré), ligne d'influence.

#include "Blueprint/BlueprintRuntime.h"

#include <string>
#include <vector>

namespace TSALab::Research::StructuralGraphs
{

struct Info
{
    std::string id;
    std::string title;
    std::string description;
};

const std::vector<Info>& catalog();

/// Remplit `graph` avec l'exemple `id` ; faux (et *error) si l'exemple est inconnu ou si un bloc manque.
bool build(const std::string& id, TSA::Blueprint::Graph& graph, std::string* error = nullptr);

/// Graphe d'analyse d'un portique importé d'un projet TSA (boîte « Importer un portique d'un projet TSA ») : bloc
/// d'import (fichier, portée), modèle gardé dans une variable (le projet est lu une seule fois par exécution), contrôle
/// des données, calcul direct du cas `loadCase` ou de la combinaison `combination` (> 0, prioritaire), visualisation.
bool buildTsaImport(const std::string& path, const std::string& scope, int loadCase, int combination, TSA::Blueprint::Graph& graph,
                    std::string* error = nullptr);

} // namespace TSALab::Research::StructuralGraphs
