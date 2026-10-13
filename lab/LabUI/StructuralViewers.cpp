// Vues des objets de la programmation structurelle (voir StructuralViewers.h).

#include "StructuralViewers.h"

#include "Research/Structural/StructuralObjects.h"
#include "UI/Blueprint/BlueprintObjectViewer.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace TSALab::UI
{

namespace S = tsalab::structural;
namespace R = TSALab::Research::Structural;

namespace
{
QString num(double v, int precision = 4)
{
    return QString::number(v == 0.0 ? 0.0 : v, 'g', precision);
}

// ---------------------------------------------------------------------------------------------
// Tracé y(x)
// ---------------------------------------------------------------------------------------------

class CurvePlot : public QWidget
{
public:
    CurvePlot(std::vector<double> x, std::vector<double> y, QString title, QString xLabel, QString yLabel, QColor color, QWidget* parent)
        : QWidget(parent), m_x(std::move(x)), m_y(std::move(y)), m_title(std::move(title)), m_xLabel(std::move(xLabel)), m_yLabel(std::move(yLabel)),
          m_color(std::move(color))
    {
        setMinimumHeight(140);
    }
    QSize sizeHint() const override { return { 640, 220 }; }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette().base());
        const QRectF area = QRectF(rect()).adjusted(64, 24, -16, -30);
        p.setPen(palette().text().color());
        p.drawText(QRectF(8, 2, width() - 16, 20), Qt::AlignLeft | Qt::AlignVCenter, m_title);
        if (m_x.size() < 2 || m_x.size() != m_y.size())
        {
            p.drawText(area, Qt::AlignCenter, QObject::tr("pas de données"));
            return;
        }
        double x0 = *std::min_element(m_x.begin(), m_x.end()), x1 = *std::max_element(m_x.begin(), m_x.end());
        double y0 = std::min(0.0, *std::min_element(m_y.begin(), m_y.end())), y1 = std::max(0.0, *std::max_element(m_y.begin(), m_y.end()));
        if (x1 - x0 <= 0.0) x1 = x0 + 1.0;
        if (y1 - y0 <= 0.0) y1 = y0 + 1.0, y0 -= 1.0;
        auto map = [&](double x, double y) {
            return QPointF(area.left() + (x - x0) / (x1 - x0) * area.width(), area.bottom() - (y - y0) / (y1 - y0) * area.height());
        };
        p.setPen(QPen(palette().mid().color(), 1.0));
        p.drawRect(area);
        p.drawLine(map(x0, 0.0), map(x1, 0.0));   // axe y = 0
        QPainterPath path(map(m_x[0], m_y[0]));
        for (std::size_t k = 1; k < m_x.size(); ++k) path.lineTo(map(m_x[k], m_y[k]));
        QPainterPath fill = path;
        fill.lineTo(map(m_x.back(), 0.0));
        fill.lineTo(map(m_x.front(), 0.0));
        QColor area2 = m_color;
        area2.setAlpha(50);
        p.fillPath(fill, area2);
        p.setPen(QPen(m_color, 2.0));
        p.drawPath(path);
        p.setPen(palette().text().color());
        QFont f = p.font();
        f.setPointSizeF(8.0);
        p.setFont(f);
        p.drawText(QRectF(2, area.top() - 8, 60, 16), Qt::AlignRight | Qt::AlignVCenter, num(y1));
        p.drawText(QRectF(2, area.bottom() - 8, 60, 16), Qt::AlignRight | Qt::AlignVCenter, num(y0));
        p.drawText(QRectF(area.left(), area.bottom() + 2, 80, 14), Qt::AlignLeft, num(x0));
        p.drawText(QRectF(area.right() - 80, area.bottom() + 2, 80, 14), Qt::AlignRight, num(x1));
        p.drawText(QRectF(area.left(), area.bottom() + 14, area.width(), 14), Qt::AlignHCenter, m_xLabel);
        p.save();
        p.translate(12, area.center().y());
        p.rotate(-90);
        p.drawText(QRectF(-area.height() / 2, -8, area.height(), 16), Qt::AlignCenter, m_yLabel);
        p.restore();
    }

private:
    std::vector<double> m_x, m_y;
    QString m_title, m_xLabel, m_yLabel;
    QColor m_color;
};

