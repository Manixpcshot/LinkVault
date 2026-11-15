#include "notebook.h"
#include "bookmark.h"

#include <QDateTime>
#include <QSet>
#include <algorithm>
#include <QUuid>
#include <utility>

#ifdef Q_OS_ANDROID
#include <QCoreApplication>
#include <QJniObject>
#elif defined(Q_OS_IOS)
void updateIosNoteWidget(const Note& note);
#endif

namespace {
QJsonObject serialize(const QList<Note>& notes, const QString& widgetId) {
    QJsonArray entries;
    for (const auto& note : notes) entries.append(note.toJson());
    return {{"version", 1}, {"entries", entries}, {"widgetId", widgetId}};
}

QList<Note> readEntries(const QJsonObject& root) {
    QList<Note> notes;
    QSet<QString> ids;
    for (const auto& entry : root.value("entries").toArray()) {
        auto note = Note::fromJson(entry.toObject());
        if (note.id.isEmpty() || ids.contains(note.id) || (note.title.isEmpty() && note.body.isEmpty())) continue;
        ids.insert(note.id);
        notes.append(std::move(note));
    }
    return notes;
}
}

QJsonObject Note::toJson() const {
    return {{"id", id}, {"title", title}, {"body", body}, {"category", category}, {"color", color},
            {"updated", QString::number(updated)}, {"deadline", QString::number(deadline)},
            {"pinned", pinned}, {"notified", notified}};
}

Note Note::fromJson(const QJsonObject& obj) {
    return {obj["id"].toString(), obj["title"].toString(), obj["body"].toString(),
            obj["category"].toString(), obj["color"].toString(), obj["updated"].toString().toLongLong(),
            obj["deadline"].toString().toLongLong(), obj["pinned"].toBool(), obj["notified"].toBool()};
}

QString Note::displayTitle() const {
    return title.isEmpty() ? QStringLiteral("Untitled note") : title;
}

QString Note::countdown(qint64 now) const {
    if (deadline <= 0) return {};
    if (deadline <= now) return QStringLiteral("Time’s up");
    const qint64 left = deadline - now;
    const qint64 days = left / 86400;
    const QString clock = QString("%1:%2:%3").arg(left / 3600 % 24, 2, 10, QChar('0'))
        .arg(left / 60 % 60, 2, 10, QChar('0')).arg(left % 60, 2, 10, QChar('0'));
    return days > 0 ? QString("%1d %2").arg(days).arg(clock) : clock;
}

Notebook::Notebook(QObject* parent) : QObject(parent), m_file(Storage::dataDir() + "/notes.json") {
    QFile file(m_file);
    if (file.open(QIODevice::ReadOnly)) {
        const auto bytes = file.readAll();
        const auto root = QJsonDocument::fromJson(bytes).object();
        if (root["version"].toInt() == 1 && root["entries"].isArray()) {
            m_notes = readEntries(root);
            m_widgetId = root["widgetId"].toString();
        } else if (!bytes.trimmed().isEmpty() && !QFile::exists(m_file + ".bak")) {
            file.copy(m_file + ".bak");
        }
    }
    connect(&m_clock, &QTimer::timeout, this, &Notebook::updateClock);
    m_clock.start(1000);
    synchronizeWidget();
}

QStringList Notebook::categories() const {
    QStringList result;
    for (const auto& note : m_notes)
        if (!note.category.isEmpty() && !result.contains(note.category)) result.append(note.category);
    result.sort(Qt::CaseInsensitive);
    return result;
}

Note Notebook::note(const QString& id) const {
    for (const auto& entry : m_notes) if (entry.id == id) return entry;
    return {};
}

Note Notebook::widgetNote() const {
    const auto selected = note(m_widgetId);
    if (!selected.id.isEmpty()) return selected;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    const Note* next = nullptr;
    const Note* recent = nullptr;
    for (const auto& entry : m_notes) {
        if (entry.deadline > now && (!next || entry.deadline < next->deadline)) next = &entry;
        if (!recent || (entry.pinned != recent->pinned ? entry.pinned : entry.updated > recent->updated))
            recent = &entry;
    }
    return next ? *next : recent ? *recent : Note{};
}

bool Notebook::commit(const QList<Note>& notes, const QString& widgetId, bool announce) {
    if (!Storage::writeJson(m_file, QJsonDocument(serialize(notes, widgetId)))) return false;
    m_notes = notes;
    m_widgetId = widgetId;
    synchronizeWidget();
    if (announce) emit changed();
    return true;
}

bool Notebook::save(Note entry, bool showInWidget) {
    if (entry.id.isEmpty()) entry.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.updated = QDateTime::currentMSecsSinceEpoch();
    auto copy = m_notes;
    auto it = std::find_if(copy.begin(), copy.end(), [&entry](const Note& old) { return entry.id == old.id; });
    if (it == copy.end()) copy.append(entry);
    else {
        if (it->deadline == entry.deadline) entry.notified = it->notified;
        *it = entry;
    }
    QString widgetId = m_widgetId;
    if (showInWidget) widgetId = entry.id;
    else if (widgetId == entry.id) widgetId.clear();
    return commit(copy, widgetId);
}

bool Notebook::remove(const QString& id) {
    auto copy = m_notes;
    copy.removeIf([&id](const Note& note) { return note.id == id; });
    return commit(copy, m_widgetId == id ? QString() : m_widgetId);
}

QJsonObject Notebook::backup() const { return serialize(m_notes, m_widgetId); }

bool Notebook::restore(const QJsonObject& data) {
    if (data.isEmpty()) return true; // LinkVault 4.0 and older backups
    if (data["version"].toInt() != 1 || !data["entries"].isArray()) return false;
    auto copy = m_notes;
    for (const auto& incoming : readEntries(data)) {
        auto it = std::find_if(copy.begin(), copy.end(), [&incoming](const Note& local) { return local.id == incoming.id; });
        if (it == copy.end()) copy.append(incoming);
        else if (incoming.updated > it->updated) *it = incoming;
    }
    QString selection = m_widgetId.isEmpty() ? data["widgetId"].toString() : m_widgetId;
    if (std::none_of(copy.cbegin(), copy.cend(), [&selection](const Note& note) { return note.id == selection; }))
        selection.clear();
    return commit(copy, selection);
}

void Notebook::updateClock() {
    const auto now = QDateTime::currentSecsSinceEpoch();
    QStringList finished;
    for (const auto& note : m_notes)
        if (note.deadline > 0 && note.deadline <= now && !note.notified) finished.append(note.displayTitle());
    if (!finished.isEmpty()) {
        auto copy = m_notes;
        for (auto& note : copy) if (note.deadline > 0 && note.deadline <= now) note.notified = true;
        if (commit(copy, m_widgetId, false)) {
            emit changed();
            for (const auto& title : finished) emit countdownFinished(title);
        }
    }
    emit tick(now);
}

void Notebook::synchronizeWidget() const {
#ifdef Q_OS_ANDROID
    const auto entry = widgetNote();
    const auto context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) return;
    const auto title = QJniObject::fromString(entry.id.isEmpty() ? QString() : entry.displayTitle().left(100));
    const auto preview = QJniObject::fromString(entry.body.simplified().left(240));
    QJniObject::callStaticMethod<void>("io/github/manixpcshot/linkvault/NotesAppWidget", "publish",
        "(Landroid/content/Context;Ljava/lang/String;Ljava/lang/String;J)V",
        context.object(), title.object<jstring>(), preview.object<jstring>(), jlong(entry.deadline));
#elif defined(Q_OS_IOS)
    updateIosNoteWidget(widgetNote());
#endif
}
