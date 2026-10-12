#include "BlueprintExamples.h"

#include "Blueprint/BlueprintRuntime.h"

namespace TSALab::Research::BlueprintExamples
{

using namespace TSA::Blueprint;

const std::vector<Info>& catalog()
{
    static const std::vector<Info> list = {
        { "parametric-portal", "Portique paramétrique",
          "Portée, hauteur et charge en paramètres : 4 nœuds, 2 poteaux HEB 200, 1 poutre IPE 300, encastrements, "
          "cas « Exploitation » et charge répartie." },
        { "node-row", "Rangée de nœuds (boucle)", "Boucle « Pour » : n nœuds espacés d'un pas, paramètres « nombre » et « pas »." },
        { "validation-bench", "Banc de validation des solveurs",
          "Nœud TSALab : benchmarks analytiques + validation croisée de tous les solveurs plans, rapport dans le journal." },
        { "simply-supported-experiment", "Expérience : poutre bi-appuyée",
          "Chaîne complète en nœuds : portée, charge et section en paramètres ; géométrie, affectation de section, appuis, "
          "force centrale, calcul Custom2D sans poids propre ; réaction, moment et flèche comparés à P/2, P·L/4 et "
          "P·L³/(48·E·I) (E et I lus sur la barre)." },
    };
    return list;
}

namespace
{
/// Aide à la construction : nœuds placés en colonnes, liens contrôlés par la bibliothèque.
class Builder
{
public:
    explicit Builder(Graph& g) : m_g(g), m_lib(NodeLibrary::standard()) {}

