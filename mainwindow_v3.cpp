// LinkVault 3.0 features for MainWindow: collections, link health, reminders, tray,
// global hotkeys + Quick Add + Command Palette, Mica-style material and folder sync.
#include "mainwindow.h"
#include "autostart.h"
#include "platform.h"
#include "backdrop.h"
#include "card.h"
#include "dialogs.h"
#include "hotkey.h"
#include "linkchecker.h"
#include "settingspage.h"
#include "theme.h"
#include "titlebar.h"
#include "widgets.h"
#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QIcon>
#include <QGuiApplication>
#include <QScreen>
#include <QSettings>
#include <QSystemTrayIcon>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace {
QString normQ(QString s) {
    s = s.toLower();
    s.replace(QChar(0x064A), QChar(0x06CC));
    s.replace(QChar(0x0643), QChar(0x06A9));
    s.remove(QChar(0x200C));
    return s;
}
qint64 nowSecs() { return QDateTime::currentSecsSinceEpoch(); }
QString urlKey(const QString& u) { return QUrl(u).adjusted(QUrl::StripTrailingSlash).toString(); }

// later lambdas: actions from a card menu must not touch cards while the menu is still unwinding
template <typename F> void later(QObject* ctx, F f) { QTimer::singleShot(0, ctx, f); }

QString wallpaperPath() {
#ifdef Q_OS_WIN
    wchar_t buf[MAX_PATH * 2] = {};
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH * 2, buf, 0)) {
        const QString p = QString::fromWCharArray(buf);
        if (!p.isEmpty() && QFile::exists(p)) return p;
    }
    // slideshow / Spotlight: Windows keeps the current picture here
    const QString t = QDir::fromNativeSeparators(qEnvironmentVariable("APPDATA"))
                      + "/Microsoft/Windows/Themes/TranscodedWallpaper";
    if (QFile::exists(t)) return t;
#endif
    return {};
}
} // namespace

// ============================== persistence ==============================

QByteArray MainWindow::itemHash(const Bookmark& b) const {
    QJsonObject o = b.toJson();
    o.remove("modified");
    o.remove("checkedAt");          // a routine link check is not a user change
    return QCryptographicHash::hash(QJsonDocument(o).toJson(QJsonDocument::Compact), QCryptographicHash::Md5);
}

void MainWindow::rehash() {
    m_hashes.clear();
    for (const Bookmark& b : std::as_const(m_items)) m_hashes.insert(b.id, itemHash(b));
}

void MainWindow::persist() {
    const qint64 now = nowSecs();
    for (Bookmark& b : m_items) {
        const QByteArray h = itemHash(b);
        auto it = m_hashes.find(b.id);
        if (it == m_hashes.end() || it.value() != h) {
            b.modified = now;
            m_hashes.insert(b.id, h);
        }
    }
    for (auto it = m_deleted.begin(); it != m_deleted.end();)          // forget tombstones after 90 days
        it = (now - it.value() > 90LL * 86400) ? m_deleted.erase(it) : std::next(it);
    Storage::save(m_items);
    Storage::saveCollections(m_collections);
    Storage::saveDeleted(m_deleted);
    if (m_sync) m_sync->schedule();
}

SettingsState MainWindow::settingsState() const {
    SettingsState s;
    s.themeMode = m_themeMode;
    s.autoStart = AutoStart::isEnabled();
    s.autoStartSupported = AutoStart::supported();
    s.traySupported = m_tray != nullptr;
    s.animations = Theme::animations();
    s.minimizeOnOpen = m_minimizeOnOpen;
    s.useClipboard = m_useClipboard;
    s.mica = m_mica;
    s.micaSupported = micaSupported();
    s.closeToTray = m_closeToTray;
    s.linkCheck = m_linkCheck;
    s.hotkeysSupported = GlobalHotkeys::isSupported();
    s.quickAddKey = m_quickKey;
    s.paletteKey = m_paletteKey;
    s.syncFolder = m_sync ? m_sync->folder() : QString();
    s.syncStatus = syncStatusText();
    if (m_checker && m_checker->isBusy())
        s.linkStatus = QString("Checking… %1 remaining").arg(m_checker->pending());
    s.total = int(m_items.size());
    for (const auto& b : m_items) s.pinned += b.pinned ? 1 : 0;
    return s;
}

