# Changelog — TSALab

## 2026-10-08 (phase 8 : OpenSees dans science/, IA, débogueur, plugins)

### Added
- science/ : OpenSeesPlanarSolver (script Tcl 2D, processus sans fenêtre, nœud fictif pour une structure sans DDL
  libre), MddBridge (chargement et post-traitement des barres communs), ISolver::available, rapports « ignoré » ;
  test S6 (OpenSees ≈ MetDeDeplacement sur 7 benchmarks) ; banc 14/14.
- Application : assistant IA (dock, menu IA), débogueur et historique Blueprint (menu Blueprint), Aide ▸ Plugins chargés.
- plugins/sample : plugin d'exemple (commande composée sample.portal, nœud sample.golden) ; test L8 (5/5).

## 2026-10-08 (espaces Analyse / Résultats / Recherche — ADR-024 phase 7)

### Added
- Espace Analyse (AnalysisManagerPanel partagé), espace Résultats (Diagram2DWidget), espace Recherche (SOLVER LAB),
  docks Résultats et Données d'analyse, menu Analyse (Calculer F9), réglages par défaut du laboratoire.
- Test L7 : exemples plans calculés sans grille, K·U = F rejoué par LU / Cholesky / gradient conjugué (4/4).

### Changed
- SOLVER LAB : utilise F et U exportés par le moteur quand ils existent (Custom2D), sinon F = K·U (OpenSees).

## 2026-10-08 (espace Blueprint, BUG-037)

### Added
- Espace « Blueprint » (éditeur partagé de TSA), menu Blueprint (Nouveau, Exécuter F5, Valider, Exemples) ;
  3 Blueprints d'exemple (portique paramétrique, rangée de nœuds, banc de validation) ; test L6 (3/3).
- science/ : benchmark « barre unique bi-encastrée » (7/7).

### Fixed
- BUG-037 : MetDeDeplacement calcule une ossature sans DDL libre (U = 0, encastrement parfait).
- Disposition des panneaux mémorisée versionnée (une ancienne disposition masquait l'explorateur et la console).

## 2026-10-08 (IDE scientifique et cœur scientifique — ADR-024 / ADR-L05, branche feature/scientific-ide)

### Changed
- TSALab n'enveloppe plus MainWindow : fenêtre IDE propre (LabMainWindow) sur les bibliothèques partagées de TSA.
- Numerics et MetDeDeplacement déplacés dans science/ (C++ pur) ; SolverExperiment devient un adaptateur.

### Added
- science/ : API tsalab::planar, export K·U = F, banc de validation (6 benchmarks), tsalab-bench, tests.
- Console : commandes du registre central (TSA::Automation::CommandRegistry) ; espace Modèle affiché après une
  commande modifiante.

### Found
- BUG-037 : MetDeDeplacement refuse une ossature sans DDL libre (trouvé par le banc).

## 2026-10-08 (base commune avec TSA — ADR-L01, branche feature/shared-tsa-core) — SUPERSEDED

### Changed
- TSALab ne compile plus sa copie de TSA : CMakeLists appelle tsa_add_product() du dépôt ../TSA (TSA_ROOT_DIR).
- Identité : src/App/AppIdentity.h + ShellExtension/TSALabThumbnailProviderIds.h → product/ProductIdentity.h +
  product/ProductShellIds.h (API commune TSA::Product). Research déplacé de src/ vers lab/.
- Presets et run.bat : SDK OCCT / 3rdparty de ../TSA. README : clonage des deux dépôts, nouvelle organisation.

### Added
- LabStartPanel (colonne laboratoire, exemples), LabWorkspaceHost (rail MODÈLE / SOLVER LAB),
  SolverLabPage + TSALab::Research::SolverExperiment, suite de tests `lab` L1–L5 (product/ProductTests.cpp).

### Verified
- Build 0 erreur / 0 avertissement ; 217/217 tests ; lancement → « TSALab — Start Center ».

## 2026-10-07
- Phase 1 : copie de TSA, identité TSALab (AppIdentity.h), format .tsalab (TSLB), icônes ; Research (algèbre
  linéaire, exemples). Commits 5c841d9 … ce2832c.
