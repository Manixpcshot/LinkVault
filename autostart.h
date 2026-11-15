#pragma once
#include <QCoreApplication>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

// Run-at-login support (Windows registry / Linux XDG autostart)
namespace AutoStart {
inline bool supported() {
#if defined(Q_OS_WIN) || (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID))
    return true;
#else
    return false;
#endif
}
#ifdef Q_OS_WIN
inline const char* kRunKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
#endif
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
inline QString desktopFile() {
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + "/autostart/linkvault.desktop";
}
#endif

inline bool isEnabled() {
#ifdef Q_OS_WIN
    QSettings reg(kRunKey, QSettings::NativeFormat);
    return reg.contains("LinkVault");
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    return QFile::exists(desktopFile());
#else
    return false;
#endif
}

inline void setEnabled(bool on) {
    const QString exe = QDir::toNativeSeparators(QCoreApplication::applicationFilePath());
#ifdef Q_OS_WIN
    QSettings reg(kRunKey, QSettings::NativeFormat);
    if (on) reg.setValue("LinkVault", "\"" + exe + "\" --background");
    else    reg.remove("LinkVault");
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (on) {
        QDir().mkpath(QFileInfo(desktopFile()).absolutePath());
        QFile f(desktopFile());
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QString command = qEnvironmentVariable("APPIMAGE", exe);
            command.replace("\\", "\\\\").replace("\"", "\\\"").replace("$", "\\$");
            command.replace(QChar(0x60), "\\\x60").replace("%", "%%");
            command.replace("\\", "\\\\").replace("\n", "\\n").replace("\r", "\\r");
            f.write(QString("[Desktop Entry]\nType=Application\nName=LinkVault\nExec=\"%1\" --background\n"
                            "X-GNOME-Autostart-enabled=true\n").arg(command).toUtf8());
        }
    } else {
        QFile::remove(desktopFile());
    }
#else
    Q_UNUSED(on); Q_UNUSED(exe);
#endif
}
}
