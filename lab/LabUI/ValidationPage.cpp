#include "ValidationPage.h"

#include "Research/Validation/MeshConvergence.h"

#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLocale>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QThread>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <cmath>
#include <map>

namespace TSALab::UI
{

namespace
{
constexpr int kIdRole = Qt::UserRole + 1;
constexpr int kCaseRole = Qt::UserRole + 2;    // indice du cas dans le rapport (colonne 0 des résultats)
constexpr int kCheckRole = Qt::UserRole + 3;   // indice de la vérification dans le cas
QString num(double v) { return std::isfinite(v) ? QLocale(QLocale::French).toString(v, 'g', 6) : QStringLiteral("—"); }
QString pct(double v) { return std::isfinite(v) ? QLocale(QLocale::French).toString(v * 100.0, 'g', 3) + QStringLiteral(" %") : QStringLiteral("—"); }
} // namespace

ValidationPage::ValidationPage(QWidget* parent)
    : QWidget(parent)
{
    qRegisterMetaType<TSA::Validation::BenchmarkReport>("TSA::Validation::BenchmarkReport");
    buildUi();
    loadCases(Research::defaultBenchmarkDirectory());
}

ValidationPage::~ValidationPage()
{
    // Calcul en cours à la fermeture : arrêt demandé, le cas en cours se termine (aucun objet détruit sous lui)
    m_cancel = true;
    if (m_thread)
    {
        m_thread->wait();
        delete m_thread;
    }
}

void ValidationPage::buildUi()
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);

    auto* intro = new QLabel(tr("<b>Laboratoire de validation</b> — cas de référence calculés par la chaîne réelle (modèle → moteur → résultats) "
                                "et comparés à leur solution analytique ou à des résultats externes. Une grandeur que le moteur ne fournit pas "
                                "est « non disponible », jamais remplacée."),
                             this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* bar = new QHBoxLayout;
    auto* folder = new QPushButton(tr("Dossier des cas…"), this);
    auto* all = new QPushButton(tr("Tout cocher"), this);
    auto* none = new QPushButton(tr("Rien"), this);
    m_engine = new QComboBox(this);
    m_engine->addItem(tr("Moteurs déclarés par chaque cas"), QString());
    m_engine->addItem(QStringLiteral("OpenSees"), QStringLiteral("opensees"));
    m_engine->addItem(QStringLiteral("Custom2D (MetDeDeplacement)"), QStringLiteral("custom2d"));
    auto* refs = new QPushButton(tr("Références externes…"), this);
    m_run = new QPushButton(tr("▶ Exécuter les cas cochés"), this);
    m_stop = new QPushButton(tr("Arrêter"), this);
    m_export = new QPushButton(tr("Exporter le rapport…"), this);
    m_convergence = new QPushButton(tr("Convergence (CSV)…"), this);
    m_convergence->setToolTip(tr("Série de la vérification sélectionnée : une valeur par taille de maille"));
    m_stop->setEnabled(false);
    m_export->setEnabled(false);
    m_convergence->setEnabled(false);
    for (QWidget* w : std::initializer_list<QWidget*> { folder, all, none, m_engine, refs, m_run, m_stop, m_export, m_convergence }) bar->addWidget(w);
    bar->addStretch();
    root->addLayout(bar);
    m_referencesLabel = new QLabel(this);
    root->addWidget(m_referencesLabel);

    auto* split = new QSplitter(Qt::Vertical, this);
    auto* top = new QSplitter(Qt::Horizontal, split);
    m_tree = new QTreeWidget(top);
    m_tree->setHeaderLabels({ tr("Cas"), tr("Titre"), tr("Moteurs") });
    m_tree->setRootIsDecorated(true);
    m_details = new QTextBrowser(top);
    top->setStretchFactor(0, 3);
    top->setStretchFactor(1, 2);
    m_table = new QTableWidget(0, 11, split);
    m_table->setHorizontalHeaderLabels({ tr("Cas"), tr("Moteur"), tr("Grandeur"), tr("Libellé"), tr("Référence"), tr("Valeur"),
                                         tr("Écart abs."), tr("Écart rel."), tr("Tolérance"), tr("Origine"), tr("État") });
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    split->setStretchFactor(0, 2);
    split->setStretchFactor(1, 3);
    root->addWidget(split, 1);

    auto* foot = new QHBoxLayout;
    m_summary = new QLabel(tr("Aucune exécution."), this);
    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    foot->addWidget(m_summary, 1);
    foot->addWidget(m_progress);
    root->addLayout(foot);

    connect(folder, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Dossier des cas de validation"), m_directory);
        if (!d.isEmpty()) loadCases(d);
    });
    connect(all, &QPushButton::clicked, this, [this] { checkAll(true); });
    connect(none, &QPushButton::clicked, this, [this] { checkAll(false); });
    connect(refs, &QPushButton::clicked, this, &ValidationPage::importReferences);
    connect(m_run, &QPushButton::clicked, this, &ValidationPage::runChecked);
    connect(m_stop, &QPushButton::clicked, this, [this] {
        m_cancel = true;
        m_stop->setEnabled(false);
        m_summary->setText(tr("Arrêt demandé : le cas en cours se termine…"));
    });
    connect(m_export, &QPushButton::clicked, this, &ValidationPage::exportReport);
    connect(m_convergence, &QPushButton::clicked, this, &ValidationPage::exportConvergence);
    connect(m_tree, &QTreeWidget::currentItemChanged, this, &ValidationPage::showCaseDetails);
    connect(m_table, &QTableWidget::currentCellChanged, this, [this](int row) { showCheckDetails(row); });
}

