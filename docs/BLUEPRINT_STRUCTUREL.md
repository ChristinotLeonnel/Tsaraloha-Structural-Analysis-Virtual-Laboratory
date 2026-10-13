# TSALab Blueprint — Structural Programming Laboratory

Programmation visuelle du calcul des structures : des classes pré-codées (élément filaire `FrameElement2D`, charges,
assemblage, conditions aux limites, résolution) se relient dans l'éditeur Blueprint pour construire et exécuter la
méthode des déplacements, sans réécrire l'élément fini. Chaque bloc exécute un vrai calcul ; le résultat d'un graphe se
compare au calcul direct, à des solutions analytiques et à un solveur indépendant.

État au 13/10/2026 : première version opérationnelle. Ce qui est fait est vérifié par des tests (§ 10) ; ce qui ne
l'est pas est listé au § 11.

## 1. Pour commencer

1. Ouvrir l'espace **Blueprint** de TSALab.
2. Menu **Blueprint ▸ Programmation structurelle ▸ Methode_Des_Deplacements_2D**.
3. **▶ Exécuter** (F5). Le journal affiche les étapes 1 à 9 ; le dock **Résultats 2D (Blueprint)** montre le portique, la
   déformée amplifiée (facteur affiché et réglable), les réactions et le diagramme N, V ou M choisi.
4. Onglet **Valeurs** de l'éditeur : chaque sortie produite (matrices k', T, k, K, K_ff, vecteurs F, d, R…). Double-clic
   ou Entrée sur un objet : tableau de la matrice (libellés des DDL, carte de couleurs), tracé d'un diagramme, portique
   d'un résultat.
5. Sélectionner un bloc puis **F1** (ou « Documentation… ») : formulation, hypothèses, unités, limites et code source.

Les paramètres du graphe (cas, combinaison, q, H, amplification) se modifient dans le panneau du bloc paramètre ; une
nouvelle exécution ne recalcule que ce qui dépend de la valeur changée (§ 7).

## 2. Conventions

| Grandeur | Convention |
| :--- | :--- |
| Unités | kN, m, kN/m² (module E), m², m⁴, °C, rad |
| Repère global | plan (X, Y) ; rotations et moments positifs dans le sens trigonométrique |
| Repère local | x' du nœud i vers le nœud j ; y' = x' tourné de +90° |
| DDL d'un nœud | ux, uy, θz |
| DDL d'un élément | [ux_i, uy_i, θz_i, ux_j, uy_j, θz_j] |
| Numérotation globale | le k-ième nœud du modèle (k = 0, 1, …) porte les DDL 3k, 3k + 1, 3k + 2 ; indices des blocs mathématiques à partir de 0 |
| Efforts d'extrémité f' | forces exercées SUR l'élément par ses nœuds, repère local [Fx_i, Fy_i, Mz_i, Fx_j, Fy_j, Mz_j] |
| Efforts intérieurs (RDM) | N > 0 en traction ; M > 0 tend la fibre y' < 0 (fibre inférieure d'une poutre parcourue de gauche à droite) ; V = dM/dx ; M(0) = −Mz_i, M(L) = Mz_j |
| Réactions | R = K·d − F : forces exercées par les appuis sur la structure |

## 3. La classe `FrameElement2D`

Élément de portique plan à 6 DDL : Euler-Bernoulli, élastique linéaire, section constante, petits déplacements.
Code : `science/include/tsalab/structural/FrameElement2D.h`, `science/src/structural/FrameElement2D.cpp`.

**Données** : identifiant, nœuds i et j (identifiant, coordonnées), matériau (E, α, γ), section (A, Iz, h), nom.
**Contrôle** (`validate`) : longueur non nulle, E, A, Iz positifs et finis, coordonnées finies. Le bloc
`FrameElement2D` échoue avec la raison si l'élément est invalide.

