#include "ResultBoard.h"

namespace TSALab::Research::Structural
{

ResultBoard& ResultBoard::instance()
{
    static ResultBoard board;
    return board;
}

void ResultBoard::publish(const TSA::Automation::ObjectRef& object, const QString& title, double scale)
{
    if (!object) return;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.push_back({ object, title, scale });
        m_last = m_pending.back();
    }
    emit published();   // connexion automatique : livré dans le thread de la fenêtre (file d'événements)
}

std::vector<ResultBoard::Item> ResultBoard::take()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<Item> out;
    out.swap(m_pending);
    return out;
}

ResultBoard::Item ResultBoard::last() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_last;
}

} // namespace TSALab::Research::Structural
