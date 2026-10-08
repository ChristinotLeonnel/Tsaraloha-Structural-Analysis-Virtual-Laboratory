// Pont OpenSees 2D du cœur scientifique (C++ pur) : contrat tsalab::planar → script Tcl → processus
// OpenSees → résultats au format du contrat. Le chargement est d'abord réduit par le pont commun
// (MddBridge : cas, combinaison, poids propre en charges locales), puis traduit en commandes OpenSees.
//
// Modèle OpenSees : BasicBuilder -ndm 2 -ndf 3 ; barres fléchies elasticBeamColumn (geomTransf Linear) ;
// barres articulées truss (charges de barre ramenées aux nœuds par la règle du levier, exacte pour une
// barre bi-articulée) ; appuis élastiques zeroLength reliés à un nœud auxiliaire encastré ; nœuds reliés
// seulement à des barres articulées : rotation bloquée (aucune rigidité, réaction nulle).
// Analyse : statique linéaire (Plain, RCM, BandGeneral, Linear, LoadControl 1, analyze 1).

#include "tsalab/planar/PlanarSolvers.h"

#include "MddBridge.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <mutex>
#include <sstream>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace tsalab::planar
{

namespace
{
std::mutex g_pathMutex;
std::string g_executable;

std::string tclPath(const fs::path& p)
{
    const auto u8 = p.generic_u8string();
    return "{" + std::string(u8.begin(), u8.end()) + "}";
}

std::string readFile(const fs::path& p)
{
    std::ifstream in(p, std::ios::binary);
    std::ostringstream o;
    o << in.rdbuf();
    return o.str();
}

/// Lance l'exécutable avec le script ; sortie console dans logFile. Code de sortie, -1 si non lancé.
int runProcess(const std::string& exe, const fs::path& script, const fs::path& logFile)
{
#ifdef _WIN32
    auto wide = [](const std::string& s) {
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        std::wstring w(n > 0 ? static_cast<std::size_t>(n - 1) : 0, L'\0');
        if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
        return w;
    };
    SECURITY_ATTRIBUTES sa { sizeof(sa), nullptr, TRUE };
    HANDLE log = CreateFileW(logFile.wstring().c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si {};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = log;
    si.hStdError = log;
    PROCESS_INFORMATION pi {};
    std::wstring cmd = L"\"" + wide(exe) + L"\" \"" + script.wstring() + L"\"";
    const std::wstring dir = script.parent_path().wstring();
    const BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                   dir.c_str(), &si, &pi);
    if (log != INVALID_HANDLE_VALUE) CloseHandle(log);
    if (!ok) return -1;
    WaitForSingleObject(pi.hProcess, 120000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return static_cast<int>(code);
#else
    const std::string cmd = "\"" + exe + "\" \"" + script.string() + "\" > \"" + logFile.string() + "\" 2>&1";
    return std::system(cmd.c_str());
#endif
}

/// Dossier de travail unique (processus, appel) dans le dossier temporaire.
fs::path workDirectory()
{
    static std::atomic<int> counter { 0 };
#ifdef _WIN32
    const unsigned long pid = GetCurrentProcessId();
#else
    const unsigned long pid = 0;
#endif
    fs::path dir = fs::temp_directory_path() / ("tsalab-opensees-" + std::to_string(pid) + "-" + std::to_string(++counter));
    std::error_code ec;
    fs::create_directories(dir, ec);
    return dir;
}

/// Valeur d'une variable d'environnement (vide si absente).
std::string environment(const char* name)
{
#ifdef _MSC_VER
    char* value = nullptr;
    std::size_t size = 0;
    std::string s;
    if (_dupenv_s(&value, &size, name) == 0 && value) s = value;
    std::free(value);
    return s;
#else
    const char* v = std::getenv(name);
    return v ? v : "";
#endif
}

/// TSALAB_OPENSEES_KEEP : conserver script, résultats et journal d'OpenSees (diagnostic).
bool keepFiles() { return !environment("TSALAB_OPENSEES_KEEP").empty(); }

struct LeverShare
{
    double xi = 0.0, yi = 0.0, xj = 0.0, yj = 0.0;   ///< part de la charge (repère local) reprise en i et en j
};
} // namespace

std::string openSeesExecutable()
{
    {
        std::lock_guard<std::mutex> lock(g_pathMutex);
        if (!g_executable.empty()) return g_executable;
    }
    if (std::string env = environment("TSALAB_OPENSEES"); !env.empty()) return env;
#ifdef TSALAB_OPENSEES_DEFAULT_PATH
    return TSALAB_OPENSEES_DEFAULT_PATH;
#else
    return std::string();
#endif
}

void setOpenSeesExecutable(const std::string& path)
{
    std::lock_guard<std::mutex> lock(g_pathMutex);
    g_executable = path;
}

OpenSeesPlanarSolver::OpenSeesPlanarSolver(std::string executable)
    : m_executable(std::move(executable))
{
}

std::string OpenSeesPlanarSolver::name() const
{
    return "OpenSees — ossature plane (elasticBeamColumn / truss)";
}

bool OpenSeesPlanarSolver::available(std::string* why) const
{
    const std::string exe = m_executable.empty() ? openSeesExecutable() : m_executable;
    std::error_code ec;
    if (exe.empty() || !fs::exists(fs::path(std::u8string(exe.begin(), exe.end())), ec))
    {
        if (why) *why = exe.empty() ? "exécutable OpenSees non configuré (TSALAB_OPENSEES)." : "exécutable OpenSees introuvable : " + exe;
        return false;
    }
    return true;
}

std::string OpenSeesPlanarSolver::version() const
{
    if (m_versionProbed) return m_version;
    m_versionProbed = true;
    if (!available()) return m_version;
    const std::string exe = m_executable.empty() ? openSeesExecutable() : m_executable;
    const fs::path dir = workDirectory();
    const fs::path out = dir / "version.txt";
    {
        std::ofstream s(dir / "version.tcl");
        s << "set f [open " << tclPath(out) << " w]\nputs $f [version]\nclose $f\nexit\n";
    }
    runProcess(exe, dir / "version.tcl", dir / "log.txt");
    std::string v = readFile(out);
    v.erase(std::remove_if(v.begin(), v.end(), [](char c) { return c == '\r' || c == '\n'; }), v.end());
    m_version = v;
    std::error_code ec;
    fs::remove_all(dir, ec);
    return m_version;
}

Output OpenSeesPlanarSolver::solve(const Input& in)
{
    Output out;
    std::string why;
    if (!available(&why))
    {
        out.message = "OpenSees indisponible : " + why;
        return out;
    }
    const std::string exe = m_executable.empty() ? openSeesExecutable() : m_executable;
    const detail::MddBridge bridge = detail::buildMddModel(in);
    const mdd::Model& m = bridge.model;
    const std::size_t nn = m.nodes.size(), nm = m.members.size();
    const double fA = in.options.axialStiffnessFactor;

    // Géométrie des barres
    std::vector<double> len(nm), cs(nm), sn(nm);
    std::vector<int> frameCount(nn, 0), barCount(nn, 0);
    for (std::size_t k = 0; k < nm; ++k)
    {
        const auto& b = m.members[k];
        if (b.i < 0 || b.j < 0)
        {
            out.message = "Barre reliée à un nœud inexistant.";
            return out;
        }
        const double dx = m.nodes[b.j].x - m.nodes[b.i].x, dy = m.nodes[b.j].y - m.nodes[b.i].y;
        len[k] = std::hypot(dx, dy);
        if (len[k] <= 0.0)
        {
            out.message = "Barre de longueur nulle.";
            return out;
        }
        cs[k] = dx / len[k];
        sn[k] = dy / len[k];
        const bool truss = bridge.memberType[k] == ElementType::Truss;
        if (!truss && (b.releaseI || b.releaseJ))
        {
            out.message = "Rotules d'extrémité de barres fléchies : non prises en charge par le pont OpenSees 2D.";
            return out;
        }
        (truss ? barCount : frameCount)[static_cast<std::size_t>(b.i)] += 1;
        (truss ? barCount : frameCount)[static_cast<std::size_t>(b.j)] += 1;
    }

    // Charges de barre : uniformes pleine longueur et ponctuelles sur barres fléchies (eleLoad) ; tout sur
    // les barres articulées ramené aux nœuds (règle du levier).
    std::vector<LeverShare> lever(nm);
    std::vector<std::array<double, 2>> uniform(nm, { 0.0, 0.0 });   // (wx, wy) locaux
    struct PointOnFrame { std::size_t member; double a, px, py; };
    std::vector<PointOnFrame> points;
    std::vector<std::array<double, 3>> nodal(nn, { 0.0, 0.0, 0.0 });
    for (const auto& l : m.nodalLoads)
        for (int c = 0; c < 3; ++c) nodal[static_cast<std::size_t>(l.node)][static_cast<std::size_t>(c)] += c == 0 ? l.fx : c == 1 ? l.fy : l.mz;
    const double tol = 1e-9;
    for (const auto& l : m.distributedLoads)
    {
        const std::size_t k = static_cast<std::size_t>(l.member);
        const double L = len[k];
        if (bridge.memberType[k] == ElementType::Truss)
        {
            // Résultante P et moment par rapport à i de la charge trapézoïdale p(s) sur [a, b] (exacts) ;
            // part reprise en j = moment / L, en i = P − part en j.
            const double a = l.a, b = l.b, w = b - a;
            auto share = [&](double p1, double p2, double& ri, double& rj) {
                if (w <= 0.0) return;
                const double P = 0.5 * (p1 + p2) * w;
                const double moment = p1 * (b * b - a * a) / 2.0
                                    + (p2 - p1) / w * ((b * b * b - a * a * a) / 3.0 - a * (b * b - a * a) / 2.0);
                rj += moment / L;
                ri += P - moment / L;
            };
            share(l.px1, l.px2, lever[k].xi, lever[k].xj);
            share(l.py1, l.py2, lever[k].yi, lever[k].yj);
            continue;
        }
        const bool full = std::abs(l.a) <= tol * L && std::abs(l.b - L) <= tol * std::max(1.0, L);
        if (!full || std::abs(l.px1 - l.px2) > tol || std::abs(l.py1 - l.py2) > tol)
        {
            out.message = "Charge répartie partielle ou trapézoïdale sur une barre fléchie : non prise en charge par le pont OpenSees 2D.";
            return out;
        }
        uniform[k][0] += l.px1;
        uniform[k][1] += l.py1;
    }
    for (const auto& l : m.pointLoads)
    {
        const std::size_t k = static_cast<std::size_t>(l.member);
        if (bridge.memberType[k] == ElementType::Truss)
        {
            const double r = l.a / len[k];
            lever[k].xi += l.px * (1 - r); lever[k].xj += l.px * r;
            lever[k].yi += l.py * (1 - r); lever[k].yj += l.py * r;
            continue;
        }
        points.push_back({ k, l.a, l.px, l.py });
    }
    for (std::size_t k = 0; k < nm; ++k)
    {
        if (bridge.memberType[k] != ElementType::Truss) continue;
        const auto& b = m.members[k];
        auto add = [&](int node, double lx, double ly) {
            nodal[static_cast<std::size_t>(node)][0] += cs[k] * lx - sn[k] * ly;
            nodal[static_cast<std::size_t>(node)][1] += sn[k] * lx + cs[k] * ly;
        };
        add(b.i, lever[k].xi, lever[k].yi);
        add(b.j, lever[k].xj, lever[k].yj);
    }

    // --- Script Tcl
    const fs::path dir = workDirectory();
    const fs::path script = dir / "model.tcl", result = dir / "result.txt", log = dir / "opensees.log";
    if (keepFiles()) out.log.push_back("Fichiers OpenSees conservés : " + dir.string());
    std::ostringstream t;
    t << std::setprecision(17);
    t << "wipe\nmodel BasicBuilder -ndm 2 -ndf 3\n";
    for (std::size_t k = 0; k < nn; ++k) t << "node " << k + 1 << ' ' << m.nodes[k].x << ' ' << m.nodes[k].y << '\n';
    int nextNode = static_cast<int>(nn) + 1, nextElement = static_cast<int>(nm) + 1, nextMaterial = 1;
    std::map<std::size_t, int> auxOf;
    for (std::size_t k = 0; k < nn; ++k)
    {
        const auto& n = m.nodes[k];
        const bool rotFree = frameCount[k] == 0 && barCount[k] > 0;   // nœud de treillis : rotation sans rigidité
        const bool fr = n.fixRz || (rotFree && n.kRz <= 0.0);
        if (n.fixX || n.fixY || fr) t << "fix " << k + 1 << ' ' << int(n.fixX) << ' ' << int(n.fixY) << ' ' << int(fr) << '\n';
        if (n.kX > 0 || n.kY > 0 || n.kRz > 0)
        {
            const int aux = nextNode++;
            auxOf[k] = aux;
            t << "node " << aux << ' ' << n.x << ' ' << n.y << "\nfix " << aux << " 1 1 1\n";
            std::ostringstream mats, dirs;
            const double kv[3] = { n.kX, n.kY, n.kRz };
            for (int c = 0; c < 3; ++c)
                if (kv[c] > 0)
                {
                    t << "uniaxialMaterial Elastic " << nextMaterial << ' ' << kv[c] << '\n';
                    mats << ' ' << nextMaterial++;
                    dirs << ' ' << c + 1;
                }
            t << "element zeroLength " << nextElement++ << ' ' << aux << ' ' << k + 1 << " -mat" << mats.str() << " -dir" << dirs.str() << '\n';
        }
    }
    // Structure sans DDL libre (tout encastré) : OpenSees refuse un système vide. Un nœud fictif isolé sur
    // ressorts donne 3 équations sans effet sur la structure ; les efforts d'encastrement parfait restent ceux
    // calculés par OpenSees (eleLoad).
    bool anyFree = false;
    for (std::size_t k = 0; k < nn && !anyFree; ++k)
    {
        const auto& n = m.nodes[k];
        const bool fr = n.fixRz || (frameCount[k] == 0 && barCount[k] > 0 && n.kRz <= 0.0);
        anyFree = !n.fixX || !n.fixY || !fr || n.kX > 0 || n.kY > 0 || n.kRz > 0;
    }
    if (!anyFree)
    {
        const int dummy = nextNode++, anchor = nextNode++;
        t << "node " << dummy << " 0 0\nnode " << anchor << " 0 0\nfix " << anchor << " 1 1 1\n"
          << "uniaxialMaterial Elastic " << nextMaterial << " 1.0\n"
          << "element zeroLength " << nextElement++ << ' ' << anchor << ' ' << dummy << " -mat " << nextMaterial << ' '
          << nextMaterial << ' ' << nextMaterial << " -dir 1 2 3\n";
        ++nextMaterial;
    }
    t << "geomTransf Linear 1\n";
    for (std::size_t k = 0; k < nm; ++k)
    {
        const auto& b = m.members[k];
        if (bridge.memberType[k] == ElementType::Truss)
        {
            t << "uniaxialMaterial Elastic " << nextMaterial << ' ' << b.E << '\n';
            t << "element truss " << k + 1 << ' ' << b.i + 1 << ' ' << b.j + 1 << ' ' << b.A * fA << ' ' << nextMaterial++ << '\n';
        }
        else
            t << "element elasticBeamColumn " << k + 1 << ' ' << b.i + 1 << ' ' << b.j + 1 << ' ' << b.A * fA << ' ' << b.E << ' ' << b.I << " 1\n";
    }
    t << "timeSeries Linear 1\npattern Plain 1 1 {\n";
    for (std::size_t k = 0; k < nn; ++k)
        if (nodal[k][0] != 0.0 || nodal[k][1] != 0.0 || nodal[k][2] != 0.0)
            t << "  load " << k + 1 << ' ' << nodal[k][0] << ' ' << nodal[k][1] << ' ' << nodal[k][2] << '\n';
    for (std::size_t k = 0; k < nm; ++k)
        if (uniform[k][0] != 0.0 || uniform[k][1] != 0.0)
            t << "  eleLoad -ele " << k + 1 << " -type -beamUniform " << uniform[k][1] << ' ' << uniform[k][0] << '\n';
    for (const auto& p : points)
        t << "  eleLoad -ele " << p.member + 1 << " -type -beamPoint " << p.py << ' ' << p.a / len[p.member] << ' ' << p.px << '\n';
    t << "}\n";
    t << "constraints Plain\nnumberer RCM\nsystem BandGeneral\ntest NormDispIncr 1.0e-12 10 0\nalgorithm Linear\n"
         "integrator LoadControl 1.0\nanalysis Static\nset ok [analyze 1]\nreactions\n";
    t << "set f [open " << tclPath(result) << " w]\nputs $f \"OK $ok\"\nputs $f \"VERSION [version]\"\n";
    t << "foreach n {";
    for (std::size_t k = 0; k < nn; ++k) t << ' ' << k + 1;
    t << " } { puts $f \"D $n [nodeDisp $n 1] [nodeDisp $n 2] [nodeDisp $n 3]\"; puts $f \"R $n [nodeReaction $n 1] [nodeReaction $n 2] [nodeReaction $n 3]\" }\n";
    for (const auto& [k, aux] : auxOf)
        t << "puts $f \"A " << k + 1 << " [nodeReaction " << aux << " 1] [nodeReaction " << aux << " 2] [nodeReaction " << aux << " 3]\"\n";
    for (std::size_t k = 0; k < nm; ++k)
        t << "puts $f \"" << (bridge.memberType[k] == ElementType::Truss ? "T " : "F ") << k + 1 << " [eleResponse " << k + 1
          << (bridge.memberType[k] == ElementType::Truss ? " axialForce" : " localForce") << "]\"\n";
    t << "close $f\nwipe\nexit\n";
    {
        std::ofstream s(script, std::ios::binary);
        s << t.str();
    }

    const int code = runProcess(exe, script, log);
    const std::string res = readFile(result);
    const std::string console = readFile(log);
    std::error_code ec;
    if (code == -1 || res.empty())
    {
        out.message = "OpenSees n'a produit aucun résultat (code " + std::to_string(code) + ").";
        if (!console.empty()) out.log.push_back(console);
        if (!keepFiles()) fs::remove_all(dir, ec);
        return out;
    }

    // --- Lecture
    std::vector<std::array<double, 3>> disp(nn, { 0, 0, 0 }), reac(nn, { 0, 0, 0 });
    std::vector<std::vector<double>> force(nm);
    int ok = -1;
    std::istringstream rs(res);
    std::string line;
    while (std::getline(rs, line))
    {
        std::istringstream ls(line);
        std::string tag;
        ls >> tag;
        if (tag == "OK") ls >> ok;
        else if (tag == "VERSION") { std::getline(ls, m_version); m_version.erase(0, m_version.find_first_not_of(' ')); m_versionProbed = true; }
        else if (tag == "D" || tag == "R" || tag == "A")
        {
            int id = 0;
            double a = 0, b = 0, c = 0;
            ls >> id >> a >> b >> c;
            if (id < 1 || id > static_cast<int>(nn)) continue;
            auto& target = tag == "D" ? disp[static_cast<std::size_t>(id - 1)] : reac[static_cast<std::size_t>(id - 1)];
            if (tag == "D") target = { a, b, c };
            else { target[0] += a; target[1] += b; target[2] += c; }
        }
        else if (tag == "F" || tag == "T")
        {
            int id = 0;
            ls >> id;
            double v = 0;
            std::vector<double> values;
            while (ls >> v) values.push_back(v);
            if (id >= 1 && id <= static_cast<int>(nm)) force[static_cast<std::size_t>(id - 1)] = values;
        }
    }
    if (!keepFiles()) fs::remove_all(dir, ec);
    if (ok != 0)
    {
        out.message = "OpenSees : l'analyse a échoué (analyze " + std::to_string(ok) + ") — structure instable ou mal appuyée ?";
        if (!console.empty()) out.log.push_back(console);
        return out;
    }

    out.success = true;
    out.method = "OpenSees " + m_version + " — ossature plane (elasticBeamColumn, truss, zeroLength ; Linear, BandGeneral), "
                 "post-traitement des barres commun au laboratoire";
    const auto& nodeIndex = bridge.nodeIndex;
    double sumX = 0, sumY = 0;
    for (const auto& n : in.nodes)
    {
        const int k = n.index >= 0 && n.index < static_cast<int>(nodeIndex.size()) ? nodeIndex[static_cast<std::size_t>(n.index)] : -1;
        if (k < 0) continue;
        const auto& d = disp[static_cast<std::size_t>(k)];
        out.displacements.push_back({ n.index, d[0], d[1], d[2] });
        const auto& nd = m.nodes[static_cast<std::size_t>(k)];
        if (nd.fixX || nd.fixY || nd.fixRz || nd.kX > 0 || nd.kY > 0 || nd.kRz > 0)
        {
            const auto& r = reac[static_cast<std::size_t>(k)];
            out.reactions.push_back({ n.index, r[0], r[1], nd.fixRz || nd.kRz > 0 ? r[2] : 0.0 });
            sumX += r[0];
            sumY += r[1];
        }
    }

    // Efforts d'extrémité (forces exercées sur la barre, repère local) puis post-traitement commun.
    for (std::size_t k = 0; k < nm; ++k)
    {
        const auto& b = m.members[k];
        mdd::MemberEndForces end;
        const auto& f = force[k];
        if (bridge.memberType[k] == ElementType::Truss)
        {
            const double N = f.empty() ? 0.0 : f.front();
            end.fxI = -N - lever[k].xi; end.fyI = -lever[k].yi;
            end.fxJ = N - lever[k].xj;  end.fyJ = -lever[k].yj;
        }
        else if (f.size() >= 6)
        {
            end.fxI = f[0]; end.fyI = f[1]; end.mzI = f[2];
            end.fxJ = f[3]; end.fyJ = f[4]; end.mzJ = f[5];
        }
        const auto& di = disp[static_cast<std::size_t>(b.i)];
        const auto& dj = disp[static_cast<std::size_t>(b.j)];
        const std::array<double, 4> local { cs[k] * di[0] + sn[k] * di[1], -sn[k] * di[0] + cs[k] * di[1],
                                            cs[k] * dj[0] + sn[k] * dj[1], -sn[k] * dj[0] + cs[k] * dj[1] };
        mdd::MemberResult mr;
        mr.end = end;
        mdd::computeCurves(m, static_cast<int>(k), end, local, mr);
        out.elementForces.push_back(detail::toContract(bridge.memberOf[k], mr));
    }

    // Équilibre global : charges (nœuds + barres, repère global) + réactions.
    double loadX = 0, loadY = 0;
    for (const auto& l : m.nodalLoads) { loadX += l.fx; loadY += l.fy; }
    auto addLocal = [&](std::size_t k, double px, double py) {
        loadX += cs[k] * px - sn[k] * py;
        loadY += sn[k] * px + cs[k] * py;
    };
    for (const auto& l : m.distributedLoads)
    {
        const double w = l.b - l.a;
        addLocal(static_cast<std::size_t>(l.member), 0.5 * (l.px1 + l.px2) * w, 0.5 * (l.py1 + l.py2) * w);
    }
    for (const auto& l : m.pointLoads) addLocal(static_cast<std::size_t>(l.member), l.px, l.py);
    out.equilibriumResidual = std::hypot(loadX + sumX, loadY + sumY);

    std::ostringstream o;
    o << "OpenSees " << m_version << " : " << nn << " nœud(s), " << nm << " barre(s), résidu d'équilibre "
      << out.equilibriumResidual << " kN.";
    out.log.push_back(o.str());
    return out;
}

} // namespace tsalab::planar
