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
Status: PARTIAL — Accueil, Modèle. MISSING : Blueprint, Analysis, Results, Research (SolverLabPage existe mais
n'est pas encore affichée : il faut d'abord un Analysis Manager partagé pour calculer dans TSALab).

## Tests
Application 2/2 (exemples, format .tsalab / import .tsa) ; science 5/5 ; banc 6/6 (2026-10-08).
