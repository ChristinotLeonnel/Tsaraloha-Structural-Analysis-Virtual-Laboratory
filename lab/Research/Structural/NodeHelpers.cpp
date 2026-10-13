// Outils communs des blocs de la programmation structurelle.

#include "NodeHelpers.h"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <type_traits>
#include <utility>

namespace TSALab::Research::Structural::detail
{

namespace
{
std::string lower(std::string s)
{
    // Minuscules ASCII ; les caractères accentués (UTF-8) sont conservés tels quels
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return ch < 128 ? static_cast<char>(std::tolower(ch)) : static_cast<char>(ch); });
    return s;
}

std::string trim(const std::string& s)
{
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

bool toDouble(std::string t, double& out)
{
    t = trim(t);
    std::replace(t.begin(), t.end(), ',', '.');
    if (t.empty()) return false;
    try
    {
        std::size_t used = 0;
        out = std::stod(t, &used);
        return used == t.size();
    }
    catch (const std::exception&)
    {
        return false;
    }
}

/// Valeur portée par l'objet d'une entrée. L'objet reste vivant pendant l'exécution du bloc (sorties mémorisées du
/// nœud source ou entrées déjà évaluées) : le pointeur rendu est valable dans l'exécuteur.
template <typename O>
auto valueOf(ExecutionContext& c, const char* pin) -> const std::decay_t<decltype(std::declval<const O&>().value())>*
{
    const auto o = c.objectAs<O>(pin);
    return o ? &o->value() : nullptr;
}
} // namespace

PinSpec execIn()
{
    return { "exec", "", PinKind::Exec };
}

PinSpec execOut(const char* name, const char* label)
{
    return { name, label, PinKind::Exec };
}

PinSpec data(const char* name, const char* label, VT type, Value def)
{
    PinSpec p;
    p.name = name;
    p.label = label;
    p.type = type;
    p.defaultValue = std::move(def);
    return p;
}

PinSpec required(const char* name, const char* label, VT type)
{
    PinSpec p = data(name, label, type);
    p.required = true;
    return p;
}

PinSpec object(const char* name, const char* label, const char* objectType)
{
    PinSpec p = data(name, label, VT::Object);
    p.objectType = objectType;
    p.required = true;
    return p;
}

PinSpec optionalObject(const char* name, const char* label, const char* objectType)
{
    PinSpec p = object(name, label, objectType);
    p.required = false;
    return p;
}

PinSpec any(const char* name, const char* label)
{
    PinSpec p = data(name, label, VT::Text);
    p.any = true;
    return p;
}

NodeDefinition pureBlock(const char* id, const char* title, const char* category, const char* description, std::vector<PinSpec> inputs,
                         std::vector<PinSpec> outputs, std::string documentation, const char* source)
{
    NodeDefinition d;
    d.id = id;
    d.title = title;
    d.category = category;
    d.description = description;
    d.inputs = std::move(inputs);
    d.outputs = std::move(outputs);
    d.pure = true;
    d.cacheable = true;
    d.status = NodeStatus::Validated;
    d.documentation = std::move(documentation);
    d.source = source;
    return d;
}

NodeDefinition actionBlock(const char* id, const char* title, const char* category, const char* description, std::vector<PinSpec> inputs,
                           std::vector<PinSpec> outputs, std::string documentation, const char* source)
{
    NodeDefinition d = pureBlock(id, title, category, description, std::move(inputs), std::move(outputs), std::move(documentation), source);
    d.pure = false;
    d.cacheable = false;
    d.inputs.insert(d.inputs.begin(), execIn());
    d.outputs.insert(d.outputs.begin(), execOut());
    return d;
}

bool parseDirection(const std::string& text, S::LoadDirection& out)
{
    std::string t = lower(trim(text));
    t.erase(std::remove(t.begin(), t.end(), ' '), t.end());
    if (t == "ylocal" || t == "ly" || t == "localy" || t == "y'" || t == "perpendiculaire" || t == "transversale" || t.empty()) out = S::LoadDirection::LocalY;
    else if (t == "xlocal" || t == "lx" || t == "localx" || t == "x'" || t == "axiale" || t == "axial") out = S::LoadDirection::LocalX;
    else if (t == "xglobal" || t == "gx" || t == "globalx" || t == "x" || t == "horizontale" || t == "horizontal") out = S::LoadDirection::GlobalX;
    else if (t == "yglobal" || t == "gy" || t == "globaly" || t == "y" || t == "verticale" || t == "vertical") out = S::LoadDirection::GlobalY;
    else return false;
    return true;
}

bool parseComponent(const std::string& text, int& out)
{
    const std::string t = lower(trim(text));
    if (t == "ux" || t == "x") out = 0;
    else if (t == "uy" || t == "y") out = 1;
    else if (t == "rz" || t == "θz" || t == "tz" || t == "rotation" || t == "theta") out = 2;
    else return false;
    return true;
}