// ============================== collections ==============================

int MainWindow::collectionIndex(const QString& id) const {
    if (id.isEmpty()) return -1;
    for (int i = 0; i < m_collections.size(); ++i)
        if (m_collections[i].id == id) return i;
    return -1;
}

QColor MainWindow::collectionColor(const QString& id) const {
    const int ci = collectionIndex(id);
    if (ci < 0) return {};
    const QColor fixed(m_collections[ci].color);
    if (!m_collections[ci].color.isEmpty() && fixed.isValid()) return fixed;
    // automatic: brand color of the oldest site in it
    const Bookmark* first = nullptr;
    for (const auto& b : m_items)
        if (b.collection == id && (!first || b.created < first->created)) first = &b;
    if (first)
        if (auto* c = m_cards.value(first->id)) if (c->accent().isValid()) return c->accent();
    return QColor::fromHsl(int(qHash(id) % 360), 150, 135);
}

QString MainWindow::collectionLabel(const QString& id) const {
    const int ci = collectionIndex(id);
    if (ci < 0) return {};
    return (m_collections[ci].emoji + " " + m_collections[ci].name).trimmed();
}

void MainWindow::refreshCollections() {
    if (!m_colBar) return;
    QHash<QString, int> counts;
    for (const auto& b : std::as_const(m_items)) counts[b.collection]++;
    QList<CollectionBar::Chip> chips;
    chips << CollectionBar::Chip{QString(), QString("🗂 All"), QColor(), int(m_items.size())};
    for (const auto& c : std::as_const(m_collections))
        chips << CollectionBar::Chip{c.id, (c.emoji + "  " + c.name).trimmed(), collectionColor(c.id), counts.value(c.id)};
    m_colBar->setChips(chips);
    m_colBar->setCurrent(m_collectionFilter, false);
}

void MainWindow::newCollection(const QString& assignBookmark) {
    if (!isVisible()) bringToFront();
    CollectionDialog d(this, "New collection");
    if (d.exec() != QDialog::Accepted) return;
    Collection c;
    c.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    c.name = d.name();
    c.emoji = d.emoji();
    c.created = c.modified = nowSecs();
    m_collections << c;
    if (!assignBookmark.isEmpty() && indexOf(assignBookmark) >= 0) {
        moveToCollection(assignBookmark, c.id);
    } else {
        persist();
        refreshCollections();
        showToast(QString("✓ Created “%1”. Drag bookmarks into it.").arg(c.name), 3200);
    }
}

void MainWindow::editCollection(const QString& id) {
    const int ci = collectionIndex(id);
    if (ci < 0) return;
    CollectionDialog d(this, "Edit collection", m_collections[ci].name, m_collections[ci].emoji);
    if (d.exec() != QDialog::Accepted) return;
    const int cj = collectionIndex(id);
    if (cj < 0) return;
    m_collections[cj].name = d.name();
    m_collections[cj].emoji = d.emoji();
    m_collections[cj].modified = nowSecs();
    persist();
    refreshCollections();
    refresh(false);
}

void MainWindow::deleteCollection(const QString& id) {
    const int ci = collectionIndex(id);
    if (ci < 0) return;
    const QString name = m_collections[ci].name;
    const auto answer = QMessageBox::question(this, "Delete collection",
        QString("Delete “%1”?\nIts bookmarks will stay in your library.").arg(name));
    if (answer != QMessageBox::Yes) return;
    const int cj = collectionIndex(id);
    if (cj < 0) return;
    for (auto& b : m_items) if (b.collection == id) b.collection.clear();
    m_collections.removeAt(cj);
    m_deleted.insert("col:" + id, nowSecs());
    if (m_collectionFilter == id) {
        m_collectionFilter.clear();
        QSettings().remove("collectionFilter");
    }
    persist();
    refreshCollections();
    refresh(true);
    showToast(QString("Deleted collection “%1”").arg(name));
}

void MainWindow::moveToCollection(const QString& bookmarkId, const QString& collectionId) {
    const int i = indexOf(bookmarkId);
    if (i < 0) return;
    const QString target = collectionIndex(collectionId) >= 0 ? collectionId : QString();
    if (m_items[i].collection == target) return;
    m_items[i].collection = target;
    persist();
    refreshCollections();
    refresh(true);
    if (target.isEmpty()) showToast(QString("Removed “%1” from its collection").arg(m_items[i].name));
    else showToast(QString("📁 Moved “%1” to %2").arg(m_items[i].name, collectionLabel(target)));
}

