#pragma once

// Espace SOLVER LAB : rejoue la résolution K·U = F du dernier calcul avec les solveurs instrumentés
// du laboratoire (TSALab::Research::runSolver) et compare chaque méthode à la solution du moteur (Custom2D, OpenSees) :
// itérations, résidu, écart, pivots (détection de mécanisme), conditionnement, convergence du
// gradient conjugué. Lecture seule : le modèle et les résultats ne sont jamais modifiés.

#include "Research/Solver/SolverExperiment.h"

#include <QWidget>

#include <memory>

class QCheckBox;
class QLabel;
class QPushButton;
class QTableWidget;

namespace TSA::Analysis
{
class ResultsModel;
}

namespace TSALab::UI
{

class ConvergencePlot;

class SolverLabPage : public QWidget
{
    Q_OBJECT

public:
    explicit SolverLabPage(QWidget* parent = nullptr);

    /// Résultats du dernier calcul (nul : aucun). L'expérience est reconstruite à l'affichage.
    void setResults(std::shared_ptr<const TSA::Analysis::ResultsModel> results);

protected:
    void showEvent(QShowEvent* event) override;

private:
    void rebuildInput();
    void runExperiment();

private:
    std::shared_ptr<const TSA::Analysis::ResultsModel> m_results;
    bool m_inputValid = false;
    TSALab::Research::SolverExperimentInput m_input;

    QLabel* m_status = nullptr;
    QLabel* m_systemInfo = nullptr;
    QCheckBox* m_chkLu = nullptr;
    QCheckBox* m_chkCholesky = nullptr;
    QCheckBox* m_chkCg = nullptr;
    QCheckBox* m_chkCondition = nullptr;
    QPushButton* m_btnRun = nullptr;
    QTableWidget* m_table = nullptr;
    QLabel* m_conditionLabel = nullptr;
    ConvergencePlot* m_plot = nullptr;
};

} // namespace TSALab::UI
