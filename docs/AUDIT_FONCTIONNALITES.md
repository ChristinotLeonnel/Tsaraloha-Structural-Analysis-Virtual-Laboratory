# Inventaire des fonctionnalités de TSALab

État au 17/10/2026 (branche `feature/blueprint-structurel`, mission « Terminer toutes les fonctionnalités incomplètes »).
Une fonctionnalité n'est notée **faite** que si son code existe et qu'un test ou un essai de l'application réelle le
prouve ; la colonne Preuve cite le test (TSA : `tests/`, TSALab : `lab/Tests`, science : `science/tests`).

Légende : **fait** · **partiel** (ce qui manque est dit) · **non fait** (cause dite).

## 1. Application et espaces de travail

| Fonctionnalité | État | Fichiers | Dépendances | Actions | Preuve |
| :--- | :--- | :--- | :--- | :--- | :--- |
| Fenêtre IDE sur les bibliothèques partagées de TSA (aucune copie) | fait | `lab/LabApp/LabMainWindow.*` | TSA `src/` (couches model < graphics < widgets) | menus Fichier, Édition, Affichage, Modèle, Maillage, Blueprint, Analyse, IA, Panneaux, Aide | L4 à L7 ; `check_layers` 0 violation |
| Exemples de modèles (.tsalab), enregistrement et relecture | fait | `lab/Research/Examples/ExampleModels.*` | IO de TSA | Fichier ▸ Exemples | L4 |
| Import d'un projet TSA (.tsa) dans l'espace Modèle | fait | `LabMainWindow.cpp` | `TSAProjectIO` | Fichier ▸ Ouvrir | L5 |
| Analyse (gestionnaire partagé, moteurs Custom2D et OpenSees), SOLVER LAB | fait | TSA `src/Analysis/*`, `lab/LabUI/SolverLabPage.*` | OpenSees (processus externe) | Analyse ▸ Calculer (F9), SOLVER LAB | L7 |
| Laboratoire de validation (banc partagé, références externes) | fait | `lab/LabUI/ValidationPage.*` | banc de TSA (`tests/benchmarks`) | espace Validation | L9 |
| Journal d'expériences, rejeu, première expérience complète | fait | `lab/Research/Experiments/*`, `lab/LabUI/ExperimentsPage.*` | — | espace Expériences | L10, L11 |
| Laboratoire matriciel (K, F, U, DDL, transformations) | fait | `lab/Research/*` | Custom2D | Analyse ▸ Données d'analyse | L12 |
| Maillage (Gmsh), convergence, plaques (MITC4 contre Mindlin) | fait | TSA `src/Mesh/*`, menu Maillage | Gmsh (SDK, GPL), OpenSees | Maillage ▸ … | L13, L14 |
| Plugins (DLL), assistant IA, pont MCP | fait | TSA `src/Plugins`, `src/AI`, `plugins/sample` | modèle de langage configuré (IA) | Aide ▸ Plugins chargés, IA | L8 ; IA : vérification par l'interface avec un modèle configuré non refaite |

## 2. Blueprint (moteur et éditeur partagés avec TSA)

| Fonctionnalité | État | Fichiers | Preuve |
| :--- | :--- | :--- | :--- |
| Graphe typé (exécution, données, objets), validation, profil, débogueur, tâche de fond | fait | TSA `src/Blueprint/*`, `src/UI/Blueprint/*` | TSA 193 à 199 |
| Valeurs objet, réutilisation des résultats, variables, commentaires, copier-coller, fichier v2 | fait | idem | TSA 391 |
| **Sous-graphes réutilisables** : encapsulation d'une sélection, entrées / sorties typées, nom, description, catégorie, documentation, valeurs par défaut, entrées obligatoires, ouverture et modification du corps, mise à jour des instances (versions), contrôles (récursion, ports incompatibles, entrées requises, fin non atteinte), fichier v3, `.tsbps`, copier-coller entre graphes, import d'une définition présente (remplacée, signalée, annulable) | fait | TSA `src/Blueprint/BlueprintSubgraph.*`, `BlueprintFile.*`, `src/UI/Blueprint/BlueprintEditor.*`, `BlueprintSubgraphDialog.*` | TSA 392, 393 ; L28 |
| Réutilisation d'une instance de sous-graphe après modification de sa définition | fait (corrigé le 17/10) | `NodeDefinition::revision` | TSA 392 |
| Points d'arrêt à l'intérieur d'une instance exécutée | non fait (choix) | — | documenté : le corps se débogue exécuté seul |

