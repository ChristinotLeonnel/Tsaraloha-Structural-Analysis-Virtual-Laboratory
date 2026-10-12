#include "LabMainWindow.h"

#include "LabUI/LabStartPanel.h"
#include "LabUI/SolverLabPage.h"
#include "LabUI/ValidationPage.h"
#include "LabUI/ExperimentsPage.h"
#include "Research/Experiments/ExperimentJournal.h"
#include "Research/Reference/AnalyticalReference.h"
#include "Research/Examples/BlueprintExamples.h"
#include "Research/Examples/ExampleModels.h"

#include "AI/Core/AIOrchestrator.h"
#include "Analysis/AnalysisController.h"
#include "Automation/AutomationServer.h"
#include "Analysis/Engine/AnalysisModel.h"
#include "Analysis/ResultsModel.h"
#include "App/ProductInfo.h"
#include "Automation/CommandRegistry.h"
#include "IO/TSAFile.h"
#include "Model/Model.h"
#include "Plugins/PluginManager.h"
#include "Project/ProjectManager.h"
#include "Project/ProjectSession.h"
#include "Project/RecentProjects.h"
#include "UI/AI/AICoEngineeringDock.h"
#include "UI/AI/AIRuntimeDialog.h"
#include "UI/Analysis/AnalysisEngineOptions.h"
#include "UI/Analysis/AnalysisManagerPanel.h"
#include "UI/Blueprint/BlueprintEditor.h"
#include "UI/Common/SelectionSynchronizer.h"
#include "UI/Diagrams/Diagram2DWidget.h"
#include "UI/Display/DisplayMenus.h"
#include "Viewer/DisplayProfile.h"
#include "UI/Dock/AnalysisDataDock.h"
#include "UI/Dock/LogConsoleDock.h"
#include "UI/Dock/ResultsDockWidget.h"
#include "UI/Mesh/MeshAnalysisBridge.h"
#include "UI/Mesh/MeshController.h"
#include "UI/Mesh/MeshDialogs.h"
#include "UI/Mesh/MeshSettingsPanel.h"
#include "Mesh/MeshGenerator.h"
#include "Mesh/MeshResultField.h"
#include "Viewer/MeshVisual.h"
#include "UI/ModelTree/ModelTreeWidget.h"
#include "UI/Properties/PropertyPanel.h"
#include "UI/Ruler/ViewportContainer.h"
#include "UI/Theme/ThemeManager.h"
#include "Viewer/OccView.h"
#include "Viewer/ResultsVisualManager.h"
#include "Viewer/SelectionManager.h"
#include "Viewer/ViewManager.h"

#include <QAction>
#include <QPointer>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>

#include <cmath>
#include <optional>
#include <QToolBar>

#include <algorithm>

