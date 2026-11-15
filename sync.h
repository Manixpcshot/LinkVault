#pragma once
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <functional>
#include "bookmark.h"

class QFileSystemWatcher;

struct SyncData {
    QList<Bookmark> items;
    QList<Collection> collections;
    QHash<QString, qint64> deleted;     // tombstones: id -> deletion time
};

// Keeps a copy of everything in a user-chosen folder (OneDrive / Google Drive / Dropbox ...).
//  - local change  -> schedule() -> after 2 s of quiet, write LinkVault-sync.json + copy new thumbnails
//  - folder change -> remoteChanged(data) after the file settles (our own writes are ignored)
// Merging is done by MainWindow (newest "modified" wins, tombstones remove deleted items).
class SyncManager : public QObject {
    Q_OBJECT
public:
    explicit SyncManager(QObject* parent = nullptr);
    void setFolder(const QString& folder);
    QString folder() const { return m_folder; }
    bool enabled() const { return !m_folder.isEmpty(); }
    qint64 lastSync() const { return m_lastSync; }

    void schedule();                               // debounced write
    void setProvider(std::function<SyncData()> f) { m_provider = std::move(f); }
    bool readRemote(SyncData& out);                // read the folder copy now (thumbnails are imported)
    void writeNow();

signals:
    void remoteChanged(const SyncData& data);
    void statusChanged();

private:
    QString syncFile() const { return m_folder + "/LinkVault-sync.json"; }
    QString syncThumbs() const { return m_folder + "/thumbs"; }
    void watch();
    void onFolderChanged();

    QString m_folder;
    QTimer m_writeTimer, m_readTimer;
    QFileSystemWatcher* m_watcher = nullptr;
    QByteArray m_lastHash;                          // hash of the last content we wrote / read
    qint64 m_lastSync = 0;
    std::function<SyncData()> m_provider;
};
