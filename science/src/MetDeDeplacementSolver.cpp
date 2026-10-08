#include "tsalab/planar/PlanarSolvers.h"

#include "MddBridge.h"

#include <mdd/MetDeDeplacement.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>

namespace tsalab::planar
{

std::string MetDeDeplacementSolver::name() const
{
    return "MetDeDeplacement — méthode des déplacements";
}

std::string MetDeDeplacementSolver::version() const
{
    return mdd::kVersion;
}

Output MetDeDeplacementSolver::solve(const Input& in)
{
    Output out;
    const detail::MddBridge bridge = detail::buildMddModel(in);
    const mdd::Model& m = bridge.model;
    const auto& nodeIndex = bridge.nodeIndex;
    const auto& memberOf = bridge.memberOf;

    const mdd::Result r = mdd::solve(m);
    out.log = r.log;
    out.success = r.success;
    out.message = r.message;
    out.method = "Méthode des déplacements (forme matricielle de la méthode des rotations) — MetDeDeplacement "
                 + std::string(mdd::kVersion)
                 + (in.options.axialStiffnessFactor > 1.0 ? " ; barres quasi inextensibles (EA × 10⁴)" : " ; EA réel");
    if (!r.success) return out;
    out.equilibriumResidual = r.equilibriumResidual;

    for (const auto& n : in.nodes)
    {
        const int k = nodeIndex[n.index];
        if (k < 0) continue;
        const auto& d = r.displacements[k];
        out.displacements.push_back({ n.index, d[0], d[1], d[2] });
        if (r.supported[k])
        {
            const auto& re = r.reactions[k];
            out.reactions.push_back({ n.index, re[0], re[1], re[2] });
        }
    }
    for (std::size_t k = 0; k < r.members.size(); ++k)
        out.elementForces.push_back(detail::toContract(memberOf[k], r.members[k]));
    if (in.options.exportSystem && r.system.equations > 0)
    {
        auto& sys = out.system;
        sys.available = true;
        sys.equations = r.system.equations;
        sys.loads = r.system.loads;
        sys.displacements = r.system.displacements;
        for (const auto& e : r.system.stiffness) sys.stiffness.push_back({ e.row, e.col, e.value });
        // Nœuds de la bibliothèque (0-based) → indices du contrat (1..N)
        std::vector<int> contractIndex(m.nodes.size(), 0);
        for (std::size_t k = 1; k < nodeIndex.size(); ++k)
            if (nodeIndex[k] >= 0) contractIndex[static_cast<std::size_t>(nodeIndex[k])] = static_cast<int>(k);
        for (const auto& d : r.system.dofs)
            sys.dofs.push_back({ d[0] >= 0 ? contractIndex[static_cast<std::size_t>(d[0])] : 0, d[1] });
    }
    std::ostringstream o;
    o << "MetDeDeplacement : " << r.equations << " équation(s), demi-bande " << r.halfBandwidth << ".";
    out.log.push_back(o.str());
    return out;
}

std::vector<std::unique_ptr<ISolver>> createBuiltInSolvers()
{
    std::vector<std::unique_ptr<ISolver>> solvers;
    solvers.push_back(std::make_unique<MetDeDeplacementSolver>());
    solvers.push_back(std::make_unique<OpenSeesPlanarSolver>());
    return solvers;
}

} // namespace tsalab::planar
