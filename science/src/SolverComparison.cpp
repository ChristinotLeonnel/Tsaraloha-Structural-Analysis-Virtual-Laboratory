#include "tsalab/numerics/SolverComparison.h"

#include <algorithm>
#include <cmath>

namespace tsalab::numerics
{

LinearProblem makeProblem(Matrix K, Vector reference)
{
    LinearProblem p;
    p.f = LinAlg::multiply(K, reference);
    p.K = std::move(K);
    p.reference = std::move(reference);
    for (int e = 0; e < p.K.rows; ++e) p.labels.push_back("eq" + std::to_string(e));
    return p;
}

SolverRun runSolver(const LinearProblem& problem, SolverMethod method, const SolverSettings& base)
{
    SolverRun run;
    SolverSettings settings = base;
    settings.method = method;
    run.report = solve(problem.K, problem.f, run.x, settings);
    if (!run.report.success || problem.reference.empty()) return run;

    double maxRef = 0.0, maxDiff = 0.0;
    for (std::size_t i = 0; i < run.x.size() && i < problem.reference.size(); ++i)
    {
        maxRef = std::max(maxRef, std::abs(problem.reference[i]));
        maxDiff = std::max(maxDiff, std::abs(run.x[i] - problem.reference[i]));
    }
    run.deviationFromReference = maxRef > 0.0 ? maxDiff / maxRef : maxDiff;
    return run;
}

} // namespace tsalab::numerics
