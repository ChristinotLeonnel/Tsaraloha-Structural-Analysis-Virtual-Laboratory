# Current State — TSALab

Last Updated: 2026-10-08 — branche feature/shared-tsa-core (non commitée). Légende : IMPLEMENTED · PARTIAL · MISSING.
Base commune (modèle, viewport, calcul, NDC, BIM, IA…) : voir ../TSA/.claude/current-state.md.

## Build
Status: IMPLEMENTED — TSALab compile les sources de ../TSA (tsa_add_product), preset ninja-debug, 0 erreur,
0 avertissement (2026-10-08). SDK OCCT / 3rdparty : ceux de ../TSA.

## Tests
Status: IMPLEMENTED — 217/217 le 2026-10-08 (212 suites communes exécutées sur le produit TSALab, dont format
.tsalab / TSLB et DLL TSALabThumbnailProvider, + suite `lab` L1–L5).

## Copies obsolètes
Status: IMPLEMENTED (2026-10-08) — les ~970 fichiers copiés de TSA ont été retirés (git rm, accord de l'utilisateur) ;
le dépôt ne suit plus que product/, lab/, resources propres, CMake, presets, run.bat, README, .claude.
Rebuild + 217/217 après suppression. Restent sur disque, NON suivis et inutilisés : opencascade-8.0.1-vc14-64/,
3rdparty-vc14-64/, thirdparty/ (OpenSees), build/ — supprimables par l'utilisateur.

## Start Center laboratoire
Status: IMPLEMENTED — LabStartPanel (StartCenter::setLaunchPanel) : Nouveau modèle, Ouvrir (.tsalab / import .tsa),
6 exemples générés dans Documents/TSALab/Exemples. Vérifié : lancement → titre « TSALab — Start Center ».
Non vérifié en GUI : clic sur un exemple, thème clair.

## Espaces (LabWorkspaceHost)
Status: PARTIAL — MODÈLE (MainWindow commun) et SOLVER LAB implémentés ; ANALYSIS, RESULTS, ELEMENT LAB,
EXPERIMENT, VALIDATION, VISUAL CODING : MISSING (ADR-L03 : pas de page vide).

## SOLVER LAB
Status: IMPLEMENTED (code + tests L1–L3 du noyau) — rejoue K·U = F (extraction ADVANCED) avec Gauss LU, Cholesky,
gradient conjugué ; résidu, écart à OpenSees, pivots, conditionnement (n ≤ 400), courbe CG. Non vérifié en GUI
sur un vrai calcul ADVANCED. Résolution synchrone (curseur d'attente) : lente au-delà de ~1500 équations en Debug.
