#pragma once

// Outils internes des blocs de la programmation structurelle : broches, définitions, lecture des saisies texte.
// Non installé : inclus uniquement par les fichiers *Nodes.cpp de ce dossier.

#include "Blueprint/BlueprintRuntime.h"
#include "StructuralObjects.h"

#include <string>
#include <utility>
#include <vector>

namespace TSALab::Research::Structural::detail
{

using namespace TSA::Blueprint;
using VT = TSA::Blueprint::ValueType;

// Catégories de la bibliothèque (palette de l'éditeur)
inline constexpr char kModel[] = "Structure A — Modèle structurel";
inline constexpr char kElement[] = "Structure B — Élément fini";
inline constexpr char kAssembly[] = "Structure C — Assemblage";
inline constexpr char kMath[] = "Structure D — Mathématiques";
inline constexpr char kControl[] = "Structure E — Contrôle et affichage";
inline constexpr char kExperimental[] = "Expérimental — laboratoire de l'étudiant";

// Implémentations (documentation des blocs : « Code source »)
inline constexpr char kSrcFrame2D[] = "TSALab/science/src/structural/Frame2D.cpp";
inline constexpr char kSrcElement[] = "TSALab/science/src/structural/FrameElement2D.cpp";
inline constexpr char kSrcStiffness[] = "TSALab/science/src/structural/DirectStiffness2D.cpp";
inline constexpr char kSrcLinAlg[] = "TSALab/science/src/LinearAlgebra.cpp";

PinSpec execIn();
PinSpec execOut(const char* name = "then", const char* label = "");
PinSpec data(const char* name, const char* label, VT type, Value def = {});
PinSpec required(const char* name, const char* label, VT type);
PinSpec object(const char* name, const char* label, const char* objectType);
/// Entrée objet facultative (le bloc vérifie hasValue).
PinSpec optionalObject(const char* name, const char* label, const char* objectType);
PinSpec any(const char* name, const char* label);

/// Bloc pur réutilisable (fonction de ses entrées), composant validé.
NodeDefinition pureBlock(const char* id, const char* title, const char* category, const char* description, std::vector<PinSpec> inputs,
                         std::vector<PinSpec> outputs, std::string documentation, const char* source);
/// Bloc d'action (broches d'exécution ajoutées), composant validé.
NodeDefinition actionBlock(const char* id, const char* title, const char* category, const char* description, std::vector<PinSpec> inputs,
                           std::vector<PinSpec> outputs, std::string documentation, const char* source);

/// « y local », « ly », « perpendiculaire », « x local », « axial », « X global », « gx », « Y global », « gy », « vertical ».
bool parseDirection(const std::string& text, S::LoadDirection& out);
/// « ux », « uy », « rz » / « θz ».
bool parseComponent(const std::string& text, int& out);
/// Couples « a:b ; c:d » (virgule décimale admise).
bool parsePairs(const std::string& text, std::vector<std::pair<double, double>>& out, std::string* error);
/// Matrice : lignes séparées par « ; » ou un retour à la ligne, coefficients par des espaces ou des virgules (point décimal).
bool parseMatrix(const std::string& text, Matrix& out, std::string* error);
/// Vecteur : composantes séparées par « ; », des espaces, des virgules ou des retours à la ligne.
bool parseVector(const std::string& text, Vector& out, std::string* error);
/// « cholesky », « lu », « cg ».
bool parseMethod(const std::string& text, tsalab::numerics::SolverMethod& out);

std::string num(double v, int precision = 6);

// Accès aux valeurs des objets en entrée (nul et erreur enregistrée si absent ou d'un autre type)
const S::Model2D* modelIn(ExecutionContext& c, const char* pin = "model");
const S::FrameElement2D* elementIn(ExecutionContext& c, const char* pin = "element");
const S::LoadCase* loadsIn(ExecutionContext& c, const char* pin = "loads");
const DofSet* dofsIn(ExecutionContext& c, const char* pin = "dofs");
const LabeledMatrix* matrixIn(ExecutionContext& c, const char* pin);
const LabeledVector* vectorIn(ExecutionContext& c, const char* pin);
const S::AnalysisResult2D* resultIn(ExecutionContext& c, const char* pin = "result");

ObjectRef matrixOut(Matrix m, std::vector<std::string> rows = {}, std::vector<std::string> cols = {}, std::string title = {});
ObjectRef vectorOut(Vector v, std::vector<std::string> labels = {}, std::string title = {});

// Enregistrement par catégorie (StructuralLibrary.cpp)
void registerModelNodes(NodeLibrary& lib);
void registerElementNodes(NodeLibrary& lib);
void registerAssemblyNodes(NodeLibrary& lib);
void registerMathNodes(NodeLibrary& lib);
void registerControlNodes(NodeLibrary& lib);

} // namespace TSALab::Research::Structural::detail
