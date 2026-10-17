// Catégorie B — élément fini : fonctions de la classe FrameElement2D exposées une par une (géométrie, rigidités,
// transformation, charges nodales équivalentes, déplacements et efforts d'extrémité, diagrammes), opérateur
// d'assemblage A_e.

#include "NodeHelpers.h"

#include <cmath>

namespace TSALab::Research::Structural::detail
{

namespace
{
const std::vector<std::string> kLocal { "u'i", "v'i", "θi", "u'j", "v'j", "θj" };

/// Vecteur de 6 composantes en entrée (déplacements d'un élément).
const Vector* vector6(ExecutionContext& c, const char* pin)
{
    const LabeledVector* v = vectorIn(c, pin);
    if (!v) return nullptr;
    if (v->vector.size() != 6)
    {
        c.fail(std::string("entrée « ") + pin + " » : vecteur de 6 composantes attendu (DDL de l'élément), " + std::to_string(v->vector.size()) + " reçues");
        return nullptr;
    }
    return &v->vector;
}

/// Charges de l'élément en repère local (refus motivé d'une charge invalide).
bool localLoads(ExecutionContext& c, const S::FrameElement2D& e, const S::LoadCase& loads, std::vector<S::LocalLoad>& out)
{
    std::vector<std::string> errors;
    out = e.localLoads(loads.elementLoads, &errors);
    if (!errors.empty()) return c.fail(errors.front());
    return true;
}
} // namespace

void registerElementNodes(NodeLibrary& lib)
{
    lib.add(pureBlock("struct.element_geometry", "ElementGeometry", kElement, "Longueur, orientation et cosinus directeurs d'un élément.",
                      { object("element", "Élément", Type::Element) },
                      { data("length", "L (m)", VT::Real), data("angle", "α (°)", VT::Real), data("cos", "c = cos α", VT::Real), data("sin", "s = sin α", VT::Real) },
                      "L = √((xj − xi)² + (yj − yi)²) ; α = atan2(yj − yi, xj − xi), de X vers x' (sens trigonométrique) ; c = Δx/L, s = Δy/L.",
                      kSrcElement),
            [](ExecutionContext& c) {
                const S::FrameElement2D* e = elementIn(c);
                if (!e) return false;
                c.setOutput("length", e->length());
                c.setOutput("angle", e->angle() * 180.0 / std::acos(-1.0));
                c.setOutput("cos", e->cosine());
                c.setOutput("sin", e->sine());
                return true;
            });

    lib.add(pureBlock("struct.local_stiffness", "LocalStiffnessMatrix", kElement, "Matrice de rigidité locale k' (6×6) de l'élément.",
                      { object("element", "Élément", Type::Element) }, { object("k", "k'", Type::Matrix) },
                      "Portique plan élastique linéaire (Euler-Bernoulli), DDL locaux [u'i, v'i, θi, u'j, v'j, θj] :\n"
                      "  k'11 = k'44 = EA/L, k'14 = −EA/L ;\n"
                      "  k'22 = k'55 = 12EI/L³, k'25 = −12EI/L³ ;\n"
                      "  k'23 = k'26 = 6EI/L², k'35 = k'56 = −6EI/L² ;\n"
                      "  k'33 = k'66 = 4EI/L, k'36 = 2EI/L.\n"
                      "Symétrique, semi-définie positive (3 modes de corps rigide).",
                      kSrcElement),
            [](ExecutionContext& c) {
                const S::FrameElement2D* e = elementIn(c);
                if (!e) return false;
                c.setOutput("k", matrixOut(e->localStiffness(), kLocal, kLocal, "k' (élément " + std::to_string(e->id()) + ")"));
                return true;
            });

    lib.add(pureBlock("struct.transformation", "TransformationMatrix", kElement, "Matrice de transformation T (global → local) de l'élément.",
                      { object("element", "Élément", Type::Element) }, { object("T", "T", Type::Matrix) },
                      "d' = T·d, f = Tᵀ·f' ; par nœud : [c s 0 ; −s c 0 ; 0 0 1]. T est orthogonale (Tᵀ·T = I).", kSrcElement),
            [](ExecutionContext& c) {
                const S::FrameElement2D* e = elementIn(c);
                if (!e) return false;
                c.setOutput("T", matrixOut(e->transformation(), kLocal, elementDofLabels(*e), "T (élément " + std::to_string(e->id()) + ")"));
                return true;
            });

    lib.add(pureBlock("struct.global_element_stiffness", "GlobalElementStiffness", kElement, "Rigidité de l'élément dans le repère global : k = Tᵀ·k'·T.",
                      { object("element", "Élément", Type::Element) }, { object("k", "k", Type::Matrix) },
                      "k = Tᵀ·k'·T (6×6), DDL globaux de l'élément [ux_i, uy_i, θz_i, ux_j, uy_j, θz_j].", kSrcElement),
            [](ExecutionContext& c) {
                const S::FrameElement2D* e = elementIn(c);
                if (!e) return false;
                const auto labels = elementDofLabels(*e);
                c.setOutput("k", matrixOut(e->globalStiffness(), labels, labels, "k (élément " + std::to_string(e->id()) + ")"));
                return true;
            });

    lib.add(pureBlock("struct.equivalent_loads", "EquivalentNodalLoads", kElement,
                      "Charges nodales équivalentes aux charges de l'élément (efforts d'encastrement parfait changés de signe).",
                      { object("element", "Élément", Type::Element), object("loads", "Chargement", Type::LoadCase) },
                      { object("f_local", "f_eq local", Type::Vector), object("f_global", "f_eq global", Type::Vector),
                        object("fixed_end", "f0' encastrement", Type::Vector), data("count", "Charges sur l'élément", VT::Integer) },
                      "f0' = −∫ Nᵀ·p dx (efforts exercés sur l'élément bloqué à ses extrémités) : charges réparties (Gauss 3 points, exact), forces "
                      "ponctuelles (N(a)ᵀ·P), moments (N'(a)ᵀ·M), déformations initiales (EAε0, EIκ0). Charges nodales équivalentes : −f0' (local), "
                      "Tᵀ·(−f0') (global), à ajouter au vecteur F global. Exemples : charge uniforme q → f0' = [0, qL/2, qL²/12, 0, qL/2, −qL²/12] "
                      "(q vers +y').",
                      kSrcElement),
            [](ExecutionContext& c) {
                const S::FrameElement2D* e = elementIn(c);
                const S::LoadCase* loads = loadsIn(c);
                if (!e || !loads) return false;
                std::vector<S::LocalLoad> local;
                if (!localLoads(c, *e, *loads, local)) return false;
                const std::string id = std::to_string(e->id());
                c.setOutput("f_local", vectorOut(e->equivalentNodalLoadsLocal(local), kLocal, "f_eq local (élément " + id + ")"));
                c.setOutput("f_global", vectorOut(e->equivalentNodalLoadsGlobal(local), elementDofLabels(*e), "f_eq global (élément " + id + ")"));
                c.setOutput("fixed_end", vectorOut(e->fixedEndForces(local), kLocal, "f0' (élément " + id + ")"));
                c.setOutput("count", static_cast<long long>(local.size()));
                return true;
            });

    lib.add(pureBlock("struct.element_displacement", "ElementDisplacement", kElement,
                      "Déplacements des 6 DDL de l'élément extraits du vecteur global d, en repère global et local.",
                      { object("element", "Élément", Type::Element), object("d", "d global", Type::Vector), object("dofs", "DDL", Type::Dofs) },
                      { object("d_global", "d_e global", Type::Vector), object("d_local", "d'_e local", Type::Vector) },
                      "d_e = A_e·d (DDL de l'élément dans la numérotation globale), d'_e = T·d_e.", kSrcStiffness),
            [](ExecutionContext& c) {
                const S::FrameElement2D* e = elementIn(c);
                const LabeledVector* d = vectorIn(c, "d");
                const DofSet* dofs = dofsIn(c);
                if (!e || !d || !dofs) return false;
                if (static_cast<int>(d->vector.size()) != dofs->numbering.size())
                    return c.fail("d a " + std::to_string(d->vector.size()) + " composantes pour " + std::to_string(dofs->numbering.size()) + " DDL");
                const auto k = dofs->numbering.elementDofs(e->nodeI().id, e->nodeJ().id);
                Vector de(6);
                for (std::size_t a = 0; a < 6; ++a)
                {
                    if (k[a] < 0) return c.fail("nœud de l'élément " + std::to_string(e->id()) + " absent de la numérotation");
                    de[a] = d->vector[static_cast<std::size_t>(k[a])];
                }
                const std::string id = std::to_string(e->id());
                c.setOutput("d_global", vectorOut(de, elementDofLabels(*e), "d_e (élément " + id + ")"));
                c.setOutput("d_local", vectorOut(e->toLocal(de), kLocal, "d'_e (élément " + id + ")"));
                return true;
            });

    lib.add(pureBlock("struct.element_end_forces", "ElementEndForces", kElement,
                      "Efforts d'extrémité f' = k'·T·d_e + f0' et efforts intérieurs N, V, M aux deux extrémités.",
                      { object("element", "Élément", Type::Element), object("d_e", "d_e global (6)", Type::Vector), object("loads", "Chargement", Type::LoadCase) },
                      { object("f_local", "f' local", Type::Vector), object("f_global", "f global", Type::Vector), data("N_i", "N(0) (kN)", VT::Real),
                        data("V_i", "V(0) (kN)", VT::Real), data("M_i", "M(0) (kN·m)", VT::Real), data("N_j", "N(L) (kN)", VT::Real),
                        data("V_j", "V(L) (kN)", VT::Real), data("M_j", "M(L) (kN·m)", VT::Real) },
                      "f' : forces exercées SUR l'élément par ses nœuds, repère local [Fx_i, Fy_i, Mz_i, Fx_j, Fy_j, Mz_j]. Efforts intérieurs "
                      "(convention RDM) : N(0) = −Fx_i (traction > 0), V(0) = Fy_i, M(0) = −Mz_i ; N(L) = Fx_j, V(L) = −Fy_j, M(L) = Mz_j "
                      "(M > 0 tend la fibre y' < 0).",
                      kSrcElement),
            [](ExecutionContext& c) {
                const S::FrameElement2D* e = elementIn(c);
                const Vector* d = vector6(c, "d_e");
                const S::LoadCase* loads = loadsIn(c);
                if (!e || !d || !loads) return false;
                std::vector<S::LocalLoad> local;
                if (!localLoads(c, *e, *loads, local)) return false;
                const Vector f = e->endForces(*d, local);
                const std::string id = std::to_string(e->id());
                c.setOutput("f_local", vectorOut(f, kLocal, "f' (élément " + id + ")"));
                c.setOutput("f_global", vectorOut(e->toGlobal(f), elementDofLabels(*e), "f = Tᵀf' (élément " + id + ")"));
                c.setOutput("N_i", -f[0]);
                c.setOutput("V_i", f[1]);
                c.setOutput("M_i", -f[2]);
                c.setOutput("N_j", f[3]);
                c.setOutput("V_j", -f[4]);
                c.setOutput("M_j", f[5]);
                return true;
            });

    lib.add(pureBlock("struct.internal_forces", "InternalForceDiagram", kElement,
                      "Diagrammes N(x), V(x), M(x) et déformée u(x), v(x) de l'élément, avec leurs extremums.",
                      { object("element", "Élément", Type::Element), object("d_e", "d_e global (6)", Type::Vector), object("loads", "Chargement", Type::LoadCase),
                        data("points", "Points", VT::Integer, 21LL) },
                      { object("diagram", "Diagrammes", Type::Diagram), data("N_min", "N min", VT::Real), data("N_max", "N max", VT::Real),
                        data("V_min", "V min", VT::Real), data("V_max", "V max", VT::Real), data("M_min", "M min", VT::Real), data("M_max", "M max", VT::Real),
                        data("x_Mmax", "x de M max", VT::Real), data("deflection", "Flèche max (m)", VT::Real), data("closure", "Contrôle (m)", VT::Real) },
                      "Équilibre du tronçon [0, x] : N = −Fx_i − Px(x), V = Fy_i + Py(x), M = −Mz_i + x·Fy_i + MPy(x) − ΣM0 (intégrales exactes des "
                      "charges ; deux points aux discontinuités ; extremum exact de M où V = 0).\nDéformée : u' = N/EA + ε0, v'' = M/EI + κ0, "
                      "intégrées depuis le nœud i ; flèche = écart maximal à la corde.\nContrôle : écart entre u, v, v' intégrés en x = L et les "
                      "déplacements du nœud j (≈ 0 si tout est cohérent).",
                      kSrcElement),
            [](ExecutionContext& c) {
                const S::FrameElement2D* e = elementIn(c);
                const Vector* d = vector6(c, "d_e");
                const S::LoadCase* loads = loadsIn(c);
                const long long points = c.integer("points");
                if (!e || !d || !loads || c.failed()) return false;
                if (points < 2 || points > 2001) return c.fail("nombre de points hors de [2 ; 2001]");
                std::vector<S::LocalLoad> local;
                if (!localLoads(c, *e, *loads, local)) return false;
                const S::InternalForceDiagram g = e->diagram(*d, local, static_cast<int>(points));
                c.setOutput("N_min", g.Nmin);
                c.setOutput("N_max", g.Nmax);
                c.setOutput("V_min", g.Vmin);
                c.setOutput("V_max", g.Vmax);
                c.setOutput("M_min", g.Mmin);
                c.setOutput("M_max", g.Mmax);
                c.setOutput("x_Mmax", g.xMmax);
                c.setOutput("deflection", g.maxDeflection);
                c.setOutput("closure", g.closureError);
                c.setOutput("diagram", make<DiagramObject>(DiagramData { g, "Diagrammes de l'élément " + std::to_string(e->id()) }));
                return true;
            });

    lib.add(pureBlock("struct.assembly_operator", "Opérateur d'assemblage A_e", kElement,
                      "Matrice booléenne A_e (6 × n) qui extrait les DDL de l'élément du vecteur global : d_e = A_e·d.",
                      { object("element", "Élément", Type::Element), object("dofs", "DDL", Type::Dofs) },
                      { object("A", "A_e", Type::Matrix), data("dof_list", "DDL de l'élément", VT::Text) },
                      "A_e(k, dof_k) = 1 où dof_k est le numéro global du k-ième DDL de l'élément. Assemblage : K = Σ A_eᵀ·k_e·A_e et "
                      "F = Σ A_eᵀ·f_eq,e (le bloc « Aᵀ·B·A » de la catégorie D calcule A_eᵀ·k_e·A_e).",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const S::FrameElement2D* e = elementIn(c);
                const DofSet* dofs = dofsIn(c);
                if (!e || !dofs) return false;
                const auto k = dofs->numbering.elementDofs(e->nodeI().id, e->nodeJ().id);
                std::string list;
                for (int d : k)
                {
                    if (d < 0) return c.fail("nœud de l'élément " + std::to_string(e->id()) + " absent de la numérotation");
                    list += (list.empty() ? "" : ", ") + std::to_string(d);
                }
                c.setOutput("A", matrixOut(S::assemblyOperator(dofs->numbering, e->nodeI().id, e->nodeJ().id), elementDofLabels(*e),
                                           dofLabels(dofs->numbering), "A_e (élément " + std::to_string(e->id()) + ")"));
                c.setOutput("dof_list", list);
                return true;
            });
}

} // namespace TSALab::Research::Structural::detail
