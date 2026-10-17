#pragma once

// TSALab Blueprint — objets de la programmation structurelle : valeurs transportées par les liens typés des blocs
// (matrices, vecteurs, modèle, nœuds, matériaux, sections, appuis, éléments FrameElement2D, charges, cas,
// combinaisons, numérotation des DDL, système réduit, résultat, diagrammes, courbes).
//
// Chaque objet est immuable (ObjectValue) : un bloc qui « modifie » un objet en rend un nouveau. Le type
// (typeName) est celui des broches (PinSpec::objectType) ; l'égalité par contenu permet la réutilisation des
// résultats des blocs (EvaluationCache). La science est celle de tsalab::structural (cœur scientifique) : ces
// objets ne font que la transporter et la décrire.

#include "Automation/CommandRegistry.h"
#include "tsalab/structural/DirectStiffness2D.h"
#include "tsalab/structural/Dynamics2D.h"
#include "tsalab/structural/MovingLoads2D.h"

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace TSALab::Research::Structural
{

namespace S = tsalab::structural;
using tsalab::numerics::Matrix;
using tsalab::numerics::Vector;
using TSA::Automation::ObjectRef;
using TSA::Automation::ObjectValue;

/// Noms des types d'objets (broches des blocs).
namespace Type
{
inline constexpr char Matrix[] = "Matrix";
inline constexpr char Vector[] = "Vector";
inline constexpr char Model[] = "StructuralModel";
inline constexpr char Node[] = "Node2D";
inline constexpr char Material[] = "Material";
inline constexpr char Section[] = "Section2D";
inline constexpr char Support[] = "SupportCondition";
inline constexpr char Element[] = "FrameElement2D";
inline constexpr char Load[] = "Load";
inline constexpr char LoadCase[] = "LoadCase";
inline constexpr char Combination[] = "LoadCombination";
inline constexpr char Dofs[] = "DOFSet";
inline constexpr char Reduced[] = "ReducedSystem";
inline constexpr char Result[] = "SolverResult";
inline constexpr char Diagram[] = "InternalForceDiagram";
inline constexpr char Curve[] = "Curve";
// Dynamique (pédagogique) et charges roulantes
inline constexpr char Modal[] = "ModalResult";
inline constexpr char TimeFunction[] = "TimeFunction";
inline constexpr char GroundMotion[] = "GroundMotion";
inline constexpr char TimeHistory[] = "TimeHistoryResult";
inline constexpr char Spectrum[] = "ResponseSpectrum";
inline constexpr char Spectral[] = "SpectralResult";
inline constexpr char Vehicle[] = "Vehicle";
inline constexpr char MovingLoad[] = "MovingLoadResult";
} // namespace Type

/// Matrice avec libellés de lignes et de colonnes (DDL) et titre.
struct LabeledMatrix
{
    Matrix matrix;
    std::vector<std::string> rows, cols;
    std::string title;
    bool operator==(const LabeledMatrix&) const = default;
};

struct LabeledVector
{
    Vector vector;
    std::vector<std::string> labels;
    std::string title;
    bool operator==(const LabeledVector&) const = default;
};

/// Numérotation des DDL, et partition libres / imposés une fois les conditions aux limites appliquées.
struct DofSet
{
    S::DofNumbering numbering;
    bool partitioned = false;
    S::BoundaryConditions conditions;
    bool operator==(const DofSet&) const = default;
};

struct ReducedData
{
    S::ReducedSystem system;
    S::BoundaryConditions conditions;
    S::DofNumbering numbering;
    bool operator==(const ReducedData&) const = default;
};

struct DiagramData
{
    S::InternalForceDiagram diagram;
    std::string title;
    bool operator==(const DiagramData&) const = default;
};

/// Courbe y(x) (tracé : bloc « Courbe »).
struct CurveData
{
    std::vector<double> x, y;
    std::string title, xLabel, yLabel;
    bool operator==(const CurveData&) const = default;
};

// --- Descriptions (résumé d'une ligne, contenu complet) ----------------------------------------------

std::string summary(const LabeledMatrix& m);
std::string details(const LabeledMatrix& m);
std::string summary(const LabeledVector& v);
std::string details(const LabeledVector& v);
std::string summary(const S::Model2D& m);
std::string details(const S::Model2D& m);
std::string summary(const S::Node2D& n);
std::string details(const S::Node2D& n);
std::string summary(const S::Material& m);
std::string details(const S::Material& m);
std::string summary(const S::Section2D& s);
std::string details(const S::Section2D& s);
std::string summary(const S::SupportCondition& s);
std::string details(const S::SupportCondition& s);
std::string summary(const S::FrameElement2D& e);
std::string details(const S::FrameElement2D& e);
std::string summary(const S::LoadCase& c);
std::string details(const S::LoadCase& c);
std::string summary(const S::LoadCombination& c);
std::string details(const S::LoadCombination& c);
std::string summary(const DofSet& d);
std::string details(const DofSet& d);
std::string summary(const ReducedData& r);
std::string details(const ReducedData& r);
std::string summary(const S::AnalysisResult2D& r);
std::string details(const S::AnalysisResult2D& r);
std::string summary(const DiagramData& d);
std::string details(const DiagramData& d);
std::string summary(const CurveData& c);
std::string details(const CurveData& c);
std::string summary(const S::ModalResult& r);
std::string details(const S::ModalResult& r);
std::string summary(const S::TimeFunction& f);
std::string details(const S::TimeFunction& f);
std::string summary(const S::GroundMotion& g);
std::string details(const S::GroundMotion& g);
std::string summary(const S::TimeHistoryResult& r);
std::string details(const S::TimeHistoryResult& r);
std::string summary(const S::ResponseSpectrum& s);
std::string details(const S::ResponseSpectrum& s);
std::string summary(const S::SpectralResult& r);
std::string details(const S::SpectralResult& r);
std::string summary(const S::Vehicle& v);
std::string details(const S::Vehicle& v);
std::string summary(const S::MovingLoadResult& r);
std::string details(const S::MovingLoadResult& r);
/// Description d'une charge d'élément (« répartie y local de 0 à L : −15 → −15 kN/m »).
std::string describe(const S::ElementLoad& l);

/// Objet générique : une valeur, son type, ses descriptions et son égalité par contenu.
template <typename T, const char* Name>
class ValueObject final : public ObjectValue
{
public:
    explicit ValueObject(T v) : m_value(std::move(v)) {}
    std::string typeName() const override { return Name; }
    std::string summary() const override { return Structural::summary(m_value); }
    std::string details() const override { return Structural::details(m_value); }
    bool equals(const ObjectValue& other) const override
    {
        const auto* o = dynamic_cast<const ValueObject*>(&other);
        return o && o->m_value == m_value;
    }
    const T& value() const { return m_value; }

private:
    T m_value;
};

using MatrixObject = ValueObject<LabeledMatrix, Type::Matrix>;
using VectorObject = ValueObject<LabeledVector, Type::Vector>;
using ModelObject = ValueObject<S::Model2D, Type::Model>;
using NodeObject = ValueObject<S::Node2D, Type::Node>;
using MaterialObject = ValueObject<S::Material, Type::Material>;
using SectionObject = ValueObject<S::Section2D, Type::Section>;
using SupportObject = ValueObject<S::SupportCondition, Type::Support>;
using ElementObject = ValueObject<S::FrameElement2D, Type::Element>;
using LoadObject = ValueObject<S::LoadCase, Type::Load>;              ///< groupe de charges (sans identité de cas)
using LoadCaseObject = ValueObject<S::LoadCase, Type::LoadCase>;
using CombinationObject = ValueObject<S::LoadCombination, Type::Combination>;
using DofObject = ValueObject<DofSet, Type::Dofs>;
using ReducedObject = ValueObject<ReducedData, Type::Reduced>;
using ResultObject = ValueObject<S::AnalysisResult2D, Type::Result>;
using DiagramObject = ValueObject<DiagramData, Type::Diagram>;
using CurveObject = ValueObject<CurveData, Type::Curve>;
using ModalObject = ValueObject<S::ModalResult, Type::Modal>;
using TimeFunctionObject = ValueObject<S::TimeFunction, Type::TimeFunction>;
using GroundMotionObject = ValueObject<S::GroundMotion, Type::GroundMotion>;
using TimeHistoryObject = ValueObject<S::TimeHistoryResult, Type::TimeHistory>;
using SpectrumObject = ValueObject<S::ResponseSpectrum, Type::Spectrum>;
using SpectralObject = ValueObject<S::SpectralResult, Type::Spectral>;
using VehicleObject = ValueObject<S::Vehicle, Type::Vehicle>;
using MovingLoadObject = ValueObject<S::MovingLoadResult, Type::MovingLoad>;

template <typename O, typename T>
ObjectRef make(T value)
{
    return std::make_shared<const O>(std::move(value));
}

/// Libellés des DDL locaux d'un élément (« N2.ux » … « N3.θz »).
std::vector<std::string> elementDofLabels(const S::FrameElement2D& e);
/// Libellés de DDL globaux (numérotation) ; sous-ensemble si `dofs` est donné.
std::vector<std::string> dofLabels(const S::DofNumbering& numbering, const std::vector<int>* dofs = nullptr);

} // namespace TSALab::Research::Structural
