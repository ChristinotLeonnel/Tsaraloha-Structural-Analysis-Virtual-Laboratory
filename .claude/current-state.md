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

## Tests
Application 4/4 (exemples, .tsalab / import .tsa, Blueprints, analyse + SOLVER LAB) ; science 5/5 ; banc 7/7 (2026-10-08).
