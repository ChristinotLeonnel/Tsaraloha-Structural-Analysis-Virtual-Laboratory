#include "TsaImportDialog.h"

#include "Research/Examples/StructuralGraphs.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPoint>
#include <QPushButton>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <functional>

namespace TSALab::UI
{

namespace RS = TSALab::Research::Structural;
namespace S = tsalab::structural;

namespace
{
/// Travail en tâche de fond, boucle d'événements locale (interface réactive, Annuler utilisable).
void runInBackground(const std::function<void()>& work)
{
    QEventLoop loop;
    QThread* worker = QThread::create(work);
    QObject::connect(worker, &QThread::finished, &loop, &QEventLoop::quit);
    worker->start();
    loop.exec();
    worker->wait();
    delete worker;
}

QString plural(std::size_t n, const char* one, const char* many)
{
    return QStringLiteral("%1 %2").arg(n).arg(QString::fromUtf8(n > 1 ? many : one));
}
} // namespace

TsaImportDialog::TsaImportDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Importer un portique d'un projet TSA"));
    auto* root = new QVBoxLayout(this);
    auto* intro = new QLabel(tr("Le projet est lu par TSA lui-même (lecture seule ; empreinte SHA-256 contrôlée avant et après) et converti en "
                                "portique plan par le chemin d'analyse 2D de TSA. L'aperçu détaille ce qui est importé, converti ou ignoré ; "
                                "rien n'est converti en silence."),
                             this);
    intro->setWordWrap(true);
    root->addWidget(intro);

    auto* form = new QFormLayout();
    auto* fileRow = new QHBoxLayout();
    m_path = new QLineEdit(this);
    m_path->setAccessibleName(tr("Fichier .tsa"));
    m_path->setPlaceholderText(tr("Projet TSA (.tsa)"));
    auto* browseButton = new QPushButton(tr("Parcourir…"), this);
    fileRow->addWidget(m_path, 1);
    fileRow->addWidget(browseButton);
    form->addRow(tr("Fichier"), fileRow);
    m_scope = new QComboBox(this);
    m_scope->setAccessibleName(tr("Portée plane"));
    m_scope->setToolTip(tr("Plan du modèle (détecté), axe de grille, niveau ou plan de travail : portées planes proposées par le projet"));
    form->addRow(tr("Portée plane"), m_scope);
    m_case = new QComboBox(this);
    m_case->setAccessibleName(tr("Cas ou combinaison"));
    m_case->setEnabled(false);
    form->addRow(tr("Calcul"), m_case);
    root->addLayout(form);

    auto* actions = new QHBoxLayout();
    m_previewButton = new QPushButton(tr("Aperçu"), this);
    m_previewButton->setToolTip(tr("Importer en tâche de fond et afficher le rapport (le projet n'est pas modifié)"));
    m_cancelButton = new QPushButton(tr("Annuler l'aperçu"), this);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    actions->addWidget(m_previewButton);
    actions->addWidget(m_cancelButton);
    actions->addWidget(m_status, 1);
    root->addLayout(actions);
    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    root->addWidget(m_summary);
    m_reportView = new QPlainTextEdit(this);
    m_reportView->setAccessibleName(tr("Rapport d'import"));
    m_reportView->setReadOnly(true);
    m_reportView->setPlaceholderText(tr("Choisir un fichier .tsa puis « Aperçu »"));
    root->addWidget(m_reportView, 1);

    auto* box = new QDialogButtonBox(this);
    m_createButton = box->addButton(tr("Créer le graphe d'analyse"), QDialogButtonBox::AcceptRole);
    m_createButton->setToolTip(tr("Ouvrir dans l'éditeur Blueprint un graphe : import, contrôle des données, calcul direct, visualisation"));
    box->addButton(tr("Fermer"), QDialogButtonBox::RejectRole);
    root->addWidget(box);

    connect(browseButton, &QPushButton::clicked, this, &TsaImportDialog::browse);
    // Différé : editingFinished est émis pendant un changement de focus (clic sur « Aperçu ») ; une boucle d'événements
    // locale lancée à ce moment avalerait le relâchement du bouton, et le clic serait perdu
    connect(m_path, &QLineEdit::editingFinished, this, [this] { QTimer::singleShot(0, this, &TsaImportDialog::loadScopes); });
    connect(m_path, &QLineEdit::textChanged, this, [this] {
        m_ok = false;
        updateButtons();
    });
    connect(m_scope, &QComboBox::currentIndexChanged, this, [this] {
        m_ok = m_ok && m_scope->currentText() == m_previewScope;
        updateButtons();
    });
    connect(m_previewButton, &QPushButton::clicked, this, [this] { preview(); });
    connect(m_cancelButton, &QPushButton::clicked, this, [this] { m_cancel = true; });
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &TsaImportDialog::reject);
    resize(820, 620);
    updateButtons();
}

void TsaImportDialog::setFile(const QString& path)
{
    m_path->setText(path);
    loadScopes();
}

void TsaImportDialog::browse()
{
    const QString start = m_path->text().isEmpty() ? QString() : QFileInfo(m_path->text()).absolutePath();
    const QString path = QFileDialog::getOpenFileName(this, tr("Projet TSA à importer"), start, tr("Projet TSA (*.tsa)"));
    if (!path.isEmpty()) setFile(path);
}

