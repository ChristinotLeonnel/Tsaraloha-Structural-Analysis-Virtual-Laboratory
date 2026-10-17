// Catégories F et G — dynamique des portiques plans (laboratoire pédagogique) et charges roulantes : matrice de masse,
// analyse modale, fonctions du temps et accélérogrammes FOURNIS par l'utilisateur, intégration de Newmark, spectres de
// réponse, analyse spectrale ; convois d'essieux, enveloppes et lignes d'influence (succession de calculs statiques).
// La science est celle de tsalab::structural (Dynamics2D, MovingLoads2D) ; ces blocs l'exposent étape par étape.

#include "NodeHelpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace TSALab::Research::Structural::detail
{

namespace
{
const std::string kDynUnits =
    "Unités : kN, m, s, t (1 t = 1 kN·s²/m), accélérations en m/s², pulsations en rad/s, fréquences en Hz.\n"
    "Laboratoire pédagogique : TSA (production) reste limité au calcul statique (ADR-022).";
const std::string kMovingNote =
    "Succession d'analyses STATIQUES linéaires (une par position du convoi, K factorisée une fois) : ni inertie, ni interaction "
    "véhicule-pont, ni coefficient de majoration dynamique (valeur normative à inclure par l'utilisateur dans la charge des essieux "
    "s'il y a lieu). Unités : kN, m.";

std::string simplified(const std::string& text)
{
    std::string t;
    for (char ch : text)
        if (!std::isspace(static_cast<unsigned char>(ch))) t += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return t;
}

bool startsWith(const std::string& t, const char* prefix)
{
    return t.rfind(prefix, 0) == 0;
}

bool parseMassModel(const std::string& text, S::MassModel& out)
{
    const std::string t = simplified(text);
    if (t.empty() || startsWith(t, "coh") || startsWith(t, "consist")) out = S::MassModel::Consistent;
    else if (startsWith(t, "conc") || t == "lumped" || startsWith(t, "diag")) out = S::MassModel::Lumped;
    else return false;
    return true;
}

bool massModel(ExecutionContext& c, S::MassModel& m)
{
    const std::string t = c.text("mass");
    if (c.failed()) return false;
    if (!parseMassModel(t, m)) return c.fail("modèle de masse inconnu « " + t + " » (cohérente, concentrée)");
    return true;
}

bool axis(ExecutionContext& c, const char* pin, int& direction)
{
    const std::string t = simplified(c.text(pin));
    if (c.failed()) return false;
    if (t == "x" || t == "0") direction = 0;
    else if (t == "y" || t == "1") direction = 1;
    else return c.fail("direction « " + c.text(pin) + " » : X ou Y");
    return true;
}

/// Liste d'identifiants « 1, 2, 3 » (séparateurs : virgule, point-virgule, espace).
bool parseIds(const std::string& text, std::vector<int>& out, std::string* error)
{
    out.clear();
    std::string t = text;
    for (char& ch : t)
        if (ch == ',' || ch == ';') ch = ' ';
    std::istringstream in(t);
    for (std::string tok; in >> tok;)
    {
        std::size_t used = 0;
        long long v = 0;
        try
        {
            v = std::stoll(tok, &used);
        }
        catch (...)
        {
            used = 0;
        }
        if (used != tok.size() || v <= 0)
        {
            if (error) *error = "identifiant invalide « " + tok + " » (entiers positifs séparés par des virgules)";
            return false;
        }
        out.push_back(static_cast<int>(v));
    }
    if (out.empty() && error) *error = "liste d'éléments vide";
    return !out.empty();
}

ObjectRef curveOut(std::vector<double> x, std::vector<double> y, std::string title, std::string xLabel, std::string yLabel)
{
    return make<CurveObject>(CurveData { std::move(x), std::move(y), std::move(title), std::move(xLabel), std::move(yLabel) });
}

const S::ModalResult* modalIn(ExecutionContext& c)
{
    const auto o = c.objectAs<ModalObject>("modal");
    return o ? &o->value() : nullptr;
}
} // namespace

// =============================================================================================
// F — Dynamique
// =============================================================================================
void registerDynamicsNodes(NodeLibrary& lib)
{
    lib.add(pureBlock("struct.mass_matrix", "MassMatrix", kDynamics, "Matrice de masse globale M (n × n) : masse des éléments (ρ·A) et masses nodales.",
                      { object("model", "Modèle", Type::Model), object("dofs", "DDL", Type::Dofs),
                        data("mass", "Modèle de masse (cohérente, concentrée)", VT::Text, std::string("cohérente")) },
                      { object("M", "M", Type::Matrix), data("total_x", "Masse de translation X (t)", VT::Real), data("total_y", "Masse de translation Y (t)", VT::Real) },
                      "M = Σ A_eᵀ·m_e·A_e + masses nodales, m_e = Tᵀ·m'·T avec m̄ = ρ·A (t/m).\n"
                      "Masse cohérente (fonctions de forme de la rigidité) : m̄L/6·[2 1 ; 1 2] en axial, m̄L/420·[156 22L 54 −13L ; 22L 4L² 13L −3L² ; "
                      "54 13L 156 −22L ; −13L −3L² −22L 4L²] en flexion ; aux rotules : Cᵀ·m'·C.\n"
                      "Masse concentrée : m̄L/2 sur les translations de chaque nœud, inertie de rotation nulle (rotations sans masse).\n"
                      "Masses de translation : ιᵀ·M·ι pour une translation d'ensemble suivant X ou Y (tous les DDL, appuis compris).\n"
                      + kDynUnits,
                      kSrcDynamics),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const DofSet* d = dofsIn(c);
                S::MassModel mm;
                if (!m || !d || !massModel(c, mm)) return false;
                Matrix M;
                std::string error;
                if (!S::assembleMass(*m, d->numbering, mm, M, &error)) return c.fail(error);
                double tx = 0.0, ty = 0.0;
                for (int i = 0; i < M.rows; ++i)
                    for (int j = 0; j < M.cols; ++j)
                    {
                        if (i % 3 == 0 && j % 3 == 0) tx += M(i, j);
                        if (i % 3 == 1 && j % 3 == 1) ty += M(i, j);
                    }
                const auto labels = dofLabels(d->numbering);
                c.setOutput("M", matrixOut(std::move(M), labels, labels, std::string("M (masse ") + (mm == S::MassModel::Lumped ? "concentrée)" : "cohérente)")));
                c.setOutput("total_x", tx);
                c.setOutput("total_y", ty);
                return true;
            });

    lib.add(pureBlock("struct.modal_analysis", "ModalAnalysis", kDynamics, "Modes propres K·φ = ω²·M·φ, facteurs de participation et masses effectives.",
                      { object("model", "Modèle", Type::Model), data("modes", "Nombre de modes (0 : tous)", VT::Integer, 6LL),
                        data("mass", "Modèle de masse (cohérente, concentrée)", VT::Text, std::string("cohérente")) },
                      { object("modal", "Modes", Type::Modal), object("frequencies", "f (Hz)", Type::Vector), object("periods", "T (s)", Type::Vector),
                        data("f1", "f1 (Hz)", VT::Real), data("summary", "Résumé", VT::Text) },
                      "Problème aux valeurs propres généralisé sur les DDL libres K_ff·φ = ω²·M_ff·φ (appuis et ressorts ; déplacements imposés "
                      "ignorés), résolu par réduction de Cholesky de K_ff (A = L⁻¹·M·L⁻ᵀ, Jacobi) : fréquences croissantes, modes normés par "
                      "rapport à la masse (φᵀ·M·φ = 1), signe tel que la plus grande composante soit positive ; modes de masse nulle écartés.\n"
                      "Participation Γ_k = φ_kᵀ·M_ff·ι (ι : translation d'ensemble des DDL libres suivant X ou Y), masse effective Γ_k² (t), "
                      "Σ Γ_k² = ιᵀ·M_ff·ι (masse mobilisable) sur tous les modes ; couplage de masse avec les appuis négligé (nul en masse "
                      "concentrée), comme d'usage.\n"
                      "Refus motivés : modèle invalide, mécanisme (K_ff non définie positive : DDL fautif et mode diagnostiqués), masse nulle.\n"
                      + kDynUnits,
                      kSrcDynamics),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const long long count = c.integer("modes");
                S::MassModel mm;
                if (!m || !massModel(c, mm) || c.failed()) return false;
                if (count < 0) return c.fail("nombre de modes négatif");
                S::ModalResult r = S::modalAnalysis(*m, static_cast<int>(count), mm);
                for (const auto& l : r.log) c.log(l);
                if (!r.success) return c.fail(r.message);
                std::vector<std::string> labels;
                for (std::size_t k = 0; k < r.omega.size(); ++k) labels.push_back("mode " + std::to_string(k + 1));
                c.setOutput("frequencies", vectorOut(r.frequency, labels, "f (Hz)"));
                c.setOutput("periods", vectorOut(r.period, labels, "T (s)"));
                c.setOutput("f1", r.frequency.front());
                c.setOutput("summary", summary(r));
                c.setOutput("modal", make<ModalObject>(std::move(r)));
                return true;
            });

    lib.add(pureBlock("struct.mode_shape", "ModeShape", kDynamics, "Déformée modale φ_k, pulsation, période, participation et masse effective d'un mode.",
                      { object("modal", "Modes", Type::Modal), data("mode", "Mode (1, 2…)", VT::Integer, 1LL) },
                      { object("phi", "φ_k", Type::Vector), data("omega", "ω (rad/s)", VT::Real), data("frequency", "f (Hz)", VT::Real),
                        data("period", "T (s)", VT::Real), data("gamma_x", "Γ X", VT::Real), data("gamma_y", "Γ Y", VT::Real),
                        data("meff_x", "Masse effective X (t)", VT::Real), data("meff_y", "Masse effective Y (t)", VT::Real) },
                      "φ_k sur tous les DDL (nul sur les DDL bloqués), normé par rapport à la masse (φᵀ·M·φ = 1, unité t^-½) ; f = ω/2π, T = 1/f.",
                      kSrcDynamics),
            [](ExecutionContext& c) {
                const S::ModalResult* r = modalIn(c);
                const long long k = c.integer("mode");
                if (!r || c.failed()) return false;
                if (k < 1 || k > static_cast<long long>(r->omega.size()))
                    return c.fail("mode " + std::to_string(k) + " hors de 1 à " + std::to_string(r->omega.size()));
                const auto i = static_cast<std::size_t>(k - 1);
                c.setOutput("phi", vectorOut(r->mode(static_cast<int>(i)), dofLabels(r->dofs), "φ" + std::to_string(k)));
                c.setOutput("omega", r->omega[i]);
                c.setOutput("frequency", r->frequency[i]);
                c.setOutput("period", r->period[i]);
                c.setOutput("gamma_x", r->participationX[i]);
                c.setOutput("gamma_y", r->participationY[i]);
                c.setOutput("meff_x", r->effectiveMassX[i]);
                c.setOutput("meff_y", r->effectiveMassY[i]);
                return true;
            });

    lib.add(pureBlock("struct.time_function", "Fonction du temps", kDynamics, "Fonction f(t) linéaire par morceaux saisie (t:valeur ; …).",
                      { data("points", "Points t:valeur", VT::Text, std::string("0:0 ; 0.1:1 ; 10:1")) }, { object("function", "f(t)", Type::TimeFunction) },
                      "Points (t en s, valeur) à temps strictement croissants, reliés linéairement ; la fonction est NULLE hors de [t_premier, "
                      "t_dernier] (fin d'un signal). Sert de facteur de charge F(t) = f(t)·F_cas, ou d'accélération du sol (bloc « Mouvement du "
                      "sol »).",
                      kSrcDynamics),
            [](ExecutionContext& c) {
                S::TimeFunction f;
                std::string error;
                const std::string text = c.text("points");
                if (c.failed()) return false;
                if (!parsePairs(text, f.points, &error) || !f.valid(&error)) return c.fail("fonction du temps : " + error);
                c.setOutput("function", make<TimeFunctionObject>(std::move(f)));
                return true;
            });

    lib.add(pureBlock("struct.harmonic_signal", "Signal harmonique de vérification", kDynamics,
                      "Signal analytique A·sin(2π·f·t) échantillonné (vérification des calculs, PAS un séisme).",
                      { data("amplitude", "A", VT::Real, 1.0), data("frequency", "f (Hz)", VT::Real, 1.0), data("duration", "Durée (s)", VT::Real, 2.0),
                        data("dt", "Pas d'échantillonnage (s)", VT::Real, 0.005) },
                      { object("function", "f(t)", Type::TimeFunction), data("label", "Description", VT::Text) },
                      "Signal de vérification dont la réponse d'un oscillateur à 1 DDL est connue analytiquement : sans amortissement, depuis "
                      "le repos, u(t) = −A/(ω² − Ω²)·[sin Ωt − (Ω/ω)·sin ωt] pour une accélération du sol A·sin Ωt. Ce n'est pas un "
                      "accélérogramme : TSALab ne fabrique aucun séisme ; un accélérogramme vient d'un fichier fourni (bloc « Accélérogramme »).",
                      kSrcDynamics),
            [](ExecutionContext& c) {
                const double A = c.real("amplitude"), f = c.real("frequency"), T = c.real("duration"), dt = c.real("dt");
                if (c.failed()) return false;
                if (!(f > 0.0) || !(T > 0.0) || !(dt > 0.0) || !std::isfinite(A)) return c.fail("fréquence, durée et pas strictement positifs requis");
                if (T / dt > 1e6) return c.fail("plus d'un million de points : augmenter le pas");
                S::TimeFunction s;
                const int n = static_cast<int>(std::ceil(T / dt - 1e-9));
                for (int k = 0; k <= n; ++k)
                {
                    const double t = std::min(k * dt, T);
                    s.points.emplace_back(t, A * std::sin(2.0 * std::acos(-1.0) * f * t));
                }
                c.setOutput("label", "signal harmonique de vérification " + num(A) + "·sin(2π·" + num(f) + "·t), " + num(T) + " s (analytique, pas un séisme)");
                c.setOutput("function", make<TimeFunctionObject>(std::move(s)));
                return true;
            });

    lib.add(pureBlock("struct.ground_motion", "Mouvement du sol", kDynamics, "Accélération du sol a_g(t) suivant X ou Y, depuis une fonction du temps.",
                      { object("function", "a_g(t) (m/s²)", Type::TimeFunction), data("direction", "Direction (X, Y)", VT::Text, std::string("X")),
                        data("source", "Provenance", VT::Text, std::string("saisie de l'utilisateur")) },
                      { object("motion", "Mouvement du sol", Type::GroundMotion) },
                      "Excitation par translation rigide du sol : forces effectives −M_ff·ι·a_g(t), déplacements calculés RELATIFS au sol. La "
                      "provenance est conservée et affichée avec les résultats (fichier, signal de vérification…).",
                      kSrcDynamics),
            [](ExecutionContext& c) {
                const auto f = c.objectAs<TimeFunctionObject>("function");
                int dir = 0;
                const std::string source = c.text("source");
                if (!f || !axis(c, "direction", dir) || c.failed()) return false;
                c.setOutput("motion", make<GroundMotionObject>(S::GroundMotion { dir, f->value(), source }));
                return true;
            });

    {
        NodeDefinition def = pureBlock(
            "struct.accelerogram_file", "Accélérogramme (fichier)", kDynamics, "Accélérogramme FOURNI lu dans un fichier texte (jamais modifié).",
            { data("path", "Fichier", VT::Text, std::string()), data("dt", "Pas (s) — 0 : colonnes t, a", VT::Real, 0.0),
              data("scale", "Facteur d'unité vers m/s²", VT::Real, 1.0), data("direction", "Direction (X, Y)", VT::Text, std::string("X")) },
            { object("motion", "Mouvement du sol", Type::GroundMotion), data("points", "Points", VT::Integer), data("peak", "Pic |a_g| (m/s²)", VT::Real),
              data("skipped", "Lignes ignorées", VT::Integer) },
            "Lecture sans supposer de format propriétaire : lignes à deux nombres (t, a), ou, si un pas dt > 0 est donné, valeurs successives "
            "(une ou plusieurs par ligne) au pas dt depuis t = 0. Les lignes contenant un élément non numérique (en-têtes, commentaires) sont "
            "ignorées et comptées ; séparateurs : espace, tabulation, virgule, point-virgule (point décimal). Facteur d'unité : par exemple "
            "9,80665 pour des valeurs en g (valeur conventionnelle de g). Le fichier est relu à chaque exécution et n'est jamais modifié ; "
            "aucun accélérogramme n'est fabriqué par TSALab.",
            kSrcDynamics);
        def.cacheable = false;   // le fichier peut changer entre deux exécutions
        lib.add(def, [](ExecutionContext& c) {
            const std::string path = c.text("path");
            const double dt = c.real("dt"), scale = c.real("scale");
            int dir = 0;
            if (!axis(c, "direction", dir) || c.failed()) return false;
            if (path.empty()) return c.fail("fichier d'accélérogramme non indiqué");
            const std::filesystem::path p(std::u8string(reinterpret_cast<const char8_t*>(path.data()), path.size()));
            std::error_code ec;
            const auto size = std::filesystem::file_size(p, ec);
            if (ec) return c.fail("fichier illisible « " + path + " » : " + ec.message());
            if (size > 64ull * 1024 * 1024) return c.fail("fichier de plus de 64 Mo refusé");
            std::ifstream in(p, std::ios::binary);
            if (!in) return c.fail("ouverture impossible de « " + path + " »");
            std::ostringstream buffer;
            buffer << in.rdbuf();
            S::GroundMotion g;
            g.direction = dir;
            std::string error;
            int skipped = 0;
            if (!S::parseTimeSeries(buffer.str(), dt, scale, g.acceleration, &error, &skipped)) return c.fail("« " + path + " » : " + error);
            double peak = 0.0;
            for (const auto& pt : g.acceleration.points) peak = std::max(peak, std::abs(pt.second));
            const auto name = p.filename().u8string();
            g.source = "fichier « " + std::string(reinterpret_cast<const char*>(name.data()), name.size()) + " » (" + std::to_string(g.acceleration.points.size())
                       + " points, facteur " + num(scale) + ")";
            c.log("Accélérogramme : " + g.source + (skipped ? ", " + std::to_string(skipped) + " ligne(s) non numérique(s) ignorée(s)" : std::string()));
            c.setOutput("points", static_cast<long long>(g.acceleration.points.size()));
            c.setOutput("peak", peak);
            c.setOutput("skipped", static_cast<long long>(skipped));
            c.setOutput("motion", make<GroundMotionObject>(std::move(g)));
            return true;
        });
    }

    lib.add(pureBlock("struct.rayleigh", "Amortissement de Rayleigh", kDynamics, "Coefficients a0, a1 de C = a0·M + a1·K donnant ξ aux pulsations ω1 et ω2.",
                      { data("xi", "ξ", VT::Real, 0.05), data("omega1", "ω1 (rad/s)", VT::Real, 10.0), data("omega2", "ω2 (rad/s)", VT::Real, 50.0) },
                      { data("a0", "a0 (1/s)", VT::Real), data("a1", "a1 (s)", VT::Real) },
                      "ξ(ω) = a0/(2ω) + a1·ω/2 ; ξ(ω1) = ξ(ω2) = ξ ⇒ a0 = 2ξ·ω1·ω2/(ω1 + ω2), a1 = 2ξ/(ω1 + ω2). Entre ω1 et ω2 l'amortissement est "
                      "plus faible, au-delà plus fort. Le taux ξ est une donnée de l'utilisateur (aucune valeur réglementaire n'est imposée).",
                      kSrcDynamics),
            [](ExecutionContext& c) {
                const double xi = c.real("xi"), w1 = c.real("omega1"), w2 = c.real("omega2");
                if (c.failed()) return false;
                if (!(xi >= 0.0 && xi < 1.0) || !(w1 > 0.0) || !(w2 > 0.0) || w1 == w2) return c.fail("0 ≤ ξ < 1, ω1 et ω2 positifs et distincts requis");
                const S::DampingSpec d = S::rayleighDamping(xi, w1, w2);
                c.setOutput("a0", d.a0);
                c.setOutput("a1", d.a1);
                return true;
            });

    lib.add(pureBlock("struct.time_history", "TimeHistoryAnalysis (Newmark)", kDynamics,
                      "Réponse temporelle linéaire par intégration de Newmark : charge F(t) = f(t)·F_cas et/ou accélération du sol.",
                      { object("model", "Modèle", Type::Model), data("dt", "Δt (s)", VT::Real, 0.01), data("duration", "Durée (s)", VT::Real, 2.0),
                        data("beta", "β", VT::Real, 0.25), data("gamma", "γ", VT::Real, 0.5),
                        data("mass", "Modèle de masse (cohérente, concentrée)", VT::Text, std::string("cohérente")), data("a0", "a0 (1/s)", VT::Real, 0.0),
                        data("a1", "a1 (s)", VT::Real, 0.0), data("case", "Cas de charge (0 : aucun)", VT::Integer, 0LL),
                        optionalObject("load_function", "f(t) de la charge", Type::TimeFunction), optionalObject("ground", "Mouvement du sol", Type::GroundMotion) },
                      { object("history", "Historique", Type::TimeHistory), data("peak", "max |u| (m ou rad)", VT::Real), data("summary", "Résumé", VT::Text) },
                      "M·ü + C·u̇ + K·u = f(t)·F − M·ι·a_g(t) sur les DDL libres, C = a0·M + a1·K (Rayleigh). Newmark : u_{n+1} et ü_{n+1} liés par "
                      "u_{n+1} = u_n + Δt·u̇_n + Δt²·[(½ − β)·ü_n + β·ü_{n+1}], u̇_{n+1} = u̇_n + Δt·[(1 − γ)·ü_n + γ·ü_{n+1}] ; rigidité effective "
                      "K + γ/(βΔt)·C + 1/(βΔt²)·M factorisée une fois (Cholesky). β = 1/4, γ = 1/2 (accélération moyenne) : inconditionnellement "
                      "stable, sans amortissement numérique, erreur de période ≈ (ωΔt)²/12 ; 2β < γ (accélération linéaire β = 1/6) : Δt limité "
                      "(contrôlé, refus au-delà). DDL sans masse (rotations en masse concentrée) : condensation statique exacte si a1 = 0, refus "
                      "motivé sinon. Départ au repos. Déplacements relatifs au sol pour un mouvement du sol. Les déplacements imposés "
                      "(multi-appuis) ne sont pas pris en charge. Cas de charge et fonction f(t) vont ensemble.\n"
                      + kDynUnits,
                      kSrcDynamics),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                S::TimeHistoryOptions o;
                o.dt = c.real("dt");
                o.duration = c.real("duration");
                o.beta = c.real("beta");
                o.gamma = c.real("gamma");
                o.damping = { c.real("a0"), c.real("a1") };
                const long long caseId = c.integer("case");
                if (!m || !massModel(c, o.mass) || c.failed()) return false;
                std::shared_ptr<const TimeFunctionObject> f;
                std::shared_ptr<const GroundMotionObject> g;
                if (c.hasValue("load_function") && !(f = c.objectAs<TimeFunctionObject>("load_function"))) return false;
                if (c.hasValue("ground") && !(g = c.objectAs<GroundMotionObject>("ground"))) return false;
                if ((caseId > 0) != static_cast<bool>(f)) return c.fail("charge dynamique : indiquer à la fois le cas de charge et sa fonction du temps f(t)");
                S::DynamicLoad load { static_cast<int>(caseId), f ? f->value() : S::TimeFunction {} };
                S::TimeHistoryResult r = S::timeHistory(*m, o, f ? &load : nullptr, g ? &g->value() : nullptr);
                for (const auto& l : r.log) c.log(l);
                if (!r.success) return c.fail(r.message);
                double peak = 0.0;
                for (double v : r.peakAbsDisplacement) peak = std::max(peak, v);
                c.setOutput("peak", peak);
                c.setOutput("summary", summary(r));
                c.setOutput("history", make<TimeHistoryObject>(std::move(r)));
                return true;
            });

    lib.add(pureBlock("struct.history_curve", "Historique d'un DDL", kDynamics, "Courbe temporelle d'un DDL (déplacement, vitesse, accélération), de l'énergie ou du signal.",
                      { object("history", "Historique", Type::TimeHistory), data("node", "Nœud", VT::Integer, 1LL), data("component", "Composante (ux, uy, θz)", VT::Text, std::string("ux")),
                        data("quantity", "Grandeur (déplacement, vitesse, accélération, énergie, sol)", VT::Text, std::string("déplacement")) },
                      { object("curve", "Courbe", Type::Curve), data("max_abs", "max |valeur|", VT::Real) },
                      "Déplacement (m ou rad), vitesse, accélération relatives au sol au DDL choisi ; « énergie » : ½u̇ᵀMu̇ + ½uᵀKu (kN·m, nœud "
                      "ignoré ; constante sans amortissement ni excitation avec β = 1/4, γ = 1/2) ; « sol » : a_g(t). Tracé : bloc « Courbe ».",
                      kSrcDynamics),
            [](ExecutionContext& c) {
                const auto h = c.objectAs<TimeHistoryObject>("history");
                const long long node = c.integer("node");
                const std::string comp = c.text("component"), q = simplified(c.text("quantity"));
                if (!h || c.failed()) return false;
                const S::TimeHistoryResult& r = h->value();
                std::vector<double> y;
                std::string label;
                if (startsWith(q, "én") || startsWith(q, "en") || q == "e")
                {
                    for (std::size_t k = 0; k < r.time.size(); ++k) y.push_back(r.kineticEnergy[k] + r.strainEnergy[k]);
                    label = "énergie (kN·m)";
                }
                else if (startsWith(q, "sol") || q == "ag")
                {
                    y = r.groundAcceleration;
                    label = "a_g (m/s²)";
                }
                else
                {
                    int component = 0;
                    if (!parseComponent(comp, component)) return c.fail("composante inconnue « " + comp + " » (ux, uy, θz)");
                    const int dof = r.dofs.dof(static_cast<int>(node), component);
                    if (dof < 0) return c.fail("nœud " + std::to_string(node) + " absent du modèle");
                    const std::vector<Vector>* series = &r.displacement;
                    label = "u";
                    if (startsWith(q, "vit") || q == "v") series = &r.velocity, label = "v";
                    else if (startsWith(q, "acc") || q == "a") series = &r.acceleration, label = "a";
                    else if (!(startsWith(q, "dép") || startsWith(q, "dep") || q == "u" || q == "d")) return c.fail("grandeur inconnue « " + c.text("quantity") + " »");
                    for (const auto& v : *series) y.push_back(v[static_cast<std::size_t>(dof)]);
                    label += " " + r.dofs.label(dof);
                }
                double mx = 0.0;
                for (double v : y) mx = std::max(mx, std::abs(v));
                c.setOutput("max_abs", mx);
                c.setOutput("curve", curveOut(r.time, std::move(y), label, "t (s)", label));
                return true;
            });

    lib.add(pureBlock("struct.history_forces", "Efforts à un instant", kDynamics, "Efforts d'extrémité d'un élément au pas le plus proche d'un instant donné.",
                      { object("history", "Historique", Type::TimeHistory), data("time", "t (s)", VT::Real, 0.0), data("element", "Élément", VT::Integer, 1LL) },
                      { object("forces", "f' local", Type::Vector), data("step", "Pas", VT::Integer), data("t", "t du pas (s)", VT::Real) },
                      "Efforts élastiques f' = k'·d'_élément + f(t)·f0' déduits des déplacements du pas (pratique usuelle des calculs par éléments "
                      "finis) ; les forces d'inertie réparties le long de la barre ne figurent pas entre les nœuds.",
                      kSrcDynamics),
            [](ExecutionContext& c) {
                const auto h = c.objectAs<TimeHistoryObject>("history");
                const double t = c.real("time");
                const long long element = c.integer("element");
                if (!h || c.failed()) return false;
                const S::TimeHistoryResult& r = h->value();
                if (r.time.empty()) return c.fail("historique vide");
                const auto it = std::min_element(r.time.begin(), r.time.end(), [t](double a, double b) { return std::abs(a - t) < std::abs(b - t); });
                const int step = static_cast<int>(it - r.time.begin());
                std::vector<S::ElementResult> er;
                std::string error;
                if (!S::elementResultsAt(r, step, 2, er, &error)) return c.fail(error);
                for (const auto& e : er)
                    if (e.element == element)
                    {
                        c.setOutput("forces", vectorOut(e.endForces, { "Fx i", "Fy i", "Mz i", "Fx j", "Fy j", "Mz j" },
                                                        "f' élément " + std::to_string(element) + " à t = " + num(*it) + " s"));
                        c.setOutput("step", static_cast<long long>(step));
                        c.setOutput("t", *it);
                        return true;
                    }
                return c.fail("élément " + std::to_string(element) + " absent du modèle");
            });

    lib.add(pureBlock("struct.response_spectrum", "Spectre de réponse (saisi)", kDynamics, "Spectre de pseudo-accélération Sa(T) saisi (T:Sa ; …).",
                      { data("points", "Points T:Sa (s, m/s²)", VT::Text, std::string()), data("source", "Provenance", VT::Text, std::string("spectre saisi par l'utilisateur")) },
                      { object("spectrum", "Spectre", Type::Spectrum), object("curve", "Courbe", Type::Curve) },
                      "Points (T en s, Sa en m/s²) à périodes strictement croissantes, interpolés linéairement ; une période hors de l'intervalle "
                      "est refusée par l'analyse (pas d'extrapolation). Aucun spectre réglementaire n'est construit par TSALab : un spectre de "
                      "règlement doit être saisi avec ses paramètres par l'utilisateur, qui en indique la provenance.",
                      kSrcDynamics),
            [](ExecutionContext& c) {
                S::ResponseSpectrum s;
                std::string error;
                const std::string text = c.text("points");
                s.source = c.text("source");
                if (c.failed()) return false;
                if (!parsePairs(text, s.points, &error) || !s.valid(&error)) return c.fail("spectre : " + error);
                std::vector<double> x, y;
                for (const auto& p : s.points) x.push_back(p.first), y.push_back(p.second);
                c.setOutput("curve", curveOut(std::move(x), std::move(y), "Sa(T)", "T (s)", "Sa (m/s²)"));
                c.setOutput("spectrum", make<SpectrumObject>(std::move(s)));
                return true;
            });

    lib.add(pureBlock("struct.accelerogram_spectrum", "Spectre d'un accélérogramme", kDynamics,
                      "Spectre de réponse élastique Sa(T) = ω²·max|u| d'un mouvement du sol fourni (oscillateurs à 1 DDL).",
                      { object("motion", "Mouvement du sol", Type::GroundMotion), data("periods", "Périodes (s)", VT::Text, std::string("0.05 0.1 0.2 0.3 0.5 0.75 1 1.5 2 3")),
                        data("xi", "ξ", VT::Real, 0.05) },
                      { object("spectrum", "Spectre", Type::Spectrum), object("curve", "Courbe", Type::Curve) },
                      "Pour chaque période T : u'' + 2ξω·u' + ω²·u = −a_g(t), départ au repos, Newmark (accélération moyenne) au pas min(pas du "
                      "signal, T/50) sur la durée du signal ; Sa = ω²·max|u| (pseudo-accélération). Relie l'analyse temporelle et l'analyse "
                      "spectrale d'un même signal (comparaison pédagogique).",
                      kSrcDynamics),
            [](ExecutionContext& c) {
                const auto g = c.objectAs<GroundMotionObject>("motion");
                const std::string text = c.text("periods");
                const double xi = c.real("xi");
                if (!g || c.failed()) return false;
                Vector periods;
                std::string error;
                if (!parseVector(text, periods, &error)) return c.fail("périodes : " + error);
                S::ResponseSpectrum s;
                if (!S::accelerogramSpectrum(g->value().acceleration, periods, xi, s, &error)) return c.fail(error);
                s.source += " — " + g->value().source;
                std::vector<double> x, y;
                for (const auto& p : s.points) x.push_back(p.first), y.push_back(p.second);
                c.setOutput("curve", curveOut(std::move(x), std::move(y), "Sa(T), ξ = " + num(xi), "T (s)", "Sa (m/s²)"));
                c.setOutput("spectrum", make<SpectrumObject>(std::move(s)));
                return true;
            });

    lib.add(pureBlock("struct.spectral_analysis", "SpectralAnalysis", kDynamics, "Réponse spectrale modale combinée (SRSS ou CQC) suivant X ou Y.",
                      { object("modal", "Modes", Type::Modal), object("spectrum", "Spectre", Type::Spectrum), data("direction", "Direction (X, Y)", VT::Text, std::string("X")),
                        data("combination", "Combinaison (SRSS, CQC)", VT::Text, std::string("CQC")), data("xi", "ξ (CQC)", VT::Real, 0.05) },
                      { object("spectral", "Réponse spectrale", Type::Spectral), data("base_shear", "Effort tranchant à la base (kN)", VT::Real),
                        data("mass_ratio", "Masse effective cumulée", VT::Real), object("peak", "u de pointe", Type::Vector), data("summary", "Résumé", VT::Text) },
                      "Mode k : Sd_k = Sa(T_k)/ω_k², u_k = φ_k·Γ_k·Sd_k, effort tranchant Γ_k²·Sa_k ; efforts d'extrémité modaux déduits de u_k. "
                      "Combinaison composante par composante : SRSS √(Σ r_k²) (modes indépendants) ou CQC √(Σ_i Σ_j ρ_ij·r_i·r_j), "
                      "ρ_ij = 8ξ²(1 + r)·r^1,5 / ((1 − r²)² + 4ξ²·r·(1 + r)²), r = ω_j/ω_i (Der Kiureghian, ξ commun). Valeurs de pointe "
                      "positives (signes perdus). Avertissement si la masse effective cumulée est inférieure au seuil usuel de 90 %. Période "
                      "hors du spectre : refus (pas d'extrapolation).\n"
                      + kDynUnits,
                      kSrcDynamics),
            [](ExecutionContext& c) {
                const S::ModalResult* modal = modalIn(c);
                const auto spec = c.objectAs<SpectrumObject>("spectrum");
                int dir = 0;
                const std::string comb = simplified(c.text("combination"));
                const double xi = c.real("xi");
                if (!modal || !spec || !axis(c, "direction", dir) || c.failed()) return false;
                S::ModalCombination mc;
                if (comb == "srss") mc = S::ModalCombination::SRSS;
                else if (comb == "cqc") mc = S::ModalCombination::CQC;
                else return c.fail("combinaison inconnue « " + c.text("combination") + " » (SRSS, CQC)");
                S::SpectralResult r = S::spectralAnalysis(*modal, spec->value(), dir, mc, xi);
                for (const auto& l : r.log) c.log(l);
                if (!r.success) return c.fail(r.message);
                c.setOutput("base_shear", r.combinedBaseShear);
                c.setOutput("mass_ratio", r.massRatio);
                c.setOutput("peak", vectorOut(r.peakDisplacement, dofLabels(r.dofs), "u de pointe"));
                c.setOutput("summary", summary(r));
                c.setOutput("spectral", make<SpectralObject>(std::move(r)));
                return true;
            });
}

