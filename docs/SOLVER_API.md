# API des solveurs

La mission demandait une interface du type `IStructuralSolver`. Elle existe dans la base commune sous le nom
`TSA::Analysis::AnalysisEngine` (`TSA/src/Analysis/Engine/AnalysisEngine.h`, détails dans
`TSA/docs/ANALYSIS_ENGINES.md`). TSALab l'utilise telle quelle : une seconde interface dupliquerait l'existante.

## 1. Contrat d'un moteur

| Méthode | Rôle |
| :--- | :--- |
| `info()` | identifiant, nom, **version réellement constatée** (vide si inconnue, jamais inventée), description |
| `capabilities()` | ce que le moteur fait réellement (tableau ci-dessous) |
| `availability()` | disponibilité sur le poste (exécutable présent…), raison explicite sinon |
| `provision()` | installation automatique si possible, avec l'accord de l'utilisateur |
| `defaultSettings()` | réglages propres au moteur (JSON) |
| `validate(context, model)` | contrôles propres au moteur (les contrôles communs sont faits par `AnalysisManager`) |
| `run(context, model, callbacks)` | calcul sur le modèle d'analyse neutre ; progression, journal, annulation |

Un moteur ne reçoit jamais le modèle TSA : il reçoit un `AnalysisModel` préparé et validé. Les résultats reviennent
indexés par identifiants TSA (`ResultsModel`).

## 2. Capacités déclarées

`AnalysisCapabilities` couvre :
- **les dimensions** : 2D, 3D ;
- **les familles d'éléments** : barres fléchies, treillis, câbles, coques, volumes, ressorts ;
- **les types d'analyse** : statique linéaire, non linéaire, modale ; le drapeau flambement existe, sans type
  d'analyse associé ;
- **les résultats fournis** : déplacements, réactions, efforts, matrices élémentaires et globale, numérotation des DDL ;
- **la politique pour les éléments non pris en charge** : refus, ou exclusion **avec avertissement explicite**.

L'interface (types d'analyse proposés, onglets de résultats) et la validation générique sont pilotées par ces
capacités. Aucun code ne teste l'identité d'un moteur. Une option qu'aucun moteur ne déclare n'est pas proposée.

Moteurs intégrés au 12/10/2026 :

| Moteur | Version lue | Dim. | Éléments | Analyses |
| :--- | :--- | :--- | :--- | :--- |
| OpenSees (processus externe) | 3.8.0 | 3D | barres, treillis, câbles, coques (maillées), ressorts | statique, non linéaire, modale |
| Custom2D (MetDeDeplacement, `tsalab_science`) | 2.0.0 | 2D | barres | statique |

## 3. Ajouter un moteur ou un solveur expérimental

1. Écrire un adaptateur dérivant de `AnalysisEngine`, avec ses capacités réelles et sa disponibilité.
2. L'enregistrer dans `registerBuiltInEngines` (`TSA/src/Analysis/Engines/BuiltInEngines.cpp`). Un moteur propre
   au laboratoire peut être ajouté au registre de la session de TSALab (`m_session->analysis().registry()`) ; ce
   chemin n'est pas encore couvert par un test ni par un exemple.
3. Ajouter ses cas au banc de validation (`tests/benchmarks`, champ `engines`) : l'espace Validation les exécute et
   compare ses résultats aux références analytiques.

Un solveur expérimental purement numérique (sans modèle structurel) va dans `tsalab_science` : C++ pur, testé par
`tsalab_science_tests`.

**Sécurité — limite actuelle** : les plugins (`TSA/docs/PLUGINS.md`) sont des DLL natives chargées **dans le
processus** de l'application, sur action explicite de l'utilisateur ; rien ne protège l'application d'une DLL non
fiable. OpenSees, lui, s'exécute dans un processus séparé. La feuille de route prévoit d'isoler les extensions
(processus séparé ou extensions signées) avant d'en encourager l'usage (`LABORATORY_ROADMAP.md`).

## 4. Solveurs instrumentés (SOLVER LAB)

`tsalab::numerics` (dans `science/`) fournit Gauss LU, Cholesky et gradient conjugué instrumentés : itérations,
résidus, pivots (détection de mécanisme), conditionnement. SOLVER LAB rejoue avec eux le système `K·U = F` exporté
par le dernier calcul (`lab/Research/Solver`) et mesure l'écart au moteur (test L7).
