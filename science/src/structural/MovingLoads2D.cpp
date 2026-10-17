// Charges roulantes sur portiques plans : succession d'analyses statiques, enveloppes, lignes d'influence
// (voir MovingLoads2D.h).

#include "tsalab/structural/MovingLoads2D.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <sstream>

namespace tsalab::structural
{

namespace
{
constexpr double kMaxStoredValues = 5e6;

std::string num(double v, int precision = 6)
{
    std::ostringstream o;
    o.precision(precision);
    o << v;
    return o.str();
}

struct Segment
{
    std::size_t index = 0;   ///< position de l'élément dans model.elements
    double length = 0.0;
    bool forward = true;     ///< parcouru de i vers j
    double start = 0.0;      ///< abscisse du début du segment sur le chemin
};

/// Données communes à toutes les positions : numérotation, K factorisée, chemin, charges permanentes.
struct Engine
{
    const Model2D& model;
    DofNumbering dofs;
    BoundaryConditions bc;
    Matrix K, Kfc;
    Vector Fbase, dc;
    numerics::CholeskyFactorization solver;
    std::vector<Segment> path;
    double pathLength = 0.0;
    std::vector<FrameElement2D> elements;
    std::vector<std::array<int, 6>> elementDofs;
    std::vector<std::vector<LocalLoad>> baseLocal;

    explicit Engine(const Model2D& m) : model(m) {}

    bool setup(const std::vector<int>& pathIds, int baseCase, std::string& err, std::vector<std::string>& log)
    {
        const ValidationReport v = validate(model);
        if (!v.ok())
        {
            err = "Modèle invalide : " + v.errors.front();
            return false;
        }
        for (const auto& w : v.warnings) log.push_back("Avertissement : " + w);
        dofs = numberDofs(model);
        LoadCase base;
        if (baseCase != 0)
        {
            if (!resolveLoads(model, LoadSelection { baseCase, 0 }, base, &err)) return false;
            log.push_back("Cas de charge superposé à chaque position : " + std::to_string(baseCase));
        }
        if (!boundaryConditions(model, dofs, base, bc, &err) || !assembleStiffness(model, dofs, K, &err) || !assembleLoads(model, dofs, base, Fbase, &err))
            return false;
        if (bc.freeDofs.empty())
        {
            err = "Aucun DDL libre : tous les déplacements sont bloqués.";
            return false;
        }
        ReducedSystem rs;
        if (!reduce(K, Fbase, bc, rs, &err)) return false;
        Kfc = rs.Kfc;
        dc = rs.dc;
        if (!solver.factor(rs.Kff))
        {
            const DisplacementSolve s = solveDisplacements(rs, bc, dofs, &model);   // diagnostic du mécanisme
            err = "Structure instable : " + (s.success ? solver.message : s.message);
            return false;
        }
        for (const auto& def : model.elements)
        {
            FrameElement2D el;
            if (!makeElement(model, def, el, &err)) return false;
            std::vector<std::string> issues;
            baseLocal.push_back(el.localLoads(base.elementLoads, &issues));
            if (!issues.empty())
            {
                err = "élément " + std::to_string(def.id) + " : " + issues.front();
                return false;
            }
            elements.push_back(el);
            elementDofs.push_back(dofs.elementDofs(def.nodeI, def.nodeJ));
        }
        // Chemin : chaîne continue d'éléments distincts
        if (pathIds.empty())
        {
            err = "chemin de roulement vide : indiquer les éléments parcourus";
            return false;
        }
        int entry = 0;
        for (std::size_t k = 0; k < pathIds.size(); ++k)
        {
            const ElementDefinition* e = model.element(pathIds[k]);
            if (!e)
            {
                err = "chemin : élément " + std::to_string(pathIds[k]) + " inexistant";
                return false;
            }
            if (std::count(pathIds.begin(), pathIds.end(), pathIds[k]) > 1)
            {
                err = "chemin : élément " + std::to_string(pathIds[k]) + " répété";
                return false;
            }
            Segment seg;
            seg.index = static_cast<std::size_t>(e - model.elements.data());
            seg.length = elements[seg.index].length();
            if (k == 0)
            {
                // Sens du premier élément : sortie par le nœud partagé avec le suivant (i → j s'il est seul)
                bool forward = true;
                if (pathIds.size() > 1)
                {
                    const ElementDefinition* next = model.element(pathIds[1]);
                    if (next && (e->nodeI == next->nodeI || e->nodeI == next->nodeJ)) forward = false;
                }
                seg.forward = forward;
            }
            else if (e->nodeI == entry)
                seg.forward = true;
            else if (e->nodeJ == entry)
                seg.forward = false;
            else
            {
                err = "chemin discontinu : l'élément " + std::to_string(pathIds[k]) + " ne touche pas le nœud " + std::to_string(entry) + " où s'arrête l'élément "
                      + std::to_string(pathIds[k - 1]);
                return false;
            }
            entry = seg.forward ? e->nodeJ : e->nodeI;
            seg.start = pathLength;
            pathLength += seg.length;
            path.push_back(seg);
        }
        return true;
    }

