// Graphes d'exemple de la programmation structurelle (voir StructuralGraphs.h).

#include "StructuralGraphs.h"

#include <algorithm>
#include <limits>

namespace TSALab::Research::StructuralGraphs
{

using namespace TSA::Blueprint;

namespace
{
/// Construction contrôlée : chaque lien passe par NodeLibrary::connect ; la première erreur est conservée.
class Builder
{
public:
    Builder(Graph& g, const NodeLibrary& lib) : m_g(g), m_lib(lib) {}

    int node(const std::string& type, double x, double y)
    {
        if (!m_lib.find(type)) fail("bloc inconnu « " + type + " » (bibliothèque structurelle non enregistrée ?)");
        return m_g.addNode(type, x, y);
    }
    void value(int n, const std::string& pin, const Value& v) { m_g.setValue(n, pin, v); }
    void link(int a, const std::string& out, int b, const std::string& in)
    {
        std::string why;
        if (!m_lib.connect(m_g, { a, out, b, in }, &why)) fail("lien #" + std::to_string(a) + "." + out + " → #" + std::to_string(b) + "." + in + " : " + why);
    }
    void then(int a, int b) { link(a, "then", b, "exec"); }
    int param(const std::string& type, const std::string& name, const Value& v, double x, double y)
    {
        const int p = node(type, x, y);
        value(p, "name", name);
        value(p, "value", v);
        return p;
    }
    /// Affichage : debug.print de « texte » + valeur reliée (concaténation).
    int print(const std::string& text, int source, const std::string& pin, double x, double y)
    {
        const int cat = node("text.concat", x, y + 150);
        value(cat, "a", text);
        link(source, pin, cat, "b");
        const int p = node("debug.print", x, y);
        link(cat, "result", p, "value");
        return p;
    }
    int details(const std::string& title, int source, const std::string& pin, double x, double y)
    {
        const int p = node("debug.print_details", x, y);
        value(p, "title", title);
        link(source, pin, p, "value");
        return p;
    }
    int setVar(const std::string& name, int source, const std::string& pin, double x, double y)
    {
        const int s = node("var.set", x, y);
        value(s, "name", name);
        if (source) link(source, pin, s, "value");
        return s;
    }
    int getVar(const std::string& name, double x, double y)
    {
        const int g = node("var.get", x, y);
        value(g, "name", name);
        return g;
    }
    /// Cadre commentaire autour de nœuds (taille estimée d'après leurs broches).
    void frame(const std::vector<int>& nodes, const std::string& text, const std::string& color)
    {
        double x0 = std::numeric_limits<double>::max(), y0 = x0, x1 = -x0, y1 = -x0;
        for (int id : nodes)
        {
            const auto* n = m_g.node(id);
            if (!n) continue;
            const auto* d = m_lib.find(n->type);
            const double rows = d ? static_cast<double>(std::max(d->inputs.size(), d->outputs.size())) : 1.0;
            x0 = std::min(x0, n->x), y0 = std::min(y0, n->y);
            x1 = std::max(x1, n->x + 200.0), y1 = std::max(y1, n->y + 24.0 + rows * 20.0 + 26.0);
        }
        if (x1 < x0) return;
        Comment c;
        c.x = x0 - 30.0, c.y = y0 - 60.0, c.width = x1 - x0 + 60.0, c.height = y1 - y0 + 80.0;
        c.text = text;
        c.color = color;
        m_g.addComment(c);
    }
    bool ok(std::string* error) const
    {
        if (!m_error.empty() && error) *error = m_error;
        return m_error.empty();
    }

private:
    void fail(const std::string& m)
    {
        if (m_error.empty()) m_error = m;
    }
    Graph& m_g;
    const NodeLibrary& m_lib;
    std::string m_error;
};

// =============================================================================================
// Methode_Des_Deplacements_2D
// =============================================================================================
bool buildMethod(Graph& g, std::string* error)
{
    g.clear();
    g.name = "Methode_Des_Deplacements_2D";
    g.description = "Méthode de rigidité directe d'un portique plan linéaire élastique, pas à pas : modèle, propriétés des éléments (k', T, "
                    "k = Tᵀk'T, charges équivalentes), assemblage K = Σ A_eᵀ·k_e·A_e et F dans une boucle, conditions aux limites "
                    "(K_ff·d_f = F_f − K_fc·d_c), résolution, déplacements, réactions R = K·d − F et équilibre, efforts internes, "
                    "visualisation ; résultat comparé au calcul direct (bloc unique). Paramètres : cas, combinaison, q, H, amplification.";
    Builder b(g, NodeLibrary::standard());
    // Paramètres
    const int pCase = b.param("param.integer", "cas", 1LL, -1550, 0);
    const int pCombo = b.param("param.integer", "combinaison", 0LL, -1550, 130);
    const int pQ = b.param("param.real", "q", -15.0, -1550, 260);
    const int pH = b.param("param.real", "H", 20.0, -1550, 390);
    const int pScale = b.param("param.real", "amplification", 0.0, -1550, 520);
    b.frame({ pCase, pCombo, pQ, pH, pScale },
            "Paramètres\ncas (1 G, 2 W, 3 tassement) ou combinaison (1 : 1,35 G + 1,5 W) ; q et H du portique ; amplification de la déformée (0 : "
            "automatique)",
            "#5E35B1");

    // Étape 1 — modèle
    const int start = b.node("event.start", -1250, -320);
    const int model = b.node("struct.model_demo_portal", -1250, 120);
    b.link(pQ, "value", model, "q");
    b.link(pH, "value", model, "H");
    const int valid = b.node("struct.model_validate", -980, 60);
    b.link(model, "model", valid, "model");
    const int loads = b.node("struct.loading", -980, 260);
    b.link(model, "model", loads, "model");
    b.link(pCase, "value", loads, "case");
    b.link(pCombo, "value", loads, "combination");
    const int p1 = b.details("Étape 1 — modèle structurel (nœuds, éléments, matériaux, sections, appuis, cas de charge)", model, "model", -1250, -320);
    const int p1b = b.print("Étape 1 — contrôle des données : ", valid, "report", -980, -320);
    b.then(start, p1);
    b.then(p1, p1b);
    b.frame({ start, model, valid, loads, p1, p1b }, "Étape 1 — Construire le modèle\nnœuds, éléments, matériaux, sections, appuis, cas de charge ; contrôle", "#00796B");

    // Étapes 2 et 3 — propriétés des éléments, assemblage dans une boucle
    const int dofs = b.node("struct.dof_numbering", -700, 160);
    b.link(model, "model", dofs, "model");
    const int zeros = b.node("matrix.zeros", -450, 120);
    b.link(dofs, "count", zeros, "rows");
    b.link(dofs, "count", zeros, "cols");
    const int fnodal = b.node("struct.nodal_load_vector", -450, 300);
    b.link(dofs, "dofs", fnodal, "dofs");
    b.link(loads, "loads", fnodal, "loads");
    const int setK0 = b.setVar("K", zeros, "M", -700, -320);
    const int setF0 = b.setVar("F", fnodal, "F", -450, -320);
    const int loop1 = b.node("struct.for_each_element", -200, -320);
    b.link(model, "model", loop1, "model");
    const int kl = b.node("struct.local_stiffness", -200, 60);
    const int T = b.node("struct.transformation", -200, 200);
    b.link(loop1, "element", kl, "element");
    b.link(loop1, "element", T, "element");
    const int kg = b.node("matrix.congruence", 50, 100);   // k = Tᵀ·k'·T
    b.link(T, "T", kg, "A");
    b.link(kl, "k", kg, "B");
    const int Ae = b.node("struct.assembly_operator", 50, 280);
    b.link(loop1, "element", Ae, "element");
    b.link(dofs, "dofs", Ae, "dofs");
    const int getK = b.getVar("K", 300, 0);
    const int AkA = b.node("matrix.congruence", 300, 140);   // A_eᵀ·k_e·A_e
    b.link(Ae, "A", AkA, "A");
    b.link(kg, "C", AkA, "B");
    const int sumK = b.node("matrix.add", 550, 60);
    b.link(getK, "value", sumK, "A");
    b.link(AkA, "C", sumK, "B");
    const int feq = b.node("struct.equivalent_loads", 50, 440);
    b.link(loop1, "element", feq, "element");
    b.link(loads, "loads", feq, "loads");
    const int AeT = b.node("matrix.transpose", 300, 330);
    b.link(Ae, "A", AeT, "A");
    const int AtF = b.node("matrix.vector_product", 550, 380);
    b.link(AeT, "At", AtF, "A");
    b.link(feq, "f_global", AtF, "x");
    const int getF = b.getVar("F", 300, 520);
    const int sumF = b.node("vector.add", 550, 540);
    b.link(getF, "value", sumF, "x");
    b.link(AtF, "y", sumF, "y");
    const int setK = b.setVar("K", sumK, "C", 50, -320);
    const int setF = b.setVar("F", sumF, "z", 300, -320);
    b.then(p1b, setK0);
    b.then(setK0, setF0);
    b.then(setF0, loop1);
    b.link(loop1, "body", setK, "exec");
    b.then(setK, setF);
    b.frame({ dofs, zeros, fnodal, setK0, setF0, loop1, kl, T, kg, Ae, getK, AkA, sumK, feq, AeT, AtF, getF, sumF, setK, setF },
            "Étapes 2 et 3 — Propriétés des éléments et assemblage\nPour chaque élément : k' et T, k = Tᵀ·k'·T, A_e, charges équivalentes ; "
            "K ← K + A_eᵀ·k_e·A_e, F ← F + A_eᵀ·f_eq (K nulle et F = charges nodales au départ)",
            "#0277BD");

    // Après la boucle : K et F assemblées
    const int getK2 = b.getVar("K", 850, 60);
    const int getF2 = b.getVar("F", 850, 200);
    const int p3 = b.details("Étape 3 — matrice de rigidité globale K = Σ A_eᵀ·k_e·A_e", getK2, "value", 850, -320);
    const int p3b = b.details("Étape 3 — vecteur global des charges F", getF2, "value", 1100, -320);
    b.link(loop1, "completed", p3, "exec");
    b.then(p3, p3b);

    // Étapes 4 et 5 — conditions aux limites, système réduit, résolution
    const int bc = b.node("struct.boundary_conditions", 1350, 120);
    b.link(model, "model", bc, "model");
    b.link(dofs, "dofs", bc, "dofs");
    b.link(loads, "loads", bc, "loads");
    const int rs = b.node("struct.reduced_system", 1600, 120);
    b.link(getK2, "value", rs, "K");
    b.link(getF2, "value", rs, "F");
    b.link(bc, "dofs", rs, "dofs");
    const int sol = b.node("struct.solve_displacements", 1850, 120);
    b.link(rs, "system", sol, "system");
    b.link(model, "model", sol, "model");
    b.value(sol, "method", std::string("cholesky"));
    const int p4 = b.details("Étape 4 — conditions aux limites et système réduit K_ff·d_f = F_f − K_fc·d_c", rs, "system", 1350, -320);
    const int p5 = b.print("Étape 5 — ", sol, "report", 1600, -320);
    b.then(p3b, p4);
    b.then(p4, p5);
    b.frame({ bc, rs, sol, p4, p5 }, "Étapes 4 et 5 — Conditions aux limites et résolution\npartition DDL libres / imposés, K_ff·d_f = F_f − K_fc·d_c, "
                                     "Cholesky (pas d'inverse explicite), diagnostic des mécanismes",
            "#3949AB");

    // Étapes 6 et 7 — déplacements, réactions, équilibre
    const int disp = b.node("struct.displacement_recovery", 2100, 120);
    b.link(sol, "d_f", disp, "d_f");
    b.link(bc, "dofs", disp, "dofs");
    const int reac = b.node("struct.reaction_recovery", 2350, 120);
    b.link(getK2, "value", reac, "K");
    b.link(disp, "d", reac, "d");
    b.link(getF2, "value", reac, "F");
    b.link(bc, "dofs", reac, "dofs");
    const int eq = b.node("struct.equilibrium", 2600, 120);
    b.link(model, "model", eq, "model");
    b.link(bc, "dofs", eq, "dofs");
    b.link(loads, "loads", eq, "loads");
    b.link(reac, "R", eq, "R");
    b.link(getK2, "value", eq, "K");
    b.link(disp, "d", eq, "d");
    b.link(getF2, "value", eq, "F");
    const int p6 = b.print("Étape 6 — déplacements nodaux (m, rad) :\n", disp, "table", 2100, -320);
    const int p7 = b.print("Étape 7 — réactions R = K·d − F :\n", reac, "table", 2350, -320);
    const int p7b = b.print("Étape 7 — bilan d'équilibre :\n", eq, "report", 2600, -320);
    b.then(p5, p6);
    b.then(p6, p7);
    b.then(p7, p7b);
    b.frame({ disp, reac, eq, p6, p7, p7b }, "Étapes 6 et 7 — Déplacements et réactions\nd = [d_f ; d_c], R = K·d − F, équilibre global des forces et des moments",
            "#558B2F");

    // Étape 8 — efforts internes
    const int loop2 = b.node("struct.for_each_element", 2850, -320);
    b.link(model, "model", loop2, "model");
    const int ed = b.node("struct.element_displacement", 2850, 120);
    b.link(loop2, "element", ed, "element");
    b.link(disp, "d", ed, "d");
    b.link(dofs, "dofs", ed, "dofs");
    const int ifd = b.node("struct.internal_forces", 3100, 120);
    b.link(loop2, "element", ifd, "element");
    b.link(ed, "d_global", ifd, "d_e");
    b.link(loads, "loads", ifd, "loads");
    const int p8 = b.print("Étape 8 — ", ifd, "diagram", 3100, -320);
    b.then(p7b, loop2);
    b.link(loop2, "body", p8, "exec");
    b.frame({ loop2, ed, ifd, p8 }, "Étape 8 — Efforts internes\nPour chaque élément : d_e = A_e·d, f' = k'·T·d_e + f0', diagrammes N, V, M et déformée", "#5D4037");

    // Étape 9 — visualisation, comparaison au calcul direct
    const int res = b.node("struct.result_assemble", 3350, 120);
    b.link(model, "model", res, "model");
    b.link(loads, "loads", res, "loads");
    b.link(bc, "dofs", res, "dofs");
    b.link(getK2, "value", res, "K");
    b.link(getF2, "value", res, "F");
    b.link(disp, "d", res, "d");
    const int direct = b.node("struct.solve_direct", 3350, 400);
    b.link(model, "model", direct, "model");
    b.link(pCase, "value", direct, "case");
    b.link(pCombo, "value", direct, "combination");
    const int cmp = b.node("struct.compare_results", 3600, 220);
    b.link(res, "result", cmp, "result");
    b.link(direct, "result", cmp, "reference");
    const int show = b.node("struct.show", 3350, -320);
    b.link(res, "result", show, "value");
    b.value(show, "title", std::string("Methode_Des_Deplacements_2D"));
    b.link(pScale, "value", show, "scale");
    const int p9 = b.print("Étape 9 — comparaison au calcul direct : ", cmp, "report", 3600, -320);
    const int p9b = b.print("Graphe conforme au calcul direct : ", cmp, "passed", 3850, -320);
    b.link(loop2, "completed", show, "exec");
    b.then(show, p9);
    b.then(p9, p9b);
    b.frame({ res, direct, cmp, show, p9, p9b }, "Étape 9 — Visualiser\nmodèle, déformée amplifiée, réactions, diagrammes, équilibre ; comparaison au calcul direct (1e-9)",
            "#E65100");
    return b.ok(error);
}

// =============================================================================================
// Classe FrameElement2D (mode 1 : utilisation directe)
// =============================================================================================
bool buildElementClass(Graph& g, std::string* error)
{
    g.clear();
    g.name = "Classe FrameElement2D";
    g.description = "Mode 1 — utilisation directe : deux nœuds, un matériau et une section du catalogue, la classe FrameElement2D rend sa "
                    "longueur, son orientation, k', T, k = Tᵀk'T et les charges nodales équivalentes d'une charge verticale.";
    Builder b(g, NodeLibrary::standard());
    const int ni = b.node("struct.node", -900, 0);
    b.value(ni, "id", 1LL);
    const int nj = b.node("struct.node", -900, 200);
    b.value(nj, "id", 2LL);
    b.value(nj, "x", 4.0);
    b.value(nj, "y", 3.0);
    const int mat = b.node("struct.material_catalog", -900, 400);
    const int sec = b.node("struct.section_catalog", -900, 560);
    const int el = b.node("struct.frame_element", -600, 160);
    b.link(ni, "node", el, "node_i");
    b.link(nj, "node", el, "node_j");
    b.link(mat, "material", el, "material");
    b.link(sec, "section", el, "section");
    const int geo = b.node("struct.element_geometry", -300, 420);
    b.link(el, "element", geo, "element");
    const int load = b.node("struct.load_uniform", -600, 560);
    b.value(load, "q", -10.0);
    b.value(load, "direction", std::string("Y global"));
    const int lc = b.node("struct.load_case", -300, 600);
    b.value(lc, "name", std::string("q vertical"));
    b.link(load, "load", lc, "loads");
    const int eq = b.node("struct.equivalent_loads", 0, 560);
    b.link(el, "element", eq, "element");
    b.link(lc, "case", eq, "loads");
    const int start = b.node("event.start", -600, -300);
    const int p1 = b.print("Élément : ", el, "element", -350, -300);
    const int p2 = b.print("Angle (°) : ", geo, "angle", -100, -300);
    const int p3 = b.details("Rigidité locale k'", el, "k_local", 150, -300);
    const int p4 = b.details("Transformation T", el, "T", 400, -300);
    const int p5 = b.details("Rigidité globale k = Tᵀ·k'·T", el, "k_global", 650, -300);
    const int p6 = b.details("Charges nodales équivalentes (repère global)", eq, "f_global", 900, -300);
    b.then(start, p1);
    b.then(p1, p2);
    b.then(p2, p3);
    b.then(p3, p4);
    b.then(p4, p5);
    b.then(p5, p6);
    b.frame({ ni, nj, mat, sec, el }, "Classe FrameElement2D\nnœuds, matériau et section → élément contrôlé (longueur, E, A, Iz) et ses matrices", "#00796B");
    b.frame({ load, lc, eq, geo }, "Charges de l'élément\ncharge uniforme verticale (Y global) décomposée en repère local, charges nodales équivalentes", "#0277BD");
    return b.ok(error);
}

// =============================================================================================
// Solveur de l'étudiant (mode 2) : gradient conjugué en blocs mathématiques
// =============================================================================================
/// Système réduit du portique de démonstration (assemblage en un bloc) et solution de référence (Cholesky).
struct ReducedBlocks
{
    int rs = 0, ref = 0, bc = 0;
};

ReducedBlocks reducedSystemBlocks(Builder& b)
{
    const int model = b.node("struct.model_demo_portal", -1500, 100);
    const int loads = b.node("struct.loading", -1250, 260);
    b.link(model, "model", loads, "model");
    const int dofs = b.node("struct.dof_numbering", -1250, 0);
    b.link(model, "model", dofs, "model");
    const int K = b.node("struct.global_stiffness", -1000, 0);
    b.link(model, "model", K, "model");
    b.link(dofs, "dofs", K, "dofs");
    const int F = b.node("struct.global_loads", -1000, 200);
    b.link(model, "model", F, "model");
    b.link(dofs, "dofs", F, "dofs");
    b.link(loads, "loads", F, "loads");
    const int bc = b.node("struct.boundary_conditions", -1000, 420);
    b.link(model, "model", bc, "model");
    b.link(dofs, "dofs", bc, "dofs");
    b.link(loads, "loads", bc, "loads");
    const int rs = b.node("struct.reduced_system", -750, 200);
    b.link(K, "K", rs, "K");
    b.link(F, "F", rs, "F");
    b.link(bc, "dofs", rs, "dofs");
    const int ref = b.node("matrix.solve", -500, 420);
    b.link(rs, "K_ff", ref, "K");
    b.link(rs, "rhs", ref, "f");
    b.value(ref, "method", std::string("cholesky"));
    b.frame({ model, loads, dofs, K, F, bc, rs, ref }, "Système réduit et solution de référence\nK_ff·d_f = F_f − K_fc·d_c ; référence : Cholesky", "#3949AB");
    return { rs, ref, bc };
}

bool buildStudentSolver(Graph& g, std::string* error)
{
    g.clear();
    g.name = "Solveur de l'étudiant (gradient conjugué)";
    g.description = "Mode 2 — le bloc de résolution est remplacé par un gradient conjugué écrit avec des blocs mathématiques et des variables "
                    "(r = f − K·x, α = r·r / p·Kp, x ← x + α·p, r ← r − α·Kp, β, p ← r + β·p), sur le système réduit du portique de "
                    "démonstration ; solution comparée à la résolution de référence (Cholesky). Variante : « Solveur de l'étudiant "
                    "(Gauss-Seidel) », qui boucle sur le bloc expérimental d'itération de Gauss-Seidel.";
    Builder b(g, NodeLibrary::standard());
    const auto [rs, ref, bc] = reducedSystemBlocks(b);

    // Initialisation : x = 0, r = p = f, rr = r·r, it = 0
    const int zeros = b.node("vector.zeros", -500, 100);
    b.link(bc, "free", zeros, "n");
    const int rr0 = b.node("vector.dot", -250, 300);
    b.link(rs, "rhs", rr0, "x");
    b.link(rs, "rhs", rr0, "y");
    const int start = b.node("event.start", -1500, -320);
    const int setX0 = b.setVar("x", zeros, "v", -1250, -320);
    const int setR0 = b.setVar("r", rs, "rhs", -1000, -320);
    const int setP0 = b.setVar("p", rs, "rhs", -750, -320);
    const int setRR0 = b.setVar("rr", rr0, "dot", -500, -320);
    const int setIt0 = b.setVar("it", 0, "", -250, -320);
    b.value(setIt0, "value", 0LL);
    b.then(start, setX0);
    b.then(setX0, setR0);
    b.then(setR0, setP0);
    b.then(setP0, setRR0);
    b.then(setRR0, setIt0);

    // Condition : ‖r‖/‖f‖ > tolérance et it < maximum
    const int getR = b.getVar("r", 0, 520);
    const int getIt = b.getVar("it", 0, 660);
    const int rnorm = b.node("vector.norm", 250, 480);
    b.link(getR, "value", rnorm, "x");
    const int fnorm = b.node("vector.norm", 250, 620);
    b.link(rs, "rhs", fnorm, "x");
    const int ratio = b.node("math.divide", 500, 520);
    b.link(rnorm, "norm", ratio, "a");
    b.link(fnorm, "norm", ratio, "b");
    const int c1 = b.node("math.compare", 750, 480);
    b.link(ratio, "result", c1, "a");
    b.value(c1, "b", 1e-13);
    b.value(c1, "op", std::string(">"));
    const int c2 = b.node("math.compare", 750, 640);
    b.link(getIt, "value", c2, "a");
    b.value(c2, "b", 60.0);
    b.value(c2, "op", std::string("<"));
    const int cond = b.node("logic.and", 1000, 560);
    b.link(c1, "result", cond, "a");
    b.link(c2, "result", cond, "b");
    const int loop = b.node("flow.while", 0, -320);
    b.link(cond, "result", loop, "condition");
    b.then(setIt0, loop);

    // Corps : une itération du gradient conjugué
    const int getX = b.getVar("x", 250, -60);
    const int getP = b.getVar("p", 250, 60);
    const int getRR = b.getVar("rr", 250, 180);
    const int Kp = b.node("matrix.vector_product", 500, 0);
    b.link(rs, "K_ff", Kp, "A");
    b.link(getP, "value", Kp, "x");
    const int pKp = b.node("vector.dot", 750, 20);
    b.link(getP, "value", pKp, "x");
    b.link(Kp, "y", pKp, "y");
    const int alpha = b.node("math.divide", 1000, 60);
    b.link(getRR, "value", alpha, "a");
    b.link(pKp, "dot", alpha, "b");
    const int ap = b.node("vector.scale", 1250, -60);
    b.link(getP, "value", ap, "x");
    b.link(alpha, "result", ap, "s");
    const int xNew = b.node("vector.add", 1500, -60);
    b.link(getX, "value", xNew, "x");
    b.link(ap, "z", xNew, "y");
    const int aKp = b.node("vector.scale", 1250, 120);
    b.link(Kp, "y", aKp, "x");
    b.link(alpha, "result", aKp, "s");
    const int getR2 = b.getVar("r", 1250, 280);
    const int rNew = b.node("vector.subtract", 1500, 140);
    b.link(getR2, "value", rNew, "x");
    b.link(aKp, "z", rNew, "y");
    const int getRn = b.getVar("r_next", 1750, 140);
    const int rrNew = b.node("vector.dot", 2000, 140);
    b.link(getRn, "value", rrNew, "x");
    b.link(getRn, "value", rrNew, "y");
    const int beta = b.node("math.divide", 2250, 140);
    b.link(rrNew, "dot", beta, "a");
    b.link(getRR, "value", beta, "b");
    const int bp = b.node("vector.scale", 2500, 60);
    b.link(getP, "value", bp, "x");
    b.link(beta, "result", bp, "s");
    const int pNew = b.node("vector.add", 2750, 100);
    b.link(getRn, "value", pNew, "x");
    b.link(bp, "z", pNew, "y");
    const int itNew = b.node("math.add", 2750, 300);
    b.link(getIt, "value", itNew, "a");
    b.value(itNew, "b", 1.0);
    const int setX = b.setVar("x", xNew, "z", 250, -320);
    const int setRn = b.setVar("r_next", rNew, "z", 500, -320);
    const int setP = b.setVar("p", pNew, "z", 750, -320);
    const int setRR = b.setVar("rr", rrNew, "dot", 1000, -320);
    const int setR = b.setVar("r", getRn, "value", 1250, -320);
    const int setIt = b.setVar("it", itNew, "result", 1500, -320);
    b.link(loop, "body", setX, "exec");
    b.then(setX, setRn);
    b.then(setRn, setP);
    b.then(setP, setRR);
    b.then(setRR, setR);
    b.then(setR, setIt);
    b.frame({ loop, getX, getP, getRR, Kp, pKp, alpha, ap, xNew, aKp, getR2, rNew, getRn, rrNew, beta, bp, pNew, itNew, setX, setRn, setP, setRR, setR, setIt },
            "Algorithme de l'étudiant : une itération du gradient conjugué\nα = r·r / p·Kp ; x ← x + α·p ; r ← r − α·Kp ; β = r'·r' / r·r ; "
            "p ← r' + β·p (ordre des affectations : nouvelles valeurs calculées avant d'être enregistrées)",
            "#E65100");

    // Comparaison à la référence
    const int getX2 = b.getVar("x", 1750, 420);
    const int cmp = b.node("vector.compare", 2000, 420);
    b.link(getX2, "value", cmp, "x");
    b.link(ref, "x", cmp, "reference");
    b.value(cmp, "tolerance", 1e-9);
    const int getIt2 = b.getVar("it", 2250, 600);
    const int p1 = b.print("Itérations du gradient conjugué : ", getIt2, "value", 1750, -320);
    const int p2 = b.print("Solveur de l'étudiant comparé à Cholesky : ", cmp, "report", 2000, -320);
    const int p3 = b.print("Solveur de l'étudiant conforme : ", cmp, "passed", 2250, -320);
    b.link(loop, "completed", p1, "exec");
    b.then(p1, p2);
    b.then(p2, p3);
    b.frame({ getX2, cmp, getIt2, p1, p2, p3 }, "Comparaison à la solution de référence\nécart relatif max|x − x_réf| / max|x_réf| ≤ 1e-9", "#558B2F");
    return b.ok(error);
}

// =============================================================================================
// Solveur de l'étudiant (mode 2) : itérations de Gauss-Seidel (composant expérimental)
// =============================================================================================
bool buildStudentGaussSeidel(Graph& g, std::string* error)
{
    g.clear();
    g.name = "Solveur de l'étudiant (Gauss-Seidel)";
    g.description = "Mode 2 — le bloc de résolution est remplacé par des itérations de Gauss-Seidel (composant expérimental du laboratoire de "
                    "l'étudiant) bouclées par « Tant que » jusqu'à un résidu relatif de 1e-10, sur le système réduit du portique ; solution "
                    "comparée à la résolution de référence (Cholesky). Gauss-Seidel converge pour une matrice symétrique définie positive, "
                    "d'autant plus lentement qu'elle est mal conditionnée : comparer le nombre d'itérations au gradient conjugué.";
    Builder b(g, NodeLibrary::standard());
    const auto [rs, ref, bc] = reducedSystemBlocks(b);
    const int zeros = b.node("vector.zeros", -500, 100);
    b.link(bc, "free", zeros, "n");
    const int start = b.node("event.start", -1500, -320);
    const int setX0 = b.setVar("x", zeros, "v", -1250, -320);
    const int setRes0 = b.setVar("res", 0, "", -1000, -320);
    b.value(setRes0, "value", 1.0);
    const int setIt0 = b.setVar("it", 0, "", -750, -320);
    b.value(setIt0, "value", 0LL);
    b.then(start, setX0);
    b.then(setX0, setRes0);
    b.then(setRes0, setIt0);
    // Condition : résidu > 1e-10 et it < 20 000
    const int getRes = b.getVar("res", 0, 480);
    const int getIt = b.getVar("it", 0, 640);
    const int c1 = b.node("math.compare", 250, 480);
    b.link(getRes, "value", c1, "a");
    b.value(c1, "b", 1e-10);
    b.value(c1, "op", std::string(">"));
    const int c2 = b.node("math.compare", 250, 640);
    b.link(getIt, "value", c2, "a");
    b.value(c2, "b", 20000.0);
    b.value(c2, "op", std::string("<"));
    const int cond = b.node("logic.and", 500, 560);
    b.link(c1, "result", cond, "a");
    b.link(c2, "result", cond, "b");
    const int loop = b.node("flow.while", -500, -320);
    b.link(cond, "result", loop, "condition");
    b.then(setIt0, loop);
    // Corps : une itération, résidu mémorisé, compteur
    const int getX = b.getVar("x", 0, 60);
    const int sweep = b.node("exp.gauss_seidel_sweep", 250, 100);
    b.link(rs, "K_ff", sweep, "K");
    b.link(rs, "rhs", sweep, "f");
    b.link(getX, "value", sweep, "x");
    const int itNew = b.node("math.add", 500, 300);
    b.link(getIt, "value", itNew, "a");
    b.value(itNew, "b", 1.0);
    const int setX = b.setVar("x", sweep, "x_next", 0, -320);
    const int setRes = b.setVar("res", sweep, "residual", 250, -320);
    const int setIt = b.setVar("it", itNew, "result", 500, -320);
    b.link(loop, "body", setX, "exec");
    b.then(setX, setRes);
    b.then(setRes, setIt);
    b.frame({ loop, getX, sweep, itNew, setX, setRes, setIt, getRes, getIt, c1, c2, cond },
            "Algorithme de l'étudiant : itérations de Gauss-Seidel\nx ← GS(K_ff, F_f − K_fc·d_c, x) tant que ‖K·x − f‖/‖f‖ > 1e-10 (20 000 au plus)",
            "#E65100");
    const int getX2 = b.getVar("x", 750, 420);
    const int cmp = b.node("vector.compare", 1000, 420);
    b.link(getX2, "value", cmp, "x");
    b.link(ref, "x", cmp, "reference");
    b.value(cmp, "tolerance", 1e-8);
    const int getIt2 = b.getVar("it", 1250, 600);
    const int p1 = b.print("Itérations de Gauss-Seidel : ", getIt2, "value", 750, -320);
    const int p2 = b.print("Gauss-Seidel comparé à Cholesky : ", cmp, "report", 1000, -320);
    const int p3 = b.print("Solveur de l'étudiant (Gauss-Seidel) conforme : ", cmp, "passed", 1250, -320);
    b.link(loop, "completed", p1, "exec");
    b.then(p1, p2);
    b.then(p2, p3);
    b.frame({ getX2, cmp, getIt2, p1, p2, p3 }, "Comparaison à la solution de référence\nécart relatif ≤ 1e-8 (résidu 1e-10 × conditionnement)", "#558B2F");
    return b.ok(error);
}

// =============================================================================================
// Mécanisme
// =============================================================================================
bool buildMechanism(Graph& g, std::string* error)
{
    g.clear();
    g.name = "Système instable : détection du mécanisme";
    g.description = "Poutre posée sur deux appuis simples verticaux : la translation suivant X n'est pas bloquée. Le calcul direct rend un message ; "
                    "le bloc de résolution du chemin pas à pas s'arrête avec le diagnostic (DDL au pivot nul, mode de corps rigide, causes).";
    Builder b(g, NodeLibrary::standard());
    const int model = b.node("struct.model_example", -900, 100);
    b.value(model, "name", std::string("poutre instable"));
    const int direct = b.node("struct.solve_direct", -650, 300);
    b.link(model, "model", direct, "model");
    const int loads = b.node("struct.loading", -650, 100);
    b.link(model, "model", loads, "model");
    const int dofs = b.node("struct.dof_numbering", -400, 100);
    b.link(model, "model", dofs, "model");
    const int K = b.node("struct.global_stiffness", -150, 0);
    b.link(model, "model", K, "model");
    b.link(dofs, "dofs", K, "dofs");
    const int F = b.node("struct.global_loads", -150, 160);
    b.link(model, "model", F, "model");
    b.link(dofs, "dofs", F, "dofs");
    b.link(loads, "loads", F, "loads");
    const int bc = b.node("struct.boundary_conditions", -150, 340);
    b.link(model, "model", bc, "model");
    b.link(dofs, "dofs", bc, "dofs");
    b.link(loads, "loads", bc, "loads");
    const int rs = b.node("struct.reduced_system", 100, 160);
    b.link(K, "K", rs, "K");
    b.link(F, "F", rs, "F");
    b.link(bc, "dofs", rs, "dofs");
    const int sol = b.node("struct.solve_displacements", 350, 160);
    b.link(rs, "system", sol, "system");
    b.link(model, "model", sol, "model");
    const int start = b.node("event.start", -900, -300);
    const int p1 = b.print("Calcul direct : ", direct, "message", -650, -300);
    const int p2 = b.print("Résolution pas à pas : ", sol, "report", 350, -300);
    b.then(start, p1);
    b.then(p1, p2);
    b.frame({ model, direct, loads, dofs, K, F, bc, rs, sol, p1, p2 },
            "Mécanisme\nla factorisation de K_ff rencontre un pivot nul : le bloc de résolution échoue avec le DDL, le mode de mécanisme et les causes",
            "#B71C1C");
    return b.ok(error);
}

// =============================================================================================
// Tassement d'appui (déplacement imposé non nul)
// =============================================================================================
bool buildSettlement(Graph& g, std::string* error)
{
    g.clear();
    g.name = "Tassement d'appui (déplacement imposé)";
    g.description = "Poutre continue à deux travées de 5 m : tassement de 5 mm de l'appui central (cas 2), traité par K_ff·d_f = F_f − K_fc·d_c ; "
                    "réaction centrale comparée à −6EIΔ/L³ calculée dans le graphe (E et Iz du catalogue).";
    Builder b(g, NodeLibrary::standard());
    const int model = b.node("struct.model_example", -900, 100);
    b.value(model, "name", std::string("poutre continue"));
    const int direct = b.node("struct.solve_direct", -650, 100);
    b.link(model, "model", direct, "model");
    b.value(direct, "case", 2LL);
    const int rn = b.node("struct.result_node", -400, 100);
    b.link(direct, "result", rn, "result");
    b.value(rn, "node", 2LL);
    const int mat = b.node("struct.material_catalog", -900, 420);
    const int sec = b.node("struct.section_catalog", -900, 600);
    const int ei = b.node("math.multiply", -650, 480);
    b.link(mat, "E", ei, "a");
    b.link(sec, "Iz", ei, "b");
    const int num = b.node("math.multiply", -400, 480);   // 6·Δ·EI avec Δ = 0,005 m
    b.link(ei, "result", num, "a");
    b.value(num, "b", 6.0 * 0.005);
    const int refAbs = b.node("math.divide", -150, 480);   // / L³, L = 5 m
    b.link(num, "result", refAbs, "a");
    b.value(refAbs, "b", 125.0);
    const int ref = b.node("math.multiply", 100, 480);     // réaction vers le bas
    b.link(refAbs, "result", ref, "a");
    b.value(ref, "b", -1.0);
    const int cmp = b.node("struct.compare_values", 350, 300);
    b.link(rn, "Ry", cmp, "value");
    b.link(ref, "result", cmp, "reference");
    b.value(cmp, "rel_tol", 1e-9);
    const int start = b.node("event.start", -900, -300);
    const int show = b.node("struct.show", -650, -300);
    b.link(direct, "result", show, "value");
    b.value(show, "title", std::string("Tassement d'appui"));
    const int p1 = b.print("Réaction de l'appui central (kN) — ", cmp, "report", -400, -300);
    const int p2 = b.print("Conforme à −6EIΔ/L³ : ", cmp, "passed", -150, -300);
    b.then(start, show);
    b.then(show, p1);
    b.then(p1, p2);
    b.frame({ model, direct, rn }, "Calcul avec déplacement imposé\ncas 2 : uy imposé = −5 mm en N2 (DDL bloqué), K_ff·d_f = F_f − K_fc·d_c", "#00796B");
    b.frame({ mat, sec, ei, num, refAbs, ref, cmp }, "Référence analytique\nR = −6·E·I·Δ / L³ (poutre continue, travées égales L)", "#558B2F");
    return b.ok(error);
}
} // namespace

const std::vector<Info>& catalog()
{
    static const std::vector<Info> list = {
        { "methode-deplacements-2d", "Methode_Des_Deplacements_2D",
          "Méthode de rigidité directe pas à pas (étapes 1 à 9) sur le portique de démonstration : K = Σ A_eᵀ·k_e·A_e assemblée dans une "
          "boucle, K_ff·d_f = F_f − K_fc·d_c, réactions, équilibre, efforts, visualisation ; comparée au calcul direct." },
        { "classe-frame-element", "Classe FrameElement2D (utilisation directe)",
          "Mode 1 : nœuds, matériau et section du catalogue → élément, longueur, angle, k', T, k = Tᵀk'T, charges nodales équivalentes." },
        { "solveur-etudiant", "Solveur de l'étudiant (gradient conjugué)",
          "Mode 2 : la résolution est écrite avec des blocs mathématiques et des variables, puis comparée à la solution de référence." },
        { "solveur-etudiant-gs", "Solveur de l'étudiant (Gauss-Seidel)",
          "Mode 2 : itérations de Gauss-Seidel (composant expérimental) bouclées jusqu'à convergence, comparées à la solution de référence." },
        { "mecanisme", "Système instable : détection du mécanisme",
          "Poutre sans appui horizontal : diagnostic du mécanisme (DDL, mode de corps rigide, causes possibles)." },
        { "tassement", "Tassement d'appui (déplacement imposé)",
          "Poutre continue, tassement de l'appui central : réaction comparée à −6EIΔ/L³ calculée dans le graphe." },
    };
    return list;
}

bool build(const std::string& id, Graph& graph, std::string* error)
{
    if (id == "methode-deplacements-2d") return buildMethod(graph, error);
    if (id == "classe-frame-element") return buildElementClass(graph, error);
    if (id == "solveur-etudiant") return buildStudentSolver(graph, error);
    if (id == "solveur-etudiant-gs") return buildStudentGaussSeidel(graph, error);
    if (id == "mecanisme") return buildMechanism(graph, error);
    if (id == "tassement") return buildSettlement(graph, error);
    if (error) *error = "exemple inconnu « " + id + " »";
    return false;
}

} // namespace TSALab::Research::StructuralGraphs