QWidget* diagramWidget(const R::DiagramData& d, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QVBoxLayout(w);
    std::vector<double> x, N, V, M, v;
    for (const auto& p : d.diagram.points)
    {
        x.push_back(p.x), N.push_back(p.N), V.push_back(p.V), M.push_back(p.M), v.push_back(p.v * 1000.0);
    }
    const QString id = QString::number(d.diagram.element);
    lay->addWidget(new CurvePlot(x, N, QObject::tr("Effort normal N (kN), traction > 0 — élément %1").arg(id), "x (m)", "N", QColor("#1E88E5"), w));
    lay->addWidget(new CurvePlot(x, V, QObject::tr("Effort tranchant V (kN), V = dM/dx — élément %1").arg(id), "x (m)", "V", QColor("#43A047"), w));
    lay->addWidget(new CurvePlot(x, M, QObject::tr("Moment fléchissant M (kN·m), > 0 tend la fibre y' < 0 — élément %1").arg(id), "x (m)", "M", QColor("#E53935"), w));
    lay->addWidget(new CurvePlot(x, v, QObject::tr("Déplacement transversal v (mm), repère local — élément %1").arg(id), "x (m)", "v", QColor("#8E24AA"), w));
    return w;
}

QWidget* curveWidget(const R::CurveData& c, QWidget* parent)
{
    return new CurvePlot(c.x, c.y, QString::fromStdString(c.title), QString::fromStdString(c.xLabel), QString::fromStdString(c.yLabel), QColor("#1E88E5"), parent);
}

// ---------------------------------------------------------------------------------------------
// Tableau d'une matrice ou d'un vecteur
// ---------------------------------------------------------------------------------------------

QWidget* tableWidget(const tsalab::numerics::Matrix& m, const std::vector<std::string>& rows, const std::vector<std::string>& cols, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* lay = new QVBoxLayout(w);
    constexpr int kMaxCells = 300;
    if (m.rows > kMaxCells || m.cols > kMaxCells)
    {
        lay->addWidget(new QLabel(QObject::tr("Matrice %1×%2 : trop grande pour le tableau (voir l'onglet Texte).").arg(m.rows).arg(m.cols), w));
        return w;
    }
    auto* heat = new QCheckBox(QObject::tr("Carte de couleurs (|aᵢⱼ| / max) et zéros estompés"), w);
    heat->setChecked(true);
    auto* table = new QTableWidget(m.rows, m.cols, w);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    QStringList h, v;
    for (int j = 0; j < m.cols; ++j) h << (j < static_cast<int>(cols.size()) ? QString::fromStdString(cols[static_cast<std::size_t>(j)]) : QString::number(j));
    for (int i = 0; i < m.rows; ++i) v << (i < static_cast<int>(rows.size()) ? QString::fromStdString(rows[static_cast<std::size_t>(i)]) : QString::number(i));
    table->setHorizontalHeaderLabels(h);
    table->setVerticalHeaderLabels(v);
    double peak = 0.0;
    for (double x : m.data) peak = std::max(peak, std::abs(x));
    for (int i = 0; i < m.rows; ++i)
        for (int j = 0; j < m.cols; ++j)
        {
            auto* item = new QTableWidgetItem(num(m(i, j), 6));
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            item->setToolTip(QString::number(m(i, j), 'g', 17));
            table->setItem(i, j, item);
        }
    auto paint = [table, m, peak](bool on) {
        for (int i = 0; i < m.rows; ++i)
            for (int j = 0; j < m.cols; ++j)
            {
                auto* item = table->item(i, j);
                if (!on || peak <= 0.0)
                {
                    item->setBackground(QBrush());
                    item->setForeground(QBrush());
                    continue;
                }
                const double a = std::abs(m(i, j)) / peak;
                QColor c = m(i, j) >= 0.0 ? QColor(229, 57, 53) : QColor(30, 136, 229);
                c.setAlphaF(a > 0.0 ? 0.12 + 0.6 * std::sqrt(a) : 0.0);
                item->setBackground(c);
                item->setForeground(m(i, j) == 0.0 ? QBrush(QColor(150, 150, 150)) : QBrush());
            }
    };
    paint(true);
    QObject::connect(heat, &QCheckBox::toggled, table, paint);
    table->resizeColumnsToContents();
    lay->addWidget(heat);
    lay->addWidget(table, 1);
    return w;
}
} // namespace

// ---------------------------------------------------------------------------------------------
// Portique
// ---------------------------------------------------------------------------------------------

