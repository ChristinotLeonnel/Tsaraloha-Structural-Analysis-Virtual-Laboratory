#pragma once

// TSALab — charges roulantes sur portiques plans (cœur scientifique, module séparé).
//
// Un convoi d'essieux (forces verticales dirigées suivant −Y global) avance pas à pas le long d'un chemin d'éléments ;
// chaque position fait l'objet d'une analyse STATIQUE linéaire (K factorisée une seule fois), éventuellement superposée
// à un cas de charge permanent. On en tire les enveloppes (min/max) des efforts N, V, M en des sections régulières de
// chaque élément, des déplacements et des réactions, et les lignes d'influence d'une grandeur.
//
// Ce n'est PAS une analyse dynamique : ni inertie, ni interaction véhicule-pont, ni coefficient de majoration
// dynamique (valeur normative : à inclure par l'utilisateur dans la charge des essieux s'il y a lieu).
//
// Unités : kN, m.

#include "tsalab/structural/DirectStiffness2D.h"

#include <string>
#include <vector>

namespace tsalab::structural
{

struct Axle
{
    double offset = 0.0;   ///< distance derrière le premier essieu (m, ≥ 0)
    double load = 0.0;     ///< kN, dirigée suivant −Y global si positive
    bool operator==(const Axle&) const = default;
};

struct Vehicle
{
    std::string name;
    std::vector<Axle> axles;
    double length() const;                    ///< plus grand décalage (m)
    bool valid(std::string* error = nullptr) const;
    bool operator==(const Vehicle&) const = default;
};

struct MovingLoadOptions
{
    std::vector<int> path;     ///< éléments parcourus dans l'ordre de la marche (chaîne continue)
    double step = 0.25;        ///< avance du convoi entre deux positions (m)
    int stations = 21;         ///< sections d'enveloppe régulièrement réparties sur chaque élément (≥ 2)
    int baseCase = 0;          ///< cas de charge superposé à chaque position (0 : aucun)
};

struct EnvelopeStation
{
    int element = 0;
    double x = 0.0;                            ///< m depuis le nœud i de l'élément
    double Nmin = 0.0, Nmax = 0.0, Vmin = 0.0, Vmax = 0.0, Mmin = 0.0, Mmax = 0.0;
    double sMmin = 0.0, sMmax = 0.0;           ///< position du premier essieu donnant Mmin, Mmax
    bool operator==(const EnvelopeStation&) const = default;
};

struct MovingLoadResult
{
    bool success = false;
    std::string message;
    std::vector<std::string> log;
    Model2D model;
    Vehicle vehicle;
    DofNumbering dofs;
    BoundaryConditions bc;
    double pathLength = 0.0;
    std::vector<double> positions;            ///< abscisse du premier essieu le long du chemin (m)
    std::vector<Vector> displacements;        ///< par position (n DDL)
    std::vector<EnvelopeStation> stations;
    Vector displacementMin, displacementMax;  ///< n
    Vector reactionMin, reactionMax;          ///< n (nulles aux DDL libres)

    /// Section d'enveloppe de plus grand Mmax (nullptr si aucune).
    const EnvelopeStation* maxMoment() const;
    const EnvelopeStation* minMoment() const;
    bool operator==(const MovingLoadResult&) const = default;
};

MovingLoadResult movingLoadAnalysis(const Model2D& model, const Vehicle& vehicle, const MovingLoadOptions& options);

/// Grandeur suivie par une ligne d'influence.
struct ResponseQuantity
{
    enum class Kind
    {
        Displacement,   ///< node, component
        Reaction,       ///< node, component (DDL bloqué)
        AxialForce,     ///< element, x
        ShearForce,     ///< element, x (limite à gauche d'une force placée en x)
        BendingMoment   ///< element, x
    };
    Kind kind = Kind::BendingMoment;
    int node = 0, component = 0;
    int element = 0;
    double x = 0.0;    ///< m depuis le nœud i
    bool operator==(const ResponseQuantity&) const = default;
};

struct InfluenceLine
{
    bool success = false;
    std::string message;
    std::string quantity;              ///< libellé (« M élément 1, x = 5 m »)
    std::vector<double> s;             ///< position de la force unitaire le long du chemin (m)
    std::vector<double> value;         ///< grandeur pour 1 kN suivant −Y
    double minimum = 0.0, maximum = 0.0, sMinimum = 0.0, sMaximum = 0.0;
    bool operator==(const InfluenceLine&) const = default;
};

/// Ligne d'influence : force unitaire de 1 kN (suivant −Y) déplacée sur le chemin, au pas `step` (m).
InfluenceLine influenceLine(const Model2D& model, const std::vector<int>& path, double step, const ResponseQuantity& quantity);

} // namespace tsalab::structural
