// TSALab — Tsaraloha Structural Analysis Laboratory : point d'entrée.
// Initialisation commune de l'écosystème (EcosystemApplication, partagée avec TSA) + fenêtre IDE.
#include "LabMainWindow.h"

#include "Blueprint/BlueprintRuntime.h"
#include "LabUI/StructuralViewers.h"
#include "Research/Structural/StructuralLibrary.h"
#include "UI/Common/EcosystemApplication.h"

int main(int argc, char* argv[])
{
    TSA::UI::EcosystemApplication app(argc, argv);
    if (!app.registerPlatformIntegration()) return 0;

    // TSALab Blueprint — programmation structurelle : blocs pré-codés et vues de leurs objets, avant la fenêtre
    // (palette de l'éditeur) et avant toute exécution de graphe.
    TSALab::Research::Structural::registerStructuralNodes(TSA::Blueprint::NodeLibrary::global());
    TSALab::UI::registerStructuralViewers();

    TSALab::UI::LabMainWindow window;
    window.show();
    const QStringList files = app.projectFilesFromArguments();
    if (!files.isEmpty()) window.openProjectFile(files.first());
    return app.exec();
}
