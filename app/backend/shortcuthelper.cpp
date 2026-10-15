#include "shortcuthelper.h"

#include <QFile>
#include <QDir>
#include <QBuffer>
#include <QDataStream>
#include <QtEndian>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QRegularExpression>
#include <QProcess>

#if defined(Q_OS_DARWIN)
QString chooseIconFileMac();
QString createShortcutMac(const QString& computerName,
                          const QString& computerUuid,
                          const QString& appName,
                          const QString& iconPath);
void applyShortcutIdentityMac(const QString& appName, const QString& iconPath);
#elif defined(Q_OS_WIN)
#include <windows.h>
#include <shlobj.h>
#include <propvarutil.h>
#include <propkey.h>

#if defined(_MSC_VER)
#pragma comment(lib, "comdlg32.lib")
#pragma comment(lib, "propsys.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#endif

static QString chooseIconFileWin()
{
    wchar_t filename[MAX_PATH] = {0};
    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.lpstrFilter = L"Icon Files (*.ico;*.png;*.jpg;*.jpeg)\0*.ico;*.png;*.jpg;*.jpeg\0All Files (*.*)\0*.*\0";
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = L"Select Game Icon";

    if (GetOpenFileNameW(&ofn)) {
        return QString::fromWCharArray(filename);
    }
    return QString();
}

static void applyShortcutIdentityWin(const QString& appName, const QString& iconPath)
{
    if (!appName.isEmpty()) {
        QString slug = QString("Moonlight.Game.%1").arg(appName.toLower().replace(QRegularExpression("[^a-z0-9]"), "-"));
        SetCurrentProcessExplicitAppUserModelID(slug.toStdWString().c_str());
        QGuiApplication::setApplicationDisplayName(appName);
    }
    if (!iconPath.isEmpty() && QFile::exists(iconPath)) {
        QGuiApplication::setWindowIcon(QIcon(iconPath));
    }
}

static QString createShortcutWin(const QString& computerName,
                                 const QString& /*computerUuid*/,
                                 const QString& appName,
                                 const QString& iconPath)
{
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

    QString appDataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appDataDir + "/shortcuts");
    QString targetIcoPath = appDataDir + "/shortcuts/" + safeName + ".ico";

    QImage srcImg(iconPath);
    if (!srcImg.isNull()) {
        if (iconPath.endsWith(".ico", Qt::CaseInsensitive)) {
            QFile::remove(targetIcoPath);
            QFile::copy(iconPath, targetIcoPath);
        } else {
            ShortcutHelper::writeIcoFromImage(srcImg, targetIcoPath);
        }
    } else {
        return "Failed to load image file";
    }

    CoInitialize(NULL);
    IShellLinkW* psl = NULL;
    HRESULT hr = CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (LPVOID*)&psl);
    if (SUCCEEDED(hr)) {
        QString moonlightExe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
        psl->SetPath(moonlightExe.toStdWString().c_str());

        QString args = QString("stream \"%1\" \"%2\" --icon \"%3\"")
                           .arg(computerName, appName, QDir::toNativeSeparators(targetIcoPath));
        psl->SetArguments(args.toStdWString().c_str());
        psl->SetIconLocation(QDir::toNativeSeparators(targetIcoPath).toStdWString().c_str(), 0);

        // Set AppUserModelID for taskbar pinning / grouping
        IPropertyStore* pps = NULL;
        if (SUCCEEDED(psl->QueryInterface(IID_IPropertyStore, (void**)&pps))) {
            PROPVARIANT pv;
            QString appId = QString("Moonlight.Game.%1").arg(safeName.toLower().replace(QRegularExpression("[^a-z0-9]"), "-"));
            if (SUCCEEDED(InitPropVariantFromString(appId.toStdWString().c_str(), &pv))) {
                pps->SetValue(PKEY_AppUserModel_ID, pv);
                PropVariantClear(&pv);
            }
            pps->Commit();
            pps->Release();
        }

        IPersistFile* ppf = NULL;
        hr = psl->QueryInterface(IID_IPersistFile, (LPVOID*)&ppf);
        if (SUCCEEDED(hr)) {
            QString lnkPath = QDir::toNativeSeparators(desktopPath + "/" + safeName + ".lnk");
            hr = ppf->Save(lnkPath.toStdWString().c_str(), TRUE);
            ppf->Release();
        }
        psl->Release();
    }
    CoUninitialize();

    if (FAILED(hr)) {
        return "Failed to save Windows shortcut file";
    }
    return QString();
}
#else
// Linux / generic Unix implementation
static QString chooseIconFileLinux()
{
    // Try using kdialog or zenity if available
    QString result;
    QProcess process;
    process.start("zenity", QStringList() << "--file-selection" << "--title=Select Game Icon"
                                          << "--file-filter=Icons (*.png *.ico *.svg *.jpg) | *.png *.ico *.svg *.jpg");
    if (process.waitForFinished(10000) && process.exitCode() == 0) {
        result = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    } else {
        process.start("kdialog", QStringList() << "--getopenfilename" << QDir::homePath()
                                               << "*.png *.ico *.svg *.jpg | Icons");
        if (process.waitForFinished(10000) && process.exitCode() == 0) {
            result = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
        }
    }
    return result;
}

