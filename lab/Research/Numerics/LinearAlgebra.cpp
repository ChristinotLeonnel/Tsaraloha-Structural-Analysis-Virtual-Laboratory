#include "LinearAlgebra.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>

namespace TSALab::Research
{

namespace
{
using Clock = std::chrono::steady_clock;

double elapsedMs(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double maxDiagonal(const Matrix& a)
{
    double m = 0.0;
    for (int i = 0; i < std::min(a.rows, a.cols); ++i) m = std::max(m, std::abs(a(i, i)));
    return m;
}

void finishReport(const Matrix& K, const Vector& f, const Vector& x, SolverReport& r)
{
    const Vector Kx = LinAlg::multiply(K, x);
    const double nf = LinAlg::norm2(f);
    const double nr = LinAlg::norm2(LinAlg::subtract(Kx, f));
    r.relativeResidual = nf > 0.0 ? nr / nf : nr;
}

/// Cholesky en place (partie inférieure de L) : faux si un pivot est ≤ seuil.
bool choleskyFactor(Matrix& L, double pivotThreshold, double* minPivot, double* maxPivot, int* failed)
{
    const int n = L.rows;
    double mn = std::numeric_limits<double>::max(), mx = 0.0;
    for (int j = 0; j < n; ++j)
    {
        double d = L(j, j);
        for (int k = 0; k < j; ++k) d -= L(j, k) * L(j, k);
        mn = std::min(mn, d);
        mx = std::max(mx, d);
        if (!(d > pivotThreshold))
        {
            if (failed) *failed = j;
            if (minPivot) *minPivot = d;
            if (maxPivot) *maxPivot = mx;
            return false;
        }
        const double ljj = std::sqrt(d);
        L(j, j) = ljj;
        for (int i = j + 1; i < n; ++i)
        {
            double s = L(i, j);
            for (int k = 0; k < j; ++k) s -= L(i, k) * L(j, k);
            L(i, j) = s / ljj;
        }
        for (int i = 0; i < j; ++i) L(i, j) = 0.0; // partie supérieure nettoyée
    }
    if (minPivot) *minPivot = n ? mn : 0.0;
    if (maxPivot) *maxPivot = mx;
    return true;
}

void forwardSubstitute(const Matrix& L, Vector& b)
{
    for (int i = 0; i < L.rows; ++i)
    {
        double s = b[i];
        for (int k = 0; k < i; ++k) s -= L(i, k) * b[k];
        b[i] = s / L(i, i);
    }
}

void backSubstituteTransposed(const Matrix& L, Vector& b)
{
    for (int i = L.rows - 1; i >= 0; --i)
    {
        double s = b[i];
        for (int k = i + 1; k < L.rows; ++k) s -= L(k, i) * b[k];
        b[i] = s / L(i, i);
    }
}

SolverReport solveGauss(const Matrix& K, const Vector& f, Vector& x, const SolverSettings& s)
{
    SolverReport r;
    const int n = K.rows;
    Matrix A = K;
    Vector b = f;
    std::vector<int> perm(n);
    std::iota(perm.begin(), perm.end(), 0);
    const double threshold = s.pivotTolerance * std::max(maxDiagonal(K), std::numeric_limits<double>::min());
    r.minPivot = std::numeric_limits<double>::max();
    for (int k = 0; k < n; ++k)
    {
        int p = k;
        for (int i = k + 1; i < n; ++i)
            if (std::abs(A(i, k)) > std::abs(A(p, k))) p = i;
        const double piv = A(p, k);
        r.minPivot = std::min(r.minPivot, std::abs(piv));
        r.maxPivot = std::max(r.maxPivot, std::abs(piv));
        if (std::abs(piv) <= threshold)
        {
            r.failedEquation = k;
            r.message = "Pivot nul à l'inconnue " + std::to_string(k) + " : système singulier (mécanisme ou DDL sans rigidité).";
            return r;
        }
        if (p != k)
        {
            for (int j = 0; j < n; ++j) std::swap(A(k, j), A(p, j));
            std::swap(b[k], b[p]);
            std::swap(perm[k], perm[p]);
        }
        for (int i = k + 1; i < n; ++i)
        {
            const double m = A(i, k) / piv;
            if (m == 0.0) continue;
            A(i, k) = 0.0;
            for (int j = k + 1; j < n; ++j) A(i, j) -= m * A(k, j);
            b[i] -= m * b[k];
        }
    }
    x.assign(n, 0.0);
    for (int i = n - 1; i >= 0; --i)
    {
        double sum = b[i];
        for (int j = i + 1; j < n; ++j) sum -= A(i, j) * x[j];
        x[i] = sum / A(i, i);
    }
    r.success = true;
    r.iterations = 1;
    if (n == 0) r.minPivot = 0.0;
    return r;
}

SolverReport solveCholesky(const Matrix& K, const Vector& f, Vector& x, const SolverSettings& s)
{
    SolverReport r;
    Matrix L = K;
    const double threshold = s.pivotTolerance * std::max(maxDiagonal(K), std::numeric_limits<double>::min());
    if (!choleskyFactor(L, threshold, &r.minPivot, &r.maxPivot, &r.failedEquation))
    {
        r.message = r.minPivot < 0.0
            ? "Pivot négatif à l'équation " + std::to_string(r.failedEquation) + " : K n'est pas définie positive."
            : "Pivot nul à l'équation " + std::to_string(r.failedEquation) + " : mécanisme (structure instable) ou DDL sans rigidité.";
        return r;
    }
    x = f;
    forwardSubstitute(L, x);
    backSubstituteTransposed(L, x);
    r.success = true;
    r.iterations = 1;
    return r;
}

SolverReport solveConjugateGradient(const Matrix& K, const Vector& f, Vector& x, const SolverSettings& s)
{
    SolverReport r;
    const int n = K.rows;
    x.assign(n, 0.0);
    const double nf = LinAlg::norm2(f);
    if (nf == 0.0)
    {
        r.success = true;
        r.residualHistory.push_back(0.0);
        return r;
    }
    // Préconditionneur de Jacobi : diag(K)⁻¹ (pivots non positifs → système non SPD).
    Vector invDiag(n);
    for (int i = 0; i < n; ++i)
    {
        if (!(K(i, i) > 0.0))
        {
            r.failedEquation = i;
            r.message = "Terme diagonal non positif à l'équation " + std::to_string(i) + " : gradient conjugué impossible.";
            return r;
        }
        invDiag[i] = 1.0 / K(i, i);
    }
    Vector res = f, z(n), p(n);
    for (int i = 0; i < n; ++i) z[i] = invDiag[i] * res[i];
    p = z;
    double rz = LinAlg::dot(res, z);
    const int maxIt = s.maxIterations > 0 ? s.maxIterations : 10 * std::max(1, n);
    r.residualHistory.push_back(1.0);
    for (int it = 1; it <= maxIt; ++it)
    {
        const Vector Kp = LinAlg::multiply(K, p);
        const double pKp = LinAlg::dot(p, Kp);
        if (!(pKp > 0.0))
        {
            r.iterations = it;
            r.failedEquation = -1;
            r.message = "Direction de courbure non positive (pᵀKp ≤ 0) à l'itération " + std::to_string(it)
                      + " : K n'est pas définie positive (mécanisme).";
            return r;
        }
        const double alpha = rz / pKp;
        for (int i = 0; i < n; ++i)
        {
            x[i] += alpha * p[i];
            res[i] -= alpha * Kp[i];
        }
        const double rel = LinAlg::norm2(res) / nf;
        r.residualHistory.push_back(rel);
        r.iterations = it;
        if (rel <= s.tolerance)
        {
            r.success = true;
            return r;
        }
        for (int i = 0; i < n; ++i) z[i] = invDiag[i] * res[i];
        const double rzNew = LinAlg::dot(res, z);
        const double beta = rzNew / rz;
        rz = rzNew;
        for (int i = 0; i < n; ++i) p[i] = z[i] + beta * p[i];
    }
    r.message = "Non convergé en " + std::to_string(r.iterations) + " itérations (résidu "
              + std::to_string(r.residualHistory.back()) + ").";
    return r;
}

} // namespace

// -------------------------------------------------------------------------------------------
// LinAlg
// -------------------------------------------------------------------------------------------

Matrix LinAlg::identity(int n)
{
    Matrix m(n, n);
    for (int i = 0; i < n; ++i) m(i, i) = 1.0;
    return m;
}

Matrix LinAlg::transpose(const Matrix& a)
{
    Matrix t(a.cols, a.rows);
    for (int i = 0; i < a.rows; ++i)
        for (int j = 0; j < a.cols; ++j) t(j, i) = a(i, j);
    return t;
}

Matrix LinAlg::multiply(const Matrix& a, const Matrix& b)
{
    Matrix c(a.rows, b.cols);
    if (a.cols != b.rows) return c;
    for (int i = 0; i < a.rows; ++i)
        for (int k = 0; k < a.cols; ++k)
        {
            const double aik = a(i, k);
            if (aik == 0.0) continue;
            for (int j = 0; j < b.cols; ++j) c(i, j) += aik * b(k, j);
        }
    return c;
}

Vector LinAlg::multiply(const Matrix& a, const Vector& x)
{
    Vector y(static_cast<std::size_t>(a.rows), 0.0);
    if (static_cast<int>(x.size()) != a.cols) return y;
    for (int i = 0; i < a.rows; ++i)
    {
        double s = 0.0;
        for (int j = 0; j < a.cols; ++j) s += a(i, j) * x[j];
        y[i] = s;
    }
    return y;
}

Matrix LinAlg::congruence(const Matrix& a, const Matrix& b)
{
    return multiply(transpose(a), multiply(b, a));
}

Matrix LinAlg::add(const Matrix& a, const Matrix& b, double factorB)
{
    Matrix c = a;
    if (a.rows != b.rows || a.cols != b.cols) return c;
    for (std::size_t k = 0; k < c.data.size(); ++k) c.data[k] += factorB * b.data[k];
    return c;
}

Matrix LinAlg::scaled(const Matrix& a, double s)
{
    Matrix c = a;
    for (double& v : c.data) v *= s;
    return c;
}

double LinAlg::dot(const Vector& a, const Vector& b)
{
    double s = 0.0;
    for (std::size_t i = 0; i < std::min(a.size(), b.size()); ++i) s += a[i] * b[i];
    return s;
}

double LinAlg::norm2(const Vector& a)
{
    return std::sqrt(dot(a, a));
}

double LinAlg::normInf(const Vector& a)
{
    double m = 0.0;
    for (double v : a) m = std::max(m, std::abs(v));
    return m;
}

Vector LinAlg::subtract(const Vector& a, const Vector& b)
{
    Vector c(a.size(), 0.0);
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i) c[i] = a[i] - b[i];
    return c;
}

double LinAlg::maxAbs(const Matrix& a)
{
    double m = 0.0;
    for (double v : a.data) m = std::max(m, std::abs(v));
    return m;
}

double LinAlg::asymmetry(const Matrix& a)
{
    if (a.rows != a.cols) return std::numeric_limits<double>::infinity();
    const double ref = maxAbs(a);
    if (ref == 0.0) return 0.0;
    double m = 0.0;
    for (int i = 0; i < a.rows; ++i)
        for (int j = i + 1; j < a.cols; ++j) m = std::max(m, std::abs(a(i, j) - a(j, i)));
    return m / ref;
}

int LinAlg::nonZeroCount(const Matrix& a, double tolerance)
{
    int n = 0;
    for (double v : a.data)
        if (std::abs(v) > tolerance) ++n;
    return n;
}

// -------------------------------------------------------------------------------------------
// Résolution
// -------------------------------------------------------------------------------------------

const char* solverMethodId(SolverMethod m)
{
    switch (m)
    {
    case SolverMethod::GaussLU: return "lu";
    case SolverMethod::Cholesky: return "cholesky";
    case SolverMethod::ConjugateGradient: return "cg";
    }
    return "cholesky";
}

const char* solverMethodName(SolverMethod m)
{
    switch (m)
    {
    case SolverMethod::GaussLU: return "Gauss (LU, pivot partiel)";
    case SolverMethod::Cholesky: return "Cholesky (L·Lᵀ)";
    case SolverMethod::ConjugateGradient: return "Gradient conjugué (Jacobi)";
    }
    return "Cholesky (L·Lᵀ)";
}

SolverMethod solverMethodFromId(const std::string& id, SolverMethod fallback)
{
    if (id == "lu") return SolverMethod::GaussLU;
    if (id == "cholesky") return SolverMethod::Cholesky;
    if (id == "cg") return SolverMethod::ConjugateGradient;
    return fallback;
}

SolverReport solve(const Matrix& K, const Vector& f, Vector& x, const SolverSettings& settings)
{
    const auto start = Clock::now();
    SolverReport r;
    if (K.rows != K.cols || static_cast<int>(f.size()) != K.rows)
    {
        r.method = settings.method;
        r.message = "Dimensions incohérentes entre K et f.";
        return r;
    }
    switch (settings.method)
    {
    case SolverMethod::GaussLU: r = solveGauss(K, f, x, settings); break;
    case SolverMethod::Cholesky: r = solveCholesky(K, f, x, settings); break;
    case SolverMethod::ConjugateGradient: r = solveConjugateGradient(K, f, x, settings); break;
    }
    r.method = settings.method;
    r.equations = K.rows;
    if (r.success)
    {
        finishReport(K, f, x, r);
        if (settings.method != SolverMethod::ConjugateGradient) r.residualHistory = { r.relativeResidual };
        if (r.message.empty()) r.message = "Résolu.";
    }
    else
    {
        x.assign(static_cast<std::size_t>(K.rows), 0.0);
    }
    r.elapsedMs = elapsedMs(start);
    return r;
}

// -------------------------------------------------------------------------------------------
// Valeurs propres
// -------------------------------------------------------------------------------------------

EigenResult symmetricEigen(const Matrix& input, double tolerance, int maxSweeps)
{
    EigenResult res;
    const int n = input.rows;
    if (n != input.cols)
    {
        res.message = "Matrice non carrée.";
        return res;
    }
    Matrix a = input;
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j) a(i, j) = a(j, i) = 0.5 * (a(i, j) + a(j, i));
    Matrix v = LinAlg::identity(n);

