# Synchronisation TSA ↔ TSALab

Historique des synchronisations entre les deux dépôts. Chaque entrée est établie à partir du code, de `git log` et des
builds et tests exécutés ce jour-là, jamais à partir des noms de fichiers.

## Principe (rappel)

TSALab ne copie aucun fichier de TSA. Il compile les sources du dépôt voisin `../TSA/src` dans trois bibliothèques à
son identité (`TSALab_Model`, `TSALab_Graphics`, `TSALab_Widgets`, voir `ARCHITECTURE.md`). En conséquence :

- tout ce qui vit dans les **bibliothèques partagées** de TSA arrive dans TSALab **à la compilation**, sans fusion ;
- ce qui vit dans la **fenêtre de TSA** (`MainWindow*`, `AppShell`, ruban, `MeshController`) n'y arrive pas : il faut
  d'abord le déplacer dans une couche partagée ;
- une branche de TSALab qui utilise une API nouvelle de TSA **dépend** de la branche TSA correspondante : TSA est
  fusionné en premier, et TSALab est compilé contre la même révision de TSA.

Synchroniser, c'est donc :
1. vérifier ce que TSA a changé ;
2. recompiler TSALab contre cette révision ;
3. corriger ce qui casse ;
4. exposer dans le laboratoire ce qui est utile ;
5. déplacer vers une couche partagée ce qui est bloqué dans la fenêtre de TSA.

## 12/10/2026 — synchronisation 1 (mission « TSALab, laboratoire opérationnel »)

Base : TSA `d4296cf` (finalisation multi-documents), TSALab `9dc205d`. Détails : `AUDIT_TSA_TSALAB.md`.

| Sujet | Constat | Action |
| :--- | :--- | :--- |
| Compilation de TSALab contre TSA `d4296cf` | réussie sans modification | — |
| Couches (`check_layers.py`) | 1 violation (formule de contrainte de fibre dans le graphique) | déplacée dans la couche modèle (TSA `5e2ae70`) |
| Échelles, profils et diagnostic d'affichage de TSA | dans la fenêtre de TSA, absents de TSALab | composant partagé `UI/Display/DisplayMenus`, utilisé par les deux applications |
| Fin de calcul | aucun signal commun aux calculs synchrones et en tâche de fond | `AnalysisController::runCompleted` (TSA `8677cc6`) |
| Version d'OpenSees dans les rapports du banc | pouvait être affichée sans avoir été lue | jamais inventée (TSA `2b8cb0b`) |
| Onglets multi-projets, copier-coller entre projets | `AppShell` (fenêtre de TSA) | non repris, voir « Incompatibilités » |

Fusions : TSA `72f031a`, TSALab `2bd96c3` ; poussées après autorisation.

## 12/10/2026 — synchronisation 2 (mission « Suite du développement »)

Base : TSA `main` `72f031a`, TSALab `main` `2bd96c3`. Depuis la synchronisation 1, TSA n'a reçu aucun commit hors des
branches de cette mission. Toutes ses autres branches sont déjà fusionnées dans `main` (`git branch --no-merged
main` ne liste que les branches ci-dessous).

### Branches (non fusionnées, non poussées)

Les branches de chaque dépôt sont **chaînées** : chacune part de la précédente.

