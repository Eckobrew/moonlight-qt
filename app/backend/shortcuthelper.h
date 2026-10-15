#pragma once

#include <QString>
#include <QImage>

class ShortcutHelper
{
public:
    // Opens a native file dialog to choose an icon file (.icns, .png, .ico, .jpg, .jpeg)
    static QString chooseIconFile();

    // Creates a shortcut for the given computer and game app on Desktop
    // Returns empty string on success, or an error message on failure
    static QString createShortcut(const QString& computerName,
                                  const QString& computerUuid,
                                  const QString& appName,
                                  const QString& iconPath);

    // Applies custom icon and dock/taskbar identity to the running application
    static void applyShortcutIdentity(const QString& appName, const QString& iconPath);

    // Utility to generate a valid Apple .icns file from a QImage
    static bool writeIcnsFromImage(const QImage& sourceImage, const QString& outputPath);

    // Utility to generate a valid Windows .ico file from a QImage
    static bool writeIcoFromImage(const QImage& sourceImage, const QString& outputPath);
};