    double total = 0.0;
    for (double x : a.data) total += x * x;
    const double target = tolerance * tolerance * std::max(total, std::numeric_limits<double>::min());

    for (int sweep = 0; sweep < maxSweeps; ++sweep)
    {
        double off = 0.0;
        for (int i = 0; i < n; ++i)
            for (int j = i + 1; j < n; ++j) off += 2.0 * a(i, j) * a(i, j);
        res.sweeps = sweep;
        if (off <= target) break;

        for (int p = 0; p < n; ++p)
            for (int q = p + 1; q < n; ++q)
            {
                const double apq = a(p, q);
                if (std::abs(apq) <= std::numeric_limits<double>::min()) continue;
                const double theta = (a(q, q) - a(p, p)) / (2.0 * apq);
                const double t = (theta >= 0.0 ? 1.0 : -1.0) / (std::abs(theta) + std::sqrt(theta * theta + 1.0));
                const double c = 1.0 / std::sqrt(t * t + 1.0), s = t * c;
                for (int k = 0; k < n; ++k)
                {
                    const double akp = a(k, p), akq = a(k, q);
                    a(k, p) = c * akp - s * akq;
                    a(k, q) = s * akp + c * akq;
                }
                for (int k = 0; k < n; ++k)
                {
                    const double apk = a(p, k), aqk = a(q, k);
                    a(p, k) = c * apk - s * aqk;
                    a(q, k) = s * apk + c * aqk;
                }
                for (int k = 0; k < n; ++k)
                {
                    const double vkp = v(k, p), vkq = v(k, q);
                    v(k, p) = c * vkp - s * vkq;
                    v(k, q) = s * vkp + c * vkq;
                }
            }
        res.sweeps = sweep + 1;
    }

    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int i, int j) { return a(i, i) < a(j, j); });
    res.values.resize(n);
    res.vectors = Matrix(n, n);
    for (int k = 0; k < n; ++k)
    {
        res.values[k] = a(order[k], order[k]);
        for (int i = 0; i < n; ++i) res.vectors(i, k) = v(i, order[k]);
    }
    res.success = true;
    return res;
}

