#pragma once

// TSALab Blueprint — tableau des résultats : les blocs « Visualiser » y déposent un objet (résultat, matrice, diagramme,
// courbe) ; la fenêtre de TSALab l'affiche (vue du portique, tableau, tracé). Utilisable depuis le thread d'exécution
// d'un graphe (tâche de fond) : le signal est livré dans le thread de la fenêtre.

#include "Automation/CommandRegistry.h"

#include <QObject>
#include <QString>

#include <mutex>
#include <vector>

namespace TSALab::Research::Structural
{

class ResultBoard : public QObject
{
    Q_OBJECT

public:
    struct Item
    {
        TSA::Automation::ObjectRef object;
        QString title;
        double scale = 0.0;   ///< amplification de la déformée demandée (0 : automatique)
    };

    static ResultBoard& instance();

    /// Dépose un objet (tout thread) ; émet published().
    void publish(const TSA::Automation::ObjectRef& object, const QString& title, double scale = 0.0);
    /// Objets déposés depuis le dernier appel (thread de la fenêtre).
    std::vector<Item> take();
    /// Dernier objet déposé (conservé après take()).
    Item last() const;

signals:
    void published();

private:
    ResultBoard() = default;
    mutable std::mutex m_mutex;
    std::vector<Item> m_pending;
    Item m_last;
};

} // namespace TSALab::Research::Structural
