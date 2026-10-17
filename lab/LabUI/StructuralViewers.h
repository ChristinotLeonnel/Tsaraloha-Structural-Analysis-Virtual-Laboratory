#pragma once

// TSALab Blueprint — vues des objets de la programmation structurelle (couche interface du laboratoire) :
//   Matrix / Vector          : tableau avec libellés des DDL, carte de couleurs facultative ;
//   InternalForceDiagram     : tracés N(x), V(x), M(x) et déformée v(x) ;
//   Curve                    : tracé y(x) ;
//   SolverResult, Model      : portique (Frame2DView) : modèle initial, déformée amplifiée (facteur affiché et réglable),
//                              réactions, diagrammes N, V, M, bilan d'équilibre et tableau des déplacements.
// registerStructuralViewers() les enregistre auprès de l'éditeur Blueprint (BlueprintObjectViewer).

#include "tsalab/structural/DirectStiffness2D.h"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPlainTextEdit;

namespace TSALab::UI
{

class Frame2DCanvas;

/// Portique plan et ses résultats.
class Frame2DView : public QWidget
{
    Q_OBJECT

public:
    explicit Frame2DView(QWidget* parent = nullptr);

    /// Résultat à afficher ; scale > 0 : amplification imposée, 0 : automatique (8 % de la plus grande dimension).
    void setResult(const tsalab::structural::AnalysisResult2D& result, double scale = 0.0);
    /// Modèle seul (avant calcul).
    void setModel(const tsalab::structural::Model2D& model);
    void setTitle(const QString& title);

    double amplification() const;
    bool hasResult() const;
    /// Amplification automatique calculée pour le résultat affiché.
    double automaticAmplification() const;

private:
    void refreshInfo();

    Frame2DCanvas* m_canvas = nullptr;
    QLabel* m_title = nullptr;
    QDoubleSpinBox* m_scale = nullptr;
    QLabel* m_scaleLabel = nullptr;
    QComboBox* m_diagram = nullptr;
    QCheckBox* m_reactions = nullptr;
    QCheckBox* m_labels = nullptr;
    QPlainTextEdit* m_text = nullptr;
};

/// Enregistre les vues auprès de l'éditeur Blueprint (une fois, au démarrage).
void registerStructuralViewers();

} // namespace TSALab::UI
