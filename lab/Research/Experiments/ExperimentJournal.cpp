#include "ExperimentJournal.h"

#include "Analysis/Engine/AnalysisEngineRegistry.h"
#include "Analysis/Engine/AnalysisManager.h"
#include "Analysis/ResultsModel.h"
#include "App/ProductInfo.h"
#include "Grid/GridManager.h"
#include "IO/TSAFile.h"
#include "Model/Model.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>
#include <cmath>

namespace TSALab::Research
{

namespace
{
constexpr const char* kFormat = "tsalab.experiment/1";

QStringList fromArray(const QJsonValue& v)
{
    QStringList out;
    for (const auto& x : v.toArray()) out << x.toString();
    return out;
}

QString sha256(const QString& file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    h.addData(&f);
    return QString::fromLatin1(h.result().toHex());
}

QString nowIso() { return QDateTime::currentDateTime().toString(Qt::ISODate); }
} // namespace

QJsonObject ExperimentRecord::toJson() const
{
    return QJsonObject { { "format", kFormat },
                         { "id", id },
                         { "kind", kind },
                         { "title", title },
                         { "date", date },
                         { "labVersion", labVersion },
                         { "engine", QJsonObject { { "id", engineId }, { "name", engineName }, { "version", engineVersion } } },
                         { "projectFile", projectFile },
                         { "modelSnapshot", modelSnapshot },
                         { "modelHash", modelHash },
                         { "analysisContext", analysisContext },
                         { "units", units },
                         { "status", status },
                         { "warnings", QJsonArray::fromStringList(warnings) },
                         { "errors", QJsonArray::fromStringList(errors) },
                         { "results", results },
                         { "provenance", provenance } };
}

ExperimentRecord ExperimentRecord::fromJson(const QJsonObject& o, QStringList* problems)
{
    ExperimentRecord r;
    if (o.value("format").toString() != QLatin1String(kFormat) && problems)
        *problems << QStringLiteral("format « %1 » inattendu (lu au mieux)").arg(o.value("format").toString());
    r.id = o.value("id").toString();
    r.kind = o.value("kind").toString();
    r.title = o.value("title").toString();
    r.date = o.value("date").toString();
    r.labVersion = o.value("labVersion").toString();
    const QJsonObject e = o.value("engine").toObject();
    r.engineId = e.value("id").toString();
    r.engineName = e.value("name").toString();
    r.engineVersion = e.value("version").toString();
    r.projectFile = o.value("projectFile").toString();
    r.modelSnapshot = o.value("modelSnapshot").toString();
    r.modelHash = o.value("modelHash").toString();
    r.analysisContext = o.value("analysisContext").toObject();
    r.units = o.value("units").toObject();
    r.status = o.value("status").toString();
    r.warnings = fromArray(o.value("warnings"));
    r.errors = fromArray(o.value("errors"));
    r.results = o.value("results").toObject();
    r.provenance = o.value("provenance").toString();
    if (r.id.isEmpty() && problems) *problems << QStringLiteral("identifiant manquant");
    return r;
}

QJsonObject summarizeResults(const TSA::Analysis::ResultsModel& results)
{
    QJsonObject s;
    double maxU = -1.0;
    int maxNode = 0;
    for (const auto& [id, d] : results.allDisplacements())
        if (d.translationMagnitude() > maxU) maxU = d.translationMagnitude(), maxNode = id;
    if (maxU >= 0.0)
    {
        s["displacement.max_translation"] = maxU;
        s["displacement.max_translation_node"] = maxNode;
    }
    if (!results.allReactions().empty())
    {
        double fx = 0, fy = 0, fz = 0;
        for (const auto& [id, r] : results.allReactions()) fx += r.rx, fy += r.ry, fz += r.rz;
        s["reaction.sum_fx"] = fx;
        s["reaction.sum_fy"] = fy;
        s["reaction.sum_fz"] = fz;
    }
    if (!results.allElementResults().empty())
    {
        double n = 0, my = 0, m = 0, v = 0;
        for (const auto& [key, e] : results.allElementResults())
        {
            n = std::max({ n, std::abs(e.maxNormalForce()), std::abs(e.minNormalForce()) });
            my = std::max(my, e.maxAbsMy());
            m = std::max(m, e.maxBendingMoment());
            v = std::max(v, e.maxShearForce());
        }
        s["member.max_abs_N"] = n;
        s["member.max_abs_My"] = my;
        s["member.max_bending"] = m;
        s["member.max_shear"] = v;
    }
    const auto& modes = results.modal().modes;
    for (std::size_t i = 0; i < modes.size() && i < 12; ++i) s[QStringLiteral("modal.f%1").arg(i + 1)] = modes[i].frequency;
    return s;
}

bool ReproductionResult::identical() const
{
    if (!ran || comparisons.empty()) return false;
    return std::all_of(comparisons.begin(), comparisons.end(), [](const QuantityComparison& c) { return c.present && c.equal; });
}

ExperimentJournal::ExperimentJournal(const QString& directory)
    : m_directory(directory)
{
}

QString ExperimentJournal::directoryForProject(const QString& projectFile)
{
    if (!projectFile.isEmpty())
    {
        const QFileInfo fi(projectFile);
        return fi.absoluteDir().filePath(fi.completeBaseName() + QStringLiteral(".experiments"));
    }
    return QDir(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)).filePath(QStringLiteral("experiments/modeles-non-enregistres"));
}

