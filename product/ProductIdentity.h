#pragma once

// Identité du produit TSALab (Tsaraloha Structural Analysis Laboratory).
//
// TSALab compile les sources communes de TSA (dépôt voisin, voir CMakeLists.txt) : tout ce qui le
// distingue de TSA passe par ce fichier (même API que TSA/product/ProductIdentity.h), jamais par une
// copie des sources. TSA et TSALab coexistent sur le poste : paramètres (QSettings), données
// utilisateur, extension .tsalab, ProgID, CLSID de l'extension Explorateur et AppUserModelID propres.
//
// C++ pur (aucune dépendance Qt) : inclus aussi par la DLL Explorateur.

#include <cstdint>

namespace TSA::Product
{

// --- Application -----------------------------------------------------------------------------
inline constexpr char kName[] = "TSALab";
inline constexpr char kLongName[] = "Tsaraloha Structural Analysis Laboratory";
inline constexpr char kKind[] = "laboratoire";
inline constexpr char kVersion[] = "0.1.0";
inline constexpr char kReportVersion[] = "0.1.0";
inline constexpr char kBadgeVersion[] = "v0.1";
inline constexpr char kMainWindowTitle[] = "TSALab - Structural Engineering Laboratory";
inline constexpr char kConsoleBanner[] = "TSALab — Structural Engineering Laboratory initialisé avec succès.";
inline constexpr char kStartCenterSubtitle[] = "Structural Engineering Research Laboratory";
inline constexpr char kAboutIntroHtml[] =
    "<p><b>TSALab</b> est le laboratoire d'ingénierie structurale de l'écosystème Tsaraloha : modéliser, "
    "expérimenter, inspecter, tester, valider et comprendre. Il est construit sur la base technique de TSA "
    "(logiciel de production) dont il partage le code : modélisation, viewport 3D et moteurs de calcul.</p>";
inline constexpr char kPlatformLabel[] = "Base technique commune avec TSA - Plateforme de Conception & Calcul de Structures 3D";
inline constexpr char kHttpUserAgent[] = "TSALab-Structural-Laboratory";
inline constexpr char kIfcOriginatingSystem[] = "TSALab - Tsaraloha Structural Analysis Laboratory";
inline constexpr char kPreviewBadge[] = "TSALab";
inline constexpr char kAiModelsSource[] = "TSALab";
inline constexpr wchar_t kAppUserModelId[] = L"Tsaraloha.TSALab.Laboratory.0.1";

// --- QSettings (organisation, application) : …/Tsaraloha/TSALab --------------------------------
inline constexpr char kOrganizationName[] = "Tsaraloha";
inline constexpr char kOrganizationDomain[] = "tsaraloha.lab";
inline constexpr char kSettingsApplication[] = "TSALab";
inline constexpr char kOpenSeesSettingsOrganization[] = "Tsaraloha";
inline constexpr char kOpenSeesSettingsApplication[] = "TSALab";
inline constexpr char kLayoutSettingsOrganization[] = "Tsaraloha";
inline constexpr char kLayoutSettingsApplication[] = "TSALab";

// --- Projets ---------------------------------------------------------------------------------
inline constexpr char kProjectExtension[] = ".tsalab";                   // format natif (signature 'TSLB')
inline constexpr char kLegacyExtension[] = ".tsa";                       // modèles TSA : ouverts en import, jamais réécrits
inline constexpr char kLegacyProductName[] = "TSA";
inline constexpr char kProgId[] = "TSALab.Project";
inline constexpr char kProjectFriendlyName[] = "TSALab Project";
inline constexpr char kProjectContentType[] = "application/x-tsalab-project";
inline constexpr char kDocumentsFolder[] = "TSALab";                     // Documents/TSALab
inline constexpr char kDefaultProjectName[] = "Projet TSALab";
inline constexpr char kDefaultAuthor[] = "TSALab User";

// Signatures (Little-Endian) : 'TSLB' = 0x424C5354 (TSALab), 'TSAF' = 0x46415354 (TSA, lu en import).
// Même conteneur à chunks : un .tsalab = un .tsa de même version + signature propre.
inline constexpr std::uint32_t kNativeFileMagic = 0x424C5354;
inline constexpr std::uint32_t kLegacyFileMagic = 0x46415354;

// --- Ressources (resources/lab.qrc) ------------------------------------------------------------
inline constexpr char kIconIco[] = ":/icons/TSALab.ico";
inline constexpr char kIconSvg[] = ":/icons/TSALab.svg";
inline constexpr char kIconSvgOnDark[] = ":/icons/TSALab.svg";
inline constexpr char kGlyphSvg[] = ":/icons/TSALab_glyph.svg";

// --- Extension Explorateur (DLL de miniatures) -------------------------------------------------
inline constexpr char kThumbnailProviderDll[] = "TSALabThumbnailProvider.dll";

} // namespace TSA::Product