class Frame2DCanvas : public QWidget
{
public:
    explicit Frame2DCanvas(QWidget* parent) : QWidget(parent) { setMinimumSize(240, 200); }
    QSize sizeHint() const override { return { 900, 560 }; }

    S::Model2D model;
    std::optional<S::AnalysisResult2D> result;
    double scale = 0.0;      ///< amplification effective
    int diagram = 0;         ///< 0 aucun, 1 N, 2 V, 3 M
    bool reactions = true, labels = true;

    double automaticScale() const
    {
        if (!result || !result->success) return 1.0;
        double umax = 0.0;
        for (std::size_t p = 0; p < result->dofs.nodeIds.size(); ++p) umax = std::max(umax, std::hypot(result->d[3 * p], result->d[3 * p + 1]));
        for (const auto& e : result->elements)
            for (const auto& q : e.diagram.points) umax = std::max(umax, std::hypot(q.u, q.v));
        const double extent = std::max(model.extent(), 1e-9);
        return umax > 0.0 ? 0.08 * extent / umax : 1.0;
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), palette().base());
        const QColor ink = palette().text().color();
        if (model.nodes.empty())
        {
            p.setPen(ink);
            p.drawText(rect(), Qt::AlignCenter, QObject::tr("Aucun modèle : exécuter un graphe qui visualise un résultat (bloc « Visualiser »)."));
            return;
        }
        double x0 = model.nodes[0].x, x1 = x0, y0 = model.nodes[0].y, y1 = y0;
        for (const auto& n : model.nodes) x0 = std::min(x0, n.x), x1 = std::max(x1, n.x), y0 = std::min(y0, n.y), y1 = std::max(y1, n.y);
        const double extent = std::max({ x1 - x0, y1 - y0, 1e-6 });
        const double pad = 0.22 * extent;
        x0 -= pad, x1 += pad, y0 -= pad, y1 += pad;
        const double margin = 30.0, legendBand = 30.0;
        const double k = std::min((width() - 2 * margin) / (x1 - x0), (height() - legendBand - 2 * margin) / (y1 - y0));
        const double contentH = k * (y1 - y0);
        const double ox = (width() - k * (x1 - x0)) / 2.0;
        const double top = legendBand + (height() - legendBand - contentH) / 2.0;
        auto map = [&](double x, double y) { return QPointF(ox + (x - x0) * k, top + contentH - (y - y0) * k); };   // Y vers le haut
        auto nodeOf = [&](int id) { return model.node(id); };

        // Diagramme : échelle commune à tous les éléments
        double dmax = 0.0;
        auto value = [&](const S::DiagramPoint& q) { return diagram == 1 ? q.N : diagram == 2 ? q.V : q.M; };
        if (result && result->success && diagram > 0)
            for (const auto& e : result->elements)
                for (const auto& q : e.diagram.points) dmax = std::max(dmax, std::abs(value(q)));
        const double dscale = dmax > 0.0 ? 0.12 * extent / dmax : 0.0;

        // Éléments (modèle initial)
        p.setPen(QPen(QColor(140, 140, 140), 2.0));
        for (const auto& e : model.elements)
        {
            const auto* a = nodeOf(e.nodeI);
            const auto* b = nodeOf(e.nodeJ);
            if (a && b) p.drawLine(map(a->x, a->y), map(b->x, b->y));
        }
        // Diagrammes N, V, M (M tracé du côté de la fibre tendue)
        if (dscale > 0.0)
        {
            const QColor colors[4] = { QColor(), QColor("#1E88E5"), QColor("#43A047"), QColor("#E53935") };
            for (const auto& er : result->elements)
            {
                const auto* def = model.element(er.element);
                const auto* a = def ? nodeOf(def->nodeI) : nullptr;
                const auto* b = def ? nodeOf(def->nodeJ) : nullptr;
                if (!a || !b) continue;
                const double L = std::hypot(b->x - a->x, b->y - a->y);
                if (L <= 0.0) continue;
                const double c = (b->x - a->x) / L, s = (b->y - a->y) / L;
                const double sign = diagram == 3 ? -1.0 : 1.0;   // M > 0 : fibre y' < 0 tendue
                QPolygonF poly;
                poly << map(a->x, a->y);
                double best = 0.0;
                QPointF bestAt;
                for (const auto& q : er.diagram.points)
                {
                    const double off = sign * value(q) * dscale;
                    const QPointF pt = map(a->x + c * q.x - s * off, a->y + s * q.x + c * off);
                    poly << pt;
                    if (std::abs(value(q)) > std::abs(best)) best = value(q), bestAt = pt;
                }
                poly << map(b->x, b->y);
                QColor fill = colors[diagram];
                fill.setAlpha(60);
                p.setPen(QPen(colors[diagram], 1.2));
                p.setBrush(fill);
                p.drawPolygon(poly);
                p.setBrush(Qt::NoBrush);
                if (best != 0.0)
                {
                    p.setPen(colors[diagram]);
                    p.drawText(bestAt + QPointF(4, -4), num(best));
                }
            }
        }
        // Déformée amplifiée
        if (result && result->success)
        {
            p.setPen(QPen(QColor("#1565C0"), 2.0, Qt::SolidLine, Qt::RoundCap));
            for (const auto& er : result->elements)
            {
                const auto* def = model.element(er.element);
                const auto* a = def ? nodeOf(def->nodeI) : nullptr;
                const auto* b = def ? nodeOf(def->nodeJ) : nullptr;
                if (!a || !b) continue;
                const double L = std::hypot(b->x - a->x, b->y - a->y);
                if (L <= 0.0) continue;
                const double c = (b->x - a->x) / L, s = (b->y - a->y) / L;
                QPainterPath path;
                bool first = true;
                for (const auto& q : er.diagram.points)
                {
                    const double xl = q.x + scale * q.u, yl = scale * q.v;
                    const QPointF pt = map(a->x + c * xl - s * yl, a->y + s * xl + c * yl);
                    if (first) path.moveTo(pt), first = false;
                    else path.lineTo(pt);
                }
                p.drawPath(path);
            }
        }
        // Appuis
        p.setPen(QPen(ink, 1.3));
        for (const auto& sup : model.supports)
        {
            const auto* n = nodeOf(sup.node);
            if (!n) continue;
            const QPointF c = map(n->x, n->y);
            if (sup.fixUx && sup.fixUy && sup.fixRz)
            {
                p.drawLine(c + QPointF(-12, 6), c + QPointF(12, 6));
                for (int h = -12; h <= 8; h += 5) p.drawLine(c + QPointF(h, 6), c + QPointF(h + 4, 11));
            }
            else
            {
                QPolygonF tri { c, c + QPointF(-8, 12), c + QPointF(8, 12) };
                p.drawPolygon(tri);
                if (!sup.fixUx) p.drawLine(c + QPointF(-10, 16), c + QPointF(10, 16));
            }
        }
        // Nœuds et libellés
        for (const auto& n : model.nodes)
        {
            const QPointF c = map(n.x, n.y);
            p.setBrush(ink);
            p.drawEllipse(c, 2.5, 2.5);
            p.setBrush(Qt::NoBrush);
            if (labels) p.drawText(c + QPointF(5, -5), QStringLiteral("N%1").arg(n.id));
        }
        if (labels)
        {
            p.setPen(QColor(120, 120, 120));
            for (const auto& e : model.elements)
            {
                const auto* a = nodeOf(e.nodeI);
                const auto* b = nodeOf(e.nodeJ);
                if (a && b) p.drawText(map((a->x + b->x) / 2, (a->y + b->y) / 2) + QPointF(4, 14), QStringLiteral("e%1").arg(e.id));
            }
        }
        // Réactions
        if (reactions && result && result->success)
        {
            const QColor rc("#D81B60");
            p.setPen(QPen(rc, 1.8));
            for (const auto& sup : model.supports)
            {
                const auto* n = nodeOf(sup.node);
                if (!n) continue;
                const auto r = result->nodeReaction(sup.node);
                const QPointF c = map(n->x, n->y);
                auto arrow = [&](QPointF dir, double v, const QString& name) {
                    if (std::abs(v) < 1e-9) return;
                    const QPointF tip = c + dir * 6.0, tail = c + dir * 46.0;
                    const QPointF from = v > 0 ? tail : tip, to = v > 0 ? tip : tail;   // la flèche pointe dans le sens de la force
                    p.drawLine(from, to);
                    const QPointF u = (to - from) / std::hypot(to.x() - from.x(), to.y() - from.y());
                    const QPointF nrm(-u.y(), u.x());
                    p.drawLine(to, to - u * 8 + nrm * 4);
                    p.drawLine(to, to - u * 8 - nrm * 4);
                    p.drawText(tail + QPointF(4, 4), QStringLiteral("%1 = %2").arg(name, num(v)));
                };
                arrow(QPointF(-1, 0), sup.fixUx ? r[0] : 0.0, QStringLiteral("Rx"));    // flèche horizontale à gauche du nœud
                arrow(QPointF(0, 1), sup.fixUy ? r[1] : 0.0, QStringLiteral("Ry"));     // flèche verticale sous le nœud
                if (sup.fixRz && std::abs(r[2]) > 1e-9) p.drawText(c + QPointF(10, 30), QStringLiteral("Mz = %1 kN·m").arg(num(r[2])));
            }
        }
        // Légende
        p.setPen(ink);
        QString legend;
        if (result && result->success)
            legend = QObject::tr("Déformée amplifiée ×%1 (bleu)").arg(num(scale, 4));
        else if (result)
            legend = QObject::tr("Calcul en échec : %1").arg(QString::fromStdString(result->message));
        else
            legend = QObject::tr("Modèle (non calculé)");
        if (dscale > 0.0)
            legend += QObject::tr("   |   %1 : max |valeur| = %2 %3").arg(diagram == 1 ? "N" : diagram == 2 ? "V" : "M", num(dmax), diagram == 3 ? "kN·m" : "kN");
        p.drawText(QRectF(8, 4, width() - 16, 22), Qt::AlignLeft | Qt::AlignVCenter, legend);
    }
};

