#include "StructuralLibrary.h"

#include "NodeHelpers.h"

namespace TSALab::Research::Structural
{

bool registerStructuralNodes(TSA::Blueprint::NodeLibrary& library)
{
    if (library.find("struct.frame_element")) return false;
    detail::registerModelNodes(library);
    detail::registerElementNodes(library);
    detail::registerAssemblyNodes(library);
    detail::registerMathNodes(library);
    detail::registerControlNodes(library);
    detail::registerDynamicsNodes(library);
    detail::registerMovingLoadNodes(library);
    return true;
}

std::vector<std::string> structuralNodeIds(const TSA::Blueprint::NodeLibrary& library)
{
    std::vector<std::string> ids;
    for (const auto* d : library.definitions())
    {
        const std::string& c = d->category;
        if (c.rfind("Structure ", 0) == 0 || c.rfind("Expérimental", 0) == 0) ids.push_back(d->id);
    }
    return ids;
}

} // namespace TSALab::Research::Structural
