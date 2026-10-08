#include "ExampleModels.h"

#include "Model/Model.h"
#include "Model/Load/LoadCase.h"
#include "Model/Load/MemberLoad.h"
#include "Model/Load/NodalLoad.h"

namespace TSALab::Research::Examples
{

using namespace TSA::Model;

namespace
{

int addCase(Model& model, const std::string& name, LoadCaseCategory category = LoadCaseCategory::Live)
{
    return model.loadManager().addLoadCase(LoadCase(0, name, category));
}

// Console IPE 300 de 4 m selon X, encastrée en x = 0, P = 10 kN vers le bas à l'extrémité.
void buildCantilever(Model& model)
{
    const int a = model.addNode(0, 0, 0, "", "Encastrement");
    const int b = model.addNode(4, 0, 0, "", "Extrémité");
    model.getNode(a)->setSupport(SupportDefinition::fixed());
    model.addBar(a, b, Section::ipe(300), Material::steelS235(), BarRole::Beam, 0.0, "Console");
    const int lc = addCase(model, "P extrémité");
    model.loadManager().addNodalLoad(NodalLoad(0, b, lc, 0.0, 0.0, -10.0));
}

// Poutre bi-appuyée IPE 300 de 6 m (rotule + appui glissant), q = 10 kN/m.
void buildSimplySupported(Model& model)
{
    const int a = model.addNode(0, 0, 0, "", "Appui A");
    const int b = model.addNode(6, 0, 0, "", "Appui B");
    model.getNode(a)->setSupport(SupportDefinition::pinned());
    model.getNode(b)->setSupport(SupportDefinition::roller());
    const int bm = model.addBar(a, b, Section::ipe(300), Material::steelS235(), BarRole::Beam, 0.0, "Poutre");
    const int lc = addCase(model, "q uniforme");
    model.loadManager().addMemberLoad(MemberLoad::uniform(bm, lc, 10.0));
}

// Poutre bi-encastrée IPE 300 de 6 m, q = 10 kN/m.
void buildFixedFixed(Model& model)
{
    const int a = model.addNode(0, 0, 0, "", "Encastrement A");
    const int b = model.addNode(6, 0, 0, "", "Encastrement B");
    model.getNode(a)->setSupport(SupportDefinition::fixed());
    model.getNode(b)->setSupport(SupportDefinition::fixed());
    const int bm = model.addBar(a, b, Section::ipe(300), Material::steelS235(), BarRole::Beam, 0.0, "Poutre");
    const int lc = addCase(model, "q uniforme");
    model.loadManager().addMemberLoad(MemberLoad::uniform(bm, lc, 10.0));
}

// Portique plan (plan XZ) : poteaux HEB 200 de 3 m encastrés, traverse IPE 300 de 6 m.
void buildPortal(Model& model)
{
    const int a = model.addNode(0, 0, 0), b = model.addNode(0, 0, 3), c = model.addNode(6, 0, 3), d = model.addNode(6, 0, 0);
    model.getNode(a)->setSupport(SupportDefinition::fixed());
    model.getNode(d)->setSupport(SupportDefinition::fixed());
    model.addColumn(a, b, Section::heb(200), Material::steelS235(), 0.0, "Poteau gauche");
    const int bm = model.addBar(b, c, Section::ipe(300), Material::steelS235(), BarRole::Beam, 0.0, "Traverse");
    model.addColumn(d, c, Section::heb(200), Material::steelS235(), 0.0, "Poteau droit");
    const int lc = addCase(model, "Vent + exploitation");
    model.loadManager().addNodalLoad(NodalLoad(0, b, lc, 15.0, 0.0, 0.0));
    model.loadManager().addMemberLoad(MemberLoad::uniform(bm, lc, 12.0));
}

// Treillis de Warren (plan XZ) : 6 panneaux de 2 m, hauteur 1,5 m, barres articulées Ø 80 mm.
void buildWarrenTruss(Model& model)
{
    constexpr int panels = 6;
    constexpr double w = 2.0, h = 1.5;
    std::vector<int> bottom, top;
    for (int i = 0; i <= panels; ++i) bottom.push_back(model.addNode(i * w, 0, 0));
    for (int i = 0; i < panels; ++i) top.push_back(model.addNode((i + 0.5) * w, 0, h));
    // Appuis : rotule à gauche, appui glissant à droite ; le treillis plan est tenu hors plan.
    model.getNode(bottom.front())->setSupport(SupportDefinition::pinned());
    model.getNode(bottom.back())->setSupport(SupportDefinition::roller());
    std::vector<int> members;
    for (int i = 0; i < panels; ++i) members.push_back(model.addTrussMember(bottom[i], bottom[i + 1], 0.08, "Membrure inférieure", TrussMemberRole::BottomChord));
    for (int i = 0; i + 1 < panels; ++i) members.push_back(model.addTrussMember(top[i], top[i + 1], 0.08, "Membrure supérieure", TrussMemberRole::TopChord));
    for (int i = 0; i < panels; ++i)
    {
        members.push_back(model.addTrussMember(bottom[i], top[i], 0.08, "Diagonale", TrussMemberRole::Diagonal));
        members.push_back(model.addTrussMember(top[i], bottom[i + 1], 0.08, "Diagonale", TrussMemberRole::Diagonal));
    }
    for (int id : members)
        if (auto* t = model.getTrussMember(id)) t->setMaterial(Material::steelS235());
    const int lc = addCase(model, "Charges nodales");
    for (int i = 1; i < panels; ++i) model.loadManager().addNodalLoad(NodalLoad(0, bottom[i], lc, 0.0, 0.0, -10.0));
}

// Portique spatial à un niveau (2 × 1 travées) : 6 poteaux HEB 200, poutres IPE 300.
void buildFrame3D(Model& model)
{
    const double xs[3] = { 0.0, 5.0, 10.0 }, ys[2] = { 0.0, 4.0 }, zTop = 3.5;
    int base[3][2] = {}, head[3][2] = {};
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 2; ++j)
        {
            base[i][j] = model.addNode(xs[i], ys[j], 0);
            head[i][j] = model.addNode(xs[i], ys[j], zTop);
            model.getNode(base[i][j])->setSupport(SupportDefinition::fixed());
            model.addColumn(base[i][j], head[i][j], Section::heb(200), Material::steelS235());
        }
    const int lc = addCase(model, "Exploitation");
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i)
        {
            const int bm = model.addBar(head[i][j], head[i + 1][j], Section::ipe(300), Material::steelS235(), BarRole::Beam);
            model.loadManager().addMemberLoad(MemberLoad::uniform(bm, lc, 8.0));
        }
    for (int i = 0; i < 3; ++i) model.addBar(head[i][0], head[i][1], Section::ipe(300), Material::steelS235(), BarRole::Beam);
    model.loadManager().addNodalLoad(NodalLoad(0, head[0][0], lc, 10.0, 5.0, 0.0));
}

} // namespace

