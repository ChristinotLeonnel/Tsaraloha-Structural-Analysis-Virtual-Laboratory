#pragma once

// Espace « Expériences » de TSALab : journal des calculs et des exécutions du banc de validation du projet ouvert
// (Research/Experiments/ExperimentJournal) — versions, moteur, modèle figé, hypothèses, unités, statut,
// avertissements, résultats et provenance ; rejeu d'un calcul et comparaison grandeur par grandeur.

#include "Research/Experiments/ExperimentJournal.h"

#include <QWidget>

#include <vector>

class QLabel;
class QPushButton;
class QTableWidget;
class QTextBrowser;
class QThread;

namespace TSALab::UI
{

class ExperimentsPage : public QWidget
{
    Q_OBJECT

public:
    explicit ExperimentsPage(QWidget* parent = nullptr);
    ~ExperimentsPage() override;

    /// Journal affiché (dossier des expériences du projet ouvert).
    void setDirectory(const QString& directory);
    QString directory() const { return m_directory; }
    void refresh();
    int recordCount() const { return static_cast<int>(m_records.size()); }

signals:
    void message(const QString& text, const QString& type);
    /// Copie du modèle d'une expérience enregistrée par l'utilisateur, à ouvrir comme projet.
    void openModelRequested(const QString& path);

private:
    void showDetails();
    void reproduceSelected();
    const Research::ExperimentRecord* selected() const;

    QString m_directory;
    std::vector<Research::ExperimentRecord> m_records;
    QThread* m_thread = nullptr;
    QLabel* m_header = nullptr;
    QTableWidget* m_list = nullptr;
    QTextBrowser* m_details = nullptr;
    QTableWidget* m_comparison = nullptr;
    QLabel* m_verdict = nullptr;
    QPushButton* m_reproduce = nullptr;
    QPushButton* m_openFolder = nullptr;
    QPushButton* m_resume = nullptr;
};

} // namespace TSALab::UI
