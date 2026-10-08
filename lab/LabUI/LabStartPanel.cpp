#include "LabStartPanel.h"

#include "App/ProductInfo.h"
#include "Research/Examples/ExampleModels.h"
#include "UI/Theme/ThemeManager.h"

#include <QIcon>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace TSALab::UI
{

namespace
{
constexpr int kColumnWidth = 340;
}

LabStartPanel::LabStartPanel(QWidget* parent)
    : QFrame(parent)
{
    setObjectName("LabColumn");
    setFixedWidth(kColumnWidth);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(28, 32, 28, 20);
    lay->setSpacing(0);

    // Identité du laboratoire
    auto* logo = new QLabel(this);
    logo->setPixmap(QIcon(QString::fromLatin1(TSA::Product::kIconSvg)).pixmap(64, 64));
    lay->addWidget(logo);
    lay->addSpacing(12);
    auto* title = new QLabel(TSA::Product::name(), this);
    title->setObjectName("LabTitle");
    lay->addWidget(title);
    auto* subtitle = new QLabel(QString::fromUtf8(TSA::Product::kStartCenterSubtitle), this);
    subtitle->setObjectName("LabMuted");
    subtitle->setWordWrap(true);
    lay->addWidget(subtitle);
    lay->addSpacing(10);

    // Démarche du laboratoire (rappel, non interactif)
    auto* flow = new QLabel(QStringLiteral("MODEL → EXPERIMENT → INSPECT → TEST → VALIDATE → UNDERSTAND"), this);
    flow->setObjectName("LabFlow");
    flow->setWordWrap(true);
    flow->setToolTip(tr("TSA produit (modéliser → analyser → dimensionner → rédiger) ;\n"
                        "TSALab explore : modéliser, expérimenter, inspecter, tester, valider, comprendre."));
    lay->addWidget(flow);
    lay->addSpacing(24);

    // Actions principales
    auto* btnNew = new QPushButton(QIcon(":/icons/file_new.svg"), tr("Nouveau modèle structural"), this);
    btnNew->setObjectName("LabPrimary");
    btnNew->setToolTip(tr("Créer un projet %1 (Ctrl+N)").arg(TSA::Product::projectExtension()));
    auto* btnOpen = new QPushButton(QIcon(":/icons/file_open.svg"), tr("Ouvrir un projet…"), this);
    btnOpen->setObjectName("LabSecondary");
    btnOpen->setToolTip(tr("Ouvrir un projet %1 ou importer un modèle TSA (.tsa) (Ctrl+O)").arg(TSA::Product::projectExtension()));
    for (auto* b : { btnNew, btnOpen })
    {
        b->setMinimumHeight(40);
        b->setIconSize(QSize(18, 18));
        b->setCursor(Qt::PointingHandCursor);
        lay->addWidget(b);
        lay->addSpacing(8);
    }
    connect(btnNew, &QPushButton::clicked, this, &LabStartPanel::newModelRequested);
    connect(btnOpen, &QPushButton::clicked, this, &LabStartPanel::openRequested);
    lay->addSpacing(20);

    // Exemples : vrais modèles (TSALab::Research::Examples), générés puis ouverts
    auto* examplesTitle = new QLabel(tr("EXEMPLES"), this);
    examplesTitle->setObjectName("LabSection");
    lay->addWidget(examplesTitle);
    lay->addSpacing(6);
    for (const auto& ex : TSALab::Research::Examples::catalog())
    {
        auto* b = new QPushButton(QString::fromStdString(ex.title), this);
        b->setObjectName("LabExample");
        b->setCursor(Qt::PointingHandCursor);
        b->setToolTip(QStringLiteral("%1\n\nRéférence : %2").arg(QString::fromStdString(ex.description),
                                                                 QString::fromStdString(ex.reference)));
        b->setAccessibleName(QString::fromStdString(ex.title));
        const QString id = QString::fromStdString(ex.id);
        connect(b, &QPushButton::clicked, this, [this, id] { emit exampleRequested(id); });
        lay->addWidget(b);
    }
    lay->addStretch(1);

    auto* footer = new QLabel(tr("Base technique commune avec TSA · OpenCASCADE · OpenSees · Qt"), this);
    footer->setObjectName("LabMuted");
    footer->setWordWrap(true);
    lay->addWidget(footer);

    connect(&TSA::UI::ThemeManager::instance(), &TSA::UI::ThemeManager::themeChanged, this, &LabStartPanel::applyTheme);
    applyTheme(TSA::UI::ThemeManager::instance().isDarkMode());
}

void LabStartPanel::applyTheme(bool dark)
{
    // Accents du laboratoire : violet (actions) et turquoise (repères), distincts du bleu de TSA.
    const QString accent = dark ? "#7C4DFF" : "#6236E0";
    const QString accentHover = dark ? "#9470FF" : "#4E25C4";
    const QString teal = dark ? "#18F0D8" : "#0E9E8E";
    const QString muted = dark ? "#8B949E" : "#57606A";
    const QString border = dark ? "#30363D" : "#D0D7DE";
    const QString columnBg = dark ? "#161B22" : "#F3F1FA";
    setStyleSheet(QStringLiteral(
        "#LabColumn { background: %6; border-right: 1px solid %5; }"
        "#LabTitle { font-size: 32px; font-weight: 700; letter-spacing: 1px; }"
        "#LabMuted { font-size: 12px; color: %4; }"
        "#LabFlow { font-size: 10px; font-weight: 600; letter-spacing: 1px; color: %3; }"
        "#LabSection { font-size: 11px; font-weight: 700; letter-spacing: 2px; color: %4; }"
        "QPushButton#LabPrimary { background: %1; color: white; border: none; border-radius: 4px;"
        "   font-size: 13px; font-weight: 600; text-align: left; padding-left: 14px; }"
        "QPushButton#LabPrimary:hover { background: %2; }"
        "QPushButton#LabSecondary { background: transparent; border: 1px solid %5; border-radius: 4px;"
        "   font-size: 13px; font-weight: 600; text-align: left; padding-left: 14px; }"
        "QPushButton#LabSecondary:hover { border-color: %1; color: %1; }"
        "QPushButton#LabExample { background: transparent; border: none; border-left: 2px solid transparent;"
        "   text-align: left; padding: 6px 8px; font-size: 12px; }"
        "QPushButton#LabExample:hover { border-left: 2px solid %3; background: %5; }")
        .arg(accent, accentHover, teal, muted, border, columnBg));
}

} // namespace TSALab::UI