| Fonction | Formulation |
| :--- | :--- |
| `length`, `angle` | L = √(Δx² + Δy²) ; α = atan2(Δy, Δx) ; c = Δx/L, s = Δy/L |
| `localStiffness` | k' : EA/L (axial) ; 12EI/L³, 6EI/L², 4EI/L, 2EI/L (flexion) |
| `transformation` | d' = T·d, par nœud [c s 0 ; −s c 0 ; 0 0 1] ; T orthogonale |
| `globalStiffness` | k = Tᵀ·k'·T |
| `localLoads` | charges de l'élément converties dans le repère local (§ 4) ; charge invalide écartée avec sa raison |
| `fixedEndForces` | f0' = −∫ Nᵀ·p dx (fonctions de forme exactes de la barre prismatique : Hermite en flexion, linéaires en axial ; Gauss à 3 points, exact pour ces intégrandes de degré ≤ 4) ; moment concentré : −M·N'(a) ; déformation initiale : [EAε0, 0, EIκ0, −EAε0, 0, −EIκ0] |
| `equivalentNodalLoadsLocal/Global` | −f0' et Tᵀ·(−f0') |
| `endForces` | f' = k'·T·d + f0' |
| `diagram` | équilibre du tronçon [0, x] : N = −Fx_i − Px(x), V = Fy_i + Py(x), M = −Mz_i + x·Fy_i + MPy(x) − ΣM0 ; déformée u' = N/EA + ε0, v'' = M/EI + κ0 intégrée depuis le nœud i ; extremum exact de M où V = 0 ; deux points aux discontinuités ; contrôle de fermeture (u, v, v' en x = L comparés aux déplacements du nœud j) |
| `loadResultant` | résultante et moment des charges, calculés directement depuis leur définition (bilan d'équilibre indépendant de F) |

Pourquoi les efforts d'encastrement sont exacts : pour une barre prismatique d'Euler-Bernoulli, les fonctions d'Hermite
sont les solutions exactes de l'équation homogène ; le travail des charges sur ces fonctions donne exactement les
réactions de la barre bi-encastrée (qL/2 et qL²/12 pour une charge uniforme, P·a·b²/L² pour une force excentrée…),
et les déplacements nodaux du modèle assemblé sont exacts.

## 4. Charges

Les charges d'élément sont mesurées depuis le nœud i, en mètres ou en fraction de L (« positions relatives »). Une
charge de direction globale est décomposée dans le repère local : (px, py) = (c·qX + s·qY, −s·qX + c·qY).

| Charge (bloc) | Données, unités | Repère, transformation | Hypothèses, limites | Test de référence |
| :--- | :--- | :--- | :--- | :--- |
| Charge nodale | Fx, Fy (kN), Mz (kN·m), composantes simultanées | global, ajoutée à F | — | S8, S9, S16 (moment nodal ML/EI, ML²/2EI) |
| Charge répartie | q1 en a, q2 en b (kN/m) ; uniforme, triangulaire, trapézoïdale, partielle | x local, y local, X global, Y global ; « projetée » (direction globale) : q·\|c\| pour Y, q·\|s\| pour X | linéaire entre a et b, a < b dans l'élément | S8 (axiale pL²/2EA), S10 (5qL⁴/384EI, qL²/12, triangulaire qL²/(9√3), partielle, projetée ou non) |
| Charge uniforme | q (kN/m) sur toute la longueur | idem | — | S10 |
| Charge multi-segments | points « a:q ; … » reliés linéairement | idem | positions strictement croissantes | S10 (≡ segments séparés) |
| Force ponctuelle | P (kN) en a | x local (axiale), y local (transversale), X ou Y global | a dans l'élément ; saut de V (ou de N) en a | S8 (P·b/L, P·a/L), S9 (Pab/L, Pa²b²/3EIL) |
| Moment ponctuel | M (kN·m) en a | autour de z | saut de M en a | S7 (M/4, 3M/2L), S10 (M·a/L, −M·b/L) |
| Poids propre | γ·A (kN/m) × coefficient ; élément 0 = tous | −Y global, par mètre de longueur réelle | γ > 0 requis | S10 |
| Action thermique | ΔT uniforme (°C), gradient T(y'<0) − T(y'>0) (°C) | ε0 = α·ΔT, κ0 = α·gradient/h | α > 0 ; h > 0 pour le gradient ; température linéaire dans la hauteur | S8 (N = −EAαΔT, allongement αΔT·L), S10 (M = −EIκ0, flèche −κ0L²/8) |
| Déformation initiale | ε0, κ0 (1/m) : défaut de longueur, précontrainte par déformation imposée | mêmes efforts que l'action thermique | élément bloqué : N = −EAε0, M = −EIκ0 | S8, S10 |
| Déplacement imposé | ux, uy (m) ou θz (rad) d'un DDL **bloqué par un appui** | partition K_ff·d_f = F_f − K_fc·d_c ; jamais une force nodale | refusé sur un DDL libre | S13 (12EIΔ/L³, 6EIΔ/L², 4EIθ/L, 6EIΔ/L³ en poutre continue) |

Combinaisons : Σ γk × cas k, charges ET déplacements imposés multipliés (superposition linéaire, S16). Les
coefficients sont ceux saisis ; aucun coefficient normatif n'est ajouté par les blocs.

**Non implémentées (non exposées)** : actions sismiques, dynamiques, charges de trafic et mobiles. Elles demandent un
moteur adapté (analyse modale ou temporelle, lignes d'influence) et des cas de validation avant toute exposition.

## 5. La méthode des déplacements, étape par étape

| Étape | Calcul | Fonction du cœur scientifique | Blocs |
| :--- | :--- | :--- | :--- |
| 1. Modèle | nœuds, éléments, matériaux, sections, appuis, cas ; contrôle | `validate`, `resolveLoads` | Portique de démonstration (ou construction par blocs), Valider le modèle, Chargement à calculer |
| 2. Éléments | L, α, k', T, k = Tᵀ·k'·T, charges équivalentes | `FrameElement2D` | LocalStiffnessMatrix, TransformationMatrix, Aᵀ·B·A, EquivalentNodalLoads, A_e |
| 3. Assemblage | K = Σ A_eᵀ·k_e·A_e ; F = charges nodales + Σ A_eᵀ·f_eq,e | `assembleStiffness`, `assembleLoads` | boucle « Pour chaque élément » + variables (graphe) ; GlobalStiffnessAssembly, GlobalLoadVectorAssembly (en un bloc) |
| 4. Conditions aux limites | DDL libres f et imposés c ; d_c ; K_ff·d_f = F_f − K_fc·d_c | `boundaryConditions`, `reduce` | BoundaryConditions, ReducedSystem |
| 5. Résolution | Cholesky (défaut), LU ou gradient conjugué ; jamais d'inverse explicite ; diagnostic | `solveDisplacements` | Résoudre K_ff·d_f = F_f − K_fc·d_c |
| 6. Déplacements | d = [d_f ; d_c] ; d_e = A_e·d, d'_e = T·d_e | `fullDisplacements` | DisplacementRecovery, ElementDisplacement |
| 7. Réactions, équilibre | R = K·d − F ; Σ charges + Σ réactions = 0 (forces, moment / origine) | `reactions`, `checkEquilibrium` | ReactionRecovery, Bilan d'équilibre |
| 8. Efforts internes | f' = k'·T·d_e + f0' ; N, V, M, déformée | `elementResults` | ElementEndForces, InternalForceDiagram |
| 9. Visualisation | portique, déformée amplifiée, réactions, diagrammes, matrices, bilan | — | Visualiser, Courbe d'un diagramme, valeurs produites |

Le **calcul direct** (`solveLinearStatic`, bloc « Calcul direct ») enchaîne les mêmes étapes en un bloc. Le graphe
`Methode_Des_Deplacements_2D` les écrit une par une, avec une différence voulue : il assemble K et F dans une boucle par
les produits matriciels A_eᵀ·k_e·A_e (bloc « Aᵀ·B·A »), alors que le calcul direct assemble par adressage direct. Le
graphe compare son résultat au calcul direct (écart attendu ≤ 1e-9, mesuré ≤ 1e-12).

### Diagnostic d'un système singulier

Si la factorisation rencontre un pivot nul, le bloc de résolution échoue avec :
- le DDL au pivot nul (« N2.ux ») ;
- le mode à énergie nulle (vecteur propre de la plus petite valeur propre de K_ff, jusqu'à 300 équations) et sa nature :
  translation ou rotation d'ensemble (appuis insuffisants, avec le centre de rotation), nœud relié à aucun élément,
  mécanisme interne ;
- ou, si aucun mode n'est nul, un système **quasi singulier** par écart de rigidités (conditionnement donné) ;
- les causes possibles.

Le conditionnement κ(K_ff) = λmax/λmin est calculé jusqu'à 150 équations et signalé au-delà de 1e10 (environ 6
chiffres significatifs fiables).

## 6. Bibliothèque de composants

Palette de l'éditeur : catégories « Structure A » à « Structure E » et « Expérimental ». ✓ = composant validé
(logique testée contre des références), ⚗ = composant expérimental (non validé). Chaque bloc a sa documentation
détaillée et son fichier source (F1). Les blocs purs réutilisent leur résultat tant que leurs entrées ne changent pas.

| Catégorie | Blocs |
| :--- | :--- |
| A — Modèle structurel | Node2D, Material, Matériau du catalogue (S235, S355, C25/30, C30/37, C24), Section2D, Section du catalogue (IPE, HEA, HEB, UPN, RECT, CIRC), SupportCondition, **FrameElement2D**, StructuralModel (vide), Ajouter un nœud / un élément / un appui / un cas / une combinaison, Valider le modèle, Informations du modèle, Portique de démonstration, Modèle d'exemple, Nœud du modèle, Élément du modèle ; charges : nodale, répartie, uniforme, force ponctuelle, moment ponctuel, multi-segments, action thermique, déformation initiale, poids propre, déplacement imposé, Regrouper des charges, LoadCase, LoadCombination, Chargement à calculer |
| B — Élément fini | ElementGeometry, LocalStiffnessMatrix, TransformationMatrix, GlobalElementStiffness, EquivalentNodalLoads, ElementDisplacement, ElementEndForces, InternalForceDiagram, Opérateur d'assemblage A_e |
| C — Assemblage | GlobalDOFNumbering, GlobalStiffnessAssembly, Vecteur des charges nodales, GlobalLoadVectorAssembly, BoundaryConditions, ReducedSystem, Résoudre K_ff·d_f = F_f − K_fc·d_c, DisplacementRecovery, ReactionRecovery, Bilan d'équilibre, Assembler le résultat, Calcul direct, Résultat en un nœud, Résultat d'un élément, Comparer deux résultats |
| D — Mathématiques | Matrice et vecteur (saisie, nuls, identité), A + B, A − B, A·B, s·A, Aᵀ, A·x, Aᵀ·B·A, x + y, x − y, s·x, x·y, ‖x‖, coefficients (lire, modifier), dimensions, Vérifier les dimensions, sous-matrice, sous-vecteur, ligne, colonne, Résoudre K·x = f, Décomposition LU, Inversion contrôlée, Résidu, Conditionnement, Symétrie, exports CSV |
| E — Contrôle et affichage | Pour chaque élément, Pour chaque identifiant, Sélection d'éléments (tous, horizontaux, verticaux, inclinés, ids, longueur), Comparer deux vecteurs, Comparer deux valeurs, Courbe (x, y), Courbe d'un diagramme, Visualiser, Exporter les résultats (CSV) ; et les nœuds standard : Si, Pour, Tant que, Séquence, Comparer, Afficher, Afficher le contenu, variables |
| Expérimental | Itération de Jacobi, Itération de Gauss-Seidel (exercice) |

Types des liaisons : `StructuralModel`, `Node2D`, `Material`, `Section2D`, `SupportCondition`, `FrameElement2D`,
`Load` (groupe de charges), `LoadCase`, `LoadCombination`, `Matrix`, `Vector`, `DOFSet`, `ReducedSystem`,
`SolverResult`, `InternalForceDiagram`, `Curve`, et les types simples (booléen, entier, réel, texte, point, liste).

## 7. Construire un graphe

- **Liaisons typées** : une connexion incompatible est refusée à la souris, avec les deux types (« Matrix → Vector »).
  Un objet du mauvais type arrivé par une broche générique (variable) est refusé à l'exécution.
- **Validation** (avant chaque exécution, onglet « Problèmes ») : bloc inconnu ou d'une version plus récente, lien
  invalide, entrée obligatoire non reliée, cycle de données, cycle d'exécution. **Avertissement** : un bloc lit la
  sortie d'un bloc d'action exécuté après lui.
- **Erreurs** à l'exécution : le bloc fautif est centré et surligné, l'onglet « Problèmes » donne le message (dimensions
  incompatibles, matrice singulière, charge hors de l'élément…).
- **Variables** : « Affecter une variable » / « Lire une variable », pour les accumulations d'une boucle
  (K ← K + A_eᵀ·k_e·A_e). Dans une boucle, calculer les nouvelles valeurs avant de les affecter (voir le gradient
  conjugué du solveur de l'étudiant).
- **Réexécution** : les blocs purs gardent leur résultat d'une exécution à l'autre tant que leurs entrées sont
  identiques (comparaison par contenu) ; le profil sous chaque bloc indique ↺ pour un résultat réutilisé.
- **Édition** : Annuler / Rétablir (graphe), Copier / Coller / Dupliquer (Ctrl+C, Ctrl+V, Ctrl+D dans le graphe),
  commentaires (touche C : cadre titré autour de la sélection, déplacé avec son contenu, double-clic pour le texte),
  points d'arrêt et pas à pas, Enregistrer / Ouvrir (`.tsbp`, version 2 : nœuds et leur version, valeurs saisies,
  liens, commentaires ; les objets calculés ne sont jamais enregistrés).
- **Exécution en tâche de fond** : un graphe qui n'utilise pas le projet (tous les graphes de programmation
  structurelle) s'exécute sans bloquer l'interface ; ■ Arrêter l'interrompt.

## 8. Mode 1 — utilisation directe

Exemple **Classe FrameElement2D** : deux blocs Node2D, un matériau et une section du catalogue → bloc FrameElement2D →
longueur, angle, k', T, k ; une charge verticale (Y global) → charges nodales équivalentes. Un modèle complet se
construit par blocs (StructuralModel vide, Ajouter un élément, un appui, un cas) : le test L20 vérifie que le modèle et
dix types de charges ainsi construits sont identiques au même modèle écrit en C++.

## 9. Mode 2 — apprentissage et développement

Exemple **Solveur de l'étudiant (gradient conjugué)** : le système réduit du portique est résolu par un gradient
conjugué écrit avec des blocs mathématiques et des variables, puis comparé à la résolution de référence (Cholesky,
« Comparer deux vecteurs », tolérance 1e-9). Les composants de base restent ceux du laboratoire validé ; seul
l'algorithme de résolution est remplacé.

Composants expérimentaux (séparés des composants validés : une modification pédagogique ne touche pas les blocs
validés) :
- **Itération de Jacobi** : x_suivant = D⁻¹·(f − (K − D)·x) ; converge si K est à diagonale strictement dominante
  (souvent lentement, voire pas du tout, pour une rigidité de portique).
- **Itération de Gauss-Seidel (exercice)** : à écrire dans `lab/Research/Structural/ControlNodes.cpp` (fonction
  `gaussSeidelSweep`). Tant qu'elle n'est pas écrite, le bloc échoue avec un message explicite. Une fois écrite, le test
  L19 vérifie qu'elle converge vers la solution de référence.

Consulter la logique d'un bloc pré-codé : F1 ▸ onglet « Code source » (fichier présent sur un poste de développement).

## 10. Vérification

Mission (§ 9) et tests :

| Cas | Test | Référence, tolérance |
| :--- | :--- | :--- |
| 1. Barre axiale | S8 | PL/EA, pL²/2EA, P·b/L, EAαΔT, αΔT·L ; 1e-9 |
| 2. Poutre bi-appuyée, charge ponctuelle | S9 | PL³/48EI, PL²/16EI, Pab/L, Pa²b²/3EIL ; 1e-9 |
| 3. Charge répartie | S10 | 5qL⁴/384EI, qL²/8, qL²/12, qL⁴/8EI, triangulaire, trapézoïdale, partielle, multi-segments ; 1e-9 |
| 4. Portique élémentaire | S11 | solution fermée du portique encastré (barres inextensibles) 1e-6 ; MetDeDeplacement 1e-9 |
| 5. Élément incliné | S12 | composantes par transformation, invariance par rotation ; 1e-9 |
| 6. Déplacements imposés | S13, L21 | 12EIΔ/L³, 6EIΔ/L², 4EIθ/L, 6EIΔ/L³ ; refus sur DDL libre ; 1e-9 |
| 7. Système instable | S14, L18 | translation, rotation autour de l'appui, nœud isolé, système quasi singulier |
| 8. Nœuds partagés | S15 | équilibre du nœud commun à trois éléments, MetDeDeplacement, poutre continue 5qL/4 ; 1e-9 |
| 9. Calcul direct ≡ Blueprint | L16 | écart ≤ 1e-12 (cas G, combinaison ELU, tassement) |
| 10. Sauvegarde et réouverture | L17 | graphe relu à l'identique, réexécuté, résultat identique ; réutilisation des résultats |

Autres tests : S7 (classe FrameElement2D : matrices, corps rigides, efforts d'encastrement des tables, validation),
S16 (moment nodal, combinaisons, portique à versants ≡ MetDeDeplacement), L15 (bibliothèque : typage, documentation,
sources, statut), L19 (mode 2), L20 (mode 1, modèle par blocs ≡ C++), L22 (blocs mathématiques, exports, courbes) ;
TSA test 391 (objets typés, réutilisation, variables, cycles, dépendances, fichier v2, copier-coller).
Implémentation indépendante : MetDeDeplacement (moteur de production Custom2D, déjà validé contre OpenSees), par
`toMdd` dans `science/tests/StructuralTests.cpp`.

## 11. Limites et suite

| Sujet | État |
| :--- | :--- |
| Sous-graphes (blocs composés réutilisables) | **non fait** : les étapes sont regroupées par des commentaires (cadres titrés), pas encore encapsulées dans des blocs composés |
| Modèle importé du projet TSA | **non fait** : les modèles viennent des blocs, du portique de démonstration ou des exemples ; l'import d'un projet (plan d'un portique) est à écrire sur l'adaptateur Custom2D |
| Rotules d'extrémité, appuis élastiques, barres articulées | non pris en charge par `FrameElement2D` (présents dans MetDeDeplacement) |
| Grands systèmes | matrices pleines (pédagogie) ; conditionnement et modes jusqu'à 150 et 300 équations ; OpenSees reste le moteur des grands modèles |
| Actions sismiques, dynamiques, trafic | non implémentées (§ 4) |

## 12. Où est le code

| Partie | Fichiers |
| :--- | :--- |
| Science (C++ pur, testée sans interface) | `science/include/tsalab/structural/` (`Frame2D.h`, `FrameElement2D.h`, `DirectStiffness2D.h`), `science/src/structural/`, tests `science/tests/StructuralTests.cpp` |
| Objets typés, blocs | `lab/Research/Structural/` (`StructuralObjects`, `*Nodes.cpp`, `StructuralLibrary`, `StructuralModels`, `ResultBoard`) |
| Graphes d'exemple | `lab/Research/Examples/StructuralGraphs.*` |
| Vues (matrice, diagrammes, portique) | `lab/LabUI/StructuralViewers.*` |
| Tests du laboratoire | `lab/Tests/StructuralLabTests.cpp` (L15 à L22) |
| Moteur et éditeur partagés (TSA) | `TSA/src/Blueprint/`, `TSA/src/UI/Blueprint/` (voir `TSA/docs/BLUEPRINT.md`) |
