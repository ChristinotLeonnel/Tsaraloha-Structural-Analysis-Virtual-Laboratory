# Audit TSA / TSALab — 12/10/2026

Point de départ de la mission « TSALab, laboratoire opérationnel ». Toutes les constatations ci-dessous viennent du
code, des builds et des tests exécutés ce jour ; rien n'est déduit des noms de fichiers seuls.

## 1. Dépôts présents

| Dépôt | Chemin | Branche, dernier commit | Rôle |
| :--- | :--- | :--- | :--- |
| TSA | `D:\Tsaraloha Platforme\TSA` | `main` d4296cf (12/10) | application TSA et **sources des bibliothèques partagées** (`src/`) |
| TSALab | `D:\Tsaraloha Platforme\TSALab` | `main` 9dc205d (11/10) | application TSALab, **cœur scientifique** `science/`, recherche `lab/` |
| Tsaraloha-Web | `D:\Tsaraloha Platforme\Tsaraloha-Web` | `main` b7bb1bc | site public (hors périmètre) |

## 2. Architecture constatée (ADR-024, `TSA/docs/TSARALOHA_ARCHITECTURE.md`)

TSALab **ne contient aucune copie** du code de TSA. Il compile les sources du dépôt voisin `../TSA` en trois
bibliothèques à son identité :
- `TSALab_Model` : modèle, commandes, undo, IO, analyse, résultats, note de calcul, IA, validation ;
- `TSALab_Graphics` : vue OCCT, sélection, grilles, accrochage, outils ;
- `TSALab_Widgets` : arbre, propriétés, console, panneau d'analyse, Blueprint, docks.

En sens inverse, TSA compile le cœur scientifique `TSALab/science` (C++ pur : ni Qt, ni OCCT, ni modèle TSA) pour
son moteur Custom2D. Il n'y a pas de cycle entre bibliothèques : `science` ne dépend d'aucune bibliothèque TSA.
Les couches sont contrôlées par `TSA/tools/check_layers.py`, un test CTest.

**Conséquence majeure** : toute évolution de TSA qui vit dans les bibliothèques partagées arrive dans TSALab à la
compilation. Ce qui vit dans la fenêtre de TSA (`UI_MAINWINDOW_SOURCES` : `MainWindow*`, `AppShell`, ruban) n'y
arrive pas : TSALab a sa propre fenêtre `LabMainWindow`, avec une session de projet.

## 3. Référence avant modification

| Contrôle | Résultat |
| :--- | :--- |
| Dernier build de TSALab | 11/10, 12:30 (avant les surfaces, l'audit, l'affichage et le multi-documents de TSA) |
| Compilation Debug de TSALab contre TSA d4296cf | **réussie**, sans aucune modification |
| `TSALab_TestSuite` | 5 / 5 (L4 à L8) |
| `tsalab_science_tests` | 6 / 6 (S1 à S6 ; 7 benchmarks plans, OpenSees 3.8.0 ≈ MetDeDeplacement) |
| Lancement réel de `TSALab.exe` | fenêtre ouverte, 6 espaces et 6 exemples, fermeture propre ; console attachée en Debug (voulue, comme TSA) |
| `check_layers.py` | **1 violation** : `NDC/NDCProjectNote.cpp` (modèle) incluait `Geometry/DiagramGeometry.h` (graphique) |
| TSA, suite complète | 387 / 387 en Debug et en Release (12/10) |

L'en-tête du README de TSALab annonce « 212 tests ». Il s'agit des tests communs de TSA à une date antérieure ; ils
sont aujourd'hui 387, et TSALab en a 5 propres.

## 4. Évolutions de TSA et état dans TSALab

Légende : **intégrée** = disponible dans TSALab et utilisable ; **partielle** = code partagé présent mais sans
interface ou sans test dans TSALab ; **absente** = rien dans TSALab ; **testée** = couverte par un test qui
s'exécute pour TSALab.

