#include "ValidationRunner.h"

#include "Analysis/Engine/AnalysisEngineRegistry.h"
#include "App/ProductInfo.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSysInfo>

#include <algorithm>
#include <cmath>
#include <set>

namespace TSALab::Research
{

QString defaultBenchmarkDirectory()
{
    const QString shipped = QDir(QCoreApplication::applicationDirPath()).filePath("validation/cases");
    if (QFileInfo(shipped).isDir()) return shipped;
#ifdef TSALAB_TSA_BENCHMARKS
    return QStringLiteral(TSALAB_TSA_BENCHMARKS);
#else
    return shipped;
#endif
}

std::vector<LoadedCase> loadCases(const QString& directoryOrFile)
{
    std::vector<LoadedCase> out;
    const std::vector<QString> files = QFileInfo(directoryOrFile).isDir() ? TSA::Validation::benchmarkFiles(directoryOrFile)
                                                                          : std::vector<QString> { directoryOrFile };
    for (const QString& f : files)
    {
        LoadedCase c;
        c.file = f;
        std::vector<std::string> errors;
        if (!TSA::Validation::loadBenchmarkFile(f, &c.benchmark, &errors))
        {
            for (const auto& e : errors) c.errors << QString::fromStdString(e);
            if (c.errors.isEmpty()) c.errors << QStringLiteral("cas illisible");
            if (c.benchmark.id.empty()) c.benchmark.id = QFileInfo(f).completeBaseName().toStdString();
        }
        out.push_back(std::move(c));
    }
    return out;
}

std::string engineVersions(const std::vector<std::string>& engineIds)
{
    TSA::Analysis::AnalysisEngineRegistry registry;
    TSA::Analysis::registerBuiltInEngines(registry);
    std::string out;
    for (const auto& key : engineIds)   // identifiant (« opensees ») ou nom affiché par le banc (« OpenSees 3.8.0 »)
    {
        std::string label = key, version;
        for (const auto& info : registry.engines())
            if (info.id == key || info.name == key)
            {
                label = info.name;
                version = info.version;
            }
        if (version.empty()) label += " (version inconnue)";
        else if (label.find(version) == std::string::npos) label += " " + version;
        out += (out.empty() ? "" : " ; ") + label;
    }
    return out;
}

TSA::Validation::BenchmarkReport runValidation(const std::vector<TSA::Validation::BenchmarkCase>& cases,
                                               const TSA::Validation::RunOptions& options,
                                               const std::function<void(const ValidationProgress&)>& progress,
                                               const std::function<bool()>& cancel)
{
    TSA::Validation::BenchmarkReport report;
    report.tsaVersion = (TSA::Product::name() + QStringLiteral(" ") + TSA::Product::version()).toStdString();
    report.timestamp = QDateTime::currentDateTime().toString(Qt::ISODate).toStdString();
    report.machine = (QSysInfo::prettyProductName() + " / " + QSysInfo::currentCpuArchitecture()).toStdString();
    std::set<std::string> engines;   // identifiants des moteurs effectivement demandés
    int done = 0;
    for (const auto& c : cases)
    {
        if (cancel && cancel()) break;
        if (progress) progress({ done, static_cast<int>(cases.size()), QString::fromStdString(c.id) });
        for (const auto& e : c.engines)
            if (options.engines.empty() || std::find(options.engines.begin(), options.engines.end(), e) != options.engines.end()) engines.insert(e);
        for (auto& o : TSA::Validation::runBenchmark(c, options)) report.cases.push_back(std::move(o));
        ++done;
    }
    if (progress) progress({ done, static_cast<int>(cases.size()), QString() });
    report.engineVersions = engineVersions(std::vector<std::string>(engines.begin(), engines.end()));
    return report;
}

QString relativeErrorText(const TSA::Validation::CheckOutcome& c)
{
    if (!c.available) return QStringLiteral("—");
    if (std::isnan(c.relError)) return QStringLiteral("réf. ≈ 0");
    return QLocale(QLocale::French).toString(c.relError * 100.0, 'g', 3) + QStringLiteral(" %");
}

QString checkStatusText(const TSA::Validation::CheckOutcome& c)
{
    if (!c.available) return QStringLiteral("NON DISPONIBLE");
    return c.passed ? QStringLiteral("RÉUSSI") : QStringLiteral("ÉCHEC");
}

} // namespace TSALab::Research
