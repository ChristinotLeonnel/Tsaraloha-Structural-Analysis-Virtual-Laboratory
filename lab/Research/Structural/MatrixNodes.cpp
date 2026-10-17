// Catégorie D — mathématiques : matrices et vecteurs (saisie, opérations, extraction, résolution, LU, inversion
// contrôlée, résidu, conditionnement, symétrie, contrôle des dimensions, export CSV).

#include "NodeHelpers.h"

#include <QDir>
#include <QFileInfo>
#include <QSaveFile>

#include <cmath>
#include <limits>
#include <sstream>

namespace TSALab::Research::Structural::detail
{

namespace
{
namespace LA = tsalab::numerics::LinAlg;

std::string dims(const Matrix& m) { return std::to_string(m.rows) + "×" + std::to_string(m.cols); }

std::vector<std::string> pick(const std::vector<std::string>& labels, const std::vector<int>& idx)
{
    std::vector<std::string> out;
    if (labels.empty()) return out;
    for (int i : idx) out.push_back(i >= 0 && i < static_cast<int>(labels.size()) ? labels[static_cast<std::size_t>(i)] : std::to_string(i));
    return out;
}

bool sameSize(ExecutionContext& c, const Matrix& a, const Matrix& b, const char* op)
{
    if (a.rows == b.rows && a.cols == b.cols) return true;
    return c.fail(std::string("dimensions incompatibles pour ") + op + " : A est " + dims(a) + ", B est " + dims(b));
}

bool indexIn(ExecutionContext& c, long long i, int size, const char* what)
{
    if (i >= 0 && i < size) return true;
    return c.fail(std::string(what) + " " + std::to_string(i) + " hors de [0 ; " + std::to_string(size) + "[ (indices à partir de 0)");
}

/// Indices d'une liste (IdList) contrôlés.
bool indices(ExecutionContext& c, const char* pin, int size, std::vector<int>& out)
{
    const Value v = c.input(pin);
    if (c.failed()) return false;
    if (const auto* ids = std::get_if<std::vector<int>>(&v)) out = *ids;
    else if (const auto* i = std::get_if<long long>(&v)) out = { static_cast<int>(*i) };
    else return c.fail(std::string("entrée « ") + pin + " » : liste d'indices attendue (« 0,1,2 »)");
    if (out.empty()) return c.fail(std::string("entrée « ") + pin + " » : liste d'indices vide");
    for (int i : out)
        if (!indexIn(c, i, size, "indice")) return false;
    return true;
}

/// P·A = L·U (Doolittle, pivot partiel). Faux si un pivot est nul (matrice singulière).
bool luDecompose(const Matrix& A, Matrix& L, Matrix& U, Matrix& P, int& failed, double& minPivot)
{
    const int n = A.rows;
    U = A;
    L = LA::identity(n);
    P = LA::identity(n);
    failed = -1;
    minPivot = std::numeric_limits<double>::infinity();
    double scale = 0.0;
    for (double v : A.data) scale = std::max(scale, std::abs(v));
    const double tiny = 1e-14 * std::max(scale, std::numeric_limits<double>::min());
    for (int k = 0; k < n; ++k)
    {
        int p = k;
        for (int i = k + 1; i < n; ++i)
            if (std::abs(U(i, k)) > std::abs(U(p, k))) p = i;
        if (std::abs(U(p, k)) <= tiny)
        {
            failed = k;
            minPivot = 0.0;
            return false;
        }
        if (p != k)
        {
            for (int j = 0; j < n; ++j)
            {
                std::swap(U(k, j), U(p, j));
                std::swap(P(k, j), P(p, j));
            }
            for (int j = 0; j < k; ++j) std::swap(L(k, j), L(p, j));
        }
        minPivot = std::min(minPivot, std::abs(U(k, k)));
        for (int i = k + 1; i < n; ++i)
        {
            const double f = U(i, k) / U(k, k);
            L(i, k) = f;
            for (int j = k; j < n; ++j) U(i, j) -= f * U(k, j);
            U(i, k) = 0.0;
        }
    }
    return true;
}

double normInfMatrix(const Matrix& a)
{
    double best = 0.0;
    for (int i = 0; i < a.rows; ++i)
    {
        double row = 0.0;
        for (int j = 0; j < a.cols; ++j) row += std::abs(a(i, j));
        best = std::max(best, row);
    }
    return best;
}

bool writeCsv(ExecutionContext& c, const std::string& path, const std::string& content)
{
    if (path.empty()) return c.fail("chemin du fichier CSV vide");
    QSaveFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::WriteOnly) || f.write(QByteArray::fromStdString(content)) < 0 || !f.commit())
        return c.fail("écriture impossible : " + path + " (" + f.errorString().toStdString() + ")");
    c.log("Fichier écrit : " + QFileInfo(QString::fromStdString(path)).absoluteFilePath().toStdString());
    return true;
}

std::string csvValue(double v)
{
    std::ostringstream o;
    o.precision(17);
    o << v;
    return o.str();
}
} // namespace