void ValidationPage::loadCases(const QString& directory)
{
    m_directory = directory;
    m_cases = Research::loadCases(directory);
    m_tree->clear();
    std::map<QString, QTreeWidgetItem*> groups;
    int invalid = 0;
    for (int i = 0; i < static_cast<int>(m_cases.size()); ++i)
    {
        const auto& c = m_cases[std::size_t(i)];
        const QString category = c.benchmark.category.empty() ? QFileInfo(c.file).dir().dirName() : QString::fromStdString(c.benchmark.category);
        QTreeWidgetItem*& group = groups[category];
        if (!group)
        {
            group = new QTreeWidgetItem(m_tree, { category });
            group->setFirstColumnSpanned(true);
            group->setExpanded(true);
        }
        QStringList engines;
        for (const auto& e : c.benchmark.engines) engines << QString::fromStdString(e);
        auto* item = new QTreeWidgetItem(group, { QString::fromStdString(c.benchmark.id), QString::fromStdString(c.benchmark.title), engines.join(", ") });
        item->setData(0, kIdRole, i);
        if (c.valid())
        {
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(0, Qt::Unchecked);
        }
        else
        {
            ++invalid;
            item->setForeground(0, QColor(0xE0, 0x50, 0x50));
            item->setToolTip(0, tr("Cas invalide, non exécutable :\n%1").arg(c.errors.join('\n')));
        }
    }
    m_tree->resizeColumnToContents(0);
    m_summary->setText(tr("%1 cas dans %2%3").arg(m_cases.size()).arg(QDir::toNativeSeparators(directory),
                                                                       invalid ? tr(" — %1 invalide(s)").arg(invalid) : QString()));
}

void ValidationPage::checkAll(bool on)
{
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g)
        for (int k = 0; k < m_tree->topLevelItem(g)->childCount(); ++k)
        {
            QTreeWidgetItem* it = m_tree->topLevelItem(g)->child(k);
            if (it->flags() & Qt::ItemIsUserCheckable) it->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
        }
}

void ValidationPage::checkCases(const QStringList& ids)
{
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g)
        for (int k = 0; k < m_tree->topLevelItem(g)->childCount(); ++k)
        {
            QTreeWidgetItem* it = m_tree->topLevelItem(g)->child(k);
            if (it->flags() & Qt::ItemIsUserCheckable) it->setCheckState(0, ids.contains(it->text(0)) ? Qt::Checked : Qt::Unchecked);
        }
}

void ValidationPage::setEngineFilter(const QString& engineId)
{
    const int i = m_engine->findData(engineId);
    m_engine->setCurrentIndex(i >= 0 ? i : 0);
}

