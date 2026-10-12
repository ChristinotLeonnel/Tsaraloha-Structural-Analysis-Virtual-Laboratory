#pragma once

// Espace « Validation » de TSALab : banc de validation partagé avec TSA (docs/VALIDATION.md). Cas de référence,
// exécution par la chaîne de calcul réelle (en tâche de fond), écarts à la référence analytique ou externe,
// rapports. La logique est dans Research/Validation/ValidationRunner (sans interface).

#include "Research/Validation/ValidationRunner.h"

#include <QWidget>

#include <atomic>
#include <memory>

class QComboBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QTextBrowser;
class QThread;
class QTreeWidget;

namespace TSALab::UI
{

class ValidationPage : public QWidget
{
    Q_OBJECT

public:
    explicit ValidationPage(QWidget* parent = nullptr);
    ~ValidationPage() override;

    /// Charge les cas d'un dossier (défaut : banc partagé).
    void loadCases(const QString& directory);
    QString casesDirectory() const { return m_directory; }
    int caseCount() const { return static_cast<int>(m_cases.size()); }
    /// Coche tous les cas valides (ou aucun).
    void checkAll(bool on);
    /// Coche les cas dont l'identifiant figure dans la liste (tests, reprise d'une expérience).
    void checkCases(const QStringList& ids);
    /// Filtre de moteur : vide = moteurs déclarés par chaque cas.
    void setEngineFilter(const QString& engineId);
    /// Exécute les cas cochés en tâche de fond ; finished(report) à la fin.
    void runChecked();
    bool isRunning() const { return m_thread != nullptr; }
    const TSA::Validation::BenchmarkReport& lastReport() const { return m_report; }
    int resultRowCount() const;

signals:
    void finished(const TSA::Validation::BenchmarkReport& report);
    void message(const QString& text, const QString& type);

private:
    void buildUi();
    void showCaseDetails();
    void showReport();
    void importReferences();
    void exportReport();
    void setRunning(bool running);

    QString m_directory;
    std::vector<Research::LoadedCase> m_cases;
    std::vector<TSA::Validation::ExternalReference> m_external;
    TSA::Validation::BenchmarkReport m_report;
    std::atomic<bool> m_cancel { false };
    QThread* m_thread = nullptr;

    QTreeWidget* m_tree = nullptr;
    QTextBrowser* m_details = nullptr;
    QTableWidget* m_table = nullptr;
    QComboBox* m_engine = nullptr;
    QLabel* m_summary = nullptr;
    QLabel* m_referencesLabel = nullptr;
    QProgressBar* m_progress = nullptr;
    QPushButton* m_run = nullptr;
    QPushButton* m_stop = nullptr;
    QPushButton* m_export = nullptr;
};

} // namespace TSALab::UI