EigenResult generalizedEigen(const Matrix& K, const Matrix& M, int maxModes)
{
    EigenResult res;
    const int n = K.rows;
    if (n == 0 || K.cols != n || M.rows != n || M.cols != n)
    {
        res.message = "Dimensions incohérentes entre K et M.";
        return res;
    }
    Matrix L = K;
    int failed = -1;
    double minPivot = 0.0, maxPivot = 0.0;
    if (!choleskyFactor(L, 1e-11 * std::max(maxDiagonal(K), std::numeric_limits<double>::min()), &minPivot, &maxPivot, &failed))
    {
        res.message = "K n'est pas définie positive (équation " + std::to_string(failed) + ") : analyse modale impossible.";
        return res;
    }
    // Y = L⁻¹·M (colonne par colonne), puis A = L⁻¹·Yᵀ = L⁻¹·M·L⁻ᵀ (M symétrique).
    Matrix Y(n, n), A(n, n);
    Vector col(n);
    for (int j = 0; j < n; ++j)
    {
        for (int i = 0; i < n; ++i) col[i] = M(i, j);
        forwardSubstitute(L, col);
        for (int i = 0; i < n; ++i) Y(i, j) = col[i];
    }
    for (int j = 0; j < n; ++j)
    {
        for (int i = 0; i < n; ++i) col[i] = Y(j, i);
        forwardSubstitute(L, col);
        for (int i = 0; i < n; ++i) A(i, j) = col[i];
    }
    EigenResult inner = symmetricEigen(A);
    if (!inner.success)
    {
        res.message = inner.message;
        return res;
    }
    // μ = 1/λ : les plus grands μ donnent les plus basses fréquences. μ ≈ 0 : DDL sans masse.
    const double muMax = inner.values.empty() ? 0.0 : inner.values.back();
    const double muFloor = 1e-12 * std::max(muMax, std::numeric_limits<double>::min());
    std::vector<int> kept;
    for (int k = n - 1; k >= 0; --k)
        if (inner.values[k] > muFloor) kept.push_back(k);
    if (maxModes > 0 && static_cast<int>(kept.size()) > maxModes) kept.resize(maxModes);

    res.values.resize(kept.size());
    res.vectors = Matrix(n, static_cast<int>(kept.size()));
    for (std::size_t m = 0; m < kept.size(); ++m)
    {
        const int k = kept[m];
        const double mu = inner.values[k];
        res.values[m] = 1.0 / mu;
        for (int i = 0; i < n; ++i) col[i] = inner.vectors(i, k);
        backSubstituteTransposed(L, col);            // φ = L⁻ᵀ·ψ
        const double scale = 1.0 / std::sqrt(mu);    // φᵀ·M·φ = μ → normalisation à 1
        for (int i = 0; i < n; ++i) res.vectors(i, static_cast<int>(m)) = col[i] * scale;
    }
    res.sweeps = inner.sweeps;
    res.success = true;
    if (kept.empty()) res.message = "Aucun mode : matrice de masse nulle.";
    return res;
}

double conditionNumber(const Matrix& a)
{
    const EigenResult e = symmetricEigen(a, 1e-10, 60);
    if (!e.success || e.values.empty()) return std::numeric_limits<double>::infinity();
    const double lo = e.values.front(), hi = e.values.back();
    if (!(lo > 0.0)) return std::numeric_limits<double>::infinity();
    return hi / lo;
}

} // namespace TSALab::Research
