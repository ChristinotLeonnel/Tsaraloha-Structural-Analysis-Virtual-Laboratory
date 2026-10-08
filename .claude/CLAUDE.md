# TSALab — Tsaraloha Structural Analysis Laboratory

Mémoire technique persistante pour Claude Code. **Le code réel fait foi** : corriger cette documentation en cas
de divergence.

## Identité

Laboratoire d'ingénierie structurale de l'écosystème Tsaraloha : modéliser, expérimenter, inspecter, tester,
valider, comprendre. TSA est le logiciel de production ; TSALab est construit **sur la même base technique**.

## Base commune avec TSA (ADR-023 de TSA, ADR-L01 ici) — règle n° 1

TSALab ne contient **aucune copie** des sources de TSA. Son CMakeLists compile les sources du dépôt TSA voisin
(`TSA_ROOT_DIR`, par défaut `../TSA`) via `tsa_add_product()` de `TSA/cmake/TSAProduct.cmake`.

```text
TSALab/
  product/   ProductIdentity.h (nom, .tsalab, signature TSLB, QSettings…), ProductShellIds.h (CLSID),
             ProductHooks.cpp (configureShell : panneau Start Center, rail des espaces),
             ProductTests.cpp (suite --suite=lab, tests L1–L5)
  lab/       Research/ (Numerics : solveurs instrumentés ; Examples : modèles d'exemple ;
             Solver : SolverExperiment) — compilés dans TSALab_Core, testés
             LabUI/ (LabStartPanel, LabWorkspaceHost, SolverLabPage) — exécutable seulement
  resources/ lab.qrc, TSALab.rc, icônes TSALab
```

- Un bug de la base commune se corrige **dans TSA** (`../TSA/src`), jamais dans TSALab.
- Besoin d'un comportement différent dans TSALab : constante dans `product/ProductIdentity.h` (même API que
  `TSA/product/ProductIdentity.h` — ajouter la constante aux DEUX) ou point d'extension dans TSA
  (`StartCenter::setLaunchPanel`, `AppShell::setWorkspaceDecorator`, `MainWindow::resultsChanged`…).
- `lab/` ne doit jamais contenir un chemin qui existe dans `TSA/src` (contrôlé par CMake, FATAL_ERROR).
- Toute modification de `TSA/src` se vérifie sur les deux produits : TSA (212 tests) et TSALab (212 + 5).
- Mémoire de la base commune (architecture, bugs, décisions) : `../TSA/.claude/`. Ne pas la dupliquer ici.

## Build & test

```powershell
$vcvars = "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
cmd /c "`"$vcvars`" >nul && cmake --preset ninja-debug && cmake --build --preset ninja-debug -- -k 0 -j 4"
build-ninja-debug\TSALab_TestSuite.exe            # toutes les suites ; --suite=lab pour le laboratoire
```
- SDK (OCCT, 3rdparty, OpenSees, Extensions/) : ceux du dépôt TSA (presets → `../TSA/opencascade-…`).
- Ne pas lancer les builds TSA et TSALab en parallèle (`-j 4` chacun : erreurs mémoire PCH C3859).
- Si `CMAKE_CXX_FLAGS` est vide dans `build-*/CMakeCache.txt` (pas de /EHsc → test 96 échoue) :
  `cmake --preset ninja-debug --fresh`.

## Mémoire

→ `current-state.md`, `tasks.md`, `decisions.md`, `changelog.md` (propres à TSALab) ;
base commune : `../TSA/.claude/architecture.md`, `known-issues.md`, `decisions.md`.
Ne jamais marquer une fonctionnalité IMPLEMENTED sans preuve dans le code ni un bug FIXED sans test.
