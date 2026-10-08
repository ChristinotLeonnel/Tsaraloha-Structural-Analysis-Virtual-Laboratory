// Plugin d'exemple de l'écosystème Tsaraloha (DLL chargée par TSA et TSALab depuis <application>/plugins).
// Il n'utilise que l'API de plugins (TSA/src/Plugins/PluginApi.h, bibliothèque standard seule) :
//   - commande « sample.portal » : portique encastré composé des commandes du registre central (chaque
//     étape garde son entrée Annuler) ; disponible dans la console, le Blueprint (nœud) et pour l'IA ;
//   - nœud pur « sample.golden » : x × φ (nombre d'or).

#include "Plugins/PluginApi.h"

#include <cmath>

namespace
{
using namespace TSA;
using Automation::Arguments;
using Automation::CommandResult;
using Automation::ParameterSpec;
using Automation::Quantity;
using Automation::ValueType;

ParameterSpec parameter(std::string name, std::string label, ValueType type, Quantity q, Automation::Value def = {})
{
    ParameterSpec p;
    p.name = std::move(name);
    p.label = std::move(label);
    p.type = type;
    p.quantity = q;
    p.required = std::holds_alternative<std::monostate>(def);
    p.defaultValue = std::move(def);
    return p;
}

CommandResult failure(const std::string& message)
{
    CommandResult r;
    r.message = message;
    return r;
}

class SamplePlugin final : public Plugins::IPlugin
{
public:
    Plugins::PluginInfo info() const override
    {
        return { "tsalab.sample", "Plugin d'exemple TSALab", "1.0.0",
                 "Portique encastré (commande composée) et nombre d'or (nœud Blueprint pur)." };
    }

    bool initialize(Plugins::IPluginHost& host, std::string* error) override
    {
        Automation::CommandSpec portal;
        portal.id = "sample.portal";
        portal.title = "Portique encastré (plugin)";
        portal.category = "Plugins";
        portal.description = "Crée un portique : 2 poteaux encastrés, une traverse IPE 300 et une charge répartie "
                             "(plugin d'exemple, composé des commandes du registre central).";
        portal.parameters = { parameter("span", "Portée", ValueType::Real, Quantity::Length, 6.0),
                              parameter("height", "Hauteur", ValueType::Real, Quantity::Length, 3.0),
                              parameter("q", "Charge", ValueType::Real, Quantity::ForcePerLength, 10.0) };
        portal.outputs = { parameter("beam", "Traverse", ValueType::Integer, Quantity::None, 0LL) };
        const bool commandOk = host.addCommand(portal, [](Plugins::ICommandContext& c, const Arguments& a) {
            const double L = std::get<double>(a.at("span")), H = std::get<double>(a.at("height")), q = std::get<double>(a.at("q"));
            if (L <= 0.0 || H <= 0.0) return failure("Portée et hauteur doivent être positives.");
            auto id = [](const CommandResult& r) { return r.ok ? std::get<long long>(r.outputs.at("id")) : -1LL; };
            auto node = [&](double x, double z) { return id(c.execute("model.create_node", { { "position", Automation::Point3 { x, 0.0, z } } })); };
            const long long n1 = node(0, 0), n2 = node(0, H), n3 = node(L, H), n4 = node(L, 0);
            if (n1 < 0 || n2 < 0 || n3 < 0 || n4 < 0) return failure("Création des nœuds impossible.");
            for (const auto& [a0, b0] : { std::pair { n1, n2 }, std::pair { n4, n3 } })
                if (!c.execute("model.create_column", { { "start", a0 }, { "end", b0 } }).ok) return failure("Création d'un poteau impossible.");
            const long long beam = id(c.execute("model.create_beam", { { "start", n2 }, { "end", n3 }, { "section", std::string("IPE 300") } }));
            if (beam < 0) return failure("Création de la traverse impossible.");
            for (long long n : { n1, n4 })
                c.execute("model.set_support", { { "node", n }, { "type", std::string("fixed") } });
            const long long lc = id(c.execute("loads.create_case", { { "name", std::string("Plugin — exploitation") }, { "category", std::string("live") } }));
            if (lc < 0 || !c.execute("loads.add_uniform", { { "beam", beam }, { "case", lc }, { "q", q } }).ok)
                return failure("Charge répartie impossible.");
            CommandResult r;
            r.ok = true;
            r.message = "Portique du plugin : portée " + std::to_string(L) + " m, hauteur " + std::to_string(H) + " m, traverse #"
                        + std::to_string(beam) + ".";
            r.outputs["beam"] = beam;
            return r;
        });

        Blueprint::NodeDefinition golden;
        golden.id = "sample.golden";
        golden.title = "Nombre d'or (plugin)";
        golden.category = "Plugins";
        golden.description = "y = x × φ, φ = (1 + √5) / 2 (nœud pur du plugin d'exemple).";
        golden.pure = true;
        Blueprint::PinSpec x;
        x.name = "x";
        x.label = "x";
        x.type = ValueType::Real;
        x.defaultValue = 1.0;
        Blueprint::PinSpec y;
        y.name = "y";
        y.label = "y";
        y.type = ValueType::Real;
        golden.inputs = { x };
        golden.outputs = { y };
        const bool nodeOk = host.addNode(golden, [](Plugins::INodeContext& c) {
            const Automation::Value v = c.input("x");
            const double* xv = std::get_if<double>(&v);
            if (!xv) return c.fail("x : réel attendu");
            c.setOutput("y", *xv * (1.0 + std::sqrt(5.0)) / 2.0);
            return true;
        });

        host.log("Plugin d'exemple initialisé pour " + host.applicationName() + ".");
        if (!commandOk || !nodeOk)
        {
            if (error) *error = "commande ou nœud déjà enregistré";
            return false;
        }
        return true;
    }
};
} // namespace

TSARALOHA_PLUGIN(SamplePlugin)
