#pragma once

// TSALab — dynamique des portiques plans (cœur scientifique, module séparé de l'élément FrameElement2D).
//
// Périmètre (laboratoire pédagogique ; TSA reste limité au statique, ADR-022) :
//   - matrice de masse : cohérente ou concentrée des éléments (ρ·A) + masses nodales ;
//   - analyse modale : K_ff·φ = ω²·M_ff·φ (appuis et ressorts ; déplacements imposés ignorés), modes normés
//     (φᵀ·M·φ = 1), facteurs de participation Γ = φᵀ·M·ι et masses effectives Γ² suivant X et Y ;
//   - analyse temporelle linéaire : Newmark (accélération moyenne β = 1/4, γ = 1/2 par défaut, inconditionnellement
//     stable, sans amortissement numérique), amortissement de Rayleigh C = a0·M + a1·K ; chargement F(t) = f(t)·F_cas
//     et/ou accélération du sol a_g(t) (forces effectives −M·ι·a_g, déplacements RELATIFS au sol) ;
//     DDL sans masse : condensation statique exacte si a1 = 0, refus motivé sinon ;
//   - analyse spectrale : spectre de réponse en accélération Sa(T) FOURNI par l'utilisateur (aucun spectre
//     réglementaire n'est inventé), réponse modale u_k = φ_k·Γ_k·Sa(T_k)/ω_k², combinaison SRSS ou CQC
//     (coefficients de Der Kiureghian, amortissement ξ identique pour tous les modes).
// Les accélérogrammes et spectres viennent de l'utilisateur (fichier, saisie) ; un signal harmonique de vérification
// est un signal analytique, pas un séisme.
//
// Unités : kN, m, s, t (tonne) ; accélérations en m/s² ; pulsations en rad/s ; fréquences en Hz.

#include "tsalab/structural/DirectStiffness2D.h"

#include <string>
#include <utility>
#include <vector>

namespace tsalab::structural
{

/// M (n × n) = Σ A_eᵀ·m_e·A_e + masses nodales.
bool assembleMass(const Model2D& model, const DofNumbering& dofs, MassModel massModel, Matrix& M, std::string* error = nullptr);

// ---------------------------------------------------------------------------------------------
// Analyse modale
// ---------------------------------------------------------------------------------------------

struct ModalResult
{
    bool success = false;
    std::string message;
    std::vector<std::string> log;
    Model2D model;
    MassModel massModel = MassModel::Consistent;
    DofNumbering dofs;
    BoundaryConditions bc;
    std::vector<double> omega, frequency, period;          ///< rad/s, Hz, s (fréquences croissantes)
    Matrix modes;                                          ///< n × m : φ_k (DDL imposés nuls), φᵀ·M·φ = 1
    std::vector<double> participationX, participationY;    ///< Γ_k = φ_kᵀ·M·ι (t^½·…, normalisation de masse)
    std::vector<double> effectiveMassX, effectiveMassY;    ///< Γ_k² (t)
    double totalMassX = 0.0, totalMassY = 0.0;             ///< ιᵀ·M·ι sur les DDL libres (t)

    Vector mode(int k) const;
    bool operator==(const ModalResult&) const = default;
};

/// Analyse modale ; modeCount ≤ 0 : tous les modes de masse non nulle.
ModalResult modalAnalysis(const Model2D& model, int modeCount, MassModel massModel);

// ---------------------------------------------------------------------------------------------
// Analyse temporelle
// ---------------------------------------------------------------------------------------------

/// Fonction du temps linéaire par morceaux, NULLE hors de [t_premier, t_dernier] (fin d'un accélérogramme).
struct TimeFunction
{
    std::vector<std::pair<double, double>> points;   ///< (t en s, valeur), t croissants
    double at(double t) const;
    bool valid(std::string* error = nullptr) const;
    bool operator==(const TimeFunction&) const = default;
};

struct DynamicLoad
{
    int caseId = 0;            ///< cas de charge : forme spatiale F
    TimeFunction factor;       ///< f(t) : F(t) = f(t)·F
    bool operator==(const DynamicLoad&) const = default;
};

struct GroundMotion
{
    int direction = 0;         ///< 0 : X, 1 : Y
    TimeFunction acceleration; ///< a_g(t), m/s²
    std::string source;        ///< provenance (fichier, signal de vérification…)
    bool operator==(const GroundMotion&) const = default;
};

struct DampingSpec
{
    double a0 = 0.0;           ///< C = a0·M + a1·K
    double a1 = 0.0;
    bool operator==(const DampingSpec&) const = default;
};

/// Coefficients de Rayleigh donnant le taux d'amortissement ξ aux pulsations ω1 et ω2 (ω1 ≠ ω2).
DampingSpec rayleighDamping(double xi, double omega1, double omega2);

struct TimeHistoryOptions
{
    double dt = 0.01;          ///< pas de temps (s)
    double duration = 1.0;     ///< durée (s)
    double beta = 0.25, gamma = 0.5;
    MassModel mass = MassModel::Consistent;
    DampingSpec damping;
};

struct TimeHistoryResult
{
    bool success = false;
    std::string message;
    std::vector<std::string> log;
    Model2D model;
    DofNumbering dofs;
    BoundaryConditions bc;
    std::vector<double> time;
    std::vector<Vector> displacement;    ///< par pas : n DDL (relatifs au sol pour une accélération du sol)
    std::vector<Vector> velocity;        ///< DDL massiques (nuls sur les DDL sans masse condensés)
    std::vector<Vector> acceleration;    ///< relatives au sol
    std::vector<double> groundAcceleration;   ///< a_g(t) (0 sans mouvement du sol)
    LoadCase loads;                      ///< forme spatiale F de la charge dynamique (vide sans charge)
    std::vector<double> loadFactor;      ///< f(t) par pas (0 sans charge)
    std::vector<double> kineticEnergy, strainEnergy;   ///< ½vᵀMv, ½uᵀKu (kN·m)
    Vector peakAbsDisplacement;          ///< max |u| par DDL

