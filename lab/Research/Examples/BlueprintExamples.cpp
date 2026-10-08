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
} // namespace

bool build(const std::string& id, Graph& graph)
{
    graph.clear();
    if (id == "parametric-portal") return buildPortal(graph);
    if (id == "node-row") return buildNodeRow(graph);
    if (id == "validation-bench") return buildBench(graph);
    return false;
}

} // namespace TSALab::Research::BlueprintExamples
