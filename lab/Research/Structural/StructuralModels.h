#pragma once

// TSALab Blueprint — modèles de démonstration de la programmation structurelle et accès aux catalogues de TSA
// (profils, matériaux) convertis dans les unités du cœur scientifique (kN, m).

#include "tsalab/structural/Frame2D.h"

#include <string>

namespace TSALab::Research::Structural
{

namespace S = tsalab::structural;

/// Profil du catalogue de TSA (« IPE 300 », « HEB 200 »…) : A, Iz (flexion autour de l'axe fort, dans le plan du
/// portique), h. Taille hors catalogue refusée (*error), comme pour les commandes de TSA.
bool catalogSection(const std::string& profile, S::Section2D& out, std::string* error = nullptr);
/// Matériau du catalogue de TSA (S235, S355, C25/30, C30/37, C24) : E en kN/m², α en 1/°C, γ = ρ·g (g = 9,81 m/s²,
/// valeur du calcul du poids propre de TSA) en kN/m³.
bool catalogMaterial(const std::string& name, S::Material& out, std::string* error = nullptr);

struct PortalParameters
{
    double span = 6.0;          ///< m
    double height = 4.0;        ///< m
    double q = -15.0;           ///< kN/m sur la traverse, y local (négatif : vers le bas), cas 1 « G »
    double H = 20.0;            ///< kN en tête du poteau gauche, X global, cas 2 « W »
    double settlement = 0.01;   ///< m, tassement du pied droit (déplacement imposé, cas 3 « T »)
    std::string column = "HEB 200";
    std::string beam = "IPE 300";
    std::string material = "S235";
};

/// Portique de démonstration : pieds encastrés (N1, N4), traverse N2-N3.
///   cas 1 « G » : charge q répartie sur la traverse + poids propre de tous les éléments ;
///   cas 2 « W » : force H en tête du poteau gauche + pression 0,2·H/h kN/m sur le poteau gauche (X global) ;
///   cas 3 « T » : tassement du pied droit (uy imposé = −settlement) ;
///   combinaison 1 « ELU 6.10 » : 1,35 G + 1,5 W (exemple d'EN 1990, 6.10 : γG = 1,35 et γQ = 1,5 ; sans autre action).
bool demoPortal(const PortalParameters& p, S::Model2D& out, std::string* error = nullptr);

/// Poutre posée sur deux appuis simples verticaux (translation X libre) : modèle instable (mécanisme).
S::Model2D unstableBeam();

/// Poutre continue à deux travées de 5 m (IPE 300, S235) : cas 1 « q » (10 kN/m), cas 2 « tassement » (appui central −5 mm).
S::Model2D settlementBeam();

} // namespace TSALab::Research::Structural
