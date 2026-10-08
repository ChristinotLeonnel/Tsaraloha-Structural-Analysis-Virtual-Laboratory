#include "SolverLabPage.h"

#include "Analysis/ResultsModel.h"

#include <QApplication>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>

namespace TSALab::UI
{

using namespace TSALab::Research;

// -----------------------------------------------------------------------------
// Courbe de convergence du gradient conjugué : log10(‖r_k‖/‖f‖) en fonction de k.
// -----------------------------------------------------------------------------
class ConvergencePlot : public QWidget
{
public:
    explicit ConvergencePlot(QWidget* parent = nullptr) : QWidget(parent) { setMinimumHeight(180); }

    void setHistory(std::vector<double> history, double tolerance)
    {
        m_history = std::move(history);
        m_tolerance = tolerance;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const QRectF area = QRectF(rect()).adjusted(52, 12, -12, -28);
        const QColor fg = palette().color(QPalette::WindowText);
        QColor grid = fg;
        grid.setAlpha(40);
        p.setPen(grid);
        p.drawRect(area);

        p.setPen(fg);
        p.drawText(QRectF(0, rect().bottom() - 22, width(), 20), Qt::AlignCenter,
                   tr("Gradient conjugué : log₁₀(‖r‖/‖f‖) par itération"));
        if (m_history.size() < 2)
        {
            p.drawText(area, Qt::AlignCenter, tr("Lancer l'expérience avec le gradient conjugué pour tracer la convergence."));
            return;
        }

        std::vector<double> logs;
        logs.reserve(m_history.size());
        for (double r : m_history) logs.push_back(std::log10(std::max(r, 1e-300)));
        double lo = std::min(*std::min_element(logs.begin(), logs.end()), std::log10(m_tolerance));
        double hi = *std::max_element(logs.begin(), logs.end());
        lo = std::floor(lo);
        hi = std::ceil(hi);
        if (hi - lo < 1.0) hi = lo + 1.0;

        auto toPoint = [&](std::size_t k, double v) {
            const double x = area.left() + area.width() * double(k) / double(logs.size() - 1);
            const double y = area.bottom() - area.height() * (v - lo) / (hi - lo);
            return QPointF(x, y);
        };

        // Graduations décimales
        for (int d = int(lo); d <= int(hi); ++d)
        {
            const double y = area.bottom() - area.height() * (d - lo) / (hi - lo);
            p.setPen(grid);
            p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
            p.setPen(fg);
            p.drawText(QRectF(0, y - 8, area.left() - 6, 16), Qt::AlignRight | Qt::AlignVCenter, QString::number(d));
        }

        // Tolérance visée
        const double yTol = area.bottom() - area.height() * (std::log10(m_tolerance) - lo) / (hi - lo);
        p.setPen(QPen(QColor("#18A999"), 1, Qt::DashLine));
        p.drawLine(QPointF(area.left(), yTol), QPointF(area.right(), yTol));

        QPainterPath path(toPoint(0, logs[0]));
        for (std::size_t k = 1; k < logs.size(); ++k) path.lineTo(toPoint(k, logs[k]));
        p.setPen(QPen(QColor("#7C4DFF"), 2));
        p.drawPath(path);
    }

private:
    std::vector<double> m_history;
    double m_tolerance = 1e-10;
};

// -----------------------------------------------------------------------------
// SolverLabPage
// -----------------------------------------------------------------------------

namespace
{
enum Column { ColMethod, ColStatus, ColIterations, ColResidual, ColDeviation, ColMinPivot, ColMaxPivot, ColTime, ColCount };

QString sci(double v) { return QString::number(v, 'e', 3); }
} // namespace

SolverLabPage::SolverLabPage(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 20, 24, 20);
    root->setSpacing(10);

    auto* title = new QLabel(tr("SOLVER LAB — résolution de K·U = F"), this);
    title->setStyleSheet("font-size: 18px; font-weight: 700;");
    root->addWidget(title);
    auto* intro = new QLabel(tr("Rejoue la résolution du dernier calcul OpenSees avec les solveurs instrumentés du laboratoire. "
                                "K et U sont ceux extraits du calcul (mode ADVANCED) ; F = K·U regroupe les efforts nodaux "
                                "équivalents vus par le solveur. Chaque méthode est comparée à la solution d'OpenSees."), this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    m_status->setTextInteractionFlags(Qt::TextSelectableByMouse);
    root->addWidget(m_status);
    m_systemInfo = new QLabel(this);
    m_systemInfo->setWordWrap(true);
    root->addWidget(m_systemInfo);

    auto* options = new QHBoxLayout();
    m_chkLu = new QCheckBox(QString::fromUtf8(solverMethodName(SolverMethod::GaussLU)), this);
    m_chkCholesky = new QCheckBox(QString::fromUtf8(solverMethodName(SolverMethod::Cholesky)), this);
    m_chkCg = new QCheckBox(QString::fromUtf8(solverMethodName(SolverMethod::ConjugateGradient)), this);
    m_chkCondition = new QCheckBox(tr("Conditionnement spectral (n ≤ %1)").arg(kMaxSpectralEquations), this);
    for (auto* c : { m_chkLu, m_chkCholesky, m_chkCg }) c->setChecked(true);
    m_btnRun = new QPushButton(tr("Lancer l'expérience"), this);
    for (QWidget* w : std::initializer_list<QWidget*>{ m_chkLu, m_chkCholesky, m_chkCg, m_chkCondition }) options->addWidget(w);
    options->addStretch();
    options->addWidget(m_btnRun);
    root->addLayout(options);
    connect(m_btnRun, &QPushButton::clicked, this, &SolverLabPage::runExperiment);

    m_table = new QTableWidget(0, ColCount, this);
    m_table->setHorizontalHeaderLabels({ tr("Méthode"), tr("Statut"), tr("Itérations"), tr("Résidu ‖K·x − F‖/‖F‖"),
                                         tr("Écart / OpenSees"), tr("Pivot min"), tr("Pivot max"), tr("Temps (ms)") });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->hide();
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setMinimumHeight(120);
    root->addWidget(m_table);

    m_conditionLabel = new QLabel(this);
    m_conditionLabel->setWordWrap(true);
    root->addWidget(m_conditionLabel);

    m_plot = new ConvergencePlot(this);
    root->addWidget(m_plot, 1);

    rebuildInput();
}

void SolverLabPage::setResults(std::shared_ptr<const TSA::Analysis::ResultsModel> results)
{
    m_results = std::move(results);
    m_table->setRowCount(0);
    m_conditionLabel->clear();
    m_plot->setHistory({}, 1e-10);
    if (isVisible()) rebuildInput();
    else m_inputValid = false;
}

void SolverLabPage::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    rebuildInput(); // les résultats ont pu devenir obsolètes (modèle modifié) depuis le dernier affichage
}

