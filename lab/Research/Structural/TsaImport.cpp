// Import d'un projet TSA vers Model2D par le chemin d'analyse plane de TSA (voir TsaImport.h).

#include "TsaImport.h"

#include "Analysis/Engine/AnalysisEngineRegistry.h"
#include "Analysis/Engine/AnalysisManager.h"
#include "Analysis/Engines/Custom2D/Custom2DAdapter.h"
#include "Core/Units.h"
#include "Grid/GridManager.h"
#include "IO/TSAFile.h"
#include "Model/Model.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <map>
#include <sstream>

namespace TSALab::Research::Structural
{

namespace
{
namespace A = TSA::Analysis;
namespace C2 = TSA::Analysis::Custom2D;

constexpr char kModelPlane[] = "Plan du modèle (détecté)";

std::string num(double v, int precision = 6)
{
    std::ostringstream o;
    o.precision(precision);
    o << v;
    return o.str();
}

QString qpath(const std::string& p)
{
    return QString::fromUtf8(p.c_str());
}

std::string lowerAscii(std::string s)
{
    for (char& c : s)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

/// Portées planes : plan du modèle puis celles proposées par TSA (axes, niveaux, plans de travail).
std::vector<std::pair<std::string, A::AnalysisScope>> planarScopes(const TSA::Model::Model& model, const TSA::Grid::GridManager& grids)
{
    std::vector<std::pair<std::string, A::AnalysisScope>> out;
    A::AnalysisScope plane;
    plane.type = A::ScopeType::ModelPlane;
    out.emplace_back(kModelPlane, plane);
    for (const auto& o : A::AnalysisScopeResolver::availableScopes(model, &grids))
        if (o.scope.isPlanar() && o.scope.type != A::ScopeType::ModelPlane) out.emplace_back(o.label, o.scope);
    return out;
}

bool cancelled(const std::atomic<bool>* cancel, TsaImportReport& r)
{
    if (!cancel || !cancel->load()) return false;
    r.errors.push_back("import annulé par l'utilisateur");
    r.message = "Import annulé.";
    return true;
}

bool refuse(TsaImportReport& r, const std::string& message)
{
    r.errors.push_back(message);
    r.message = "Import refusé : " + message;
    r.success = false;
    return false;
}

std::string joined(const std::vector<std::string>& items, std::size_t max = 8)
{
    std::string s;
    for (std::size_t k = 0; k < items.size() && k < max; ++k) s += (k ? ", " : "") + items[k];
    if (items.size() > max) s += "… (" + std::to_string(items.size()) + " au total)";
    return s;
}
} // namespace

std::string TsaImportReport::text() const
{
    std::ostringstream o;
    o << message << "\n";
    o << "Fichier : " << path << "\n";
    if (!sha256.empty()) o << "SHA-256 : " << sha256 << " (identique avant et après l'import)\n";
    if (!formatVersion.empty()) o << "Format TSA : " << formatVersion << "\n";
    if (!projectName.empty()) o << "Projet : " << projectName << (author.empty() ? std::string() : " — " + author) << "\n";
    if (!scopeLabel.empty()) o << "Portée : " << scopeLabel << "\n";
    auto section = [&](const char* title, const std::vector<std::string>& lines) {
        if (lines.empty()) return;
        o << "\n" << title << " :\n";
        for (const auto& l : lines) o << "  • " << l << "\n";
    };
    section("Importé", imported);
    section("Converti", converted);
    section("Ignoré (non transmis)", ignored);
    section("Avertissements", warnings);
    section("Erreurs", errors);
    if (!scopes.empty())
    {
        o << "\nPortées planes disponibles :\n";
        for (const auto& s : scopes) o << "  • " << s << "\n";
    }
    return o.str();
}

std::string fileSha256(const std::string& path)
{
    QFile f(qpath(path));
    if (!f.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f)) return {};
    return h.result().toHex().toStdString();
}

bool listTsaScopes(const std::string& path, std::vector<std::string>& scopes, std::string* error)
{
    scopes.clear();
    TSA::Model::Model model;
    TSA::Grid::GridManager grids;
    QString err;
    if (!TSA::IO::TSAProjectIO::loadProject(qpath(path), model, &grids, nullptr, nullptr, nullptr, &err))
    {
        if (error) *error = "lecture du projet impossible : " + err.toStdString();
        return false;
    }
    for (const auto& s : planarScopes(model, grids)) scopes.push_back(s.first);
    return true;
}

bool importTsaProject(const std::string& path, const TsaImportOptions& options, S::Model2D& out, TsaImportReport& r, const std::atomic<bool>* cancel,
                      tsalab::planar::Input* planarInput)
{
    out = S::Model2D {};
    r = TsaImportReport {};
    r.path = path;

    // 1. Fichier : présence, empreinte, en-tête (version, chiffrement), lecture par TSA
    const QFileInfo info(qpath(path));
    if (path.empty() || !info.isFile()) return refuse(r, "fichier introuvable « " + path + " »");
    r.sha256 = fileSha256(path);
    if (r.sha256.empty()) return refuse(r, "fichier illisible « " + path + " »");
    TSA::IO::TSAFileHeader header;
    std::string headerError;
    if (!TSA::IO::TSAFileReader::readHeader(path, header, &headerError))
        return refuse(r, "en-tête TSA invalide : " + (headerError.empty() ? std::string("fichier non reconnu") : headerError));
    r.formatVersion = std::to_string(header.versionMajor) + "." + std::to_string(header.versionMinor) + " (TSA " + std::to_string(header.appVersionMajor) + "."
                      + std::to_string(header.appVersionMinor) + "." + std::to_string(header.appVersionPatch) + ")";
    if (header.flags & TSA::IO::FLAG_ENCRYPTED) return refuse(r, "projet chiffré par mot de passe : import non pris en charge");
    if (cancelled(cancel, r)) return false;

    TSA::Model::Model model;
    TSA::Grid::GridManager grids;
    QString name, author, err;
    if (!TSA::IO::TSAProjectIO::loadProject(qpath(path), model, &grids, &name, &author, nullptr, &err))
        return refuse(r, "lecture du projet par TSA impossible : " + (err.isEmpty() ? std::string("format non reconnu") : err.toStdString()));
    r.projectName = name.toStdString();
    r.author = author.toStdString();
    if (cancelled(cancel, r)) return false;

    // 2. Portée plane
    const auto scopes = planarScopes(model, grids);
    for (const auto& s : scopes) r.scopes.push_back(s.first);
    const std::string wanted = lowerAscii(options.scope);
    const A::AnalysisScope* scope = nullptr;
    for (const auto& s : scopes)
        if ((wanted.empty() || wanted == "plan") ? s.first == kModelPlane : lowerAscii(s.first) == wanted)
        {
            scope = &s.second;
            r.scopeLabel = s.first;
            break;
        }
    if (!scope) return refuse(r, "portée « " + options.scope + " » inconnue (portées planes : " + joined(r.scopes, 20) + ")");

    A::AnalysisContext context;
    context.engineId = "custom2d";
    context.dimension = A::AnalysisDimension::Plane2D;
    context.type = A::AnalysisType::LinearStatic;
    context.scope = *scope;
    A::AnalysisEngineRegistry registry;
    A::registerBuiltInEngines(registry);
    A::AnalysisManager manager(registry);
    const A::PreparedAnalysis prepared = manager.prepare(model, &grids, context);
    for (const auto& m : prepared.validation.messages())
    {
        const std::string line = (m.category.empty() ? std::string() : m.category + " : ") + m.text;
        if (m.severity == A::ValidationSeverity::Error) r.errors.push_back("TSA — " + line);
        else if (m.severity == A::ValidationSeverity::Warning) r.warnings.push_back("TSA — " + line);
    }
    if (!prepared.extracted) return refuse(r, "portée non extraite par TSA (" + joined(r.errors) + ")");
    if (!prepared.validation.isValid())
    {
        r.message = "Import refusé : la portée n'est pas calculable en 2D par TSA (" + std::to_string(prepared.validation.count(A::ValidationSeverity::Error))
                    + " erreur(s), voir ci-dessous).";
        return false;
    }
    if (!prepared.model.scopeLabel.empty()) r.scopeLabel = prepared.model.scopeLabel;
    if (cancelled(cancel, r)) return false;

    // 3. Conversion de TSA vers le contrat plan (Custom2D)
    A::ValidationResult diag;
    const C2::Input in = C2::buildInput(context, prepared.model, &diag);
    if (planarInput) *planarInput = in;
    for (const auto& m : diag.messages()) r.warnings.push_back("TSA (projection plane) — " + m.text);
    const auto& snap = prepared.model.snapshot;
    const auto& mapping = prepared.model.mapping;
    if (in.nodes.empty() || in.elements.empty()) return refuse(r, "portée sans barre : rien à importer");

    // Inventaire non transmis par l'extraction de TSA
    const auto& am = prepared.model;
    if (!am.slabs.empty()) r.ignored.push_back(std::to_string(am.slabs.size()) + " dalle(s) : éléments surfaciques, hors du modèle de barres");
    if (!am.walls.empty()) r.ignored.push_back(std::to_string(am.walls.size()) + " voile(s) : éléments surfaciques, hors du modèle de barres");
    if (!am.foundations.empty()) r.ignored.push_back(std::to_string(am.foundations.size()) + " fondation(s) non transmise(s)");
    if (!am.crossingElements.empty())
    {
        std::vector<std::string> labels;
        for (const auto& k : am.crossingElements) labels.push_back(k.label());
        r.ignored.push_back(std::to_string(labels.size()) + " barre(s) reliée(s) à la portée par une seule extrémité, ni rigidité ni charge : " + joined(labels));
    }
    if (!am.outOfPlaneNodes.empty()) r.ignored.push_back(std::to_string(am.outOfPlaneNodes.size()) + " nœud(s) hors du plan au-delà de la tolérance");
    if (am.droppedNodalLoads) r.ignored.push_back(std::to_string(am.droppedNodalLoads) + " charge(s) nodale(s) hors portée");
    if (am.droppedMemberLoads) r.ignored.push_back(std::to_string(am.droppedMemberLoads) + " charge(s) sur barre hors portée");

    // 4. Correspondance avec Model2D
    S::Model2D& m = out;
    m.name = r.projectName.empty() ? info.completeBaseName().toStdString() : r.projectName;
    for (const auto& n : in.nodes)
    {
        const int tsaId = mapping.tsaNode(n.index);
        m.nodes.push_back({ n.index, n.x, n.y, tsaId ? "TSA N" + std::to_string(tsaId) : std::string() });
        if (n.fixX || n.fixY || n.fixRz || n.kX > 0.0 || n.kY > 0.0 || n.kRz > 0.0)
        {
            m.supports.push_back({ n.index, n.fixX, n.fixY, n.fixRz, n.kX, n.kY, n.kRz });
            r.springs += (n.kX > 0.0) + (n.kY > 0.0) + (n.kRz > 0.0);
        }
    }
    std::vector<std::string> trussLabels, cableLabels;
    std::map<int, double> weightPerLength;   // élément → poids linéique (kN/m) de TSA
    for (const auto& e : in.elements)
    {
        const auto it = snap.elements().find(e.index);
        if (it == snap.elements().end()) return refuse(r, "élément d'analyse " + std::to_string(e.index) + " sans élément TSA correspondant");
        const A::SnapshotElement& se = it->second;
        if (se.type == A::StructuralElementKind::Cable)
        {
            cableLabels.push_back(se.key().label());
            continue;
        }
        S::ElementDefinition d;
        d.id = e.index;
        d.nodeI = e.nodeI;
        d.nodeJ = e.nodeJ;
        d.name = se.key().label();
        d.material.name = se.material.name;
        d.material.E = e.E;                                              // kPa (Custom2D : Pa × 1e-3)
        d.material.alpha = se.material.thermalCoeff;                     // 1/K
        d.material.density = se.material.density / 1000.0;              // kg/m³ → t/m³
        d.material.unitWeight = se.material.density * TSA::Units::kGravity / 1000.0;   // γ = ρ·g (g de TSA), kN/m³
        d.section.name = se.section.name;
        d.section.A = e.A;
        if (e.type == C2::ElementType::Truss)
        {
            // Barre de treillis : effort normal seul ≡ barre bi-articulée ; son inertie n'intervient ni dans K (rotules aux
            // deux extrémités : flexion condensée) ni dans les déplacements nodaux ; Iz de la section, pour la déformée.
            d.releaseI = d.releaseJ = true;
            d.section.Iz = std::max(se.section.iy(), se.section.iz());
            if (!(d.section.Iz > 0.0)) return refuse(r, "barre de treillis " + d.name + " : section sans inertie (" + se.section.name + ")");
            trussLabels.push_back(d.name);
        }
        else
        {
            d.section.Iz = e.I;
            d.releaseI = e.releaseI;
            d.releaseJ = e.releaseJ;
            r.releases += (e.releaseI ? 1 : 0) + (e.releaseJ ? 1 : 0);
        }
        weightPerLength[d.id] = e.weightPerLength;
        m.elements.push_back(d);
    }
    if (!cableLabels.empty())
        return refuse(r, std::to_string(cableLabels.size()) + " câble(s) (" + joined(cableLabels)
                             + ") : élément en traction seule, non linéaire, sans équivalent dans le modèle linéaire de TSALab — aucune "
                               "conversion silencieuse ; choisir une portée sans câble");
    if (!trussLabels.empty())
    {
        r.trussBars = trussLabels.size();
        r.converted.push_back(std::to_string(trussLabels.size()) + " barre(s) de treillis → barre(s) bi-articulée(s) (rotules aux deux extrémités, effort "
                              "normal seul ; Iz de la section sans effet sur K) : " + joined(trussLabels));
    }
    if (cancelled(cancel, r)) return false;

    // Cas de charge, poids propre, charges nodales et sur barres, combinaisons
    const bool gravityDown = std::abs(in.request.gravityX) < 1e-9 && std::abs(in.request.gravityY + 1.0) < 1e-9;
    std::size_t selfWeightCases = 0;
    for (const auto& lc : in.loadCases)
    {
        S::LoadCase c;
        c.id = lc.id;
        c.name = lc.name;
        if (lc.includeSelfWeight && in.request.includeSelfWeight)
        {
            ++selfWeightCases;
            for (const auto& [id, w] : weightPerLength)
            {
                if (!(w > 0.0)) continue;
                if (gravityDown)
                    c.elementLoads.push_back(S::selfWeight(id, lc.selfWeightFactor));
                else
                {
                    if (in.request.gravityX != 0.0) c.elementLoads.push_back(S::uniformLoad(id, w * lc.selfWeightFactor * in.request.gravityX, S::LoadDirection::GlobalX));
                    if (in.request.gravityY != 0.0) c.elementLoads.push_back(S::uniformLoad(id, w * lc.selfWeightFactor * in.request.gravityY, S::LoadDirection::GlobalY));
                }
            }
        }
        m.loadCases.push_back(std::move(c));
    }
    if (selfWeightCases && !gravityDown)
        r.converted.push_back("poids propre de " + std::to_string(selfWeightCases) + " cas : pesanteur projetée dans le plan (" + num(in.request.gravityX) + ", "
                              + num(in.request.gravityY) + ") ≠ (0, −1) → charges réparties globales w·g (kN/m)");
    auto caseOf = [&](int id) -> S::LoadCase* {
        for (auto& c : m.loadCases)
            if (c.id == id) return &c;
        return nullptr;
    };
    std::size_t orphan = 0;
    for (const auto& l : in.nodalLoads)
    {
        S::LoadCase* c = caseOf(l.loadCaseId);
        if (!c)
        {
            ++orphan;
            continue;
        }
        c->nodalLoads.push_back({ l.node, l.fx, l.fy, l.mz });
        ++r.nodalLoads;
    }
    std::vector<std::string> loadedTruss;
    for (const auto& l : in.memberLoads)
    {
        S::LoadCase* c = caseOf(l.loadCaseId);
        const S::ElementDefinition* d = m.element(l.element);
        if (!c || !d)
        {
            ++orphan;
            continue;
        }
        if (d->releaseI && d->releaseJ && std::find(trussLabels.begin(), trussLabels.end(), d->name) != trussLabels.end()
            && std::find(loadedTruss.begin(), loadedTruss.end(), d->name) == loadedTruss.end())
            loadedTruss.push_back(d->name);
        if (l.kind == C2::MemberLoadKind::Distributed)
        {
            if (l.px1 != 0.0 || l.px2 != 0.0) c->elementLoads.push_back(S::linearLoad(l.element, l.px1, l.px2, l.a, l.b, S::LoadDirection::LocalX));
            if (l.py1 != 0.0 || l.py2 != 0.0) c->elementLoads.push_back(S::linearLoad(l.element, l.py1, l.py2, l.a, l.b, S::LoadDirection::LocalY));
        }
        else
        {
            if (l.px1 != 0.0) c->elementLoads.push_back(S::pointForce(l.element, l.px1, l.a, S::LoadDirection::LocalX));
            if (l.py1 != 0.0) c->elementLoads.push_back(S::pointForce(l.element, l.py1, l.a, S::LoadDirection::LocalY));
        }
        ++r.memberLoads;
    }
    if (orphan) r.ignored.push_back(std::to_string(orphan) + " charge(s) d'un cas absent ou d'une barre non importée");
    if (!loadedTruss.empty())
        r.warnings.push_back("charge sur barre de treillis (" + joined(loadedTruss) + ") : reprise en flexion simple par la barre bi-articulée (un treillis "
                             "idéal ne porte que des charges nodales)");
    for (const auto& cb : in.combinations)
    {
        S::LoadCombination c;
        c.id = cb.id;
        c.name = cb.name;
        for (const auto& [caseId, f] : cb.factors) c.factors.emplace_back(caseId, f);
        m.combinations.push_back(std::move(c));
    }

    // Traçabilité
    m.metadata = {
        { "source", "projet TSA" },
        { "fichier", path },
        { "sha256", r.sha256 },
        { "format", r.formatVersion },
        { "projet", r.projectName },
        { "auteur", r.author },
        { "portée", r.scopeLabel },
        { "conversion", "TSAProjectIO::loadProject → AnalysisManager::prepare (custom2d, Plane2D) → Custom2D::buildInput → Model2D" },
        { "date d'import", QDateTime::currentDateTime().toString(Qt::ISODate).toStdString() },
        { "nœuds", "identifiant = indice d'analyse ; nom = nœud TSA" },
        { "éléments", "identifiant = indice d'analyse ; nom = élément TSA" },
    };

    // 5. Contrôle du modèle obtenu
    const S::ValidationReport v = S::validate(m);
    for (const auto& w : v.warnings) r.warnings.push_back("Model2D — " + w);
    if (!v.ok())
    {
        for (const auto& e : v.errors) r.errors.push_back("Model2D — " + e);
        r.message = "Import refusé : modèle obtenu invalide (" + std::to_string(v.errors.size()) + " erreur(s)).";
        out = S::Model2D {};
        return false;
    }
    if (fileSha256(path) != r.sha256) return refuse(r, "le fichier source a changé pendant l'import");

    r.nodes = m.nodes.size();
    r.elements = m.elements.size();
    r.supports = m.supports.size();
    r.loadCases = m.loadCases.size();
    r.combinations = m.combinations.size();
    r.imported.push_back(std::to_string(r.nodes) + " nœud(s), " + std::to_string(r.elements) + " barre(s), " + std::to_string(r.supports) + " appui(s) dont "
                         + std::to_string(r.springs) + " ressort(s), " + std::to_string(r.releases) + " rotule(s) d'extrémité");
    r.imported.push_back(std::to_string(r.loadCases) + " cas de charge, " + std::to_string(r.combinations) + " combinaison(s), " + std::to_string(r.nodalLoads)
                         + " charge(s) nodale(s), " + std::to_string(r.memberLoads) + " charge(s) sur barre" + (selfWeightCases ? ", poids propre de "
                         + std::to_string(selfWeightCases) + " cas" : std::string()));
    r.imported.push_back("unités : kN, m, kN/m² (E du projet en Pa × 10⁻³), masses volumiques en t/m³ ; plan (x, y) = repère (u, v) de la portée");
    r.success = true;
    r.message = "Import réussi : « " + m.name + " », portée « " + r.scopeLabel + " » (" + std::to_string(r.nodes) + " nœuds, " + std::to_string(r.elements)
                + " barres).";
    return true;
}

} // namespace TSALab::Research::Structural
