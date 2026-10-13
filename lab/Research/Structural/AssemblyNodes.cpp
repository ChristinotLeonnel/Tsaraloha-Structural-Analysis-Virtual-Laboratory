// Catégorie C — assemblage et résolution : numérotation des DDL, K et F globaux, conditions aux limites, système
// réduit, résolution avec diagnostic, déplacements complets, réactions, bilan d'équilibre, résultat, calcul direct.

#include "NodeHelpers.h"

#include <cmath>
#include <sstream>

namespace TSALab::Research::Structural::detail
{

namespace
{
const DofSet* partitionedDofs(ExecutionContext& c, const char* pin = "dofs")
{
    const DofSet* d = dofsIn(c, pin);
    if (!d) return nullptr;
    if (!d->partitioned)
    {
        c.fail(std::string("entrée « ") + pin + " » : numérotation sans conditions aux limites (relier la sortie du bloc « BoundaryConditions »)");
        return nullptr;
    }
    return d;
}

std::string nodeTable(const S::DofNumbering& n, const Vector& v, const char* unit1, const char* unit2)
{
    std::ostringstream o;
    o.precision(6);
    for (std::size_t p = 0; p < n.nodeIds.size() && 3 * p + 2 < v.size(); ++p)
        o << "N" << n.nodeIds[p] << " : " << v[3 * p] << " " << unit1 << ", " << v[3 * p + 1] << " " << unit1 << ", " << v[3 * p + 2] << " " << unit2 << "\n";
    return o.str();
}
} // namespace

void registerAssemblyNodes(NodeLibrary& lib)
{
    lib.add(pureBlock("struct.dof_numbering", "GlobalDOFNumbering", kAssembly, "Numérotation globale : 3 DDL par nœud (ux, uy, θz), dans l'ordre des nœuds.",
                      { object("model", "Modèle", Type::Model) },
                      { object("dofs", "DDL", Type::Dofs), data("count", "Nombre de DDL", VT::Integer), data("table", "Table", VT::Text) },
                      "Le k-ième nœud du modèle (k = 0, 1, …) porte les DDL 3k (ux), 3k + 1 (uy), 3k + 2 (θz) ; n = 3 × nombre de nœuds.",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                if (!m) return false;
                DofSet d { S::numberDofs(*m), false, {} };
                c.setOutput("count", static_cast<long long>(d.numbering.size()));
                c.setOutput("table", details(d));
                c.setOutput("dofs", make<DofObject>(std::move(d)));
                return true;
            });

    lib.add(pureBlock("struct.global_stiffness", "GlobalStiffnessAssembly", kAssembly, "Matrice de rigidité globale K = Σ A_eᵀ·k_e·A_e (n × n).",
                      { object("model", "Modèle", Type::Model), object("dofs", "DDL", Type::Dofs) }, { object("K", "K", Type::Matrix) },
                      "Assemblage par adressage direct : k_e (= Tᵀ·k'·T) de chaque élément ajoutée aux lignes et colonnes de ses 6 DDL globaux ; "
                      "résultat identique à la somme Σ A_eᵀ·k_e·A_e (que le graphe « Methode_Des_Deplacements_2D » calcule explicitement). K est "
                      "symétrique ; singulière tant que les conditions aux limites ne sont pas appliquées. Refus : élément invalide.",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const DofSet* d = dofsIn(c);
                if (!m || !d) return false;
                Matrix K;
                std::string error;
                if (!S::assembleStiffness(*m, d->numbering, K, &error)) return c.fail(error);
                const auto labels = dofLabels(d->numbering);
                c.setOutput("K", matrixOut(std::move(K), labels, labels, "K"));
                return true;
            });

    lib.add(pureBlock("struct.nodal_load_vector", "Vecteur des charges nodales", kAssembly, "Charges nodales seules (Fx, Fy, Mz) rangées aux DDL de leurs nœuds.",
                      { object("dofs", "DDL", Type::Dofs), object("loads", "Chargement", Type::LoadCase) }, { object("F", "F nodal", Type::Vector) },
                      "Point de départ de l'assemblage de F dans une boucle : F = F_nodal + Σ A_eᵀ·f_eq,e. Les déplacements imposés n'y figurent pas.",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const DofSet* d = dofsIn(c);
                const S::LoadCase* loads = loadsIn(c);
                if (!d || !loads) return false;
                for (const auto& p : loads->nodalLoads)
                    if (d->numbering.nodePosition(p.node) < 0) return c.fail("charge nodale sur le nœud " + std::to_string(p.node) + " inexistant");
                c.setOutput("F", vectorOut(S::nodalLoadVector(d->numbering, *loads), dofLabels(d->numbering), "F nodal"));
                return true;
            });

    lib.add(pureBlock("struct.global_loads", "GlobalLoadVectorAssembly", kAssembly,
                      "Vecteur global des charges F = charges nodales + Σ A_eᵀ·(charges nodales équivalentes des charges d'élément).",
                      { object("model", "Modèle", Type::Model), object("dofs", "DDL", Type::Dofs), object("loads", "Chargement", Type::LoadCase) },
                      { object("F", "F", Type::Vector) },
                      "Charges d'élément converties par la classe FrameElement2D (charges nodales équivalentes globales). Les déplacements imposés ne "
                      "sont PAS des forces : ils interviennent dans le système réduit (K_fc·d_c). Refus : charge invalide (raison).",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const DofSet* d = dofsIn(c);
                const S::LoadCase* loads = loadsIn(c);
                if (!m || !d || !loads) return false;
                Vector F;
                std::string error;
                if (!S::assembleLoads(*m, d->numbering, *loads, F, &error)) return c.fail(error);
                c.setOutput("F", vectorOut(std::move(F), dofLabels(d->numbering), "F"));
                return true;
            });

    lib.add(pureBlock("struct.boundary_conditions", "BoundaryConditions", kAssembly,
                      "Partition des DDL : libres (f) et imposés (c) d'après les appuis ; valeurs imposées d_c d'après le chargement.",
                      { object("model", "Modèle", Type::Model), object("dofs", "DDL", Type::Dofs), object("loads", "Chargement", Type::LoadCase) },
                      { object("dofs", "DDL partitionnés", Type::Dofs), data("free", "DDL libres", VT::Integer), data("constrained", "DDL imposés", VT::Integer),
                        object("d_c", "d_c", Type::Vector) },
                      "DDL bloqué par un appui → imposé (valeur 0, ou celle d'un déplacement imposé du chargement, qui doit porter sur un DDL bloqué). "
                      "Les déplacements imposés non nuls sont ainsi pris en compte sans être convertis en forces.",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const DofSet* d = dofsIn(c);
                const S::LoadCase* loads = loadsIn(c);
                if (!m || !d || !loads) return false;
                DofSet out { d->numbering, true, {} };
                std::string error;
                if (!S::boundaryConditions(*m, d->numbering, *loads, out.conditions, &error)) return c.fail(error);
                c.setOutput("free", static_cast<long long>(out.conditions.freeDofs.size()));
                c.setOutput("constrained", static_cast<long long>(out.conditions.constrainedDofs.size()));
                c.setOutput("d_c", vectorOut(out.conditions.imposed, dofLabels(d->numbering, &out.conditions.constrainedDofs), "d_c"));
                c.setOutput("dofs", make<DofObject>(std::move(out)));
                return true;
            });

    lib.add(pureBlock("struct.reduced_system", "ReducedSystem", kAssembly, "Système réduit K_ff·d_f = F_f − K_fc·d_c.",
                      { object("K", "K", Type::Matrix), object("F", "F", Type::Vector), object("dofs", "DDL partitionnés", Type::Dofs) },
                      { object("system", "Système réduit", Type::Reduced), object("K_ff", "K_ff", Type::Matrix), object("K_fc", "K_fc", Type::Matrix),
                        object("F_f", "F_f", Type::Vector), object("d_c", "d_c", Type::Vector), object("rhs", "F_f − K_fc·d_c", Type::Vector) },
                      "Partition de K = [K_ff K_fc ; K_cf K_cc] et de F = [F_f ; F_c] selon les DDL libres f et imposés c. Second membre "
                      "F_f − K_fc·d_c : prend en compte les déplacements imposés non nuls. Contrôle des dimensions (K n × n, F n, partition).",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const LabeledMatrix* K = matrixIn(c, "K");
                const LabeledVector* F = vectorIn(c, "F");
                const DofSet* d = partitionedDofs(c);
                if (!K || !F || !d) return false;
                ReducedData r { {}, d->conditions, d->numbering };
                std::string error;
                if (!S::reduce(K->matrix, F->vector, d->conditions, r.system, &error)) return c.fail(error);
                const auto free = dofLabels(d->numbering, &d->conditions.freeDofs);
                const auto fixed = dofLabels(d->numbering, &d->conditions.constrainedDofs);
                c.setOutput("K_ff", matrixOut(r.system.Kff, free, free, "K_ff"));
                c.setOutput("K_fc", matrixOut(r.system.Kfc, free, fixed, "K_fc"));
                c.setOutput("F_f", vectorOut(r.system.Ff, free, "F_f"));
                c.setOutput("d_c", vectorOut(r.system.dc, fixed, "d_c"));
                c.setOutput("rhs", vectorOut(r.system.rhs, free, "F_f − K_fc·d_c"));
                c.setOutput("system", make<ReducedObject>(std::move(r)));
                return true;
            });

    lib.add(pureBlock("struct.solve_displacements", "Résoudre K_ff·d_f = F_f − K_fc·d_c", kAssembly,
                      "Résolution du système réduit par factorisation (Cholesky, LU ou gradient conjugué), avec diagnostic des systèmes singuliers.",
                      { object("system", "Système réduit", Type::Reduced), optionalObject("model", "Modèle (diagnostic)", Type::Model),
                        data("method", "Méthode (cholesky, lu, cg)", VT::Text, std::string("cholesky")) },
                      { object("d_f", "d_f", Type::Vector), data("residual", "Résidu relatif", VT::Real), data("condition", "Conditionnement", VT::Real),
                        data("report", "Rapport", VT::Text) },
                      "Pas d'inverse explicite : Cholesky (K = L·Lᵀ, défaut : K_ff symétrique définie positive ⇔ structure stable), LU à pivot partiel, "
                      "ou gradient conjugué préconditionné. Résidu ‖K·x − f‖/‖f‖ recalculé ; conditionnement λmax/λmin calculé jusqu'à 150 équations "
                      "(signalé au-delà de 1e10).\nSystème singulier : le bloc échoue avec le DDL au pivot nul, le mode à énergie nulle (valeur propre "
                      "minimale) et sa nature — translation ou rotation d'ensemble (appuis insuffisants), nœud non relié, mécanisme interne, ou "
                      "système quasi singulier par écart de rigidités — et les causes possibles. Relier le modèle améliore le diagnostic.",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const auto r = c.objectAs<ReducedObject>("system");
                const std::string methodText = c.text("method");
                if (!r || c.failed()) return false;
                const S::Model2D* model = nullptr;
                if (c.hasValue("model"))
                {
                    model = modelIn(c);
                    if (!model) return false;
                }
                tsalab::numerics::SolverMethod method;
                if (!parseMethod(methodText, method)) return c.fail("méthode inconnue « " + methodText + " » (cholesky, lu, cg)");
                const auto& data = r->value();
                const S::DisplacementSolve s = S::solveDisplacements(data.system, data.conditions, data.numbering, model, method);
                if (!s.success) return c.fail(s.message);
                std::string report = s.message;
                for (const auto& w : s.warnings) report += "\nAvertissement : " + w;
                for (const auto& w : s.warnings) c.log("Résolution : " + w);
                c.setOutput("d_f", vectorOut(s.df, dofLabels(data.numbering, &data.conditions.freeDofs), "d_f"));
                c.setOutput("residual", s.report.relativeResidual);
                c.setOutput("condition", s.conditionNumber);
                c.setOutput("report", report);
                return true;
            });

    lib.add(pureBlock("struct.displacement_recovery", "DisplacementRecovery", kAssembly, "Déplacements complets d = [d_f ; d_c] remis dans la numérotation globale.",
                      { object("d_f", "d_f", Type::Vector), object("dofs", "DDL partitionnés", Type::Dofs) },
                      { object("d", "d", Type::Vector), data("table", "Déplacements nodaux", VT::Text) },
                      "d(f_k) = d_f(k), d(c_k) = d_c(k) (déplacements imposés, nuls sur un appui fixe).", kSrcStiffness),
            [](ExecutionContext& c) {
                const LabeledVector* df = vectorIn(c, "d_f");
                const DofSet* d = partitionedDofs(c);
                if (!df || !d) return false;
                Vector full;
                std::string error;
                if (!S::fullDisplacements(d->conditions, df->vector, d->numbering.size(), full, &error)) return c.fail(error);
                c.setOutput("table", nodeTable(d->numbering, full, "m", "rad"));
                c.setOutput("d", vectorOut(std::move(full), dofLabels(d->numbering), "d"));
                return true;
            });

    lib.add(pureBlock("struct.reaction_recovery", "ReactionRecovery", kAssembly, "Réactions R = K·d − F (forces exercées par les appuis).",
                      { object("K", "K", Type::Matrix), object("d", "d", Type::Vector), object("F", "F", Type::Vector), object("dofs", "DDL partitionnés", Type::Dofs) },
                      { object("R", "R", Type::Vector), data("table", "Réactions", VT::Text), data("sum_x", "Σ Rx (kN)", VT::Real), data("sum_y", "Σ Ry (kN)", VT::Real) },
                      "Système global complet : R = K·d − F. Aux DDL imposés, R est la réaction d'appui (charges appliquées directement à l'appui "
                      "et charges équivalentes des éléments voisins déduites) ; aux DDL libres, R ≈ 0 (résidu d'équilibre nodal).",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const LabeledMatrix* K = matrixIn(c, "K");
                const LabeledVector* d = vectorIn(c, "d");
                const LabeledVector* F = vectorIn(c, "F");
                const DofSet* dofs = partitionedDofs(c);
                if (!K || !d || !F || !dofs) return false;
                Vector R;
                std::string error;
                if (!S::reactions(K->matrix, d->vector, F->vector, R, &error)) return c.fail(error);
                std::ostringstream t;
                t.precision(6);
                double sx = 0.0, sy = 0.0;
                for (int k : dofs->conditions.constrainedDofs)
                {
                    t << dofs->numbering.label(k) << " : " << R[static_cast<std::size_t>(k)] << (k % 3 == 2 ? " kN·m" : " kN") << "\n";
                    if (k % 3 == 0) sx += R[static_cast<std::size_t>(k)];
                    if (k % 3 == 1) sy += R[static_cast<std::size_t>(k)];
                }
                c.setOutput("table", t.str());
                c.setOutput("sum_x", sx);
                c.setOutput("sum_y", sy);
                c.setOutput("R", vectorOut(std::move(R), dofLabels(dofs->numbering), "R = K·d − F"));
                return true;
            });

    lib.add(pureBlock("struct.equilibrium", "Bilan d'équilibre", kAssembly,
                      "Équilibre global : résultante des charges (calculée depuis leur définition) + réactions = 0, en forces et en moment.",
                      { object("model", "Modèle", Type::Model), object("dofs", "DDL partitionnés", Type::Dofs), object("loads", "Chargement", Type::LoadCase),
                        object("R", "R", Type::Vector), optionalObject("K", "K (référence)", Type::Matrix), optionalObject("d", "d (référence)", Type::Vector),
                        optionalObject("F", "F (référence)", Type::Vector), data("tolerance", "Tolérance relative", VT::Real, 1e-9) },
                      { data("balanced", "Équilibré", VT::Bool), data("force_residual", "Écart relatif (forces)", VT::Real),
                        data("moment_residual", "Écart relatif (moment)", VT::Real), data("report", "Bilan", VT::Text) },
                      "ΣFx, ΣFy et ΣM/O (moment par rapport à l'origine) des charges nodales et des charges d'élément (intégrées directement, pas "
                      "depuis F) et des réactions aux DDL imposés. Écarts rapportés à la plus grande des grandeurs : charges, réactions et, si K, d, "
                      "F sont reliés, ‖K·d‖ et ‖F‖ (cas sans charge : dilatation libre, tassement).",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const DofSet* d = partitionedDofs(c);
                const S::LoadCase* loads = loadsIn(c);
                const LabeledVector* R = vectorIn(c, "R");
                const double tol = c.real("tolerance");
                if (!m || !d || !loads || !R || c.failed()) return false;
                double reference = 0.0;
                if (c.hasValue("K") && c.hasValue("d") && c.hasValue("F"))
                {
                    const LabeledMatrix* K = matrixIn(c, "K");
                    const LabeledVector* dv = vectorIn(c, "d");
                    const LabeledVector* F = vectorIn(c, "F");
                    if (!K || !dv || !F) return false;
                    reference = S::referenceForce(K->matrix, dv->vector, F->vector);
                }
                const auto q = S::checkEquilibrium(*m, d->numbering, d->conditions, *loads, R->vector, reference);
                c.setOutput("balanced", q.balanced(tol));
                c.setOutput("force_residual", q.relativeForceResidual);
                c.setOutput("moment_residual", q.relativeMomentResidual);
                c.setOutput("report", q.text());
                return true;
            });

    lib.add(pureBlock("struct.result_assemble", "Assembler le résultat", kAssembly,
                      "Résultat complet (SolverResult) à partir des étapes du graphe : réactions, efforts et diagrammes par élément, équilibre.",
                      { object("model", "Modèle", Type::Model), object("loads", "Chargement", Type::LoadCase), object("dofs", "DDL partitionnés", Type::Dofs),
                        object("K", "K", Type::Matrix), object("F", "F", Type::Vector), object("d", "d", Type::Vector), data("points", "Points par diagramme", VT::Integer, 21LL) },
                      { object("result", "Résultat", Type::Result), data("summary", "Résumé", VT::Text) },
                      "Mêmes calculs que la fin du calcul direct (R = K·d − F, efforts d'extrémité et diagrammes de chaque élément, bilan "
                      "d'équilibre) à partir des valeurs produites par le graphe : permet de comparer un graphe au calcul direct.",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const S::LoadCase* loads = loadsIn(c);
                const DofSet* dofs = partitionedDofs(c);
                const LabeledMatrix* K = matrixIn(c, "K");
                const LabeledVector* F = vectorIn(c, "F");
                const LabeledVector* d = vectorIn(c, "d");
                const long long points = c.integer("points");
                if (!m || !loads || !dofs || !K || !F || !d || c.failed()) return false;
                S::AnalysisResult2D r;
                std::string error;
                if (!S::assembleResult(*m, *loads, dofs->numbering, dofs->conditions, K->matrix, F->vector, d->vector, static_cast<int>(std::max(2LL, points)), r, &error))
                    return c.fail(error);
                r.title = loads->id < 0 ? "combinaison « " + loads->name + " »" : "cas " + std::to_string(loads->id) + " « " + loads->name + " »";
                r.message = "Résultat assemblé par le graphe (" + r.title + ").";
                c.setOutput("summary", summary(r));
                c.setOutput("result", make<ResultObject>(std::move(r)));
                return true;
            });

    lib.add(pureBlock("struct.solve_direct", "Calcul direct (méthode des déplacements)", kAssembly,
                      "Méthode des déplacements complète en un bloc (étapes 1 à 8) : la référence des graphes pas à pas.",
                      { object("model", "Modèle", Type::Model), data("case", "Cas", VT::Integer, 1LL), data("combination", "Combinaison (0 : aucune)", VT::Integer, 0LL),
                        data("method", "Méthode (cholesky, lu, cg)", VT::Text, std::string("cholesky")), data("points", "Points par diagramme", VT::Integer, 21LL) },
                      { object("result", "Résultat", Type::Result), data("success", "Réussi", VT::Bool), data("message", "Message", VT::Text) },
                      "Contrôle du modèle, chargement effectif, numérotation, assemblage de K et F, conditions aux limites, système réduit, "
                      "résolution (diagnostic des mécanismes), déplacements complets, réactions, efforts et diagrammes, bilan d'équilibre. En "
                      "cas d'échec (modèle invalide, mécanisme), le bloc ne s'arrête pas : success = faux et message explicatif.",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const long long caseId = c.integer("case"), combo = c.integer("combination"), points = c.integer("points");
                const std::string methodText = c.text("method");
                if (!m || c.failed()) return false;
                S::SolveOptions o;
                if (!parseMethod(methodText, o.method)) return c.fail("méthode inconnue « " + methodText + " » (cholesky, lu, cg)");
                o.diagramPoints = static_cast<int>(std::max(2LL, points));
                S::AnalysisResult2D r = S::solveLinearStatic(*m, { static_cast<int>(caseId), static_cast<int>(combo) }, o);
                c.setOutput("success", r.success);
                c.setOutput("message", r.message);
                c.setOutput("result", make<ResultObject>(std::move(r)));
                return true;
            });

    lib.add(pureBlock("struct.result_node", "Résultat en un nœud", kAssembly, "Déplacements et réactions d'un nœud dans un résultat.",
                      { object("result", "Résultat", Type::Result), data("node", "Nœud", VT::Integer, 1LL) },
                      { data("ux", "ux (m)", VT::Real), data("uy", "uy (m)", VT::Real), data("rz", "θz (rad)", VT::Real), data("Rx", "Rx (kN)", VT::Real),
                        data("Ry", "Ry (kN)", VT::Real), data("Mz", "Mz (kN·m)", VT::Real) },
                      "Réactions : composantes nulles sur les DDL libres. Refus : résultat en échec ou nœud absent.", kSrcStiffness),
            [](ExecutionContext& c) {
                const S::AnalysisResult2D* r = resultIn(c);
                const long long node = c.integer("node");
                if (!r || c.failed()) return false;
                if (!r->success) return c.fail("résultat en échec : " + r->message);
                if (r->dofs.nodePosition(static_cast<int>(node)) < 0) return c.fail("nœud " + std::to_string(node) + " absent du résultat");
                const auto d = r->nodeDisplacement(static_cast<int>(node));
                const auto R = r->nodeReaction(static_cast<int>(node));
                c.setOutput("ux", d[0]);
                c.setOutput("uy", d[1]);
                c.setOutput("rz", d[2]);
                c.setOutput("Rx", R[0]);
                c.setOutput("Ry", R[1]);
                c.setOutput("Mz", R[2]);
                return true;
            });

    lib.add(pureBlock("struct.result_element", "Résultat d'un élément", kAssembly, "Efforts d'extrémité, diagrammes et extremums d'un élément dans un résultat.",
                      { object("result", "Résultat", Type::Result), data("element", "Élément", VT::Integer, 1LL) },
                      { object("diagram", "Diagrammes", Type::Diagram), object("end_forces", "f' local", Type::Vector), data("N_min", "N min", VT::Real),
                        data("N_max", "N max", VT::Real), data("M_min", "M min", VT::Real), data("M_max", "M max", VT::Real), data("deflection", "Flèche max (m)", VT::Real) },
                      "Valeurs du résultat (convention RDM des diagrammes). Refus : résultat en échec ou élément absent.", kSrcStiffness),
            [](ExecutionContext& c) {
                const S::AnalysisResult2D* r = resultIn(c);
                const long long id = c.integer("element");
                if (!r || c.failed()) return false;
                if (!r->success) return c.fail("résultat en échec : " + r->message);
                const S::ElementResult* e = r->element(static_cast<int>(id));
                if (!e) return c.fail("élément " + std::to_string(id) + " absent du résultat");
                c.setOutput("diagram", make<DiagramObject>(DiagramData { e->diagram, "Diagrammes de l'élément " + std::to_string(id) }));
                c.setOutput("end_forces", vectorOut(e->endForces, { "Fx_i", "Fy_i", "Mz_i", "Fx_j", "Fy_j", "Mz_j" }, "f' (élément " + std::to_string(id) + ")"));
                c.setOutput("N_min", e->diagram.Nmin);
                c.setOutput("N_max", e->diagram.Nmax);
                c.setOutput("M_min", e->diagram.Mmin);
                c.setOutput("M_max", e->diagram.Mmax);
                c.setOutput("deflection", e->diagram.maxDeflection);
                return true;
            });

    lib.add(pureBlock("struct.compare_results", "Comparer deux résultats", kAssembly,
                      "Écarts relatifs entre un résultat et une référence (déplacements, réactions, efforts d'extrémité).",
                      { object("result", "Résultat", Type::Result), object("reference", "Référence", Type::Result), data("tolerance", "Tolérance relative", VT::Real, 1e-9) },
                      { data("deviation", "Écart maximal", VT::Real), data("passed", "Conforme", VT::Bool), data("report", "Rapport", VT::Text) },
                      "Écart = max|a − b| / max|référence| pour chaque famille (déplacements, réactions aux DDL imposés, efforts d'extrémité) ; "
                      "conforme si l'écart maximal ≤ tolérance. Non comparable (tailles différentes, résultat en échec) : conforme = faux.",
                      kSrcStiffness),
            [](ExecutionContext& c) {
                const S::AnalysisResult2D* a = resultIn(c);
                const S::AnalysisResult2D* b = resultIn(c, "reference");
                const double tol = c.real("tolerance");
                if (!a || !b || c.failed()) return false;
                const auto k = S::compareResults(*a, *b);
                const double dev = k.comparable ? k.maxDeviation() : -1.0;
                c.setOutput("deviation", dev);
                c.setOutput("passed", k.comparable && dev <= tol);
                c.setOutput("report", k.comparable ? "Écarts relatifs : déplacements " + num(k.displacementDeviation, 3) + ", réactions " + num(k.reactionDeviation, 3)
                                                         + ", efforts " + num(k.forceDeviation, 3) + " (tolérance " + num(tol, 3) + ")"
                                                   : "Non comparable : " + k.message);
                return true;
            });
}

} // namespace TSALab::Research::Structural::detail
