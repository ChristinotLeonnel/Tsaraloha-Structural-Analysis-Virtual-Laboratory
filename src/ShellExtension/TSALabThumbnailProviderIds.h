#pragma once

// Identifiants de l'extension Explorateur de TSALab, partagés par la DLL et ses tests.
// Propres à TSALab : TSA conserve les siens ({5BA6698A-…}, .tsa, TSA.Project), les deux logiciels
// coexistant sur le poste.

#include <guiddef.h>

namespace TSALab::ShellExtension
{

// {B796AF69-BD0F-42F2-981B-30C203EE9791}
inline constexpr CLSID kThumbnailProviderClsid = { 0xb796af69, 0xbd0f, 0x42f2, { 0x98, 0x1b, 0x30, 0xc2, 0x03, 0xee, 0x97, 0x91 } };
inline constexpr wchar_t kThumbnailProviderClsidString[] = L"{B796AF69-BD0F-42F2-981B-30C203EE9791}";
inline constexpr wchar_t kExtension[] = L".tsalab";
inline constexpr wchar_t kProgId[] = L"TSALab.Project";

} // namespace TSALab::ShellExtension