    int node(const std::string& type, double x, double y) { return m_g.addNode(type, x, y); }
    void value(int n, const std::string& pin, const Value& v) { m_g.setValue(n, pin, v); }
    bool link(int a, const std::string& out, int b, const std::string& in) { return m_lib.connect(m_g, { a, out, b, in }); }
    /// Chaîne d'exécution « then » → « exec ».
    bool chain(const std::vector<int>& nodes)
    {
        for (std::size_t i = 1; i < nodes.size(); ++i)
            if (!link(nodes[i - 1], "then", nodes[i], "exec")) return false;
        return true;
    }
    int param(const std::string& type, const std::string& name, const Value& v, double x, double y)
    {
        const int p = node(type, x, y);
        value(p, "name", name);
        value(p, "value", v);
        return p;
    }

private:
    Graph& m_g;
    const NodeLibrary& m_lib;
};

bool buildPortal(Graph& g)
{
    g.name = "Portique paramétrique";
    g.description = "Portique plan encastré construit par les commandes du registre central.";
    Builder b(g);
    const int L = b.param("param.real", "portée", 6.0, -700, 300);
    const int H = b.param("param.real", "hauteur", 3.0, -700, 420);
    const int Q = b.param("param.real", "q", 12.0, -700, 540);

    const int pB = b.node("math.make_point", -450, 380);
    const int pC = b.node("math.make_point", -450, 500);
    const int pD = b.node("math.make_point", -450, 620);
    bool ok = b.link(H, "value", pB, "z") && b.link(L, "value", pC, "x") && b.link(H, "value", pC, "z") && b.link(L, "value", pD, "x");

    const int start = b.node("event.start", -700, 0);
    const int nA = b.node("cmd.model.create_node", -450, 0);
    b.value(nA, "position", TSA::Blueprint::Point3 { 0, 0, 0 });
    const int nB = b.node("cmd.model.create_node", -200, 0);
    const int nC = b.node("cmd.model.create_node", 50, 0);
    const int nD = b.node("cmd.model.create_node", 300, 0);
    ok = ok && b.link(pB, "point", nB, "position") && b.link(pC, "point", nC, "position") && b.link(pD, "point", nD, "position");

    const int colL = b.node("cmd.model.create_column", 550, 0);
    const int beam = b.node("cmd.model.create_beam", 800, 0);
    const int colR = b.node("cmd.model.create_column", 1050, 0);
    b.value(colL, "section", std::string("HEB 200"));
    b.value(colR, "section", std::string("HEB 200"));
    b.value(beam, "section", std::string("IPE 300"));
    ok = ok && b.link(nA, "id", colL, "start") && b.link(nB, "id", colL, "end") && b.link(nB, "id", beam, "start")
         && b.link(nC, "id", beam, "end") && b.link(nD, "id", colR, "start") && b.link(nC, "id", colR, "end");

    const int supA = b.node("cmd.model.set_support", 1300, 0);
    const int supD = b.node("cmd.model.set_support", 1550, 0);
    b.value(supA, "type", std::string("fixed"));
    b.value(supD, "type", std::string("fixed"));
    ok = ok && b.link(nA, "id", supA, "node") && b.link(nD, "id", supD, "node");

    const int lc = b.node("cmd.loads.create_case", 1800, 0);
    b.value(lc, "name", std::string("Exploitation"));
    b.value(lc, "category", std::string("live"));
    const int load = b.node("cmd.loads.add_uniform", 2050, 0);
    ok = ok && b.link(beam, "id", load, "beam") && b.link(lc, "id", load, "case") && b.link(Q, "value", load, "q");
    const int summary = b.node("cmd.query.model_summary", 2300, 0);

    return ok && b.chain({ start, nA, nB, nC, nD, colL, beam, colR, supA, supD, lc, load, summary });
}

bool buildNodeRow(Graph& g)
{
    g.name = "Rangée de nœuds";
    Builder b(g);
    const int start = b.node("event.start", -600, 0);
    const int count = b.param("param.integer", "nombre", 5LL, -600, 200);
    const int step = b.param("param.real", "pas", 2.5, -600, 320);
    const int last = b.node("math.subtract", -350, 200);
    b.value(last, "b", 1.0);
    const int lastInt = b.node("math.to_integer", -150, 200);
    const int loop = b.node("flow.for", -150, 0);
    const int x = b.node("math.multiply", 100, 200);
    const int point = b.node("math.make_point", 300, 200);
    const int create = b.node("cmd.model.create_node", 350, 0);
    const int print = b.node("debug.print", 350, -150);
    b.value(print, "value", std::string("Rangée de nœuds créée."));
    return b.link(start, "then", loop, "exec") && b.link(count, "value", last, "a") && b.link(last, "result", lastInt, "value")
           && b.link(lastInt, "result", loop, "last") && b.link(loop, "index", x, "a") && b.link(step, "value", x, "b")
           && b.link(x, "result", point, "x") && b.link(loop, "body", create, "exec") && b.link(point, "point", create, "position")
           && b.link(loop, "completed", print, "exec");
}

bool buildBench(Graph& g)
{
    g.name = "Banc de validation";
    Builder b(g);
    const int start = b.node("event.start", -400, 0);
    const int bench = b.node("science.validation_bench", -150, 0);
    const int print = b.node("debug.print", 150, 0);
    return b.link(start, "then", bench, "exec") && b.link(bench, "then", print, "exec") && b.link(bench, "report", print, "value");
}
bool buildExperiment(Graph& g)
{
    g.name = "Expérience : poutre bi-appuyée";
    g.description = "Poutre bi-appuyée, force centrale : calcul et comparaison aux formules d'Euler-Bernoulli, uniquement par des nœuds.";
    Builder b(g);
    const int L = b.param("param.real", "portée", 6.0, -900, 300);
    const int P = b.param("param.real", "P", 50.0, -900, 420);
    const int S = b.param("param.text", "section", std::string("IPE 300"), -900, 540);

    // Géométrie : A (0), C (L/2), B (L)
    const int half = b.node("math.divide", -650, 300);
    b.value(half, "b", 2.0);
    const int pC = b.node("math.make_point", -450, 300);
    const int pB = b.node("math.make_point", -450, 420);
    bool ok = b.link(L, "value", half, "a") && b.link(half, "result", pC, "x") && b.link(L, "value", pB, "x");

    const int start = b.node("event.start", -900, 0);
    const int nA = b.node("cmd.model.create_node", -650, 0);
    b.value(nA, "position", TSA::Blueprint::Point3 { 0, 0, 0 });
    const int nC = b.node("cmd.model.create_node", -400, 0);
    const int nB = b.node("cmd.model.create_node", -150, 0);
    ok = ok && b.link(pC, "point", nC, "position") && b.link(pB, "point", nB, "position");
    const int b1 = b.node("cmd.model.create_beam", 100, 0);
    const int b2 = b.node("cmd.model.create_beam", 350, 0);
    ok = ok && b.link(nA, "id", b1, "start") && b.link(nC, "id", b1, "end") && b.link(nC, "id", b2, "start") && b.link(nB, "id", b2, "end");

    // Propriétés affectées après coup (nœud d'affectation), appuis, cas et force
    const int s1 = b.node("cmd.model.set_section", 600, 0);
    const int s2 = b.node("cmd.model.set_section", 850, 0);
    b.value(s1, "material", std::string("S235"));
    b.value(s2, "material", std::string("S235"));
    ok = ok && b.link(b1, "id", s1, "bar") && b.link(S, "value", s1, "section") && b.link(b2, "id", s2, "bar") && b.link(S, "value", s2, "section");
    const int supA = b.node("cmd.model.set_support", 1100, 0);
    const int supB = b.node("cmd.model.set_support", 1350, 0);
    b.value(supA, "type", std::string("pinned"));
    b.value(supB, "type", std::string("roller"));
    ok = ok && b.link(nA, "id", supA, "node") && b.link(nB, "id", supB, "node");
    const int lc = b.node("cmd.loads.create_case", 1600, 0);
    b.value(lc, "name", std::string("P"));
    b.value(lc, "category", std::string("live"));
    const int fz = b.node("math.multiply", 1600, 300);
    b.value(fz, "b", -1.0);
    const int load = b.node("cmd.loads.add_nodal", 1850, 0);
    ok = ok && b.link(P, "value", fz, "a") && b.link(nC, "id", load, "node") && b.link(lc, "id", load, "case") && b.link(fz, "result", load, "fz");

    // Calcul (hypothèse de la formule : poids propre exclu)
    const int run = b.node("cmd.analysis.run", 2100, 0);
    b.value(run, "engine", std::string("custom2d"));
    b.value(run, "axis", std::string("plan"));
    b.value(run, "self_weight", std::string("non"));

    // Extraction
    const int bar = b.node("cmd.query.bar", 2350, 0);
    const int disp = b.node("cmd.results.node_displacement", 2600, 0);
    const int reac = b.node("cmd.results.node_reaction", 2850, 0);
    const int forces = b.node("cmd.results.member_forces", 3100, 0);
    ok = ok && b.link(b1, "id", bar, "id") && b.link(nC, "id", disp, "id") && b.link(nA, "id", reac, "id") && b.link(b1, "id", forces, "id");

    // Références : δ = −P·L³/(48·E·I), R = P/2, M = P·L/4
    const int l2 = b.node("math.multiply", 2350, 300), l3 = b.node("math.multiply", 2350, 420);
    const int ei = b.node("math.multiply", 2600, 300), ei48 = b.node("math.multiply", 2600, 420);
    const int num = b.node("math.multiply", 2850, 300), delta = b.node("math.divide", 2850, 420);
    b.value(ei48, "b", 48.0);
    ok = ok && b.link(L, "value", l2, "a") && b.link(L, "value", l2, "b") && b.link(l2, "result", l3, "a") && b.link(L, "value", l3, "b") &&
         b.link(bar, "e", ei, "a") && b.link(bar, "iy", ei, "b") && b.link(ei, "result", ei48, "a") && b.link(fz, "result", num, "a") &&
         b.link(l3, "result", num, "b") && b.link(num, "result", delta, "a") && b.link(ei48, "result", delta, "b");
    const int rRef = b.node("math.divide", 3100, 300);
    b.value(rRef, "b", 2.0);
    const int pl = b.node("math.multiply", 3350, 300), mRef = b.node("math.divide", 3350, 420);
    b.value(mRef, "b", 4.0);
    ok = ok && b.link(P, "value", rRef, "a") && b.link(P, "value", pl, "a") && b.link(L, "value", pl, "b") && b.link(pl, "result", mRef, "a");
    const int uz = b.node("math.break_point", 3100, 540), rz = b.node("math.break_point", 3350, 540);
    ok = ok && b.link(disp, "displacement", uz, "point") && b.link(reac, "force", rz, "point");

    // Comparaisons (tolérance relative 1e-6 par défaut), verdicts dans le journal
    const int cD = b.node("cmd.compare.values", 3350, 0);
    const int cR = b.node("cmd.compare.values", 3600, 0);
    const int cM = b.node("cmd.compare.values", 3850, 0);
    ok = ok && b.link(uz, "z", cD, "value") && b.link(delta, "result", cD, "reference") && b.link(rz, "z", cR, "value") && b.link(rRef, "result", cR, "reference") &&
         b.link(forces, "m_max", cM, "value") && b.link(mRef, "result", cM, "reference");
    const int all = b.node("logic.and", 4100, 300), all3 = b.node("logic.and", 4100, 420);
    ok = ok && b.link(cD, "passed", all, "a") && b.link(cR, "passed", all, "b") && b.link(all, "result", all3, "a") && b.link(cM, "passed", all3, "b");
    const int verdict = b.node("text.concat", 4350, 300);
    b.value(verdict, "a", std::string("Expérience conforme à la référence (flèche, réaction, moment) : "));
    const int print = b.node("debug.print", 4100, 0);
    ok = ok && b.link(all3, "result", verdict, "b") && b.link(verdict, "result", print, "value");

    return ok && b.chain({ start, nA, nC, nB, b1, b2, s1, s2, supA, supB, lc, load, run, bar, disp, reac, forces, cD, cR, cM, print });
}
} // namespace

bool build(const std::string& id, Graph& graph)
{
    graph.clear();
    if (id == "parametric-portal") return buildPortal(graph);
    if (id == "node-row") return buildNodeRow(graph);
    if (id == "validation-bench") return buildBench(graph);
    if (id == "simply-supported-experiment") return buildExperiment(graph);
    return false;
}

} // namespace TSALab::Research::BlueprintExamples