static void applyShortcutIdentityLinux(const QString& appName, const QString& iconPath)
{
    if (!appName.isEmpty()) {
        QString slug = QString("moonlight-stream-%1").arg(appName.toLower().replace(QRegularExpression("[^a-z0-9]"), "-"));
        qputenv("SDL_VIDEO_WAYLAND_WMCLASS", slug.toUtf8());
        qputenv("SDL_VIDEO_X11_WMCLASS", slug.toUtf8());
        QGuiApplication::setDesktopFileName(slug);
        QGuiApplication::setApplicationDisplayName(appName);
    }
    if (!iconPath.isEmpty() && QFile::exists(iconPath)) {
        QGuiApplication::setWindowIcon(QIcon(iconPath));
    }
}

static QString createShortcutLinux(const QString& computerName,
                                   const QString& /*computerUuid*/,
                                   const QString& appName,
                                   const QString& iconPath)
{
    if (appName.isEmpty()) {
        return "Application name is empty";
    }
    if (iconPath.isEmpty() || !QFile::exists(iconPath)) {
        return "Icon file does not exist";
    }

    QString safeName = appName;
    safeName.replace(QRegularExpression("[/:\\\\*?\"<>|]"), "_");
    QString slug = safeName.toLower().replace(QRegularExpression("[^a-z0-9]"), "-");

    QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    QString appEntriesDir = QDir::homePath() + "/.local/share/applications";
    QString iconsDir = QDir::homePath() + "/.local/share/icons/hicolor/256x256/apps";
    QDir().mkpath(appEntriesDir);
    QDir().mkpath(iconsDir);

    QString destIcon = iconsDir + "/moonlight-" + slug + ".png";
    QImage srcImg(iconPath);
    if (!srcImg.isNull()) {
        srcImg.save(destIcon, "PNG");
    } else {
        QFile::copy(iconPath, destIcon);
    }

    QString moonlightBin = QCoreApplication::applicationFilePath();
    QString desktopEntry = QString(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=%1\n"
        "Comment=Stream %1 via Moonlight\n"
        "Exec=\"%2\" stream \"%3\" \"%4\" --icon \"%5\"\n"
        "Icon=%5\n"
        "Terminal=false\n"
        "StartupWMClass=moonlight-stream-%6\n"
        "Categories=Game;\n"
    ).arg(appName, moonlightBin, computerName, appName, destIcon, slug);

    QString appDesktopFile = appEntriesDir + "/moonlight-" + slug + ".desktop";
    QFile fileApp(appDesktopFile);
    if (fileApp.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        fileApp.write(desktopEntry.toUtf8());
        fileApp.close();
        fileApp.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                               QFile::ReadGroup | QFile::ExeGroup |
                               QFile::ReadOther | QFile::ExeOther);
    }

    if (!desktopPath.isEmpty()) {
        QString desktopFile = desktopPath + "/" + safeName + ".desktop";
        QFile fileDesk(desktopFile);
        if (fileDesk.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            fileDesk.write(desktopEntry.toUtf8());
            fileDesk.close();
            fileDesk.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner |
                                    QFile::ReadGroup | QFile::ExeGroup |
                                    QFile::ReadOther | QFile::ExeOther);
        }
    }

    return QString();
}
#endif

QString ShortcutHelper::chooseIconFile()
{
#if defined(Q_OS_DARWIN)
    return chooseIconFileMac();
#elif defined(Q_OS_WIN)
    return chooseIconFileWin();
#else
    return chooseIconFileLinux();
#endif
}

