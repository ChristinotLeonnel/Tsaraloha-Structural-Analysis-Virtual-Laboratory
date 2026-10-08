# Changelog — TSALab

## 2026-10-08 (base commune avec TSA — ADR-L01, branche feature/shared-tsa-core)

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
