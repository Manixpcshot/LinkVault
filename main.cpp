#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include <QSystemTrayIcon>
#include "mainwindow.h"
#include "autostart.h"
#include "platform.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("LinkVault");
    app.setOrganizationName("LinkVault");
    app.setWindowIcon(QIcon(":/icon.png"));
    app.setApplicationVersion(LINKVAULT_VERSION);
    app.setLayoutDirection(Qt::LeftToRight);
    app.setQuitOnLastWindowClosed(false);      // tray / popups decide when we quit (MainWindow::quitApp)

    // single instance: a second launch just brings the open window forward
    // (prevents two copies overwriting each other's bookmarks)
    const QString key = "LinkVault-" + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    if (!Platform::mobile) {
        QLocalSocket probe;
        probe.connectToServer(key);
        if (probe.waitForConnected(300)) {
            probe.write("show");
            probe.waitForBytesWritten(300);
            return 0;
        }
    }

    QFont f = app.font();
    f.setPixelSize(14);
    app.setFont(f);

    if (AutoStart::isEnabled()) AutoStart::setEnabled(true);   // keep path current if exe moved

    MainWindow w;

    QLocalServer server;
    if (!Platform::mobile) {
        server.setSocketOptions(QLocalServer::UserAccessOption);
        QLocalServer::removeServer(key);
        server.listen(key);
        QObject::connect(&server, &QLocalServer::newConnection, &w, [&] {
            while (QLocalSocket* c = server.nextPendingConnection()) {
                QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
                w.bringToFront();
            }
        });
    }

    // started by Windows at login with "close to tray" on: stay quietly in the tray
    const bool background = app.arguments().contains("--background") && QSystemTrayIcon::isSystemTrayAvailable()
                            && QSettings().value("closeToTray", false).toBool();
    if (!background || Platform::mobile) w.show();
    return app.exec();
}
