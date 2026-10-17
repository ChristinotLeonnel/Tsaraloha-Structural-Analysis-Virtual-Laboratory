# Current State — TSALab

Last Updated: 2026-10-08 — branche feature/scientific-ide. Légende : IMPLEMENTED · PARTIAL · MISSING.

## Application (TSALab.exe)
Status: IMPLEMENTED — fenêtre IDE LabMainWindow sur les bibliothèques partagées (aucune copie de TSA) :
espaces Accueil / Modèle, docks Explorateur / Propriétés / Console, menus Fichier / Édition / Affichage /
Modèle / Aide, outils de dessin du viewport partagé, vues standard, disposition mémorisée, .tsalab / import .tsa,
exemples. Console : commandes du registre central (help). Vérifié dans l'interface (captures 2026-10-08) :
ouverture d'un .tsa, portique créé à la console, charges listées dans l'arbre.

## Cœur scientifique (science/)
Status: IMPLEMENTED — numerics, API tsalab::planar, MetDeDeplacement (export K·U = F), 6 benchmarks validés
(analytique + validation croisée), tsalab-bench, 5 tests sans Qt. Utilisé par TSA (moteur custom2d).

## Espaces de travail
Status: IMPLEMENTED — Accueil, Modèle, Blueprint, Analyse (gestionnaire partagé, calcul en tâche de fond, F9),
Résultats (diagrammes 2D ; déformée dans le viewport via le dock Résultats), Recherche (SOLVER LAB sur K·U = F).
Réglages par défaut d'un projet sans réglages : Custom2D, plan du modèle, système exporté (non écrits dans le
modèle tant que l'utilisateur ne les modifie pas). Vérifié en GUI 2026-10-08 (exemple Portique plan).

## Programmation structurelle Blueprint (2026-10-17, branche feature/blueprint-structurel)
Status: IMPLEMENTED — science : FrameElement2D (rotules, masses), méthode directe (ressorts), dynamique pédagogique,
charges roulantes ; laboratoire : 127 blocs en 8 catégories, exemples (dont la méthode en six sous-graphes), import
de projets TSA (calcul, bloc, boîte d'aperçu), persistance JSON du modèle et des résultats. Vérifié : science 22/22,
laboratoire 27/27 (Debug, 2026-10-17), essai de l'interface réelle (UIA) 28/28. Inventaire : docs/AUDIT_FONCTIONNALITES.md.

## Phase 8 (2026-10-08)
Status: IMPLEMENTED — science/ : OpenSeesPlanarSolver (C++ pur, processus OpenSees) + pont commun MddBridge ; le
banc ignore un solveur indisponible. Application : assistant IA, débogueur Blueprint, plugins (Aide ▸ Plugins chargés).

## Tests
Application 5/5 (exemples, .tsalab / import .tsa, Blueprints, analyse + SOLVER LAB, plugin) ; science 6/6 ; banc 14/14
(MetDeDeplacement et OpenSees) (2026-10-08).
