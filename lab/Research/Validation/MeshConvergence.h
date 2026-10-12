#pragma once

// Étude de convergence en maillage (docs/VALIDATION.md) : à partir de la série que le banc de validation enregistre
// pour un cas déclarant « meshSizes » (TSA::Validation::CheckOutcome::convergence), écart à la référence pour chaque
// maillage, variation d'un maillage au suivant, ordre de convergence observé et extrapolation de Richardson.
//
// L'ordre observé p = ln((f1 − f2)/(f2 − f3)) / ln(r) n'est calculé que sur les trois maillages les plus fins, avec un
// rapport de raffinement r constant et une convergence monotone ; sinon il est « non calculé » et la raison est donnée.
// L'extrapolation sépare l'erreur de discrétisation de l'écart de modélisation (théorie du moteur ≠ théorie de la
// référence) : elle n'est jamais présentée comme une valeur du moteur. Sans interface graphique.

#include "Validation/BenchmarkSuite.h"

#include <QString>
#include <QStringList>

#include <vector>

namespace TSALab::Research
{

struct MeshConvergenceRow
{
    double meshSize = 0.0;
    QString mesh;                 ///< maillage réellement produit (composition)
    bool available = false;
    double value = 0.0;
    double relErrorToReference;   ///< |f − réf| / |réf| ; NaN si indisponible ou réf. ≈ 0
    double relChange;             ///< |f_i − f_(i−1)| / |f_i| ; NaN pour le premier point ou un voisin indisponible
};

struct MeshConvergenceStudy
{
    std::vector<MeshConvergenceRow> rows;   ///< ordre du cas (du plus grossier au plus fin)
    double reference = 0.0;
    bool applicable = false;      ///< au moins deux maillages calculés
    QString reason;               ///< pourquoi l'étude n'est pas applicable
    bool errorDecreasing = false; ///< écart à la référence non croissant d'un maillage au suivant
    double refinementRatio;       ///< h_(i−1)/h_i s'il est constant sur les trois derniers points, sinon NaN
    double observedOrder;         ///< NaN si non calculé (orderNote donne la raison)
    double extrapolated;          ///< Richardson à partir de l'ordre observé ; NaN sinon
    double extrapolatedRelError;  ///< |extrapolé − réf| / |réf| ; NaN sinon
    QString orderNote;
};

MeshConvergenceStudy analyseMeshConvergence(const TSA::Validation::CheckOutcome& check);

/// CSV « mesh_size,mesh,value,rel_error_to_reference,rel_change » précédé d'un en-tête commenté (cas, grandeur,
/// référence, ordre observé, extrapolation) ; vide si la vérification n'a pas de série.
QString meshConvergenceCsv(const TSA::Validation::CheckOutcome& check, const MeshConvergenceStudy& study);

} // namespace TSALab::Research