## 3. Programmation structurelle (TSALab Blueprint)

| Fonctionnalité | État | Fichiers | Preuve |
| :--- | :--- | :--- | :--- |
| Élément `FrameElement2D` à 6 DDL (k', T, k = Tᵀk'T, efforts d'encastrement) | fait | `science/…/FrameElement2D.*` | S7 |
| Charges : nodales, réparties (uniformes à multi-segments, locales ou globales, projetées), ponctuelles, moments, poids propre, thermiques, déformations initiales, déplacements imposés | fait | `Frame2D.*`, `FrameElement2D.*` | S8 à S13, L20 |
| Méthode des déplacements pas à pas (K = Σ A_eᵀk_eA_e, conditions aux limites, résolution, réactions, équilibre, efforts) | fait | `DirectStiffness2D.*`, `lab/Research/Structural/*Nodes.cpp` | S11 à S16, L16, L21 |
| Diagnostic des mécanismes et du conditionnement (aucune rigidité ajoutée) | fait | `DirectStiffness2D.cpp` | S14, L18 |
| Rotules d'extrémité (condensation statique), rotations libres éliminées | fait | `FrameElement2D.*`, `DirectStiffness2D.*` | S17, L23, L24 |
| Appuis élastiques (ressorts de translation et de rotation) | fait | idem | S18, L23, L24 |
| Bibliothèque de 131 blocs en 8 catégories (A à G, expérimental), documentés, sources présentes | fait | `lab/Research/Structural/*` | L15 |
| Exemples : méthode pas à pas, méthode en six sous-graphes, classe FrameElement2D, solveurs de l'étudiant, mécanisme, tassement, rotule et ressort, dynamique, charges roulantes | fait | `lab/Research/Examples/StructuralGraphs.*` | L16 à L28 |
| Mode 2 : gradient conjugué en blocs ; Jacobi et **Gauss-Seidel** (composants expérimentaux) | fait | `ControlNodes.cpp` (`gaussSeidelSweep`) | L19 (Gauss-Seidel : 954 itérations, ≡ Cholesky) |
| Comparaison de deux méthodes de résolution sur un même système | fait (bloc) | `MatrixNodes.cpp` | L25 |
| Vues : matrices, diagrammes, portique et déformée | fait | `lab/LabUI/StructuralViewers.*` | TSALab réel (13/10) |

## 4. Dynamique, sismique, trafic (modules pédagogiques)

| Fonctionnalité | État | Fichiers | Preuve |
| :--- | :--- | :--- | :--- |
| Masses (cohérente, concentrée), analyse modale | fait | `science/…/Dynamics2D.*` | S19, L23 |
| Intégration temporelle de Newmark, amortissement de Rayleigh | fait | idem | S20, L23 |
| Accélérogramme **fourni par l'utilisateur** (fichier jamais modifié), spectre de réponse calculé | fait | idem, `DynamicsNodes.cpp` | S20, L25 |
| Analyse spectrale (spectre saisi, SRSS / CQC) | fait | idem | S21, L23 |
| Charges roulantes : lignes d'influence, enveloppes (calculs statiques successifs) | fait | `MovingLoads2D.*` | S22, L23 |
| Accélérogrammes ou spectres réglementaires intégrés | non fait (volontaire) | — | aucune donnée sismique n'est fabriquée ; les coefficients normatifs ne sont jamais inventés |
| Vibration couplée véhicule-pont | non fait (hors du périmètre implémenté) | — | le journal précise « aucune interaction véhicule-pont » (L23) |
| Dynamique dans le calcul de production de TSA | non fait (ADR-022) | — | statique seul dans TSA ; modules pédagogiques dans TSALab |

## 5. Import d'un projet TSA vers un portique plan

| Fonctionnalité | État | Fichiers | Preuve |
| :--- | :--- | :--- | :--- |
| Import par le chemin d'analyse 2D de TSA (portées planes, appuis, ressorts, rotules, charges, cas, combinaisons, poids propre), traçabilité (fichier, SHA-256, format, projet, portée, date), source jamais écrite | fait | `lab/Research/Structural/TsaImport.*` | L27 (14 portées importées, 4 refusées avec la raison, 13 comparées au solveur 2D de TSA à 1e-9) |
| Refus motivés : fichier absent, non TSA, tronqué, version future, chiffré, portée inconnue ou non plane, câbles | fait | idem | L27 |
| Annulation | fait | idem | L27 |
| Bloc `struct.model_import_tsa` | fait | `ModelNodes.cpp` | L15, L29 |
| **Boîte d'aperçu** (fichier, portée, aperçu en tâche de fond annulable, rapport importé / converti / ignoré, choix du cas, graphe d'analyse) | fait | `lab/LabUI/TsaImportDialog.*`, `StructuralGraphs::buildTsaImport` | L29 (graphe : projet lu une fois, ≡ calcul direct) ; essai de l'interface (UIA) : § 7 |
| Dalles, voiles, éléments hors du plan | non fait (hors du modèle plan) | — | ignorés et listés dans le rapport, jamais convertis en silence |

