#pragma once
#include <QString>
#include <QList>
#include <QHash>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QStandardPaths>
#include <QUuid>

// Link health, filled by LinkChecker
enum LinkHealth { HealthUnknown = 0, HealthOk = 1, HealthRedirect = 2, HealthBroken = 3 };

struct Bookmark {
    QString id;
    QString name;
    QString url;
    QString imagePath;   // cached cover/preview image on disk
    QString iconPath;    // cached favicon on disk
    qint64  created = 0;
    bool    pinned = false;
    qint64  pinnedAt = 0;
    int     visits = 0;
    qint64  lastOpened = 0;
    // ---- v3 (all optional in JSON, old files load fine) ----
    QString collection;          // Collection::id, empty = none
    qint64  modified = 0;        // last local change (secs), used by sync merge
    int     health = HealthUnknown;
    QString redirectTo;          // final URL when health == HealthRedirect
    qint64  checkedAt = 0;       // last link check (secs)
    qint64  remindAt = 0;        // "read later" reminder (secs), 0 = none

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id; o["name"] = name; o["url"] = url;
        o["image"] = imagePath; o["icon"] = iconPath; o["created"] = QString::number(created);
        o["pinned"] = pinned; o["pinnedAt"] = QString::number(pinnedAt);
        o["visits"] = visits; o["lastOpened"] = QString::number(lastOpened);
        if (!collection.isEmpty()) o["collection"] = collection;
        if (modified) o["modified"] = QString::number(modified);
        if (health) o["health"] = health;
        if (!redirectTo.isEmpty()) o["redirectTo"] = redirectTo;
        if (checkedAt) o["checkedAt"] = QString::number(checkedAt);
        if (remindAt) o["remindAt"] = QString::number(remindAt);
        return o;
    }
    static Bookmark fromJson(const QJsonObject& o) {
        Bookmark b;
        b.id = o["id"].toString(); b.name = o["name"].toString(); b.url = o["url"].toString();
        b.imagePath = o["image"].toString(); b.iconPath = o["icon"].toString(); b.created = o["created"].toString().toLongLong();
        b.pinned = o["pinned"].toBool(); b.pinnedAt = o["pinnedAt"].toString().toLongLong();
        b.visits = o["visits"].toInt(); b.lastOpened = o["lastOpened"].toString().toLongLong();
        b.collection = o["collection"].toString();
        b.modified = o["modified"].toString().toLongLong();
        b.health = qBound(0, o["health"].toInt(), 3);
        b.redirectTo = o["redirectTo"].toString();
        b.checkedAt = o["checkedAt"].toString().toLongLong();
        b.remindAt = o["remindAt"].toString().toLongLong();
        return b;
    }
};

struct Collection {
    QString id;
    QString name;
    QString emoji;
    QString color;        // "#RRGGBB", empty = automatic (brand color of its first site)
    qint64  created = 0;
    qint64  modified = 0;

    QJsonObject toJson() const {
        QJsonObject o;
        o["id"] = id; o["name"] = name; o["emoji"] = emoji; o["color"] = color;
        o["created"] = QString::number(created); o["modified"] = QString::number(modified);
        return o;
    }
    static Collection fromJson(const QJsonObject& o) {
        Collection c;
        c.id = o["id"].toString(); c.name = o["name"].toString(); c.emoji = o["emoji"].toString();
        c.color = o["color"].toString(); c.created = o["created"].toString().toLongLong();
        c.modified = o["modified"].toString().toLongLong();
        return c;
    }
};

namespace Storage {
inline QString dataDir() {
    const QString d = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(d + "/thumbs");
    return d;
}
inline QString thumbsDir()       { return dataDir() + "/thumbs"; }
inline QString dataFile()        { return dataDir() + "/bookmarks.json"; }
// kept in separate files so bookmarks.json stays readable by older versions
inline QString collectionsFile() { return dataDir() + "/collections.json"; }
inline QString deletedFile()     { return dataDir() + "/deleted.json"; }   // tombstones for sync

inline bool writeJson(const QString& path, const QJsonDocument& doc) {
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(doc.toJson());
    return f.commit();
}

inline QList<Bookmark> load() {
    QList<Bookmark> out;
    QFile f(dataFile());
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QByteArray data = f.readAll();
    f.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        // keep a copy of the broken file so saving never destroys user data
        if (!data.trimmed().isEmpty()) {
            QFile::remove(dataFile() + ".bak");
            QFile::copy(dataFile(), dataFile() + ".bak");
        }
        return out;
    }
    for (const auto& v : doc.array()) {
        Bookmark b = Bookmark::fromJson(v.toObject());
        if (b.url.isEmpty()) continue;
        if (b.id.isEmpty()) b.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        if (!b.imagePath.isEmpty() && !QFile::exists(b.imagePath)) b.imagePath.clear();
        if (!b.iconPath.isEmpty() && !QFile::exists(b.iconPath)) b.iconPath.clear();
        out << b;
    }
    return out;
}
inline void save(const QList<Bookmark>& items) {
    QJsonArray arr;
    for (const auto& b : items) arr.append(b.toJson());
    writeJson(dataFile(), QJsonDocument(arr));
}

inline QList<Collection> loadCollections() {
    QList<Collection> out;
    QFile f(collectionsFile());
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    for (const auto& v : doc.array()) {
        Collection c = Collection::fromJson(v.toObject());
        if (!c.id.isEmpty() && !c.name.isEmpty()) out << c;
    }
    return out;
}
inline void saveCollections(const QList<Collection>& cols) {
    QJsonArray arr;
    for (const auto& c : cols) arr.append(c.toJson());
    writeJson(collectionsFile(), QJsonDocument(arr));
}

inline QHash<QString, qint64> loadDeleted() {
    QHash<QString, qint64> out;
    QFile f(deletedFile());
    if (!f.open(QIODevice::ReadOnly)) return out;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    for (auto it = o.begin(); it != o.end(); ++it) out.insert(it.key(), it.value().toString().toLongLong());
    return out;
}
inline void saveDeleted(const QHash<QString, qint64>& d) {
    QJsonObject o;
    for (auto it = d.begin(); it != d.end(); ++it) o[it.key()] = QString::number(it.value());
    writeJson(deletedFile(), QJsonDocument(o));
}
}
