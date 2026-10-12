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
- capacités lues dans `capabilities()` (`OpenSeesEngine.cpp`, `Custom2DEngine.cpp`) ;
- disponibilité des matrices constatée par les tests L7, L11 et L12.

| | OpenSees (processus externe) | Custom2D (MetDeDeplacement, `tsalab_science`) |
| :--- | :--- | :--- |
| Version lue | 3.8.0 | 2.0.0 |
| Dimension | 3D (6 DDL par nœud) | 2D (plan du modèle ou axe de grille) |
| Barres fléchies | `elasticBeamColumn`, Euler-Bernoulli | Euler-Bernoulli |
| Treillis, ressorts | oui (`truss`, `zeroLength`) | selon le solveur branché (`Custom2D::Features`) |
| Câbles | oui (`corotTruss` + précontrainte) | non |
| Coques | oui, maillées par Gmsh (`ShellMITC4`, `ShellDKGT`) | non : refus |
| Éléments non pris en charge | exclus, avec un avertissement explicite | refus du calcul |
| Analyses | statique linéaire, statique non linéaire, modale | statique linéaire |
| Déplacements, réactions, efforts | oui | oui |
| Numérotation des DDL, K globale | en extraction avancée (`extractionLevel = Advanced`) ; K plafonnée en nombre de DDL | avec l'option « Exporter le système K·U = F » |
| Vecteur F (second membre) | **non exporté** | avec la même option |
| Vecteur U | déduit des déplacements nodaux (signalé comme tel dans l'export) | exporté par le moteur |
| Matrices élémentaires (k_basic, k_local, K_global) et rotation local → global | en extraction avancée | **non exportées** : l'export des transformations est refusé, avec la raison |

**Fonctions non disponibles** (aucun moteur ne les déclare, donc absentes de l'interface) :
- flambement linéaire ;
- dynamique temporelle, analyse spectrale, pushover ;
- thermique ;
- éléments volumiques ;
- déformation d'effort tranchant des barres (Timoshenko).

La comparaison d'une barre courte à une référence avec cisaillement est donc hors domaine pour les deux moteurs.

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

## 4. Laboratoire matriciel

Les matrices sont celles **exportées par le moteur**, jamais recalculées par TSALab. Elles sont dans le dock « Données
d'analyse » (menu Analyse) et s'exportent ainsi :

| Données | Export | Si le moteur ne les fournit pas |
| :--- | :--- | :--- |
| numérotation des DDL | CSV, JSON | refus, avec la raison |
| K globale (format COO) | CSV, JSON | refus, avec la raison |
| vecteurs F et U par équation | CSV `equation,node,dof,F,U,U_source` | F : colonne vide, signalée en en-tête ; U déduit des déplacements, provenance « déplacements nodaux » |
| matrices élémentaires | TXT, JSON | refus, avec la raison |
| transformations local / global | CSV `element,class,nodeI,nodeJ,length,x_X…z_Z` | refus, avec la raison ; éléments manquants listés en fin de fichier |

Le test L12 vérifie :
- K·U = F à partir des seules données exportées par Custom2D (résidu ≤ 1e-9·|F|) ;
- l'export des transformations des deux barres par OpenSees en extraction avancée ;
- le refus de chaque donnée absente.

## 5. Solveurs instrumentés (SOLVER LAB)

`tsalab::numerics` (dans `science/`) fournit Gauss LU, Cholesky et gradient conjugué instrumentés : itérations,
résidus, pivots (détection de mécanisme), conditionnement. SOLVER LAB rejoue avec eux le système `K·U = F` exporté
par le dernier calcul (`lab/Research/Solver`) et mesure l'écart au moteur (test L7).
