# Architecture de TSALab

La décision de référence est l'ADR-024 (`TSA/docs/TSARALOHA_ARCHITECTURE.md`). Ce document la résume du point de vue
de TSALab et décrit ce qui est propre au laboratoire.

## 1. Principe

TSALab est une application distincte de TSA (fenêtre, espaces, identité, format `.tsalab`). Elle repose sur les
**mêmes sources** que TSA pour tout ce qui est commun : un objet, une représentation, une commande. TSALab ne copie
aucun fichier de TSA.

```text
TSALab.exe ─────────────── LabMainWindow, espaces, pages du laboratoire (lab/LabApp, lab/LabUI)
   │
   ├─ TSALab_Research ──── exemples, SOLVER LAB, validation, journal d'expériences (lab/Research, sans widget)
   │
   ├─ TSALab_Widgets ───┐
   ├─ TSALab_Graphics ──┤  bibliothèques compilées depuis TSA/src avec l'identité de TSALab
   ├─ TSALab_Model ─────┘  (modèle, commandes, IO, analyse, résultats, vue OCCT, panneaux, Blueprint…)
   │
   └─ tsalab_science ───── cœur scientifique C++ pur (science/), utilisé aussi par TSA (moteur Custom2D)
```

Couches (contrôlées par `TSA/tools/check_layers.py`, un test CTest de TSA) :

| Couche | Règle |
| :--- | :--- |
| `tsalab_science` | ni Qt, ni OpenCASCADE, ni modèle TSA : il reçoit un modèle d'analyse neutre |
| modèle (`*_Model`) | aucun widget, aucune inclusion graphique |
| graphique (`*_Graphics`) | n'inclut pas l'interface d'une application |
| widgets (`*_Widgets`) | composants d'interface partagés, sans fenêtre d'application |
| application | `MainWindow`, ruban et `AppShell` pour TSA ; `LabMainWindow` pour TSALab |

Le 12/10/2026, une violation existante a été corrigée (formule de contrainte de fibre déplacée dans la couche
modèle) : 0 violation.

## 2. Ce qui est propre à TSALab

| Dossier | Contenu | Bibliothèque |
| :--- | :--- | :--- |
| `product/` | identité : nom, version, extension `.tsalab`, signature, réglages, CLSID de l'extension Explorateur | toutes |
| `lab/Research/Examples` | modèles et Blueprints d'exemple à solution analytique connue | `TSALab_Research` |
| `lab/Research/Solver` | SOLVER LAB : problème `K·U = F` d'un calcul → solveurs instrumentés de `tsalab_science` | `TSALab_Research` |
| `lab/Research/Validation` | exécution du banc de validation partagé avec TSA ; étude de convergence en maillage (`MeshConvergence`) | `TSALab_Research` |
| `lab/Research/Reference` | références analytiques appliquées au modèle de l'utilisateur, hypothèses vérifiées (`AnalyticalReference`) | `TSALab_Research` |
| `lab/Research/Experiments` | journal d'expériences : traçabilité, référence, rejeu | `TSALab_Research` |
| `lab/Research/Structural` | TSALab Blueprint — programmation structurelle : objets typés (matrices, modèle, éléments, charges, résultats), blocs pré-codés (catégories A à E, expérimental), modèles de démonstration, tableau des résultats (`BLUEPRINT_STRUCTUREL.md`) | `TSALab_Research` |
| `lab/LabUI` | pages Accueil, SOLVER LAB, Validation, Expériences ; vues des objets de la programmation structurelle (matrice, diagrammes, portique `Frame2DView`) | exécutable |
| `lab/LabApp` | fenêtre et câblage des espaces | exécutable |
| `science/` | numerics, solveurs plans, MetDeDeplacement, benchmarks plans ; `structural/` : `FrameElement2D` (6 DDL) et méthode des déplacements étape par étape | `tsalab_science` |

La logique scientifique est toujours séparée de l'interface (`Research`, `science`) pour être testée sans fenêtre.

## 3. Points d'extension partagés ajoutés pour le laboratoire

| Composant (dépôt TSA) | Rôle | Utilisé par |
| :--- | :--- | :--- |
| `UI/Display/DisplayMenus` | échelles des annotations et des charges, profils d'affichage, diagnostic | TSA et TSALab |
| `AnalysisController::runCompleted` | fin de tout calcul (tâche de fond ou synchrone) avec le contexte calculé | journal d'expériences |
| `Validation/BenchmarkSuite` | banc de validation (cas JSON, références analytiques ou externes, rapports) | TSA (`TSA_Benchmark`), TSALab (espace Validation) |
| `Analysis/Engine/AnalysisEngine` | interface des moteurs de calcul et de leurs capacités | voir `SOLVER_API.md` |
| `Analysis/ResultsExport` | exports des données d'analyse, dont les vecteurs F/U et les transformations local / global | dock « Données d'analyse » (TSA et TSALab) |
| `Validation/BenchmarkSuite` : `CheckOutcome::convergence` | série de convergence en maillage structurée | espace Validation, rapport JSON |
| `Automation/CommandRegistry` | commandes de propriétés, de lecture d'efforts et de comparaison | console, Blueprint, MCP (TSA et TSALab) |
| `UI/Mesh/MeshAnalysisBridge`, `AnalysisManagerPanel::setContextPreparer` | maillage du projet transmis au calcul (contrôles de compatibilité, choix explicite si obsolète) | fenêtres de TSA et de TSALab |

