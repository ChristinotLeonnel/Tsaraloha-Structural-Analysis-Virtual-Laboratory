# Feuille de route du laboratoire

État au 12/10/2026. Une étape n'est marquée « fait » qu'avec les preuves de son exécution : compilation, tests et,
quand c'est pertinent, essai dans l'application réelle. Le détail de départ est dans `AUDIT_TSA_TSALAB.md`.

## Fait

| Étape | Preuve |
| :--- | :--- |
| Audit TSA / TSALab, référence (build, tests, lancement réel) | `AUDIT_TSA_TSALAB.md` |
| Violation de couches corrigée (formule de contrainte de fibre dans la couche modèle) | `check_layers` : 0 violation |
| Menus d'affichage partagés (échelles, profils, diagnostic) dans TSA et TSALab | test 388 (TSA) ; menu Affichage de TSALab réel |
| Espace Validation sur le banc partagé | test L9 ; 144 vérifications réussies dans TSALab réel |
| Journal d'expériences et rejeu | test L10 ; rejeu identique dans TSALab réel |
| Version d'OpenSees jamais inventée dans les rapports du banc | code du banc (TSA) |

Les briques déjà présentes avant cette mission restent en place, et vérifiées par L4 à L8 et S1 à S6 :
- Blueprint (programmation visuelle typée, débogueur) ;
- SOLVER LAB ;
- plugins ;
- MCP ;
- cœur scientifique.

## Prochaines étapes (par priorité)

| Priorité | Étape | Contenu | Dépendances, risques |
| :--- | :--- | :--- | :--- |
| P1 | Comparaison de solveurs sur un même problème | page « Comparer » : un modèle du projet calculé par plusieurs moteurs (OpenSees, Custom2D, solveur expérimental), avec ou sans référence analytique ; écarts par grandeur avec vérification préalable des unités, du plan, des appuis et des charges | réutiliser `BenchmarkSuite` (comparaison) et `AnalysisManager` ; Custom2D limité au plan |
| P1 | Maillage dans TSALab | sortir `MeshController` de la fenêtre de TSA vers la couche widgets, puis page Maillage : génération Gmsh, qualité, vue, études de convergence | refonte d'un composant de TSA : à faire avec ses tests (suites `mesh`, `meshview`, `surfaces`) |
| P1 | Coques calculables depuis TSALab | transmettre le maillage au calcul (déjà fait dans TSA) | dépend du maillage |
| P2 | Plusieurs projets ou expériences | onglets de sessions dans `LabMainWindow` (même principe que `AppShell` de TSA : une session par onglet, aucune donnée partagée) | `AutomationServer` : un seul serveur à l'écoute |
| P2 | Laboratoire matriciel | visualisation des matrices locales et globales, des transformations et de la numérotation des DDL (données déjà exportées : dock « Données d'analyse ») ; conditionnement et singularités (déjà dans SOLVER LAB) | — |
| P2 | Nœuds Blueprint du laboratoire | exécuter le banc, enregistrer une expérience, comparer deux résultats, tracer une courbe | API du registre de commandes |
| P2 | Rapports scientifiques | rapport d'expérience (démarche, hypothèses, équations, données, résultats, provenance) exporté depuis le journal | NDC existante pour la partie calcul |
| P3 | Nouveaux types d'analyse | flambement linéaire, temporel, spectral : type d'analyse, capacité déclarée, adaptateur OpenSees, cas de validation **avant** exposition dans l'interface | aucune option affichée tant que le chemin de calcul n'est pas vérifié |
| P2 | Isolation des extensions | les plugins sont des DLL chargées dans le processus de l'application : exécution des extensions dans un processus séparé, ou extensions signées et vérifiées avant chargement | compatibilité avec l'API de plugins existante |
| P3 | Études paramétriques | variation d'un paramètre, série d'expériences enregistrées, courbes | journal d'expériences |
| P3 | Formats d'échange analytiques | export analytique (IFC structural analysis view déjà présent dans TSA ; autres formats à étudier), pertes signalées | — |

## Limites connues

- **Analyses** : dynamique temporelle, spectrale, pushover, flambement et thermique ne sont déclarées par aucun
  moteur, donc absentes.
- **Maillage** : il n'est pas accessible depuis l'interface de TSALab (outil `tsa-mesh` livré).
- **Documents** : un seul projet par fenêtre.
- **Sécurité** : les plugins natifs s'exécutent dans le processus de l'application (voir `SOLVER_API.md`).
- **Rejeu** : il dépend des moteurs installés sur le poste. Une version différente est signalée, sans bloquer le
  rejeu.
