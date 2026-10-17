#pragma once

// TSALab — import d'un projet TSA (.tsa) vers un modèle de portique plan Model2D (programmation structurelle).
//
// Chemin de TSA lui-même, sans réécriture :
//   1. en-tête lu par TSAFileReader::readHeader (version du format, fichier chiffré), projet lu par
//      TSAProjectIO::loadProject (le fichier n'est jamais écrit ; empreinte SHA-256 contrôlée avant et après) ;
//   2. portée PLANE (plan du modèle, axe de grille, niveau, plan de travail) résolue, extraite et validée par
//      AnalysisManager::prepare pour le moteur 2D « custom2d » ;
//   3. conversion par Custom2D::buildInput (projection dans le plan, appuis et ressorts, rotules, charges) ;
//   4. correspondance avec Model2D, contrôlée par validate(), décrite élément par élément : importé, converti (barre de
//      treillis → barre bi-articulée, poids propre hors de −Y → charges réparties globales), ignoré (dalles, voiles,
//      barres coupant la portée, charges hors portée ou hors plan) ; refus motivé de ce qui n'a pas d'équivalent
//      linéaire (câbles) ou de ce qui rend le modèle invalide.
// Traçabilité dans Model2D::metadata : fichier, empreinte, version du format, projet, auteur, portée, date d'import,
// correspondance nœuds/éléments TSA.

#include "StructuralObjects.h"
#include "tsalab/planar/PlanarSolver.h"

#include <atomic>
#include <string>
#include <vector>

namespace TSALab::Research::Structural
{

struct TsaImportOptions
{
    /// Libellé d'une portée plane proposée par le projet (listTsaScopes), ou vide / « plan » : plan du modèle (détecté).
    std::string scope;
};

struct TsaImportReport
{
    bool success = false;
    std::string message;
    std::string path, sha256, formatVersion, projectName, author, scopeLabel;
    std::vector<std::string> scopes;      ///< portées planes disponibles
    std::vector<std::string> imported;    ///< inventaire importé
    std::vector<std::string> converted;   ///< conversions effectuées (avec la raison)
    std::vector<std::string> ignored;     ///< objets ou charges non transmis
    std::vector<std::string> warnings;    ///< validation de TSA et de Model2D
    std::vector<std::string> errors;      ///< causes de refus
    std::size_t nodes = 0, elements = 0, supports = 0, springs = 0, releases = 0, trussBars = 0;
    std::size_t nodalLoads = 0, memberLoads = 0, loadCases = 0, combinations = 0;

    std::string text() const;
};

/// SHA-256 (hexadécimal) du contenu d'un fichier ; vide si illisible.
std::string fileSha256(const std::string& path);

/// Portées planes proposées par le projet (lecture seule) : « Plan du modèle (détecté) », axes de grille, niveaux,
/// plans de travail.
bool listTsaScopes(const std::string& path, std::vector<std::string>& scopes, std::string* error = nullptr);

/// Import ; `cancel` (facultatif) interrompt l'import entre deux étapes (rapport : « import annulé »). `planarInput`
/// (facultatif) reçoit l'entrée du solveur 2D de TSA produite par Custom2D::buildInput (validation croisée).
bool importTsaProject(const std::string& path, const TsaImportOptions& options, S::Model2D& out, TsaImportReport& report,
                      const std::atomic<bool>* cancel = nullptr, tsalab::planar::Input* planarInput = nullptr);

} // namespace TSALab::Research::Structural
