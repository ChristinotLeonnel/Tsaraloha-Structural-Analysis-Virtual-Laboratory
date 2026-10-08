# Tasks — TSALab

Last Updated: 2026-10-08.

- [x] Copies obsolètes de TSA supprimées (2026-10-08, 217/217 ensuite).
- [ ] Commit de la branche feature/shared-tsa-core (TSALab) et feature/shared-core (TSA) — à fusionner ENSEMBLE.
- [ ] Vérifier en GUI : exemple ouvert depuis le Start Center, calcul F5 en ADVANCED puis SOLVER LAB (3 méthodes,
      conditionnement), rail MODÈLE ↔ SOLVER LAB, thème clair, ouverture d'un .tsa puis Enregistrer → .tsalab.
- [ ] SOLVER LAB : calcul en tâche de fond (QThread) pour les grands systèmes ; export CSV du tableau.
- [ ] Espace VALIDATION : comparer chaque exemple à sa solution analytique (`ExampleInfo::reference`).
- [ ] Espaces ANALYSIS / RESULTS / ELEMENT LAB / EXPERIMENT / VISUAL CODING : à définir avant implémentation.
- [ ] Les SDK OCCT / 3rdparty / thirdparty/OpenSees encore présents (non suivis) dans TSALab ne servent plus :
      supprimables par l'utilisateur pour libérer de la place.
