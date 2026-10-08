#include "LabWorkspaceHost.h"

#include "SolverLabPage.h"

#include "UI/MainWindow.h"
#include "UI/Theme/ThemeManager.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QToolButton>
#include <QVBoxLayout>

namespace TSALab::UI
{

namespace
{
struct SpaceInfo
{
    LabSpace space;
    const char* label;
    const char* tooltip;
};

constexpr SpaceInfo kSpaces[] = {
    { LabSpace::Model, "MODÈLE", "Modélisation, calcul, résultats et note de calcul (base commune avec TSA)" },
    { LabSpace::SolverLab, "SOLVER\nLAB", "Rejouer la résolution K·U = F du dernier calcul avec les solveurs du laboratoire" },
};
} // namespace

LabWorkspaceHost::LabWorkspaceHost(MainWindow* workspace, QWidget* parent)
    : QWidget(parent)
    , m_workspace(workspace)
{
    setObjectName("LabWorkspaceHost");
    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_rail = new QWidget(this);
    m_rail->setObjectName("LabRail");
    m_rail->setFixedWidth(76);
    auto* railLay = new QVBoxLayout(m_rail);
    railLay->setContentsMargins(6, 10, 6, 10);
    railLay->setSpacing(6);
    m_buttons = new QButtonGroup(this);
    m_buttons->setExclusive(true);
    for (const SpaceInfo& s : kSpaces)
    {
        auto* b = new QToolButton(m_rail);
        b->setText(tr(s.label));
        b->setToolTip(tr(s.tooltip));
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        b->setToolButtonStyle(Qt::ToolButtonTextOnly);
        b->setFixedSize(64, 52);
        m_buttons->addButton(b, int(s.space));
        railLay->addWidget(b);
    }
    railLay->addStretch();
    lay->addWidget(m_rail);

    m_stack = new QStackedWidget(this);
    lay->addWidget(m_stack, 1);
    workspace->setParent(m_stack);
    m_stack->addWidget(workspace);                 // index LabSpace::Model
    m_solverLab = new SolverLabPage(m_stack);
    m_stack->addWidget(m_solverLab);               // index LabSpace::SolverLab

    connect(m_buttons, &QButtonGroup::idClicked, this, [this](int id) { showSpace(LabSpace(id)); });
    connect(workspace, &MainWindow::resultsChanged, this, [this] { m_solverLab->setResults(m_workspace->resultsModel()); });
    m_solverLab->setResults(workspace->resultsModel());

    connect(&TSA::UI::ThemeManager::instance(), &TSA::UI::ThemeManager::themeChanged, this, &LabWorkspaceHost::applyTheme);
    applyTheme(TSA::UI::ThemeManager::instance().isDarkMode());
    showSpace(LabSpace::Model);
}

void LabWorkspaceHost::showSpace(LabSpace space)
{
    m_stack->setCurrentIndex(int(space));
    if (auto* b = m_buttons->button(int(space))) b->setChecked(true);
}

LabSpace LabWorkspaceHost::currentSpace() const
{
    return LabSpace(m_stack->currentIndex());
}

void LabWorkspaceHost::applyTheme(bool dark)
{
    const QString bg = dark ? "#161B22" : "#F3F1FA";
    const QString border = dark ? "#30363D" : "#D0D7DE";
    const QString accent = dark ? "#7C4DFF" : "#6236E0";
    const QString hover = dark ? "#21262D" : "#E6E1F7";
    m_rail->setStyleSheet(QStringLiteral(
        "#LabRail { background: %1; border-right: 1px solid %2; }"
        "QToolButton { border: none; border-left: 3px solid transparent; border-radius: 0;"
        "   font-size: 10px; font-weight: 700; letter-spacing: 1px; }"
        "QToolButton:hover { background: %4; }"
        "QToolButton:checked { border-left: 3px solid %3; color: %3; background: %4; }")
        .arg(bg, border, accent, hover));
}

} // namespace TSALab::UI