Frame2DView::Frame2DView(QWidget* parent) : QWidget(parent)
{
    // Largeur minimale explicite : elle prime sur celle de la disposition, la vue se resserre dans un dock étroit au lieu
    // d'élargir la fenêtre principale (qui doit tenir sur l'écran).
    setMinimumSize(260, 260);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(4, 4, 4, 4);
    m_title = new QLabel(tr("Résultats 2D"), this);
    m_title->setStyleSheet(QStringLiteral("font-weight: 700;"));
    m_title->setWordWrap(true);
    lay->addWidget(m_title);
    // Commandes sur deux lignes : la vue tient dans un dock étroit (largeur minimale de la fenêtre inchangée)
    auto* bar = new QHBoxLayout();
    auto* bar2 = new QHBoxLayout();
    bar->addWidget(new QLabel(tr("Amplification de la déformée :"), this));
    m_scale = new QDoubleSpinBox(this);
    m_scale->setRange(0.0, 1e7);
    m_scale->setDecimals(1);
    m_scale->setSpecialValueText(tr("automatique"));
    m_scale->setToolTip(tr("Facteur appliqué aux déplacements pour le dessin de la déformée (0 : automatique, 8 % de la plus grande dimension)"));
    bar->addWidget(m_scale);
    m_scaleLabel = new QLabel(this);
    m_scaleLabel->setWordWrap(true);
    bar->addWidget(m_scaleLabel);
    bar->addStretch();
    bar2->addWidget(new QLabel(tr("Diagramme :"), this));
    m_diagram = new QComboBox(this);
    m_diagram->addItems({ tr("aucun"), tr("N (kN)"), tr("V (kN)"), tr("M (kN·m)") });
    m_diagram->setCurrentIndex(3);
    bar2->addWidget(m_diagram);
    m_reactions = new QCheckBox(tr("Réactions"), this);
    m_reactions->setChecked(true);
    m_labels = new QCheckBox(tr("Libellés"), this);
    m_labels->setChecked(true);
    bar2->addWidget(m_reactions);
    bar2->addWidget(m_labels);
    bar2->addStretch();
    lay->addLayout(bar);
    lay->addLayout(bar2);
    auto* split = new QSplitter(Qt::Vertical, this);
    m_canvas = new Frame2DCanvas(split);
    m_text = new QPlainTextEdit(split);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_text->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    split->setStretchFactor(0, 3);
    split->setStretchFactor(1, 1);
    lay->addWidget(split, 1);
    m_canvas->diagram = 3;
    connect(m_scale, &QDoubleSpinBox::valueChanged, this, [this](double) {
        m_canvas->scale = m_scale->value() > 0.0 ? m_scale->value() : m_canvas->automaticScale();
        refreshInfo();
        m_canvas->update();
    });
    connect(m_diagram, &QComboBox::currentIndexChanged, this, [this](int i) {
        m_canvas->diagram = i;
        m_canvas->update();
    });
    connect(m_reactions, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->reactions = on;
        m_canvas->update();
    });
    connect(m_labels, &QCheckBox::toggled, this, [this](bool on) {
        m_canvas->labels = on;
        m_canvas->update();
    });
    refreshInfo();
}

