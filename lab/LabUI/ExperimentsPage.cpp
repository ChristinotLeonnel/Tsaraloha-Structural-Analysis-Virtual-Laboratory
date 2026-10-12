#include "ExperimentsPage.h"

#include "Research/Reference/AnalyticalReference.h"

#include <QDesktopServices>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonDocument>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QTextBrowser>
#include <QThread>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>

namespace TSALab::UI
{

namespace
{
QString num(double v) { return QLocale(QLocale::French).toString(v, 'g', 10); }

/// Référence analytique de l'expérience : hypothèses, valeurs, écarts ; ou raisons de non-application.
QString referenceHtml(const QJsonObject& ref)
{
    if (ref.isEmpty()) return QString();
    const auto ar = Research::ReferenceComparison::fromJson(ref);
    QString h = QObject::tr("<p><b>Référence analytique</b> — %1</p>").arg(ar.title.toHtmlEscaped());
    if (!ar.applicable)
    {
        h += QObject::tr("<p>Non appliquée : hypothèse(s) non remplie(s)</p><ul>");
        for (const auto& v : ar.violations) h += QStringLiteral("<li>%1</li>").arg(v.toHtmlEscaped());
        return h + QStringLiteral("</ul>");
    }
    h += QObject::tr("<p>Hypothèses vérifiées :</p><ul>");
    for (const auto& v : ar.hypotheses) h += QStringLiteral("<li>%1</li>").arg(v.toHtmlEscaped());
    h += QObject::tr("</ul><table border='1' cellspacing='0' cellpadding='3'><tr><th>Grandeur</th><th>Formule</th><th>Référence</th><th>Calculée</th>"
                     "<th>Écart abs.</th><th>Écart rel.</th><th>Tolérance</th><th>État</th></tr>");
    for (const auto& c : ar.checks)
    {
        const QString rel = !c.available ? QStringLiteral("—") : std::isnan(c.relError) ? QStringLiteral("réf. ≈ 0") : num(c.relError);
        const QString state = !c.available ? QObject::tr("NON DISPONIBLE") : c.passed ? QObject::tr("RÉUSSI") : QObject::tr("ÉCHEC");
        h += QStringLiteral("<tr><td>%1</td><td>%2</td><td>%3 %4</td><td>%5</td><td>%6</td><td>%7</td><td>%8</td><td>%9</td></tr>")
                 .arg(c.label.toHtmlEscaped(), c.formula.toHtmlEscaped(), num(c.reference), c.unit, c.available ? num(c.computed) : QStringLiteral("—"),
                      c.available ? num(c.absError) : QStringLiteral("—"), rel, QStringLiteral("%1 rel. + %2 abs.").arg(num(c.tolRel), num(c.tolAbs)), state);
    }
    h += QStringLiteral("</table>");
    for (const auto& w : ar.warnings) h += QStringLiteral("<p>⚠ %1</p>").arg(w.toHtmlEscaped());
    return h;
}
} // namespace

ExperimentsPage::ExperimentsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 8, 8, 8);
    m_header = new QLabel(this);
    m_header->setWordWrap(true);
    root->addWidget(m_header);
    auto* bar = new QHBoxLayout;
    auto* refresh = new QPushButton(tr("Actualiser"), this);
    m_reproduce = new QPushButton(tr("Rejouer le calcul"), this);
    m_openFolder = new QPushButton(tr("Ouvrir le dossier"), this);
    auto* otherJournal = new QPushButton(tr("Ouvrir un journal…"), this);
    m_resume = new QPushButton(tr("Reprendre le modèle…"), this);
    otherJournal->setToolTip(tr("Consulter les expériences d'un autre projet (dossier <projet>.experiments) ; le projet ouvert n'est pas modifié"));
    m_resume->setToolTip(tr("Enregistre une copie du modèle figé de l'expérience et l'ouvre comme projet (la copie figée reste intacte)"));
    m_reproduce->setToolTip(tr("Recharge le modèle figé, relance le même calcul (mêmes réglages, moteurs de ce poste) et compare les résultats"));
    for (QWidget* w : std::initializer_list<QWidget*> { refresh, m_reproduce, m_openFolder, otherJournal, m_resume }) bar->addWidget(w);
    connect(otherJournal, &QPushButton::clicked, this, [this] {
        const QString d = QFileDialog::getExistingDirectory(this, tr("Journal d'expériences (dossier .experiments)"), m_directory);
        if (!d.isEmpty()) setDirectory(d);
    });
    connect(m_resume, &QPushButton::clicked, this, [this] {
        const auto* r = selected();
        if (!r || r->modelSnapshot.isEmpty()) return;
        const QString src = QDir(r->folder).filePath(r->modelSnapshot);
        const QString suffix = QFileInfo(src).suffix();
        const QString proposed = QDir(QFileInfo(m_directory).absolutePath()).filePath(QStringLiteral("Reprise-%1.%2").arg(r->id.left(8), suffix));
        const QString dst = QFileDialog::getSaveFileName(this, tr("Reprendre le modèle de l'expérience"), proposed, tr("Projets (*.%1)").arg(suffix));
        if (dst.isEmpty()) return;
        QFile::remove(dst);
        if (!QFile::copy(src, dst))
        {
            emit message(tr("Copie du modèle impossible vers %1").arg(QDir::toNativeSeparators(dst)), QStringLiteral("WARN"));
            return;
        }
        emit openModelRequested(dst);
    });
    bar->addStretch();
    root->addLayout(bar);

    auto* split = new QSplitter(Qt::Vertical, this);
    m_list = new QTableWidget(0, 5, split);
    m_list->setHorizontalHeaderLabels({ tr("Date"), tr("Type"), tr("Titre"), tr("Statut"), tr("Moteur") });
    m_list->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_list->horizontalHeader()->setStretchLastSection(true);
    m_list->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_list->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    auto* bottom = new QSplitter(Qt::Horizontal, split);
    m_details = new QTextBrowser(bottom);
    auto* right = new QWidget(bottom);
    auto* rl = new QVBoxLayout(right);
    rl->setContentsMargins(0, 0, 0, 0);
    m_verdict = new QLabel(tr("Sélectionnez un calcul puis « Rejouer le calcul »."), right);
    m_verdict->setWordWrap(true);
    m_comparison = new QTableWidget(0, 6, right);
    m_comparison->setHorizontalHeaderLabels({ tr("Grandeur"), tr("Enregistrée"), tr("Rejouée"), tr("Écart abs."), tr("Écart rel."), tr("Identique") });
    m_comparison->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_comparison->setEditTriggers(QAbstractItemView::NoEditTriggers);
    rl->addWidget(m_verdict);
    rl->addWidget(m_comparison, 1);
    root->addWidget(split, 1);

    connect(refresh, &QPushButton::clicked, this, &ExperimentsPage::refresh);
    connect(m_list, &QTableWidget::itemSelectionChanged, this, &ExperimentsPage::showDetails);
    connect(m_reproduce, &QPushButton::clicked, this, &ExperimentsPage::reproduceSelected);
    connect(m_openFolder, &QPushButton::clicked, this, [this] {
        const auto* r = selected();
        QDesktopServices::openUrl(QUrl::fromLocalFile(r ? r->folder : m_directory));
    });
    showDetails();
}

