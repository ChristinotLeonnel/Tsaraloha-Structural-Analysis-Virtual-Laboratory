#include "SolverExperiment.h"

#include "Analysis/ResultsModel.h"

#include <algorithm>
#include <cmath>

namespace TSALab::Research
{

bool buildSolverExperiment(const TSA::Analysis::ResultsModel& results, SolverExperimentInput& out, std::string* why)
{
    auto fail = [why](const std::string& reason) {
        if (why) *why = reason;
        return false;
    };

    const auto& adv = results.advanced();
    if (!results.hasResults()) return fail("Aucun résultat de calcul valide : lancer un calcul (F5).");
    if (!adv.available || !adv.hasGlobalStiffness)
    {
        std::string reason = "La matrice de rigidité globale n'a pas été extraite de ce calcul. "
                             "Relancer le calcul OpenSees en extraction ADVANCED (fenêtre Analysis).";
        if (!adv.kGlobalUnavailableReason.empty()) reason += " Motif : " + adv.kGlobalUnavailableReason;
        return fail(reason);
    }

    const int n = adv.kGlobal.rows;
    if (n <= 0 || adv.kGlobal.cols != n) return fail("Matrice globale vide ou non carrée.");
    if (adv.dofMap.equationCount() != n)
        return fail("Numérotation des DDL incohérente avec la matrice globale (" + std::to_string(adv.dofMap.equationCount())
                    + " équations pour une matrice " + std::to_string(n) + "×" + std::to_string(n) + ").");
    if (n > kMaxDenseEquations)
        return fail("Système de " + std::to_string(n) + " équations : au-delà de " + std::to_string(kMaxDenseEquations)
                    + ", le laboratoire (matrices denses) ne traite pas le modèle complet.");

    SolverExperimentInput in;
    in.K = Matrix(n, n);
    for (std::size_t k = 0; k < adv.kGlobal.values.size(); ++k)
        in.K(adv.kGlobal.rowIndex[k], adv.kGlobal.colIndex[k]) = adv.kGlobal.values[k];
    in.reference = adv.globalDisplacementVector(results.allDisplacements());
    in.f = LinAlg::multiply(in.K, in.reference);
    in.labels.reserve(static_cast<std::size_t>(n));
    for (int e = 0; e < n; ++e) in.labels.push_back(adv.dofMap.equationLabel(e));
    in.caseName = results.caseOrComboName();
    in.units = adv.kGlobalMeta.units;
    out = std::move(in);
    return true;
}

SolverExperimentInput makeSolverExperiment(Matrix K, Vector reference)
{
    SolverExperimentInput in;
    in.f = LinAlg::multiply(K, reference);
    in.K = std::move(K);
    in.reference = std::move(reference);
    for (int e = 0; e < in.K.rows; ++e) in.labels.push_back("eq" + std::to_string(e));
    return in;
}

SolverRun runSolver(const SolverExperimentInput& input, SolverMethod method, const SolverSettings& base)
{
    SolverRun run;
    SolverSettings settings = base;
    settings.method = method;
    run.report = solve(input.K, input.f, run.x, settings);
    if (!run.report.success) return run;

    double maxRef = 0.0, maxDiff = 0.0;
    for (std::size_t i = 0; i < run.x.size() && i < input.reference.size(); ++i)
    {
        maxRef = std::max(maxRef, std::abs(input.reference[i]));
        maxDiff = std::max(maxDiff, std::abs(run.x[i] - input.reference[i]));
    }
    run.deviationFromReference = maxRef > 0.0 ? maxDiff / maxRef : maxDiff;
    return run;
}

} // namespace TSALab::Research
