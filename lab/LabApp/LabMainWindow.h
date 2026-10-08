#pragma once

// Fenêtre principale de TSALab : environnement scientifique (IDE) à panneaux ancrables.
//
// Elle ne contient AUCUN moteur graphique ni modèle propre : elle assemble les composants partagés
// avec TSA (ADR-024, TSA/docs/TSARALOHA_ARCHITECTURE.md) :
//   ProjectSession (modèle, commandes, grilles, fichier) · OccView + ViewportContainer (viewport,
//   caméra, grilles, accrochage, sélection, outils de dessin) · ModelTreeWidget · PropertyPanel ·
//   LogConsoleDock · SelectionSynchronizer.
// Espaces de travail (onglets centraux) : Accueil, Modèle, Blueprint (programmation visuelle partagée),
// Analyse (gestionnaire d'analyse partagé : moteurs, réglages, calcul en tâche de fond), Résultats
// (diagrammes 2D), Recherche (SOLVER LAB). Docks : explorateur, propriétés, console, résultats (affichage
// dans le viewport), données d'analyse (K globale, K·U = F, DDL). Le calcul est piloté par le contrôleur
// d'analyse de la session (TSA::Analysis::AnalysisController), le même que dans TSA.

#include <QMainWindow>

#include <memory>

class OccView;
class QAction;
class QLabel;
class QTabWidget;

namespace TSA::Project
{
class ProjectSession;
}
namespace TSA::Viewer
{
class SelectionManager;
}
namespace TSA::UI
{
class AnalysisDataDock;
class AnalysisEngineOptionsRegistry;
class AnalysisManagerPanel;
class Diagram2DWidget;
class ResultsDockWidget;
class BlueprintEditor;
class LogConsoleDock;
class ModelTreeWidget;
class PropertyPanel;
class ViewportContainer;
}

namespace TSALab::UI
{

class LabStartPanel;
class SolverLabPage;

class LabMainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit LabMainWindow(QWidget* parent = nullptr);
    ~LabMainWindow() override;

    bool openProjectFile(const QString& path);
    bool openExample(const QString& exampleId);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void createWorkspaces();
    void createDocks();
    void createActions();
    void createMenus();
    void createStatusBar();

    void newProject();
    void openProject();
    bool save();
    bool saveAs();
    bool saveTo(const QString& path);
    bool maybeSave();
    void afterProjectLoaded();
    void updateTitle();
    void updateHistoryActions();
    /// Console : commandes du registre central partagé (TSA::Automation::CommandRegistry), « help » pour la liste.
    void runConsoleCommand(const QString& line);
    /// Résultats du contrôleur d'analyse → viewport, docks, diagrammes, SOLVER LAB.
    void onResultsChanged();
    void onResultsBecameStale();
    /// Réglages d'analyse du laboratoire pour un projet qui n'en a pas (Custom2D, plan du modèle, K·U = F).
    void applyLabAnalysisDefaults();
    void log(const QString& text, const QString& type = QStringLiteral("SYS"));

private:
    std::unique_ptr<TSA::Project::ProjectSession> m_session;     ///< déclarée en premier : détruite en dernier
    std::unique_ptr<TSA::Viewer::SelectionManager> m_selection;
    std::unique_ptr<TSA::UI::AnalysisEngineOptionsRegistry> m_engineOptions;

    QTabWidget* m_workspaces = nullptr;
    LabStartPanel* m_home = nullptr;
    OccView* m_view = nullptr;
    TSA::UI::ViewportContainer* m_viewport = nullptr;
    TSA::UI::BlueprintEditor* m_blueprint = nullptr;
    TSA::UI::AnalysisManagerPanel* m_analysisPanel = nullptr;
    TSA::UI::Diagram2DWidget* m_diagram = nullptr;
    SolverLabPage* m_solverLab = nullptr;
    TSA::UI::ResultsDockWidget* m_resultsDock = nullptr;
    TSA::UI::AnalysisDataDock* m_dataDock = nullptr;
    TSA::UI::ModelTreeWidget* m_tree = nullptr;
    TSA::UI::PropertyPanel* m_properties = nullptr;
    TSA::UI::LogConsoleDock* m_console = nullptr;
    QDockWidget* m_treeDock = nullptr;
    QDockWidget* m_propertiesDock = nullptr;

    QAction* m_actNew = nullptr;
    QAction* m_actOpen = nullptr;
    QAction* m_actSave = nullptr;
    QAction* m_actSaveAs = nullptr;
    QAction* m_actUndo = nullptr;
    QAction* m_actRedo = nullptr;
    QLabel* m_statusPrompt = nullptr;
    QLabel* m_statusCoordinates = nullptr;
    QByteArray m_defaultLayout;
};

} // namespace TSALab::UI