void ValidationPage::showCaseDetails()
{
    QTreeWidgetItem* it = m_tree->currentItem();
    if (!it || !it->data(0, kIdRole).isValid())
    {
        m_details->clear();
        return;
    }
    const auto& c = m_cases[std::size_t(it->data(0, kIdRole).toInt())];
    const auto& b = c.benchmark;
    QString html = QStringLiteral("<h3>%1 — %2</h3><p>%3</p>").arg(QString::fromStdString(b.id).toHtmlEscaped(), QString::fromStdString(b.title).toHtmlEscaped(),
                                                                  QString::fromStdString(b.description).toHtmlEscaped());
    auto list = [](const QString& title, const std::vector<std::string>& items) {
        if (items.empty()) return QString();
        QString s = QStringLiteral("<p><b>%1</b></p><ul>").arg(title);
        for (const auto& i : items) s += QStringLiteral("<li>%1</li>").arg(QString::fromStdString(i).toHtmlEscaped());
        return s + QStringLiteral("</ul>");
    };
    html += list(tr("Hypothèses"), b.hypotheses) + list(tr("Sources"), b.sources);
    if (!b.parameters.empty())
    {
        html += tr("<p><b>Paramètres</b></p><ul>");
        for (const auto& [k, v] : b.parameters) html += QStringLiteral("<li>%1 = %2</li>").arg(QString::fromStdString(k), num(v));
        html += QStringLiteral("</ul>");
    }
    html += tr("<p><b>Vérifications</b> : %1</p><ul>").arg(b.checks.size());
    for (const auto& k : b.checks)
        html += QStringLiteral("<li>%1 (%2) : %3</li>")
                    .arg(QString::fromStdString(k.label).toHtmlEscaped(), QString::fromStdString(k.quantity).toHtmlEscaped(),
                         QString::fromStdString(k.formula.empty() ? std::to_string(k.expected) : k.formula).toHtmlEscaped());
    html += QStringLiteral("</ul>");
    if (!c.valid()) html += tr("<p style='color:#e05050'><b>Cas invalide :</b><br>%1</p>").arg(c.errors.join("<br>").toHtmlEscaped());
    html += tr("<p><i>Fichier : %1</i></p>").arg(QDir::toNativeSeparators(c.file).toHtmlEscaped());
    m_details->setHtml(html);
}

void ValidationPage::importReferences()
{
    const QString f = QFileDialog::getOpenFileName(this, tr("Références externes"), m_directory, tr("Références (*.csv *.json)"));
    if (f.isEmpty()) return;
    std::vector<std::string> errors;
    m_external = TSA::Validation::loadExternalReferences(f, &errors);
    for (const auto& e : errors) emit message(tr("Références externes : %1").arg(QString::fromStdString(e)), QStringLiteral("WARN"));
    m_referencesLabel->setText(tr("%1 référence(s) externe(s) chargée(s) depuis %2").arg(m_external.size()).arg(QFileInfo(f).fileName()));
}

void ValidationPage::setRunning(bool running)
{
    m_run->setEnabled(!running);
    m_stop->setEnabled(running);
    m_export->setEnabled(!running && !m_report.cases.empty());
    m_progress->setVisible(running);
}

void ValidationPage::runChecked()
{
    if (m_thread) return;
    std::vector<TSA::Validation::BenchmarkCase> selected;
    for (int g = 0; g < m_tree->topLevelItemCount(); ++g)
        for (int k = 0; k < m_tree->topLevelItem(g)->childCount(); ++k)
        {
            QTreeWidgetItem* it = m_tree->topLevelItem(g)->child(k);
            if ((it->flags() & Qt::ItemIsUserCheckable) && it->checkState(0) == Qt::Checked)
                selected.push_back(m_cases[std::size_t(it->data(0, kIdRole).toInt())].benchmark);
        }
    if (selected.empty())
    {
        m_summary->setText(tr("Cochez au moins un cas valide."));
        return;
    }
    TSA::Validation::RunOptions options;
    if (const QString e = m_engine->currentData().toString(); !e.isEmpty()) options.engines = { e.toStdString() };
    options.external = m_external;
    m_cancel = false;
    m_progress->setRange(0, static_cast<int>(selected.size()));
    m_progress->setValue(0);
    setRunning(true);
    m_summary->setText(tr("Exécution de %1 cas…").arg(selected.size()));
    // Calcul hors du fil de l'interface : chaque cas construit son propre modèle (aucun objet de la fenêtre partagé)
    m_thread = QThread::create([this, selected, options] {
        auto report = Research::runValidation(
            selected, options,
            [this](const Research::ValidationProgress& p) {
                QMetaObject::invokeMethod(this, [this, p] {
                    m_progress->setValue(p.done);
                    if (!p.caseId.isEmpty()) m_summary->setText(tr("Cas %1 / %2 : %3").arg(p.done + 1).arg(p.total).arg(p.caseId));
                }, Qt::QueuedConnection);
            },
            [this] { return m_cancel.load(); });
        QMetaObject::invokeMethod(this, [this, report] {
            m_report = report;
            m_thread->wait();
            delete m_thread;
            m_thread = nullptr;
            setRunning(false);
            showReport();
            emit finished(m_report);
        }, Qt::QueuedConnection);
    });
    m_thread->start();
}