QString ShortcutHelper::createShortcut(const QString& computerName,
                                       const QString& computerUuid,
                                       const QString& appName,
                                       const QString& iconPath)
{
#if defined(Q_OS_DARWIN)
    return createShortcutMac(computerName, computerUuid, appName, iconPath);
#elif defined(Q_OS_WIN)
    return createShortcutWin(computerName, computerUuid, appName, iconPath);
#else
    return createShortcutLinux(computerName, computerUuid, appName, iconPath);
#endif
}

void ShortcutHelper::applyShortcutIdentity(const QString& appName, const QString& iconPath)
{
#if defined(Q_OS_DARWIN)
    applyShortcutIdentityMac(appName, iconPath);
#elif defined(Q_OS_WIN)
    applyShortcutIdentityWin(appName, iconPath);
#else
    applyShortcutIdentityLinux(appName, iconPath);
#endif
}

bool ShortcutHelper::writeIcnsFromImage(const QImage& sourceImage, const QString& outputPath)
{
    if (sourceImage.isNull()) {
        return false;
    }

    struct IconFormat {
        const char* tag;
        int size;
    };
    static const IconFormat formats[] = {
        {"icp4", 16},
        {"icp5", 32},
        {"icp6", 64},
        {"ic07", 128},
        {"ic08", 256},
        {"ic09", 512},
        {"ic10", 1024}
    };

    QByteArray chunksData;
    for (const auto& fmt : formats) {
        QImage scaled = sourceImage.scaled(fmt.size, fmt.size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QByteArray pngBytes;
        QBuffer buffer(&pngBytes);
        buffer.open(QIODevice::WriteOnly);
        scaled.save(&buffer, "PNG");
        buffer.close();

        if (pngBytes.isEmpty()) {
            continue;
        }

        quint32 chunkSize = 8 + pngBytes.size();
        chunksData.append(fmt.tag, 4);
        quint32 beChunkSize = qToBigEndian(chunkSize);
        chunksData.append(reinterpret_cast<const char*>(&beChunkSize), 4);
        chunksData.append(pngBytes);
    }

    if (chunksData.isEmpty()) {
        return false;
    }

    quint32 totalSize = 8 + chunksData.size();
    QByteArray finalData;
    finalData.append("icns", 4);
    quint32 beTotalSize = qToBigEndian(totalSize);
    finalData.append(reinterpret_cast<const char*>(&beTotalSize), 4);
    finalData.append(chunksData);

    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        return false;
    }
    outFile.write(finalData);
    outFile.close();
    return true;
}

bool ShortcutHelper::writeIcoFromImage(const QImage& sourceImage, const QString& outputPath)
{
    if (sourceImage.isNull()) {
        return false;
    }

    static const int sizes[] = { 16, 32, 48, 64, 128, 256 };
    const int count = sizeof(sizes) / sizeof(sizes[0]);

    QVector<QByteArray> pngBuffers;
    for (int size : sizes) {
        QImage scaled = sourceImage.scaled(size, size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        QByteArray pngBytes;
        QBuffer buffer(&pngBytes);
        buffer.open(QIODevice::WriteOnly);
        scaled.save(&buffer, "PNG");
        buffer.close();
        pngBuffers.append(pngBytes);
    }

    QByteArray icoData;
    QDataStream stream(&icoData, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);

    // ICONDIR header
    stream << quint16(0); // Reserved
    stream << quint16(1); // Type 1 = Icon
    stream << quint16(count);

    quint32 offset = 6 + (16 * count);
    for (int i = 0; i < count; ++i) {
        int sz = sizes[i];
        quint8 w = (sz >= 256) ? 0 : static_cast<quint8>(sz);
        quint8 h = (sz >= 256) ? 0 : static_cast<quint8>(sz);
        quint8 colors = 0;
        quint8 reserved = 0;
        quint16 planes = 1;
        quint16 bpp = 32;
        quint32 bytesInRes = pngBuffers[i].size();

        stream << w << h << colors << reserved;
        stream << planes << bpp << bytesInRes << offset;
        offset += bytesInRes;
    }

    for (int i = 0; i < count; ++i) {
        icoData.append(pngBuffers[i]);
    }

    QFile outFile(outputPath);
    if (!outFile.open(QIODevice::WriteOnly)) {
        return false;
    }
    outFile.write(icoData);
    outFile.close();
    return true;
}