namespace TSALab::UI
{

namespace
{
const char* kStateKey = "LabMainWindow/state";
const char* kGeometryKey = "LabMainWindow/geometry";
/// Version de la disposition des panneaux : à incrémenter quand les docks / barres changent. Une disposition
/// mémorisée d'une autre version est ignorée (elle pourrait masquer des panneaux ou faire flotter une barre).
constexpr int kLayoutVersion = 5;

QSettings labSettings()
{
    return QSettings(QString::fromLatin1(TSA::Product::kLayoutSettingsOrganization),
                     QString::fromLatin1(TSA::Product::kLayoutSettingsApplication));
}
} // namespace

LabMainWindow::LabMainWindow(QWidget* parent)
    : QMainWindow(parent)
    , m_session(std::make_unique<TSA::Project::ProjectSession>(this))
    , m_selection(std::make_unique<TSA::Viewer::SelectionManager>(this))
    , m_engineOptions(std::make_unique<TSA::UI::AnalysisEngineOptionsRegistry>())
{
    TSA::UI::registerBuiltInEngineOptions(*m_engineOptions);
    setObjectName("TSALabMainWindow");
    setDockNestingEnabled(true);
    resize(1500, 900);

    createWorkspaces();
    createDocks();
    createAssistant();
    createMesh();
    createActions();
    createMenus();
    createStatusBar();

    // Sélection ↔ viewport ↔ arbre ↔ propriétés : même composant que TSA.
    auto* sync = new TSA::UI::SelectionSynchronizer(&m_session->model(), m_selection.get(), m_view, m_tree,
                                                    m_properties, m_viewport, this);
    connect(sync, &TSA::UI::SelectionSynchronizer::statusMessage, this,
            [this](const QString& text) { statusBar()->showMessage(text, 4000); });
    connect(sync, &TSA::UI::SelectionSynchronizer::modelEdited, this, &LabMainWindow::updateHistoryActions);

    connect(&m_session->project(), &TSA::Project::ProjectManager::modifiedChanged, this, &LabMainWindow::updateTitle);
    connect(m_view, &OccView::elementCreated, this, &LabMainWindow::updateHistoryActions);
    connect(m_console, &TSA::UI::LogConsoleDock::commandEntered, this, &LabMainWindow::runConsoleCommand);
    connect(m_blueprint, &TSA::UI::BlueprintEditor::logMessage, this, [this](const QString& text, const QString& type) { log(text, type); });
    connect(m_blueprint, &TSA::UI::BlueprintEditor::projectModified, this, [this] {
        updateHistoryActions();
        m_view->update();
    });

    // Analyse : contrôleur partagé de la session (même orchestration que TSA).
    auto& analysis = m_session->analysis();
    connect(&analysis, &TSA::Analysis::AnalysisController::resultsChanged, this, &LabMainWindow::onResultsChanged);
    connect(&analysis, &TSA::Analysis::AnalysisController::resultsBecameStale, this, &LabMainWindow::onResultsBecameStale);
    // Journal d'expériences : chaque calcul terminé (interface, console, Blueprint, MCP) est enregistré avec le contexte
    // réellement calculé, le modèle figé, le moteur et sa version, les avertissements et un résumé des résultats.
    connect(&analysis, &TSA::Analysis::AnalysisController::runCompleted, this,
            [this](const TSA::Analysis::AnalysisContext& context, bool success, const QString& message) {
                auto& a = m_session->analysis();
                const auto prepared = a.prepare(context);   // avertissements de validation du calcul
                const auto results = a.results();
                const QString project = m_session->project().hasFilePath() ? m_session->project().currentFilePath() : QString();
                QString error;
                // Référence analytique : reconnue dans le modèle réel, hypothèses vérifiées, comparée aux résultats
                const auto reference = TSALab::Research::analyticalReference(m_session->model(), context, success ? results.get() : nullptr);
                TSALab::Research::ExperimentJournal journal(TSALab::Research::ExperimentJournal::directoryForProject(project));
                const auto rec = journal.recordAnalysis(m_session->model(), &m_session->grids(), context, success, message,
                                                        success ? results.get() : nullptr, a.registry(), project, &prepared, &error, reference.toJson());
                if (!error.isEmpty()) log(tr("Journal d'expériences : %1").arg(error), QStringLiteral("WARN"));
                else log(tr("Expérience enregistrée : %1 (%2)").arg(rec.title, rec.status));
                if (reference.compared())
                {
                    int ok = 0;
                    for (const auto& c : reference.checks) ok += c.available && c.passed;
                    log(tr("Référence analytique (%1) : %2 / %3 vérification(s) dans la tolérance").arg(reference.title).arg(ok).arg(reference.checks.size()),
                        reference.passed() ? QStringLiteral("SYS") : QStringLiteral("WARN"));
                }
                m_experiments->setDirectory(journal.directory());
            });
    connect(m_validation, &ValidationPage::finished, this, [this](const TSA::Validation::BenchmarkReport& report) {
        const QString project = m_session->project().hasFilePath() ? m_session->project().currentFilePath() : QString();
        TSALab::Research::ExperimentJournal journal(TSALab::Research::ExperimentJournal::directoryForProject(project));
        QString error;
        const auto rec = journal.recordValidation(report, &error);
        log(error.isEmpty() ? tr("Validation enregistrée dans le journal : %1 (%2)").arg(rec.title, rec.status) : tr("Journal d'expériences : %1").arg(error),
            error.isEmpty() ? QStringLiteral("SYS") : QStringLiteral("WARN"));
        m_experiments->setDirectory(journal.directory());
    });
    connect(m_validation, &ValidationPage::message, this, [this](const QString& text, const QString& type) { log(text, type); });
    connect(m_experiments, &ExperimentsPage::message, this, [this](const QString& text, const QString& type) { log(text, type); });
    // Reprise du modèle d'une expérience : copie enregistrée par l'utilisateur, ouverte comme projet (la copie figée
    // du journal n'est jamais modifiée : l'expérience reste rejouable)
    connect(m_experiments, &ExperimentsPage::openModelRequested, this, [this](const QString& path) {
        if (!maybeSave()) return;
        openProjectFile(path);
    });
    connect(m_analysisPanel, &TSA::UI::AnalysisManagerPanel::logMessage, this,
            [this](const QString& text, const QString& type) { log(text, type); });
    connect(m_analysisPanel, &TSA::UI::AnalysisManagerPanel::settingsChanged, this, &LabMainWindow::updateTitle);
    m_analysisPanel->setSelectionProvider([this] { return m_selection->selectedElements(); });
    // Maillage du projet transmis au calcul (coques, barres subdivisées) : même logique que TSA, choix explicite si obsolète
    m_analysisPanel->setContextPreparer([this](TSA::Analysis::AnalysisContext& context, const TSA::Analysis::AnalysisEngine& engine) {
        return TSA::UI::applyMeshToAnalysis(m_mesh, context, engine, this, [this](const QString& text, const QString& type) { log(text, type); });
    });

    m_defaultLayout = saveState();
    QSettings s = labSettings();
    restoreGeometry(s.value(kGeometryKey).toByteArray());
    if (!restoreState(s.value(kStateKey).toByteArray(), kLayoutVersion)) restoreState(m_defaultLayout);

    m_session->project().newProject(m_session->model(), &m_session->grids());
    m_tree->setGridManager(&m_session->grids());
    applyLabAnalysisDefaults();
    m_analysisPanel->refresh();
    updateTitle();
    updateHistoryActions();
    log(tr("%1 initialisé — base commune TSA (modèle, viewport, commandes, panneaux).").arg(TSA::Product::name()));
}

LabMainWindow::~LabMainWindow()
{
    // Les enfants QObject (assistant IA, éditeur Blueprint, docks, viewport…) sont détruits APRÈS ce
    // destructeur, par ~QObject : un signal émis pendant leur destruction vers une lambda de cette fenêtre
    // s'exécuterait sur un objet déjà détruit (assertion Qt, BUG-039). Même règle que MainWindow (TSA).
    for (QObject* child : findChildren<QObject*>())
        QObject::disconnect(child, nullptr, this, nullptr);
}

// -----------------------------------------------------------------------------
// Construction de l'interface
// -----------------------------------------------------------------------------

void LabMainWindow::createWorkspaces()
{
    m_workspaces = new QTabWidget(this);
    m_workspaces->setDocumentMode(true);
    m_workspaces->setObjectName("LabWorkspaces");

    // Accueil : nouveau modèle, ouverture, exemples à solution analytique connue.
    m_home = new LabStartPanel(m_workspaces);
    m_workspaces->addTab(m_home, QIcon(QString::fromLatin1(TSA::Product::kGlyphSvg)), tr("Accueil"));
    connect(m_home, &LabStartPanel::newModelRequested, this, &LabMainWindow::newProject);
    connect(m_home, &LabStartPanel::openRequested, this, &LabMainWindow::openProject);
    connect(m_home, &LabStartPanel::exampleRequested, this, &LabMainWindow::openExample);

    // Modèle : viewport partagé avec TSA (rendu, caméra, grilles, accrochage, sélection, dessin).
    m_view = new OccView(m_workspaces);
    m_view->setSelectionManager(m_selection.get());
    m_view->setModel(&m_session->model());
    m_view->setGridManager(&m_session->grids(), &m_session->gridSnap());
    m_viewport = new TSA::UI::ViewportContainer(m_view, m_workspaces);
    m_viewport->setModel(&m_session->model());
    m_workspaces->addTab(m_viewport, QIcon(":/icons/view/view_3d.svg"), tr("Modèle"));

    // Blueprint : programmation visuelle (éditeur partagé) exécutée sur le projet ouvert ; chaque commande
    // du registre central est un nœud, avec ses entrées Annuler / Rétablir.
    m_blueprint = new TSA::UI::BlueprintEditor(m_workspaces);
    m_blueprint->setSession(m_session.get());
    m_workspaces->addTab(m_blueprint, QIcon(":/icons/modeling/load_dist.svg"), tr("Blueprint"));

    // Analyse : gestionnaire d'analyse partagé (moteurs, réglages du projet, validation, calcul en tâche de fond).
    m_analysisPanel = new TSA::UI::AnalysisManagerPanel(m_session.get(), m_engineOptions.get(), m_workspaces);
    m_workspaces->addTab(m_analysisPanel, QIcon(":/icons/analysis_run.svg"), tr("Analyse"));

    // Résultats : diagrammes 2D par barre (la déformée et les diagrammes 3D sont dans le viewport, dock Résultats).
    m_diagram = new TSA::UI::Diagram2DWidget(m_workspaces);
    m_diagram->setModel(&m_session->model());
    m_workspaces->addTab(m_diagram, QIcon(":/icons/results_force.svg"), tr("Résultats"));

    // Recherche : SOLVER LAB (résolution instrumentée de K·U = F du dernier calcul).
    m_solverLab = new SolverLabPage(m_workspaces);
    m_workspaces->addTab(m_solverLab, QIcon(":/icons/results_disp.svg"), tr("Recherche"));

    // Validation : banc de validation partagé avec TSA (cas de référence, écarts à la solution analytique, rapports)
    m_validation = new ValidationPage(m_workspaces);
    m_workspaces->addTab(m_validation, QIcon(":/icons/analysis_run.svg"), tr("Validation"));

    // Expériences : journal des calculs et des validations du projet (traçabilité, rejeu)
    m_experiments = new ExperimentsPage(m_workspaces);
    m_workspaces->addTab(m_experiments, QIcon(":/icons/results_force.svg"), tr("Expériences"));

    setCentralWidget(m_workspaces);
}

void LabMainWindow::createDocks()
{
    m_treeDock = new QDockWidget(tr("Explorateur du projet"), this);
    m_treeDock->setObjectName("LabProjectExplorer");
    m_tree = new TSA::UI::ModelTreeWidget(&m_session->model(), m_treeDock);
    m_treeDock->setWidget(m_tree);
    m_treeDock->setMinimumWidth(260);
    addDockWidget(Qt::LeftDockWidgetArea, m_treeDock);

    m_propertiesDock = new QDockWidget(tr("Propriétés"), this);
    m_propertiesDock->setObjectName("LabProperties");
    m_properties = new TSA::UI::PropertyPanel(&m_session->model(), m_propertiesDock);
    m_propertiesDock->setWidget(m_properties);
    m_propertiesDock->setMinimumWidth(280);
    addDockWidget(Qt::RightDockWidgetArea, m_propertiesDock);

    m_console = new TSA::UI::LogConsoleDock(this);
    m_console->setObjectName("LabConsole");
    m_console->setWindowTitle(tr("Console / Sortie"));
    addDockWidget(Qt::BottomDockWidgetArea, m_console);

    // Résultats affichés dans le viewport partagé (déformée, diagrammes, réactions) : même dock que TSA.
    m_resultsDock = new TSA::UI::ResultsDockWidget(this);
    m_resultsDock->setObjectName("LabResults");
    m_resultsDock->setModel(&m_session->model());
    addDockWidget(Qt::RightDockWidgetArea, m_resultsDock);
    tabifyDockWidget(m_propertiesDock, m_resultsDock);
    m_propertiesDock->raise();
    if (auto* rv = m_view->resultsVisual())
    {
        using RD = TSA::UI::ResultsDockWidget;
        using RV = TSA::Viewer::ResultsVisualManager;
        connect(m_resultsDock, &RD::deformedToggled, rv, &RV::setDeformedVisible);
        connect(m_resultsDock, &RD::deformedDisplayModeChanged, rv, &RV::setDeformedDisplayMode);
        connect(m_resultsDock, &RD::deformationScalePresetChanged, rv, &RV::setDeformationScalePreset);
        connect(m_resultsDock, &RD::diagramTypeChanged, rv, &RV::setDiagramType);
        connect(m_resultsDock, &RD::diagramScalePresetChanged, rv, &RV::setDiagramScalePreset);
        connect(m_resultsDock, &RD::diagramLabelsToggled, rv, &RV::setDiagramLabelsVisible);
        connect(m_resultsDock, &RD::reactionsToggled, rv, &RV::setReactionsVisible);
        connect(m_resultsDock, &RD::activeStepChanged, rv, &RV::setActiveStep);
        connect(m_resultsDock, &RD::legendToggled, rv, &RV::setLegendVisible);
    }
    connect(m_resultsDock, &TSA::UI::ResultsDockWidget::nodesVisibleToggled, m_view, &OccView::setNodesVisible);
    connect(m_resultsDock, &TSA::UI::ResultsDockWidget::nodeLabelsToggled, m_view, &OccView::setNodeLabelsVisible);
    connect(m_resultsDock, &TSA::UI::ResultsDockWidget::nodeFilterChanged, m_view, &OccView::setNodeDisplayFilter);
    connect(m_resultsDock, &TSA::UI::ResultsDockWidget::fitModelRequested, m_view, &OccView::fitModel);
    connect(m_resultsDock, &TSA::UI::ResultsDockWidget::fitResultsRequested, m_view, &OccView::fitResults);
    connect(m_resultsDock, &TSA::UI::ResultsDockWidget::fitDeformedRequested, m_view, &OccView::fitDeformed);
    connect(m_resultsDock, &TSA::UI::ResultsDockWidget::fitSelectionRequested, m_view, &OccView::fitSelection);

    // Données numériques du calcul : K globale, K·U = F, DDL, efforts bruts (matrix viewer partagé).
    m_dataDock = new TSA::UI::AnalysisDataDock(this);
    m_dataDock->setObjectName("LabAnalysisData");
    m_dataDock->setModel(&m_session->model());
    addDockWidget(Qt::BottomDockWidgetArea, m_dataDock);
    tabifyDockWidget(m_console, m_dataDock);
    m_console->raise();
}

void LabMainWindow::createAssistant()
{
    // Assistant de co-ingénierie (composants partagés avec TSA) : sources = modèle, résultats et sélection.
    m_ai = new TSA::AI::AIOrchestrator(this);
    const auto sourcesProvider = [this] {
        TSA::AI::EngineeringSources src;
        src.model = &m_session->model();
        const auto results = m_session->analysis().results();
        src.results = results.get();
        src.resultsUpToDate = m_session->analysis().resultsUpToDate();
        const auto& pm = m_session->project();
        src.projectName = pm.hasFilePath() ? QFileInfo(pm.currentFilePath()).completeBaseName() : tr("Nouveau modèle");
        const auto sel = m_selection->selectedElements();
        auto add = [&](const QString& type, const std::set<int>& ids) {
            for (int id : ids)
                if (src.selection.size() < 20) src.selection.push_back({ type, id });
        };
        add("beam", sel.beams);
        add("column", sel.columns);
        add("node", sel.nodes);
        return src;
    };
    m_ai->setSourcesProvider(sourcesProvider);

    // Serveur d'automatisation (pont MCP : Claude Code co-ingénieur sur le projet ouvert, TSA/docs/MCP.md).
    m_automation = new TSA::Automation::AutomationServer(m_session.get(), this);
    m_automation->setSourcesProvider(sourcesProvider);
    m_automation->setProjectInfoProvider([this] {
        const auto& pm = m_session->project();
        QJsonObject o { { "project", pm.hasFilePath() ? QFileInfo(pm.currentFilePath()).completeBaseName() : tr("Nouveau modèle") } };
        if (pm.hasFilePath()) o["path"] = pm.currentFilePath();
        return o;
    });
    connect(m_automation, &TSA::Automation::AutomationServer::projectModified, this, [this] {
        updateHistoryActions();
        m_view->update();
    });
    connect(m_automation, &TSA::Automation::AutomationServer::logMessage, this,
            [this](const QString& text, const QString& type) { log(text, type); });
    QString automationError;
    if (!m_automation->start(TSA::Automation::AutomationServer::defaultName(), &automationError))
        log(tr("Serveur d'automatisation (MCP) indisponible : %1").arg(automationError), QStringLiteral("WARN"));
    m_aiDock = new TSA::UI::AICoEngineeringDock(m_ai, this);
    m_aiDock->setObjectName("LabAssistant");
    addDockWidget(Qt::RightDockWidgetArea, m_aiDock);
    m_aiDock->hide();
    auto openConfig = [this] {
        if (!m_aiDialog) m_aiDialog = new TSA::UI::AIRuntimeDialog(m_ai, this);
        m_aiDialog->show();
        m_aiDialog->raise();
    };
    connect(m_aiDock, &TSA::UI::AICoEngineeringDock::configureRequested, this, openConfig);
    // Blueprint proposé par l'IA et accepté : ouvert dans l'éditeur (l'ingénieur l'exécute ou le débogue).
    connect(m_ai, &TSA::AI::AIOrchestrator::blueprintAccepted, this, [this](const QString& title, const QString& script) {
        QString error;
        if (!m_blueprint->importScript(script, &error))
        {
            log(tr("Blueprint de l'IA « %1 » refusé : %2").arg(title, error), QStringLiteral("ERROR"));
            return;
        }
        m_blueprint->graph().name = title.toStdString();
        m_workspaces->setCurrentWidget(m_blueprint);
        log(tr("Blueprint proposé par l'IA ouvert : « %1 » (Exécuter ou Déboguer pour l'appliquer).").arg(title));
    });
    connect(m_ai, &TSA::AI::AIOrchestrator::runAnalysisRequested, this, [this] {
        m_workspaces->setCurrentWidget(m_analysisPanel);
        m_analysisPanel->run();
    });
    connect(m_ai, &TSA::AI::AIOrchestrator::modelChanged, this, [this] {
        updateHistoryActions();
        m_view->update();
    });
}

void LabMainWindow::showPlugins()
{
    const auto& list = TSA::Plugins::PluginManager::instance().plugins();
    QString text = tr("<p>Dossier : <code>%1</code></p>").arg(QDir::toNativeSeparators(TSA::Plugins::PluginManager::defaultDirectory()).toHtmlEscaped());
    if (list.empty()) text += tr("<p>Aucun plugin chargé.</p>");
    for (const auto& p : list)
    {
        QStringList items;
        for (const auto& c : p.commands) items << QString::fromStdString(c);
        for (const auto& n : p.nodes) items << QString::fromStdString(n);
        text += p.loaded ? tr("<p><b>%1</b> %2 — %3<br>%4</p>")
                               .arg(QString::fromStdString(p.info.name).toHtmlEscaped(), QString::fromStdString(p.info.version).toHtmlEscaped(),
                                    QString::fromStdString(p.info.description).toHtmlEscaped(), items.join(QStringLiteral(", ")).toHtmlEscaped())
                         : tr("<p><b>Refusé</b> : %1<br>%2</p>").arg(QDir::toNativeSeparators(p.path).toHtmlEscaped(), p.error.toHtmlEscaped());
    }
    QMessageBox::information(this, tr("Plugins"), text);
}

void LabMainWindow::createActions()
{
    m_actNew = new QAction(QIcon(":/icons/file_new.svg"), tr("&Nouveau modèle"), this);
    m_actNew->setShortcut(QKeySequence::New);
    connect(m_actNew, &QAction::triggered, this, &LabMainWindow::newProject);

    m_actOpen = new QAction(QIcon(":/icons/file_open.svg"), tr("&Ouvrir…"), this);
    m_actOpen->setShortcut(QKeySequence::Open);
    connect(m_actOpen, &QAction::triggered, this, &LabMainWindow::openProject);

    m_actSave = new QAction(QIcon(":/icons/file_save.svg"), tr("&Enregistrer"), this);
    m_actSave->setShortcut(QKeySequence::Save);
    connect(m_actSave, &QAction::triggered, this, &LabMainWindow::save);

    m_actSaveAs = new QAction(QIcon(":/icons/file/file_save_as.svg"), tr("Enregistrer &sous…"), this);
    m_actSaveAs->setShortcut(QKeySequence::SaveAs);
    connect(m_actSaveAs, &QAction::triggered, this, &LabMainWindow::saveAs);

    m_actUndo = new QAction(QIcon(":/icons/undo.svg"), tr("&Annuler"), this);
    m_actUndo->setShortcut(QKeySequence::Undo);
    connect(m_actUndo, &QAction::triggered, this, [this] {
        std::string name;
        if (m_session->undo(&name)) statusBar()->showMessage(tr("Action annulée : %1").arg(QString::fromStdString(name)), 3000);
        updateHistoryActions();
    });

    m_actRedo = new QAction(QIcon(":/icons/redo.svg"), tr("&Rétablir"), this);
    m_actRedo->setShortcut(QKeySequence::Redo);
    connect(m_actRedo, &QAction::triggered, this, [this] {
        std::string name;
        if (m_session->redo(&name)) statusBar()->showMessage(tr("Action rétablie : %1").arg(QString::fromStdString(name)), 3000);
        updateHistoryActions();
    });
}

void LabMainWindow::createMenus()
{
    // --- Fichier
    QMenu* file = menuBar()->addMenu(tr("&Fichier"));
    file->addAction(m_actNew);
    file->addAction(m_actOpen);
    QMenu* examples = file->addMenu(tr("E&xemples"));
    for (const auto& ex : TSALab::Research::Examples::catalog())
    {
        const QString id = QString::fromStdString(ex.id);
        QAction* a = examples->addAction(QString::fromStdString(ex.title));
        a->setToolTip(QString::fromStdString(ex.reference));
        connect(a, &QAction::triggered, this, [this, id] { openExample(id); });
    }
    file->addSeparator();
    file->addAction(m_actSave);
    file->addAction(m_actSaveAs);
    file->addSeparator();
    QAction* quit = file->addAction(QIcon(":/icons/file_exit.svg"), tr("&Quitter"), this, &QWidget::close);
    quit->setShortcut(QKeySequence::Quit);

    // --- Édition
    QMenu* edit = menuBar()->addMenu(tr("&Édition"));
    edit->addAction(m_actUndo);
    edit->addAction(m_actRedo);

    // --- Affichage
    QMenu* view = menuBar()->addMenu(tr("&Affichage"));
    QAction* fit = view->addAction(tr("&Ajuster au modèle"), this, [this] { m_view->fitModel(); });
    fit->setShortcut(QKeySequence(Qt::Key_F));
    QMenu* standard = view->addMenu(tr("Vues standard"));
    using TSA::Viewer::StandardCameraView;
    const std::pair<QString, StandardCameraView> views[] = {
        { tr("3D isométrique"), StandardCameraView::ThreeD_Axo }, { tr("Dessus"), StandardCameraView::Top },
        { tr("Face"), StandardCameraView::Front }, { tr("Droite"), StandardCameraView::Right },
    };
    for (const auto& [label, v] : views)
        standard->addAction(label, this, [this, v = v] { m_view->applyStandardView(v); });
    QAction* grid = view->addAction(tr("&Grille"));
    grid->setCheckable(true);
    grid->setChecked(true);
    connect(grid, &QAction::toggled, this, [this](bool on) { m_view->setGridVisible(on); });
    view->addSeparator();
    // Échelles des annotations et des charges, profils d'affichage, diagnostic : composant partagé avec TSA
    // (TSA/src/UI/Display/DisplayMenus) ; représentation seulement, le modèle et les résultats ne changent pas.
    m_displayMenus = new TSA::UI::DisplayMenus(this, { [this] { return m_view; },
                                                       [this]() -> const TSA::Model::Model* { return &m_session->model(); },
                                                       [this] { return m_session->project().hasFilePath() ? m_session->project().currentFilePath() : QString(); } });
    m_displayMenus->addTo(view);
    connect(m_displayMenus, &TSA::UI::DisplayMenus::profileApplied, this, [this, grid] {
        const QSignalBlocker block(grid);
        grid->setChecked(m_view->isGridVisible());
        if (m_resultsDock && m_view->resultsVisual()) m_resultsDock->syncFromVisualManager(m_view->resultsVisual());
    });
    connect(m_displayMenus, &TSA::UI::DisplayMenus::message, this, [this](const QString& text, const QString& type) { log(text, type); });
    view->addSeparator();
    QMenu* panels = view->addMenu(tr("&Panneaux"));
    for (QDockWidget* d : { m_treeDock, m_propertiesDock, static_cast<QDockWidget*>(m_console),
                            static_cast<QDockWidget*>(m_resultsDock), static_cast<QDockWidget*>(m_dataDock),
                            static_cast<QDockWidget*>(m_aiDock) })
        panels->addAction(d->toggleViewAction());
    view->addAction(tr("Disposition par défaut"), this, [this] { restoreState(m_defaultLayout); });
    view->addAction(tr("Thème clair / sombre"), this, [] { TSA::UI::ThemeManager::instance().toggleTheme(); });

    // --- Modèle : outils de dessin du viewport partagé (objets réels du modèle)
    QMenu* model = menuBar()->addMenu(tr("&Modèle"));
    using Mode = OccView::InteractionMode;
    const std::pair<QString, Mode> tools[] = {
        { tr("Sélection"), Mode::Select }, { tr("Nœud"), Mode::DrawNode }, { tr("Poutre"), Mode::DrawBeam },
        { tr("Poteau"), Mode::DrawColumn }, { tr("Barre"), Mode::DrawBar },
    };
    QToolBar* bar = addToolBar(tr("Barre d'outils"));
    bar->setObjectName("LabToolBar");
    for (QAction* a : { m_actNew, m_actOpen, m_actSave, m_actUndo, m_actRedo }) bar->addAction(a);
    bar->addSeparator();
    for (const auto& [label, mode] : tools)
    {
        QAction* a = model->addAction(label, this, [this, mode = mode] {
            m_workspaces->setCurrentWidget(m_viewport);
            m_view->setInteractionMode(mode);
        });
        bar->addAction(a);
    }
    bar->addAction(fit);

    // --- Blueprint
    createMeshMenu();

    QMenu* bp = menuBar()->addMenu(tr("&Blueprint"));
    bp->addAction(tr("Nouveau Blueprint"), this, [this] {
        m_blueprint->newBlueprint();
        m_workspaces->setCurrentWidget(m_blueprint);
    });
    QAction* runBp = bp->addAction(tr("▶ Exécuter le Blueprint"), this, [this] {
        m_workspaces->setCurrentWidget(m_blueprint);
        m_blueprint->run();
    });
    runBp->setShortcut(QKeySequence(Qt::Key_F5));
    bp->addAction(tr("Valider"), this, [this] { m_blueprint->validate(); });
    bp->addSeparator();
    QAction* debugBp = bp->addAction(tr("Déboguer"), this, [this] {
        m_workspaces->setCurrentWidget(m_blueprint);
        m_blueprint->debug();
    });
    debugBp->setShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F5));
    QAction* stepBp = bp->addAction(tr("Pas à pas"), this, [this] {
        m_workspaces->setCurrentWidget(m_blueprint);
        m_blueprint->step();
    });
    stepBp->setShortcut(QKeySequence(Qt::Key_F10));
    QAction* continueBp = bp->addAction(tr("Continuer"), this, [this] { m_blueprint->continueExecution(); });
    continueBp->setShortcut(QKeySequence(Qt::Key_F8));
    bp->addAction(tr("Arrêter l'exécution"), this, [this] { m_blueprint->stop(); });
    bp->addAction(tr("Point d'arrêt sur le nœud sélectionné"), this, [this] {
        m_blueprint->toggleBreakpoint(m_blueprint->selectedNode());
    });
    bp->addSeparator();
    bp->addAction(tr("Annuler (graphe)"), this, [this] { m_blueprint->undo(); });
    bp->addAction(tr("Rétablir (graphe)"), this, [this] { m_blueprint->redo(); });
    QMenu* bpExamples = bp->addMenu(tr("Exemples"));
    for (const auto& ex : TSALab::Research::BlueprintExamples::catalog())
    {
        const std::string id = ex.id;
        QAction* a = bpExamples->addAction(QString::fromStdString(ex.title), this, [this, id] {
            TSA::Blueprint::Graph g;
            if (!TSALab::Research::BlueprintExamples::build(id, g)) return;
            m_blueprint->setGraph(std::move(g));
            m_workspaces->setCurrentWidget(m_blueprint);
        });
        a->setToolTip(QString::fromStdString(ex.description));
    }

    // --- Analyse : gestionnaire partagé (mêmes moteurs, mêmes réglages que TSA)
    QMenu* an = menuBar()->addMenu(tr("A&nalyse"));
    an->addAction(tr("Gestionnaire d'analyse"), this, [this] { m_workspaces->setCurrentWidget(m_analysisPanel); });
    an->addAction(tr("Configurer le calcul…"), this, [this] {
        m_workspaces->setCurrentWidget(m_analysisPanel);
        m_analysisPanel->configure();
    });
    an->addAction(tr("Valider le modèle d'analyse"), this, [this] {
        m_workspaces->setCurrentWidget(m_analysisPanel);
        m_analysisPanel->validate();
    });
    QAction* runAnalysis = an->addAction(QIcon(":/icons/analysis_run.svg"), tr("▶ Calculer"), this, [this] {
        m_workspaces->setCurrentWidget(m_analysisPanel);
        m_analysisPanel->run();
    });
    runAnalysis->setShortcut(QKeySequence(Qt::Key_F9));
    an->addAction(tr("Annuler le calcul"), this, [this] { m_analysisPanel->cancel(); });
    an->addSeparator();
    an->addAction(tr("Diagrammes 2D"), this, [this] { m_workspaces->setCurrentWidget(m_diagram); });
    an->addAction(tr("Données d'analyse (K, F, U)"), this, [this] {
        m_dataDock->show();
        m_dataDock->raise();
    });
    an->addAction(tr("SOLVER LAB"), this, [this] { m_workspaces->setCurrentWidget(m_solverLab); });
    bar->addSeparator();
    bar->addAction(runAnalysis);

    // --- Assistant IA
    QMenu* ai = menuBar()->addMenu(tr("&IA"));
    QAction* assistant = m_aiDock->toggleViewAction();
    assistant->setText(tr("Assistant IA"));
    assistant->setShortcut(QKeySequence("Ctrl+Shift+I"));
    ai->addAction(assistant);
    ai->addAction(tr("Configuration IA…"), this, [this] {
        if (!m_aiDialog) m_aiDialog = new TSA::UI::AIRuntimeDialog(m_ai, this);
        m_aiDialog->show();
        m_aiDialog->raise();
    });

    // --- Aide
    QMenu* help = menuBar()->addMenu(tr("&Aide"));
    help->addAction(tr("Plugins chargés…"), this, &LabMainWindow::showPlugins);
    help->addAction(tr("À propos de %1").arg(TSA::Product::name()), this, [this] {
        QMessageBox::about(this, tr("À propos de %1").arg(TSA::Product::name()),
                           tr("<h3>%1 %2</h3><p>%3</p>%4")
                               .arg(TSA::Product::name(), TSA::Product::version(), TSA::Product::longName(),
                                    QString::fromUtf8(TSA::Product::kAboutIntroHtml)));
    });
}

