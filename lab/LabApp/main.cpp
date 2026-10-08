// TSALab — Tsaraloha Structural Analysis Laboratory : point d'entrée.
// Initialisation commune de l'écosystème (EcosystemApplication, partagée avec TSA) + fenêtre IDE.
#include "LabMainWindow.h"

#include "UI/Common/EcosystemApplication.h"

int main(int argc, char* argv[])
{
    TSA::UI::EcosystemApplication app(argc, argv);
    if (!app.registerPlatformIntegration()) return 0;

    TSALab::UI::LabMainWindow window;
    window.show();
    const QStringList files = app.projectFilesFromArguments();
    if (!files.isEmpty()) window.openProjectFile(files.first());
    return app.exec();
}
