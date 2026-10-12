#pragma once

// Références analytiques appliquées au modèle de l'utilisateur (docs/VALIDATION.md § Référence analytique).
//
// Premier problème pris en charge : poutre d'Euler-Bernoulli sur deux appuis simples, force ponctuelle centrale P.
//   R_A = R_B = P/2, δ_max = P·L³/(48·E·I) à mi-portée, M_max = P·L/4, |V| = P/2.
// Les données (L, E, I, P) sont lues dans le modèle réel, jamais saisies à part. Chaque hypothèse de la formule est
// vérifiée sur le modèle et le calcul :
//   - deux appuis, translation dans la direction de la charge bloquée, rotation de flexion libre ;
//   - barres alignées de A à B (poutre droite, horizontale), même section, même matériau, sans relâchement ;
//   - une seule force, verticale, appliquée au nœud de mi-portée (valeur nodale exacte de la flèche) ;
//   - aucune autre charge, poids propre exclu, cas de charge (pas une combinaison) ;
//   - élément d'Euler-Bernoulli (cisaillement transverse négligé : c'est le cas des deux moteurs intégrés).
// Une hypothèse non remplie est nommée et la comparaison n'est pas faite : aucune référence n'est appliquée hors de
// son domaine de validité.

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <vector>

namespace TSA::Model
{
class Model;
}
namespace TSA::Analysis
{
class ResultsModel;
struct AnalysisContext;
}

namespace TSALab::Research
{

struct ReferenceCheck
{
    QString quantity;   ///< « reaction:<nœud>:fz », « node:<nœud>:uz », « member:M:absmax »…
    QString label;      ///< « flèche à mi-portée »
    QString formula;    ///< « P·L³/(48·E·I) »
    QString unit;
    double reference = 0.0, computed = 0.0, absError = 0.0;
    double relError = 0.0;   ///< NaN si |référence| ≈ 0
    double tolRel = 1e-6, tolAbs = 1e-9;
    bool available = false;  ///< grandeur fournie par le calcul
    bool passed = false;
};

struct ReferenceComparison
{
    QString problemId;        ///< « simply-supported-central-load »
    QString title;
    bool applicable = false;  ///< toutes les hypothèses sont remplies
    QStringList hypotheses;   ///< hypothèses vérifiées (texte)
    QStringList violations;   ///< hypothèses non remplies (la comparaison n'est pas faite)
    QJsonObject parameters;   ///< L, E, I, P avec unités
    std::vector<ReferenceCheck> checks;
    QStringList warnings;
    bool compared() const { return applicable && !checks.empty(); }
    bool passed() const;
    QJsonObject toJson() const;
    static ReferenceComparison fromJson(const QJsonObject& json);
};

/// Reconnaît le problème dans le modèle et le contexte de calcul, vérifie ses hypothèses et calcule les valeurs de
/// référence. results : résultats du calcul à comparer (nul : références seules, sans comparaison).
ReferenceComparison simplySupportedCentralLoad(const TSA::Model::Model& model, const TSA::Analysis::AnalysisContext& context,
                                               const TSA::Analysis::ResultsModel* results);

/// Deuxième problème : plaque carrée de Mindlin, appui simple « dur » sur les quatre bords (translations et rotation
/// tangentielle au bord bloquées), pression uniforme q (cas FEA-07 du banc).
///   w_centre = α·q·a⁴/D + β·q·a²/(κ·G·e), D = E·e³/(12(1−ν²)), G = E/(2(1+ν)), κ = 5/6,
///   α = 0,00406235 (Navier), β = 0,0736714 (moment de Marcus) — relation exacte de Wang, Lim, Reddy et Lee (2001).
///   Σ R_z = q·a².
/// Hypothèses vérifiées : une seule dalle (formulation coque, sans ouverture), carrée, horizontale, alignée sur les axes ;
/// aucune barre ni voile ; appuis de bord durs sur les quatre côtés, sans raideur ; une seule pression uniforme
/// (gravité) ; poids propre exclu ; maillage transmis au calcul. La flèche est jugée avec une tolérance de 1 % (erreur
/// de discrétisation, voir l'étude de convergence de FEA-07).
ReferenceComparison squarePlateHardSupport(const TSA::Model::Model& model, const TSA::Analysis::AnalysisContext& context,
                                           const TSA::Analysis::ResultsModel* results);

/// Référence adaptée au modèle : plaque si le modèle contient des dalles, poutre sinon.
ReferenceComparison analyticalReference(const TSA::Model::Model& model, const TSA::Analysis::AnalysisContext& context,
                                        const TSA::Analysis::ResultsModel* results);

} // namespace TSALab::Research
