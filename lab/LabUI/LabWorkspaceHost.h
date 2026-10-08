#pragma once

// Page du mode Workspace de TSALab (point d'extension AppShell::setWorkspaceDecorator de la base
// commune) : rail vertical des espaces du laboratoire autour du MainWindow commun.
//   MODÈLE     : MainWindow (modélisation, calcul, résultats, NDC) — identique à TSA ;
//   SOLVER LAB : rejeu instrumenté de la résolution K·U = F du dernier calcul (SolverLabPage).
// Les espaces s'ajoutent ici au fur et à mesure qu'ils existent réellement.

#include <QWidget>

class MainWindow;
class QButtonGroup;
class QStackedWidget;

namespace TSALab::UI
{

class SolverLabPage;

enum class LabSpace
{
    Model,
    SolverLab
};

class LabWorkspaceHost : public QWidget
{
    Q_OBJECT

public:
    LabWorkspaceHost(MainWindow* workspace, QWidget* parent = nullptr);

    void showSpace(LabSpace space);
    LabSpace currentSpace() const;

private:
    void applyTheme(bool dark);

private:
    MainWindow* m_workspace = nullptr;
    QWidget* m_rail = nullptr;
    QButtonGroup* m_buttons = nullptr;
    QStackedWidget* m_stack = nullptr;
    SolverLabPage* m_solverLab = nullptr;
};

} // namespace TSALab::UI
