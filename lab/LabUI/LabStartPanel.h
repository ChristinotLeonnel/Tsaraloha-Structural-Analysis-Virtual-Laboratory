#pragma once

// Colonne « laboratoire » du Start Center de TSALab (point d'extension StartCenter::setLaunchPanel
// de la base commune) : identité, Nouveau modèle, Ouvrir, Exemples (TSALab::Research::Examples).

#include <QFrame>

namespace TSALab::UI
{

class LabStartPanel : public QFrame
{
    Q_OBJECT

public:
    explicit LabStartPanel(QWidget* parent = nullptr);

signals:
    void newModelRequested();
    void openRequested();
    /// Exemple du catalogue TSALab::Research::Examples (identifiant stable).
    void exampleRequested(const QString& exampleId);

private:
    void applyTheme(bool dark);
};

} // namespace TSALab::UI
