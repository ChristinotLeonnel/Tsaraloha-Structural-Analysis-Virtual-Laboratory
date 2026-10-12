# Validation et traçabilité dans TSALab

## 1. Laboratoire de validation (espace « Validation »)

L'espace exécute le **banc de validation partagé avec TSA** (`TSA/src/Validation/BenchmarkSuite`, principes et cas
détaillés dans `TSA/docs/BENCHMARKS.md`). La logique est dans `lab/Research/Validation/ValidationRunner`, la page
dans `lab/LabUI/ValidationPage`.

- **Cas** : fichiers JSON « tsa.benchmark/1 ». Le paquet les livre dans `validation/cases` ; sur le poste de
  développement, ils sont lus dans `TSA/tests/benchmarks`. Un cas invalide (champ manquant, formule ou grandeur
  inconnue) est affiché avec ses erreurs et n'est pas exécuté.
- **Exécution** : chaîne réelle modèle → `AnalysisManager` → moteur → résultats, en tâche de fond, avec progression.
  L'arrêt prend effet entre deux cas.
- **Références** : les formules analytiques du cas, évaluées et jamais recopiées. On peut aussi importer des
  références externes (CSV ou JSON : résultats d'un autre logiciel, essais).
- **Comparaison**, pour chaque grandeur :
  - référence, valeur calculée, écart absolu, écart relatif ;
  - tolérance mixte |x − x_ref| ≤ ε_abs + ε_rel·|x_ref| ;
  - origine de la valeur : analytique ou externe, exacte ou interpolée ;
  - état : réussi, échec, ou **non disponible** quand le moteur ne fournit pas la grandeur (jamais remplacée).
- **Référence ≈ 0** : l'écart relatif n'est pas défini ; il est affiché « réf. ≈ 0 » et la grandeur est jugée sur la
  tolérance absolue.
- **Rapport** : en-tête avec la version de TSALab, les versions des moteurs **lues sur les moteurs**, la date et le
  poste ; export Markdown, CSV et JSON (l'historique n'est jamais écrasé).

Résultat vérifié dans TSALab réel le 12/10/2026 : 27 cas, **144 vérifications réussies**, 0 en échec, 0 non
disponible (Custom2D 2.0.0, OpenSees 3.8.0).

Avant de comparer deux résultats, le banc fixe pour chaque cas les unités, les conventions de signe, les appuis, les
matériaux, les charges et la formulation (hypothèses écrites dans le cas). Deux moteurs ne sont jamais déclarés
équivalents sans ces vérifications.

### Convergence en maillage

Un cas qui déclare `meshSizes` est calculé une fois par taille de maille. La vérification porte sur le maillage le plus
fin, et le banc enregistre la série complète (`CheckOutcome::convergence`) :
- la taille ;
- le maillage réellement produit ;
- la valeur, ou son absence.

La série est aussi dans le rapport JSON. Dans l'espace Validation, sélectionner une ligne de résultat affiche son
détail. Pour une série, le panneau montre `lab/Research/Validation/MeshConvergence` :
- **le tableau** : écart à la référence et variation d'un maillage au suivant ;
- **l'ordre observé** p = ln((f1 − f2)/(f2 − f3))/ln(r), calculé seulement sur les trois maillages les plus fins, si le
  rapport r est constant et la convergence monotone ; sinon « non calculé », avec la raison ;
- **l'extrapolation de Richardson**, présentée comme un post-traitement de TSALab, jamais comme une valeur du moteur.

Le bouton « Convergence (CSV)… » exporte la série.

Constats sur le banc actuel (OpenSees 3.8.0, test L13 et rapport du test 367 de TSA) :

| Cas | Élément | Écart à la référence de Kirchhoff (h = 1 → 0,5 → 0,25 m) | Lecture |
| :--- | :--- | :--- | :--- |
| FEA-01 | `ShellMITC4` (Mindlin), appui « mou » (translations seules bloquées) | 0,27 % → 1,63 % → 2,65 % | l'écart **croît** quand on raffine : l'accord du maillage grossier est une compensation d'erreurs. Le cisaillement seul n'explique que 0,5 % (référence de Mindlin) ; le reste vient de la couche limite de l'appui mou, que le maillage résout de mieux en mieux. L'ordre apparent (0,40) et l'extrapolation (5,9 %) ne sont pas fiables. |
| FEA-02 | `ShellDKGT` (Kirchhoff discret) | 2,1 % → 0,6 % → 0,08 % | convergence monotone vers la référence de même théorie |
| FEA-03 | `ShellMITC4`, bande de coque | identique sur les trois maillages | solution exacte dès le maillage grossier ; ordre non défini (variations nulles) |
| FEA-07 | `ShellMITC4`, appui « dur » (rotation tangentielle bloquée), **référence de Mindlin** (h = 1 → 0,125 m) | 0,98 % → 0,24 % → 0,059 % → 0,015 % | ordre observé **2,01**, extrapolation de Richardson à 1,5e-6 de la référence : MITC4 converge vers la solution de Mindlin, avec l'ordre attendu |

Tous les cas passent leur tolérance. FEA-01 ne passe que grâce à sa tolérance de 5 % : il ne démontre pas la
convergence de MITC4. FEA-07 la démontre. Référence de Mindlin pour une plaque polygonale simplement appuyée (appui dur) :
w = α·q·a⁴/D + β·q·a²/(κ·G·e) (Wang, Lim, Reddy, Lee, 2001), avec α = 0,00406235 et β = 0,0736714 calculés par séries.
**Leçon de modélisation** : pour une coque de Mindlin, bloquer seulement les translations d'un bord donne un appui « mou »
plus souple que l'appui simple de la théorie des plaques minces.

## 2. Première expérience complète : poutre bi-appuyée, force centrale

L'exemple « Poutre bi-appuyée — charge ponctuelle centrale » :
- IPE 300, S235 ;
- L = 6 m ;
- P = 50 kN ;
- Custom2D, poids propre exclu.

Pour **tout modèle de l'utilisateur**, la référence analytique `lab/Research/Reference/AnalyticalReference` vérifie
d'abord les hypothèses de la formule :
- une seule portée horizontale, deux appuis, rotation de flexion libre ;
- même section, même matériau, même orientation ;
- aucun relâchement ;
- une seule force verticale à mi-portée ;
- poids propre exclu, aucune combinaison.

Si une hypothèse n'est pas remplie, elle est **nommée** et aucune comparaison n'est faite. Sinon, elle compare :

| Grandeur | Référence (Euler-Bernoulli) |
| :--- | :--- |
| réactions aux appuis | P/2 |
| flèche à mi-portée | P·L³/(48·E·I) |
| moment maximal | P·L/4 |
| effort tranchant maximal | P/2 |

Chaque grandeur est jugée avec une tolérance relative de 1e-6. Le résultat (hypothèses, paramètres, écarts) est
enregistré avec l'expérience.

Vérifié dans TSALab réel le 12/10/2026 :
- calcul Custom2D ;
- flèche −12,8221 mm (écart 3,5e-18 m), réactions 25 kN, M = 75 kN·m, V = 25 kN ;
- déformée et réactions affichées ;
- projet enregistré puis rouvert : l'expérience et sa référence sont relues.

Test L11 : même expérience construite par les commandes du registre, calculée par Custom2D **et** OpenSees (écarts
≤ 2e-14), refus avec poids propre, portique hors domaine, enregistrement puis réouverture.

La même expérience existe en Blueprint (« Expérience : poutre bi-appuyée », menu Blueprint ▸ Exemples, test L6). La
chaîne est entièrement faite de nœuds :
1. paramètres ;
2. géométrie ;
3. affectation de section ;
4. appuis et force ;
5. calcul sans poids propre ;
6. lecture de E et I sur la barre ;
7. flèche, réaction et efforts ;
8. trois nœuds « Comparer à une référence » et verdict.

## 3. Journal d'expériences (espace « Expériences »)

Chaque calcul est enregistré dans `lab/Research/Experiments/ExperimentJournal`, quelle que soit son origine :
interface, console, Blueprint ou MCP. Les exécutions du banc le sont aussi.

| Champ | Contenu |
| :--- | :--- |
| version | « TSALab 0.1.0 » |
| moteur | identifiant, nom et version lus sur le moteur (« version non lue » sinon) |
| modèle | copie figée `model.tsalab` et son empreinte SHA-256 |
| hypothèses et paramètres | contexte de calcul complet (`AnalysisContext` JSON) |
| unités | force, longueur, moment des résultats |
| date, statut | ISO 8601 ; réussi, échec |
| avertissements et erreurs | validation du calcul, message du moteur |
| résultats | résumé chiffré : déplacement maximal, sommes des réactions, efforts extrêmes, fréquences |
| provenance | « calculé par <moteur> <version> le <date> » |
| référence analytique | applicabilité, hypothèses non remplies, paramètres, valeurs de référence et écarts (vide si aucune référence ne s'applique) |

- **Stockage** : `<projet>.experiments/<horodatage>-<id>/record.json`, à côté du projet. Un modèle non enregistré
  va dans le dossier de données de l'application. Le format est « tsalab.experiment/1 », en lecture tolérante :
  un enregistrement illisible est ignoré.
- **Plusieurs expériences indépendantes** : chaque projet a son propre journal, et un calcul ajoute un enregistrement
  sans jamais remplacer celui d'un autre calcul ni d'un autre projet. « Ouvrir un journal… » consulte le journal
  d'un autre projet. « Reprendre le modèle… » copie le modèle figé d'une expérience dans un fichier choisi, puis
  l'ouvre.
- **Rejeu** : le modèle figé est rechargé, avec le même contexte et les moteurs du poste. Les résultats sont comparés
  grandeur par grandeur (identiques si |Δ| ≤ 1e-9·|x| + 1e-12).
- **Cas refusés** : un rejeu est refusé si la copie figée a été modifiée (empreinte différente). Une version de
  moteur différente est signalée.

Vérifié dans TSALab réel le 12/10/2026 : calcul Custom2D du portique plan enregistré, rejeu identique sur
9 grandeurs (test L10 : rejeu identique d'un calcul OpenSees).

## 4. Tests

| Test | Objet |
| :--- | :--- |
| L9 | cas du banc (27 valides), cas invalide signalé, exécution réelle (RDM-01, RDM-06 sur deux moteurs), en-tête du rapport, arrêt, référence externe erronée, référence nulle |
| L10 | enregistrement complet, relecture, rejeu identique, copie figée modifiée refusée, calcul en échec, exécution du banc, enregistrement illisible ignoré, signal de fin pour un calcul en console |
| L11 | première expérience complète : modèle par commandes, deux moteurs, référence analytique, hypothèses non remplies nommées, enregistrement, réouverture, journal relu |
| L12 | laboratoire matriciel : K, F, U, DDL exportés, K·U = F, exports CSV, transformations (OpenSees avancé), refus des données absentes |
| L13 | convergence en maillage : ordre et Richardson exacts sur une série synthétique, cas dégénérés, FEA-01 réel (Gmsh → OpenSees) |
| L6 (Blueprint) | expérience en nœuds : 3 comparaisons dans la tolérance, modèle reconnu par la référence, section hors catalogue arrêtée |
| 389 (TSA) | commandes du laboratoire : section, lecture de barre, réaction, efforts, comparaison, Annuler |
| S1–S6 | cœur scientifique : solveurs, MetDeDeplacement, 7 benchmarks plans, OpenSees ≈ MetDeDeplacement |
