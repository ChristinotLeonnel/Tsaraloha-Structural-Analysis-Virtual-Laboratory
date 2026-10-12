#include "AnalyticalReference.h"

#include "Analysis/Engine/AnalysisContext.h"
#include "Analysis/ResultsModel.h"
#include "Model/Model.h"
#include "Model/Load/LoadManager.h"
#include "Model/Load/SurfaceLoad.h"
#include "Model/Slab.h"

#include <QJsonArray>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <set>

namespace TSALab::Research
{

namespace
{
using namespace TSA::Model;

QJsonArray toArray(const QStringList& l) { return QJsonArray::fromStringList(l); }
QStringList fromArray(const QJsonValue& v)
{
    QStringList out;
    for (const auto& x : v.toArray()) out << x.toString();
    return out;
}

void finish(ReferenceCheck& c)
{
    if (!c.available) return;
    c.absError = std::abs(c.computed - c.reference);
    c.relError = std::abs(c.reference) > 1e-12 ? c.absError / std::abs(c.reference) : std::numeric_limits<double>::quiet_NaN();
    c.passed = c.absError <= c.tolAbs + c.tolRel * std::abs(c.reference);
}

bool includedCase(const TSA::Analysis::AnalysisContext& ctx, int caseId)
{
    return ctx.loadCaseIds.empty() || std::find(ctx.loadCaseIds.begin(), ctx.loadCaseIds.end(), caseId) != ctx.loadCaseIds.end();
}
} // namespace

bool ReferenceComparison::passed() const
{
    return compared() && std::all_of(checks.begin(), checks.end(), [](const ReferenceCheck& c) { return c.available && c.passed; });
}

QJsonObject ReferenceComparison::toJson() const
{
    QJsonArray cs;
    for (const auto& c : checks)
        cs.append(QJsonObject { { "quantity", c.quantity }, { "label", c.label }, { "formula", c.formula }, { "unit", c.unit },
                                { "reference", c.reference }, { "computed", c.computed }, { "absError", c.absError },
                                { "relError", std::isnan(c.relError) ? QJsonValue() : QJsonValue(c.relError) },
                                { "tolRel", c.tolRel }, { "tolAbs", c.tolAbs }, { "available", c.available }, { "passed", c.passed } });
    return QJsonObject { { "problem", problemId }, { "title", title }, { "applicable", applicable },
                         { "hypotheses", toArray(hypotheses) }, { "violations", toArray(violations) },
                         { "parameters", parameters }, { "checks", cs }, { "warnings", toArray(warnings) } };
}

ReferenceComparison ReferenceComparison::fromJson(const QJsonObject& o)
{
    ReferenceComparison r;
    r.problemId = o.value("problem").toString();
    r.title = o.value("title").toString();
    r.applicable = o.value("applicable").toBool();
    r.hypotheses = fromArray(o.value("hypotheses"));
    r.violations = fromArray(o.value("violations"));
    r.parameters = o.value("parameters").toObject();
    r.warnings = fromArray(o.value("warnings"));
    for (const auto& v : o.value("checks").toArray())
    {
        const QJsonObject c = v.toObject();
        ReferenceCheck k;
        k.quantity = c.value("quantity").toString();
        k.label = c.value("label").toString();
        k.formula = c.value("formula").toString();
        k.unit = c.value("unit").toString();
        k.reference = c.value("reference").toDouble();
        k.computed = c.value("computed").toDouble();
        k.absError = c.value("absError").toDouble();
        k.relError = c.value("relError").isDouble() ? c.value("relError").toDouble() : std::numeric_limits<double>::quiet_NaN();
        k.tolRel = c.value("tolRel").toDouble(1e-6);
        k.tolAbs = c.value("tolAbs").toDouble(1e-9);
        k.available = c.value("available").toBool();
        k.passed = c.value("passed").toBool();
        r.checks.push_back(k);
    }
    return r;
}

ReferenceComparison simplySupportedCentralLoad(const Model& model, const TSA::Analysis::AnalysisContext& ctx, const TSA::Analysis::ResultsModel* results)
{
    ReferenceComparison r;
    r.problemId = QStringLiteral("simply-supported-central-load");
    r.title = QStringLiteral("Poutre d'Euler-Bernoulli sur deux appuis simples, force ponctuelle centrale");
    auto fail = [&r](const QString& why) { r.violations << why; };

    // --- Éléments : uniquement des barres fléchies (poutres, poteaux) --------------------------------------------
    if (!model.trussMembers().empty() || !model.cables().empty() || !model.slabs().empty() || !model.walls().empty() || !model.foundations().empty())
        fail(QStringLiteral("le modèle contient d'autres éléments que des poutres (treillis, câbles, dalles, voiles ou fondations)"));
    struct Bar
    {
        int a, b;
        BarProperties p;
    };
    std::vector<Bar> bars;
    for (const auto& [id, bm] : model.beams()) bars.push_back({ bm.startNodeId(), bm.endNodeId(), bm.properties() });
    for (const auto& [id, c] : model.columns()) bars.push_back({ c.startNodeId(), c.endNodeId(), c.properties() });
    if (bars.empty())
    {
        fail(QStringLiteral("aucune poutre dans le modèle"));
        return r;
    }

    // --- Appuis : exactement deux nœuds appuyés -----------------------------------------------------------------
    std::vector<int> supports;
    for (const auto& [id, n] : model.nodes())
        if (n.support().isSupported()) supports.push_back(id);
    if (supports.size() != 2)
    {
        fail(QStringLiteral("%1 nœud(s) appuyé(s) au lieu de 2").arg(supports.size()));
        return r;
    }
    const Node* A = model.getNode(supports[0]);
    const Node* B = model.getNode(supports[1]);
    if (A->x() > B->x()) std::swap(A, B);
    const double dx = B->x() - A->x(), dy = B->y() - A->y(), dz = B->z() - A->z();
    const double L = std::sqrt(dx * dx + dy * dy + dz * dz);
    if (L <= 0.0)
    {
        fail(QStringLiteral("les deux appuis sont confondus"));
        return r;
    }
    const double ux = dx / L, uy = dy / L, uz = dz / L;
    if (std::abs(uz) > 1e-9) fail(QStringLiteral("poutre non horizontale (référence automatique : poutre horizontale, charge verticale)"));
    r.hypotheses << QStringLiteral("deux appuis : nœuds %1 et %2, portée L = %3 m").arg(A->id()).arg(B->id()).arg(L, 0, 'g', 10);

    // --- Barres alignées de A à B, longueur totale L, même section, matériau, orientation, sans relâchement ------
    auto onSegment = [&](const Node* n, double* t) {
        const double px = n->x() - A->x(), py = n->y() - A->y(), pz = n->z() - A->z();
        *t = px * ux + py * uy + pz * uz;
        const double ex = px - *t * ux, ey = py - *t * uy, ez = pz - *t * uz;
        return std::sqrt(ex * ex + ey * ey + ez * ez) <= 1e-9 * L && *t >= -1e-9 * L && *t <= L * (1 + 1e-9);
    };
    double total = 0.0;
    bool aligned = true, uniform = true, released = false;
    for (const auto& br : bars)
    {
        const Node* n1 = model.getNode(br.a);
        const Node* n2 = model.getNode(br.b);
        double t1 = 0, t2 = 0;
        if (!n1 || !n2 || !onSegment(n1, &t1) || !onSegment(n2, &t2)) aligned = false;
        total += std::abs(t2 - t1);
        const auto& p0 = bars.front().p;
        if (br.p.section.name != p0.section.name || std::abs(br.p.section.iy() - p0.section.iy()) > 1e-15 || br.p.material.E != p0.material.E ||
            std::abs(br.p.rotation - p0.rotation) > 1e-9)
            uniform = false;
        if (br.p.startRelease.my || br.p.startRelease.mz || br.p.endRelease.my || br.p.endRelease.mz || br.p.startRelease.fz || br.p.endRelease.fz)
            released = true;
    }
    if (!aligned) fail(QStringLiteral("des barres ne sont pas sur la droite joignant les deux appuis"));
    else if (std::abs(total - L) > 1e-9 * L) fail(QStringLiteral("les barres ne couvrent pas exactement la portée (longueur totale %1 m pour L = %2 m)").arg(total).arg(L));
    if (!uniform) fail(QStringLiteral("section, matériau ou orientation différents d'une barre à l'autre (EI non constant)"));
    if (released) fail(QStringLiteral("relâchement de flexion ou d'effort tranchant sur une barre"));
    const BarProperties& p = bars.front().p;
    const double rot = std::fmod(std::abs(p.rotation), 180.0);
    const bool strong = rot < 1e-9 || std::abs(rot - 180.0) < 1e-9;
    const bool weak = std::abs(rot - 90.0) < 1e-9;
    if (!strong && !weak) fail(QStringLiteral("orientation de section de %1° (référence automatique : 0° ou 90°)").arg(p.rotation));
    const double I = strong ? p.section.iy() : p.section.iz();
    const double Epa = p.material.E;
    r.hypotheses << QStringLiteral("%1 barre(s) alignée(s), section %2 (I = %3 m⁴, flexion autour de l'axe %4), E = %5 Pa, sans relâchement")
                        .arg(bars.size())
                        .arg(QString::fromStdString(p.section.name))
                        .arg(I, 0, 'g', 8)
                        .arg(strong ? QStringLiteral("fort") : QStringLiteral("faible"))
                        .arg(Epa, 0, 'g', 6);

    // --- Appuis simples : translation verticale bloquée, rotation de flexion libre ------------------------------
    for (const Node* s : { A, B })
    {
        const auto& sd = s->support();
        if (sd.tz() != DOFState::Fixed) fail(QStringLiteral("appui du nœud %1 : translation verticale non bloquée").arg(s->id()));
        // Poutre selon X (resp. Y) : la flexion sous charge verticale tourne autour de Y (resp. X)
        const DOFState bend = std::abs(ux) >= std::abs(uy) ? sd.ry() : sd.rx();
        if (bend != DOFState::Free) fail(QStringLiteral("appui du nœud %1 : rotation de flexion bloquée (ce n'est pas un appui simple)").arg(s->id()));
    }
    r.hypotheses << QStringLiteral("appuis simples : translation verticale bloquée, rotation de flexion libre");

    // --- Chargement : une force verticale au nœud de mi-portée, rien d'autre -------------------------------------
    if (ctx.combinationId > 0) fail(QStringLiteral("calcul d'une combinaison (référence : un cas de charge)"));
    if (ctx.common.includeSelfWeight) fail(QStringLiteral("poids propre inclus dans le calcul : la formule P·L³/(48·E·I) ne le contient pas"));
    const auto& lm = model.loadManager();
    for (const auto& [id, ml] : lm.memberLoads())
        if (includedCase(ctx, ml.loadCaseId())) fail(QStringLiteral("charge sur barre présente (référence : force ponctuelle au nœud de mi-portée)"));
    for (const auto& [id, sl] : lm.surfaceLoads())
        if (includedCase(ctx, sl.loadCaseId)) fail(QStringLiteral("charge surfacique présente"));
    double Fz = 0.0, other = 0.0;
    std::set<int> loadedNodes;
    for (const auto& [id, nl] : lm.nodalLoads())
    {
        if (!includedCase(ctx, nl.loadCaseId())) continue;
        loadedNodes.insert(nl.nodeId());
        Fz += nl.fz();
        other += std::abs(nl.fx()) + std::abs(nl.fy()) + std::abs(nl.mx()) + std::abs(nl.my()) + std::abs(nl.mz());
    }
    int C = 0;
    if (loadedNodes.size() != 1) fail(QStringLiteral("%1 nœud(s) chargé(s) au lieu d'un seul").arg(loadedNodes.size()));
    else
    {
        C = *loadedNodes.begin();
        double t = 0;
        const Node* nc = model.getNode(C);
        if (!nc || !onSegment(nc, &t) || std::abs(t - L / 2) > 1e-9 * L) fail(QStringLiteral("la force n'est pas appliquée au nœud de mi-portée"));
    }
    if (other > 1e-12) fail(QStringLiteral("la charge a des composantes autres que verticales (Fx, Fy ou moments)"));
    if (std::abs(Fz) <= 1e-12) fail(QStringLiteral("force verticale nulle"));
    r.hypotheses << QStringLiteral("une force verticale P = %1 kN au nœud %2 (mi-portée), poids propre exclu").arg(std::abs(Fz), 0, 'g', 10).arg(C);
    r.hypotheses << QStringLiteral("élément d'Euler-Bernoulli (cisaillement transverse négligé), élasticité linéaire, petits déplacements");

    r.parameters = QJsonObject { { "L_m", L }, { "E_Pa", Epa }, { "I_m4", I }, { "P_kN", std::abs(Fz) }, { "supportA", A->id() }, { "supportB", B->id() }, { "midNode", C } };
    r.applicable = r.violations.isEmpty();
    if (!r.applicable) return r;

    // --- Valeurs de référence (unités du calcul : kN et m → E en kN/m²) -----------------------------------------
    const bool kN = !results || results->units().force != "N";
    const double E = kN ? Epa / 1000.0 : Epa;
    const double P = kN ? Fz : Fz * 1000.0;   // force signée (vers le bas : négative)
    const QString fu = kN ? QStringLiteral("kN") : QStringLiteral("N"), mu = kN ? QStringLiteral("kN·m") : QStringLiteral("N·m");
    auto make = [](QString q, QString label, QString formula, QString unit, double ref) {
        ReferenceCheck c;
        c.quantity = q, c.label = label, c.formula = formula, c.unit = unit, c.reference = ref;
        return c;
    };
    r.checks.push_back(make(QStringLiteral("reaction:%1:fz").arg(A->id()), QStringLiteral("réaction verticale en A"), QStringLiteral("−P/2"), fu, -P / 2.0));
    r.checks.push_back(make(QStringLiteral("reaction:%1:fz").arg(B->id()), QStringLiteral("réaction verticale en B"), QStringLiteral("−P/2"), fu, -P / 2.0));
    r.checks.push_back(make(QStringLiteral("node:%1:uz").arg(C), QStringLiteral("flèche à mi-portée"), QStringLiteral("P·L³/(48·E·I)"), QStringLiteral("m"), P * L * L * L / (48.0 * E * I)));
    r.checks.push_back(make(QStringLiteral("member:M:absmax"), QStringLiteral("moment fléchissant maximal"), QStringLiteral("|P|·L/4"), mu, std::abs(P) * L / 4.0));
    r.checks.push_back(make(QStringLiteral("member:V:absmax"), QStringLiteral("effort tranchant maximal"), QStringLiteral("|P|/2"), fu, std::abs(P) / 2.0));
    if (!results) return r;

    // --- Valeurs calculées --------------------------------------------------------------------------------------
    for (auto& c : r.checks)
    {
        if (c.quantity.startsWith("reaction:"))
        {
            const int id = c.quantity.section(':', 1, 1).toInt();
            if (const auto* rr = results->getNodeReaction(id)) c.computed = rr->rz, c.available = true;
        }
        else if (c.quantity.startsWith("node:"))
        {
            if (const auto* d = results->getNodeDisplacement(C)) c.computed = d->uz, c.available = true;
        }
        else
        {
            const bool moment = c.quantity == "member:M:absmax";
            double best = -1.0;
            for (const auto& [key, er] : results->allElementResults())
            {
                std::vector<TSA::Analysis::StationForces> st { er.startForces };
                st.insert(st.end(), er.intermediateStations.begin(), er.intermediateStations.end());
                st.push_back(er.endForces);
                for (const auto& s : st) best = std::max(best, moment ? std::hypot(s.My, s.Mz) : std::hypot(s.Vy, s.Vz));
            }
            if (best >= 0.0) c.computed = best, c.available = true;
        }
        finish(c);
        if (!c.available) r.warnings << QStringLiteral("%1 : grandeur non fournie par le moteur (non disponible, non remplacée)").arg(c.label);
    }
    return r;
}

ReferenceComparison squarePlateHardSupport(const Model& model, const TSA::Analysis::AnalysisContext& ctx, const TSA::Analysis::ResultsModel* results)
{
    ReferenceComparison r;
    r.problemId = QStringLiteral("square-plate-hard-support");
    r.title = QStringLiteral("Plaque carrée, appui simple dur, pression uniforme (Mindlin)");
    auto fail = [&](const QString& why) { r.violations << why; };

    if (!model.beams().empty() || !model.trussMembers().empty() || !model.cables().empty() || !model.walls().empty() || !model.foundations().empty())
        fail(QStringLiteral("le modèle contient d'autres éléments que la dalle (barres, voiles, fondations)"));
    if (model.slabs().size() != 1)
    {
        fail(QStringLiteral("une seule dalle attendue (%1 dans le modèle)").arg(model.slabs().size()));
        return r;
    }
    const auto& [slabId, slab] = *model.slabs().begin();
    if (slab.formulation() != SurfaceFormulation::Shell) fail(QStringLiteral("la dalle n'est pas calculée en coque"));
    if (!slab.openings().empty()) fail(QStringLiteral("la dalle a des ouvertures"));
    const auto& ids = slab.nodeIds();
    if (ids.size() != 4)
    {
        fail(QStringLiteral("la dalle n'a pas quatre sommets"));
        return r;
    }
    std::vector<const Node*> p;
    for (int id : ids) p.push_back(model.getNode(id));
    if (std::any_of(p.begin(), p.end(), [](const Node* n) { return !n; }))
    {
        fail(QStringLiteral("sommet de dalle introuvable"));
        return r;
    }
    const double a = std::hypot(p[1]->x() - p[0]->x(), p[1]->y() - p[0]->y());
    for (int k = 0; k < 4; ++k)
    {
        const Node* u = p[k];
        const Node* v = p[(k + 1) % 4];
        const double dx = v->x() - u->x(), dy = v->y() - u->y();
        if (std::abs(u->z() - p[0]->z()) > 1e-9) fail(QStringLiteral("la dalle n'est pas horizontale"));
        if (std::abs(std::hypot(dx, dy) - a) > 1e-9 * a) fail(QStringLiteral("la dalle n'est pas carrée"));
        if (std::abs(dx) > 1e-9 * a && std::abs(dy) > 1e-9 * a) fail(QStringLiteral("la dalle n'est pas alignée sur les axes X et Y"));
    }
    r.violations.removeDuplicates();
    if (!r.violations.isEmpty()) return r;

    // Appuis de bord : translations et rotation tangentielle bloquées, rotation de flexion libre, aucune raideur
    for (int e = 0; e < 4; ++e)
    {
        std::array<bool, 6> fixed {};
        bool spring = false;
        for (const auto& sp : slab.edgeSupports())
            if (sp.edge == -1 || sp.edge == e)
                for (int d = 0; d < 6; ++d) fixed[d] = fixed[d] || sp.fixed[d], spring = spring || sp.stiffness[d] > 0.0;
        const bool alongX = std::abs(p[(e + 1) % 4]->x() - p[e]->x()) > 1e-9 * a;
        const int tangential = alongX ? 4 : 3, bending = alongX ? 3 : 4;   // ry pour un bord selon X, rx pour un bord selon Y
        if (!fixed[0] || !fixed[1] || !fixed[2]) fail(QStringLiteral("bord %1 : translations non toutes bloquées").arg(e + 1));
        if (!fixed[tangential]) fail(QStringLiteral("bord %1 : rotation tangentielle libre (appui « mou », hors du domaine de la formule)").arg(e + 1));
        if (fixed[bending]) fail(QStringLiteral("bord %1 : rotation de flexion bloquée (encastrement)").arg(e + 1));
        if (spring) fail(QStringLiteral("bord %1 : appui élastique").arg(e + 1));
    }

    // Chargement
    const auto& lm = model.loadManager();
    double q = 0.0;
    int pressures = 0;
    for (const auto& [lid, sl] : lm.surfaceLoads())
    {
        if (!includedCase(ctx, sl.loadCaseId)) continue;
        ++pressures;
        if (sl.targetKind != SurfaceTargetKind::Slab || sl.surfaceId != slabId || sl.distribution != SurfaceLoadDistribution::Uniform ||
            sl.direction != SurfaceLoadDirection::Gravity || sl.zone || sl.gradU != 0.0 || sl.gradV != 0.0)
            fail(QStringLiteral("charge surfacique non uniforme, partielle ou non verticale"));
        q += sl.value;
    }
    if (pressures != 1) fail(QStringLiteral("une seule pression uniforme attendue (%1 dans les cas calculés)").arg(pressures));
    for (const auto& [nid, nl] : lm.nodalLoads())
        if (includedCase(ctx, nl.loadCaseId())) fail(QStringLiteral("charges nodales présentes"));
    if (ctx.combinationId > 0) fail(QStringLiteral("calcul d'une combinaison (cas de charge attendu)"));
    if (ctx.common.includeSelfWeight) fail(QStringLiteral("poids propre inclus (la formule suppose la seule pression q)"));
    if (ctx.surfaces.empty() || !ctx.common.useGeneratedMesh) fail(QStringLiteral("maillage non transmis au calcul : la dalle n'est pas calculée"));
    if (std::abs(q) <= 1e-12) fail(QStringLiteral("pression nulle"));
    r.violations.removeDuplicates();
    if (!r.violations.isEmpty()) return r;

    // Valeurs de référence (kN, m)
    const bool kN = !results || results->units().force != "N";
    const double E = slab.material().E / 1000.0, nu = slab.material().nu, e = slab.thickness();
    const double D = E * e * e * e / (12.0 * (1.0 - nu * nu)), G = E / (2.0 * (1.0 + nu)), kappa = 5.0 / 6.0;
    const double alpha = 0.004062352660675069, beta = 0.07367135326539033;
    const double w = -(alpha * q * std::pow(a, 4) / D + beta * q * a * a / (kappa * G * e));
    r.applicable = true;
    r.hypotheses << QStringLiteral("dalle carrée a = %1 m, e = %2 m, horizontale, coque sans ouverture").arg(a, 0, 'g', 10).arg(e, 0, 'g', 10)
                 << QStringLiteral("appui simple dur sur les quatre bords (translations et rotation tangentielle bloquées)")
                 << QStringLiteral("pression uniforme q = %1 kN/m², poids propre exclu").arg(q, 0, 'g', 10)
                 << QStringLiteral("théorie de Mindlin, κ = 5/6 (ElasticMembranePlateSection) ; %1").arg(QString::fromStdString(ctx.meshLabel));
    r.parameters = QJsonObject { { "a_m", a }, { "e_m", e }, { "E_kPa", E }, { "nu", nu }, { "q_kPa", q }, { "mesh", QString::fromStdString(ctx.meshLabel) } };

    ReferenceCheck cw;
    cw.quantity = QStringLiteral("surface:uz:absmax"), cw.label = QStringLiteral("flèche au centre"), cw.formula = QStringLiteral("α·q·a⁴/D + β·q·a²/(κ·G·e)");
    cw.unit = QStringLiteral("m"), cw.reference = w, cw.tolRel = 1e-2;
    ReferenceCheck cr;
    cr.quantity = QStringLiteral("reactionsum:fz"), cr.label = QStringLiteral("somme des réactions"), cr.formula = QStringLiteral("q·a²");
    cr.unit = kN ? QStringLiteral("kN") : QStringLiteral("N"), cr.reference = q * a * a * (kN ? 1.0 : 1000.0);
    if (results && results->isValid())
    {
        double best = 0.0;
        for (const auto& [id, d] : results->surfaceNodeDisplacements())
            if (std::abs(d.uz) > std::abs(best)) best = d.uz;
        if (!results->surfaceNodeDisplacements().empty()) cw.computed = best, cw.available = true;
        cr.computed = results->equilibrium().reactionFz, cr.available = true;
    }
    for (auto* c : { &cw, &cr })
    {
        finish(*c);
        if (results && !c->available) r.warnings << QStringLiteral("%1 : grandeur non fournie par le moteur (non disponible, non remplacée)").arg(c->label);
        r.checks.push_back(*c);
    }
    return r;
}

ReferenceComparison analyticalReference(const Model& model, const TSA::Analysis::AnalysisContext& ctx, const TSA::Analysis::ResultsModel* results)
{
    return model.slabs().empty() ? simplySupportedCentralLoad(model, ctx, results) : squarePlateHardSupport(model, ctx, results);
}

} // namespace TSALab::Research
