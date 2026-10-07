#pragma once

#include <QString>

namespace TSA::Platform
{

/**
 * @brief Gestionnaire de l'association de fichiers .tsalab avec le système Windows
 *
 * Enregistre .tsalab dans le registre Windows (HKCU/Software/Classes) :
 * - Extension .tsalab associée au ProgID "TSALab.Project" (jamais .tsa, qui appartient à TSA)
 * - Description "TSALab Project"
 * - Icône par défaut (DefaultIcon) pointant vers l'exécutable TSALab.exe
 * - Commande d'ouverture (shell/open/command) avec passage d'argument "%1"
 * - Rafraîchissement automatique du Shell Windows (SHChangeNotify)
 */
class WindowsAssociation
{
public:
    /**
     * @brief Enregistre l'extension .tsalab pour l'exécutable TSALab actuel
     * @param executablePath Chemin absolu vers TSALab.exe (détecté automatiquement si vide)
     * @return true si l'enregistrement a réussi
     */
    static bool registerFileAssociation(const QString& executablePath = QString());

    /**
     * @brief Supprime l'association .tsalab du registre pour l'utilisateur actuel
     */
    static bool unregisterFileAssociation();

    /**
     * @brief Vérifie si .tsalab est déjà associé à TSALab
     */
    static bool isFileAssociationRegistered();

    /**
     * @brief Enregistre / retire l'extension Explorateur TSALabThumbnailProvider.dll (HKCU) placée
     *        à côté de TSALab.exe : miniature du dernier état du modèle pour les fichiers .tsalab.
     */
    static bool registerThumbnailProvider();
    static bool unregisterThumbnailProvider();

    /**
     * @brief Signale à l'Explorateur qu'un fichier a changé (invalidation de sa miniature en cache).
     */
    static void notifyFileUpdated(const QString& filePath);
};

} // namespace TSA::Platform
