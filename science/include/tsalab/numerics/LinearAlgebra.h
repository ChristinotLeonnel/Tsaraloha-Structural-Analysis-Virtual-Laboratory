#pragma once

// TSALab — algèbre linéaire dense du laboratoire (cœur scientifique, C++ pur).
//
// Outils volontairement simples, lisibles et instrumentés : le laboratoire montre ce que fait un
// solveur (pivots, résidus, itérations, conditionnement), il ne cherche pas la performance d'un
// solveur de production (OpenSees reste le moteur des grands modèles).
// Stockage : tsalab::numerics::DenseMatrix (ligne par ligne).

#include "tsalab/numerics/Matrix.h"

#include <string>
#include <vector>

namespace tsalab::numerics
{

using Matrix = DenseMatrix;

namespace LinAlg
{
Matrix identity(int n);
Matrix transpose(const Matrix& a);
Matrix multiply(const Matrix& a, const Matrix& b);
Vector multiply(const Matrix& a, const Vector& x);
/// Aᵀ · B · A (changement de repère d'une matrice de rigidité ou de masse).
Matrix congruence(const Matrix& a, const Matrix& b);
Matrix add(const Matrix& a, const Matrix& b, double factorB = 1.0);
Matrix scaled(const Matrix& a, double s);

double dot(const Vector& a, const Vector& b);
double norm2(const Vector& a);
double normInf(const Vector& a);
Vector subtract(const Vector& a, const Vector& b);

double maxAbs(const Matrix& a);
/// Écart maximal |A − Aᵀ| rapporté au plus grand coefficient (0 = symétrique).
double asymmetry(const Matrix& a);
int nonZeroCount(const Matrix& a, double tolerance = 0.0);
} // namespace LinAlg

// -------------------------------------------------------------------------------------------
// Résolution de K·x = f
// -------------------------------------------------------------------------------------------

enum class SolverMethod
{
    GaussLU,            ///< élimination de Gauss, pivot partiel (matrice quelconque)
    Cholesky,           ///< K = L·Lᵀ (symétrique définie positive : pivots > 0 ⇔ structure stable)
    ConjugateGradient   ///< gradient conjugué préconditionné (Jacobi), itératif
};

const char* solverMethodId(SolverMethod m);     ///< "lu", "cholesky", "cg" (persistance)
const char* solverMethodName(SolverMethod m);   ///< libellé affiché
SolverMethod solverMethodFromId(const std::string& id, SolverMethod fallback = SolverMethod::Cholesky);

struct SolverSettings
{
    SolverMethod method = SolverMethod::Cholesky;
    double tolerance = 1e-10;        ///< CG : ‖r‖/‖f‖ visé
    int maxIterations = 0;           ///< CG : 0 = 10·n
    /// Pivot jugé nul si |pivot| ≤ pivotTolerance · max|diag(K)| : mécanisme / DDL non retenu.
    double pivotTolerance = 1e-11;
};

struct SolverReport
{
    bool success = false;
    std::string message;
    SolverMethod method = SolverMethod::Cholesky;
    int equations = 0;
    int iterations = 0;                  ///< CG ; 1 pour les méthodes directes
    std::vector<double> residualHistory; ///< CG : ‖r_k‖/‖f‖ ; direct : résidu final seul
    double relativeResidual = 0.0;       ///< ‖K·x − f‖ / ‖f‖ recalculé après résolution
    double minPivot = 0.0;               ///< direct : plus petit pivot (en valeur absolue)
    double maxPivot = 0.0;
    int failedEquation = -1;             ///< équation au pivot nul / négatif (mécanisme)
    double elapsedMs = 0.0;
};

/// Résout K·x = f selon la méthode demandée (K n'est pas modifiée).
SolverReport solve(const Matrix& K, const Vector& f, Vector& x, const SolverSettings& settings);

// -------------------------------------------------------------------------------------------
// Valeurs propres
// -------------------------------------------------------------------------------------------

struct EigenResult
{
    bool success = false;
    std::string message;
    Vector values;           ///< croissantes
    Matrix vectors;          ///< colonne k = vecteur propre de values[k]
    int sweeps = 0;
};

/// Valeurs et vecteurs propres d'une matrice symétrique (méthode de Jacobi cyclique).
EigenResult symmetricEigen(const Matrix& a, double tolerance = 1e-12, int maxSweeps = 100);

/// Problème généralisé K·φ = λ·M·φ (K symétrique définie positive, M symétrique semi-définie
/// positive : masses nulles admises). Réduction par Cholesky de K : A = L⁻¹·M·L⁻ᵀ, μ = 1/λ.
/// Les modes de masse nulle (μ = 0) sont écartés. Vecteurs normalisés par rapport à M (φᵀMφ = 1).
EigenResult generalizedEigen(const Matrix& K, const Matrix& M, int maxModes = 0);

/// Conditionnement spectral λmax/λmin d'une matrice symétrique définie positive (∞ si singulière).
double conditionNumber(const Matrix& a);

} // namespace tsalab::numerics
