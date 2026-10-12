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

## 2. Journal d'expériences (espace « Expériences »)

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

- **Stockage** : `<projet>.experiments/<horodatage>-<id>/record.json`, à côté du projet. Un modèle non enregistré
  va dans le dossier de données de l'application. Le format est « tsalab.experiment/1 », en lecture tolérante :
  un enregistrement illisible est ignoré.
- **Rejeu** : le modèle figé est rechargé, avec le même contexte et les moteurs du poste. Les résultats sont comparés
  grandeur par grandeur (identiques si |Δ| ≤ 1e-9·|x| + 1e-12).
- **Cas refusés** : un rejeu est refusé si la copie figée a été modifiée (empreinte différente). Une version de
  moteur différente est signalée.

Vérifié dans TSALab réel le 12/10/2026 : calcul Custom2D du portique plan enregistré, rejeu identique sur
9 grandeurs (test L10 : rejeu identique d'un calcul OpenSees).

## 3. Tests

| Test | Objet |
| :--- | :--- |
| L9 | cas du banc (27 valides), cas invalide signalé, exécution réelle (RDM-01, RDM-06 sur deux moteurs), en-tête du rapport, arrêt, référence externe erronée, référence nulle |
| L10 | enregistrement complet, relecture, rejeu identique, copie figée modifiée refusée, calcul en échec, exécution du banc, enregistrement illisible ignoré, signal de fin pour un calcul en console |
| S1–S6 | cœur scientifique : solveurs, MetDeDeplacement, 7 benchmarks plans, OpenSees ≈ MetDeDeplacement |
