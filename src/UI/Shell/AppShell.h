#pragma once

// Fenêtre unique de TSALab : barre de titre personnalisée + deux modes exclusifs.
//   StartCenter      : écran d'accueil seul (créé au lancement) ;
//   ProjectWorkspace : LabWorkspaceHost (rail des espaces MODEL, ANALYSIS, RESULTS, ELEMENT LAB,
//                      SOLVER LAB, EXPERIMENT, VALIDATION, VISUAL CODING) autour de MainWindow
//                      (viewport, ruban, docks, barre d'état), créé au premier projet ouvert ou créé,
//                      puis conservé et vidé à la fermeture du projet.
// Cycle de vie : Start Center → Nouveau / Ouvrir / Projet récent → Workspace → Fermer → Start Center.

#include <QHash>
#include <QWidget>

#include <memory>

class MainWindow;
namespace TSALab::UI { class LabWorkspaceHost; }
class QAction;
class QAbstractNativeEventFilter;
class QMenu;
class QShortcut;
class QStackedWidget;
class QVBoxLayout;

namespace TSA::UI
{

class StartCenter;
class TitleBar;

enum class ApplicationMode
{
    StartCenter,
    ProjectWorkspace
};

class AppShell : public QWidget
{
    Q_OBJECT

public:
    explicit AppShell(QWidget* parent = nullptr);
    ~AppShell() override;

    ApplicationMode mode() const { return m_mode; }

    void showStartCenter();
    void showProjectWorkspace();

    void createNewProject();
    /// Nouveau projet ouvert directement dans l'espace EXPERIMENT.
    void createNewExperiment();
    void openProject();
    /// Ouvre un projet .tsalab ou importe un modèle .tsa (projets récents, glisser-déposer, ligne de commande).
    bool openProjectFile(const QString& path);
    /// Ouvre la copie de travail d'un exemple (Documents/TSALab/Exemples), générée si absente.
    bool openExample(const QString& exampleId);
    bool closeProject();

protected:
    void closeEvent(QCloseEvent* event) override;
    void changeEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;
#ifdef _WIN32
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;
#endif

private:
    MainWindow* ensureWorkspace();
    bool isProjectOpen() const { return m_mode == ApplicationMode::ProjectWorkspace; }
    bool loadIntoWorkspace(const QString& path);
    void setMode(ApplicationMode mode);
    void updateTitle();
    void createActions();
    void bindToWorkspace(QAction* proxy, QAction* target);
    void syncProxyActions();
    void rebuildApplicationMenu();
    void restoreShellGeometry();
    void saveShellGeometry() const;

    // Cadre natif Windows sans barre de titre système (ancrage, animations, redimensionnement conservés).
    void applyNativeFrame();
    void installResizeBorderFilter();
    void updateMaximizedMargins();

private:
    ApplicationMode m_mode = ApplicationMode::StartCenter;
    QVBoxLayout* m_layout = nullptr;
    TitleBar* m_titleBar = nullptr;
    QStackedWidget* m_stack = nullptr;
    StartCenter* m_startCenter = nullptr;
    MainWindow* m_workspace = nullptr;
    TSALab::UI::LabWorkspaceHost* m_labHost = nullptr; ///< page du mode ProjectWorkspace (contient m_workspace)

    QAction* m_actNew = nullptr;
    QAction* m_actOpen = nullptr;
    QAction* m_actSave = nullptr;
    QAction* m_actSaveAs = nullptr;
    QAction* m_actUndo = nullptr;
    QAction* m_actRedo = nullptr;
    QAction* m_actCloseProject = nullptr;
    QAction* m_actTheme = nullptr;
    QAction* m_actExit = nullptr;
    QMenu* m_recentMenu = nullptr;
    QList<QShortcut*> m_startCenterShortcuts;
    QHash<QAction*, QAction*> m_proxyTargets; // action de la barre de titre → action du workspace

    bool m_nativeFrameApplied = false;
    std::unique_ptr<QAbstractNativeEventFilter> m_resizeFilter;
    bool m_wasMaximizedBeforeFullScreen = false;
};

} // namespace TSA::UI