    /// Élément et abscisse locale (depuis son nœud i) d'un point du chemin ; faux hors du chemin.
    bool locate(double s, std::size_t& index, double& a) const
    {
        const double tol = 1e-9 * std::max(1.0, pathLength);
        if (s < -tol || s > pathLength + tol) return false;
        s = std::clamp(s, 0.0, pathLength);
        for (const auto& seg : path)
            if (s <= seg.start + seg.length + tol || &seg == &path.back())
            {
                const double t = std::clamp(s - seg.start, 0.0, seg.length);
                index = seg.index;
                a = seg.forward ? t : seg.length - t;
                return true;
            }
        return false;
    }

    /// Déplacements, réactions et charges locales par élément pour des forces (s, P) dirigées suivant −Y.
    bool solve(const std::vector<std::pair<double, double>>& forces, Vector& d, Vector& R, std::vector<std::vector<LocalLoad>>& local, std::string& err)
    {
        Vector F = Fbase;
        local = baseLocal;
        for (const auto& [s, P] : forces)
        {
            std::size_t idx = 0;
            double a = 0.0;
            if (!locate(s, idx, a)) continue;
            std::vector<std::string> issues;
            const std::vector<LocalLoad> ll =
                elements[idx].localLoads({ pointForce(elements[idx].id(), -P, a, LoadDirection::GlobalY) }, &issues);
            if (!issues.empty())
            {
                err = issues.front();
                return false;
            }
            local[idx].insert(local[idx].end(), ll.begin(), ll.end());
            const Vector feq = elements[idx].equivalentNodalLoadsGlobal(ll);
            for (int k = 0; k < 6; ++k) F[static_cast<std::size_t>(elementDofs[idx][static_cast<std::size_t>(k)])] += feq[static_cast<std::size_t>(k)];
        }
        Vector rhs(bc.freeDofs.size());
        const Vector kd = numerics::LinAlg::multiply(Kfc, dc);
        for (std::size_t i = 0; i < bc.freeDofs.size(); ++i) rhs[i] = F[static_cast<std::size_t>(bc.freeDofs[i])] - kd[i];
        const Vector df = solver.solve(rhs);
        if (!fullDisplacements(bc, df, dofs.size(), d, &err) || !reactions(K, d, F, R, &err)) return false;
        for (int k : bc.freeDofs) R[static_cast<std::size_t>(k)] = 0.0;
        for (int k : bc.eliminatedDofs) R[static_cast<std::size_t>(k)] = 0.0;
        return true;
    }