ExperimentsPage::~ExperimentsPage()
{
    if (m_thread)
    {
        m_thread->wait();
        delete m_thread;
    }
}

void ExperimentsPage::setDirectory(const QString& directory)
{
    m_directory = directory;
    refresh();
}

void ExperimentsPage::refresh()
{
    m_records = Research::ExperimentJournal(m_directory).records();
    m_header->setText(tr("<b>Journal d'expériences</b> — %1 enregistrement(s) dans %2. Chaque calcul et chaque exécution du banc de validation y est "
                         "enregistré : versions, moteur, modèle figé et son empreinte, hypothèses, unités, statut, avertissements, résultats et provenance.")
                          .arg(m_records.size())
                          .arg(QDir::toNativeSeparators(m_directory)));
    m_list->setRowCount(0);
    for (int i = static_cast<int>(m_records.size()) - 1; i >= 0; --i)   // plus récent en premier
    {
        const auto& r = m_records[std::size_t(i)];
        const int row = m_list->rowCount();
        m_list->insertRow(row);
        const QStringList cells { r.date.left(19).replace('T', ' '), r.kind == "analysis" ? tr("calcul") : tr("validation"), r.title, r.status,
                                  r.engineName + (r.engineVersion.isEmpty() || r.engineName.contains(r.engineVersion) ? QString() : QStringLiteral(" ") + r.engineVersion) };
        for (int c = 0; c < cells.size(); ++c)
        {
            auto* item = new QTableWidgetItem(cells[c]);
            item->setData(Qt::UserRole, i);
            m_list->setItem(row, c, item);
        }
        m_list->item(row, 3)->setForeground(r.status == QLatin1String("réussi") ? QColor(0x3F, 0xB9, 0x50) : QColor(0xE0, 0x50, 0x50));
    }
    showDetails();
}

const Research::ExperimentRecord* ExperimentsPage::selected() const
{
    const auto items = m_list->selectedItems();
    if (items.isEmpty()) return nullptr;
    const int i = items.first()->data(Qt::UserRole).toInt();
    return i >= 0 && i < static_cast<int>(m_records.size()) ? &m_records[std::size_t(i)] : nullptr;
}