int ValidationPage::resultRowCount() const
{
    return m_table->rowCount();
}

void ValidationPage::showReport()
{
    m_table->setRowCount(0);
    int skipped = 0, notRun = 0;
    m_convergence->setEnabled(false);
    for (std::size_t ci = 0; ci < m_report.cases.size(); ++ci)
    {
        const auto& c = m_report.cases[ci];
        if (c.skipped) ++skipped;
        if (!c.ran)
        {
            ++notRun;
            const int r = m_table->rowCount();
            m_table->insertRow(r);
            m_table->setItem(r, 0, new QTableWidgetItem(QString::fromStdString(c.caseId)));
            m_table->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(c.engine)));
            auto* msg = new QTableWidgetItem(c.skipped ? tr("ignoré : %1").arg(QString::fromStdString(c.message)) : tr("calcul non abouti : %1").arg(QString::fromStdString(c.message)));
            m_table->setItem(r, 3, msg);
            m_table->setItem(r, 10, new QTableWidgetItem(c.skipped ? tr("IGNORÉ") : tr("ÉCHEC")));
            continue;
        }
        for (std::size_t ki = 0; ki < c.checks.size(); ++ki)
        {
            const auto& k = c.checks[ki];
            const int r = m_table->rowCount();
            m_table->insertRow(r);
            const QString tol = tr("%1 rel. + %2 abs.").arg(num(k.tolerance.rel), num(k.tolerance.abs));
            const QString origin = QString::fromStdString(k.source) + (k.interpolated ? tr(", interpolée") : QString()) +
                                   (k.convergence.size() > 1 ? tr(", convergence sur %1 maillages").arg(k.convergence.size()) : QString());
            const QStringList cells { QString::fromStdString(c.caseId), QString::fromStdString(c.engine), QString::fromStdString(k.quantity),
                                      QString::fromStdString(k.label), num(k.reference) + QStringLiteral(" ") + QString::fromStdString(k.unit),
                                      k.available ? num(k.value) : QStringLiteral("—"), k.available ? num(k.absError) : QStringLiteral("—"),
                                      Research::relativeErrorText(k), tol, origin, Research::checkStatusText(k) };
            for (int col = 0; col < cells.size(); ++col) m_table->setItem(r, col, new QTableWidgetItem(cells[col]));
            m_table->item(r, 0)->setData(kCaseRole, static_cast<int>(ci));
            m_table->item(r, 0)->setData(kCheckRole, static_cast<int>(ki));
            const QColor color = !k.available ? QColor(0xD0, 0xA0, 0x30) : k.passed ? QColor(0x3F, 0xB9, 0x50) : QColor(0xE0, 0x50, 0x50);
            m_table->item(r, 10)->setForeground(color);
            if (!k.note.empty()) m_table->item(r, 10)->setToolTip(QString::fromStdString(k.note));
        }
    }
    m_summary->setText(tr("%1 vérification(s) réussie(s), %2 en échec, %3 non disponible(s) ; %4 cas ignoré(s), %5 non abouti(s) — %6 ; moteurs : %7")
                           .arg(m_report.checksPassed())
                           .arg(m_report.checksFailed())
                           .arg(m_report.checksUnavailable())
                           .arg(skipped)
                           .arg(notRun - skipped)
                           .arg(QString::fromStdString(m_report.tsaVersion), QString::fromStdString(m_report.engineVersions)));
    m_export->setEnabled(!m_report.cases.empty());
}

const TSA::Validation::CheckOutcome* ValidationPage::checkAt(int row) const
{
    const QTableWidgetItem* it = row >= 0 && row < m_table->rowCount() ? m_table->item(row, 0) : nullptr;
    if (!it || !it->data(kCaseRole).isValid()) return nullptr;
    const auto ci = static_cast<std::size_t>(it->data(kCaseRole).toInt());
    const auto ki = static_cast<std::size_t>(it->data(kCheckRole).toInt());
    if (ci >= m_report.cases.size() || ki >= m_report.cases[ci].checks.size()) return nullptr;
    return &m_report.cases[ci].checks[ki];
}

QString ValidationPage::detailsHtml() const { return m_details->toHtml(); }

