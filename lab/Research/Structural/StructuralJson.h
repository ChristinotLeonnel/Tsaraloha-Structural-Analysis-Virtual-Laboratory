#pragma once

// TSALab — persistance JSON du portique plan (Model2D) et de ses résultats (AnalysisResult2D) : programmation
// structurelle (blocs Enregistrer / Lire), archivage d'un calcul, échanges.
//
// Formats versionnés « tsalab-model2d » et « tsalab-result2d », version 1, unités du cœur scientifique (kN, m, kN/m²,
// m², m⁴, t, t/m³, °C, rad) rappelées dans le fichier. Relecture à l'identique : les réels sont écrits au plus court qui
// se relit exactement, les valeurs non finies (NaN, ±∞) en texte (« nan », « inf », « -inf ») ; un modèle ou un
// résultat relu est égal (==) à celui qui a été enregistré.
// Lecture stricte : document illisible, autre format, version future, champ manquant ou de mauvais type, énumération
// inconnue, matrice de dimensions incohérentes → refus avec le chemin du champ (« model.elements[3].section.A ») ;
// jamais de valeur par défaut substituée. Écriture d'un bloc (QSaveFile) : un échec ne laisse pas de fichier tronqué.

#include "tsalab/structural/DirectStiffness2D.h"

#include <QByteArray>

#include <string>

namespace TSALab::Research::Structural
{

inline constexpr int kModelJsonVersion = 1;
inline constexpr int kResultJsonVersion = 1;

QByteArray modelToJson(const tsalab::structural::Model2D& model);
bool modelFromJson(const QByteArray& json, tsalab::structural::Model2D& model, std::string* error = nullptr);
QByteArray resultToJson(const tsalab::structural::AnalysisResult2D& result);
bool resultFromJson(const QByteArray& json, tsalab::structural::AnalysisResult2D& result, std::string* error = nullptr);

bool saveModelJson(const tsalab::structural::Model2D& model, const std::string& path, std::string* error = nullptr);
bool loadModelJson(const std::string& path, tsalab::structural::Model2D& model, std::string* error = nullptr);
bool saveResultJson(const tsalab::structural::AnalysisResult2D& result, const std::string& path, std::string* error = nullptr);
bool loadResultJson(const std::string& path, tsalab::structural::AnalysisResult2D& result, std::string* error = nullptr);

} // namespace TSALab::Research::Structural