void LabMainWindow::createStatusBar()
{
    m_statusPrompt = new QLabel(this);
    m_statusCoordinates = new QLabel(this);
    m_statusCoordinates->setMinimumWidth(260);
    statusBar()->addWidget(m_statusPrompt, 1);
    statusBar()->addPermanentWidget(m_statusCoordinates);
    connect(m_view, &OccView::drawingPromptChanged, m_statusPrompt, &QLabel::setText);
    connect(m_view, &OccView::mouseCoordinatesChanged, this, [this](double x, double y, double z) {
        m_statusCoordinates->setText(QStringLiteral("X %1  Y %2  Z %3 m")
                                         .arg(x, 0, 'f', 3).arg(y, 0, 'f', 3).arg(z, 0, 'f', 3));
    });
}

// -----------------------------------------------------------------------------
// Projet
// -----------------------------------------------------------------------------

void LabMainWindow::newProject()
{
    if (!maybeSave()) return;
    m_selection->clearSelection();
    m_session->project().newProject(m_session->model(), &m_session->grids());
    m_workspaces->setCurrentWidget(m_viewport);
    afterProjectLoaded();
    log(tr("Nouveau modèle."));
}

void LabMainWindow::openProject()
{
    if (!maybeSave()) return;
    const auto recent = TSA::Project::RecentProjects().list(false);
    const QString dir = recent.isEmpty() ? QString() : QFileInfo(recent.first().path).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Ouvrir un projet"), dir, TSA::Product::openFileFilter());
    if (!path.isEmpty()) openProjectFile(path);
}