bool ValidationPage::showCheckDetails(int row)
{
    const auto* k = checkAt(row);
    m_convergence->setEnabled(k && k->convergence.size() > 1);
    if (!k) return false;
    const QString unit = QString::fromStdString(k->unit).toHtmlEscaped();
    QString html = tr("<h3>%1 — %2</h3><p>%3 · moteur %4</p>")
                       .arg(QString::fromStdString(k->caseId).toHtmlEscaped(), QString::fromStdString(k->quantity).toHtmlEscaped(),
                            QString::fromStdString(k->label).toHtmlEscaped(), QString::fromStdString(k->engine).toHtmlEscaped());
    html += tr("<p>Référence (%1) : <b>%2 %3</b>%4<br>Valeur TSA : <b>%5</b> — écart relatif %6 — %7</p>")
                .arg(QString::fromStdString(k->source).toHtmlEscaped(), num(k->reference), unit,
                     k->formula.empty() ? QString() : QStringLiteral(" = ") + QString::fromStdString(k->formula).toHtmlEscaped(),
                     k->available ? num(k->value) + QStringLiteral(" ") + unit : tr("non disponible"), Research::relativeErrorText(*k).toHtmlEscaped(),
                     Research::checkStatusText(*k));
    if (k->convergence.size() > 1)
    {
        const auto st = Research::analyseMeshConvergence(*k);
        html += tr("<p><b>Convergence en maillage</b> — la vérification porte sur le maillage le plus fin.</p>"
                   "<table border='1' cellspacing='0' cellpadding='3'><tr><th>h (m)</th><th>Maillage produit</th><th>Valeur</th>"
                   "<th>Écart à la réf.</th><th>Variation</th></tr>");
        for (const auto& r : st.rows)
            html += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3</td><td>%4</td><td>%5</td></tr>")
                        .arg(num(r.meshSize), r.mesh.toHtmlEscaped(), r.available ? num(r.value) : tr("non calculé"), pct(r.relErrorToReference), pct(r.relChange));
        html += QStringLiteral("</table>");
        if (!st.applicable)
            html += tr("<p>Étude non applicable : %1.</p>").arg(st.reason.toHtmlEscaped());
        else
        {
            html += tr("<p>Écart à la référence %1 d'un maillage au suivant.<br>Ordre de convergence observé : <b>%2</b> (%3).</p>")
                        .arg(st.errorDecreasing ? tr("décroissant") : tr("<b>non monotone</b>"),
                             std::isfinite(st.observedOrder) ? num(st.observedOrder) : tr("non calculé"), st.orderNote.toHtmlEscaped());
            if (std::isfinite(st.extrapolated))
                html += tr("<p>Extrapolation de Richardson (post-traitement de TSALab, pas une valeur du moteur) : %1 %2, écart à la référence %3. "
                           "Un écart extrapolé qui ne tend pas vers zéro relève de la modélisation (théorie du moteur ≠ théorie de la référence), "
                           "pas de la discrétisation.</p>")
                            .arg(num(st.extrapolated), unit, pct(st.extrapolatedRelError));
        }
    }
    else if (!k->note.empty())
        html += tr("<p><i>%1</i></p>").arg(QString::fromStdString(k->note).toHtmlEscaped());
    m_details->setHtml(html);
    return true;
}

void ValidationPage::exportConvergence()
{
    const auto* k = checkAt(m_table->currentRow());
    if (!k || k->convergence.size() < 2) return;
    const QString name = QStringLiteral("%1_%2_convergence.csv").arg(QString::fromStdString(k->caseId), QString::fromStdString(k->quantity).replace(':', '_'));
    const QString path = QFileDialog::getSaveFileName(this, tr("Exporter la convergence"), QDir::home().filePath(name), tr("CSV (*.csv)"));
    if (path.isEmpty()) return;
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(Research::meshConvergenceCsv(*k, Research::analyseMeshConvergence(*k)).toUtf8()) < 0 || !f.commit())
    {
        QMessageBox::warning(this, tr("Convergence"), tr("Écriture impossible : %1").arg(f.errorString()));
        return;
    }
    emit message(tr("Convergence exportée : %1").arg(QDir::toNativeSeparators(path)), QStringLiteral("INFO"));
}

void ValidationPage::exportReport()
{
    const QString d = QFileDialog::getExistingDirectory(this, tr("Dossier du rapport de validation"), QDir::homePath());
    if (d.isEmpty()) return;
    QString error;
    if (!TSA::Validation::writeReport(m_report, d, &error))
    {
        QMessageBox::warning(this, tr("Rapport de validation"), error);
        return;
    }
    emit message(tr("Rapport de validation écrit dans %1 (Markdown, CSV, JSON)").arg(QDir::toNativeSeparators(d)), QStringLiteral("INFO"));
}

} // namespace TSALab::UI
