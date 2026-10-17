# Changelog — TSALab

## 2026-10-17 (mission « Terminer les fonctionnalités incomplètes » — branche feature/blueprint-structurel, fusionnée dans main)

### Added
- Rotules d'extrémité (condensation statique, rotations libres éliminées) et appuis élastiques dans `tsalab::structural`
  (S17, S18) ; blocs et exemple « Rotule et appui élastique » (L23, L24).
- Dynamique pédagogique `Dynamics2D` (masses, analyse modale, Newmark, accélérogramme fourni par l'utilisateur, spectre
  saisi SRSS / CQC) et `MovingLoads2D` (lignes d'influence, enveloppes, calculs statiques successifs) : S19 à S22,
  blocs F / G, exemples, L23, L25, L26. Aucune donnée sismique fabriquée ; pas d'interaction véhicule-pont.
- Import d'un projet TSA (`TsaImport`, bloc `struct.model_import_tsa`) par le chemin d'analyse plane de TSA, source
  intacte (SHA-256), rapport importé / converti / ignoré, refus motivés, annulation : L27 (14 portées importées,
  4 refusées, 13 comparées au solveur 2D de TSA à 1e-9).
- Exemple « Methode_Des_Deplacements_2D en sous-graphes » (six sous-graphes obtenus par encapsulation, interfaces
  typées, documentation) : L28 (K, F, d, R identiques au graphe pas à pas). Sous-graphes du moteur partagé : TSA
  tests 392–393.
- Gauss-Seidel écrit (`gaussSeidelSweep`) et exemple « Solveur de l'étudiant (Gauss-Seidel) » ; L19 strict.
- Boîte « Blueprint ▸ Importer un portique d'un projet TSA… » (`lab/LabUI/TsaImportDialog.*`) : portées, aperçu en tâche
  de fond annulable, rapport, choix du cas, graphe d'analyse (`StructuralGraphs::buildTsaImport`, projet lu une fois) : L29.
- Persistance JSON du modèle et des résultats (`StructuralJson.*`, formats `tsalab-model2d` / `tsalab-result2d`,
  relecture à l'identique, lecture stricte) et quatre blocs Enregistrer / Lire : L30.
- Inventaire `docs/AUDIT_FONCTIONNALITES.md` ; feuille de route et documentation mises à jour.

### Fixed
- Boîte d'import : un clic sur « Aperçu » juste après la saisie du chemin pouvait être perdu (lecture des portées lancée
  pendant le changement de focus, boucle locale avalant le relâchement du bouton) ; lecture différée. Trouvé par l'essai
  de l'interface (UIA).

### Verified
- Debug : science 22/22, laboratoire 27/27 (L4 à L30), TSA 393/393 ; essai de TSALab réel (UIA) 28/28 : sous-graphes
  (double-clic sur une instance, fil d'Ariane, propriétés, retour, exécution) et import (refus motivé, aperçu, graphe
  d'analyse exécuté, projet intact) ; réglages utilisateur restaurés à l'identique.
- Release : TSA 393/393, science 22/22, laboratoire 27/27, essai de l'interface 28/28.

## 2026-10-09 (Claude Code co-ingénieur par MCP)

### Added
- AutomationServer démarré par LabMainWindow (canal « tsaraloha-tsalab ») et pont `tsaraloha-mcp.exe` construit à côté
  de TSALab.exe (TSA/docs/MCP.md). Vérifié : portique construit, calculé et lu par le pont sur TSALab ouvert.

## 2026-10-09 (fermeture de TSALab)

### Fixed
- BUG-039 (voir TSA/.claude/known-issues.md) : assertion Qt à la fermeture avec des nœuds Blueprint sélectionnés.
  Corrigé dans l'éditeur partagé (TSA) ; ~LabMainWindow coupe aussi les connexions de ses enfants vers la fenêtre.
  Vérifié en GUI : exemple portique, 5 nœuds sélectionnés, fermeture normale, journal sans assertion.

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