void MainWindow::showCollectionMenu(const QString& id, const QPoint& globalPos) {
    const int ci = collectionIndex(id);
    if (ci < 0) return;
    QMenu m(this);
    QAction* edit = m.addAction("✏️ Change name and icon");
    QMenu* colors = m.addMenu("🎨 Color");
    QAction* autoColor = colors->addAction("✨ Automatic (site colors)");
    autoColor->setCheckable(true);
    autoColor->setChecked(m_collections[ci].color.isEmpty());
    const QList<QPair<QString, QString>> presets = {
        {"#6C5CE7", "Purple"}, {"#3B82F6", "Blue"}, {"#14B8A6", "Teal"}, {"#22C55E", "Green"},
        {"#F59E0B", "Yellow"}, {"#F97316", "Orange"}, {"#EF4444", "Red"}, {"#EC4899", "Pink"}};
    QList<QAction*> colorActs;
    for (const auto& pr : presets) {
        QPixmap dot(16, 16);
        dot.fill(Qt::transparent);
        { QPainter dp(&dot); dp.setRenderHint(QPainter::Antialiasing); dp.setPen(Qt::NoPen);
          dp.setBrush(QColor(pr.first)); dp.drawEllipse(2, 2, 12, 12); }
        QAction* a = colors->addAction(QIcon(dot), pr.second);
        a->setData(pr.first);
        a->setCheckable(true);
        a->setChecked(m_collections[ci].color.compare(pr.first, Qt::CaseInsensitive) == 0);
        colorActs << a;
    }
    m.addSeparator();
    QAction* del = m.addAction("🗑 Delete collection");
    QAction* act = m.exec(globalPos);
    if (!act) return;
    if (act == edit) { editCollection(id); return; }
    if (act == del) { deleteCollection(id); return; }
    const int cj = collectionIndex(id);
    if (cj < 0) return;
    if (act == autoColor) m_collections[cj].color.clear();
    else if (colorActs.contains(act)) m_collections[cj].color = act->data().toString();
    else return;
    m_collections[cj].modified = nowSecs();
    persist();
    refreshCollections();
    refresh(false);
}

