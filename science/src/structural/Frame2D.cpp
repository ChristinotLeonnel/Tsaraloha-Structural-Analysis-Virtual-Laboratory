// Modèle de portique plan : recherches, aides de construction des charges, contrôle, chargement effectif.

#include "tsalab/structural/Frame2D.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace tsalab::structural
{

namespace
{
std::string num(double v)
{
    std::ostringstream o;
    o.precision(6);
    o << v;
    return o.str();
}

bool finite(double v) { return std::isfinite(v); }
} // namespace

// -----------------------------------------------------------------------------
// LoadCase, Model2D
// -----------------------------------------------------------------------------

void LoadCase::append(const LoadCase& other, double f)
{
    for (auto p : other.nodalLoads)
    {
        p.fx *= f, p.fy *= f, p.mz *= f;
        nodalLoads.push_back(p);
    }
    for (auto l : other.elementLoads)
    {
        l.q1 *= f, l.q2 *= f;
        l.deltaT *= f, l.gradientT *= f;
        l.epsilon0 *= f, l.kappa0 *= f;
        l.factor *= f;
        elementLoads.push_back(l);
    }
    for (auto d : other.imposedDisplacements)
    {
        d.value *= f;
        imposedDisplacements.push_back(d);
    }
}

const Node2D* Model2D::node(int id) const
{
    for (const auto& n : nodes)
        if (n.id == id) return &n;
    return nullptr;
}

const ElementDefinition* Model2D::element(int id) const
{
    for (const auto& e : elements)
        if (e.id == id) return &e;
    return nullptr;
}

const SupportCondition* Model2D::support(int nodeId) const
{
    for (const auto& s : supports)
        if (s.node == nodeId) return &s;
    return nullptr;
}

const LoadCase* Model2D::loadCase(int id) const
{
    for (const auto& c : loadCases)
        if (c.id == id) return &c;
    return nullptr;
}

const LoadCombination* Model2D::combination(int id) const
{
    for (const auto& c : combinations)
        if (c.id == id) return &c;
    return nullptr;
}

int Model2D::elementCount(int nodeId) const
{
    int n = 0;
    for (const auto& e : elements) n += (e.nodeI == nodeId) + (e.nodeJ == nodeId);
    return n;
}

bool Model2D::rotationFree(int nodeId) const
{
    const SupportCondition* s = support(nodeId);
    if (s && (s->fixRz || s->kRz > 0.0)) return false;
    int count = 0;
    for (const auto& e : elements)
    {
        if (e.nodeI == nodeId)
        {
            ++count;
            if (!e.releaseI) return false;
        }
        if (e.nodeJ == nodeId)
        {
            ++count;
            if (!e.releaseJ) return false;
        }
    }
    return count > 0;
}

std::string Model2D::metadataValue(const std::string& key) const
{
    for (const auto& [k, v] : metadata)
        if (k == key) return v;
    return {};
}

double Model2D::extent() const
{
    if (nodes.empty()) return 0.0;
    double x0 = nodes.front().x, x1 = x0, y0 = nodes.front().y, y1 = y0;
    for (const auto& n : nodes)
    {
        x0 = std::min(x0, n.x), x1 = std::max(x1, n.x);
        y0 = std::min(y0, n.y), y1 = std::max(y1, n.y);
    }
    return std::max(x1 - x0, y1 - y0);
}

// -----------------------------------------------------------------------------
// Aides de construction
// -----------------------------------------------------------------------------

ElementLoad uniformLoad(int element, double q, LoadDirection direction)
{
    return linearLoad(element, q, q, 0.0, -1.0, direction);
}

ElementLoad linearLoad(int element, double q1, double q2, double a, double b, LoadDirection direction, bool relative)
{
    ElementLoad l;
    l.element = element;
    l.type = ElementLoadType::Distributed;
    l.direction = direction;
    l.relative = relative;
    l.a = a, l.b = b;
    l.q1 = q1, l.q2 = q2;
    return l;
}

ElementLoad pointForce(int element, double P, double a, LoadDirection direction, bool relative)
{
    ElementLoad l;
    l.element = element;
    l.type = ElementLoadType::PointForce;
    l.direction = direction;
    l.relative = relative;
    l.a = a;
    l.q1 = P;
    return l;
}

ElementLoad pointMoment(int element, double M, double a, bool relative)
{
    ElementLoad l;
    l.element = element;
    l.type = ElementLoadType::PointMoment;
    l.relative = relative;
    l.a = a;
    l.q1 = M;
    return l;
}

ElementLoad thermalLoad(int element, double deltaT, double gradientT)
{
    ElementLoad l;
    l.element = element;
    l.type = ElementLoadType::Thermal;
    l.deltaT = deltaT;
    l.gradientT = gradientT;
    return l;
}

ElementLoad initialStrain(int element, double epsilon0, double kappa0)
{
    ElementLoad l;
    l.element = element;
    l.type = ElementLoadType::InitialStrain;
    l.epsilon0 = epsilon0;
    l.kappa0 = kappa0;
    return l;
}

ElementLoad selfWeight(int element, double factor)
{
    ElementLoad l;
    l.element = element;
    l.type = ElementLoadType::SelfWeight;
    l.direction = LoadDirection::GlobalY;
    l.factor = factor;
    return l;
}

std::vector<ElementLoad> piecewiseLinearLoad(int element, const std::vector<std::pair<double, double>>& points,
                                             LoadDirection direction, bool relative, std::string* error)
{
    std::vector<ElementLoad> out;
    if (points.size() < 2)
    {
        if (error) *error = "charge multi-segments : au moins deux points (position, intensité) sont nécessaires";
        return out;
    }
    for (std::size_t k = 1; k < points.size(); ++k)
    {
        const auto& [a, qa] = points[k - 1];
        const auto& [b, qb] = points[k];
        if (!(b > a))
        {
            if (error) *error = "charge multi-segments : positions non strictement croissantes (" + num(a) + " puis " + num(b) + ")";
            return {};
        }
        out.push_back(linearLoad(element, qa, qb, a, b, direction, relative));
    }
    return out;
}

const char* directionName(LoadDirection d)
{
    switch (d)
    {
    case LoadDirection::LocalX: return "x local";
    case LoadDirection::LocalY: return "y local";
    case LoadDirection::GlobalX: return "X global";
    case LoadDirection::GlobalY: return "Y global";
    }
    return "?";
}

const char* loadTypeName(ElementLoadType t)
{
    switch (t)
    {
    case ElementLoadType::Distributed: return "répartie";
    case ElementLoadType::PointForce: return "force ponctuelle";
    case ElementLoadType::PointMoment: return "moment ponctuel";
    case ElementLoadType::Thermal: return "thermique";
    case ElementLoadType::InitialStrain: return "déformation initiale";
    case ElementLoadType::SelfWeight: return "poids propre";
    }
    return "?";
}

const char* componentName(int component)
{
    return component == 0 ? "ux" : component == 1 ? "uy" : component == 2 ? "θz" : "?";
}

// -----------------------------------------------------------------------------
// Contrôle
// -----------------------------------------------------------------------------

std::string ValidationReport::text() const
{
    std::ostringstream o;
    if (errors.empty() && warnings.empty()) return "Modèle valide.";
    for (const auto& e : errors) o << "Erreur : " << e << "\n";
    for (const auto& w : warnings) o << "Avertissement : " << w << "\n";
    std::string s = o.str();
    if (!s.empty()) s.pop_back();
    return s;
}

namespace
{
void validateLoadCase(const Model2D& model, const LoadCase& c, ValidationReport& r)
{
    const std::string where = "cas " + std::to_string(c.id) + (c.name.empty() ? std::string() : " « " + c.name + " »");
    for (const auto& p : c.nodalLoads)
    {
        if (!model.node(p.node)) r.errors.push_back(where + " : charge nodale sur le nœud " + std::to_string(p.node) + " inexistant");
        if (!finite(p.fx) || !finite(p.fy) || !finite(p.mz)) r.errors.push_back(where + " : charge nodale non finie");
    }
    for (const auto& l : c.elementLoads)
    {
        const ElementDefinition* e = l.element > 0 ? model.element(l.element) : nullptr;
        if (l.element == 0 && l.type != ElementLoadType::SelfWeight)
        {
            r.errors.push_back(where + " : charge " + loadTypeName(l.type) + " sans élément (seul le poids propre accepte « tous les éléments »)");
            continue;
        }
        if (l.element != 0 && !e)
        {
            r.errors.push_back(where + " : charge " + loadTypeName(l.type) + " sur l'élément " + std::to_string(l.element) + " inexistant");
            continue;
        }
        if (!finite(l.q1) || !finite(l.q2) || !finite(l.a) || !finite(l.b) || !finite(l.deltaT) || !finite(l.gradientT)
            || !finite(l.epsilon0) || !finite(l.kappa0) || !finite(l.factor))
            r.errors.push_back(where + " : charge non finie sur l'élément " + std::to_string(l.element));
        const bool positioned = l.type == ElementLoadType::Distributed || l.type == ElementLoadType::PointForce || l.type == ElementLoadType::PointMoment;
        if (positioned && e)
        {
            const Node2D* ni = model.node(e->nodeI);
            const Node2D* nj = model.node(e->nodeJ);
            if (ni && nj)
            {
                const double L = std::hypot(nj->x - ni->x, nj->y - ni->y);
                const double scale = l.relative ? 1.0 : L;
                const double tol = 1e-9 * std::max(1.0, scale);
                const double b = l.type == ElementLoadType::Distributed && l.b < 0.0 ? scale : l.b;
                if (l.a < -tol || l.a > scale + tol)
                    r.errors.push_back(where + " : position a = " + num(l.a) + (l.relative ? "" : " m") + " hors de l'élément " + std::to_string(l.element));
                if (l.type == ElementLoadType::Distributed && (b < l.a - tol || b > scale + tol))
                    r.errors.push_back(where + " : charge répartie de " + num(l.a) + " à " + num(b) + " hors de l'élément " + std::to_string(l.element));
            }
        }
        if (l.projected && (l.direction == LoadDirection::LocalX || l.direction == LoadDirection::LocalY))
            r.errors.push_back(where + " : projection demandée pour une direction locale (élément " + std::to_string(l.element) + ")");
        auto checkElement = [&](const ElementDefinition& def) {
            if (l.type == ElementLoadType::Thermal)
            {
                if ((l.deltaT != 0.0 || l.gradientT != 0.0) && !(def.material.alpha > 0.0))
                    r.errors.push_back(where + " : action thermique sur l'élément " + std::to_string(def.id) + " sans coefficient de dilatation");
                if (l.gradientT != 0.0 && !(def.section.h > 0.0))
                    r.errors.push_back(where + " : gradient thermique sur l'élément " + std::to_string(def.id) + " sans hauteur de section");
            }
            if (l.type == ElementLoadType::SelfWeight && !(def.material.unitWeight > 0.0))
                r.errors.push_back(where + " : poids propre de l'élément " + std::to_string(def.id) + " sans poids volumique");
        };
        if (e) checkElement(*e);
        else if (l.type == ElementLoadType::SelfWeight)
            for (const auto& def : model.elements) checkElement(def);
    }
    for (const auto& d : c.imposedDisplacements)
    {
        const SupportCondition* s = model.support(d.node);
        if (!model.node(d.node)) r.errors.push_back(where + " : déplacement imposé au nœud " + std::to_string(d.node) + " inexistant");
        else if (d.component < 0 || d.component > 2) r.errors.push_back(where + " : composante de déplacement imposé invalide");
        else if (!s || !(d.component == 0 ? s->fixUx : d.component == 1 ? s->fixUy : s->fixRz))
            r.errors.push_back(where + " : déplacement imposé " + componentName(d.component) + " au nœud " + std::to_string(d.node)
                               + " sur un DDL libre (un déplacement imposé s'applique à un DDL bloqué par un appui)");
        if (!finite(d.value)) r.errors.push_back(where + " : déplacement imposé non fini");
    }
}
} // namespace

ValidationReport validate(const Model2D& model)
{
    ValidationReport r;
    if (model.nodes.empty()) r.errors.push_back("aucun nœud");
    if (model.elements.empty()) r.errors.push_back("aucun élément");
    std::set<int> ids;
    for (const auto& n : model.nodes)
    {
        if (!ids.insert(n.id).second) r.errors.push_back("identifiant de nœud en double : " + std::to_string(n.id));
        if (!finite(n.x) || !finite(n.y)) r.errors.push_back("nœud " + std::to_string(n.id) + " : coordonnées non finies");
    }
    const double tol = 1e-9 * std::max(1.0, model.extent());
    ids.clear();
    for (const auto& e : model.elements)
    {
        const std::string name = "élément " + std::to_string(e.id);
        if (!ids.insert(e.id).second) r.errors.push_back("identifiant d'élément en double : " + std::to_string(e.id));
        const Node2D* ni = model.node(e.nodeI);
        const Node2D* nj = model.node(e.nodeJ);
        if (!ni || !nj)
        {
            r.errors.push_back(name + " : nœud " + std::to_string(!ni ? e.nodeI : e.nodeJ) + " inexistant");
            continue;
        }
        if (e.nodeI == e.nodeJ || std::hypot(nj->x - ni->x, nj->y - ni->y) <= tol)
            r.errors.push_back(name + " : longueur nulle (nœuds " + std::to_string(e.nodeI) + " et " + std::to_string(e.nodeJ) + ")");
        if (!(e.material.E > 0.0) || !finite(e.material.E)) r.errors.push_back(name + " : module d'Young E non positif");
        if (!(e.section.A > 0.0) || !finite(e.section.A)) r.errors.push_back(name + " : aire A non positive");
        if (!(e.section.Iz > 0.0) || !finite(e.section.Iz)) r.errors.push_back(name + " : moment quadratique Iz non positif");
    }
    ids.clear();
    for (const auto& s : model.supports)
    {
        const std::string who = "appui du nœud " + std::to_string(s.node);
        if (!model.node(s.node)) r.errors.push_back("appui sur le nœud " + std::to_string(s.node) + " inexistant");
        if (!ids.insert(s.node).second) r.errors.push_back("plusieurs appuis sur le nœud " + std::to_string(s.node));
        if (!finite(s.kUx) || !finite(s.kUy) || !finite(s.kRz) || s.kUx < 0.0 || s.kUy < 0.0 || s.kRz < 0.0)
            r.errors.push_back(who + " : rigidité de ressort négative ou non finie");
        if (!s.fixUx && !s.fixUy && !s.fixRz && !s.hasSpring()) r.warnings.push_back(who + " sans DDL bloqué ni ressort");
        if ((s.fixUx && s.kUx > 0.0) || (s.fixUy && s.kUy > 0.0) || (s.fixRz && s.kRz > 0.0))
            r.warnings.push_back(who + " : ressort sur un DDL bloqué, sans effet (le DDL bloqué reprend tout)");
    }
    for (const auto& m : model.nodalMasses)
    {
        if (!model.node(m.node)) r.errors.push_back("masse sur le nœud " + std::to_string(m.node) + " inexistant");
        if (!finite(m.mx) || !finite(m.my) || !finite(m.jz) || m.mx < 0.0 || m.my < 0.0 || m.jz < 0.0)
            r.errors.push_back("masse du nœud " + std::to_string(m.node) + " négative ou non finie");
    }
    for (const auto& e : model.elements)
        if (!finite(e.material.density) || e.material.density < 0.0)
            r.errors.push_back("élément " + std::to_string(e.id) + " : masse volumique négative ou non finie");
    for (const auto& n : model.nodes)
        if (model.rotationFree(n.id))
            r.warnings.push_back("nœud " + std::to_string(n.id) + " : toutes les barres y sont articulées, sans appui en rotation — rotation éliminée du système");
    for (const auto& n : model.nodes)
        if (model.elementCount(n.id) == 0) r.warnings.push_back("nœud " + std::to_string(n.id) + " relié à aucun élément");
    ids.clear();
    for (const auto& c : model.loadCases)
    {
        if (!ids.insert(c.id).second) r.errors.push_back("identifiant de cas de charge en double : " + std::to_string(c.id));
        validateLoadCase(model, c, r);
    }
    ids.clear();
    for (const auto& k : model.combinations)
    {
        if (!ids.insert(k.id).second) r.errors.push_back("identifiant de combinaison en double : " + std::to_string(k.id));
        if (k.factors.empty()) r.errors.push_back("combinaison " + std::to_string(k.id) + " vide");
        for (const auto& [cid, f] : k.factors)
        {
            if (!model.loadCase(cid)) r.errors.push_back("combinaison " + std::to_string(k.id) + " : cas " + std::to_string(cid) + " inexistant");
            if (!finite(f)) r.errors.push_back("combinaison " + std::to_string(k.id) + " : coefficient non fini");
        }
    }
    return r;
}

bool resolveLoads(const Model2D& model, const LoadSelection& selection, LoadCase& out, std::string* error)
{
    auto fail = [error](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    out = LoadCase {};
    if (selection.combinationId > 0)
    {
        const LoadCombination* k = model.combination(selection.combinationId);
        if (!k) return fail("combinaison " + std::to_string(selection.combinationId) + " inexistante");
        out.id = -k->id;
        out.name = k->name.empty() ? "combinaison " + std::to_string(k->id) : k->name;
        for (const auto& [cid, f] : k->factors)
        {
            const LoadCase* c = model.loadCase(cid);
            if (!c) return fail("combinaison " + std::to_string(k->id) + " : cas " + std::to_string(cid) + " inexistant");
            out.append(*c, f);
        }
    }
    else
    {
        const LoadCase* c = model.loadCase(selection.caseId);
        if (!c) return fail("cas de charge " + std::to_string(selection.caseId) + " inexistant");
        out = *c;
    }
    // Poids propre « tous les éléments » développé élément par élément
    std::vector<ElementLoad> expanded;
    for (const auto& l : out.elementLoads)
    {
        if (l.type == ElementLoadType::SelfWeight && l.element == 0)
            for (const auto& e : model.elements)
            {
                ElementLoad x = l;
                x.element = e.id;
                expanded.push_back(x);
            }
        else
            expanded.push_back(l);
    }
    out.elementLoads = std::move(expanded);
    return true;
}

} // namespace tsalab::structural