    Vector history(int dof) const;
    bool operator==(const TimeHistoryResult&) const = default;
};

/// Newmark sur les DDL libres, départ des conditions initiales données (repos par défaut). load et ground facultatifs
/// (au moins un, ou des conditions initiales non nulles).
TimeHistoryResult timeHistory(const Model2D& model, const TimeHistoryOptions& options, const DynamicLoad* load, const GroundMotion* ground,
                              const Vector* initialDisplacement = nullptr, const Vector* initialVelocity = nullptr);

/// Efforts des éléments au pas `step` : efforts élastiques déduits des déplacements du pas (relatifs au sol) et des
/// charges f(t)·F (pratique usuelle des calculs par éléments finis ; les forces d'inertie réparties le long des barres
/// ne figurent pas dans les diagrammes entre nœuds).
bool elementResultsAt(const TimeHistoryResult& result, int step, int points, std::vector<ElementResult>& out, std::string* error = nullptr);

/// Lecture d'une série temporelle FOURNIE (accélérogramme, fonction de charge) depuis un texte, sans supposer de format
/// propriétaire : lignes à deux nombres (t, valeur), ou, si dt > 0, valeurs successives (une ou plusieurs par ligne)
/// au pas dt depuis t = 0. Les lignes contenant un élément non numérique (en-têtes, commentaires #, %, //) sont
/// ignorées et comptées dans *skippedLines. scale : facteur d'unité appliqué aux valeurs (ex. g → m/s²).
bool parseTimeSeries(const std::string& text, double dt, double scale, TimeFunction& out, std::string* error = nullptr,
                     int* skippedLines = nullptr);

// ---------------------------------------------------------------------------------------------
// Analyse spectrale
// ---------------------------------------------------------------------------------------------

/// Spectre de réponse en pseudo-accélération Sa(T) (m/s²), fourni par l'utilisateur, interpolé linéairement ; une
/// période hors de l'intervalle des points est refusée (pas d'extrapolation).
struct ResponseSpectrum
{
    std::vector<std::pair<double, double>> points;   ///< (T en s, Sa en m/s²), T croissants
    std::string source;
    bool at(double period, double& sa) const;
    bool valid(std::string* error = nullptr) const;
    bool operator==(const ResponseSpectrum&) const = default;
};

enum class ModalCombination
{
    SRSS,
    CQC
};

struct ElementPeak
{
    int element = 0;
    std::vector<double> endForces;       ///< 6 composantes combinées (valeurs absolues de pointe)
    bool operator==(const ElementPeak&) const = default;
};

struct SpectralResult
{
    bool success = false;
    std::string message;
    std::vector<std::string> log;
    int direction = 0;
    ModalCombination combination = ModalCombination::SRSS;
    double damping = 0.05;
    std::vector<double> period, Sa, Sd;              ///< par mode
    std::vector<double> participation, effectiveMass, baseShear;   ///< par mode (Γ, Γ² en t, Γ²·Sa en kN)
    std::vector<Vector> modalDisplacement;           ///< u_k = φ_k·Γ_k·Sd_k (n)
    Vector peakDisplacement;                         ///< combinaison (n, ≥ 0)
    double combinedBaseShear = 0.0;                  ///< kN
    double massRatio = 0.0;                          ///< Σ masses effectives / masse totale (direction)
    std::vector<ElementPeak> elementPeaks;
    DofNumbering dofs;
    bool operator==(const SpectralResult&) const = default;
};

/// Spectre de réponse élastique d'un accélérogramme FOURNI : pour chaque période T, oscillateur à 1 DDL
/// u'' + 2ξω·u' + ω²·u = −a_g(t) intégré par Newmark (accélération moyenne) au pas min(pas du signal, T/50) sur la
/// durée du signal ; Sa(T) = ω²·max|u| (pseudo-accélération), T > 0 croissantes.
bool accelerogramSpectrum(const TimeFunction& groundAcceleration, const std::vector<double>& periods, double damping, ResponseSpectrum& out,
                          std::string* error = nullptr);

/// Coefficient de corrélation CQC de deux modes de pulsations ωi, ωj (amortissement ξ commun).
double cqcCoefficient(double omegaI, double omegaJ, double xi);

SpectralResult spectralAnalysis(const ModalResult& modal, const ResponseSpectrum& spectrum, int direction, ModalCombination combination,
                                double damping = 0.05);

} // namespace tsalab::structural