bool ExperimentJournal::write(ExperimentRecord& record, QString* error) const
{
    QDir dir(m_directory);
    const QString sub = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + QStringLiteral("-") + record.id.left(8);
    if (!dir.mkpath(sub))
    {
        if (error) *error = QStringLiteral("dossier d'expérience impossible à créer : %1").arg(QDir::toNativeSeparators(dir.filePath(sub)));
        return false;
    }
    record.folder = dir.filePath(sub);
    QFile f(QDir(record.folder).filePath("record.json"));
    if (!f.open(QIODevice::WriteOnly) || f.write(QJsonDocument(record.toJson()).toJson(QJsonDocument::Indented)) < 0)
    {
        if (error) *error = QStringLiteral("enregistrement de l'expérience impossible : %1").arg(f.errorString());
        return false;
    }
    return true;
}

ExperimentRecord ExperimentJournal::recordAnalysis(const TSA::Model::Model& model, const TSA::Grid::GridManager* grids,
                                                   const TSA::Analysis::AnalysisContext& context, bool success, const QString& message,
                                                   const TSA::Analysis::ResultsModel* results, const TSA::Analysis::AnalysisEngineRegistry& registry,
                                                   const QString& projectFile, const TSA::Analysis::PreparedAnalysis* prepared, QString* error)
{
    ExperimentRecord r;
    r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.kind = QStringLiteral("analysis");
    r.date = nowIso();
    r.labVersion = TSA::Product::name() + QStringLiteral(" ") + TSA::Product::version();
    r.engineId = QString::fromStdString(context.engineId);
    // Identité du moteur lue sur le registre (version lue sur le moteur), jamais les valeurs par défaut des métadonnées
    if (const auto* e = registry.engine(context.engineId))
    {
        const auto info = e->info();
        r.engineName = QString::fromStdString(info.name);
        r.engineVersion = QString::fromStdString(info.version);
    }
    r.projectFile = projectFile;
    r.analysisContext = context.toJson();
    const bool ok = success && results && results->isValid();
    r.status = ok ? QStringLiteral("réussi") : QStringLiteral("échec");
    if (!ok) r.errors << (message.isEmpty() ? QStringLiteral("aucun résultat") : message);
    if (prepared)
    {
        for (const auto& w : prepared->validation.texts(TSA::Analysis::ValidationSeverity::Warning)) r.warnings << QString::fromStdString(w);
        for (const auto& e : prepared->validation.texts(TSA::Analysis::ValidationSeverity::Error)) r.errors << QString::fromStdString(e);
    }
    if (ok)
    {
        const auto& u = results->units();
        r.units = QJsonObject { { "force", QString::fromStdString(u.force) }, { "length", QString::fromStdString(u.length) },
                                { "moment", QString::fromStdString(u.moment) } };
        r.results = summarizeResults(*results);
    }
    const QString engineLabel = (r.engineName.isEmpty() ? r.engineId : r.engineName) +
                                (r.engineVersion.isEmpty() ? QStringLiteral(" (version non lue)") : QStringLiteral(" ") + r.engineVersion);
    r.title = QStringLiteral("Calcul %1 — %2").arg(engineLabel, projectFile.isEmpty() ? QStringLiteral("modèle non enregistré") : QFileInfo(projectFile).fileName());
    r.provenance = ok ? QStringLiteral("calculé par %1 le %2").arg(engineLabel, r.date) : QStringLiteral("calcul non abouti (%1)").arg(engineLabel);
    if (!write(r, error)) return r;
    // Modèle figé : rejouer l'expérience même si le projet est modifié ou supprimé ensuite
    const QString snapshot = QDir(r.folder).filePath(QStringLiteral("model") + TSA::Product::projectExtension());
    std::string err;
    if (TSA::IO::TSAProjectIO::saveToFile(snapshot, model, grids, &err))
    {
        r.modelSnapshot = QFileInfo(snapshot).fileName();
        r.modelHash = sha256(snapshot);
    }
    else
        r.warnings << QStringLiteral("copie figée du modèle impossible : %1 (expérience non rejouable)").arg(QString::fromStdString(err));
    QFile f(QDir(r.folder).filePath("record.json"));
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(r.toJson()).toJson(QJsonDocument::Indented));
    return r;
}

