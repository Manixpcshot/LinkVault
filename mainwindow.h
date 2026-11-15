#pragma once
#include <QMainWindow>
#include <QHash>
#include <QKeySequence>
#include <QPointer>
#include <QSet>
#include <QStringList>
#include "bookmark.h"
#include "popups.h"
#include "sync.h"

class QLabel;
class QMenu;
class QPropertyAnimation;
class QPushButton;
class QScrollArea;
class QSystemTrayIcon;
class QTimer;
class QVBoxLayout;
class BookmarkCard;
class AddCard;
class ThumbnailFetcher;
class Backdrop;
class TitleBar;
class SlideStack;
class SettingsPage;
struct SettingsState;
class SearchBox;
class SegmentedControl;
class CardGrid;
class CollectionBar;
class Toast;
class GlobalHotkeys;
class LinkChecker;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    void bringToFront();
    Q_INVOKABLE void toggleFullScreen();
    void showQuickAdd();
    void showPalette();

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override;
    void closeEvent(QCloseEvent* e) override;
    void changeEvent(QEvent* e) override;
    void showEvent(QShowEvent* e) override;
    void moveEvent(QMoveEvent* e) override;
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    QWidget* buildHome();
    void connectSettings();
    SettingsState settingsState() const;

    // view
    void refresh(bool animate);
    int matchScore(const Bookmark& b, const QStringList& tokens) const;
    BookmarkCard* ensureCard(const Bookmark& b);
    void smoothScrollTo(int value);
    void showToast(const QString& text, int msec = 2200);
    void updateSortButton();
    void showSortMenu();

    // pages / theme
    void openSettings();
    void goHome();
    bool resolveDark() const;
    void applyTheme(bool crossfade);

    // actions
    enum class AddResult { Added, Duplicate, Invalid };
    AddResult addBookmarkData(const QString& url, const QString& name, const QString& collection, QString* outName);
    void addBookmark();
    void onOpen(const QString& id);
    void onEdit(const QString& id);
    void onDelete(const QString& id);
    void onCopy(const QString& id);
    void togglePin(const QString& id);
    void undoDelete();
    void commitPendingDelete();
    void exportData();
    void importData();

    // persistence: saves everything, stamps `modified` on changed items, schedules sync
    void persist();
    void rehash();                                   // remember current state without stamping
    QByteArray itemHash(const Bookmark& b) const;

    // thumbnails (small queue so a big import doesn't open 100 connections)
    void startFetch(const QString& id);
    void pumpFetches();
    void onFetched(const QString& id, const QString& cover, const QString& icon, const QString& title);

    // collections
    void refreshCollections();
    int collectionIndex(const QString& id) const;
    QColor collectionColor(const QString& id) const;
    QString collectionLabel(const QString& id) const;
    void newCollection(const QString& assignBookmark = {});
    void editCollection(const QString& id);
    void deleteCollection(const QString& id);
    void moveToCollection(const QString& bookmarkId, const QString& collectionId);
    void showCollectionMenu(const QString& id, const QPoint& globalPos);
    void extendCardMenu(QMenu& menu, const QString& id);

    // link health & reminders
    void checkLinks(bool force);
    void onLinkChecked(const QString& id, int health, const QString& redirect);
    void setReminder(const QString& id, qint64 when);
    void checkReminders();

    // tray, global hotkeys, palette
    void setupTray();
    void quitApp();
    void applyHotkeys(bool report);
    QList<PaletteItem> paletteQuery(const QString& text);
    void onPaletteActivated(const PaletteItem& item, bool copyOnly);

    // Mica-style material & sync
    bool micaSupported() const;
    void updateMaterial();
    SyncData syncSnapshot() const;
    void mergeRemote(const SyncData& d);
    void chooseSyncFolder();
    QString syncStatusText() const;

    int indexOf(const QString& id) const;
    Qt::Edges edgesAt(const QPoint& pos) const;
    void updateFrameState();
    void applyNativeFrame();      // Windows: give the frameless window a real caption style -> DWM animations

    QList<Bookmark> m_items;
    QList<Collection> m_collections;
    QHash<QString, qint64> m_deleted;
    QHash<QString, QByteArray> m_hashes;
    QHash<QString, BookmarkCard*> m_cards;
    QStringList m_visibleOrder;
    QSet<QString> m_autoName, m_loading;
    QStringList m_fetchQueue;
    int m_activeFetches = 0;

    struct PendingDelete { Bookmark b; int index = -1; bool active = false; } m_pending;

    Backdrop* m_backdrop = nullptr;
    TitleBar* m_titleBar = nullptr;
    QVBoxLayout* m_outer = nullptr;
    SlideStack* m_stack = nullptr;
    SettingsPage* m_settings = nullptr;
    SearchBox* m_search = nullptr;
    QTimer* m_searchTimer = nullptr;
    SegmentedControl* m_filter = nullptr;
    CollectionBar* m_colBar = nullptr;
    QPushButton* m_sortBtn = nullptr;
    QLabel* m_resultLabel = nullptr;
    QLabel* m_count = nullptr;
    QPointer<QLabel> m_themeOverlay;
    QPointer<QPropertyAnimation> m_scrollMotion;
    QScrollArea* m_scroll = nullptr;
    CardGrid* m_grid = nullptr;
    QLabel* m_pinHeader = nullptr;
    QLabel* m_allHeader = nullptr;
    AddCard* m_addCard = nullptr;
    QWidget* m_emptyBox = nullptr;
    QLabel* m_emptyTitle = nullptr;
    QLabel* m_emptyText = nullptr;
    QPushButton* m_emptyBtn = nullptr;
    Toast* m_toast = nullptr;
    ThumbnailFetcher* m_fetcher = nullptr;
    LinkChecker* m_checker = nullptr;
    SyncManager* m_sync = nullptr;
    GlobalHotkeys* m_hotkeys = nullptr;
    QuickAddPanel* m_quickAdd = nullptr;
    CommandPalette* m_palette = nullptr;
    QSystemTrayIcon* m_tray = nullptr;
    QMenu* m_trayMenu = nullptr;
    QTimer* m_checkTimer = nullptr;
    QTimer* m_reminderTimer = nullptr;
    QTimer* m_materialTimer = nullptr;

    bool m_dark = false;
    bool m_nativeStyled = false;
    bool m_firstShow = true;
    bool m_wasMaximized = false;  // state to return to when leaving full screen
    int m_themeMode = 0;          // Theme::Mode, saved as an integer
    int m_sort = 0;               // 0 newest, 1 oldest, 2 name, 3 most opened, 4 recently opened
    int m_filterMode = 0;         // 0 all, 1 pinned only, 2 links with problems
    QString m_collectionFilter;   // empty = all collections
    bool m_minimizeOnOpen = false;
    bool m_useClipboard = true;
    bool m_mica = false;
    bool m_closeToTray = false;
    bool m_trayHintShown = false;
    bool m_quitting = false;
    bool m_linkCheck = true;
    int m_checkFound = 0;         // problems found in the running check (for the summary toast)
    bool m_checkManual = false;
    QKeySequence m_quickKey, m_paletteKey;
    QString m_lastNotified;       // bookmark id of the last reminder balloon
};