void SolverLabPage::rebuildInput()
{
    m_inputValid = false;
    m_systemInfo->clear();
    std::string why = "Aucun calcul : ouvrir l'espace MODÈLE et lancer un calcul OpenSees (F5) en extraction ADVANCED.";
    if (m_results && buildSolverExperiment(*m_results, m_input, &why))
    {
        m_inputValid = true;
        const int n = m_input.K.rows;
        m_status->setText(tr("✔ Système disponible — cas « %1 ».").arg(QString::fromStdString(m_input.caseName)));
        m_systemInfo->setText(tr("%1 équations · %2 coefficients non nuls (%3 %) · asymétrie relative %4 · unités de K : %5")
                                  .arg(n)
                                  .arg(LinAlg::nonZeroCount(m_input.K))
                                  .arg(100.0 * LinAlg::nonZeroCount(m_input.K) / std::max(1.0, double(n) * n), 0, 'f', 2)
                                  .arg(sci(LinAlg::asymmetry(m_input.K)))
                                  .arg(m_input.units.empty() ? tr("non précisées") : QString::fromStdString(m_input.units)));
        m_chkCondition->setEnabled(n <= kMaxSpectralEquations);
    }
    else
    {
        m_status->setText(tr("Expérience indisponible : %1").arg(QString::fromStdString(why)));
        m_chkCondition->setEnabled(false);
    }
    m_btnRun->setEnabled(m_inputValid);
}

void SolverLabPage::runExperiment()
{
    if (!m_inputValid) return;
    QApplication::setOverrideCursor(Qt::WaitCursor);

    std::vector<SolverMethod> methods;
    if (m_chkLu->isChecked()) methods.push_back(SolverMethod::GaussLU);
    if (m_chkCholesky->isChecked()) methods.push_back(SolverMethod::Cholesky);
    if (m_chkCg->isChecked()) methods.push_back(SolverMethod::ConjugateGradient);

    const SolverSettings settings;
    m_table->setRowCount(0);
    m_plot->setHistory({}, settings.tolerance);
    for (SolverMethod method : methods)
    {
        const SolverRun run = runSolver(m_input, method, settings);
        const SolverReport& r = run.report;
        const int row = m_table->rowCount();
        m_table->insertRow(row);
        const bool direct = method != SolverMethod::ConjugateGradient;
        QString status = r.success ? tr("Résolu") : QString::fromStdString(r.message);
        if (!r.success && r.failedEquation >= 0 && r.failedEquation < int(m_input.labels.size()))
            status += tr(" — équation %1").arg(QString::fromStdString(m_input.labels[std::size_t(r.failedEquation)]));
        const QStringList cells = {
            QString::fromUtf8(solverMethodName(method)),
            status,
            QString::number(r.iterations),
            r.success ? sci(r.relativeResidual) : QStringLiteral("—"),
            run.deviationFromReference >= 0.0 ? sci(run.deviationFromReference) : QStringLiteral("—"),
            direct && r.minPivot > 0.0 ? sci(r.minPivot) : QStringLiteral("—"),
            direct && r.maxPivot > 0.0 ? sci(r.maxPivot) : QStringLiteral("—"),
            QString::number(r.elapsedMs, 'f', 1),
        };
        for (int c = 0; c < ColCount; ++c)
        {
            auto* item = new QTableWidgetItem(cells[c]);
            if (c == ColStatus) item->setToolTip(QString::fromStdString(r.message));
            m_table->setItem(row, c, item);
        }
        if (method == SolverMethod::ConjugateGradient) m_plot->setHistory(r.residualHistory, settings.tolerance);
    }

    if (m_chkCondition->isEnabled() && m_chkCondition->isChecked())
    {
        const double kappa = conditionNumber(m_input.K);
        m_conditionLabel->setText(std::isfinite(kappa)
            ? tr("Conditionnement spectral κ(K) = λmax/λmin = %1 : environ %2 chiffres significatifs perdus en double précision.")
                  .arg(sci(kappa)).arg(std::log10(std::max(kappa, 1.0)), 0, 'f', 1)
            : tr("Conditionnement spectral infini : K singulière (mécanisme ou DDL non retenu)."));
    }
    else
    {
        m_conditionLabel->clear();
    }
    QApplication::restoreOverrideCursor();
}

} // namespace TSALab::UI