// extra items in every card's context menu
void MainWindow::extendCardMenu(QMenu& menu, const QString& id) {
    const int i = indexOf(id);
    if (i < 0) return;
    const Bookmark& b = m_items[i];

    // ---- move to collection ----
    QMenu* mv = menu.addMenu("📁 Move to collection");
    QAction* none = mv->addAction("No collection");
    none->setCheckable(true);
    none->setChecked(b.collection.isEmpty());
    connect(none, &QAction::triggered, this, [this, id] { later(this, [this, id] { moveToCollection(id, {}); }); });
    if (!m_collections.isEmpty()) mv->addSeparator();
    for (const auto& c : std::as_const(m_collections)) {
        QAction* a = mv->addAction((c.emoji + "  " + c.name).trimmed());
        a->setCheckable(true);
        a->setChecked(b.collection == c.id);
        const QString cid = c.id;
        connect(a, &QAction::triggered, this, [this, id, cid] { later(this, [this, id, cid] { moveToCollection(id, cid); }); });
    }
    mv->addSeparator();
    QAction* fresh = mv->addAction("+ New collection…");
    connect(fresh, &QAction::triggered, this, [this, id] { later(this, [this, id] { newCollection(id); }); });

    // ---- read later ----
    QMenu* rm = menu.addMenu(b.remindAt > 0 ? "⏰ Reminder set" : "⏰ Read later");
    const QDateTime now = QDateTime::currentDateTime();
    auto addWhen = [&](const QString& text, const QDateTime& when) {
        QAction* a = rm->addAction(text);
        const qint64 t = when.toSecsSinceEpoch();
        connect(a, &QAction::triggered, this, [this, id, t] { later(this, [this, id, t] { setReminder(id, t); }); });
    };
    addWhen("In one hour", now.addSecs(3600));
    if (now.time().hour() < 19) addWhen("Tonight at 8 PM", QDateTime(now.date(), QTime(20, 0)));
    addWhen("Tomorrow at 9 AM", QDateTime(now.date().addDays(1), QTime(9, 0)));
    addWhen("Friday at 10 AM", QDateTime(now.date().addDays((5 - now.date().dayOfWeek() + 7) % 7 == 0 ? 7
                                         : (5 - now.date().dayOfWeek() + 7) % 7), QTime(10, 0)));
    QAction* custom = rm->addAction("Choose a time…");
    connect(custom, &QAction::triggered, this, [this, id] {
        later(this, [this, id] {
            ReminderDialog d(this, QDateTime::currentDateTime().addSecs(2 * 3600));
            if (d.exec() == QDialog::Accepted) setReminder(id, d.dateTime().toSecsSinceEpoch());
        });
    });
    if (b.remindAt > 0) {
        rm->addSeparator();
        QAction* cancel = rm->addAction("✕ Cancel reminder");
        connect(cancel, &QAction::triggered, this, [this, id] { later(this, [this, id] { setReminder(id, 0); }); });
    }

    // ---- link health ----
    if (b.health == HealthRedirect && !b.redirectTo.isEmpty()) {
        QAction* fix = menu.addAction("↪ Use the new address");
        fix->setToolTip(b.redirectTo);
        connect(fix, &QAction::triggered, this, [this, id] {
            later(this, [this, id] {
                const int j = indexOf(id);
                if (j < 0) return;
                m_items[j].url = m_items[j].redirectTo;
                m_items[j].redirectTo.clear();
                m_items[j].health = HealthOk;
                persist();
                if (auto* c = m_cards.value(id)) c->setBookmark(m_items[j]);
                refresh(true);
                startFetch(id);
                showToast("✓ Address updated");
            });
        });
    }
    if (b.health == HealthBroken || b.health == HealthRedirect) {
        QAction* ignore = menu.addAction("✓ Dismiss this warning");
        connect(ignore, &QAction::triggered, this, [this, id] {
            later(this, [this, id] {
                const int j = indexOf(id);
                if (j < 0) return;
                m_items[j].health = HealthOk;
                m_items[j].redirectTo.clear();
                m_items[j].checkedAt = nowSecs() + 30LL * 86400;     // don't nag again for a month
                persist();
                if (auto* c = m_cards.value(id)) c->setBookmark(m_items[j]);
                refresh(true);
            });
        });
    }
    QAction* checkOne = menu.addAction("🔍 Check this link");
    connect(checkOne, &QAction::triggered, this, [this, id] {
        later(this, [this, id] {
            const int j = indexOf(id);
            if (j < 0) return;
            m_checkManual = true;
            m_checkFound = 0;
            m_checker->enqueue(id, QUrl(m_items[j].url));
        });
    });
}

// ============================== link health ==============================

void MainWindow::checkLinks(bool force) {
    if (!force && !m_linkCheck) return;
    const qint64 now = nowSecs();
    if (!m_checker->isBusy()) m_checkFound = 0;
    int n = 0;
    for (const auto& b : std::as_const(m_items)) {
        if (force || now - b.checkedAt > 86400) { m_checker->enqueue(b.id, QUrl(b.url)); ++n; }
    }
    if (n == 0) { if (force) showToast("There are no links to check."); return; }
    if (force) {
        m_checkManual = true;
        showToast(QString("🔍 Checking links: %1…").arg(n), 2500);
        m_settings->setLinkStatus(QString("Checking links: %1…").arg(n));
    }
}

void MainWindow::onLinkChecked(const QString& id, int health, const QString& redirect) {
    const int i = indexOf(id);
    if (i < 0) return;
    Bookmark& b = m_items[i];
    b.checkedAt = nowSecs();
    if (health != HealthUnknown) {                       // inconclusive results keep the old verdict
        b.health = health;
        b.redirectTo = health == HealthRedirect ? redirect : QString();
    }
    if (b.health == HealthBroken || b.health == HealthRedirect) ++m_checkFound;
    if (auto* c = m_cards.value(id)) c->setBookmark(b);
    if (m_checker->pending() % 10 == 0 && m_checker->isBusy())
        m_settings->setLinkStatus(QString("Checking… %1 remaining").arg(m_checker->pending()));
}

// ============================== reminders ==============================