// =============================================================================================
// G — Charges roulantes
// =============================================================================================
void registerMovingLoadNodes(NodeLibrary& lib)
{
    lib.add(pureBlock("struct.vehicle", "Convoi d'essieux", kMoving, "Convoi : essieux (décalage derrière le premier essieu : charge).",
                      { data("axles", "Essieux décalage:charge (m, kN)", VT::Text, std::string("0:100 ; 2:100")), data("name", "Nom", VT::Text, std::string("convoi")) },
                      { object("vehicle", "Convoi", Type::Vehicle), data("length", "Longueur (m)", VT::Real), data("total", "Charge totale (kN)", VT::Real) },
                      "Chaque essieu est une force verticale dirigée suivant −Y (charge positive), placée à `décalage` mètres derrière le premier "
                      "essieu. Les valeurs sont celles de l'utilisateur : TSALab ne fournit aucun modèle de charge réglementaire ni coefficient "
                      "dynamique.\n" + kMovingNote,
                      kSrcMoving),
            [](ExecutionContext& c) {
                std::vector<std::pair<double, double>> pairs;
                std::string error;
                const std::string text = c.text("axles");
                S::Vehicle v;
                v.name = c.text("name");
                if (c.failed()) return false;
                if (!parsePairs(text, pairs, &error)) return c.fail("essieux : " + error);
                double total = 0.0;
                for (const auto& [offset, load] : pairs) v.axles.push_back({ offset, load }), total += load;
                if (!v.valid(&error)) return c.fail(error);
                c.setOutput("length", v.length());
                c.setOutput("total", total);
                c.setOutput("vehicle", make<VehicleObject>(std::move(v)));
                return true;
            });

    lib.add(pureBlock("struct.moving_load", "MovingLoadAnalysis", kMoving, "Convoi déplacé pas à pas sur un chemin d'éléments : enveloppes des efforts.",
                      { object("model", "Modèle", Type::Model), object("vehicle", "Convoi", Type::Vehicle), data("path", "Chemin (éléments)", VT::Text, std::string("1")),
                        data("step", "Pas d'avancement (m)", VT::Real, 0.25), data("stations", "Sections par élément", VT::Integer, 21LL),
                        data("base_case", "Cas superposé (0 : aucun)", VT::Integer, 0LL) },
                      { object("result", "Enveloppes", Type::MovingLoad), data("m_max", "M max (kN·m)", VT::Real), data("m_min", "M min (kN·m)", VT::Real),
                        data("summary", "Résumé", VT::Text) },
                      "Le premier essieu parcourt le chemin (éléments dans l'ordre de marche, chaîne continue) de son entrée jusqu'à la sortie du "
                      "dernier essieu, au pas indiqué ; à chaque position, les essieux présents sur le chemin sont des forces ponctuelles sur les "
                      "éléments (suivant −Y) et une analyse statique est faite, superposée au cas de charge indiqué. Enveloppes min/max de N, V, M "
                      "en sections régulières de chaque élément (deux côtés d'une force placée sur la section), des déplacements et des réactions ; "
                      "position du convoi donnant M max et M min.\n" + kMovingNote,
                      kSrcMoving),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const auto v = c.objectAs<VehicleObject>("vehicle");
                const std::string path = c.text("path");
                S::MovingLoadOptions o;
                o.step = c.real("step");
                o.stations = static_cast<int>(c.integer("stations"));
                o.baseCase = static_cast<int>(c.integer("base_case"));
                if (!m || !v || c.failed()) return false;
                std::string error;
                if (!parseIds(path, o.path, &error)) return c.fail("chemin : " + error);
                S::MovingLoadResult r = S::movingLoadAnalysis(*m, v->value(), o);
                for (const auto& l : r.log) c.log(l);
                if (!r.success) return c.fail(r.message);
                c.setOutput("m_max", r.maxMoment()->Mmax);
                c.setOutput("m_min", r.minMoment()->Mmin);
                c.setOutput("summary", summary(r));
                c.setOutput("result", make<MovingLoadObject>(std::move(r)));
                return true;
            });

    lib.add(pureBlock("struct.envelope_curve", "Enveloppe d'un élément", kMoving, "Courbe enveloppe (max ou min) de N, V ou M le long d'un élément.",
                      { object("result", "Enveloppes", Type::MovingLoad), data("element", "Élément", VT::Integer, 1LL),
                        data("quantity", "Grandeur (N, V, M)", VT::Text, std::string("M")), data("bound", "Borne (max, min)", VT::Text, std::string("max")) },
                      { object("curve", "Courbe", Type::Curve), data("extreme", "Extrême", VT::Real) },
                      "Valeurs extrêmes, sur toutes les positions du convoi, en chaque section de l'élément (x depuis son nœud i). Convention RDM : "
                      "N > 0 en traction, M > 0 tend la fibre y' < 0.",
                      kSrcMoving),
            [](ExecutionContext& c) {
                const auto r = c.objectAs<MovingLoadObject>("result");
                const long long element = c.integer("element");
                const std::string q = simplified(c.text("quantity")), bound = simplified(c.text("bound"));
                if (!r || c.failed()) return false;
                if (q != "n" && q != "v" && q != "m") return c.fail("grandeur inconnue « " + c.text("quantity") + " » (N, V, M)");
                if (bound != "max" && bound != "min") return c.fail("borne inconnue « " + c.text("bound") + " » (max, min)");
                std::vector<double> x, y;
                for (const auto& s : r->value().stations)
                    if (s.element == element)
                    {
                        x.push_back(s.x);
                        const bool mx = bound == "max";
                        y.push_back(q == "n" ? (mx ? s.Nmax : s.Nmin) : q == "v" ? (mx ? s.Vmax : s.Vmin) : (mx ? s.Mmax : s.Mmin));
                    }
                if (x.empty()) return c.fail("élément " + std::to_string(element) + " absent des enveloppes");
                const double ext = bound == "max" ? *std::max_element(y.begin(), y.end()) : *std::min_element(y.begin(), y.end());
                const std::string name = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(q[0])))) + " " + bound;
                c.setOutput("extreme", ext);
                c.setOutput("curve", curveOut(std::move(x), std::move(y), name + " élément " + std::to_string(element), "x (m)", name));
                return true;
            });

    lib.add(pureBlock("struct.influence_line", "Ligne d'influence", kMoving, "Ligne d'influence d'une grandeur pour une force unitaire de 1 kN suivant −Y.",
                      { object("model", "Modèle", Type::Model), data("path", "Chemin (éléments)", VT::Text, std::string("1")), data("step", "Pas (m)", VT::Real, 0.25),
                        data("quantity", "Grandeur (M, V, N, déplacement, réaction)", VT::Text, std::string("M")), data("element", "Élément", VT::Integer, 1LL),
                        data("x", "x dans l'élément (m)", VT::Real, 0.0), data("node", "Nœud", VT::Integer, 1LL),
                        data("component", "Composante (ux, uy, θz)", VT::Text, std::string("uy")) },
                      { object("curve", "Courbe", Type::Curve), data("min", "Minimum", VT::Real), data("max", "Maximum", VT::Real), data("label", "Grandeur", VT::Text) },
                      "Valeur de la grandeur (efforts M, V, N en une section x d'un élément ; déplacement ou réaction d'appui d'un nœud) quand une "
                      "force de 1 kN dirigée suivant −Y occupe chaque position du chemin. V en x : limite à gauche d'une force placée en x. "
                      "Théorème de Müller-Breslau (vérification) : la ligne d'influence a la forme de la déformée associée.\n" + kMovingNote,
                      kSrcMoving),
            [](ExecutionContext& c) {
                const S::Model2D* m = modelIn(c);
                const std::string path = c.text("path"), q = simplified(c.text("quantity")), comp = c.text("component");
                const double step = c.real("step"), x = c.real("x");
                const long long element = c.integer("element"), node = c.integer("node");
                if (!m || c.failed()) return false;
                std::vector<int> ids;
                std::string error;
                if (!parseIds(path, ids, &error)) return c.fail("chemin : " + error);
                S::ResponseQuantity rq;
                using K = S::ResponseQuantity::Kind;
                rq.element = static_cast<int>(element);
                rq.x = x;
                rq.node = static_cast<int>(node);
                if (q == "m") rq.kind = K::BendingMoment;
                else if (q == "v") rq.kind = K::ShearForce;
                else if (q == "n") rq.kind = K::AxialForce;
                else if (startsWith(q, "dép") || startsWith(q, "dep") || q == "u") rq.kind = K::Displacement;
                else if (startsWith(q, "réa") || startsWith(q, "rea") || q == "r") rq.kind = K::Reaction;
                else return c.fail("grandeur inconnue « " + c.text("quantity") + " » (M, V, N, déplacement, réaction)");
                if ((rq.kind == K::Displacement || rq.kind == K::Reaction) && !parseComponent(comp, rq.component))
                    return c.fail("composante inconnue « " + comp + " » (ux, uy, θz)");
                const S::InfluenceLine il = S::influenceLine(*m, ids, step, rq);
                if (!il.success) return c.fail(il.message);
                c.log(il.message);
                c.setOutput("min", il.minimum);
                c.setOutput("max", il.maximum);
                c.setOutput("label", il.quantity);
                c.setOutput("curve", curveOut(il.s, il.value, "ligne d'influence de " + il.quantity, "position de la force unitaire (m)", il.quantity));
                return true;
            });
}

} // namespace TSALab::Research::Structural::detail
