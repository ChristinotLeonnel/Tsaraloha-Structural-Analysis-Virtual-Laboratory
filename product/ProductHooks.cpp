// Points d'extension du produit TSALab (voir TSA/src/App/ProductHooks.h) : la fenêtre commune reçoit
// la colonne « laboratoire » du Start Center et le rail des espaces autour du workspace.
#include "App/ProductHooks.h"

#include "App/ProductInfo.h"
#include "IO/TSAFile.h"
#include "LabUI/LabStartPanel.h"
#include "LabUI/LabWorkspaceHost.h"
#include "Model/Model.h"
#include "Research/Examples/ExampleModels.h"
#include "UI/Home/StartCenter.h"
#include "UI/Shell/AppShell.h"

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>

namespace
{

/// Ouvre la copie de travail d'un exemple (Documents/TSALab/Exemples), générée au premier usage puis
/// rouverte telle que l'utilisateur l'a laissée.
bool openExample(TSA::UI::AppShell& shell, const QString& exampleId)
{
    namespace Examples = TSALab::Research::Examples;
    const auto& catalog = Examples::catalog();
    const auto it = std::find_if(catalog.begin(), catalog.end(),
                                 [&](const auto& e) { return QString::fromStdString(e.id) == exampleId; });
    if (it == catalog.end()) return false;

    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation))
                            .filePath(QStringLiteral("%1/Exemples").arg(TSA::Product::kDocumentsFolder));
    const QString title = QString::fromStdString(it->title);
    QString fileName = title;
    static const QRegularExpression invalidChars(QStringLiteral(R"([<>:"/\\|?*])"));
    fileName.replace(invalidChars, QStringLiteral("-"));
    const QString path = QDir(dir).filePath(fileName + TSA::Product::projectExtension());

    if (!QFileInfo::exists(path))
    {
        QString error;
        TSA::Model::Model model;
        if (!QDir().mkpath(dir) || !Examples::build(exampleId.toStdString(), model)
            || !TSA::IO::TSAProjectIO::saveProject(path, model, nullptr, title, QString(), true, QImage(), &error))
        {
            QMessageBox::warning(&shell, QObject::tr("Exemple"),
                                 QObject::tr("Impossible de créer l'exemple « %1 » :\n%2").arg(title, error));
            return false;
        }
    }
    return shell.openProjectFile(path);
}

} // namespace

namespace TSA::Product
{

void configureShell(TSA::UI::AppShell& shell)
{
    auto* panel = new TSALab::UI::LabStartPanel;
    shell.startCenter()->setLaunchPanel(panel);
    QObject::connect(panel, &TSALab::UI::LabStartPanel::newModelRequested, &shell, &TSA::UI::AppShell::createNewProject);
    QObject::connect(panel, &TSALab::UI::LabStartPanel::openRequested, &shell, &TSA::UI::AppShell::openProject);
    QObject::connect(panel, &TSALab::UI::LabStartPanel::exampleRequested, &shell,
                     [&shell](const QString& id) { openExample(shell, id); });

    shell.setWorkspaceDecorator([](MainWindow* workspace, QWidget* parent) -> QWidget* {
        return new TSALab::UI::LabWorkspaceHost(workspace, parent);
    });
}

} // namespace TSA::Product