void MainWindow::setReminder(const QString& id, qint64 when) {
    const int i = indexOf(id);
    if (i < 0) return;
    m_items[i].remindAt = when;
    persist();
    if (auto* c = m_cards.value(id)) c->setBookmark(m_items[i]);
    if (when > 0)
        showToast(QString("⏰ Reminder set for %1")
                  .arg(QDateTime::fromSecsSinceEpoch(when).toString("yyyy/MM/dd  HH:mm")), 3000);
    else
        showToast("Reminder canceled");
}

void MainWindow::checkReminders() {
    const qint64 now = nowSecs();
    bool changed = false;
    for (auto& b : m_items) {
        if (b.remindAt <= 0 || b.remindAt > now) continue;
        b.remindAt = 0;
        changed = true;
        m_lastNotified = b.id;
        QString host = QUrl(b.url).host();
        if (host.startsWith("www.")) host = host.mid(4);
        if (m_tray && m_tray->isVisible() && QSystemTrayIcon::supportsMessages()) {
            m_tray->showMessage("⏰ Time to read", b.name + "\n" + host + "\n(Select to open)",
                                QSystemTrayIcon::Information, 12000);
        } else {
            bringToFront();
            showToast(QString("⏰ Reminder: %1").arg(b.name), 6000);
        }
        if (auto* c = m_cards.value(b.id)) c->setBookmark(b);
    }
    if (changed) persist();
}

// ============================== tray & quitting ==============================

void MainWindow::setupTray() {
    if (!QSystemTrayIcon::isSystemTrayAvailable()) return;
    m_tray = new QSystemTrayIcon(QIcon(":/icon.png"), this);
    m_tray->setToolTip("LinkVault");
    m_trayMenu = new QMenu(this);
    m_trayMenu->addAction("🔖 Open LinkVault", this, [this] { bringToFront(); });
    m_trayMenu->addAction("⚡ Quick add", this, [this] { showQuickAdd(); });
    m_trayMenu->addAction("🔍 Quick search", this, [this] { showPalette(); });
    m_trayMenu->addSeparator();
    m_trayMenu->addAction("⏻ Quit", this, [this] { quitApp(); });
    m_tray->setContextMenu(m_trayMenu);
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) bringToFront();
    });
    connect(m_tray, &QSystemTrayIcon::messageClicked, this, [this] {
        if (!m_lastNotified.isEmpty() && indexOf(m_lastNotified) >= 0) onOpen(m_lastNotified);
    });
    m_tray->show();
}

void MainWindow::quitApp() {
    if (m_quitting && !isVisible()) { qApp->quit(); return; }
    m_quitting = true;
    commitPendingDelete();
    if (isVisible()) QSettings().setValue("geometry", saveGeometry());
    if (m_sync && m_sync->enabled()) m_sync->writeNow();
    if (m_tray) m_tray->hide();
    QTimer::singleShot(0, qApp, &QCoreApplication::quit);
}

// ============================== hotkeys, quick add, palette ==============================

void MainWindow::applyHotkeys(bool report) {
    if (!GlobalHotkeys::isSupported()) return;
    const bool okQ = m_hotkeys->setHotkey(GlobalHotkeys::QuickAdd, m_quickKey);
    const bool okP = m_hotkeys->setHotkey(GlobalHotkeys::Palette, m_paletteKey);
    const QString err = "This shortcut is in use. Try another combination using Ctrl or Alt.";
    m_settings->setHotkeyError(1, okQ ? QString() : err);
    m_settings->setHotkeyError(2, okP ? QString() : err);
    if (report && okQ && okP) showToast("✓ Shortcut saved");
}

void MainWindow::showQuickAdd() {
    if (m_palette->isVisible()) m_palette->dismiss();
    QString clip = QApplication::clipboard()->text().trimmed();
    if (!(clip.startsWith("http") || clip.startsWith("www.")) || clip.contains('\n') || clip.size() > 2000) clip.clear();
    QList<QPair<QString, QString>> cols{{QString(), QString("📂 No collection")}};
    for (const auto& c : std::as_const(m_collections)) cols << qMakePair(c.id, (c.emoji + "  " + c.name).trimmed());
    m_quickAdd->open(clip, cols, m_collectionFilter);
}