## 6. Persistance

| Fonctionnalité | État | Fichiers | Preuve |
| :--- | :--- | :--- | :--- |
| Graphes `.tsbp` (v2, v3 avec sous-graphes), versions des nœuds, relecture à l'identique | fait | TSA `BlueprintFile.*` | L17, L26, L28 ; TSA 391, 392 |
| Sous-graphes `.tsbps` (dépendances comprises) | fait | idem | TSA 392, 393 |
| Paramètres des solveurs (méthode, points, tolérances) | fait | valeurs saisies des blocs, enregistrées avec le graphe | L17, L26 |
| **Modèle et résultats en JSON** (`tsalab-model2d`, `tsalab-result2d`, version 1) : relecture à l'identique, NaN et ±∞, lecture stricte avec le chemin du champ en erreur, blocs Enregistrer / Lire | fait | `lab/Research/Structural/StructuralJson.*`, `ControlNodes.cpp` | L30 (4 modèles, 5 résultats dont un mécanisme) |

## 7. Construction, tests et essais de l'interface

| Vérification | Résultat |
| :--- | :--- |
| TSA, Debug : `TSA_TestSuite` | 393 / 393 |
| TSALab, Debug : science | 22 / 22 |
| TSALab, Debug : laboratoire (L4 à L30) | 27 / 27 |
| Couches (ADR-024) | `check_layers` : 0 violation |
| Release (TSA, TSALab) et essais de l'interface (UIA) | voir le rapport final de la mission (`TSALab/.claude/changelog.md`, 17/10/2026) |

## 8. Ce qui reste incomplet, et pourquoi

| Sujet | Cause |
| :--- | :--- |
| Accélérogrammes et spectres réglementaires | aucune donnée ne doit être fabriquée ; à fournir par l'utilisateur |
| Interaction véhicule-pont | modèle couplé non spécifié ; seule la succession de calculs statiques est implémentée et annoncée comme telle |
| Grands systèmes dans les blocs | matrices pleines (pédagogie) ; OpenSees reste le moteur des grands modèles |
| Points d'arrêt dans une instance de sous-graphe exécutée | choix : un corps se débogue exécuté seul (essai) |
| Page « Comparer » (plusieurs moteurs sur un modèle du projet) | feuille de route P1 ; le bloc de comparaison de solveurs existe (L25) |
| IA de bout en bout | nécessite un modèle de langage configuré sur le poste |