bool LabMainWindow::openProjectFile(const QString& path)
{
    // Le viewport doit être visible (OCCT initialisé) avant le chargement.
    m_workspaces->setCurrentWidget(m_viewport);
    m_selection->clearSelection();
    QString error;
    if (!m_session->project().openProject(path, m_session->model(), &m_session->grids(), &error))
    {
        QMessageBox::critical(this, tr("Ouverture"), tr("Échec de l'ouverture du projet :\n%1").arg(error));
        return false;
    }
    TSA::Project::RecentProjects().touch(path);
    afterProjectLoaded();
    log(tr("Projet chargé : %1").arg(QDir::toNativeSeparators(path)));
    return true;
}

bool LabMainWindow::openExample(const QString& exampleId)
{
    namespace Examples = TSALab::Research::Examples;
    const auto& catalog = Examples::catalog();
    const auto it = std::find_if(catalog.begin(), catalog.end(),
                                 [&](const auto& e) { return QString::fromStdString(e.id) == exampleId; });
    if (it == catalog.end() || !maybeSave()) return false;

    // Copie de travail (Documents/TSALab/Exemples) : générée au premier usage, puis rouverte telle quelle.
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                            .filePath(QStringLiteral("%1/Exemples").arg(TSA::Product::kDocumentsFolder));
    const QString title = QString::fromStdString(it->title);
    QString fileName = title;
    static const QRegularExpression invalid(QStringLiteral(R"([<>:"/\\|?*])"));
    fileName.replace(invalid, QStringLiteral("-"));
    const QString path = QDir(dir).filePath(fileName + TSA::Product::projectExtension());
    if (!QFileInfo::exists(path))
    {
        QString error;
        TSA::Model::Model model;
        if (!QDir().mkpath(dir) || !Examples::build(exampleId.toStdString(), model)
            || !TSA::IO::TSAProjectIO::saveProject(path, model, nullptr, title, QString(), true, QImage(), &error))
        {
            QMessageBox::warning(this, tr("Exemple"), tr("Impossible de créer l'exemple « %1 » :\n%2").arg(title, error));
            return false;
        }
    }
    return openProjectFile(path);
}