    Vector elementDisplacements(std::size_t idx, const Vector& d) const
    {
        Vector de(6);
        for (int k = 0; k < 6; ++k) de[static_cast<std::size_t>(k)] = d[static_cast<std::size_t>(elementDofs[idx][static_cast<std::size_t>(k)])];
        return de;
    }
};

std::vector<double> positionsAlong(double length, double step)
{
    std::vector<double> s;
    const int count = static_cast<int>(std::floor(length / step + 1e-9));
    for (int k = 0; k <= count; ++k) s.push_back(k * step);
    if (length - s.back() > 1e-9 * std::max(1.0, length)) s.push_back(length);
    return s;
}
} // namespace

double Vehicle::length() const
{
    double l = 0.0;
    for (const auto& a : axles) l = std::max(l, a.offset);
    return l;
}

bool Vehicle::valid(std::string* error) const
{
    auto fail = [error](const std::string& m) {
        if (error) *error = m;
        return false;
    };
    if (axles.empty()) return fail("convoi sans essieu");
    bool loaded = false;
    for (std::size_t k = 0; k < axles.size(); ++k)
    {
        if (!std::isfinite(axles[k].offset) || axles[k].offset < 0.0) return fail("essieu " + std::to_string(k + 1) + " : décalage négatif ou non fini");
        if (!std::isfinite(axles[k].load)) return fail("essieu " + std::to_string(k + 1) + " : charge non finie");
        loaded = loaded || axles[k].load != 0.0;
    }
    if (!loaded) return fail("convoi sans charge");
    return true;
}

const EnvelopeStation* MovingLoadResult::maxMoment() const
{
    const EnvelopeStation* best = nullptr;
    for (const auto& s : stations)
        if (!best || s.Mmax > best->Mmax) best = &s;
    return best;
}

const EnvelopeStation* MovingLoadResult::minMoment() const
{
    const EnvelopeStation* best = nullptr;
    for (const auto& s : stations)
        if (!best || s.Mmin < best->Mmin) best = &s;
    return best;
}

MovingLoadResult movingLoadAnalysis(const Model2D& model, const Vehicle& vehicle, const MovingLoadOptions& o)
{
    MovingLoadResult r;
    r.model = model;
    r.vehicle = vehicle;
    std::string err;
    if (!vehicle.valid(&err))
    {
        r.message = "Convoi : " + err;
        return r;
    }
    if (!(o.step > 0.0) || !std::isfinite(o.step))
    {
        r.message = "pas d'avancement du convoi nul, négatif ou non fini";
        return r;
    }
    if (o.stations < 2)
    {
        r.message = "au moins 2 sections d'enveloppe par élément";
        return r;
    }
    Engine engine(model);
    if (!engine.setup(o.path, o.baseCase, err, r.log))
    {
        r.message = err;
        return r;
    }
    r.dofs = engine.dofs;
    r.bc = engine.bc;
    r.pathLength = engine.pathLength;
    r.positions = positionsAlong(engine.pathLength + vehicle.length(), o.step);
    const int n = r.dofs.size();
    if (static_cast<double>(r.positions.size()) * n > kMaxStoredValues)
    {
        r.message = std::to_string(r.positions.size()) + " positions × " + std::to_string(n) + " DDL : trop de valeurs à stocker (augmenter le pas)";
        return r;
    }
    const double inf = std::numeric_limits<double>::infinity();
    for (std::size_t e = 0; e < engine.elements.size(); ++e)
        for (int k = 0; k < o.stations; ++k)
        {
            EnvelopeStation s;
            s.element = engine.elements[e].id();
            s.x = engine.elements[e].length() * k / (o.stations - 1);
            s.Nmin = s.Vmin = s.Mmin = inf;
            s.Nmax = s.Vmax = s.Mmax = -inf;
            r.stations.push_back(s);
        }
    r.displacementMin.assign(static_cast<std::size_t>(n), inf);
    r.displacementMax.assign(static_cast<std::size_t>(n), -inf);
    r.reactionMin.assign(static_cast<std::size_t>(n), inf);
    r.reactionMax.assign(static_cast<std::size_t>(n), -inf);

    Vector d, R;
    std::vector<std::vector<LocalLoad>> local;
    for (double sf : r.positions)
    {
        std::vector<std::pair<double, double>> forces;
        for (const auto& a : vehicle.axles) forces.emplace_back(sf - a.offset, a.load);
        if (!engine.solve(forces, d, R, local, err))
        {
            r.message = "position " + num(sf) + " m : " + err;
            return r;
        }
        for (int k = 0; k < n; ++k)
        {
            const auto kk = static_cast<std::size_t>(k);
            r.displacementMin[kk] = std::min(r.displacementMin[kk], d[kk]);
            r.displacementMax[kk] = std::max(r.displacementMax[kk], d[kk]);
            r.reactionMin[kk] = std::min(r.reactionMin[kk], R[kk]);
            r.reactionMax[kk] = std::max(r.reactionMax[kk], R[kk]);
        }
        std::size_t st = 0;
        for (std::size_t e = 0; e < engine.elements.size(); ++e)
        {
            const Vector de = engine.elementDisplacements(e, d);
            for (int k = 0; k < o.stations; ++k, ++st)
            {
                EnvelopeStation& s = r.stations[st];
                for (bool at : { false, true })   // deux côtés d'une force placée sur la section
                {
                    const auto q = engine.elements[e].internalForcesAt(de, local[e], s.x, at);
                    s.Nmin = std::min(s.Nmin, q.N), s.Nmax = std::max(s.Nmax, q.N);
                    s.Vmin = std::min(s.Vmin, q.V), s.Vmax = std::max(s.Vmax, q.V);
                    if (q.M < s.Mmin) s.Mmin = q.M, s.sMmin = sf;
                    if (q.M > s.Mmax) s.Mmax = q.M, s.sMmax = sf;
                }
            }
        }
        r.displacements.push_back(d);
    }
    for (int k = 0; k < n; ++k)
        if (!engine.bc.isConstrained(k) || engine.bc.isEliminated(k)) r.reactionMin[static_cast<std::size_t>(k)] = r.reactionMax[static_cast<std::size_t>(k)] = 0.0;
    r.log.push_back("Convoi " + (vehicle.name.empty() ? std::string("sans nom") : "« " + vehicle.name + " »") + " : " + std::to_string(vehicle.axles.size())
                    + " essieu(x), longueur " + num(vehicle.length()) + " m ; chemin de " + num(engine.pathLength) + " m (" + std::to_string(o.path.size())
                    + " élément(s)) ; " + std::to_string(r.positions.size()) + " positions au pas de " + num(o.step) + " m");
    r.log.push_back("Succession d'analyses statiques linéaires : aucune inertie, aucune interaction véhicule-pont, aucun coefficient dynamique");
    const EnvelopeStation* mx = r.maxMoment();
    const EnvelopeStation* mn = r.minMoment();
    if (mx && mn)
        r.log.push_back("M max = " + num(mx->Mmax) + " kN·m (élément " + std::to_string(mx->element) + ", x = " + num(mx->x) + " m, premier essieu en "
                        + num(mx->sMmax) + " m) ; M min = " + num(mn->Mmin) + " kN·m (élément " + std::to_string(mn->element) + ", x = " + num(mn->x) + " m)");
    r.success = true;
    r.message = "Charges roulantes : " + std::to_string(r.positions.size()) + " positions" + (mx ? ", M max = " + num(mx->Mmax) + " kN·m" : std::string()) + ".";
    return r;
}

InfluenceLine influenceLine(const Model2D& model, const std::vector<int>& path, double step, const ResponseQuantity& q)
{
    InfluenceLine r;
    if (!(step > 0.0) || !std::isfinite(step))
    {
        r.message = "pas nul, négatif ou non fini";
        return r;
    }
    Engine engine(model);
    std::string err;
    std::vector<std::string> log;
    if (!engine.setup(path, 0, err, log))
    {
        r.message = err;
        return r;
    }
    using K = ResponseQuantity::Kind;
    int dof = -1;
    std::size_t elementIndex = 0;
    if (q.kind == K::Displacement || q.kind == K::Reaction)
    {
        dof = engine.dofs.dof(q.node, q.component);
        if (dof < 0 || q.component < 0 || q.component > 2)
        {
            r.message = "nœud " + std::to_string(q.node) + " ou composante " + std::to_string(q.component) + " inexistant";
            return r;
        }
        if (q.kind == K::Reaction && (!engine.bc.isConstrained(dof) || engine.bc.isEliminated(dof)))
        {
            r.message = "réaction demandée sur " + engine.dofs.label(dof) + " : ce DDL n'est pas bloqué par un appui";
            return r;
        }
        r.quantity = std::string(q.kind == K::Displacement ? "déplacement " : "réaction ") + engine.dofs.label(dof);
    }
    else
    {
        const ElementDefinition* e = model.element(q.element);
        if (!e)
        {
            r.message = "élément " + std::to_string(q.element) + " inexistant";
            return r;
        }
        elementIndex = static_cast<std::size_t>(e - model.elements.data());
        const double L = engine.elements[elementIndex].length();
        if (q.x < -1e-9 * L || q.x > L * (1.0 + 1e-9))
        {
            r.message = "section x = " + num(q.x) + " m hors de l'élément (L = " + num(L) + " m)";
            return r;
        }
        r.quantity = std::string(q.kind == K::AxialForce ? "N" : (q.kind == K::ShearForce ? "V" : "M")) + " élément " + std::to_string(q.element) + ", x = " + num(q.x)
                     + " m";
    }
    Vector d, R;
    std::vector<std::vector<LocalLoad>> local;
    r.s = positionsAlong(engine.pathLength, step);
    for (double s : r.s)
    {
        if (!engine.solve({ { s, 1.0 } }, d, R, local, err))
        {
            r.message = "position " + num(s) + " m : " + err;
            r.s.clear();
            r.value.clear();
            return r;
        }
        double v = 0.0;
        switch (q.kind)
        {
        case K::Displacement: v = d[static_cast<std::size_t>(dof)]; break;
        case K::Reaction: v = R[static_cast<std::size_t>(dof)]; break;
        default:
        {
            const double x = std::clamp(q.x, 0.0, engine.elements[elementIndex].length());
            const auto f = engine.elements[elementIndex].internalForcesAt(engine.elementDisplacements(elementIndex, d), local[elementIndex], x, false);
            v = q.kind == K::AxialForce ? f.N : (q.kind == K::ShearForce ? f.V : f.M);
        }
        }
        r.value.push_back(v);
    }
    const auto [lo, hi] = std::minmax_element(r.value.begin(), r.value.end());
    r.minimum = *lo, r.maximum = *hi;
    r.sMinimum = r.s[static_cast<std::size_t>(lo - r.value.begin())];
    r.sMaximum = r.s[static_cast<std::size_t>(hi - r.value.begin())];
    r.success = true;
    r.message = "Ligne d'influence de " + r.quantity + " : " + std::to_string(r.s.size()) + " positions, de " + num(r.minimum) + " à " + num(r.maximum) + ".";
    return r;
}

} // namespace tsalab::structural
