#include "sync.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QFileSystemWatcher>
#include <QSysInfo>

SyncManager::SyncManager(QObject* parent) : QObject(parent) {
    m_writeTimer.setSingleShot(true);
    m_writeTimer.setInterval(2000);
    connect(&m_writeTimer, &QTimer::timeout, this, &SyncManager::writeNow);
    m_readTimer.setSingleShot(true);
    m_readTimer.setInterval(900);                   // cloud clients write in several steps; let it settle
    connect(&m_readTimer, &QTimer::timeout, this, &SyncManager::onFolderChanged);
}

void SyncManager::setFolder(const QString& folder) {
    m_folder = folder;
    m_lastHash.clear();
    if (m_watcher) { m_watcher->deleteLater(); m_watcher = nullptr; }
    if (!m_folder.isEmpty()) {
        QDir().mkpath(syncThumbs());
        m_watcher = new QFileSystemWatcher(this);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] { m_readTimer.start(); watch(); });
        connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] { m_readTimer.start(); watch(); });
        watch();
    }
    emit statusChanged();
}

void SyncManager::watch() {
    if (!m_watcher) return;
    // atomic saves replace the file, which drops it from the watcher: re-add every time
    if (!m_watcher->directories().contains(m_folder)) m_watcher->addPath(m_folder);
    if (QFile::exists(syncFile()) && !m_watcher->files().contains(syncFile())) m_watcher->addPath(syncFile());
}

void SyncManager::schedule() {
    if (enabled()) m_writeTimer.start();
}

void SyncManager::writeNow() {
    m_writeTimer.stop();
    if (!enabled() || !m_provider) return;
    const SyncData d = m_provider();
    QDir().mkpath(syncThumbs());

    QJsonArray items;
    for (const Bookmark& b : d.items) {
        QJsonObject o = b.toJson();
        // thumbnails travel as file names; each machine keeps its own copy in AppData
        auto shipThumb = [&](const QString& path, const char* key) {
            if (path.isEmpty() || !QFile::exists(path)) { o.remove(key); return; }
            const QString name = QFileInfo(path).fileName();
            const QString dst = syncThumbs() + "/" + name;
            if (!QFile::exists(dst) || QFileInfo(dst).size() != QFileInfo(path).size()) {
                QFile::remove(dst);
                QFile::copy(path, dst);
            }
            o[key] = name;
        };
        shipThumb(b.imagePath, "image");
        shipThumb(b.iconPath, "icon");
        items.append(o);
    }
    QJsonArray cols;
    for (const Collection& c : d.collections) cols.append(c.toJson());
    QJsonObject del;
    for (auto it = d.deleted.begin(); it != d.deleted.end(); ++it) del[it.key()] = QString::number(it.value());

    QJsonObject root;
    root["app"] = "LinkVault";
    root["version"] = 3;
    root["bookmarks"] = items;
    root["collections"] = cols;
    root["deleted"] = del;
    // the hash ignores the volatile header so identical data never ping-pongs between machines
    const QByteArray body = QJsonDocument(QJsonObject{{"b", items}, {"c", cols}, {"d", del}}).toJson(QJsonDocument::Compact);
    const QByteArray hash = QCryptographicHash::hash(body, QCryptographicHash::Sha1);
    if (hash == m_lastHash && QFile::exists(syncFile())) return;
    root["device"] = QSysInfo::machineHostName();
    root["written"] = QString::number(QDateTime::currentSecsSinceEpoch());

    m_lastHash = hash;
    if (Storage::writeJson(syncFile(), QJsonDocument(root))) {
        m_lastSync = QDateTime::currentSecsSinceEpoch();
        emit statusChanged();
    }
    watch();
}

bool SyncManager::readRemote(SyncData& out) {
    if (!enabled()) return false;
    QFile f(syncFile());
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject() || doc.object()["app"].toString() != "LinkVault") return false;
    const QJsonObject root = doc.object();
    const QJsonArray items = root["bookmarks"].toArray();
    const QJsonArray cols = root["collections"].toArray();
    const QJsonObject del = root["deleted"].toObject();

    const QByteArray body = QJsonDocument(QJsonObject{{"b", items}, {"c", cols}, {"d", del}}).toJson(QJsonDocument::Compact);
    const QByteArray hash = QCryptographicHash::hash(body, QCryptographicHash::Sha1);
    if (hash == m_lastHash) return false;          // our own write, or nothing new
    m_lastHash = hash;

    out = SyncData();
    const QString localThumbs = Storage::thumbsDir();
    for (const auto& v : items) {
        Bookmark b = Bookmark::fromJson(v.toObject());
        if (b.id.isEmpty() || b.url.isEmpty()) continue;
        auto bringThumb = [&](QString& field) {
            if (field.isEmpty()) return;
            const QString name = QFileInfo(field).fileName();
            const QString local = localThumbs + "/" + name;
            const QString remote = syncThumbs() + "/" + name;
            if (!QFile::exists(local) && QFile::exists(remote)) QFile::copy(remote, local);
            field = QFile::exists(local) ? local : QString();
        };
        bringThumb(b.imagePath);
        bringThumb(b.iconPath);
        out.items << b;
    }
    for (const auto& v : cols) {
        Collection c = Collection::fromJson(v.toObject());
        if (!c.id.isEmpty()) out.collections << c;
    }
    for (auto it = del.begin(); it != del.end(); ++it) out.deleted.insert(it.key(), it.value().toString().toLongLong());
    m_lastSync = QDateTime::currentSecsSinceEpoch();
    emit statusChanged();
    return true;
}

void SyncManager::onFolderChanged() {
    SyncData d;
    if (readRemote(d)) emit remoteChanged(d);
}