const std::vector<ExampleInfo>& catalog()
{
    static const std::vector<ExampleInfo> examples = {
        { "cantilever", "Console — charge d'extrémité",
          "IPE 300, L = 4 m, encastrée, P = 10 kN à l'extrémité.",
          "δ = P·L³ / (3·E·I) ; M_encastrement = P·L" },
        { "simply-supported", "Poutre bi-appuyée — charge uniforme",
          "IPE 300, L = 6 m, rotule + appui glissant, q = 10 kN/m.",
          "δ = 5·q·L⁴ / (384·E·I) ; M_max = q·L² / 8" },
        { "fixed-fixed", "Poutre bi-encastrée — charge uniforme",
          "IPE 300, L = 6 m, encastrée aux deux extrémités, q = 10 kN/m.",
          "M_appui = q·L² / 12 ; δ = q·L⁴ / (384·E·I)" },
        { "portal", "Portique plan",
          "Poteaux HEB 200 (3 m) encastrés, traverse IPE 300 (6 m), H = 15 kN, q = 12 kN/m.",
          "Comparaison de solveurs (Lab ↔ OpenSees ↔ Custom2D)" },
        { "warren-truss", "Treillis de Warren",
          "6 panneaux de 2 m, hauteur 1,5 m, barres articulées Ø 80 mm, 5 × 10 kN.",
          "Méthode des nœuds : efforts normaux seuls" },
        { "frame-3d", "Portique spatial",
          "2 × 1 travées (5 m × 4 m), un niveau de 3,5 m, poteaux HEB 200, poutres IPE 300.",
          "Comportement 3D : torsion, flexion bi-axiale, modes propres" },
    };
    return examples;
}

bool build(const std::string& id, Model& model)
{
    if (id == "cantilever") buildCantilever(model);
    else if (id == "simply-supported") buildSimplySupported(model);
    else if (id == "fixed-fixed") buildFixedFixed(model);
    else if (id == "portal") buildPortal(model);
    else if (id == "warren-truss") buildWarrenTruss(model);
    else if (id == "frame-3d") buildFrame3D(model);
    else return false;
    return true;
}

} // namespace TSALab::Research::Examples
