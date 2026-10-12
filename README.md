<p align="center">
  <img src="resources/branding/TSA_Banner.svg" alt="TSALab - Tsaraloha Structural Analysis Laboratory Banner" width="100%" />
</p>

<p align="center">
  <img src="resources/branding/TSA_Logo.svg" alt="TSALab Logo" width="130" />
</p>

<h1 align="center">TSALab — Tsaraloha Structural Analysis Laboratory</h1>

<p align="center">
  <strong>Laboratoire virtuel de modélisation CAO 3D B-Rep exacte, d'analyse structurelle multi-moteurs (FEA / RDM), d'interopérabilité openBIM (IFC 4.3) et de co-ingénierie assistée par IA pour le génie civil.</strong><br>
  Développé en <b>C++20</b> avec <b>Qt 6</b>, le noyau géométrique <b>OpenCASCADE (OCCT 8.0.1)</b>, le moteur de calcul <b>OpenSees</b> et le moteur analytique <b>MetDeDeplacement (Custom2D)</b>.
</p>

<p align="center">
  <a href="https://en.cppreference.com/w/cpp/20"><img src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=for-the-badge&logo=c%2B%2B&logoColor=white" alt="C++20" /></a>
  <a href="https://www.qt.io/"><img src="https://img.shields.io/badge/Qt-6.2%2B-41CD52?style=for-the-badge&logo=qt&logoColor=white" alt="Qt 6" /></a>
  <a href="https://dev.opencascade.org/"><img src="https://img.shields.io/badge/OpenCASCADE-8.0.1-0072FF?style=for-the-badge" alt="OpenCASCADE 8.0.1" /></a>
  <a href="https://opensees.berkeley.edu/"><img src="https://img.shields.io/badge/OpenSees-FEA%20Engine-E91E63?style=for-the-badge" alt="OpenSees" /></a>
  <a href="docs/BIM_ARCHITECTURE.md"><img src="https://img.shields.io/badge/openBIM-IFC%204.3-0284c7?style=for-the-badge&logo=buildingsmart&logoColor=white" alt="openBIM IFC 4.3" /></a>
  <a href="docs/AI_COENGINEERING.md"><img src="https://img.shields.io/badge/AI-Co--Engineering-8b5cf6?style=for-the-badge&logo=openai&logoColor=white" alt="AI Co-Engineering" /></a>
  <a href="https://www.microsoft.com/windows"><img src="https://img.shields.io/badge/Platform-Windows%20x64-0078D6?style=for-the-badge&logo=windows&logoColor=white" alt="Windows x64" /></a>
  <a href="https://cmake.org"><img src="https://img.shields.io/badge/CMake-Ninja-064F8C?style=for-the-badge&logo=cmake&logoColor=white" alt="CMake & Ninja" /></a>
  <a href="#-tests-automatisés"><img src="https://img.shields.io/badge/Tests%20TSALab-7%2F7%20%C2%B7%20science%206%2F6-10b981?style=for-the-badge&logo=checkmarx&logoColor=white" alt="Tests TSALab 7/7, science 6/6" /></a>
  <a href="docs/NORMATIVE_SYSTEM.md"><img src="https://img.shields.io/badge/Eurocodes-EN%201990--1999-f59e0b?style=for-the-badge" alt="Eurocodes" /></a>
</p>

<p align="center">
  <a href="#-présentation">Présentation</a> •
  <a href="#-getting-started--démarrage-rapide">Démarrage Rapide</a> •
  <a href="#-optimisation-du-pipeline-de-build">Build & Pipeline</a> •
  <a href="#-architecture-logicielle--source-de-vérité">Architecture</a> •
  <a href="#-fonctionnalités-majeures">Fonctionnalités</a> •
  <a href="#-ruban-principal--ergonomie-cao">Interface</a> •
  <a href="#-tests-automatisés">Tests</a> •
  <a href="#-système-dextensions-tsalib">TSALib</a> •
  <a href="#-organisation-du-dépôt">Organisation</a> •
  <a href="#-documentation-technique">Documentation</a>
