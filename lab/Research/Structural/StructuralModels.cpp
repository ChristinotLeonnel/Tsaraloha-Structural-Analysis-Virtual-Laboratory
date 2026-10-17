#include "StructuralModels.h"

#include "Automation/CommandRegistry.h"
#include "Core/Units.h"
#include "Model/Material.h"
#include "Model/Section.h"

namespace TSALab::Research::Structural
{

bool catalogSection(const std::string& profile, S::Section2D& out, std::string* error)
{
    TSA::Model::Section s;
    if (!TSA::Automation::catalogSection(profile, s, error)) return false;
    out.name = s.name.empty() ? profile : s.name;
    out.A = s.area();
    out.Iz = s.iy();      // flexion autour de l'axe fort : dans le plan du portique
    out.h = s.height;
    return true;
}

bool catalogMaterial(const std::string& name, S::Material& out, std::string* error)
{
    TSA::Model::Material m;
    if (!TSA::Automation::catalogMaterial(name, m))
    {
        if (error) *error = "matériau inconnu « " + name + " » (S235, S355, C25/30, C30/37, C24)";
        return false;
    }
    out.name = m.name.empty() ? name : m.name;
    out.E = m.E / 1000.0;                                          // Pa → kN/m²
    out.alpha = m.thermalCoeff;                                    // 1/K = 1/°C
    out.unitWeight = m.density * TSA::Units::kGravity / 1000.0;     // kg/m³ × m/s² → kN/m³
    out.density = m.density / 1000.0;                               // kg/m³ → t/m³ (dynamique)
    return true;
}

bool demoPortal(const PortalParameters& p, S::Model2D& m, std::string* error)
{
    S::Section2D column, beam;
    S::Material material;
    if (!catalogSection(p.column, column, error) || !catalogSection(p.beam, beam, error) || !catalogMaterial(p.material, material, error)) return false;
    if (!(p.span > 0.0) || !(p.height > 0.0))
    {
        if (error) *error = "portée et hauteur doivent être positives";
        return false;
    }
    m = S::Model2D {};
    m.name = "Portique de démonstration";
    m.nodes = { { 1, 0.0, 0.0, "pied gauche" }, { 2, 0.0, p.height, "tête gauche" }, { 3, p.span, p.height, "tête droite" }, { 4, p.span, 0.0, "pied droit" } };
    m.elements = { { 1, 1, 2, material, column, "poteau gauche" }, { 2, 2, 3, material, beam, "traverse" }, { 3, 4, 3, material, column, "poteau droit" } };
    m.supports = { { 1, true, true, true }, { 4, true, true, true } };
    S::LoadCase g { 1, "G", {}, { S::uniformLoad(2, p.q), S::selfWeight() }, {} };
    S::LoadCase w { 2, "W", { { 2, p.H, 0.0, 0.0 } }, { S::uniformLoad(1, 0.2 * p.H / p.height, S::LoadDirection::GlobalX) }, {} };
    S::LoadCase t { 3, "T", {}, {}, { { 4, 1, -p.settlement } } };
    m.loadCases = { g, w, t };
    m.combinations = { { 1, "ELU 6.10", { { 1, 1.35 }, { 2, 1.5 } } } };
    return true;
}

S::Model2D unstableBeam()
{
    S::Section2D s;
    S::Material mat;
    catalogSection("IPE 300", s);
    catalogMaterial("S235", mat);
    S::Model2D m;
    m.name = "Poutre sur deux appuis simples (instable)";
    m.nodes = { { 1, 0.0, 0.0, {} }, { 2, 6.0, 0.0, {} } };
    m.elements = { { 1, 1, 2, mat, s, {} } };
    m.supports = { { 1, false, true, false }, { 2, false, true, false } };
    m.loadCases = { { 1, "q", {}, { S::uniformLoad(1, -10.0) }, {} } };
    return m;
}

S::Model2D settlementBeam()
{
    S::Section2D s;
    S::Material mat;
    catalogSection("IPE 300", s);
    catalogMaterial("S235", mat);
    S::Model2D m;
    m.name = "Poutre continue, tassement d'appui";
    m.nodes = { { 1, 0.0, 0.0, {} }, { 2, 5.0, 0.0, {} }, { 3, 10.0, 0.0, {} } };
    m.elements = { { 1, 1, 2, mat, s, {} }, { 2, 2, 3, mat, s, {} } };
    m.supports = { { 1, true, true, false }, { 2, false, true, false }, { 3, false, true, false } };
    m.loadCases = { { 1, "q", {}, { S::uniformLoad(1, -10.0), S::uniformLoad(2, -10.0) }, {} }, { 2, "tassement", {}, {}, { { 2, 1, -0.005 } } } };
    return m;
}

} // namespace TSALab::Research::Structural