bool parsePairs(const std::string& text, std::vector<std::pair<double, double>>& out, std::string* error)
{
    out.clear();
    std::stringstream ss(text);
    for (std::string item; std::getline(ss, item, ';');)
    {
        if (trim(item).empty()) continue;
        const auto colon = item.find(':');
        double a = 0.0, b = 0.0;
        if (colon == std::string::npos || !toDouble(item.substr(0, colon), a) || !toDouble(item.substr(colon + 1), b))
        {
            if (error) *error = "couple invalide « " + trim(item) + " » (forme attendue : a:b ; c:d)";
            return false;
        }
        out.emplace_back(a, b);
    }
    if (out.empty() && error) *error = "aucun couple a:b";
    return !out.empty();
}

bool parseMatrix(const std::string& text, Matrix& out, std::string* error)
{
    std::vector<std::vector<double>> rows;
    std::string normalized = text;
    std::replace(normalized.begin(), normalized.end(), '\n', ';');
    std::stringstream ss(normalized);
    for (std::string line; std::getline(ss, line, ';');)
    {
        std::replace(line.begin(), line.end(), ',', ' ');
        std::stringstream ls(line);
        std::vector<double> row;
        for (std::string tok; ls >> tok;)
        {
            double v = 0.0;
            if (!toDouble(tok, v))
            {
                if (error) *error = "coefficient invalide « " + tok + " »";
                return false;
            }
            row.push_back(v);
        }
        if (row.empty()) continue;
        if (!rows.empty() && row.size() != rows.front().size())
        {
            if (error) *error = "lignes de longueurs différentes (" + std::to_string(rows.front().size()) + " et " + std::to_string(row.size()) + " coefficients)";
            return false;
        }
        rows.push_back(std::move(row));
    }
    if (rows.empty())
    {
        if (error) *error = "matrice vide";
        return false;
    }
    out = Matrix(static_cast<int>(rows.size()), static_cast<int>(rows.front().size()));
    for (std::size_t i = 0; i < rows.size(); ++i)
        for (std::size_t j = 0; j < rows[i].size(); ++j) out(static_cast<int>(i), static_cast<int>(j)) = rows[i][j];
    return true;
}

bool parseVector(const std::string& text, Vector& out, std::string* error)
{
    out.clear();
    std::string t = text;
    for (char& ch : t)
        if (ch == ';' || ch == ',' || ch == '\n' || ch == '\t') ch = ' ';
    std::stringstream ss(t);
    for (std::string tok; ss >> tok;)
    {
        double v = 0.0;
        if (!toDouble(tok, v))
        {
            if (error) *error = "composante invalide « " + tok + " »";
            return false;
        }
        out.push_back(v);
    }
    if (out.empty() && error) *error = "vecteur vide";
    return !out.empty();
}

bool parseMethod(const std::string& text, tsalab::numerics::SolverMethod& out)
{
    const std::string t = lower(trim(text));
    if (t == "cholesky" || t.empty()) out = tsalab::numerics::SolverMethod::Cholesky;
    else if (t == "lu" || t == "gauss") out = tsalab::numerics::SolverMethod::GaussLU;
    else if (t == "cg" || t == "gradient conjugué" || t == "gradient") out = tsalab::numerics::SolverMethod::ConjugateGradient;
    else return false;
    return true;
}

std::string num(double v, int precision)
{
    std::ostringstream o;
    o.precision(precision);
    o << (v == 0.0 ? 0.0 : v);
    return o.str();
}

const S::Model2D* modelIn(ExecutionContext& c, const char* pin) { return valueOf<ModelObject>(c, pin); }
const S::FrameElement2D* elementIn(ExecutionContext& c, const char* pin) { return valueOf<ElementObject>(c, pin); }
const DofSet* dofsIn(ExecutionContext& c, const char* pin) { return valueOf<DofObject>(c, pin); }
const LabeledMatrix* matrixIn(ExecutionContext& c, const char* pin) { return valueOf<MatrixObject>(c, pin); }
const LabeledVector* vectorIn(ExecutionContext& c, const char* pin) { return valueOf<VectorObject>(c, pin); }
const S::AnalysisResult2D* resultIn(ExecutionContext& c, const char* pin) { return valueOf<ResultObject>(c, pin); }

const S::LoadCase* loadsIn(ExecutionContext& c, const char* pin)
{
    // Chargement : un cas (LoadCase) ; un groupe de charges (Load) est accepté par les blocs qui déclarent « Load »
    const ObjectRef o = c.object(pin);
    if (!o) return nullptr;
    if (const auto* lc = dynamic_cast<const LoadCaseObject*>(o.get())) return &lc->value();
    if (const auto* l = dynamic_cast<const LoadObject*>(o.get())) return &l->value();
    c.fail(std::string("entrée « ") + pin + " » : charges attendues, " + o->typeName() + " reçu");
    return nullptr;
}

ObjectRef matrixOut(Matrix m, std::vector<std::string> rows, std::vector<std::string> cols, std::string title)
{
    return make<MatrixObject>(LabeledMatrix { std::move(m), std::move(rows), std::move(cols), std::move(title) });
}

ObjectRef vectorOut(Vector v, std::vector<std::string> labels, std::string title)
{
    return make<VectorObject>(LabeledVector { std::move(v), std::move(labels), std::move(title) });
}

} // namespace TSALab::Research::Structural::detail