</p>

---

## 📖 Présentation

**TSALab (Tsaraloha Structural Analysis Laboratory)** est un laboratoire virtuel et une suite logicielle desktop d'ingénierie des structures de nouvelle génération. Issu et propulsé par la base technologique de TSA, TSALab repousse les frontières de l'analyse structurelle en combinant la modélisation CAO volumique exacte (**OpenCASCADE Technology**), une architecture d'analyse multi-moteurs (**OpenSees 3.8.0** et le solveur de RDM plane **MetDeDeplacement** en C++20), l'interopérabilité native **openBIM (IFC 4.3)**, un assistant de **co-ingénierie par IA locale**, ainsi qu'une interface utilisateur réactive (**Qt 6**) orchestrée par la fenêtre moderne **AppShell** (Start Center et espace de travail à la demande).

TSALab permet de modéliser, vérifier, charger, analyser et inspecter en 3D interactive des bâtiments à étages, des halles industrielles, des fermes et treillis spatiaux, des structures haubanées et des systèmes de fondations selon les normes européennes (**Eurocodes**).

> **Base technique commune avec TSA.** TSALab ne contient pas de copie du code de TSA : il compile directement les
> sources du dépôt [TSA](https://github.com/ChristinotLeonnel/Tsaraloha-Structural-Analysis) (cloné à côté, `../TSA`).
> Un bug corrigé dans TSA l'est donc aussi dans TSALab. Ce dépôt ne contient que ce qui est propre au laboratoire :
> son identité (`product/`, format `.tsalab`), ses modules de recherche et ses espaces (`lab/`), ses icônes.

### Ce que le laboratoire ajoute à TSA

* **Start Center laboratoire** : nouveau modèle, ouverture des projets `.tsalab` et import des modèles TSA (`.tsa`,
  jamais réécrits), exemples à solution analytique connue (console, poutres, portique, treillis, portique spatial).
* **Espaces du laboratoire** autour du viewport commun :
  * **Modèle** — modélisation, calcul, résultats et note de calcul (bibliothèques partagées avec TSA) ;
  * **Blueprint** — programmation visuelle des expériences (nœuds typés, débogueur, exécution pas à pas) ;
  * **Analyse** — gestionnaire d'analyse partagé (moteurs, réglages, validation, calcul en tâche de fond) ;
  * **Résultats** — diagrammes 2D par barre ;
  * **Recherche (SOLVER LAB)** — rejoue la résolution `K·U = F` du dernier calcul avec des solveurs instrumentés
    (Gauss LU, Cholesky, gradient conjugué) : itérations, résidus, pivots (détection de mécanisme), écart au moteur,
    conditionnement spectral, courbe de convergence ;
  * **Validation** — banc de validation partagé avec TSA (27 cas, 144 vérifications) : cas de référence calculés par la
    chaîne réelle, écarts absolu et relatif à la solution analytique ou à des résultats externes, rapports md/csv/json ;
    **étude de convergence en maillage** (écart par maillage, ordre observé, extrapolation de Richardson, export CSV) ;
  * **Expériences** — journal de chaque calcul et de chaque validation (versions, moteur, modèle figé et son empreinte,
    hypothèses, unités, statut, avertissements, résultats, provenance et **référence analytique**) et **rejeu** avec
    comparaison des résultats ; journal d'un autre projet, reprise du modèle figé d'une expérience.
* **Première expérience de référence** : poutre bi-appuyée avec force centrale. La référence d'Euler-Bernoulli est
  appliquée au modèle de l'utilisateur après vérification de ses hypothèses : réactions, flèche, moment et effort
  tranchant comparés. La même expérience existe entièrement en nœuds Blueprint.
* **Laboratoire matriciel** (dock « Données d'analyse ») : DDL, K globale, matrices élémentaires, vecteurs F et U,
  transformations local / global, exports CSV des seules données fournies par le moteur.
* **Affichage** : échelles des annotations et des charges, profils d'affichage, diagnostic (composant partagé avec TSA).
* **Suite de tests `lab`** (L4–L13) et tests du cœur scientifique (S1–S6), en plus des tests de la base commune
  (dépôt TSA). Documentation du laboratoire : [`docs/`](docs/).

---

## 🚀 Getting Started / Démarrage Rapide

### Prérequis Système & Environnement
* **Système d'exploitation :** Windows 10 / 11 (64-bit)
* **Compilateur :** MSVC (Visual Studio 2022 ou 2026 x64) avec support complet C++20 *(recommandé)* ou MinGW-w64 (GCC 13+)
* **CMake :** Version 3.20+
* **Ninja :** Intégré à Visual Studio (composant *Outils CMake pour Windows*) ou via `winget install Ninja-build.Ninja`
* **Qt 6 :** Version 6.2+ (`Core`, `Gui`, `Widgets`, `Svg`, `Network`) — ex. `C:\Qt\6.11.2\msvc2022_64`
* **OpenCASCADE 8.0.1 & Dépendances 3rdparty :** Téléchargées et extraites automatiquement par CMake si absentes de la machine.

---

### <ins>1. Téléchargement du Dépôt</ins>

Clonez **les deux dépôts côte à côte** (TSALab compile les sources de TSA) :

```powershell
git clone https://github.com/ChristinotLeonnel/Tsaraloha-Structural-Analysis.git TSA
git clone https://github.com/ChristinotLeonnel/Tsaraloha-Structural-Analysis-Virtual-Laboratory.git TSALab
cd TSALab
```

Un autre emplacement de TSA s'indique à CMake : `-DTSA_ROOT_DIR=<chemin>`. OpenCASCADE et les dépendances
3rdparty sont celles du dépôt TSA (téléchargées une seule fois, dans `../TSA`).

---

### <ins>2. Dépendances</ins>

Aucune étape manuelle : à la première configuration, CMake télécharge OpenCASCADE 8.0.1 et les dépendances 3rdparty
**dans le dépôt TSA** s'ils sont absents (`TSA/cmake/SetupDependencies.cmake`). TSA et TSALab partagent ces SDK.

---

### <ins>3. Compilation du Projet</ins>

#### Option A : Ninja Presets *(Recommandé pour le développement quotidien)*
Ninja offre la vitesse de compilation maximale grâce à la compilation parallèle et aux en-têtes précompilés (PCH) :

```powershell
# 1. Charger l'environnement MSVC dans le terminal courant (une seule fois par session PowerShell)
Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass -Force
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
& "$vs\Common7\Tools\Launch-VsDevShell.ps1" -Arch amd64 -HostArch amd64

# 2. Configuration & compilation Debug
cmake --preset ninja-debug
cmake --build --preset ninja-debug -- -k 0

# Ou compilation Release
cmake --preset ninja-release
cmake --build --preset ninja-release
```

#### Option B : Visual Studio Presets
```powershell
# Configuration & compilation Release
cmake --preset windows-x64-release
cmake --build --preset windows-x64-release
```

---

### <ins>4. Lancement de TSALab</ins>

Utilisez le script automatique à la racine du projet :

```cmd
run.bat
```

> **Note :** `run.bat` initialise les variables d'environnement OpenCASCADE (`CSF_OCCTResourcePath`, `CSF_OCCTShadersPath`), injecte les bibliothèques dynamiques Qt et tierces dans le `PATH`, et démarre automatiquement le binaire le plus récent (`build-ninja-debug`, `build-ninja-release` ou `build/Release`). Pour cibler un dossier spécifique :
> ```cmd
> run.bat build-ninja-debug
> ```

Au premier lancement, la fenêtre principale **AppShell** s'affiche instantanément (< 1 s) sur le **Start Center** (galerie des projets récents avec aperçus 3D réels, création rapide, import). L'espace de travail complet (`MainWindow`) est ensuite instancié à la demande lors de l'ouverture ou de la création d'un projet.

---

## ⚡ Optimisation du Pipeline de Build

Le temps de compilation de TSALab est optimisé pour les développeurs C++ exigeants :

* **Bibliothèque `TSALab_Core` (OBJECT) :** Tout le code métier (modèle structural, calculs multi-moteurs, couche BIM, IA, commandes, grilles, I/O, extensions) est compilé **une seule et unique fois**, puis lié directement à l'exécutable principal `TSALab` et à la suite de tests `TSALab_Tests`.
* **En-têtes Précompilés (PCH) Partagés :** Les en-têtes volumineux stables (Qt Widgets/Core, OpenCASCADE `gp_*`/`TopoDS_Shape`, STL C++20) sont précompilés et partagés entre toutes les cibles.
* **Includes OpenCASCADE en `SYSTEM` :** Élimine le coût d'analyse des avertissements issus des bibliothèques externes.
* **Extension Shell Autonome (`TSALabThumbnailProvider.dll`) :** Développée en C++/Win32 pur sans dépendance Qt ni OCCT, garantissant une exécution ultra-rapide et sécurisée au sein du processus de l'Explorateur Windows.

---

## 🏛️ Architecture Logicielle & Source de Vérité

TSALab applique un principe architectural strict : **le modèle structural (`TSA::Model::Model`) est la source de vérité unique**.

```text
               +-------------------------------------------------------------+
               |                  Fenêtre Hôte AppShell                      |
               |          TitleBar (DWM) | StartCenter | MainWindow          |
               +-------------------------------------------------------------+
                                              |
                                              | Signaux / Commandes réversibles
                                              v
               +-------------------------------------------------------------+
               |           Commands & Undo/Redo (ICommand, Manager)          |
               |             Model Snapshots & Transactions d'Édition        |
               +-------------------------------------------------------------+
                                              |
                                              v
   =========================================================================================
   ===               MODÈLE STRUCTURAL (TSA::Model::Model) — SOURCE DE VÉRITÉ            ===
   ===       Node, Beam, Column, Cable, Slab, Wall, Foundation, Section, Material        ===
   =========================================================================================
          /                           |                           |                    \
         /                            |                           |                     \
        v                             v                           v                      v
+----------------+            +----------------+          +----------------+     +---------------+
| Géométrie 3D   |            | Multi-Moteurs  |          | Couche openBIM |     | Co-Ingénierie |
| (*Geometry)    |            | AnalysisManager|          |   (BimModel)   |     |   IA Locale   |
+----------------+            +----------------+          +----------------+     +---------------+
        |                             |                           |                      |
        v                             v                           v                      v
+----------------+            +----------------+          +----------------+     +---------------+
| Viewer OCCT    |            | Solveurs:      |          | StepWriter     |     | llama-server  |
| AIS_Shape      |            | • OpenSees 3D  |          | IfcExporter    |     | RAG Modèle    |
| OccView, V3d   |            | • Custom2D MDD |          | IFC4X3_ADD2    |     | Hardware Diag |
+----------------+            +----------------+          +----------------+     +---------------+
                                      |
                                      v
                              +----------------+
                              | Résultats RDM  |
                              | & Note Calcul  |
                              +----------------+
```

* Aucun widget UI ne manipule directement la géométrie OpenCASCADE.
* Aucune forme OCCT (`TopoDS_Shape`) n'est utilisée pour stocker des données mécaniques.
* Les modifications d'état transitent obligatoirement par le système de commandes transactionnelles avec support complet Undo/Redo.
* La couche openBIM maintient un mapping biunivoque non-destructif (1:N) entre produits physiques et éléments analytiques.

---

## ✨ Fonctionnalités Majeures

### 1. 🏗️ Modélisation Structurale & Noyau CAO 3D B-Rep
* **Solides B-Rep Exacts (OpenCASCADE 8.0.1)** : Pas de facettisation polygonale approximative ; géométrie volumique exacte pour tous les profilés, nœuds et assemblages.
* **Éléments Filaires 1D** : Poutres, poteaux, barres spatiales génériques, câbles et haubans tendus avec prise en compte du comportement élastique non-linéaire.
* **Générateur Automatique de Treillis** : Création instantanée et paramétrique de fermes métalliques de types **Warren** (diagonales alternées), **Pratt** (diagonales tendues) et **Howe** (diagonales comprimées).
* **Éléments Surfaciques 2D** : Dalles et planchers (report de charge unidirectionnel ou bidirectionnel), voiles et murs porteurs banchés, semelles superficielles de fondation.
* **Plans de Travail 3D Interactifs (WorkPlanes)** : Définition par 3 points arbitraires ou alignement sur les niveaux d'étages ; projection bidirectionnelle non-destructive 2D $\leftrightarrow$ 3D et magnétisme intelligent.
* **Accrochage 3D en Espace Écran (OSNAP)** : Système d'accrochage haute précision avec priorités contextuelles (Nœud > Milieu > Intersection > Centre > Grille).
* **Conditions d'Appuis Spatiales (6 DDL)** : Encastrements complets, rotules/articulations, appuis simples (rouleaux), degrés de liberté découplés ($T_x, T_y, T_z, R_x, R_y, R_z$) représentés par des solides B-Rep paramétriques (plaques d'assise, cônes, cylindres).
* **Actions & Combinaisons Eurocodes** : Forces et moments nodaux, charges réparties uniformes et trapézoïdales, calcul automatique du poids propre volumique, cas de charges et combinaisons ELU / ELS.

### 2. 📐 Outils Avancés de Modélisation & Nettoyage Topologique
* **14 Outils de Modification 3D** : Déplacer, Copier, Rotation, Symétrie (plans X, Y, Z et quelconques), Échelle, Réseaux linéaires, Réseaux polaires, Décaler, Diviser en N tronçons, Diviser au point, Intersecter des barres, Prolonger, Ajuster, Fusionner des barres alignées.
* **6 Outils de Dessin Paramétrique** : Tracé en chaîne continue, Rectangle 3D, Portique plan paramétrique, Contreventement en X, Arc de cercle discrétisé, Génération de poteaux sur trame de grille.
* **Nettoyage Topologique Automatique (`ModelCleanup`)** :
  * Fusion automatique des nœuds confondus selon une tolérance paramétrable.
  * Détection et suppression des barres en doublon avec report automatique des charges associées.
  * Raccordement des nœuds intermédiaires situés sur des barres continues (division topologique).
  * Suppression des nœuds isolés sans éléments raccordés.
  * Audit de santé pré-calcul proposé automatiquement avant toute résolution numérique.
* **Édition Groupée Multi-Éléments** : Sélection multiple avec report contextuel des modifications géométriques et matérielles au sein de sessions d'édition sécurisées.
* **Système d'Isolation 3D Unifié** : Isoler la sélection, isoler par type de famille, isoler le plan de travail actif, masquer, inverser et pile d'historique d'isolation.

### 3. 🧮 Architecture d'Analyse Multi-Moteurs (Multi-Engine)
TSALab intègre une architecture de calcul découplée (`AnalysisManager`) permettant d'exécuter différents moteurs d'analyse sur des portées arbitraires (modèle complet, sélection, axe de grille, niveau d'étage, plan de travail) :

* **Moteur 1 : Solveur Éléments Finis 3D (OpenSees 3.8.0)**
  * Analyses statiques linéaires et non-linéaires au second ordre (effets $P\text{-}\Delta$, non-linéarités géométriques).
  * Algorithmes de résolution avancés : Newton-Raphson standard, Newton avec recherche linéaire (*NewtonLineSearch*), Newton modifié, *Krylov-Newton*, *BFGS*, *Broyden*, *SecantNewton*.
  * Stratégies d'incrémentation : *LoadControl*, *DisplacementControl*, Longueur d'arc (*Arc-Length* / Crisfield), Norme de déplacement non équilibré minimale (*MinUnbalDispNorm*).
  * Formulations d'éléments standards et corotatifs (*corotTruss*) pour la stabilité en grands déplacements.
  * Prise en compte rigoureuse des rotules d'extrémités et des degrés de liberté relâchés.

* **Moteur 2 : Solveur Analytique 2D (MetDeDeplacement / Custom2D)**
  * Moteur de calcul plan C++20 sans dépendance, implémentant la méthode matricielle des déplacements.
  * Calcul exact de la déformée par intégration analytique de la ligne élastique.
  * Évaluation continue des diagrammes d'efforts tranchants et de moments de flexion sans discrétisation grossière.
  * Intégration transparente des courbes dans la Note de Calcul réglementaire.

* **Conventions RDM Normalisées & Validité** :
  * Convention stricte conforme à la RDM classique : effort normal $N > 0$ en traction, fibres tendues locales pour les moments, équilibre statique vérifié en forces et en moments.
  * Invalidation dynamique automatique des résultats (`ResultsValidityGuard`) dès modification du modèle structural.

### 4. 🏢 Couche openBIM & Interopérabilité IFC 4.3
* **Mapping Physique $\leftrightarrow$ Analytique 1:N** : Séparation claire entre les entités physiques de construction (`IfcBeam`, `IfcColumn`, `IfcSlab`, `IfcWall`, `IfcFooting`, `IfcBuildingStorey`) et les barres/nœuds du modèle analytique.
* **Identifiants Universels Stables (`IfcGuid`)** : Conservation stricte des identifiants BIM à travers les opérations de modélisation, les transactions Undo/Redo et la division d'éléments.
* **Moteur STEP & IFC Natif** :
  * Exportateur haute fidélité conforme à la norme **IFC4X3_ADD2** avec export simultané des produits physiques et du modèle analytique structurel.
  * Importateur multi-schémas (**IFC4X3**, **IFC4**, **IFC2X3**) capable de reconstruire le modèle structural à partir des représentations géométriques B-Rep ou des éléments analytiques IFC.
  * Validation automatique certifiée 0 anomalie sur les suites de tests IfcOpenShell.

### 5. 🤖 Co-Ingénierie Assistée par Intelligence Artificielle Locale
* **Assistant Structurel Souverain & 100% Hors-Ligne** : Orchestration locale d'un moteur LLM (`llama-server`) sans aucune transmission de données vers des services cloud externes.
* **Profilage Matériel Dynamique** : Analyse au lancement des capacités de la machine hôte (cœurs CPU, GPU dédié, VRAM disponible, backends Vulkan/CUDA) et sélection automatique des paramètres d'inférence optimaux.
* **Diagnostic & Vérification de Structure** : Détection assistée des instabilités géométriques, surcharges locales, éléments sous-dimensionnés et non-conformités normatives.
* **Analyse Contextuelle RAG Structurale** : Réponses fondées directement sur la topologie réelle du modèle `.tsalab`, ses descentes de charges et les exigences des Eurocodes.

### 6. 📊 Visualisation 3D des Résultats & Inspection
* **Déformée 3D Amplifiée** : 3 modes d'affichage (Initial, Déformé seul, Superposition) calibrés automatiquement sur l'envergure caractéristique ($L_{span}$).
* **Diagrammes 3D dans les Repères Locaux** :
  * Moments de flexion et de torsion : $M_z$ (axe fort), $M_y$ (axe faible), $M_x$ (torsion).
  * Efforts tranchants : $V_z$ et $V_y$.
  * Effort axial : Traction et compression $N$.
  * Déplacements et rotations nodales : $U_x, U_y, U_z, U_{res}, R_x, R_y, R_z$.
* **Légende 3D Interactive & Réactions d'Appuis** : Surimpression graphique dynamique avec extrema, échelle et unités du Système International ($kN, kNm, mm, rad$). Flèches vectorielles 3D des réactions aux nœuds d'appuis.
* **Générateur Professionnel de Note de Calcul (NDC)** : Génération automatique de rapports d'ingénierie complets au format PDF et HTML incluant descriptifs normatifs, tableaux des sollicitations extrêmes et graphiques vectoriels.

---

## 🧭 Interface du laboratoire

TSALab a sa propre fenêtre (`lab/LabApp/LabMainWindow`), distincte de celle de TSA (pas de ruban, pas d'AppShell) :
menus **Fichier, Exemples, Édition, Affichage, Modèle, Blueprint, Analyse, IA, Aide**, barre d'outils de modélisation
(Sélection, Nœud, Poutre, Poteau, Barre, Calculer) et onglets d'espaces **Accueil, Modèle, Blueprint, Analyse,
Résultats, Recherche, Validation, Expériences**.
Documentation : architecture, API des solveurs, validation, feuille de route et historique des synchronisations avec
TSA (`docs/SYNC_TSA_TSALAB.md`).

Panneaux : explorateur du projet, propriétés, résultats structuraux 3D, console (commandes du registre central),
données d'analyse (K, F, U), assistant IA. Un seul projet par fenêtre (voir `docs/LABORATORY_ROADMAP.md`).

---

## 🧪 Tests Automatisés

| Exécutable | Contenu | État au 12/10/2026 |
| :--- | :--- | :--- |
| `TSALab_TestSuite` | L4 exemples et format `.tsalab`, L5 import `.tsa`, L6 Blueprints (dont l'expérience en nœuds), L7 analyse et SOLVER LAB, L8 plugin d'exemple, L9 laboratoire de validation, L10 journal d'expériences, L11 première expérience complète, L12 laboratoire matriciel, L13 convergence en maillage | 10 / 10 |
| `tsalab_science_tests` | S1–S6 : algèbre linéaire, solveurs plans, MetDeDeplacement, benchmarks plans, OpenSees ≈ MetDeDeplacement | 6 / 6 |
| `TSA_TestSuite` (dépôt TSA) | base commune : modèle, IO, vue, analyse, résultats, maillage, coques, validation, commandes… | 389 tests dans TSA |

```powershell
cmake --build --preset ninja-debug
.\build-ninja-debug\TSALab_TestSuite.exe
ctest --test-dir build-ninja-debug --output-on-failure
```

---

## 📦 Système d'Extensions TSALib

TSALab est équipé du système d'extensions dynamiques d'ingénierie **TSALib** (`TSA::ExtensionSystem`) :
* **Découplage Total du Binaire :** Matériaux Eurocodes, profilés métalliques européens (IPE, HEA, HEB, UPN), sections personnalisées, câbles et textures PBR sont stockés au format ouvert JSON/PNG et modifiables sans recompiler le logiciel.
* **Rechargement à Chaud (Hot Reload) :** Actualisation instantanée du modèle 3D et des bibliothèques en un clic.
* **Packaging Autonome (`.tsalib`) :** Importation et exportation d'archives compressées autonomes signées par somme de contrôle SHA-256 avec protection anti-Path-Traversal.
* **Reproductibilité des Calculs :** Snapshots mécaniques scellés dans le fichier de projet `.tsalab` (`CHUNK_SNAP`).

> 📖 **Pour en savoir plus, consultez le guide dédié : [docs/TSALIB_SYSTEM.md](docs/TSALIB_SYSTEM.md).**

---

## 🎮 Navigation & Raccourcis 3D

Les interactions et raccourcis clavier respectent scrupuleusement les exigences de la norme internationale de documentation technique **IEEE Std 1063-2001** :

| Action / Commande | Raccourci Clavier / Souris |
| :--- | :--- |
| **Rotation / Orbite 3D** | Clic droit maintenu + Déplacement souris |
| **Panoramique (Pan)** | Clic molette maintenu + Déplacement souris |
| **Zoom avant / arrière** | Molette de la souris |
| **Centrer la vue (Fit All)** | <kbd>F</kbd> |
| **Cadrer la sélection** | <kbd>Maj</kbd> + <kbd>F</kbd> |
| **Vue isométrique initiale** | <kbd>R</kbd> |
| **Vue d'accueil (Home)** | <kbd>Origine (Home)</kbd> |
| **Vue de dessus (Plan XY)** | Pavé numérique <kbd>7</kbd> |
| **Vue de face (Plan XZ)** | Pavé numérique <kbd>1</kbd> |
| **Vue de droite (Plan YZ)** | Pavé numérique <kbd>3</kbd> |
| **Isoler la sélection** | <kbd>I</kbd> |
| **Masquer la sélection** | <kbd>H</kbd> |
| **Tout réafficher** | <kbd>Alt</kbd> + <kbd>H</kbd> |
| **Inspecteur des Propriétés** | <kbd>P</kbd> |
| **Lancer le Calcul Statique** | <kbd>F5</kbd> |
| **Générateur Note de Calcul** | <kbd>F8</kbd> |
| **Assistant IA Co-Engineering** | <kbd>Ctrl</kbd> + <kbd>Espace</kbd> |
| **Plein Écran** | <kbd>F11</kbd> |
| **Aide contextuelle** | <kbd>F1</kbd> |

---

## 📁 Organisation du Dépôt

```text
TSA/                            # Dépôt TSA voisin : sources des bibliothèques partagées (src/), cmake/, tests de la base
TSALab/
├── product/                    # Identité du produit TSALab (nom, version, .tsalab, signature TSLB, réglages, CLSID)
├── lab/
│   ├── LabApp/                 # main.cpp, LabMainWindow (fenêtre et espaces du laboratoire)
│   ├── LabUI/                  # LabStartPanel, SolverLabPage, ValidationPage, ExperimentsPage
│   ├── Research/               # Examples (modèles et Blueprints d'exemple), Solver (SOLVER LAB),
│   │                           # Validation (banc partagé), Experiments (journal d'expériences)
│   └── Tests/LabTests.cpp      # Suite « lab » (TSALab_TestSuite)
├── science/                    # Cœur scientifique C++ pur (numerics, solveurs plans, MetDeDeplacement, validation),
│                               # utilisé aussi par TSA (moteur Custom2D) ; tests S1–S6, outil tsalab-bench
├── plugins/sample/             # Plugin d'exemple (API docs/PLUGINS.md du dépôt TSA)
├── docs/                       # Documentation du laboratoire (architecture, API des solveurs, validation, feuille de route, audit)
├── resources/                  # Icônes et ressources Windows de TSALab
├── CMakeLists.txt              # Trouve ../TSA, compile les bibliothèques partagées à l'identité de TSALab
└── run.bat                     # Lanceur (PATH Qt / OCCT / 3rdparty du dépôt TSA)
```

Documentation de la base commune : `../TSA/docs/` (architecture, format de fichier, moteurs d'analyse, BIM, IA…).

---

## ⚖️ Conformité Normative & Documentation

* **Eurocodes Structuraux :** Conception et combinaisons conformes aux normes **EN 1990** (Bases de calcul), **EN 1991** (Actions), **EN 1992** (Béton armé), **EN 1993** (Structures métalliques) et **EN 1998** (Calcul parasismique).
* **openBIM & IFC :** Support certifié des schémas de données **IFC4X3_ADD2**, **IFC4** et **IFC2X3** selon les standards internationaux de **buildingSMART**.
* **Documentation Technique :** Rédaction et structure conformes aux normes **IEEE Std 1063-2001 (R2007)** et **ISO/IEC/IEEE 26514:2022**.
* **Cycle de Vie Logiciel :** Adhésion aux exigences de qualité logicielle **ISO/IEC 25010** et d'ingénierie **ISO/IEC/IEEE 12207**.

---

## 👨‍💻 Auteur & Crédits

* **Concepteur & Développeur Principal :** Christinot TSARALOHA
* **Technologies Clés :** C++20 • Qt 6 • OpenCASCADE Technology • OpenSees • MetDeDeplacement • openBIM IFC • CMake • Ninja