void MainWindow::showPalette() {
    if (m_quickAdd->isVisible()) m_quickAdd->dismiss();
    if (m_palette->isVisible() && !m_palette->isClosing()) { m_palette->dismiss(); return; }   // toggle
    m_palette->open();
}

QList<PaletteItem> MainWindow::paletteQuery(const QString& text) {
    QList<PaletteItem> out;
    if (text.startsWith('>')) {
        struct Cmd { const char* key; const char* glyph; const char* title; const char* sub; };
        static const Cmd cmds[] = {
            {"add", "+", "Add link", "Ctrl+N"},
            {"quick", "⚡", "Quick add from clipboard", ""},
            {"collection", "📁", "New collection", ""},
            {"settings", "⚙", "Settings", "Ctrl+,"},
            {"light", "☀️", "Light theme", ""},
            {"dark", "🌙", "Dark theme", ""},
            {"black", "◉", "Black theme", ""},
            {"system", "💻", "Follow system theme", ""},
            {"mica", "🪟", "Toggle the Mica background", ""},
            {"check", "🔍", "Check all links", ""},
            {"problems", "⚠️", "Show links with issues", ""},
            {"sync", "☁️", "Sync now", ""},
            {"export", "⬆", "Export backup", ""},
            {"folder", "📂", "Open data folder", ""},
            {"quit", "⏻", "Quit LinkVault", ""},
        };
        const QStringList tokens = normQ(text.mid(1)).simplified().split(' ', Qt::SkipEmptyParts);
        for (const Cmd& c : cmds) {
            const QString key = QString::fromLatin1(c.key);
            if ((key == "mica" && !micaSupported()) || (Platform::mobile && (key == "folder" || key == "quit")))
                continue;
            const QString title = QString::fromUtf8(c.title);
            const QString hay = normQ(title + " " + QString::fromLatin1(c.key));
            bool ok = true;
            for (const auto& t : tokens) if (!hay.contains(t)) { ok = false; break; }
            if (!ok) continue;
            PaletteItem it;
            it.key = key;
            it.title = title;
            it.subtitle = QString::fromUtf8(c.sub);
            it.glyph = QString::fromUtf8(c.glyph);
            it.command = true;
            out << it;
        }
        return out;
    }

    QStringList tokens;
    for (const auto& w : text.simplified().split(' ', Qt::SkipEmptyParts)) tokens << normQ(w);
    struct Hit { int idx; int score; };
    QList<Hit> hits;
    for (int i = 0; i < m_items.size(); ++i) {
        const int sc = tokens.isEmpty() ? 0 : matchScore(m_items[i], tokens);
        if (sc >= 0) hits << Hit{i, sc};
    }
    std::stable_sort(hits.begin(), hits.end(), [&](const Hit& a, const Hit& b) {
        if (a.score != b.score) return a.score > b.score;
        const Bookmark& x = m_items[a.idx];
        const Bookmark& y = m_items[b.idx];
        if (x.pinned != y.pinned) return x.pinned;                    // empty query: pinned + recent first
        if (x.lastOpened != y.lastOpened) return x.lastOpened > y.lastOpened;
        return x.visits > y.visits;
    });
    // collections that match also show up (jump into them)
    if (!tokens.isEmpty()) {
        for (const auto& c : std::as_const(m_collections)) {
            const QString hay = normQ(c.name);
            bool ok = true;
            for (const auto& t : tokens) if (!hay.contains(t)) { ok = false; break; }
            if (!ok) continue;
            PaletteItem it;
            it.key = "col:" + c.id;
            it.title = (c.emoji + "  " + c.name).trimmed();
            it.subtitle = "Open collection";
            it.glyph = c.emoji.isEmpty() ? QString("📁") : c.emoji;
            it.color = collectionColor(c.id);
            it.command = true;
            out << it;
        }
    }
    for (const Hit& h : hits) {
        if (out.size() >= 8) break;
        const Bookmark& b = m_items[h.idx];
        PaletteItem it;
        it.key = b.id;
        it.title = (b.pinned ? QString("📌 ") : QString()) + b.name;
        QString host = QUrl(b.url).host();
        if (host.startsWith("www.")) host = host.mid(4);
        it.subtitle = b.collection.isEmpty() ? host : host + "   ·   " + collectionLabel(b.collection);
        it.iconPath = b.iconPath;
        it.glyph = b.name.left(1).toUpper();
        if (auto* c = m_cards.value(b.id)) it.color = c->accent();
        out << it;
    }
    return out;
}

