# TSALab — Tsaraloha Structural Analysis Laboratory

Mémoire technique persistante pour Claude Code. **Le code réel fait foi.**

## Identité

Laboratoire scientifique de l'écosystème Tsaraloha : IDE Qt (TSALab.exe) pour dessiner, programmer
(Blueprint, à venir), expérimenter, calculer, valider et visualiser ; cœur scientifique C++ pur utilisé
aussi par TSA. Architecture de référence : `../TSA/docs/TSARALOHA_ARCHITECTURE.md` (ADR-024).

## Organisation

```text
TSALab/
  science/   cœur scientifique C++20 PUR (ni Qt, ni OCCT, ni modèle TSA) — tsalab_science
             numerics (solveurs instrumentés, valeurs propres, comparaison), planar (API des solveurs
             d'ossatures planes, MetDeDeplacement), validation (benchmarks analytiques + validation croisée),
             tools/tsalab-bench (console), tests/ (sans Qt). TSA le lie pour son moteur « custom2d ».
  lab/       LabApp (main, LabMainWindow : fenêtre IDE), LabUI (LabStartPanel, SolverLabPage),
             Research (exemples = modèles TSA, adaptateur SOLVER LAB), Tests (application)
  product/   identité TSALab (ProductIdentity.h : .tsalab, TSLB, QSettings…) et CLSID (ProductShellIds.h)
  resources/ icônes, lab.qrc, TSALab.rc
```
- La fenêtre de TSALab n'a ni moteur graphique ni modèle propres : ProjectSession, OccView, ViewportContainer,
  ModelTreeWidget, PropertyPanel, LogConsoleDock, SelectionSynchronizer, EcosystemApplication et
  Automation::CommandRegistry viennent des bibliothèques partagées de TSA (TSALab_Model / _Graphics / _Widgets).
- Règle n° 1 : ne jamais copier un fichier de TSA. Besoin d'un comportement : composant partagé ou point
  d'extension dans TSA ; calcul : science/ ; commande métier : CommandRegistry.
- `lab/` ne doit jamais masquer un chemin de `TSA/src` (contrôle CMake).

## Build & test

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" >nul && cmake --preset ninja-debug && cmake --build --preset ninja-debug -- -k 0 -j 4"
build-ninja-debug\TSALab_TestSuite.exe                       # application (2 tests)
build-ninja-debug\tsalab_science\tsalab_science_tests.exe    # cœur scientifique (5 tests)
build-ninja-debug\tsalab_science\tsalab-bench.exe            # banc de validation (6 benchmarks)
```
- Dépôt TSA voisin requis (`TSA_ROOT_DIR`) ; SDK OCCT / 3rdparty : ceux de TSA.
- Ne pas compiler TSA et TSALab en parallèle (mémoire PCH). Cache sans /EHsc : `--fresh` (BUG-036 de TSA).

## Mémoire
→ `current-state.md`, `tasks.md`, `decisions.md`, `changelog.md` ; base commune : `../TSA/.claude/`.
