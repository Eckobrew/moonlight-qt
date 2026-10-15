#import <Cocoa/Cocoa.h>
#include "shortcuthelper.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QRegularExpression>

QString chooseIconFileMac()
{
    @autoreleasepool {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        [panel setTitle:@"Select Game Icon"];
        [panel setMessage:@"Choose an icon for the game shortcut (.icns, .png, .ico, .jpg)"];
        [panel setCanChooseFiles:YES];
        [panel setCanChooseDirectories:NO];
        [panel setAllowsMultipleSelection:NO];
        [panel setAllowedFileTypes:@[@"icns", @"png", @"ico", @"jpg", @"jpeg"]];

        if ([panel runModal] == NSModalResponseOK) {
            NSURL* url = [[panel URLs] firstObject];
            if (url) {
                return QString::fromNSString([url path]);
            }
        }
        return QString();
    }
}

void applyShortcutIdentityMac(const QString& appName, const QString& iconPath)
{
    @autoreleasepool {
        if (!iconPath.isEmpty() && QFile::exists(iconPath)) {
            NSImage* image = [[NSImage alloc] initWithContentsOfFile:iconPath.toNSString()];
            if (image) {
                [NSApp setApplicationIconImage:image];
            }
            QGuiApplication::setWindowIcon(QIcon(iconPath));
        }
        if (!appName.isEmpty()) {
            QGuiApplication::setApplicationDisplayName(appName);
        }
    }
}

QString createShortcutMac(const QString& computerName,
                          const QString& /*computerUuid*/,
                          const QString& appName,
                          const QString& iconPath)
{
    @autoreleasepool {
        if (appName.isEmpty()) {
            return "Application name is empty";
        }
        if (iconPath.isEmpty() || !QFile::exists(iconPath)) {
            return "Icon file does not exist";
        }

        QString safeName = appName;
        safeName.replace(QRegularExpression("[/:\\\\*?\"<>|]"), "_");

        QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        if (desktopPath.isEmpty()) {
            return "Could not determine Desktop path";
        }

        QString bundlePath = desktopPath + "/" + safeName + ".app";
        QDir dir;
        if (!dir.mkpath(bundlePath + "/Contents/MacOS") ||
            !dir.mkpath(bundlePath + "/Contents/Resources")) {
            return "Failed to create application bundle directory structure";
        }

        QString slug = safeName.toLower().replace(QRegularExpression("[^a-z0-9]"), "-");

        // Write Info.plist
        QString plistContent = QString(
            "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\">\n"
            "<dict>\n"
            "    <key>CFBundleDevelopmentRegion</key>\n"
            "    <string>en</string>\n"
            "    <key>CFBundleExecutable</key>\n"
            "    <string>launch</string>\n"
            "    <key>CFBundleIconFile</key>\n"
            "    <string>app.icns</string>\n"
            "    <key>CFBundleIdentifier</key>\n"
            "    <string>com.moonlight-stream.shortcut.%1</string>\n"
            "    <key>CFBundleInfoDictionaryVersion</key>\n"
            "    <string>6.0</string>\n"
            "    <key>CFBundleName</key>\n"
            "    <string>%2</string>\n"
            "    <key>CFBundleDisplayName</key>\n"
            "    <string>%2</string>\n"
            "    <key>CFBundlePackageType</key>\n"
            "    <string>APPL</string>\n"
            "    <key>CFBundleShortVersionString</key>\n"
            "    <string>1.0</string>\n"
            "    <key>LSMinimumSystemVersion</key>\n"
            "    <string>10.15</string>\n"
            "    <key>NSHighResolutionCapable</key>\n"
            "    <true/>\n"
            "</dict>\n"
            "</plist>\n"
        ).arg(slug, appName.toHtmlEscaped());

        QFile plistFile(bundlePath + "/Contents/Info.plist");
        if (!plistFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return "Failed to write Info.plist: " + plistFile.errorString();
        }
        plistFile.write(plistContent.toUtf8());
        plistFile.close();

        // Icon handling
        QString resPngPath = bundlePath + "/Contents/Resources/app.png";
        QString resIcnsPath = bundlePath + "/Contents/Resources/app.icns";

        QImage sourceImg(iconPath);
        if (sourceImg.isNull()) {
            return "Failed to load selected icon file";
        }

        sourceImg.save(resPngPath, "PNG");

        if (iconPath.endsWith(".icns", Qt::CaseInsensitive)) {
            QFile::remove(resIcnsPath);
            QFile::copy(iconPath, resIcnsPath);
        } else {
            ShortcutHelper::writeIcnsFromImage(sourceImg, resIcnsPath);
        }

        // Set Finder custom icon via NSWorkspace
        NSImage* nsImg = [[NSImage alloc] initWithContentsOfFile:iconPath.toNSString()];
        if (nsImg) {
            [[NSWorkspace sharedWorkspace] setIcon:nsImg forFile:bundlePath.toNSString() options:0];
        }

        // Write launch script
        QString moonlightBin = QCoreApplication::applicationFilePath();
        QString escapedComp = computerName;
        escapedComp.replace("\\", "\\\\").replace("\"", "\\\"").replace("$", "\\$").replace("`", "\\`");
        QString escapedApp = appName;
        escapedApp.replace("\\", "\\\\").replace("\"", "\\\"").replace("$", "\\$").replace("`", "\\`");

        QString launchScript = QString(
            "#!/bin/bash\n"
            "DIR=\"$(cd \"$(dirname \"$0\")\" && pwd)\"\n"
            "ICON=\"$DIR/../Resources/app.png\"\n"
            "if [ ! -f \"$ICON\" ]; then\n"
            "    ICON=\"$DIR/../Resources/app.icns\"\n"
            "fi\n"
            "\n"
            "MOONLIGHT_BIN=\"%1\"\n"
            "if [ ! -x \"$MOONLIGHT_BIN\" ]; then\n"
            "    MOONLIGHT_BIN=\"/Applications/Moonlight.app/Contents/MacOS/Moonlight\"\n"
            "fi\n"
            "\n"
            "if [ -x \"$MOONLIGHT_BIN\" ]; then\n"
            "    exec \"$MOONLIGHT_BIN\" stream \"%2\" \"%3\" --icon \"$ICON\"\n"
            "else\n"
            "    open -a Moonlight --args stream \"%2\" \"%3\" --icon \"$ICON\"\n"
            "fi\n"
        ).arg(moonlightBin, escapedComp, escapedApp);

        QString launchPath = bundlePath + "/Contents/MacOS/launch";
        QFile launchFile(launchPath);
        if (!launchFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return "Failed to write launch script: " + launchFile.errorString();
        }
        launchFile.write(launchScript.toUtf8());
        launchFile.close();

        // Make launch script executable
        QFile::setPermissions(launchPath,
            QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
            QFile::ReadGroup | QFile::ExeGroup |
            QFile::ReadOther | QFile::ExeOther);

        // Notify Finder of file change
        [[NSWorkspace sharedWorkspace] noteFileSystemChanged:bundlePath.toNSString()];

        return QString();
    }
}