void MainWindow::onPaletteActivated(const PaletteItem& item, bool copyOnly) {
    if (!item.command) {
        if (copyOnly) onCopy(item.key); else onOpen(item.key);
        return;
    }
    const QString k = item.key;
    auto front = [this] { bringToFront(); goHome(); };
    if (k.startsWith("col:")) {
        front();
        m_collectionFilter = k.mid(4);
        QSettings().setValue("collectionFilter", m_collectionFilter);
        m_colBar->setCurrent(m_collectionFilter);
        refresh(true);
        smoothScrollTo(0);
    } else if (k == "add") { front(); addBookmark(); }
    else if (k == "quick") { QTimer::singleShot(220, this, [this] { showQuickAdd(); }); }
    else if (k == "collection") { front(); newCollection(); }
    else if (k == "settings") { bringToFront(); openSettings(); }
    else if (k == "light" || k == "dark" || k == "black" || k == "system") {
        m_themeMode = k == "light" ? Theme::Light : k == "dark" ? Theme::Dark : k == "black" ? Theme::Black : Theme::System;
        QSettings().setValue("themeMode", m_themeMode);
        applyTheme(true);
    } else if (k == "mica") {
        if (!micaSupported()) { showToast("The Mica background is unavailable.", 3000); return; }
        m_mica = !m_mica;
        QSettings().setValue("mica", m_mica);
        updateMaterial();
        showToast(m_mica ? "🪟 Mica background enabled" : "Mica background disabled");
    } else if (k == "check") { checkLinks(true); }
    else if (k == "problems") {
        front();
        m_filterMode = 2;
        m_filter->setCurrent(2);
        refresh(true);
    } else if (k == "sync") {
        if (!m_sync->enabled()) { bringToFront(); openSettings(); showToast("Choose a sync folder first.", 3000); return; }
        SyncData remote;
        if (m_sync->readRemote(remote)) mergeRemote(remote);
        m_sync->writeNow();
        showToast("☁️ Sync complete");
    } else if (k == "export") { bringToFront(); exportData(); }
    else if (k == "folder") { QDesktopServices::openUrl(QUrl::fromLocalFile(Storage::dataDir())); }
    else if (k == "quit") { quitApp(); }
}

// ============================== Mica-style material ==============================

bool MainWindow::micaSupported() const { return !wallpaperPath().isEmpty(); }