bool LabMainWindow::save()
{
    const auto& pm = m_session->project();
    // Un modèle d'un format importé (.tsa de TSA) n'est jamais réécrit : enregistrement en format natif.
    if (pm.hasFilePath() && TSA::Product::isNativeProjectFile(pm.currentFilePath())) return saveTo(pm.currentFilePath());
    return saveAs();
}

bool LabMainWindow::saveAs()
{
    const auto& pm = m_session->project();
    const QString proposed = pm.hasFilePath() ? TSA::Product::withProjectExtension(pm.currentFilePath())
                                              : tr("Modèle") + TSA::Product::projectExtension();
    QString path = QFileDialog::getSaveFileName(this, tr("Enregistrer le projet"), proposed, TSA::Product::saveFileFilter());
    if (path.isEmpty()) return false;
    return saveTo(TSA::Product::withProjectExtension(path));
}

bool LabMainWindow::saveTo(const QString& path)
{
    QString error;
    const QImage thumbnail = m_view->captureViewImage(640, 480);
    if (!m_session->project().saveProject(path, m_session->model(), &m_session->grids(), thumbnail, &error))
    {
        QMessageBox::critical(this, tr("Enregistrement"), tr("Échec de l'enregistrement :\n%1").arg(error));
        return false;
    }
    TSA::Project::RecentProjects().touch(path);
    m_experiments->setDirectory(TSALab::Research::ExperimentJournal::directoryForProject(path));
    updateTitle();
    log(tr("Projet enregistré : %1").arg(QDir::toNativeSeparators(path)));
    return true;
}

