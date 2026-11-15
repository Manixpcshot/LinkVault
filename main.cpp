#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSettings>
#include "mainwindow.h"
#include "autostart.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setApplicationName("LinkVault");
    app.setOrganizationName("LinkVault");
    app.setWindowIcon(QIcon(":/icon.png"));
    app.setLayoutDirection(Qt::RightToLeft);
    app.setQuitOnLastWindowClosed(false);      // tray / popups decide when we quit (MainWindow::quitApp)

    // single instance: a second launch just brings the open window forward
    // (prevents two copies overwriting each other's bookmarks)
    const QString key = "LinkVault-" + qEnvironmentVariable("USERNAME", qEnvironmentVariable("USER"));
    {
        QLocalSocket probe;
        probe.connectToServer(key);
        if (probe.waitForConnected(300)) {
            probe.write("show");
            probe.waitForBytesWritten(300);
            return 0;
        }
    }

    QFont f;
    f.setFamilies({"Vazirmatn", "Vazir", "Segoe UI", "Tahoma"});
    f.setPixelSize(14);
    app.setFont(f);

    if (AutoStart::isEnabled()) AutoStart::setEnabled(true);   // keep path current if exe moved

    MainWindow w;

    QLocalServer::removeServer(key);
    QLocalServer server;
    server.listen(key);
    QObject::connect(&server, &QLocalServer::newConnection, &w, [&] {
        while (QLocalSocket* c = server.nextPendingConnection()) {
            QObject::connect(c, &QLocalSocket::disconnected, c, &QObject::deleteLater);
            w.bringToFront();
        }
    });

    // started by Windows at login with "close to tray" on: stay quietly in the tray
    const bool background = app.arguments().contains("--background") && QSettings().value("closeToTray", false).toBool();
    if (!background) w.show();
    return app.exec();
}