| Dépôt | Branche | Commits | Contenu |
| :--- | :--- | :--- | :--- |
| TSA | `feature/lab-matrices` | `5baf4ea` | export CSV des vecteurs F/U et des transformations local/global (`ResultsExport`, dock « Données d'analyse ») |
| TSA | `feature/mesh-convergence` | `66bcfc2` | série de convergence structurée dans `CheckOutcome` et le rapport JSON du banc |
| TSA | `feature/lab-nodes` | `e9648de`, `b1faddc`, `d607042`, `2af0e78` | commandes `model.set_section`, `query.bar`, `results.node_reaction`, `results.member_forces`, `compare.values`, option `self_weight` d'`analysis.run` ; profil hors catalogue refusé |
| TSALab | `feature/experiences-matrices` | `7fd9372`, `5114d33` | première expérience complète (référence analytique, centre d'expériences), test du laboratoire matriciel |
| TSALab | `feature/convergence-maillage` | `d0fa99d` | étude de convergence en maillage, panneau de l'espace Validation |
| TSALab | `feature/noeuds-laboratoire` | `c9ec74c`, docs | Blueprint « Expérience : poutre bi-appuyée » entièrement en nœuds ; documentation |

**Ordre de fusion obligatoire** : TSA `feature/lab-nodes` (qui contient les deux autres) avant TSALab
`feature/noeuds-laboratoire`. TSALab compilé contre TSA `main` actuel échoue : il utilise
`ResultsDataset::SystemVectors`, `CheckOutcome::convergence` et les nouvelles commandes.

### Réintégrations

Rien n'a été copié d'un dépôt à l'autre. Les ajouts communs sont faits dans les bibliothèques partagées de TSA, et
TSA en profite donc aussi :
- les nouvelles commandes sont disponibles dans la console, les Blueprints et le serveur MCP de TSA ;
- les exports CSV sont dans le dock « Données d'analyse » de TSA ;
- la série de convergence est dans le rapport JSON de `TSA_Benchmark`.

Le travail propre à TSALab reste dans `lab/` :
- la référence analytique ;
- l'étude de convergence ;
- les pages du laboratoire ;
- les exemples.

### Changement de comportement dans TSA

`parseSection` (commandes `model.create_beam`, `model.create_column`, `model.set_section`) acceptait une taille hors
catalogue. Par exemple, « IPE 999 » recevait **en silence** les dimensions de l'IPE 100, sous le nom « IPE 999 ». Une
telle taille est maintenant **refusée**, avec la liste des tailles disponibles. Un script ou un Blueprint qui
utilisait une taille inexistante échoue désormais, au lieu de calculer avec une section fausse.

### Résultats

| Contrôle | Résultat |
| :--- | :--- |
| TSA Debug, suite complète | 389 / 389 (le test 262, surveillance de fichier avec un délai de 4 s, a échoué une fois sous charge, puis réussi 3 fois sur 3 seul et dans la suite complète suivante) |
| TSALab Debug : `TSALab_TestSuite`, `tsalab_science_tests` | 10 / 10, 6 / 6 |
| TSALab, préréglage `windows-x64-release` (Visual Studio 18 2026) : configuration et compilation | réussies, 0 erreur |
| TSALab Release : `TSALab_TestSuite`, `tsalab_science_tests`, `ctest -C Release` | 10 / 10, 6 / 6, 1 / 1 |
| `check_layers.py` | 0 violation |
| TSALab réel | expérience de référence (calcul, déformée, réactions, réouverture) ; panneau de convergence (FEA-01) ; Blueprint de l'expérience exécuté en Release, avec la référence applicable et 5 vérifications réussies dans le journal |

Pendant les essais d'interface, les réglages de l'utilisateur ont été sauvegardés puis restaurés à l'identique : 4 clés
du registre et `shortcut.txt`. Les fichiers créés par les essais ont été supprimés.

## 12/10/2026 — suite (après fusion locale)

Fusions locales sans poussée : TSA `main` `df8e0d8`, TSALab `main` `ea595e1`. Nouvelles branches, à fusionner dans le
même ordre (TSA d'abord) :
- TSA `feature/fea-mindlin` : cas FEA-07, annotation de FEA-01, `BENCHMARKS.md` ;
- TSALab `feature/fea-mindlin` : tests L9 (28 cas) et L13 (FEA-07), documentation.

## 12/10/2026 — maillage dans TSALab

Branches à fusionner dans cet ordre : TSA `feature/mesh-shared` (contient `feature/fea-mindlin`), puis TSALab
`feature/maillage-labo` (contient `feature/fea-mindlin`).

| Dépôt | Changement |
| :--- | :--- |
| TSA | `applyMeshToAnalysis` partagé (`UI/Mesh/MeshAnalysisBridge`) ; `MainWindow` lui délègue, sans changement de comportement |
| TSA | `AnalysisManagerPanel::setContextPreparer` : le contexte du calcul est complété par le maillage, sans être enregistré dans les réglages du projet |
| TSA | validation : avec des coques transmises, trois avertissements faux ne sont plus émis (charges surfaciques ignorées, dalles « non calculées », modèle « sans élément filaire ») ; test 390 |
| TSALab | menu Maillage (paramètres, génération Gmsh, affichage, déplacements sur le maillage, qualité, suppression), maillage transmis au calcul |
| TSALab | exemple « Plaque carrée — appui simple dur » et référence analytique de Mindlin (`squarePlateHardSupport`) ; test L14 |

Vérifié dans TSALab réel : exemple ouvert → Maillage ▸ Générer (Gmsh 4.15.2, 576 quadrangles, 0 erreur) → Calculer
(576 coques OpenSees, aucun avertissement) → expérience enregistrée avec la référence de Mindlin : flèche à 0,059 %,
réactions exactes.

## Incompatibilités et limites connues

| Fonction de TSA | Pourquoi elle n'est pas dans TSALab | Condition de reprise |
| :--- | :--- | :--- |
| Maillage interactif | **repris le 12/10/2026** (voir ci-dessous) | — |
| Onglets multi-projets (`AppShell`) | fenêtre de TSA ; un seul serveur d'automatisation à l'écoute | onglets de sessions dans `LabMainWindow` |
| Ruban de TSA | TSALab a sa propre fenêtre et ses menus | aucune (choix d'interface) |

**Correction d'un constat antérieur** : `MeshController`, `MeshSettingsPanel` et `MeshQualityPanel` étaient déjà dans
la couche widgets partagée. Seuls le câblage et la transmission au calcul étaient dans la fenêtre de TSA
(`MainWindow_Mesh.cpp`). La transmission au calcul est maintenant partagée (`UI/Mesh/MeshAnalysisBridge`) et TSALab a
son menu Maillage.

## Procédure pour la prochaine synchronisation

1. Lister les commits de TSA depuis la dernière entrée : `git log --oneline <dernier commit>..main`.
2. Compiler TSALab contre ce `main` (`cmake --build --preset ninja-debug`) et lancer `TSALab_TestSuite`,
   `tsalab_science_tests` et `check_layers.py`.
3. Classer chaque changement de TSA :
   - **partagé** : arrive seul ; décider s'il faut l'exposer dans le laboratoire ;
   - **fenêtre de TSA** : non disponible ; décider s'il faut le déplacer vers une couche partagée ;
   - **incompatible** : à corriger, ou à noter ci-dessus.
4. Ajouter ici une entrée datée : base, branches, actions, incompatibilités, résultats des tests.
