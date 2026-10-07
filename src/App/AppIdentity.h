#pragma once

// Identité de TSALab (Tsaraloha Structural Analysis Laboratory).
//
// TSALab est issu d'une copie de TSA mais doit en être totalement indépendant à l'exécution :
// paramètres (QSettings), données utilisateur (%LOCALAPPDATA%), association de fichiers, ProgID,
// CLSID de l'extension Explorateur et AppUserModelID lui sont propres. Toute valeur d'identité
// passe par ce fichier : ne pas réintroduire de littéral « TSA » dans ces usages.
//
// Le code technique hérité conserve son espace de noms `TSA::` (base technique commune, voir
// .claude/decisions.md, ADR-L01) ; les modules propres au laboratoire utilisent `TSALab::`.

#include <QString>
#include <QStringList>

#ifndef TSALAB_SOURCE_DIR
#define TSALAB_SOURCE_DIR ""
#endif

namespace TSALab::Identity
{

// --- Application -----------------------------------------------------------------------------
inline constexpr char kProductName[] = "TSALab";
inline constexpr char kProductLongName[] = "Tsaraloha Structural Analysis Laboratory";
inline constexpr char kProductTagline[] = "Structural Engineering Research Laboratory";
inline constexpr char kVersion[] = "0.1.0";
inline constexpr char kOrganizationName[] = "Tsaraloha";            // QSettings / AppData : …/Tsaraloha/TSALab
inline constexpr char kOrganizationDomain[] = "tsaraloha.lab";
inline constexpr wchar_t kAppUserModelId[] = L"Tsaraloha.TSALab.Laboratory.0.1";

// --- Projets ---------------------------------------------------------------------------------
inline constexpr char kProjectExtension[] = ".tsalab";               // format natif (en-tête 'TSLB')
inline constexpr char kLegacyExtension[] = ".tsa";                   // modèles TSA : ouverture (import) seule
inline constexpr char kProgId[] = "TSALab.Project";
inline constexpr char kProjectFriendlyName[] = "TSALab Project";
inline constexpr char kDocumentsFolder[] = "TSALab";                  // Documents/TSALab

inline QString projectExtension() { return QString::fromLatin1(kProjectExtension); }

/// Fichier ouvrable par TSALab (.tsalab natif ou .tsa importé).
inline bool isOpenableProjectFile(const QString& path)
{
    return path.endsWith(QLatin1String(kProjectExtension), Qt::CaseInsensitive)
        || path.endsWith(QLatin1String(kLegacyExtension), Qt::CaseInsensitive);
}

/// Fichier au format natif TSALab.
inline bool isNativeProjectFile(const QString& path)
{
    return path.endsWith(QLatin1String(kProjectExtension), Qt::CaseInsensitive);
}

/// Ajoute l'extension native si absente (« Essai » → « Essai.tsalab », « Essai.tsa » → « Essai.tsalab »).
inline QString withProjectExtension(QString path)
{
    if (path.endsWith(QLatin1String(kLegacyExtension), Qt::CaseInsensitive))
        path.chop(int(sizeof(kLegacyExtension)) - 1);
    if (!isNativeProjectFile(path)) path += projectExtension();
    return path;
}

inline QString openFileFilter()
{
    return QStringLiteral("Projets TSALab (*.tsalab *.tsa);;Projets TSALab natifs (*.tsalab);;"
                          "Modèles TSA (*.tsa);;Tous les fichiers (*.*)");
}

inline QString saveFileFilter() { return QStringLiteral("Projet TSALab (*.tsalab)"); }

/// Racine des sources (définie par CMake) : ressources de développement (Extensions/, thirdparty/).
/// Vide hors d'un poste de développement.
inline QString sourceDirectory() { return QString::fromUtf8(TSALAB_SOURCE_DIR); }

} // namespace TSALab::Identity
