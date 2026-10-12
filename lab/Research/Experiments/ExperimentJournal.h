#pragma once

// Journal d'expériences de TSALab (docs/VALIDATION.md § Traçabilité) : chaque calcul et chaque exécution du banc de
// validation laisse un enregistrement qui permet de le comprendre et de le rejouer. Il contient :
//   - la version de TSALab, l'identifiant, le nom et la version du moteur (lus sur le moteur, jamais inventés) ;
//   - le modèle figé (copie .tsalab) et son empreinte SHA-256 ;
//   - les hypothèses et paramètres du calcul (AnalysisContext JSON), les unités ;
//   - la date, le statut, les avertissements et erreurs ;
//   - un résumé chiffré des résultats, avec leur provenance (calculé par tel moteur, telle date).
// Stockage : <projet>.experiments/<horodatage>-<id>/record.json (+ model.tsalab), à côté du projet ; projet non
// enregistré : dossier de données de l'application. Format « tsalab.experiment/1 » (lecture tolérante).
// Rejouer : modèle figé rechargé, même contexte, même moteur ; résultats comparés grandeur par grandeur.
// Sans interface graphique (testé par la suite lab).

#include "Analysis/Engine/AnalysisEngine.h"
#include "Validation/BenchmarkSuite.h"

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <vector>

namespace TSA::Model
{
class Model;
}
namespace TSA::Grid
{
class GridManager;
}
namespace TSA::Analysis
{
class AnalysisEngineRegistry;
struct PreparedAnalysis;
}

namespace TSALab::Research
{

struct ExperimentRecord
{
    QString id;                 ///< identifiant unique
    QString kind;               ///< « analysis » ou « validation »
    QString title;
    QString date;               ///< ISO 8601, heure locale
    QString labVersion;         ///< « TSALab 0.1.0 »
    QString engineId, engineName, engineVersion;   ///< version vide : non lue (affichée « version non lue »)
    QString projectFile;        ///< projet d'origine (peut être vide : modèle non enregistré)
    QString modelSnapshot;      ///< copie figée du modèle, relative au dossier de l'expérience (vide si aucune)
    QString modelHash;          ///< SHA-256 de la copie figée
    QJsonObject analysisContext;   ///< hypothèses et paramètres du calcul (AnalysisContext::toJson)
    QJsonObject units;          ///< unités des résultats (force, longueur, moment…)
    QString status;             ///< « réussi », « échec », « annulé »
    QStringList warnings, errors;
    QJsonObject results;        ///< résumé chiffré (grandeur → valeur)
    QString provenance;         ///< « calculé par <moteur> <version> le <date> »
    QString folder;             ///< dossier de l'expérience (rempli à la lecture)

    QJsonObject toJson() const;
    static ExperimentRecord fromJson(const QJsonObject& json, QStringList* problems = nullptr);
};

/// Résumé chiffré d'un jeu de résultats : déplacement maximal, sommes des réactions, efforts extrêmes des barres,
/// fréquences propres. Clés stables (comparaison entre deux calculs).
QJsonObject summarizeResults(const TSA::Analysis::ResultsModel& results);

struct QuantityComparison
{
    QString quantity;
    double original = 0.0, reproduced = 0.0, absError = 0.0;
    double relError = 0.0;      ///< NaN si |original| ≈ 0 (écart relatif non défini)
    bool present = true;        ///< grandeur présente dans les deux résultats
    bool equal = false;         ///< |Δ| ≤ 1e-9·|original| + 1e-12
};

struct ReproductionResult
{
    bool ran = false;
    QString message;            ///< refus (moteur indisponible, modèle illisible…) ou remarques (version différente)
    std::vector<QuantityComparison> comparisons;
    bool identical() const;
};

class ExperimentJournal
{
public:
    explicit ExperimentJournal(const QString& directory);
    /// Dossier du journal d'un projet (« <dossier>/<nom>.experiments ») ; projet non enregistré : données de l'application.
    static QString directoryForProject(const QString& projectFile);
    QString directory() const { return m_directory; }

    /// Enregistre un calcul terminé (réussi ou non) : modèle figé, contexte, moteur, avertissements, résultats.
    /// results : résultats publiés du calcul (nul en cas d'échec) ; message : message du moteur.
    ExperimentRecord recordAnalysis(const TSA::Model::Model& model, const TSA::Grid::GridManager* grids,
                                    const TSA::Analysis::AnalysisContext& context, bool success, const QString& message,
                                    const TSA::Analysis::ResultsModel* results, const TSA::Analysis::AnalysisEngineRegistry& registry,
                                    const QString& projectFile, const TSA::Analysis::PreparedAnalysis* prepared = nullptr,
                                    QString* error = nullptr);
    /// Enregistre une exécution du banc de validation (bilan, moteurs, cas).
    ExperimentRecord recordValidation(const TSA::Validation::BenchmarkReport& report, QString* error = nullptr);

    /// Expériences du journal, de la plus ancienne à la plus récente ; enregistrements illisibles ignorés.
    std::vector<ExperimentRecord> records() const;

    /// Rejoue un calcul enregistré (modèle figé, même contexte, moteurs du poste) et compare les résultats.
    static ReproductionResult reproduce(const ExperimentRecord& record);

private:
    bool write(ExperimentRecord& record, QString* error) const;
    QString m_directory;
};

} // namespace TSALab::Research
