// tsalab-bench : exécute le banc de validation TSALab sur tous les solveurs d'ossatures planes.
// Application console sans Qt (cœur scientifique seul) ; code de sortie 1 si un benchmark échoue.
#include "tsalab/planar/PlanarSolvers.h"
#include "tsalab/validation/PlanarBenchmarks.h"

#include <iostream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

int main()
{
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    std::vector<tsalab::validation::BenchmarkReport> reports;
    for (const auto& solver : tsalab::planar::createBuiltInSolvers())
        for (const auto& b : tsalab::validation::planarBenchmarks())
            reports.push_back(tsalab::validation::runBenchmark(b, *solver));
    std::cout << "TSALab — banc de validation des solveurs d'ossatures planes\n\n"
              << tsalab::validation::formatReport(reports);
    for (const auto& r : reports)
        if (!r.passed) return 1;
    return 0;
}
