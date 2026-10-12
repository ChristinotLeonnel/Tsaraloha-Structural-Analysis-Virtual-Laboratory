#include "MeshConvergence.h"

#include <QTextStream>

#include <cmath>
#include <limits>

namespace TSALab::Research
{

namespace
{
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
QString num(double v) { return std::isfinite(v) ? QString::number(v, 'g', 12) : QString(); }
} // namespace

MeshConvergenceStudy analyseMeshConvergence(const TSA::Validation::CheckOutcome& check)
{
    MeshConvergenceStudy s;
    s.reference = check.reference;
    s.refinementRatio = s.observedOrder = s.extrapolated = s.extrapolatedRelError = kNaN;
    const bool refDefined = std::abs(check.reference) > 1e-12;
    for (std::size_t i = 0; i < check.convergence.size(); ++i)
    {
        const auto& p = check.convergence[i];
        MeshConvergenceRow r;
        r.meshSize = p.meshSize, r.mesh = QString::fromStdString(p.meshInfo), r.available = p.available, r.value = p.value;
        r.relErrorToReference = p.available && refDefined ? std::abs(p.value - check.reference) / std::abs(check.reference) : kNaN;
        r.relChange = kNaN;
        if (i > 0 && p.available && check.convergence[i - 1].available && std::abs(p.value) > 1e-300)
            r.relChange = std::abs(p.value - check.convergence[i - 1].value) / std::abs(p.value);
        s.rows.push_back(r);
    }

    std::vector<const MeshConvergenceRow*> ok;
    for (const auto& r : s.rows)
        if (r.available) ok.push_back(&r);
    if (s.rows.size() < 2)
    {
        s.reason = s.rows.empty() ? QStringLiteral("aucune série de convergence (calcul sur un seul maillage)") : QStringLiteral("un seul maillage");
        s.orderNote = s.reason;
        return s;
    }
    if (ok.size() < 2)
    {
        s.reason = QStringLiteral("moins de deux maillages calculés");
        s.orderNote = s.reason;
        return s;
    }
    s.applicable = true;
    if (refDefined)
    {
        s.errorDecreasing = true;
        for (std::size_t i = 1; i < ok.size(); ++i)
            if (ok[i]->relErrorToReference > ok[i - 1]->relErrorToReference * (1.0 + 1e-12) + 1e-15) s.errorDecreasing = false;
    }

    // Ordre observé sur les trois derniers maillages calculés, consécutifs dans la série
    const std::size_t n = s.rows.size();
    if (n < 3)
    {
        s.orderNote = QStringLiteral("trois maillages au moins sont nécessaires");
        return s;
    }
    const auto& a = s.rows[n - 3];
    const auto& b = s.rows[n - 2];
    const auto& c = s.rows[n - 1];
    if (!a.available || !b.available || !c.available)
    {
        s.orderNote = QStringLiteral("un des trois maillages les plus fins n'a pas été calculé");
        return s;
    }
    if (!(a.meshSize > 0 && b.meshSize > 0 && c.meshSize > 0))
    {
        s.orderNote = QStringLiteral("taille de maille non définie");
        return s;
    }
    const double r1 = a.meshSize / b.meshSize, r2 = b.meshSize / c.meshSize;
    if (r1 <= 1.0 || std::abs(r1 - r2) > 1e-6 * r1)
    {
        s.orderNote = QStringLiteral("rapport de raffinement non constant ou non décroissant (%1 puis %2)").arg(r1, 0, 'g', 6).arg(r2, 0, 'g', 6);
        return s;
    }
    s.refinementRatio = r1;
    const double d1 = a.value - b.value, d2 = b.value - c.value;
    const double scale = std::max({ std::abs(a.value), std::abs(b.value), std::abs(c.value), 1e-300 });
    if (std::abs(d2) <= 1e-12 * scale)
    {
        s.orderNote = QStringLiteral("valeurs identiques sur les deux maillages les plus fins : ordre non défini");
        return s;
    }
    if (d1 / d2 <= 0.0)
    {
        s.orderNote = QStringLiteral("convergence oscillante (variations de signes opposés) : ordre non défini");
        return s;
    }
    s.observedOrder = std::log(d1 / d2) / std::log(r1);
    const double denom = std::pow(r1, s.observedOrder) - 1.0;
    if (std::abs(denom) > 1e-12)
    {
        s.extrapolated = c.value + (c.value - b.value) / denom;
        if (refDefined) s.extrapolatedRelError = std::abs(s.extrapolated - check.reference) / std::abs(check.reference);
    }
    s.orderNote = QStringLiteral("trois maillages les plus fins, rapport %1").arg(r1, 0, 'g', 6);
    return s;
}

QString meshConvergenceCsv(const TSA::Validation::CheckOutcome& check, const MeshConvergenceStudy& s)
{
    if (s.rows.empty()) return {};
    QString out;
    QTextStream o(&out);
    o << "# cas " << QString::fromStdString(check.caseId) << " | moteur " << QString::fromStdString(check.engine) << " | grandeur "
      << QString::fromStdString(check.quantity) << " | référence " << num(check.reference) << ' ' << QString::fromStdString(check.unit) << '\n';
    o << "# ordre observé : " << (std::isfinite(s.observedOrder) ? num(s.observedOrder) : QStringLiteral("non calculé")) << " (" << s.orderNote << ")";
    if (std::isfinite(s.extrapolated))
        o << " | extrapolation de Richardson (non calculée par le moteur) : " << num(s.extrapolated) << ", écart relatif à la référence " << num(s.extrapolatedRelError);
    o << '\n';
    o << "mesh_size,mesh,value,rel_error_to_reference,rel_change\n";
    for (const auto& r : s.rows)
        o << num(r.meshSize) << ",\"" << QString(r.mesh).replace('"', QStringLiteral("\"\"")) << "\"," << (r.available ? num(r.value) : QString()) << ','
          << num(r.relErrorToReference) << ',' << num(r.relChange) << '\n';
    return out;
}

} // namespace TSALab::Research
