#pragma once

// TSALab — matrices du cœur scientifique (C++ pur).

#include <cstddef>
#include <vector>

namespace tsalab::numerics
{

/// Matrice dense, stockage ligne par ligne.
struct DenseMatrix
{
    int rows = 0;
    int cols = 0;
    std::vector<double> data;

    DenseMatrix() = default;
    DenseMatrix(int r, int c) : rows(r), cols(c), data(static_cast<std::size_t>(r) * c, 0.0) {}

    bool empty() const { return rows == 0 || cols == 0; }
    double& operator()(int i, int j) { return data[static_cast<std::size_t>(i) * cols + j]; }
    double operator()(int i, int j) const { return data[static_cast<std::size_t>(i) * cols + j]; }
};

using Vector = std::vector<double>;

} // namespace tsalab::numerics