void ExperimentsPage::showDetails()
{
    const auto* r = selected();
    m_reproduce->setEnabled(r && r->kind == QLatin1String("analysis") && !r->modelSnapshot.isEmpty() && !m_thread);
    m_resume->setEnabled(r && !r->modelSnapshot.isEmpty());
    if (!r)
    {
        m_details->setHtml(tr("<p>Aucune expérience sélectionnée.</p>"));
        return;
    }
    auto list = [](const QStringList& l) {
        if (l.isEmpty()) return QObject::tr("aucun");
        QString s = QStringLiteral("<ul>");
        for (const auto& x : l) s += QStringLiteral("<li>%1</li>").arg(x.toHtmlEscaped());
        return s + QStringLiteral("</ul>");
    };
    QString results;
    for (auto it = r->results.begin(); it != r->results.end(); ++it)
        if (it.value().isDouble()) results += QStringLiteral("<tr><td>%1</td><td align='right'>%2</td></tr>").arg(it.key(), num(it.value().toDouble()));
    QString units;
    for (auto it = r->units.begin(); it != r->units.end(); ++it) units += QStringLiteral("%1 : %2 ; ").arg(it.key(), it.value().toString());
    m_details->setHtml(tr("<h3>%1</h3>"
                          "<p><b>Date</b> : %2<br><b>Statut</b> : %3<br><b>Laboratoire</b> : %4<br><b>Moteur</b> : %5 %6<br>"
                          "<b>Projet</b> : %7<br><b>Modèle figé</b> : %8 (SHA-256 %9)<br><b>Unités</b> : %10<br><b>Provenance</b> : %11</p>"
                          "<p><b>Avertissements</b> : %12</p><p><b>Erreurs</b> : %13</p>"
                          "<p><b>Résultats</b></p><table>%14</table>"
                          "%16"
                          "<p><b>Hypothèses et paramètres du calcul</b></p><pre>%15</pre>")
                           .arg(r->title.toHtmlEscaped(), r->date, r->status, r->labVersion, r->engineName.toHtmlEscaped(),
                                r->engineVersion.isEmpty() && r->kind == "analysis" ? tr("(version non lue)") : r->engineVersion,
                                r->projectFile.isEmpty() ? tr("modèle non enregistré") : QDir::toNativeSeparators(r->projectFile),
                                r->modelSnapshot.isEmpty() ? tr("aucun") : r->modelSnapshot, r->modelHash.left(16) + QStringLiteral("…"))
                           .arg(units.isEmpty() ? tr("—") : units, r->provenance.toHtmlEscaped(), list(r->warnings), list(r->errors),
                                results.isEmpty() ? tr("<tr><td>aucun résultat</td></tr>") : results,
                                QString::fromUtf8(QJsonDocument(r->analysisContext).toJson(QJsonDocument::Indented)).toHtmlEscaped(), referenceHtml(r->reference)));
}

void ExperimentsPage::reproduceSelected()
{
    const auto* r = selected();
    if (!r || m_thread) return;
    const Research::ExperimentRecord record = *r;
    m_reproduce->setEnabled(false);
    m_verdict->setText(tr("Rejeu de « %1 »…").arg(record.title));
    m_comparison->setRowCount(0);
    m_thread = QThread::create([this, record] {
        const auto result = Research::ExperimentJournal::reproduce(record);
        QMetaObject::invokeMethod(this, [this, result, record] {
            m_thread->wait();
            delete m_thread;
            m_thread = nullptr;
            for (const auto& c : result.comparisons)
            {
                const int row = m_comparison->rowCount();
                m_comparison->insertRow(row);
                const QStringList cells { c.quantity, c.present ? num(c.original) : QStringLiteral("—"), c.present ? num(c.reproduced) : QStringLiteral("—"),
                                          num(c.absError), std::isnan(c.relError) ? QStringLiteral("réf. ≈ 0") : num(c.relError),
                                          !c.present ? tr("absente") : c.equal ? tr("oui") : tr("NON") };
                for (int col = 0; col < cells.size(); ++col) m_comparison->setItem(row, col, new QTableWidgetItem(cells[col]));
            }
            const QString verdict = !result.ran ? tr("Rejeu impossible : %1").arg(result.message)
                                    : result.identical() ? tr("Rejeu identique : %1 grandeur(s) retrouvée(s) à l'identique.").arg(result.comparisons.size())
                                                         : tr("Rejeu DIFFÉRENT : au moins une grandeur s'écarte des résultats enregistrés.");
            m_verdict->setText(verdict + (result.ran && !result.message.isEmpty() ? QStringLiteral(" (") + result.message + QStringLiteral(")") : QString()));
            emit message(tr("Expérience « %1 » : %2").arg(record.title, verdict), result.identical() ? QStringLiteral("INFO") : QStringLiteral("WARN"));
            showDetails();
        }, Qt::QueuedConnection);
    });
    m_thread->start();
}

} // namespace TSALab::UI