## 4. Séparation des données

| Nature | Où |
| :--- | :--- |
| modèle physique et analytique | `TSA::Model::Model`, `BimModel` (identifiants stables, liens physique ↔ analytique) |
| modèle de maillage | `FeMesh` (TSA), généré par Gmsh (`MeshController`, partagé) ; menu Maillage de TSALab ; transmis au calcul par `UI/Mesh/MeshAnalysisBridge` (partagé avec TSA) |
| données du solveur | `AnalysisModel`, scripts et fichiers du moteur (OpenSees) |
| résultats | `ResultsModel`, indexé par identifiants TSA ; jamais enregistré dans le projet |
| références analytiques | `ReferenceComparison`, enregistrée dans l'expérience ; jamais dans le projet |
| traçabilité | journal d'expériences (`<projet>.experiments/`), à côté du projet |

## 5. Compatibilité des fichiers

TSALab lit les projets TSA (`.tsa`, jamais réécrits) et enregistre au format `.tsalab` (même structure, signature
propre). Les profils d'affichage et le journal d'expériences ne modifient pas le format des projets.

## 6. Programmation visuelle : les nœuds du laboratoire

Il n'y a pas d'éditeur propre au laboratoire. Les nœuds sont ceux de `TSA::Blueprint::NodeLibrary` :
- les nœuds standard (paramètres, mathématiques, logique, flux) ;
- **un nœud par commande du registre central**, généré à partir de la commande (broches typées, unités). Le test 193
  de TSA vérifie que chaque commande est un nœud.

Ajouter une commande, c'est donc ajouter un nœud, qui est aussi disponible dans la console et le serveur MCP.

| Nœud demandé | Nœud réel | État |
| :--- | :--- | :--- |
| charger un modèle | — | **manque** : recharger un projet dans la session pendant l'exécution d'un graphe invaliderait les vues ouvertes ; on ouvre le projet, puis on exécute le graphe |
| sélectionner | — | **manque** : la sélection appartient à la couche graphique ; les nœuds désignent les objets par identifiant (sorties `id` des nœuds de création) |
| affecter des propriétés | `cmd.model.set_section`, `cmd.model.set_support` | fait |
| appliquer des charges | `cmd.loads.create_case`, `cmd.loads.add_nodal`, `cmd.loads.add_uniform` | fait |
| mailler | — | **manque** : le maillage existe dans l'interface (menu Maillage) mais pas encore comme commande du registre |
| lancer une analyse | `cmd.analysis.run` (moteur, axe, système, poids propre) | fait |
| extraire des résultats | `cmd.results.summary`, `node_displacement`, `node_reaction`, `member_forces`, `cmd.query.bar` | fait |
| comparer des valeurs | `cmd.compare.values` (tolérance mixte ; écart relatif absent si réf. ≈ 0) ; `math.compare` | fait |
| exporter un rapport | `science.validation_bench` (rapport du banc dans le journal) | **partiel** : pas de rapport d'expérience exporté par un nœud |

Exemple complet : Blueprint « Expérience : poutre bi-appuyée » (`lab/Research/Examples/BlueprintExamples.cpp`, test L6).

### Programmation structurelle (TSALab Blueprint)

Le laboratoire ajoute à la même bibliothèque, au démarrage (`registerStructuralNodes`, `lab/LabApp/main.cpp`), 104
blocs de calcul des structures : catégories « Structure A » (modèle, charges) à « Structure E » (contrôle, affichage)
et « Expérimental ». Ils transportent des **objets typés** (valeurs `ObjectValue` du moteur partagé :
`FrameElement2D`, `Matrix`, `StructuralModel`, `SolverResult`…), contrôlés à la connexion. La science est celle de
`science/structural` (C++ pur, tests S7 à S16) ; les graphes d'exemple (`Methode_Des_Deplacements_2D`, classe
`FrameElement2D`, solveur de l'étudiant, mécanisme, tassement) sont dans `lab/Research/Examples/StructuralGraphs.*`
(tests L15 à L22). Ces blocs n'utilisent pas le projet : leurs graphes s'exécutent en tâche de fond, et le bloc
« Visualiser » publie le résultat dans le dock « Résultats 2D » par `ResultBoard` (sûr entre threads). Détail :
`BLUEPRINT_STRUCTUREL.md`.