void MainWindow::updateMaterial() {
    if (!m_backdrop) return;
    if (Theme::isBlack() || !m_mica || !micaSupported()) { m_backdrop->setMaterial({}, {}); return; }
    QScreen* sc = screen() ? screen() : QGuiApplication::primaryScreen();
    if (!sc) return;
    const QRect g = sc->geometry();
    QImage src(wallpaperPath());
    if (src.isNull() || g.isEmpty()) { m_backdrop->setMaterial({}, {}); return; }
    // "fill" the screen like Windows does, then blur by shrinking hard and scaling back up in steps
    QImage fill = src.scaled(g.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    fill = fill.copy((fill.width() - g.width()) / 2, (fill.height() - g.height()) / 2, g.width(), g.height());
    const QSize tiny(qMax(8, g.width() / 28), qMax(6, g.height() / 28));
    const QImage blurred = fill.scaled(tiny, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .scaled(g.size() / 6, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .scaled(g.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .convertToFormat(QImage::Format_RGB32);
    m_backdrop->setMaterial(blurred, g);
    m_backdrop->setWindowPos(geometry().topLeft());
}

// ============================== sync ==============================

SyncData MainWindow::syncSnapshot() const {
    SyncData d;
    d.items = m_items;
    d.collections = m_collections;
    d.deleted = m_deleted;
    return d;
}

QString MainWindow::syncStatusText() const {
    if (!m_sync || !m_sync->enabled()) return {};
    if (m_sync->lastSync() <= 0) return "Sync has not run yet.";
    return QString("Last sync: %1").arg(QDateTime::fromSecsSinceEpoch(m_sync->lastSync()).toString("HH:mm:ss"));
}

void MainWindow::chooseSyncFolder() {
    QString start = qEnvironmentVariable("OneDrive");
    if (start.isEmpty()) start = QDir::homePath();
    const QString dir = QFileDialog::getExistingDirectory(this, "Choose a sync folder, such as a OneDrive folder", start);
    if (dir.isEmpty()) return;
    // use a LinkVault sub-folder unless the chosen folder already is one
    QString folder = dir;
    if (!QFile::exists(dir + "/LinkVault-sync.json") && QFileInfo(dir).fileName() != "LinkVault") folder = dir + "/LinkVault";
    if (!QDir().mkpath(folder)) { showToast("Could not create the folder.", 3000); return; }
    m_sync->setFolder(folder);
    QSettings().setValue("syncFolder", folder);
    SyncData remote;
    const bool hadRemote = m_sync->readRemote(remote);
    if (hadRemote) mergeRemote(remote);
    m_sync->writeNow();
    showToast(hadRemote ? "☁️ Folder sync enabled and remote links imported" : "☁️ Folder sync enabled", 3200);
}

void MainWindow::mergeRemote(const SyncData& d) {
    bool changed = false;
    QStringList toFetch;

    // 1) tombstones: newer deletes win over older edits
    for (auto it = d.deleted.begin(); it != d.deleted.end(); ++it) {
        const QString key = it.key();
        const qint64 t = it.value();
        if (m_deleted.value(key, 0) < t) m_deleted.insert(key, t);
        if (key.startsWith("col:")) {
            const int ci = collectionIndex(key.mid(4));
            if (ci >= 0 && m_collections[ci].modified <= t) {
                for (auto& b : m_items) if (b.collection == m_collections[ci].id) b.collection.clear();
                if (m_collectionFilter == m_collections[ci].id) m_collectionFilter.clear();
                m_collections.removeAt(ci);
                changed = true;
            }
            continue;
        }
        const int i = indexOf(key);
        if (i >= 0 && m_items[i].modified <= t) {
            if (BookmarkCard* c = m_cards.take(key)) { c->hide(); c->deleteLater(); }
            m_items.removeAt(i);
            changed = true;
        }
    }

    // 2) bookmarks: newest `modified` wins; nothing is ever dropped on a conflict
    for (const Bookmark& rb : d.items) {
        const qint64 tomb = m_deleted.value(rb.id, 0);
        if (tomb && tomb >= rb.modified) continue;
        const int i = indexOf(rb.id);
        if (i < 0) {
            bool dup = false;                              // same site added on both machines
            for (const auto& b : std::as_const(m_items)) if (urlKey(b.url) == urlKey(rb.url)) { dup = true; break; }
            if (dup) continue;
            m_items.append(rb);
            if (rb.imagePath.isEmpty() && rb.iconPath.isEmpty()) toFetch << rb.id;
            changed = true;
        } else if (rb.modified > m_items[i].modified) {
            Bookmark nb = rb;
            if (nb.imagePath.isEmpty()) nb.imagePath = m_items[i].imagePath;
            if (nb.iconPath.isEmpty()) nb.iconPath = m_items[i].iconPath;
            m_items[i] = nb;
            if (auto* c = m_cards.value(nb.id)) c->setBookmark(nb);
            changed = true;
        }
    }

    // 3) collections
    for (const Collection& rc : d.collections) {
        const qint64 tomb = m_deleted.value("col:" + rc.id, 0);
        if (tomb && tomb >= rc.modified) continue;
        const int ci = collectionIndex(rc.id);
        if (ci < 0) { m_collections << rc; changed = true; }
        else if (rc.modified > m_collections[ci].modified) { m_collections[ci] = rc; changed = true; }
    }
    if (!changed) return;

    for (auto& b : m_items)                                // drop links to collections that no longer exist
        if (!b.collection.isEmpty() && collectionIndex(b.collection) < 0) b.collection.clear();
    Storage::save(m_items);
    Storage::saveCollections(m_collections);
    Storage::saveDeleted(m_deleted);
    rehash();                                              // merged state is not a new local edit
    refreshCollections();
    if (!m_firstShow) refresh(true);
    m_sync->schedule();
    if (!m_firstShow) showToast("☁️ Bookmarks updated from another device", 2600);
    for (const auto& id : std::as_const(toFetch)) startFetch(id);
}