void TsaImportDialog::loadScopes()
{
    const QString path = m_path->text().trimmed();
    if (m_running || path.isEmpty() || path == m_scopesPath) return;
    std::vector<std::string> scopes;
    std::string error;
    bool ok = false;
    setRunning(true);
    m_status->setText(tr("Lecture des portées du projet…"));
    runInBackground([&] { ok = RS::listTsaScopes(path.toStdString(), scopes, &error); });
    setRunning(false);
    m_scope->clear();
    if (!ok)
    {
        m_scopesPath.clear();
        m_status->setText(tr("<span style='color:#C62828'>Projet illisible : %1</span>").arg(QString::fromStdString(error).toHtmlEscaped()));
        m_previewPending = false;
        updateButtons();
        if (m_closeRequested) QDialog::reject();
        return;
    }
    for (const auto& s : scopes) m_scope->addItem(QString::fromStdString(s));
    m_scopesPath = path;
    m_status->setText(tr("%1 proposée(s) par le projet ; choisir la portée, puis « Aperçu ».").arg(plural(scopes.size(), "portée plane", "portées planes")));
    updateButtons();
    if (m_closeRequested)
        QDialog::reject();
    else if (m_previewPending)   // « Aperçu » cliqué pendant la lecture des portées (fin de saisie du chemin)
        QTimer::singleShot(0, this, [this] { preview(); });
    m_previewPending = false;
}

bool TsaImportDialog::preview()
{
    if (m_running)
    {
        m_previewPending = true;
        return false;
    }
    const QString path = m_path->text().trimmed();
    if (path.isEmpty())
    {
        m_status->setText(tr("Choisir un fichier .tsa."));
        return false;
    }
    if (path != m_scopesPath) loadScopes();
    if (m_closeRequested || path != m_scopesPath) return false;
    const QString scope = m_scope->currentText();
    S::Model2D model;
    RS::TsaImportReport report;
    bool ok = false;
    m_cancel = false;
    setRunning(true);
    m_status->setText(tr("Import en cours (lecture du projet, extraction de la portée, conversion)…"));
    runInBackground([&] { ok = RS::importTsaProject(path.toStdString(), { scope.toStdString() }, model, report, &m_cancel); });
    setRunning(false);
    m_report = report;
    m_reportView->setPlainText(QString::fromStdString(report.text()));
    m_case->clear();
    m_ok = ok;
    if (ok)
    {
        m_previewPath = path;
        m_previewScope = scope;
        for (const auto& c : model.loadCases)
            m_case->addItem(tr("Cas %1 — %2").arg(c.id).arg(QString::fromStdString(c.name)), QPoint(c.id, 0));
        for (const auto& c : model.combinations)
            m_case->addItem(tr("Combinaison %1 — %2").arg(c.id).arg(QString::fromStdString(c.name)),
                            QPoint(model.loadCases.empty() ? 1 : model.loadCases.front().id, c.id));
        m_summary->setText(tr("%1, %2 (dont %3), %4 (dont %5), %6 ; %7, %8 ; %9, %10.")
                               .arg(plural(report.nodes, "nœud", "nœuds"), plural(report.elements, "élément", "éléments"),
                                    plural(report.trussBars, "barre de treillis", "barres de treillis"), plural(report.supports, "appui", "appuis"),
                                    plural(report.springs, "ressort", "ressorts"), plural(report.releases, "rotule", "rotules"),
                                    plural(report.nodalLoads, "charge nodale", "charges nodales"),
                                    plural(report.memberLoads, "charge sur barre", "charges sur barres"))
                               .arg(plural(report.loadCases, "cas de charge", "cas de charge"), plural(report.combinations, "combinaison", "combinaisons")));
        m_status->setText(m_case->count() > 0 ? tr("Aperçu réussi : choisir le cas ou la combinaison, puis « Créer le graphe d'analyse ».")
                                              : tr("Aperçu réussi, mais le projet n'a aucun cas de charge dans cette portée."));
        if (m_case->count() == 0) m_ok = false;
    }
    else
    {
        m_summary->clear();
        m_status->setText(tr("<span style='color:#C62828'>%1</span>").arg(QString::fromStdString(report.message).toHtmlEscaped()));
    }
    updateButtons();
    if (m_closeRequested) QDialog::reject();
    return ok;
}

bool TsaImportDialog::analysisGraph(TSA::Blueprint::Graph& graph, QString* error) const
{
    if (!m_ok || m_case->currentIndex() < 0)
    {
        if (error) *error = tr("aucun aperçu réussi : choisir un fichier et une portée, puis « Aperçu »");
        return false;
    }
    const QPoint selection = m_case->currentData().toPoint();
    std::string why;
    if (!TSALab::Research::StructuralGraphs::buildTsaImport(m_previewPath.toStdString(), m_previewScope.toStdString(), selection.x(), selection.y(), graph,
                                                           &why))
    {
        if (error) *error = QString::fromStdString(why);
        return false;
    }
    return true;
}

void TsaImportDialog::reject()
{
    if (m_running)
    {
        // Fermeture pendant l'import : il est interrompu à l'étape suivante, la fenêtre se ferme ensuite
        m_cancel = true;
        m_closeRequested = true;
        return;
    }
    QDialog::reject();
}

void TsaImportDialog::setRunning(bool running)
{
    m_running = running;
    if (running)
        QApplication::setOverrideCursor(Qt::BusyCursor);
    else
        QApplication::restoreOverrideCursor();
    updateButtons();
}

void TsaImportDialog::updateButtons()
{
    m_previewButton->setEnabled(!m_running && !m_path->text().trimmed().isEmpty());
    m_cancelButton->setEnabled(m_running);
    m_path->setEnabled(!m_running);
    m_scope->setEnabled(!m_running && m_scope->count() > 0);
    m_case->setEnabled(!m_running && m_ok);
    m_createButton->setEnabled(!m_running && m_ok);
}

} // namespace TSALab::UI
