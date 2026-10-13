#pragma once

// TSALab Blueprint — Structural Programming Laboratory : bibliothèque de composants pré-codés de la programmation
// structurelle, ajoutée à la bibliothèque de nœuds de l'éditeur Blueprint partagé (TSA::Blueprint::NodeLibrary).
//
//   A. Modèle structurel   : Node2D, Material, Section2D, SupportCondition, FrameElement2D, StructuralModel, charges,
//                            LoadCase, LoadCombination, chargement à calculer, portique de démonstration
//   B. Élément fini        : ElementGeometry, LocalStiffnessMatrix, TransformationMatrix, GlobalElementStiffness,
//                            EquivalentNodalLoads, ElementDisplacement, ElementEndForces, InternalForceDiagram, A_e
//   C. Assemblage          : GlobalDOFNumbering, GlobalStiffnessAssembly, GlobalLoadVectorAssembly, BoundaryConditions,
//                            ReducedSystem, résolution, DisplacementRecovery, ReactionRecovery, équilibre, calcul direct
//   D. Mathématiques       : matrices et vecteurs (opérations, extraction, résolution, LU, inversion contrôlée, résidu,
//                            conditionnement, dimensions, export CSV)
//   E. Contrôle, affichage : boucles, sélection et filtre d'éléments, comparaisons, courbes, visualisation, export
//   Expérimental           : laboratoire de l'étudiant (itérations de Jacobi, Gauss-Seidel), séparé des composants validés
//
// La logique scientifique est celle du cœur scientifique (tsalab::structural), testée par tsalab_science_tests (S7 à
// S16) ; les blocs sont testés par les tests du laboratoire (TSALab_TestSuite).

#include "Blueprint/BlueprintRuntime.h"

#include <string>
#include <vector>

namespace TSALab::Research::Structural
{

/// Ajoute les composants à la bibliothèque (une seule fois : faux s'ils y sont déjà). À appeler au démarrage, avant
/// la construction de l'éditeur (palette) et avant toute exécution.
bool registerStructuralNodes(TSA::Blueprint::NodeLibrary& library);

/// Identifiants des composants de la programmation structurelle présents dans une bibliothèque.
std::vector<std::string> structuralNodeIds(const TSA::Blueprint::NodeLibrary& library);

} // namespace TSALab::Research::Structural
