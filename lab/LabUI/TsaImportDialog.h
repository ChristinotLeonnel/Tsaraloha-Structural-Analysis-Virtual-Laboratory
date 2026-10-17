#pragma once

// TSALab — « Importer un portique d'un projet TSA » : aperçu avant import (menu Blueprint).
//
// Fichier .tsa choisi, portées planes lues dans le projet (plan du modèle, axes de grille, niveaux, plans de travail),
// import complet en tâche de fond (annulable) par le chemin d'analyse 2D de TSA (Research/Structural/TsaImport.h) :
// rapport importé / converti / ignoré, avertissements, refus motivés, empreinte SHA-256 contrôlée avant et après (le
// fichier n'est jamais écrit). Après un aperçu réussi : choix du cas ou de la combinaison, puis « Créer le graphe
// d'analyse » (StructuralGraphs::buildTsaImport), ouvert par l'appelant dans l'éditeur Blueprint.

#include "Blueprint/BlueprintGraph.h"
#include "Research/Structural/TsaImport.h"

#include <QDialog>

#include <atomic>

class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

namespace TSALab::UI
{

class TsaImportDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TsaImportDialog(QWidget* parent = nullptr);

    /// Fichier présélectionné : portées lues aussitôt.
    void setFile(const QString& path);
    /// Import complet en tâche de fond (Annuler l'interrompt entre deux étapes), rapport affiché ; vrai si réussi.
    bool preview();
    /// Graphe d'analyse du dernier aperçu réussi (fichier, portée, cas ou combinaison choisis).
    bool analysisGraph(TSA::Blueprint::Graph& graph, QString* error = nullptr) const;
    const TSALab::Research::Structural::TsaImportReport& report() const { return m_report; }

    void reject() override;

private:
    void browse();
    void loadScopes();
    void setRunning(bool running);
    void updateButtons();

    QLineEdit* m_path = nullptr;
    QComboBox* m_scope = nullptr;
    QComboBox* m_case = nullptr;
    QPushButton* m_previewButton = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QPushButton* m_createButton = nullptr;
    QLabel* m_status = nullptr;
    QLabel* m_summary = nullptr;
    QPlainTextEdit* m_reportView = nullptr;

    std::atomic<bool> m_cancel { false };
    bool m_running = false;
    bool m_closeRequested = false;
    bool m_previewPending = false;     ///< « Aperçu » demandé pendant la lecture des portées
    bool m_ok = false;                 ///< dernier aperçu réussi (et toujours valable : fichier et portée inchangés)
    QString m_scopesPath;              ///< fichier dont les portées sont listées
    QString m_previewPath, m_previewScope;
    TSALab::Research::Structural::TsaImportReport m_report;
};

} // namespace TSALab::UI