bool LabMainWindow::maybeSave()
{
    if (!m_session->hasUnsavedChanges()) return true;
    const auto answer = QMessageBox::warning(this, TSA::Product::name(),
                                             tr("Le modèle a été modifié. Enregistrer les modifications ?"),
                                             QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
    if (answer == QMessageBox::Save) return save();
    return answer == QMessageBox::Discard;
}

void LabMainWindow::afterProjectLoaded()
{
    m_session->model().clearUndoRedo();
    // Réglages d'analyse enregistrés avec le projet ; résultats du projet précédent oubliés.
    if (!m_session->analysis().restoreContextFromModel())
        log(tr("Paramètres d'analyse du projet illisibles : réglages par défaut."), QStringLiteral("WARN"));
    if (m_session->model().analysisSettingsJson().empty()) applyLabAnalysisDefaults();
    m_session->analysis().clearResults();
    m_analysisPanel->refresh();
    m_view->rebuildGrid();
    m_view->fitModel();
    // Profil d'affichage associé à ce fichier (réglages de l'application, partagé avec TSA ; le fichier n'en contient pas)
    if (m_displayMenus && m_session->project().hasFilePath())
        if (const QString profile = TSA::Viewer::DisplayProfileStore().documentProfile(m_session->project().currentFilePath()); !profile.isEmpty())
            m_displayMenus->applyProfile(profile);
    m_tree->setProjectName(m_session->project().hasFilePath() ? m_session->project().currentFileName()
                                                               : tr("Nouveau modèle") + TSA::Product::projectExtension());
    m_tree->refreshAll();
    m_properties->clearProperties();
    m_experiments->setDirectory(TSALab::Research::ExperimentJournal::directoryForProject(
        m_session->project().hasFilePath() ? m_session->project().currentFilePath() : QString()));
    m_mesh->setModel(&m_session->model());   // maillage et paramètres enregistrés dans le projet
    m_meshPanel->refresh();
    updateTitle();
    updateHistoryActions();
}

void LabMainWindow::updateTitle()
{
    const auto& pm = m_session->project();
    const QString name = pm.hasFilePath() ? pm.currentFileName() : tr("Nouveau modèle");
    setWindowTitle(QStringLiteral("%1%2 — %3").arg(name, m_session->hasUnsavedChanges() ? QStringLiteral(" *") : QString(),
                                                   TSA::Product::name()));
}

void LabMainWindow::updateHistoryActions()
{
    m_actUndo->setEnabled(m_session->canUndo());
    m_actRedo->setEnabled(m_session->canRedo());
    updateTitle();
}

void LabMainWindow::runConsoleCommand(const QString& line)
{
    using namespace TSA::Automation;
    const CommandRegistry& registry = CommandRegistry::builtIn();
    const QString trimmed = line.trimmed();
    if (trimmed.isEmpty()) return;

    if (trimmed.compare(QLatin1String("help"), Qt::CaseInsensitive) == 0
        || trimmed.compare(QLatin1String("aide"), Qt::CaseInsensitive) == 0)
    {
        for (const CommandSpec* spec : registry.commands())
        {
            QStringList params;
            for (const auto& p : spec->parameters)
            {
                QString unit = QString::fromUtf8(quantityUnit(p.quantity));
                params << QStringLiteral("%1%2=<%3%4>%5")
                              .arg(p.required ? QString() : QStringLiteral("["), QString::fromStdString(p.name),
                                   QString::fromUtf8(typeName(p.type)), unit.isEmpty() ? QString() : QStringLiteral(", ") + unit,
                                   p.required ? QString() : QStringLiteral("]"));
            }
            log(QStringLiteral("%1 — %2 : %3").arg(QString::fromStdString(spec->id), QString::fromStdString(spec->title),
                                                   params.join(QLatin1Char(' '))),
                QStringLiteral("INFO"));
        }
        return;
    }

    const CommandResult r = executeCommandLine(registry, *m_session, trimmed.toStdString());
    log(QString::fromStdString(r.message), r.ok ? QStringLiteral("SYS") : QStringLiteral("ERROR"));
    if (r.ok)
    {
        updateHistoryActions();
        if (const auto* spec = registry.find(trimmed.section(QLatin1Char(' '), 0, 0).toStdString()); spec && spec->modifiesModel)
            m_workspaces->setCurrentWidget(m_viewport);   // montrer le modèle modifié
        m_view->update();
    }
}

void LabMainWindow::applyLabAnalysisDefaults()
{
    // Projet sans réglages d'analyse : le laboratoire part du moteur intégré (Custom2D, cœur scientifique),
    // sur le plan du modèle s'il est plan, système K·U = F exporté (matrix viewer, SOLVER LAB). Rien n'est
    // écrit dans le modèle tant que l'utilisateur ne modifie pas les réglages.
    using namespace TSA::Analysis;
    AnalysisController& ac = m_session->analysis();
    if (!ac.registry().engine("custom2d")) return;
    AnalysisContext c = ac.context();
    c.engineId = "custom2d";
    c.dimension = AnalysisDimension::Plane2D;
    c.scope = AnalysisScope {};
    c.scope.type = ScopeType::ModelPlane;   // plan détecté à chaque calcul (le modèle peut encore être dessiné)
    QJsonObject settings = c.settingsFor("custom2d");
    settings.insert(QStringLiteral("exportSystem"), true);
    c.engineSettings["custom2d"] = settings;
    ac.setContext(c);
}

void LabMainWindow::onResultsChanged()
{
    const auto results = m_session->analysis().results();
    m_view->setResultsModel(results);
    m_resultsDock->setResultsModel(results);
    m_dataDock->setResultsModel(results);
    m_diagram->setResultsModel(results);
    m_properties->setResultsModel(results);
    m_solverLab->setResults(results);
    if (m_actMeshResults && m_actMeshResults->isChecked()) refreshMeshOverlay();
    if (results)
    {
        if (m_view->resultsVisual()) m_resultsDock->syncFromVisualManager(m_view->resultsVisual());
        m_resultsDock->show();
        m_resultsDock->raise();
        statusBar()->showMessage(tr("Résultats publiés : δmax = %1 mm").arg(results->summary().maxDisplacement * 1000.0, 0, 'f', 3), 6000);
    }
    m_view->update();
}

void LabMainWindow::onResultsBecameStale()
{
    // Les vues vérifient ResultsModel::isValid() : il suffit de les rafraîchir.
    const auto results = m_session->analysis().results();
    if (m_view->resultsVisual()) m_view->resultsVisual()->clearAllVisuals();
    m_resultsDock->setResultsModel(results);
    m_dataDock->setResultsModel(results);
    m_diagram->setResultsModel(results);
    m_properties->setResultsModel(results);
    m_solverLab->setResults(results);
    m_view->update();
    log(tr("Modèle modifié depuis le dernier calcul : résultats obsolètes, relancez l'analyse."), QStringLiteral("WARN"));
}

void LabMainWindow::createMesh()
{
    m_mesh = new TSA::UI::MeshController(this);
    m_meshPanel = new TSA::UI::MeshSettingsPanel(m_mesh, this);
    m_meshDock = new QDockWidget(tr("Paramètres du maillage"), this);
    m_meshDock->setObjectName(QStringLiteral("LabMeshSettings"));
    m_meshDock->setWidget(m_meshPanel);
    addDockWidget(Qt::RightDockWidgetArea, m_meshDock);
    m_meshDock->hide();
    m_meshQuality = new TSA::UI::MeshQualityPanel(this);
    m_meshQualityDock = new QDockWidget(tr("Qualité du maillage"), this);
    m_meshQualityDock->setObjectName(QStringLiteral("LabMeshQuality"));
    m_meshQualityDock->setWidget(m_meshQuality);
    addDockWidget(Qt::BottomDockWidgetArea, m_meshQualityDock);
    m_meshQualityDock->hide();
    // Contexte et vue lus à chaque usage : la vue OCCT n'existe qu'après le premier affichage
    const QPointer<OccView> view(m_view);
    m_meshVisual = std::make_unique<TSA::Viewer::MeshVisual>([view]() { return view ? view->context() : Handle(AIS_InteractiveContext)(); },
                                                             [view]() { return view ? view->view() : Handle(V3d_View)(); });

    connect(m_meshPanel, &TSA::UI::MeshSettingsPanel::qualityRequested, this, &LabMainWindow::showMeshQuality);
    connect(m_meshQuality, &TSA::UI::MeshQualityPanel::issueActivated, this, [this](int element, int node) {
        if (!m_mesh->hasMesh()) return;
        if (!m_actMeshShow->isChecked()) m_actMeshShow->setChecked(true);
        m_meshVisual->highlight(element, node, true);
    });
    connect(m_mesh, &TSA::UI::MeshController::generationFinished, this, [this](bool preview, bool ok, const QString& message) {
        log((preview ? tr("Aperçu du maillage (Gmsh) : %1") : tr("Maillage généré (Gmsh) : %1")).arg(message), ok ? QStringLiteral("SYS") : QStringLiteral("ERROR"));
        if (const auto* rec = preview ? m_mesh->previewRecord() : &m_mesh->project().record; ok && rec)
            for (const auto& w : rec->warnings) log(QString::fromStdString(w), QStringLiteral("WARN"));
        statusBar()->showMessage(TSA::UI::meshStatusText(*m_mesh), 8000);
        if (ok && !preview)
        {
            showMeshQuality();
            m_actMeshShow->setChecked(true);
        }
        refreshMeshOverlay();
    });
    connect(m_mesh, &TSA::UI::MeshController::previewChanged, this, &LabMainWindow::refreshMeshOverlay);
    connect(m_mesh, &TSA::UI::MeshController::meshChanged, this, &LabMainWindow::refreshMeshOverlay);
    connect(m_mesh, &TSA::UI::MeshController::settingsCommitted, this, &LabMainWindow::updateTitle);
    m_mesh->setModel(&m_session->model());
}

void LabMainWindow::createMeshMenu()
{
    QMenu* menu = menuBar()->addMenu(tr("Ma&illage"));
    menu->addAction(tr("Paramètres du maillage…"), this, [this] {
        m_meshPanel->refresh();
        m_meshDock->show();
        m_meshDock->raise();
    });
    menu->addAction(tr("Générer le maillage (Gmsh)"), this, [this] {
        if (!TSA::Mesh::meshEngineCapabilities().available)
        {
            QMessageBox::warning(this, tr("Maillage"), QString::fromStdString(TSA::Mesh::meshEngineCapabilities().unavailableReason));
            return;
        }
        if (m_session->model().nodes().empty())
        {
            QMessageBox::information(this, tr("Maillage"), tr("Le modèle est vide. Ajoutez des éléments avant de générer le maillage."));
            return;
        }
        if (!m_mesh->busy()) m_mesh->generate(false);
    });
    m_actMeshShow = menu->addAction(tr("Afficher le maillage"));
    m_actMeshShow->setCheckable(true);
    connect(m_actMeshShow, &QAction::toggled, this, &LabMainWindow::refreshMeshOverlay);
    m_actMeshResults = menu->addAction(tr("Déplacements sur le maillage"));
    m_actMeshResults->setCheckable(true);
    m_actMeshResults->setToolTip(tr("Couleurs et déformée : valeurs calculées par le moteur sur ce maillage, sans extrapolation"));
    connect(m_actMeshResults, &QAction::toggled, this, [this](bool on) {
        if (on && !m_actMeshShow->isChecked()) m_actMeshShow->setChecked(true);
        else refreshMeshOverlay();
    });
    menu->addAction(tr("Qualité du maillage"), this, &LabMainWindow::showMeshQuality);
    menu->addSeparator();
    menu->addAction(tr("Supprimer le maillage"), this, [this] {
        if (m_mesh->hasMesh() &&
            QMessageBox::question(this, tr("Supprimer le maillage"), tr("Supprimer le maillage du projet ? Les paramètres sont conservés.")) == QMessageBox::Yes)
            m_mesh->clearMesh();
    });
}

void LabMainWindow::showMeshQuality()
{
    const bool has = m_mesh->hasMesh();
    QString context = tr("Maillage du projet");
    if (has)
    {
        const auto& rec = m_mesh->project().record;
        context = tr("%1 %2 — %3").arg(QString::fromStdString(rec.mesher), QString::fromStdString(rec.mesherVersion), QString::fromStdString(rec.generated));
        if (m_mesh->stale()) context += tr(" — OBSOLÈTE : géométrie modifiée depuis la génération");
    }
    m_meshQuality->setReport(m_mesh->quality(), has ? &m_mesh->project().mesh : nullptr, context);
    m_meshQualityDock->show();
    m_meshQualityDock->raise();
}

void LabMainWindow::refreshMeshOverlay()
{
    if (!m_meshVisual || !m_mesh) return;
    if (const auto* preview = m_mesh->preview())
    {
        m_meshVisual->show(*preview, TSA::Viewer::MeshVisual::Mode::Preview);
        return;
    }
    if (!m_actMeshShow || !m_actMeshShow->isChecked() || !m_mesh->hasMesh())
    {
        m_meshVisual->clear();
        return;
    }
    const auto& mesh = m_mesh->project().mesh;
    std::optional<TSA::Mesh::MeshResultField> displacement;
    if (m_actMeshResults->isChecked())
    {
        const auto results = m_session->analysis().results();
        if (!results || !results->isValid())
            statusBar()->showMessage(tr("Déplacements sur le maillage : aucun résultat valide (lancer le calcul)"), 6000);
        else
        {
            // Uniquement des valeurs calculées par le moteur sur ce maillage (empreinte du maillage comparée)
            const auto& rec = m_mesh->project().record;
            const auto set = TSA::Mesh::ResultsModelMeshAdapter(m_session->model(), *results, rec.generated + "|" + rec.settingsHash).extract(mesh);
            if (const auto* f = set.find("Déplacement")) displacement = *f;
            for (const auto& n : set.notes) log(QString::fromStdString(n), QStringLiteral("WARN"));
            if (!displacement) log(tr("Déplacements sur le maillage : aucune valeur calculée sur ce maillage (coques non calculées ou maillage différent du calcul)."),
                                   QStringLiteral("WARN"));
        }
    }
    double scale = 1.0;
    if (displacement)
    {
        // Facteur automatique : plus grand déplacement affiché à 5 % de la dimension du maillage
        double lo[3] = { 1e300, 1e300, 1e300 }, hi[3] = { -1e300, -1e300, -1e300 };
        for (const auto& n : mesh.nodes)
            for (int k = 0; k < 3; ++k)
            {
                const double c = k == 0 ? n.x : (k == 1 ? n.y : n.z);
                lo[k] = std::min(lo[k], c), hi[k] = std::max(hi[k], c);
            }
        const double size = std::sqrt((hi[0] - lo[0]) * (hi[0] - lo[0]) + (hi[1] - lo[1]) * (hi[1] - lo[1]) + (hi[2] - lo[2]) * (hi[2] - lo[2]));
        const double umax = displacement->range().second;
        scale = umax > 1e-15 ? 0.05 * size / umax : 1.0;
    }
    const auto& q = m_mesh->quality().perElementQuality;
    m_meshVisual->show(mesh, TSA::Viewer::MeshVisual::Mode::Generated, &q, 0.1, displacement ? &*displacement : nullptr, displacement ? &*displacement : nullptr, scale);
    m_view->update();
}

void LabMainWindow::log(const QString& text, const QString& type)
{
    if (m_console) m_console->appendLog(text, type, QStringLiteral("TSALab"));
}

void LabMainWindow::closeEvent(QCloseEvent* event)
{
    if (m_session->analysis().isRunning())
    {
        QMessageBox::information(this, tr("Calcul en cours"), tr("Un calcul est en cours : annulez-le ou attendez sa fin avant de quitter."));
        event->ignore();
        return;
    }
    if (!maybeSave())
    {
        event->ignore();
        return;
    }
    QSettings s = labSettings();
    s.setValue(kGeometryKey, saveGeometry());
    s.setValue(kStateKey, saveState(kLayoutVersion));
    event->accept();
}

} // namespace TSALab::UI