ExperimentRecord ExperimentJournal::recordValidation(const TSA::Validation::BenchmarkReport& report, QString* error)
{
    ExperimentRecord r;
    r.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    r.kind = QStringLiteral("validation");
    r.date = nowIso();
    r.labVersion = QString::fromStdString(report.tsaVersion);
    r.engineName = QString::fromStdString(report.engineVersions);
    r.title = QStringLiteral("Validation — %1 cas").arg(report.cases.size());
    r.status = report.checksFailed() == 0 && report.checksUnavailable() == 0 ? QStringLiteral("réussi") : QStringLiteral("échec");
    QJsonArray cases;
    for (const auto& c : report.cases)
    {
        cases.append(QJsonObject { { "case", QString::fromStdString(c.caseId) }, { "engine", QString::fromStdString(c.engine) },
                                   { "passed", c.passed() }, { "skipped", c.skipped }, { "message", QString::fromStdString(c.message) } });
        if (!c.ran && !c.skipped) r.errors << QStringLiteral("%1 (%2) : %3").arg(QString::fromStdString(c.caseId), QString::fromStdString(c.engine), QString::fromStdString(c.message));
    }
    r.results = QJsonObject { { "checks.passed", report.checksPassed() }, { "checks.failed", report.checksFailed() },
                              { "checks.unavailable", report.checksUnavailable() }, { "cases.skipped", report.casesSkipped() }, { "cases", cases } };
    r.provenance = QStringLiteral("banc de validation exécuté par %1 le %2 (%3)").arg(r.labVersion, r.date, QString::fromStdString(report.machine));
    write(r, error);
    return r;
}

std::vector<ExperimentRecord> ExperimentJournal::records() const
{
    std::vector<ExperimentRecord> out;
    QDir dir(m_directory);
    for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name))
    {
        QFile f(dir.filePath(sub + "/record.json"));
        if (!f.open(QIODevice::ReadOnly)) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
        if (!doc.isObject()) continue;   // illisible : ignoré
        ExperimentRecord r = ExperimentRecord::fromJson(doc.object());
        if (r.id.isEmpty()) continue;
        r.folder = dir.filePath(sub);
        out.push_back(std::move(r));
    }
    return out;
}

ReproductionResult ExperimentJournal::reproduce(const ExperimentRecord& record)
{
    ReproductionResult out;
    if (record.kind != QLatin1String("analysis"))
    {
        out.message = QStringLiteral("seuls les calculs se rejouent (exécution du banc : relancer les cas depuis l'espace Validation)");
        return out;
    }
    if (record.modelSnapshot.isEmpty())
    {
        out.message = QStringLiteral("aucune copie figée du modèle : expérience non rejouable");
        return out;
    }
    const QString snapshot = QDir(record.folder).filePath(record.modelSnapshot);
    if (!record.modelHash.isEmpty() && sha256(snapshot) != record.modelHash)
    {
        out.message = QStringLiteral("la copie figée du modèle a été modifiée (empreinte SHA-256 différente) : rejeu refusé");
        return out;
    }
    TSA::Model::Model model;
    TSA::Grid::GridManager grids;
    std::string err;
    if (!TSA::IO::TSAProjectIO::loadFromFile(snapshot, model, &grids, &err))
    {
        out.message = QStringLiteral("copie figée illisible : %1").arg(QString::fromStdString(err));
        return out;
    }
    bool ok = false;
    const auto context = TSA::Analysis::AnalysisContext::fromJson(record.analysisContext, &ok);
    if (!ok)
    {
        out.message = QStringLiteral("réglages de calcul illisibles");
        return out;
    }
    TSA::Analysis::AnalysisEngineRegistry registry;
    TSA::Analysis::registerBuiltInEngines(registry);
    QStringList notes;
    if (const auto* e = registry.engine(context.engineId))
    {
        const QString v = QString::fromStdString(e->info().version);
        if (v != record.engineVersion) notes << QStringLiteral("version du moteur différente : %1 enregistrée, %2 sur ce poste").arg(record.engineVersion.isEmpty() ? QStringLiteral("non lue") : record.engineVersion, v.isEmpty() ? QStringLiteral("non lue") : v);
    }
    TSA::Analysis::AnalysisManager manager(registry);
    const auto prepared = manager.prepare(model, &grids, context);
    if (!prepared.canRun())
    {
        QStringList errs;
        for (const auto& e : prepared.validation.texts(TSA::Analysis::ValidationSeverity::Error)) errs << QString::fromStdString(e);
        out.message = QStringLiteral("calcul impossible : %1").arg(errs.isEmpty() ? QStringLiteral("contexte invalide") : errs.join(" ; "));
        return out;
    }
    const auto run = manager.run(context, prepared);
    if (!run.success)
    {
        out.message = QStringLiteral("calcul non abouti : %1").arg(QString::fromStdString(run.message));
        return out;
    }
    out.ran = true;
    const QJsonObject now = summarizeResults(run.results);
    QStringList keys = record.results.keys();
    for (const QString& k : now.keys())
        if (!keys.contains(k)) keys << k;
    for (const QString& k : keys)
    {
        QuantityComparison c;
        c.quantity = k;
        c.present = record.results.contains(k) && now.contains(k);
        c.original = record.results.value(k).toDouble();
        c.reproduced = now.value(k).toDouble();
        c.absError = std::abs(c.reproduced - c.original);
        c.relError = std::abs(c.original) > 1e-12 ? c.absError / std::abs(c.original) : std::numeric_limits<double>::quiet_NaN();
        c.equal = c.present && c.absError <= 1e-9 * std::abs(c.original) + 1e-12;
        out.comparisons.push_back(c);
    }
    out.message = notes.join(" ; ");
    return out;
}

} // namespace TSALab::Research
