#pragma once

// Laboratoire de validation de TSALab (docs/VALIDATION.md) : exécute le banc de validation partagé avec TSA
// (TSA/src/Validation/BenchmarkSuite, cas JSON « tsa.benchmark/1 ») depuis le laboratoire. Les cas sont calculés par
// la chaîne réelle (modèle → AnalysisManager → moteur) et comparés à leurs références analytiques ou externes :
// écarts absolu et relatif, tolérance mixte, référence ≈ 0 signalée (écart relatif non défini), valeur exacte ou
// interpolée. Aucune valeur n'est inventée : une grandeur que le moteur ne fournit pas est « non disponible ».
// Sans interface graphique (testé par la suite lab).

#include "Validation/BenchmarkSuite.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <string>
#include <vector>

namespace TSALab::Research
{

struct LoadedCase
{
    TSA::Validation::BenchmarkCase benchmark;
    QString file;
    QStringList errors;   ///< cas invalide (champ manquant, formule ou quantité inconnue) : non exécutable
    bool valid() const { return errors.isEmpty(); }
};

/// Dossier des cas : <exe>/validation/cases (paquet), sinon les cas du dépôt TSA (poste de développement).
QString defaultBenchmarkDirectory();

/// Cas d'un dossier (récursif, ordre alphabétique) ou d'un fichier ; les cas invalides sont gardés avec leurs erreurs.
std::vector<LoadedCase> loadCases(const QString& directoryOrFile);

/// Versions RÉELLEMENT constatées des moteurs (« OpenSees 3.8.0 ; Custom2D … (version inconnue) ») ; chaque entrée est
/// un identifiant ou un nom de moteur. Une version qui n'est pas lue sur le moteur n'est jamais inventée.
std::string engineVersions(const std::vector<std::string>& engines);

struct ValidationProgress
{
    int done = 0, total = 0;
    QString caseId;
};

/// Exécute les cas valides. progress est appelé avant chaque cas ; cancel est consulté entre deux cas (le cas en
/// cours se termine). L'en-tête du rapport porte la version de TSALab, les versions des moteurs, la date et le poste.
TSA::Validation::BenchmarkReport runValidation(const std::vector<TSA::Validation::BenchmarkCase>& cases,
                                               const TSA::Validation::RunOptions& options,
                                               const std::function<void(const ValidationProgress&)>& progress = {},
                                               const std::function<bool()>& cancel = {});

/// Écart relatif lisible : « 0,12 % », ou « réf. ≈ 0 » quand il n'est pas défini.
QString relativeErrorText(const TSA::Validation::CheckOutcome& c);
/// État d'une vérification : « RÉUSSI », « ÉCHEC », « NON DISPONIBLE ».
QString checkStatusText(const TSA::Validation::CheckOutcome& c);

} // namespace TSALab::Research
