# Feuille de route du laboratoire

État au 12/10/2026 (fin de la mission « Suite du développement »). Une étape n'est marquée « fait » qu'avec les preuves de son exécution : compilation, tests et,
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
| Première expérience complète (poutre bi-appuyée, force centrale) : référence analytique sur le modèle de l'utilisateur, hypothèses vérifiées, écarts enregistrés, réouverture | test L11 (Custom2D et OpenSees) ; TSALab réel (calcul, déformée, réactions, réouverture) |
| Centre d'expériences : journal par projet, référence par expérience, journal d'un autre projet, reprise du modèle figé | tests L10 et L11 ; TSALab réel |
| Laboratoire matriciel : exports CSV des vecteurs F/U et des transformations local / global ; données absentes refusées | test L12 |
| Convergence en maillage : série structurée du banc, ordre observé, Richardson, panneau et export CSV | test 367 (TSA), test L13 ; espace Validation de TSALab réel (FEA-01) |
| Nœuds du laboratoire : section, lecture de barre, réaction, efforts, comparaison ; expérience entièrement en nœuds | test 389 (TSA), test L6 |
| Profil hors catalogue refusé (n'est plus remplacé en silence par l'IPE 100) | test 389 (TSA), test L6 |
| Cas FEA-07 : MITC4 contre la référence de Mindlin (appui dur), ordre 2 observé ; écart de FEA-01 expliqué (appui mou) | test 367 (TSA, 146 vérifications), test L13 |

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
| P2 | Laboratoire matriciel, suite | page dédiée : matrices élémentaires et globale en tableau ou carte de couleurs, comparaison de deux calculs ; exporter les matrices élémentaires de Custom2D (aujourd'hui non exportées) | `Custom2D::Features` ; aucune matrice recalculée par TSALab |
| P2 | Nœuds Blueprint, suite | enregistrer une expérience, exporter un rapport, tracer une courbe ; « charger un modèle » et « sélectionner » à concevoir (voir `ARCHITECTURE.md` § 6) | API du registre de commandes |
| P2 | Rapports scientifiques | rapport d'expérience (démarche, hypothèses, équations, données, résultats, provenance) exporté depuis le journal | NDC existante pour la partie calcul |
| P3 | Nouveaux types d'analyse | flambement linéaire, temporel, spectral : type d'analyse, capacité déclarée, adaptateur OpenSees, cas de validation **avant** exposition dans l'interface | aucune option affichée tant que le chemin de calcul n'est pas vérifié |
| P2 | Isolation des extensions | les plugins sont des DLL chargées dans le processus de l'application : exécution des extensions dans un processus séparé, ou extensions signées et vérifiées avant chargement | compatibilité avec l'API de plugins existante |
| P3 | Études paramétriques | variation d'un paramètre, série d'expériences enregistrées, courbes | journal d'expériences |
| P3 | Formats d'échange analytiques | export analytique (IFC structural analysis view déjà présent dans TSA ; autres formats à étudier), pertes signalées | — |

## Limites connues

- **Analyses** : dynamique temporelle, spectrale, pushover, flambement et thermique ne sont déclarées par aucun
  moteur, donc absentes.
- **Maillage** : pas d'interface de maillage dans TSALab (outil `tsa-mesh` livré). La chaîne Gmsh → OpenSees est
  exécutée par le banc (FEA-01 à FEA-06) et l'étude de convergence est dans l'espace Validation.
- **Références analytiques** : une seule pour l'instant, la poutre bi-appuyée avec force centrale (Euler-Bernoulli).
  Les autres modèles sont déclarés « hors domaine », avec la raison.
- **Matrices** : Custom2D n'exporte pas de matrices élémentaires ; OpenSees n'exporte ni F ni, hors extraction
  avancée, aucune matrice.
- **Documents** : un seul projet par fenêtre.
- **Sécurité** : les plugins natifs s'exécutent dans le processus de l'application (voir `SOLVER_API.md`).
- **Rejeu** : il dépend des moteurs installés sur le poste. Une version différente est signalée, sans bloquer le
  rejeu.