void Frame2DView::setResult(const S::AnalysisResult2D& result, double scale)
{
    m_canvas->model = result.model;
    m_canvas->result = result;
    const QSignalBlocker block(m_scale);
    m_scale->setValue(scale > 0.0 ? scale : 0.0);
    m_canvas->scale = scale > 0.0 ? scale : m_canvas->automaticScale();
    m_text->setPlainText(QString::fromStdString(R::details(result)));
    refreshInfo();
    m_canvas->update();
}

void Frame2DView::setModel(const S::Model2D& model)
{
    m_canvas->model = model;
    m_canvas->result.reset();
    m_text->setPlainText(QString::fromStdString(R::details(model)));
    refreshInfo();
    m_canvas->update();
}

void Frame2DView::setTitle(const QString& title)
{
    m_title->setText(title);
}

double Frame2DView::amplification() const
{
    return m_canvas->scale;
}

bool Frame2DView::hasResult() const
{
    return m_canvas->result.has_value() && m_canvas->result->success;
}

double Frame2DView::automaticAmplification() const
{
    return m_canvas->automaticScale();
}

void Frame2DView::refreshInfo()
{
    if (!m_canvas->result || !m_canvas->result->success)
    {
        m_scaleLabel->setText(QString());
        return;
    }
    m_scaleLabel->setText(m_scale->value() > 0.0 ? tr("(×%1 imposé)").arg(num(m_canvas->scale)) : tr("(×%1 automatique)").arg(num(m_canvas->scale)));
}