void registerMathNodes(NodeLibrary& lib)
{
    const std::string idx = "Indices à partir de 0 (comme la numérotation des DDL : DDL 3k, 3k + 1, 3k + 2 du k-ième nœud).";

    // --- Saisie et construction --------------------------------------------------------------------
    lib.add(pureBlock("matrix.from_text", "Matrice (saisie)", kMath, "Matrice saisie : lignes séparées par « ; », coefficients par des espaces.",
                      { data("text", "Coefficients", VT::Text, std::string("4 1 ; 1 3")), data("title", "Titre", VT::Text, std::string()) },
                      { object("M", "Matrice", Type::Matrix) },
                      "Exemple « 4 1 ; 1 3 » → [[4, 1], [1, 3]]. Point décimal ; virgule ou espace entre coefficients. Refus : lignes de longueurs différentes.",
                      "TSALab/lab/Research/Structural/MatrixNodes.cpp"),
            [](ExecutionContext& c) {
                Matrix m;
                std::string error;
                const std::string text = c.text("text"), title = c.text("title");
                if (c.failed()) return false;
                if (!parseMatrix(text, m, &error)) return c.fail(error);
                c.setOutput("M", matrixOut(std::move(m), {}, {}, title));
                return true;
            });
    lib.add(pureBlock("matrix.zeros", "Matrice nulle", kMath, "Matrice de zéros (lignes × colonnes) : point de départ d'un assemblage.",
                      { data("rows", "Lignes", VT::Integer, 3LL), data("cols", "Colonnes", VT::Integer, 3LL) }, { object("M", "Matrice", Type::Matrix) },
                      "Dimensions de 1 à 2000.", "TSALab/lab/Research/Structural/MatrixNodes.cpp"),
            [](ExecutionContext& c) {
                const long long r = c.integer("rows"), k = c.integer("cols");
                if (c.failed()) return false;
                if (r < 1 || k < 1 || r > 2000 || k > 2000) return c.fail("dimensions hors de [1 ; 2000]");
                c.setOutput("M", matrixOut(Matrix(static_cast<int>(r), static_cast<int>(k))));
                return true;
            });
    lib.add(pureBlock("matrix.identity", "Matrice identité", kMath, "Matrice identité n × n.", { data("n", "n", VT::Integer, 3LL) },
                      { object("M", "Matrice", Type::Matrix) }, "Dimension de 1 à 2000.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const long long n = c.integer("n");
                if (c.failed()) return false;
                if (n < 1 || n > 2000) return c.fail("dimension hors de [1 ; 2000]");
                c.setOutput("M", matrixOut(LA::identity(static_cast<int>(n))));
                return true;
            });
    lib.add(pureBlock("vector.from_text", "Vecteur (saisie)", kMath, "Vecteur saisi : composantes séparées par « ; », des espaces ou des virgules.",
                      { data("text", "Composantes", VT::Text, std::string("1 ; 2")), data("title", "Titre", VT::Text, std::string()) },
                      { object("v", "Vecteur", Type::Vector) }, "Point décimal.", "TSALab/lab/Research/Structural/MatrixNodes.cpp"),
            [](ExecutionContext& c) {
                Vector v;
                std::string error;
                const std::string text = c.text("text"), title = c.text("title");
                if (c.failed()) return false;
                if (!parseVector(text, v, &error)) return c.fail(error);
                c.setOutput("v", vectorOut(std::move(v), {}, title));
                return true;
            });
    lib.add(pureBlock("vector.zeros", "Vecteur nul", kMath, "Vecteur de n zéros.", { data("n", "n", VT::Integer, 3LL) }, { object("v", "Vecteur", Type::Vector) },
                      "Dimension de 1 à 2000.", "TSALab/lab/Research/Structural/MatrixNodes.cpp"),
            [](ExecutionContext& c) {
                const long long n = c.integer("n");
                if (c.failed()) return false;
                if (n < 1 || n > 2000) return c.fail("dimension hors de [1 ; 2000]");
                c.setOutput("v", vectorOut(Vector(static_cast<std::size_t>(n), 0.0)));
                return true;
            });

    // --- Opérations ----------------------------------------------------------------------------------
    lib.add(pureBlock("matrix.add", "A + B", kMath, "Somme de deux matrices de mêmes dimensions.",
                      { object("A", "A", Type::Matrix), object("B", "B", Type::Matrix) }, { object("C", "A + B", Type::Matrix) },
                      "Contrôle : mêmes dimensions. Libellés de A conservés.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                const LabeledMatrix* b = matrixIn(c, "B");
                if (!a || !b || !sameSize(c, a->matrix, b->matrix, "A + B")) return false;
                // Libellés de A, sinon de B (assemblage : K nulle de départ sans libellés + A_eᵀ·k_e·A_e libellée)
                const bool own = !a->rows.empty() || !a->cols.empty();
                c.setOutput("C", matrixOut(LA::add(a->matrix, b->matrix), own ? a->rows : b->rows, own ? a->cols : b->cols, a->title));
                return true;
            });
    lib.add(pureBlock("matrix.subtract", "A − B", kMath, "Différence de deux matrices de mêmes dimensions.",
                      { object("A", "A", Type::Matrix), object("B", "B", Type::Matrix) }, { object("C", "A − B", Type::Matrix) }, "Contrôle : mêmes dimensions.",
                      kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                const LabeledMatrix* b = matrixIn(c, "B");
                if (!a || !b || !sameSize(c, a->matrix, b->matrix, "A − B")) return false;
                const bool own = !a->rows.empty() || !a->cols.empty();
                c.setOutput("C", matrixOut(LA::add(a->matrix, b->matrix, -1.0), own ? a->rows : b->rows, own ? a->cols : b->cols));
                return true;
            });
    lib.add(pureBlock("matrix.multiply", "A·B", kMath, "Produit matriciel.", { object("A", "A", Type::Matrix), object("B", "B", Type::Matrix) },
                      { object("C", "A·B", Type::Matrix) }, "Contrôle : colonnes de A = lignes de B. Libellés : lignes de A, colonnes de B.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                const LabeledMatrix* b = matrixIn(c, "B");
                if (!a || !b) return false;
                if (a->matrix.cols != b->matrix.rows)
                    return c.fail("dimensions incompatibles pour A·B : A est " + dims(a->matrix) + ", B est " + dims(b->matrix));
                c.setOutput("C", matrixOut(LA::multiply(a->matrix, b->matrix), a->rows, b->cols));
                return true;
            });
    lib.add(pureBlock("matrix.scale", "s·A", kMath, "Produit d'une matrice par un scalaire.", { object("A", "A", Type::Matrix), data("s", "s", VT::Real, 1.0) },
                      { object("C", "s·A", Type::Matrix) }, "Libellés conservés.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                const double s = c.real("s");
                if (!a || c.failed()) return false;
                c.setOutput("C", matrixOut(LA::scaled(a->matrix, s), a->rows, a->cols));
                return true;
            });
    lib.add(pureBlock("matrix.transpose", "Aᵀ", kMath, "Transposée.", { object("A", "A", Type::Matrix) }, { object("At", "Aᵀ", Type::Matrix) },
                      "Libellés de lignes et de colonnes échangés.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                if (!a) return false;
                c.setOutput("At", matrixOut(LA::transpose(a->matrix), a->cols, a->rows, a->title.empty() ? std::string() : a->title + "ᵀ"));
                return true;
            });
    lib.add(pureBlock("matrix.vector_product", "A·x", kMath, "Produit matrice-vecteur.", { object("A", "A", Type::Matrix), object("x", "x", Type::Vector) },
                      { object("y", "A·x", Type::Vector) }, "Contrôle : colonnes de A = taille de x. Libellés : lignes de A.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                const LabeledVector* x = vectorIn(c, "x");
                if (!a || !x) return false;
                if (a->matrix.cols != static_cast<int>(x->vector.size()))
                    return c.fail("dimensions incompatibles pour A·x : A est " + dims(a->matrix) + ", x a " + std::to_string(x->vector.size()) + " composantes");
                c.setOutput("y", vectorOut(LA::multiply(a->matrix, x->vector), a->rows));
                return true;
            });
    lib.add(pureBlock("matrix.congruence", "Aᵀ·B·A", kMath, "Produit Aᵀ·B·A : changement de repère (k = Tᵀ·k'·T) ou assemblage (A_eᵀ·k_e·A_e).",
                      { object("A", "A", Type::Matrix), object("B", "B", Type::Matrix) }, { object("C", "Aᵀ·B·A", Type::Matrix) },
                      "Contrôle : B carrée, lignes de A = dimension de B. Libellés : colonnes de A.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                const LabeledMatrix* b = matrixIn(c, "B");
                if (!a || !b) return false;
                if (b->matrix.rows != b->matrix.cols || a->matrix.rows != b->matrix.rows)
                    return c.fail("dimensions incompatibles pour Aᵀ·B·A : A est " + dims(a->matrix) + ", B est " + dims(b->matrix));
                c.setOutput("C", matrixOut(LA::congruence(a->matrix, b->matrix), a->cols, a->cols));
                return true;
            });
    lib.add(pureBlock("vector.add", "x + y", kMath, "Somme de deux vecteurs.", { object("x", "x", Type::Vector), object("y", "y", Type::Vector) },
                      { object("z", "x + y", Type::Vector) }, "Contrôle : même taille.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledVector* x = vectorIn(c, "x");
                const LabeledVector* y = vectorIn(c, "y");
                if (!x || !y) return false;
                if (x->vector.size() != y->vector.size())
                    return c.fail("tailles incompatibles pour x + y : " + std::to_string(x->vector.size()) + " et " + std::to_string(y->vector.size()));
                Vector z = x->vector;
                for (std::size_t k = 0; k < z.size(); ++k) z[k] += y->vector[k];
                c.setOutput("z", vectorOut(std::move(z), x->labels.empty() ? y->labels : x->labels, x->title));
                return true;
            });
    lib.add(pureBlock("vector.subtract", "x − y", kMath, "Différence de deux vecteurs.", { object("x", "x", Type::Vector), object("y", "y", Type::Vector) },
                      { object("z", "x − y", Type::Vector) }, "Contrôle : même taille.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledVector* x = vectorIn(c, "x");
                const LabeledVector* y = vectorIn(c, "y");
                if (!x || !y) return false;
                if (x->vector.size() != y->vector.size())
                    return c.fail("tailles incompatibles pour x − y : " + std::to_string(x->vector.size()) + " et " + std::to_string(y->vector.size()));
                c.setOutput("z", vectorOut(LA::subtract(x->vector, y->vector), x->labels));
                return true;
            });
    lib.add(pureBlock("vector.scale", "s·x", kMath, "Produit d'un vecteur par un scalaire.", { object("x", "x", Type::Vector), data("s", "s", VT::Real, 1.0) },
                      { object("z", "s·x", Type::Vector) }, "Libellés conservés.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledVector* x = vectorIn(c, "x");
                const double s = c.real("s");
                if (!x || c.failed()) return false;
                Vector z = x->vector;
                for (double& v : z) v *= s;
                c.setOutput("z", vectorOut(std::move(z), x->labels));
                return true;
            });
    lib.add(pureBlock("vector.dot", "x·y", kMath, "Produit scalaire.", { object("x", "x", Type::Vector), object("y", "y", Type::Vector) },
                      { data("dot", "x·y", VT::Real) }, "Contrôle : même taille.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledVector* x = vectorIn(c, "x");
                const LabeledVector* y = vectorIn(c, "y");
                if (!x || !y) return false;
                if (x->vector.size() != y->vector.size()) return c.fail("tailles incompatibles pour x·y");
                c.setOutput("dot", LA::dot(x->vector, y->vector));
                return true;
            });
    lib.add(pureBlock("vector.norm", "‖x‖", kMath, "Norme euclidienne (2) ou maximale (inf) d'un vecteur.",
                      { object("x", "x", Type::Vector), data("kind", "Norme (2, inf)", VT::Text, std::string("2")) }, { data("norm", "‖x‖", VT::Real) },
                      "‖x‖₂ = √Σx² ; ‖x‖∞ = max|x|.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledVector* x = vectorIn(c, "x");
                const std::string kind = c.text("kind");
                if (!x || c.failed()) return false;
                if (kind == "2") c.setOutput("norm", LA::norm2(x->vector));
                else if (kind == "inf" || kind == "∞" || kind == "max") c.setOutput("norm", LA::normInf(x->vector));
                else return c.fail("norme inconnue « " + kind + " » (2, inf)");
                return true;
            });

    // --- Coefficients, extraction --------------------------------------------------------------------
    lib.add(pureBlock("matrix.get", "Coefficient A(i, j)", kMath, "Lecture d'un coefficient.",
                      { object("A", "A", Type::Matrix), data("i", "i", VT::Integer, 0LL), data("j", "j", VT::Integer, 0LL) }, { data("value", "A(i, j)", VT::Real) }, idx,
                      kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                const long long i = c.integer("i"), j = c.integer("j");
                if (!a || c.failed() || !indexIn(c, i, a->matrix.rows, "ligne") || !indexIn(c, j, a->matrix.cols, "colonne")) return false;
                c.setOutput("value", a->matrix(static_cast<int>(i), static_cast<int>(j)));
                return true;
            });
    lib.add(pureBlock("matrix.set", "Modifier A(i, j)", kMath, "Nouvelle matrice avec un coefficient remplacé (ou augmenté).",
                      { object("A", "A", Type::Matrix), data("i", "i", VT::Integer, 0LL), data("j", "j", VT::Integer, 0LL), data("value", "Valeur", VT::Real, 0.0),
                        data("add", "Ajouter (au lieu de remplacer)", VT::Bool, false) },
                      { object("B", "Matrice", Type::Matrix) }, idx + "\nLa matrice d'entrée n'est pas modifiée (objets immuables).", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                const long long i = c.integer("i"), j = c.integer("j");
                const double v = c.real("value");
                const bool add = c.boolean("add");
                if (!a || c.failed() || !indexIn(c, i, a->matrix.rows, "ligne") || !indexIn(c, j, a->matrix.cols, "colonne")) return false;
                LabeledMatrix m = *a;
                double& cell = m.matrix(static_cast<int>(i), static_cast<int>(j));
                cell = add ? cell + v : v;
                c.setOutput("B", make<MatrixObject>(std::move(m)));
                return true;
            });
    lib.add(pureBlock("vector.get", "Composante x(i)", kMath, "Lecture d'une composante.", { object("x", "x", Type::Vector), data("i", "i", VT::Integer, 0LL) },
                      { data("value", "x(i)", VT::Real) }, idx, kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledVector* x = vectorIn(c, "x");
                const long long i = c.integer("i");
                if (!x || c.failed() || !indexIn(c, i, static_cast<int>(x->vector.size()), "indice")) return false;
                c.setOutput("value", x->vector[static_cast<std::size_t>(i)]);
                return true;
            });
    lib.add(pureBlock("vector.set", "Modifier x(i)", kMath, "Nouveau vecteur avec une composante remplacée (ou augmentée).",
                      { object("x", "x", Type::Vector), data("i", "i", VT::Integer, 0LL), data("value", "Valeur", VT::Real, 0.0), data("add", "Ajouter", VT::Bool, false) },
                      { object("y", "Vecteur", Type::Vector) }, idx, kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledVector* x = vectorIn(c, "x");
                const long long i = c.integer("i");
                const double v = c.real("value");
                const bool add = c.boolean("add");
                if (!x || c.failed() || !indexIn(c, i, static_cast<int>(x->vector.size()), "indice")) return false;
                LabeledVector y = *x;
                double& cell = y.vector[static_cast<std::size_t>(i)];
                cell = add ? cell + v : v;
                c.setOutput("y", make<VectorObject>(std::move(y)));
                return true;
            });
    lib.add(pureBlock("matrix.size", "Dimensions de A", kMath, "Nombre de lignes et de colonnes.", { object("A", "A", Type::Matrix) },
                      { data("rows", "Lignes", VT::Integer), data("cols", "Colonnes", VT::Integer) },
                      "Dimensions d'une matrice (K globale : n × n avec n = 3 × nombre de nœuds ; A_e : 6 × n). Utile pour borner une boucle "
                      "(« Pour » de 0 à lignes − 1) ou contrôler un assemblage.",
                      kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                if (!a) return false;
                c.setOutput("rows", static_cast<long long>(a->matrix.rows));
                c.setOutput("cols", static_cast<long long>(a->matrix.cols));
                return true;
            });
    lib.add(pureBlock("vector.size", "Taille de x", kMath, "Nombre de composantes.", { object("x", "x", Type::Vector) }, { data("size", "Taille", VT::Integer) },
                      "Nombre de composantes d'un vecteur (F global : n ; d_f : nombre de DDL libres). Utile pour borner une boucle ou créer un "
                      "vecteur nul de même taille.",
                      kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledVector* x = vectorIn(c, "x");
                if (!x) return false;
                c.setOutput("size", static_cast<long long>(x->vector.size()));
                return true;
            });
    lib.add(pureBlock("matrix.check_dims", "Vérifier les dimensions", kMath, "Contrôle qu'une matrice a les dimensions attendues (le bloc n'échoue pas).",
                      { object("A", "A", Type::Matrix), data("rows", "Lignes attendues", VT::Integer, 6LL), data("cols", "Colonnes attendues", VT::Integer, 6LL) },
                      { data("ok", "Conforme", VT::Bool), data("message", "Message", VT::Text) },
                      "Contrôle explicite des dimensions attendues (par exemple k_e : 6 × 6, A_e : 6 × n, K : n × n) : le bloc rend faux et un "
                      "message au lieu d'échouer, pour vérifier une étape d'un algorithme écrit par l'étudiant.",
                      kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                const long long r = c.integer("rows"), k = c.integer("cols");
                if (!a || c.failed()) return false;
                const bool ok = a->matrix.rows == r && a->matrix.cols == k;
                c.setOutput("ok", ok);
                c.setOutput("message", ok ? "dimensions conformes (" + dims(a->matrix) + ")"
                                          : "dimensions " + dims(a->matrix) + " au lieu de " + std::to_string(r) + "×" + std::to_string(k));
                return true;
            });
    lib.add(pureBlock("matrix.submatrix", "Sous-matrice", kMath, "Lignes et colonnes choisies (listes d'indices).",
                      { object("A", "A", Type::Matrix), data("rows", "Lignes (0,1,…)", VT::IdList, std::vector<int> { 0 }), data("cols", "Colonnes (0,1,…)", VT::IdList, std::vector<int> { 0 }) },
                      { object("B", "Sous-matrice", Type::Matrix) }, idx + "\nExemple : K_ff = K[f, f] avec la liste des DDL libres.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                if (!a) return false;
                std::vector<int> r, k;
                if (!indices(c, "rows", a->matrix.rows, r) || !indices(c, "cols", a->matrix.cols, k)) return false;
                Matrix b(static_cast<int>(r.size()), static_cast<int>(k.size()));
                for (std::size_t i = 0; i < r.size(); ++i)
                    for (std::size_t j = 0; j < k.size(); ++j) b(static_cast<int>(i), static_cast<int>(j)) = a->matrix(r[i], k[j]);
                c.setOutput("B", matrixOut(std::move(b), pick(a->rows, r), pick(a->cols, k)));
                return true;
            });
    lib.add(pureBlock("vector.subvector", "Sous-vecteur", kMath, "Composantes choisies (liste d'indices).",
                      { object("x", "x", Type::Vector), data("indices", "Indices (0,1,…)", VT::IdList, std::vector<int> { 0 }) },
                      { object("y", "Sous-vecteur", Type::Vector) }, idx, kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledVector* x = vectorIn(c, "x");
                if (!x) return false;
                std::vector<int> ids;
                if (!indices(c, "indices", static_cast<int>(x->vector.size()), ids)) return false;
                Vector y;
                for (int i : ids) y.push_back(x->vector[static_cast<std::size_t>(i)]);
                c.setOutput("y", vectorOut(std::move(y), pick(x->labels, ids)));
                return true;
            });
    lib.add(pureBlock("matrix.row", "Ligne de A", kMath, "Ligne i en vecteur.", { object("A", "A", Type::Matrix), data("i", "i", VT::Integer, 0LL) },
                      { object("row", "Ligne", Type::Vector) }, idx, kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                const long long i = c.integer("i");
                if (!a || c.failed() || !indexIn(c, i, a->matrix.rows, "ligne")) return false;
                Vector v(static_cast<std::size_t>(a->matrix.cols));
                for (int j = 0; j < a->matrix.cols; ++j) v[static_cast<std::size_t>(j)] = a->matrix(static_cast<int>(i), j);
                c.setOutput("row", vectorOut(std::move(v), a->cols));
                return true;
            });
    lib.add(pureBlock("matrix.column", "Colonne de A", kMath, "Colonne j en vecteur.", { object("A", "A", Type::Matrix), data("j", "j", VT::Integer, 0LL) },
                      { object("column", "Colonne", Type::Vector) }, idx, kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                const long long j = c.integer("j");
                if (!a || c.failed() || !indexIn(c, j, a->matrix.cols, "colonne")) return false;
                Vector v(static_cast<std::size_t>(a->matrix.rows));
                for (int i = 0; i < a->matrix.rows; ++i) v[static_cast<std::size_t>(i)] = a->matrix(i, static_cast<int>(j));
                c.setOutput("column", vectorOut(std::move(v), a->rows));
                return true;
            });

    // --- Résolution et analyse -------------------------------------------------------------------------
    lib.add(pureBlock("matrix.solve", "Résoudre K·x = f", kMath, "Résolution d'un système linéaire par factorisation (sans inverse explicite).",
                      { object("K", "K", Type::Matrix), object("f", "f", Type::Vector), data("method", "Méthode (cholesky, lu, cg)", VT::Text, std::string("lu")) },
                      { object("x", "x", Type::Vector), data("residual", "Résidu relatif", VT::Real), data("min_pivot", "Pivot minimal", VT::Real),
                        data("report", "Rapport", VT::Text) },
                      "LU à pivot partiel (toute matrice régulière), Cholesky (symétrique définie positive) ou gradient conjugué préconditionné "
                      "(symétrique définie positive, itératif). Résidu ‖K·x − f‖/‖f‖ recalculé. Refus motivé : pivot nul (matrice singulière : "
                      "mécanisme pour une rigidité), pivot négatif (Cholesky : matrice non définie positive), non-convergence (gradient conjugué).",
                      kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* K = matrixIn(c, "K");
                const LabeledVector* f = vectorIn(c, "f");
                const std::string methodText = c.text("method");
                if (!K || !f || c.failed()) return false;
                if (K->matrix.rows != K->matrix.cols || K->matrix.rows != static_cast<int>(f->vector.size()))
                    return c.fail("dimensions incompatibles : K est " + dims(K->matrix) + ", f a " + std::to_string(f->vector.size()) + " composantes");
                tsalab::numerics::SolverSettings s;
                if (!parseMethod(methodText, s.method)) return c.fail("méthode inconnue « " + methodText + " » (cholesky, lu, cg)");
                Vector x;
                const auto r = tsalab::numerics::solve(K->matrix, f->vector, x, s);
                if (!r.success) return c.fail(r.message);
                c.setOutput("x", vectorOut(std::move(x), K->cols, "x"));
                c.setOutput("residual", r.relativeResidual);
                c.setOutput("min_pivot", r.minPivot);
                c.setOutput("report", std::string(tsalab::numerics::solverMethodName(s.method)) + " : " + r.message + " Résidu relatif " + num(r.relativeResidual, 3) + ".");
                return true;
            });
    lib.add(pureBlock("matrix.compare_solvers", "Comparer deux solveurs", kMath,
                      "Résout le même système K·x = f par deux méthodes et compare solutions, résidus, pivots et itérations.",
                      { object("K", "K", Type::Matrix), object("f", "f", Type::Vector), data("method_a", "Méthode A (cholesky, lu, cg)", VT::Text, std::string("cholesky")),
                        data("method_b", "Méthode B (cholesky, lu, cg)", VT::Text, std::string("cg")), data("tolerance", "Tolérance du gradient conjugué", VT::Real, 1e-10) },
                      { object("x_a", "x (A)", Type::Vector), object("x_b", "x (B)", Type::Vector), data("deviation", "Écart max|xA − xB| / max|xA|", VT::Real),
                        data("residual_a", "Résidu relatif A", VT::Real), data("residual_b", "Résidu relatif B", VT::Real),
                        data("iterations_b", "Itérations B", VT::Integer), object("history_b", "Résidus de B", Type::Curve), data("report", "Rapport", VT::Text) },
                      "Même système, deux méthodes (LU à pivot partiel, Cholesky, gradient conjugué préconditionné de Jacobi) : écart relatif des "
                      "solutions, résidus ‖K·x − f‖/‖f‖ recalculés, pivots extrêmes (méthodes directes), itérations et historique des résidus "
                      "(gradient conjugué : la tolérance est celle de l'arrêt, ‖r‖/‖f‖). Les durées ne sont pas comparées (elles dépendent de la "
                      "machine). Refus motivé si une méthode échoue (pivot nul ou négatif, non-convergence), avec le nom de la méthode.",
                      kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* K = matrixIn(c, "K");
                const LabeledVector* f = vectorIn(c, "f");
                const std::string ta = c.text("method_a"), tb = c.text("method_b");
                const double tol = c.real("tolerance");
                if (!K || !f || c.failed()) return false;
                if (K->matrix.rows != K->matrix.cols || K->matrix.rows != static_cast<int>(f->vector.size()))
                    return c.fail("dimensions incompatibles : K est " + dims(K->matrix) + ", f a " + std::to_string(f->vector.size()) + " composantes");
                tsalab::numerics::SolverSettings sa, sb;
                if (!parseMethod(ta, sa.method)) return c.fail("méthode A inconnue « " + ta + " » (cholesky, lu, cg)");
                if (!parseMethod(tb, sb.method)) return c.fail("méthode B inconnue « " + tb + " » (cholesky, lu, cg)");
                if (!(tol > 0.0)) return c.fail("tolérance du gradient conjugué non positive");
                sa.tolerance = sb.tolerance = tol;
                Vector xa, xb;
                const auto ra = tsalab::numerics::solve(K->matrix, f->vector, xa, sa);
                const auto rb = tsalab::numerics::solve(K->matrix, f->vector, xb, sb);
                if (!ra.success) return c.fail(std::string("méthode A (") + tsalab::numerics::solverMethodName(sa.method) + ") : " + ra.message);
                if (!rb.success) return c.fail(std::string("méthode B (") + tsalab::numerics::solverMethodName(sb.method) + ") : " + rb.message);
                double diff = 0.0, scale = 0.0;
                for (std::size_t k = 0; k < xa.size(); ++k) diff = std::max(diff, std::abs(xa[k] - xb[k])), scale = std::max(scale, std::abs(xa[k]));
                const double deviation = scale > 0.0 ? diff / scale : diff;
                std::vector<double> it, hist;
                for (std::size_t k = 0; k < rb.residualHistory.size(); ++k) it.push_back(static_cast<double>(k)), hist.push_back(rb.residualHistory[k]);
                auto line = [](const char* tag, tsalab::numerics::SolverMethod m, const tsalab::numerics::SolverReport& r) {
                    std::string s = std::string(tag) + " " + tsalab::numerics::solverMethodName(m) + " : résidu " + num(r.relativeResidual, 3) + ", itérations "
                                    + std::to_string(r.iterations);
                    if (m != tsalab::numerics::SolverMethod::ConjugateGradient) s += ", pivots " + num(r.minPivot, 3) + " à " + num(r.maxPivot, 3);
                    return s;
                };
                c.setOutput("x_a", vectorOut(std::move(xa), K->cols, std::string("x (") + tsalab::numerics::solverMethodName(sa.method) + ")"));
                c.setOutput("x_b", vectorOut(std::move(xb), K->cols, std::string("x (") + tsalab::numerics::solverMethodName(sb.method) + ")"));
                c.setOutput("deviation", deviation);
                c.setOutput("residual_a", ra.relativeResidual);
                c.setOutput("residual_b", rb.relativeResidual);
                c.setOutput("iterations_b", static_cast<long long>(rb.iterations));
                c.setOutput("history_b", make<CurveObject>(CurveData { std::move(it), std::move(hist), "résidus de la méthode B", "itération", "‖r‖/‖f‖" }));
                c.setOutput("report", line("A", sa.method, ra) + "\n" + line("B", sb.method, rb) + "\nÉcart relatif des solutions : " + num(deviation, 3));
                return true;
            });

    lib.add(pureBlock("matrix.lu", "Décomposition LU", kMath, "P·A = L·U (élimination de Gauss à pivot partiel).",
                      { object("A", "A", Type::Matrix) },
                      { object("L", "L", Type::Matrix), object("U", "U", Type::Matrix), object("P", "P", Type::Matrix), data("min_pivot", "Pivot minimal", VT::Real) },
                      "L triangulaire inférieure à diagonale unité, U triangulaire supérieure, P matrice de permutation des lignes. Refus : A non "
                      "carrée ou singulière (pivot nul, avec le numéro de la colonne).",
                      "TSALab/lab/Research/Structural/MatrixNodes.cpp"),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                if (!a) return false;
                if (a->matrix.rows != a->matrix.cols) return c.fail("matrice non carrée (" + dims(a->matrix) + ")");
                Matrix L, U, P;
                int failed = -1;
                double minPivot = 0.0;
                if (!luDecompose(a->matrix, L, U, P, failed, minPivot))
                    return c.fail("matrice singulière : pivot nul à la colonne " + std::to_string(failed) + " (indices à partir de 0)");
                c.setOutput("L", matrixOut(std::move(L), {}, {}, "L"));
                c.setOutput("U", matrixOut(std::move(U), {}, {}, "U"));
                c.setOutput("P", matrixOut(std::move(P), {}, {}, "P"));
                c.setOutput("min_pivot", minPivot);
                return true;
            });
    lib.add(pureBlock("matrix.inverse", "Inversion contrôlée", kMath,
                      "Inverse d'une matrice carrée, refusée si elle est singulière ; conditionnement κ∞ = ‖A‖∞·‖A⁻¹‖∞ rendu et signalé.",
                      { object("A", "A", Type::Matrix) }, { object("Ainv", "A⁻¹", Type::Matrix), data("condition", "κ∞", VT::Real) },
                      "Calcul par LU (une résolution par colonne de l'identité). Refus : matrice non carrée ou singulière. κ∞ > 1e10 : avertissement "
                      "dans le journal (résultat peu fiable). Pour résoudre un système, préférer « Résoudre K·x = f » : l'inverse explicite coûte plus "
                      "cher et amplifie les erreurs d'arrondi.",
                      "TSALab/lab/Research/Structural/MatrixNodes.cpp"),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                if (!a) return false;
                const int n = a->matrix.rows;
                if (n != a->matrix.cols) return c.fail("matrice non carrée (" + dims(a->matrix) + ")");
                Matrix L, U, P;
                int failed = -1;
                double minPivot = 0.0;
                if (!luDecompose(a->matrix, L, U, P, failed, minPivot))
                    return c.fail("matrice singulière (pivot nul à la colonne " + std::to_string(failed) + ") : pas d'inverse");
                Matrix inv(n, n);
                for (int col = 0; col < n; ++col)
                {
                    Vector b(static_cast<std::size_t>(n), 0.0), y(static_cast<std::size_t>(n), 0.0), x(static_cast<std::size_t>(n), 0.0);
                    for (int i = 0; i < n; ++i) b[static_cast<std::size_t>(i)] = P(i, col);
                    for (int i = 0; i < n; ++i)
                    {
                        double s = b[static_cast<std::size_t>(i)];
                        for (int k = 0; k < i; ++k) s -= L(i, k) * y[static_cast<std::size_t>(k)];
                        y[static_cast<std::size_t>(i)] = s;
                    }
                    for (int i = n - 1; i >= 0; --i)
                    {
                        double s = y[static_cast<std::size_t>(i)];
                        for (int k = i + 1; k < n; ++k) s -= U(i, k) * x[static_cast<std::size_t>(k)];
                        x[static_cast<std::size_t>(i)] = s / U(i, i);
                    }
                    for (int i = 0; i < n; ++i) inv(i, col) = x[static_cast<std::size_t>(i)];
                }
                const double kappa = normInfMatrix(a->matrix) * normInfMatrix(inv);
                if (kappa > 1e10) c.log("Inversion contrôlée : matrice mal conditionnée (κ∞ ≈ " + num(kappa, 3) + "), inverse peu fiable");
                c.setOutput("Ainv", matrixOut(std::move(inv), a->cols, a->rows, a->title.empty() ? std::string() : a->title + "⁻¹"));
                c.setOutput("condition", kappa);
                return true;
            });
    lib.add(pureBlock("matrix.residual", "Résidu ‖K·x − f‖/‖f‖", kMath, "Résidu relatif d'une solution (contrôle d'un solveur).",
                      { object("K", "K", Type::Matrix), object("x", "x", Type::Vector), object("f", "f", Type::Vector) },
                      { data("residual", "Résidu relatif", VT::Real), object("r", "K·x − f", Type::Vector) },
                      "‖K·x − f‖₂ / ‖f‖₂ (‖K·x − f‖₂ si f = 0).", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* K = matrixIn(c, "K");
                const LabeledVector* x = vectorIn(c, "x");
                const LabeledVector* f = vectorIn(c, "f");
                if (!K || !x || !f) return false;
                if (K->matrix.cols != static_cast<int>(x->vector.size()) || K->matrix.rows != static_cast<int>(f->vector.size()))
                    return c.fail("dimensions incompatibles : K " + dims(K->matrix) + ", x " + std::to_string(x->vector.size()) + ", f " + std::to_string(f->vector.size()));
                const Vector r = LA::subtract(LA::multiply(K->matrix, x->vector), f->vector);
                const double nf = LA::norm2(f->vector);
                c.setOutput("residual", nf > 0.0 ? LA::norm2(r) / nf : LA::norm2(r));
                c.setOutput("r", vectorOut(r, K->rows, "K·x − f"));
                return true;
            });
    lib.add(pureBlock("matrix.condition", "Conditionnement", kMath, "Conditionnement spectral λmax/λmin d'une matrice symétrique définie positive.",
                      { object("A", "A", Type::Matrix) }, { data("condition", "κ", VT::Real), data("digits", "Chiffres fiables", VT::Real) },
                      "Valeurs propres de Jacobi (n ≤ 300). Chiffres significatifs fiables ≈ 16 − log10(κ) en double précision. Matrice singulière : κ infini.",
                      kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                if (!a) return false;
                if (a->matrix.rows != a->matrix.cols) return c.fail("matrice non carrée (" + dims(a->matrix) + ")");
                if (a->matrix.rows > 300) return c.fail("matrice trop grande pour le calcul des valeurs propres (n > 300)");
                const double k = tsalab::numerics::conditionNumber(a->matrix);
                c.setOutput("condition", k);
                c.setOutput("digits", std::isfinite(k) && k > 0.0 ? std::max(0.0, 16.0 - std::log10(k)) : 0.0);
                return true;
            });
    lib.add(pureBlock("matrix.symmetry", "Symétrie", kMath, "Écart à la symétrie max|A − Aᵀ| / max|A| (0 : symétrique).",
                      { object("A", "A", Type::Matrix) }, { data("asymmetry", "Écart", VT::Real), data("symmetric", "Symétrique (≤ 1e-12)", VT::Bool) },
                      "Une matrice de rigidité assemblée doit être symétrique.", kSrcLinAlg),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                if (!a) return false;
                if (a->matrix.rows != a->matrix.cols) return c.fail("matrice non carrée (" + dims(a->matrix) + ")");
                const double e = LA::asymmetry(a->matrix);
                c.setOutput("asymmetry", e);
                c.setOutput("symmetric", e <= 1e-12);
                return true;
            });

    // --- Export ----------------------------------------------------------------------------------------
    lib.add(actionBlock("matrix.export_csv", "Exporter une matrice (CSV)", kMath, "Écrit une matrice dans un fichier CSV (libellés en tête).",
                        { object("A", "A", Type::Matrix), data("path", "Fichier", VT::Text, std::string("matrice.csv")) }, {},
                        "Séparateur « ; », 17 chiffres significatifs, première ligne : libellés des colonnes, première colonne : libellés des lignes. "
                        "Le chemin écrit est indiqué dans le journal.",
                        "TSALab/lab/Research/Structural/MatrixNodes.cpp"),
            [](ExecutionContext& c) {
                const LabeledMatrix* a = matrixIn(c, "A");
                const std::string path = c.text("path");
                if (!a || c.failed()) return false;
                std::ostringstream o;
                o << "DDL";
                for (int j = 0; j < a->matrix.cols; ++j) o << ";" << (j < static_cast<int>(a->cols.size()) ? a->cols[static_cast<std::size_t>(j)] : std::to_string(j));
                o << "\n";
                for (int i = 0; i < a->matrix.rows; ++i)
                {
                    o << (i < static_cast<int>(a->rows.size()) ? a->rows[static_cast<std::size_t>(i)] : std::to_string(i));
                    for (int j = 0; j < a->matrix.cols; ++j) o << ";" << csvValue(a->matrix(i, j));
                    o << "\n";
                }
                return writeCsv(c, path, o.str()) && c.fire("then");
            });
    lib.add(actionBlock("vector.export_csv", "Exporter un vecteur (CSV)", kMath, "Écrit un vecteur dans un fichier CSV (libellé ; valeur).",
                        { object("x", "x", Type::Vector), data("path", "Fichier", VT::Text, std::string("vecteur.csv")) }, {},
                        "Séparateur « ; », 17 chiffres significatifs.", "TSALab/lab/Research/Structural/MatrixNodes.cpp"),
            [](ExecutionContext& c) {
                const LabeledVector* x = vectorIn(c, "x");
                const std::string path = c.text("path");
                if (!x || c.failed()) return false;
                std::ostringstream o;
                o << "DDL;valeur\n";
                for (std::size_t k = 0; k < x->vector.size(); ++k)
                    o << (k < x->labels.size() ? x->labels[k] : std::to_string(k)) << ";" << csvValue(x->vector[k]) << "\n";
                return writeCsv(c, path, o.str()) && c.fire("then");
            });
}

} // namespace TSALab::Research::Structural::detail
