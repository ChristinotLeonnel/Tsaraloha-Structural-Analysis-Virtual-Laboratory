# Architecture Decisions — TSALab

Last Updated: 2026-10-08. Décisions propres au laboratoire ; celles de la base commune (ADR-001 à ADR-023) sont
dans `../TSA/.claude/decisions.md` et s'appliquent aussi à TSALab.

## ADR-L01
Title: TSALab compile la base technique de TSA, sans copie (remplace « TSALab indépendant de TSA »)
Decision: TSALab ne contient que `product/` (identité, points d'extension, tests du laboratoire), `lab/` (modules
du laboratoire) et ses ressources. Les sources communes sont celles du dépôt TSA voisin (`TSA_ROOT_DIR`, défaut
`../TSA`), compilées par `tsa_add_product(NAME TSALab …)` (`TSA/cmake/TSAProduct.cmake`, ADR-023 de TSA).
Le code commun garde l'espace de noms `TSA::` ; le code du laboratoire utilise `TSALab::`.
Reason: la phase 1 (2026-10-07) avait copié l'intégralité de TSA (≈ 470 fichiers identiques) : chaque correction
devait être faite deux fois. Demande de l'utilisateur (2026-10-08) : supprimer les copies, utiliser celles de TSA.
Consequences: indépendance conservée à l'EXÉCUTION (paramètres Tsaraloha/TSALab, %LOCALAPPDATA%, .tsalab, ProgID
TSALab.Project, CLSID {B796AF69-…}, AppUserModelID propres) mais plus au BUILD : TSALab exige le dépôt TSA.
Une correction de la base se fait dans TSA. Un besoin propre au laboratoire = constante d'identité ou point
d'extension dans TSA, jamais une copie de fichier (CMake refuse tout fichier de `lab/` qui masque `TSA/src`).
Status: ACTIVE (2026-10-08)

## ADR-L02
Title: Format .tsalab = conteneur TSA avec signature 'TSLB' ; les .tsa sont importés, jamais réécrits
Decision: même conteneur à chunks et même version de format que TSA ; seule la signature diffère (`TSLB`). TSALab lit
`TSLB` et `TSAF` (modèles TSA) ; l'enregistrement d'un modèle .tsa ouvert passe toujours par un fichier .tsalab
(`MainWindow::saveFile` : `TSA::Product::isNativeProjectFile`).
Reason: TSA ne lirait pas la signature TSLB ; réécrire un .tsa avec TSLB casserait le fichier côté TSA.
Status: ACTIVE (2026-10-07, test L5 le 2026-10-08)

## ADR-L03
Title: Espaces du laboratoire = rail autour du MainWindow commun ; un espace n'apparaît que s'il existe
Decision: `TSALab::UI::LabWorkspaceHost` (décorateur du workspace) : rail vertical + pile {MainWindow, pages du
laboratoire}. Espaces actuels : MODÈLE (MainWindow), SOLVER LAB (`SolverLabPage`). Les autres espaces envisagés
(ANALYSIS, RESULTS, ELEMENT LAB, EXPERIMENT, VALIDATION, VISUAL CODING) ne sont ajoutés au rail qu'une fois
réellement implémentés (pas de page vide).
Reason: le MainWindow est le workspace commun avec TSA (modélisation, calcul, résultats) ; le laboratoire ajoute des
vues d'inspection sans dupliquer l'interface.
Status: ACTIVE (2026-10-08)

## ADR-L04
Title: SOLVER LAB rejoue la résolution sur les données réelles du calcul, en lecture seule
Decision: K (DDL libres) et U viennent de `ResultsModel::advanced()` (extraction ADVANCED d'OpenSees) ; F = K·U.
Les solveurs du laboratoire (`TSALab::Research::solve`, matrices denses, n ≤ 2000 ; conditionnement n ≤ 400)
sont comparés à U. Sans K extraite, l'espace l'explique et ne calcule rien.
Reason: inspecter ce que fait un solveur sur SON modèle (pivots, conditionnement, convergence) sans inventer de
données ni dupliquer le moteur de calcul.
Status: ACTIVE (2026-10-08)
