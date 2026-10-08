#include "LabMainWindow.h"

#include "LabUI/LabStartPanel.h"
#include "Research/Examples/ExampleModels.h"

#include "App/ProductInfo.h"
#include "Automation/CommandRegistry.h"
#include "IO/TSAFile.h"
#include "Model/Model.h"
#include "Project/ProjectManager.h"
#include "Project/ProjectSession.h"
#include "Project/RecentProjects.h"
#include "UI/Common/SelectionSynchronizer.h"
#include "UI/Dock/LogConsoleDock.h"
#include "UI/ModelTree/ModelTreeWidget.h"
#include "UI/Properties/PropertyPanel.h"
#include "UI/Ruler/ViewportContainer.h"
#include "UI/Theme/ThemeManager.h"
#include "Viewer/OccView.h"
#include "Viewer/SelectionManager.h"
#include "Viewer/ViewManager.h"

#include <QAction>
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
#include <QStandardPaths>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>

#include <algorithm>

namespace TSALab::UI
{

namespace
{
const char* kStateKey = "LabMainWindow/state";
const char* kGeometryKey = "LabMainWindow/geometry";

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
{
    setObjectName("TSALabMainWindow");
    setDockNestingEnabled(true);
    resize(1500, 900);

    createWorkspaces();
    createDocks();
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

    m_defaultLayout = saveState();
    QSettings s = labSettings();
    restoreGeometry(s.value(kGeometryKey).toByteArray());
    restoreState(s.value(kStateKey).toByteArray());

    m_session->project().newProject(m_session->model(), &m_session->grids());
    m_tree->setGridManager(&m_session->grids());
    updateTitle();
    updateHistoryActions();
    log(tr("%1 initialisé — base commune TSA (modèle, viewport, commandes, panneaux).").arg(TSA::Product::name()));
}

LabMainWindow::~LabMainWindow() = default;

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
    QMenu* panels = view->addMenu(tr("&Panneaux"));
    for (QDockWidget* d : { m_treeDock, m_propertiesDock, static_cast<QDockWidget*>(m_console) })
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

    // --- Aide
    QMenu* help = menuBar()->addMenu(tr("&Aide"));
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
    m_view->rebuildGrid();
    m_view->fitModel();
    m_tree->setProjectName(m_session->project().hasFilePath() ? m_session->project().currentFileName()
                                                               : tr("Nouveau modèle") + TSA::Product::projectExtension());
    m_tree->refreshAll();
    m_properties->clearProperties();
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

void LabMainWindow::log(const QString& text, const QString& type)
{
    if (m_console) m_console->appendLog(text, type, QStringLiteral("TSALab"));
}

void LabMainWindow::closeEvent(QCloseEvent* event)
{
    if (!maybeSave())
    {
        event->ignore();
        return;
    }
    QSettings s = labSettings();
    s.setValue(kGeometryKey, saveGeometry());
    s.setValue(kStateKey, saveState());
    event->accept();
}

} // namespace TSALab::UI