| Domaine | Dans TSA | Dans TSALab | Raison |
| :--- | :--- | :--- | :--- |
| Vue 3D OCCT, sélection, grilles, accrochage | testée | intégrée | bibliothèque graphique partagée |
| Plans de travail, niveaux, cotations | testée | intégrée (vue, arbre), cotations sans commandes UI | outils de cotation câblés dans la fenêtre de TSA |
| Poutres, poteaux, barres, câbles | testée | intégrée (barre d'outils Nœud / Poutre / Poteau / Barre) | modèle partagé |
| Dalles, voiles, fondations | testée | partielle : visibles dans l'arbre et la vue, pas d'outil de création | outils dans la fenêtre de TSA |
| Sections, matériaux, propriétés géométriques (corrections de l'audit) | testée | intégrée | modèle partagé |
| Charges, combinaisons, appuis | testée | partielle : modèle et commandes du registre, pas de dialogues de charge | dialogues dans la fenêtre de TSA |
| Maillage Gmsh (1D, 2D), qualité, vue du maillage | testée | **absente** de l'interface (outil `tsa-mesh` livré) | `MeshController` dans la fenêtre de TSA |
| Calcul des coques (dalles, voiles) OpenSees | testée (suite `surfaces`) | partielle : moteur partagé, mais aucun maillage transmis depuis TSALab | dépend du maillage |
| Statique linéaire, non linéaire, modale (OpenSees) | testée | intégrée (gestionnaire d'analyse partagé) | `AnalysisController` partagé |
| Custom2D / MetDeDeplacement | testée | intégrée, testée (S1 à S6, L7) | cœur scientifique de TSALab |
| Torsion, gauchissement, moments répartis | testée (suite `torsion`) | intégrée (moteur, résultats) | modèle et moteur partagés |
| Dynamique temporelle, spectrale, pushover, flambement, thermique | **absente** | absente | aucun moteur ne les déclare (`AnalysisCapabilities`) |
| Déformées, diagrammes, échelles séparées | testée | intégrée (dock Résultats, vue) | `ResultsVisualManager` partagé |
| Menus d'échelles (annotations, charges), profils d'affichage, diagnostic | testée | **absente** | menus construits dans `MainWindow_Actions.cpp` |
| Note de calcul | testée | intégrée (onglet Résultats / note) | NDC partagée |
| Copier-coller structurel | testée | absente de l'interface (classe partagée présente) | câblage dans la fenêtre de TSA |
| Onglets multi-documents | testée | **absente** : une session par fenêtre | `AppShell` propre à TSA |
| Banc de validation (27 cas, 144 vérifications) | testée (suite `benchmarks`, outil `TSA_Benchmark`) | **absente** de l'interface (code partagé présent) | aucun espace de validation dans TSALab |
| Serveur MCP, identifiant de document | testée | intégrée (serveur propre, canal `tsaraloha-tsalab`) | `AutomationServer` partagé |
| Blueprint (programmation visuelle), débogueur, plugins | testée | intégrée, testée (L6, L8) | éditeur partagé |
| SOLVER LAB (K·U = F instrumenté) | — | intégrée, testée (L7) | propre à TSALab |
| Journal d'expériences, traçabilité, reproduction | absente | **absente** | — |
| Comparaison de solveurs sur un même problème | banc (référence analytique vs un moteur) | partielle : S6 compare OpenSees et MetDeDeplacement en 2D, sans interface | — |

## 5. Interface des solveurs

La mission demande une interface `IStructuralSolver`. Elle existe déjà sous le nom `TSA::Analysis::AnalysisEngine`
(`TSA/src/Analysis/Engine/AnalysisEngine.h`, `docs/ANALYSIS_ENGINES.md`). Chaque moteur y déclare :
- ses dimensions et ses familles d'éléments ;
- les types d'analyse qu'il fait réellement, et les résultats qu'il produit ;
- sa disponibilité réelle sur le poste ;
- une politique explicite pour les éléments non pris en charge (refus, ou exclusion **avec avertissement**).

L'interface utilisateur et la validation générique sont pilotées par ces capacités : aucun code ne teste l'identité
d'un moteur. **Décision : ne pas créer une seconde interface**, qui dupliquerait l'existante. Les nouveaux types
d'analyse et les solveurs expérimentaux de TSALab s'y ajouteront (voir `SOLVER_API.md`).

Capacités déclarées aujourd'hui :

| Moteur | Dim. | Éléments | Analyses | Particularités |
| :--- | :--- | :--- | :--- | :--- |
| OpenSees 3.8.0 (processus externe) | 3D | barres, treillis, câbles, coques, ressorts | statique, non linéaire, modale | maillage 1D/2D transmis ; surfaces non calculables refusées |
| Custom2D (MetDeDeplacement, TSALab) | 2D | barres | statique | options propres ; refuse les surfaces |

## 6. Plan de migration et de développement

| Étape | Contenu | Critère de fin |
| :--- | :--- | :--- |
| 1 | Correction de la violation de couches ; référence établie | `check_layers` 0 violation ; TSA et TSALab compilent ; tests verts |
| 2 | Menus d'affichage (échelles, profils, diagnostic) déplacés dans une bibliothèque partagée, utilisés par TSA et TSALab | même code dans les deux applications ; tests TSA verts ; menus présents dans TSALab |
| 3 | Espace **Validation** de TSALab sur le banc partagé : cas, exécution, écarts absolus et relatifs, rapports | cas exécutés depuis TSALab, résultats identiques à l'outil `TSA_Benchmark` |
| 4 | **Journal d'expériences** : version de TSALab, moteur et version, empreinte du modèle, réglages, unités, date, statut, avertissements, résultats et provenance ; reproduction | test : enregistrer, relire, rejouer, comparer |
| 5 | Comparaison de solveurs sur un même problème (référence analytique, Custom2D, OpenSees) dans l'interface | — |
| 6 | Maillage dans TSALab (contrôleur de maillage sorti de la fenêtre de TSA) ; coques calculables depuis TSALab | — |
| 7 | Multi-documents ou multi-expériences dans TSALab | — |
| 8 | Nouveaux types d'analyse (flambement linéaire, temporel, spectral) : capacités, moteur, validation | — |

Les étapes 1 à 4 sont traitées dans cette mission ; les suivantes sont décrites dans `LABORATORY_ROADMAP.md`.