void registerStructuralViewers()
{
    using TSA::Automation::ObjectRef;
    TSA::UI::registerObjectViewer(R::Type::Matrix, [](const ObjectRef& o, QWidget* parent) -> QWidget* {
        const auto* m = dynamic_cast<const R::MatrixObject*>(o.get());
        return m ? tableWidget(m->value().matrix, m->value().rows, m->value().cols, parent) : nullptr;
    });
    TSA::UI::registerObjectViewer(R::Type::Vector, [](const ObjectRef& o, QWidget* parent) -> QWidget* {
        const auto* v = dynamic_cast<const R::VectorObject*>(o.get());
        if (!v) return nullptr;
        tsalab::numerics::Matrix column(static_cast<int>(v->value().vector.size()), 1);
        for (std::size_t k = 0; k < v->value().vector.size(); ++k) column(static_cast<int>(k), 0) = v->value().vector[k];
        return tableWidget(column, v->value().labels, { v->value().title.empty() ? std::string("valeur") : v->value().title }, parent);
    });
    TSA::UI::registerObjectViewer(R::Type::Diagram, [](const ObjectRef& o, QWidget* parent) -> QWidget* {
        const auto* d = dynamic_cast<const R::DiagramObject*>(o.get());
        return d ? diagramWidget(d->value(), parent) : nullptr;
    });
    TSA::UI::registerObjectViewer(R::Type::Curve, [](const ObjectRef& o, QWidget* parent) -> QWidget* {
        const auto* c = dynamic_cast<const R::CurveObject*>(o.get());
        return c ? curveWidget(c->value(), parent) : nullptr;
    });
    TSA::UI::registerObjectViewer(R::Type::Result, [](const ObjectRef& o, QWidget* parent) -> QWidget* {
        const auto* r = dynamic_cast<const R::ResultObject*>(o.get());
        if (!r) return nullptr;
        auto* view = new Frame2DView(parent);
        view->setResult(r->value());
        view->setTitle(QString::fromStdString(r->value().title));
        return view;
    });
    TSA::UI::registerObjectViewer(R::Type::Model, [](const ObjectRef& o, QWidget* parent) -> QWidget* {
        const auto* m = dynamic_cast<const R::ModelObject*>(o.get());
        if (!m) return nullptr;
        auto* view = new Frame2DView(parent);
        view->setModel(m->value());
        view->setTitle(QString::fromStdString(m->value().name));
        return view;
    });
}

} // namespace TSALab::UI
