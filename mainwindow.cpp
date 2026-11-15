#include "mainwindow.h"
#include "backdrop.h"
#include "titlebar.h"
#include "card.h"
#include "dialogs.h"
#include "fetcher.h"
#include "theme.h"
#include "autostart.h"
#include "platform.h"
#include "widgets.h"
#include "settingspage.h"
#include "hotkey.h"
#include "linkchecker.h"
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QCloseEvent>
#include <QDateTime>
#include <QDesktopServices>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QMoveEvent>
#include <QMessageBox>
#include <QScreen>
#include <QSystemTrayIcon>
#include <QCryptographicHash>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScrollArea>
#include <QScroller>
#include <QScrollBar>
#include <QSettings>
#include <QShortcut>
#include <QShowEvent>
#include <QStyleHints>
#include <QTextDocumentFragment>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QWindow>
#include <algorithm>
#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  include <dwmapi.h>
#  define LV_X_LPARAM(lp) (int(short(LOWORD(lp))))      // = GET_X_LPARAM, without pulling in windowsx.h
#  define LV_Y_LPARAM(lp) (int(short(HIWORD(lp))))
#  ifndef SM_CXPADDEDBORDER
#    define SM_CXPADDEDBORDER 92
#  endif
// Per-monitor-DPI aware metrics (Windows 10 1607+), with a fallback for older systems
static UINT lvDpiFor(HWND h) {
    using Fn = UINT(WINAPI*)(HWND);
    static Fn fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    return fn ? fn(h) : 96;
}
static int lvMetric(int index, UINT dpi) {
    using Fn = int(WINAPI*)(int, UINT);
    static Fn fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetSystemMetricsForDpi")));
    return fn ? fn(index, dpi) : GetSystemMetrics(index);
}
#endif

static constexpr int kResizeBorder = 6;
static constexpr int kMaxParallelFetches = 4;

static const char* kSortNames[] = {"Newest first", "Oldest first", "Name", "Most visited", "Recently opened"};

// Persian-friendly normalisation for search: Arabic yeh/kaf → Persian, drop ZWNJ, lowercase
static QString norm(QString s) {
    s = s.toLower();
    s.replace(QChar(0x064A), QChar(0x06CC));
    s.replace(QChar(0x0643), QChar(0x06A9));
    s.remove(QChar(0x200C));
    return s;
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("LinkVault");
    setWindowIcon(QIcon(":/icon.png"));
    if (Platform::customFrame)
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint | Qt::WindowSystemMenuHint);
    if (!Platform::mobile) setMinimumSize(700, 480);

    QSettings st;
    const int savedMode = st.value("themeMode", st.value("darkMode", false).toBool() ? Theme::Dark : Theme::Light).toInt();
    m_themeMode = qBound(int(Theme::Light), savedMode, int(Theme::Black));
    Theme::animRef() = st.value("animations", true).toBool();
    m_minimizeOnOpen = !Platform::mobile && st.value("minimizeOnOpen", false).toBool();
    m_useClipboard = st.value("useClipboard", !Platform::mobile).toBool();
    m_sort = qBound(0, st.value("sort", 0).toInt(), 4);
    m_mica = st.value("mica", false).toBool();
    m_closeToTray = st.value("closeToTray", false).toBool();
    m_trayHintShown = st.value("trayHintShown", false).toBool();
    m_linkCheck = st.value("linkCheck", true).toBool();
    m_quickKey = QKeySequence(st.value("hotkeyQuickAdd", "Ctrl+Alt+L").toString());
    m_paletteKey = QKeySequence(st.value("hotkeyPalette", "Ctrl+Alt+K").toString());
    m_collectionFilter = st.value("collectionFilter").toString();
    m_dark = resolveDark();
    Theme::darkRef() = m_dark;
    Theme::blackRef() = m_themeMode == Theme::Black;
    if (!Platform::mobile && !restoreGeometry(st.value("geometry").toByteArray())) resize(1200, 780);

    m_fetcher = new ThumbnailFetcher(this);
    connect(m_fetcher, &ThumbnailFetcher::finished, this, &MainWindow::onFetched);

    m_backdrop = new Backdrop;
    m_backdrop->setMouseTracking(true);          // edge-resize cursor
    if (Platform::customFrame) m_backdrop->installEventFilter(this);
    m_outer = new QVBoxLayout(m_backdrop);
    m_outer->setContentsMargins(kResizeBorder, kResizeBorder, kResizeBorder, kResizeBorder);
    m_outer->setSpacing(0);
    m_titleBar = new TitleBar(this);
    if (Platform::customFrame) m_outer->addWidget(m_titleBar);
    else m_titleBar->hide();

    m_stack = new SlideStack;
    m_outer->addWidget(m_stack, 1);
    m_stack->addPage(buildHome());
    m_settings = new SettingsPage;
    m_stack->addPage(m_settings);
    connectSettings();
    setCentralWidget(m_backdrop);

    m_toast = new Toast(m_backdrop);
    connect(m_toast, &Toast::actionTriggered, this, &MainWindow::undoDelete);
    connect(m_toast, &Toast::timedOut, this, &MainWindow::commitPendingDelete);

    // ---- shortcuts ----
    new QShortcut(QKeySequence::New, this, [this] { goHome(); addBookmark(); });
    new QShortcut(QKeySequence::Find, this, [this] { goHome(); m_search->setFocus(); m_search->selectAll(); });
    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Comma), this, [this] {
        if (m_stack->current() == 1) goHome(); else openSettings();
    });
    new QShortcut(QKeySequence::Undo, this, [this] { if (!m_search->hasFocus()) undoDelete(); });
    new QShortcut(QKeySequence(Qt::Key_Escape), this, [this] {
        if (m_stack->current() == 1) { goHome(); return; }
        if (!m_search->text().isEmpty()) m_search->clear();
        else if (m_search->hasFocus()) m_search->clearFocus();
        else if (isFullScreen()) toggleFullScreen();
    });
    new QShortcut(QKeySequence(Qt::Key_F11), this, [this] { toggleFullScreen(); });

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this, [this] {
        if (m_themeMode == 2) applyTheme(true);
    });
#endif

    new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this, [this] { showPalette(); });

    m_items = Storage::load();
    m_collections = Storage::loadCollections();
    m_deleted = Storage::loadDeleted();
    if (collectionIndex(m_collectionFilter) < 0) m_collectionFilter.clear();
    rehash();

    // ---- v3 services ----
    BookmarkCard::menuHook() = [this](QMenu& m, const QString& id) { extendCardMenu(m, id); };

    m_checker = new LinkChecker(this);
    connect(m_checker, &LinkChecker::checked, this, &MainWindow::onLinkChecked);
    connect(m_checker, &LinkChecker::idle, this, [this] {
        persist();
        refresh(true);
        const QString msg = m_checkFound > 0 ? QString("🔍 Check complete · %1 with issues").arg(m_checkFound)
                                             : QString("✓ Check complete · all links are healthy");
        if (m_checkManual || m_checkFound > 0) showToast(msg, 3500);
        m_settings->setLinkStatus(msg);
        m_checkManual = false;
    });
    m_checkTimer = new QTimer(this);
    m_checkTimer->setInterval(60 * 60 * 1000);                // re-evaluate hourly, each link max once a day
    connect(m_checkTimer, &QTimer::timeout, this, [this] { checkLinks(false); });
    m_checkTimer->start();
    QTimer::singleShot(25000, this, [this] { checkLinks(false); });

    m_reminderTimer = new QTimer(this);
    m_reminderTimer->setInterval(20000);
    connect(m_reminderTimer, &QTimer::timeout, this, &MainWindow::checkReminders);
    m_reminderTimer->start();

    m_materialTimer = new QTimer(this);
    m_materialTimer->setSingleShot(true);
    m_materialTimer->setInterval(300);
    connect(m_materialTimer, &QTimer::timeout, this, &MainWindow::updateMaterial);

    m_quickAdd = new QuickAddPanel;
    connect(m_quickAdd, &QuickAddPanel::submitted, this, [this](const QString& url, const QString& name, const QString& col) {
        QString shown;
        const AddResult r = addBookmarkData(url, name, col, &shown);
        if (r == AddResult::Added) m_quickAdd->showSaved(shown);
        else if (r == AddResult::Duplicate) m_quickAdd->showError(QString("“%1” is already saved").arg(shown));
        else m_quickAdd->showError("Enter a valid web address.");
    });
    m_palette = new CommandPalette;
    m_palette->setProvider([this](const QString& q) { return paletteQuery(q); });
    connect(m_palette, &CommandPalette::activated, this, &MainWindow::onPaletteActivated);

    m_hotkeys = new GlobalHotkeys(this);
    connect(m_hotkeys, &GlobalHotkeys::activated, this, [this](int id) {
        if (id == GlobalHotkeys::QuickAdd) showQuickAdd();
        else if (id == GlobalHotkeys::Palette) showPalette();
    });
    applyHotkeys(false);
    setupTray();

    m_sync = new SyncManager(this);
    m_sync->setProvider([this] { return syncSnapshot(); });
    connect(m_sync, &SyncManager::remoteChanged, this, &MainWindow::mergeRemote);
    connect(m_sync, &SyncManager::statusChanged, this, [this] {
        m_settings->setSyncStatus(m_sync->folder(), syncStatusText());
    });
    const QString syncFolder = st.value("syncFolder").toString();
    if (!syncFolder.isEmpty() && QDir(syncFolder).exists()) {
        m_sync->setFolder(syncFolder);
        SyncData remote;
        if (m_sync->readRemote(remote)) mergeRemote(remote);
        m_sync->schedule();
    }

    refreshCollections();
    applyTheme(false);
}

QWidget* MainWindow::buildHome() {
    auto* page = new QWidget;
    page->setObjectName("page");
    auto* root = new QVBoxLayout(page);
    const int padding = Platform::mobile ? 12 : 20;
    root->setContentsMargins(padding, padding, padding, 4);
    root->setSpacing(Platform::mobile ? 8 : 14);

    // ---- header ----
    auto* header = new QHBoxLayout;
    header->setSpacing(10);
    header->setContentsMargins(0, 0, 0, 0);
    auto* logo = new QLabel;
    logo->setPixmap(QIcon(":/icon.png").pixmap(44, 44));
    auto* titleBox = new QVBoxLayout;
    titleBox->setSpacing(0);
    auto* title = new QLabel("LinkVault");
    title->setObjectName("appTitle");
    m_count = new QLabel;
    m_count->setObjectName("muted");
    titleBox->addWidget(title);
    titleBox->addWidget(m_count);
    header->addWidget(logo);
    header->addSpacing(4);
    header->addLayout(titleBox);
    header->addStretch();

    m_search = new SearchBox;
    m_search->setPlaceholderText("Search title or URL");
    if (Platform::mobile) m_search->setStretchable();
    else m_search->setWidths(200, 250);
    m_searchTimer = new QTimer(this);
    m_searchTimer->setSingleShot(true);
    m_searchTimer->setInterval(110);                    // debounce typing
    connect(m_searchTimer, &QTimer::timeout, this, [this] { refresh(true); });
    connect(m_search, &QLineEdit::textChanged, this, [this] { m_searchTimer->start(); });
    connect(m_search, &QLineEdit::returnPressed, this, [this] {
        m_searchTimer->stop();
        refresh(true);
        if (!m_visibleOrder.isEmpty()) onOpen(m_visibleOrder.first());
    });

    auto* addBtn = new QPushButton("+ Add link");
    addBtn->setObjectName("primary");
    if (Platform::mobile) addBtn->setMinimumHeight(44);
    addBtn->setCursor(Qt::PointingHandCursor);
    connect(addBtn, &QPushButton::clicked, this, &MainWindow::addBookmark);

    auto* setBtn = new QPushButton("⚙");
    setBtn->setObjectName("iconBtn");
    setBtn->setToolTip("Settings (Ctrl+,)");
    setBtn->setAccessibleName("Settings");
    setBtn->setCursor(Qt::PointingHandCursor);
    connect(setBtn, &QPushButton::clicked, this, &MainWindow::openSettings);

    if (!Platform::mobile) header->addWidget(m_search);
    header->addWidget(addBtn);
    header->addWidget(setBtn);
    root->addLayout(header);
    if (Platform::mobile) root->addWidget(m_search);

    // ---- collections ----
    m_colBar = new CollectionBar;
    connect(m_colBar, &CollectionBar::currentChanged, this, [this](const QString& id) {
        m_collectionFilter = id;
        QSettings().setValue("collectionFilter", id);
        refresh(true);
        smoothScrollTo(0);
    });
    connect(m_colBar, &CollectionBar::addRequested, this, [this] { newCollection(); });
    connect(m_colBar, &CollectionBar::dropped, this, &MainWindow::moveToCollection, Qt::QueuedConnection);
    connect(m_colBar, &CollectionBar::menuRequested, this, &MainWindow::showCollectionMenu);
    auto* colRow = new QHBoxLayout;
    colRow->setContentsMargins(0, 0, 0, 0);
    colRow->addWidget(m_colBar);
    root->addLayout(colRow);

    // ---- filter / sort row ----
    auto* bar = new QHBoxLayout;
    bar->setContentsMargins(0, 0, 0, 0);
    bar->setSpacing(10);
    m_filter = new SegmentedControl({"All", "📌 Pinned", "⚠️ Issues"});
    connect(m_filter, &SegmentedControl::currentChanged, this, [this](int i) {
        m_filterMode = i;
        refresh(true);
        smoothScrollTo(0);
    });
    m_resultLabel = new QLabel(page);
    m_resultLabel->setObjectName("resultLabel");
    m_sortBtn = new QPushButton;
    m_sortBtn->setObjectName("sortBtn");
    m_sortBtn->setCursor(Qt::PointingHandCursor);
    connect(m_sortBtn, &QPushButton::clicked, this, &MainWindow::showSortMenu);
    updateSortButton();
    bar->addWidget(m_filter);
    if (Platform::mobile) {
        m_resultLabel->hide();
    } else {
        bar->addSpacing(6);
        bar->addWidget(m_resultLabel);
    }
    bar->addStretch();
    bar->addWidget(m_sortBtn);
    root->addLayout(bar);

    // ---- grid ----
    m_scroll = new QScrollArea;
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setWidgetResizable(true);
    m_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scroll->viewport()->setAutoFillBackground(false);
    if (Platform::mobile) QScroller::grabGesture(m_scroll->viewport(), QScroller::TouchGesture);

    auto* holder = new QWidget;
    holder->setObjectName("gridHolder");
    auto* hl = new QVBoxLayout(holder);
    hl->setContentsMargins(0, 0, 0, 0);
    hl->setSpacing(0);

    m_grid = new CardGrid(QSize(BookmarkCard::kW, BookmarkCard::kH));
    auto makeHeader = [this] {
        auto* l = new QLabel(m_grid);
        l->setObjectName("gridSection");
        l->setContentsMargins(BookmarkCard::kMargin + 4, 10, BookmarkCard::kMargin + 4, 0);
        l->setFixedHeight(40);
        l->hide();
        return l;
    };
    m_pinHeader = makeHeader();
    m_allHeader = makeHeader();
    m_addCard = new AddCard(m_dark, m_grid);
    m_addCard->hide();
    connect(m_addCard, &AddCard::clicked, this, &MainWindow::addBookmark, Qt::QueuedConnection);

    m_emptyBox = new QWidget;
    m_emptyBox->setObjectName("emptyBox");
    auto* el = new QVBoxLayout(m_emptyBox);
    el->setContentsMargins(0, 60, 0, 0);
    el->setSpacing(10);
    auto* emptyIcon = new QLabel;
    emptyIcon->setPixmap(QIcon(":/icon.png").pixmap(96, 96));
    emptyIcon->setAlignment(Qt::AlignCenter);
    m_emptyTitle = new QLabel;
    m_emptyTitle->setObjectName("emptyTitle");
    m_emptyTitle->setAlignment(Qt::AlignCenter);
    m_emptyTitle->setWordWrap(true);
    m_emptyText = new QLabel;
    m_emptyText->setObjectName("emptyText");
    m_emptyText->setAlignment(Qt::AlignCenter);
    m_emptyText->setWordWrap(true);
    m_emptyBtn = new QPushButton("+ Add your first link");
    m_emptyBtn->setObjectName("primary");
    m_emptyBtn->setCursor(Qt::PointingHandCursor);
    connect(m_emptyBtn, &QPushButton::clicked, this, &MainWindow::addBookmark);
    el->addWidget(emptyIcon);
    el->addSpacing(6);
    el->addWidget(m_emptyTitle);
    el->addWidget(m_emptyText);
    el->addSpacing(8);
    el->addWidget(m_emptyBtn, 0, Qt::AlignHCenter);

    hl->addWidget(m_grid);
    hl->addWidget(m_emptyBox);
    hl->addStretch();
    m_scroll->setWidget(holder);
    root->addWidget(m_scroll, 1);
    return page;
}

void MainWindow::connectSettings() {
    connect(m_settings, &SettingsPage::backRequested, this, &MainWindow::goHome);
    connect(m_settings, &SettingsPage::themeModeChanged, this, [this](int mode) {
        m_themeMode = mode;
        QSettings().setValue("themeMode", mode);
        applyTheme(true);
        m_settings->sync(settingsState());
    });
    connect(m_settings, &SettingsPage::autoStartChanged, this, [](bool on) { AutoStart::setEnabled(on); });
    connect(m_settings, &SettingsPage::animationsChanged, this, [](bool on) {
        Theme::animRef() = on;
        QSettings().setValue("animations", on);
    });
    connect(m_settings, &SettingsPage::minimizeOnOpenChanged, this, [this](bool on) {
        m_minimizeOnOpen = on;
        QSettings().setValue("minimizeOnOpen", on);
    });
    connect(m_settings, &SettingsPage::clipboardChanged, this, [this](bool on) {
        m_useClipboard = on;
        QSettings().setValue("useClipboard", on);
    });
    connect(m_settings, &SettingsPage::micaChanged, this, [this](bool on) {
        m_mica = on;
        QSettings().setValue("mica", on);
        updateMaterial();
    });
    connect(m_settings, &SettingsPage::closeToTrayChanged, this, [this](bool on) {
        m_closeToTray = on;
        QSettings().setValue("closeToTray", on);
    });
    connect(m_settings, &SettingsPage::linkCheckChanged, this, [this](bool on) {
        m_linkCheck = on;
        QSettings().setValue("linkCheck", on);
        if (on) checkLinks(false);
    });
    connect(m_settings, &SettingsPage::checkLinksNowRequested, this, [this] { checkLinks(true); });
    connect(m_settings, &SettingsPage::quickAddKeyChanged, this, [this](const QKeySequence& k) {
        m_quickKey = k;
        QSettings().setValue("hotkeyQuickAdd", k.toString());
        applyHotkeys(true);
    });
    connect(m_settings, &SettingsPage::paletteKeyChanged, this, [this](const QKeySequence& k) {
        m_paletteKey = k;
        QSettings().setValue("hotkeyPalette", k.toString());
        applyHotkeys(true);
    });
    connect(m_settings, &SettingsPage::syncChooseRequested, this, &MainWindow::chooseSyncFolder);
    connect(m_settings, &SettingsPage::syncNowRequested, this, [this] {
        SyncData remote;
        if (m_sync->readRemote(remote)) mergeRemote(remote);
        m_sync->writeNow();
        showToast("☁️ Sync complete");
    });
    connect(m_settings, &SettingsPage::syncDisableRequested, this, [this] {
        m_sync->setFolder({});
        QSettings().remove("syncFolder");
        showToast("Folder sync is off. Existing files are kept.", 3000);
    });
    connect(m_settings, &SettingsPage::openFolderRequested, this, [] {
        QDesktopServices::openUrl(QUrl::fromLocalFile(Storage::dataDir()));
    });
    connect(m_settings, &SettingsPage::exportRequested, this, &MainWindow::exportData);
    connect(m_settings, &SettingsPage::importRequested, this, &MainWindow::importData);
}

// ============================== window chrome ==============================

void MainWindow::bringToFront() {
    if (!isVisible()) {                       // hidden in the tray
        show();
        raise();
        activateWindow();
        return;
    }
#ifdef Q_OS_WIN
    // SC_RESTORE plays the native restore animation and keeps a maximized window maximized
    if (isMinimized()) PostMessageW(reinterpret_cast<HWND>(winId()), WM_SYSCOMMAND, SC_RESTORE, 0);
    else show();
#else
    if (isMinimized()) showNormal(); else show();
#endif
    raise();
    activateWindow();
}

// F11 / Esc. Windows has no native animation for full screen, so we fake a Windows 11
// style "zoom": the old content is snapshotted and scales into the new size while fading.
void MainWindow::toggleFullScreen() {
    const bool anim = Theme::animations() && isVisible() && !isMinimized();
    const QRect oldGeom = geometry();
    QPixmap shot;
    if (anim) shot = m_backdrop->grab();

    if (isFullScreen()) {
        if (m_wasMaximized) showMaximized(); else showNormal();
    } else {
        m_wasMaximized = isMaximized();
        showFullScreen();
    }
    if (!anim) return;
    QTimer::singleShot(0, this, [this, shot, oldGeom] {
        const QRect now = geometry();
        QRectF from = QRectF(oldGeom.translated(-now.topLeft()));
        if (from.width() > now.width()) {
            // leaving full screen: start slightly larger and settle into place
            const QSizeF s = QSizeF(now.size()) * 1.035;
            from = QRectF(QPointF(0, 0), s);
            from.moveCenter(QRectF(QRect(QPoint(0, 0), now.size())).center());
        }
        new ZoomOverlay(m_backdrop, shot, from, 420);
    });
}

void MainWindow::closeEvent(QCloseEvent* e) {
    commitPendingDelete();
    QSettings().setValue("geometry", saveGeometry());
    if (m_closeToTray && !m_quitting && m_tray && m_tray->isVisible()) {
        e->ignore();
        hide();                                    // DWM still plays the close-style hide animation
        if (!m_trayHintShown) {
            m_trayHintShown = true;
            QSettings().setValue("trayHintShown", true);
            m_tray->showMessage("LinkVault is still running",
                                "Open LinkVault from the system tray. Shortcuts and reminders remain active.",
                                QSystemTrayIcon::Information, 4000);
        }
        return;
    }
    QMainWindow::closeEvent(e);
    quitApp();
}

void MainWindow::moveEvent(QMoveEvent* e) {
    QMainWindow::moveEvent(e);
    if (m_backdrop) m_backdrop->setWindowPos(geometry().topLeft());
}

void MainWindow::keyPressEvent(QKeyEvent* e) {
    if (Platform::mobile && e->key() == Qt::Key_Back) {
        if (m_stack->current() == 1) {
            goHome();
            return;
        }
        if (!m_search->text().isEmpty()) {
            m_search->clear();
            return;
        }
    }
    QMainWindow::keyPressEvent(e);
}

Qt::Edges MainWindow::edgesAt(const QPoint& pos) const {
    Qt::Edges e;
    if (isMaximized() || isFullScreen()) return e;
    const int b = kResizeBorder;
    if (pos.x() < b) e |= Qt::LeftEdge;
    if (pos.x() >= width() - b) e |= Qt::RightEdge;
    if (pos.y() < b) e |= Qt::TopEdge;
    if (pos.y() >= height() - b) e |= Qt::BottomEdge;
    return e;
}

bool MainWindow::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == m_backdrop) {
        if (ev->type() == QEvent::MouseMove) {
            const Qt::Edges e = edgesAt(static_cast<QMouseEvent*>(ev)->position().toPoint());
            Qt::CursorShape shape = Qt::ArrowCursor;
            if (e == (Qt::LeftEdge | Qt::TopEdge) || e == (Qt::RightEdge | Qt::BottomEdge)) shape = Qt::SizeFDiagCursor;
            else if (e == (Qt::RightEdge | Qt::TopEdge) || e == (Qt::LeftEdge | Qt::BottomEdge)) shape = Qt::SizeBDiagCursor;
            else if (e & (Qt::LeftEdge | Qt::RightEdge)) shape = Qt::SizeHorCursor;
            else if (e & (Qt::TopEdge | Qt::BottomEdge)) shape = Qt::SizeVerCursor;
            m_backdrop->setCursor(shape);
        } else if (ev->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(ev);
            const Qt::Edges e = edgesAt(me->position().toPoint());
            if (me->button() == Qt::LeftButton && e && windowHandle()) {
                windowHandle()->startSystemResize(e);
                return true;
            }
        } else if (ev->type() == QEvent::Leave) {
            m_backdrop->unsetCursor();
        }
    }
    return QMainWindow::eventFilter(obj, ev);
}

void MainWindow::updateFrameState() {
    const bool max = isMaximized() || isFullScreen();
    const int b = max || !Platform::customFrame ? 0 : kResizeBorder;
    m_outer->setContentsMargins(b, b, b, b);
    m_backdrop->setFramed(Platform::customFrame && !max);
    m_titleBar->setMaximized(max);
    if (max) m_backdrop->unsetCursor();
}

void MainWindow::changeEvent(QEvent* e) {
    if (e->type() == QEvent::WindowStateChange && m_titleBar) {
        updateFrameState();
        if (!isFullScreen()) applyNativeFrame();     // Qt swaps the style for full screen; put ours back
    }
    QMainWindow::changeEvent(e);
}

void MainWindow::showEvent(QShowEvent* e) {
    QMainWindow::showEvent(e);
    updateFrameState();
    if (m_firstShow) {
        m_firstShow = false;
        // first real layout once the window has its size -> staggered card entrance
        QTimer::singleShot(0, this, [this] { refresh(true); refreshCollections(); });
    }
#ifdef Q_OS_WIN
    if (!m_nativeStyled) {
        m_nativeStyled = true;
        applyNativeFrame();
        if (QWindow* wh = windowHandle())
            connect(wh, &QWindow::screenChanged, this, [this] { m_materialTimer->start(); });
        updateMaterial();
        const DWORD kCornerPref = 33;   // DWMWA_WINDOW_CORNER_PREFERENCE
        const int kRound = 2;           // DWMWCP_ROUND
        DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), kCornerPref, &kRound, sizeof(kRound));
    }
#endif
}

// ---- Windows 11 native window animations ----
// A Qt::FramelessWindowHint window has no WS_CAPTION / WS_THICKFRAME, and DWM only plays the
// open / close / minimize / maximize / restore animations for windows that have them.
// So we add those styles back and swallow the non-client area in WM_NCCALCSIZE: the window
// still looks frameless but Windows treats it like a normal app window (animations, snap,
// taskbar click to minimize, Win+Up/Down, shadow).
void MainWindow::applyNativeFrame() {
#ifdef Q_OS_WIN
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
    if ((style & WS_POPUP) && isFullScreen()) return;
    const LONG_PTR want = WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
    if ((style & want) != want) {
        SetWindowLongPtrW(hwnd, GWL_STYLE, style | want);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE);
    }
    const MARGINS shadow{0, 0, 0, 1};   // keeps the DWM drop shadow
    DwmExtendFrameIntoClientArea(hwnd, &shadow);
#endif
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_NCCALCSIZE && msg->wParam == TRUE) {
        // whole window = client area (no classic title bar / borders drawn)
        const LONG_PTR style = GetWindowLongPtrW(msg->hwnd, GWL_STYLE);
        if (IsZoomed(msg->hwnd) && (style & WS_CAPTION)) {
            // a maximized window hangs off-screen by the frame size; pull the content back in
            auto* p = reinterpret_cast<NCCALCSIZE_PARAMS*>(msg->lParam);
            const UINT dpi = lvDpiFor(msg->hwnd);
            const int pad = lvMetric(SM_CXPADDEDBORDER, dpi);
            const int fx = lvMetric(SM_CXSIZEFRAME, dpi) + pad;
            const int fy = lvMetric(SM_CYSIZEFRAME, dpi) + pad;
            p->rgrc[0].left += fx;
            p->rgrc[0].right -= fx;
            p->rgrc[0].top += fy;
            p->rgrc[0].bottom -= fy;
        }
        *result = 0;
        return true;
    }
    if (msg->message == WM_NCHITTEST) {
        // Windows-side hit testing: native resize edges, and HTMAXBUTTON over our maximize
        // button so Windows 11 shows the Snap Layouts flyout when you hover it.
        POINT pt{LV_X_LPARAM(msg->lParam), LV_Y_LPARAM(msg->lParam)};
        ScreenToClient(msg->hwnd, &pt);
        const qreal dpr = devicePixelRatioF();
        const QPoint pos(qRound(pt.x / dpr), qRound(pt.y / dpr));
        if (!isMaximized() && !isFullScreen()) {
            const int b = kResizeBorder;
            const bool l = pos.x() < b, r = pos.x() >= width() - b, t = pos.y() < b, btm = pos.y() >= height() - b;
            LRESULT hit = 0;
            if (t && l) hit = HTTOPLEFT; else if (t && r) hit = HTTOPRIGHT;
            else if (btm && l) hit = HTBOTTOMLEFT; else if (btm && r) hit = HTBOTTOMRIGHT;
            else if (l) hit = HTLEFT; else if (r) hit = HTRIGHT;
            else if (t) hit = HTTOP; else if (btm) hit = HTBOTTOM;
            if (hit) { m_titleBar->maxButton()->setNcHover(false); *result = hit; return true; }
        }
        if (!isFullScreen() && m_titleBar->isVisible() && m_titleBar->maxButtonRect().contains(pos)) {
            *result = HTMAXBUTTON;
            return true;
        }
        m_titleBar->maxButton()->setNcHover(false);
        return false;                                  // client area: let Qt handle it
    }
    if (msg->message == WM_NCMOUSEMOVE) {
        const bool over = msg->wParam == HTMAXBUTTON;
        m_titleBar->maxButton()->setNcHover(over);
        if (over) {
            TRACKMOUSEEVENT tme{sizeof(TRACKMOUSEEVENT), TME_LEAVE | TME_NONCLIENT, msg->hwnd, 0};
            TrackMouseEvent(&tme);
            *result = 0;                                   // don't let Windows paint a classic button
            return true;
        }
    }
    if (msg->message == WM_NCMOUSELEAVE) m_titleBar->maxButton()->setNcHover(false);
    if (msg->message == WM_NCLBUTTONDOWN || msg->message == WM_NCLBUTTONDBLCLK) {
        if (msg->wParam == HTMAXBUTTON) {                // swallow, or Windows draws a classic button
            m_titleBar->maxButton()->setNcDown(true);
            *result = 0;
            return true;
        }
    }
    if (msg->message == WM_NCLBUTTONUP && msg->wParam == HTMAXBUTTON) {
        m_titleBar->maxButton()->setNcDown(false);
        m_titleBar->toggleMax();
        *result = 0;
        return true;
    }
    if (msg->message == WM_SETTINGCHANGE && msg->wParam == SPI_SETDESKWALLPAPER) {
        if (m_mica) m_materialTimer->start();             // wallpaper changed: rebuild the material
    }
    if (msg->message == WM_NCACTIVATE) {
        // stop Windows from repainting a classic frame when focus changes
        *result = DefWindowProcW(msg->hwnd, msg->message, msg->wParam, -1);
        return true;
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

// ============================== theme & pages ==============================

bool MainWindow::resolveDark() const {
    if (m_themeMode == Theme::Dark || m_themeMode == Theme::Black) return true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (m_themeMode == 2) return qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#endif
    return false;
}

void MainWindow::applyTheme(bool crossfade) {
    const bool dark = resolveDark();
    const bool black = m_themeMode == Theme::Black;
    QLabel* shot = nullptr;
    if (m_themeOverlay) delete m_themeOverlay.data();
    if (crossfade && Theme::animations() && isVisible() && (dark != m_dark || black != Theme::isBlack())) {
        // snapshot the old look and fade it out over the new one
        shot = new QLabel(m_backdrop);
        shot->setPixmap(m_backdrop->grab());
        shot->setGeometry(m_backdrop->rect());
        shot->setAttribute(Qt::WA_TransparentForMouseEvents);
        shot->show();
        shot->raise();
        m_themeOverlay = shot;
    }
    m_dark = dark;
    Theme::darkRef() = dark;
    Theme::blackRef() = black;
    qApp->setStyleSheet(Theme::styleSheet(dark));
    m_backdrop->setDark(dark);
    updateMaterial();
    m_titleBar->setDark(dark);
    m_search->setDark(dark);
    for (auto* c : std::as_const(m_cards)) c->setDark(dark);
    m_addCard->setDark(dark);
    m_backdrop->update();
    if (m_colBar) m_colBar->update();
#ifdef Q_OS_WIN
    if (m_nativeStyled) {
        const BOOL darkAttr = dark ? TRUE : FALSE;     // DWMWA_USE_IMMERSIVE_DARK_MODE: dark border/shadow on Win11
        DwmSetWindowAttribute(reinterpret_cast<HWND>(winId()), 20, &darkAttr, sizeof(darkAttr));
    }
#endif

    if (shot) {
        auto* eff = new QGraphicsOpacityEffect(shot);
        shot->setGraphicsEffect(eff);
        auto* a = new QPropertyAnimation(eff, "opacity", shot);
        a->setDuration(220);
        a->setEasingCurve(Theme::easeInOut());
        a->setStartValue(1.0);
        a->setEndValue(0.0);
        connect(a, &QPropertyAnimation::finished, shot, &QObject::deleteLater);
        a->start();
    }
    if (!m_firstShow) refresh(false);
}

void MainWindow::openSettings() {
    int pinned = 0;
    for (const auto& b : m_items) pinned += b.pinned ? 1 : 0;
    Q_UNUSED(pinned);
    m_settings->sync(settingsState());
    m_stack->slideTo(1);
}

void MainWindow::goHome() { m_stack->slideTo(0); }

void MainWindow::smoothScrollTo(int value) {
    QScrollBar* sb = m_scroll->verticalScrollBar();
    if (m_scrollMotion) m_scrollMotion->stop();
    if (!Theme::animations() || !isVisible() || sb->value() == value) { sb->setValue(value); return; }
    auto* a = new QPropertyAnimation(sb, "value", sb);
    m_scrollMotion = a;
    a->setDuration(260);
    a->setEasingCurve(Theme::easeOut());
    a->setStartValue(sb->value());
    a->setEndValue(value);
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::showToast(const QString& text, int msec) {
    commitPendingDelete();                      // a plain message replaces any "undo" toast
    m_toast->showMessage(text, {}, msec);
}

void MainWindow::updateSortButton() {
    const auto sort = QString::fromUtf8(kSortNames[m_sort]);
    m_sortBtn->setText(Platform::mobile ? QStringLiteral("⇅") : QString("⇅  %1").arg(sort));
    m_sortBtn->setToolTip(QString("Sort: %1").arg(sort));
    m_sortBtn->setAccessibleName(m_sortBtn->toolTip());
}

void MainWindow::showSortMenu() {
    QMenu menu(this);
    auto* group = new QActionGroup(&menu);
    for (int i = 0; i < 5; ++i) {
        QAction* a = menu.addAction(QString::fromUtf8(kSortNames[i]));
        a->setCheckable(true);
        a->setChecked(i == m_sort);
        a->setData(i);
        group->addAction(a);
    }
    QAction* chosen = menu.exec(m_sortBtn->mapToGlobal(QPoint(0, m_sortBtn->height() + 6)));
    if (!chosen) return;
    m_sort = chosen->data().toInt();
    QSettings().setValue("sort", m_sort);
    updateSortButton();
    refresh(true);
}

// ============================== view ==============================

int MainWindow::indexOf(const QString& id) const {
    for (int i = 0; i < m_items.size(); ++i)
        if (m_items[i].id == id) return i;
    return -1;
}

int MainWindow::matchScore(const Bookmark& b, const QStringList& tokens) const {
    if (tokens.isEmpty()) return 0;
    const QString name = norm(b.name), url = norm(b.url);
    QString host = norm(QUrl(b.url).host());
    if (host.startsWith("www.")) host = host.mid(4);
    int total = 0;
    for (const QString& t : tokens) {               // every word must match somewhere
        int s = -1;
        if (name.startsWith(t)) s = 40;
        else if (name.contains(t)) s = 25;
        else if (host.startsWith(t)) s = 20;
        else if (host.contains(t)) s = 12;
        else if (url.contains(t)) s = 5;
        if (s < 0) return -1;
        total += s;
    }
    return total;
}

BookmarkCard* MainWindow::ensureCard(const Bookmark& b) {
    if (auto* c = m_cards.value(b.id)) return c;
    auto* c = new BookmarkCard(b, m_dark, m_grid);
    c->hide();
    c->setLoading(m_loading.contains(b.id));
    // queued: let the card's menu/event finish before we touch or delete cards
    connect(c, &BookmarkCard::openRequested, this, &MainWindow::onOpen, Qt::QueuedConnection);
    connect(c, &BookmarkCard::editRequested, this, &MainWindow::onEdit, Qt::QueuedConnection);
    connect(c, &BookmarkCard::deleteRequested, this, &MainWindow::onDelete, Qt::QueuedConnection);
    connect(c, &BookmarkCard::refreshRequested, this, &MainWindow::startFetch, Qt::QueuedConnection);
    connect(c, &BookmarkCard::pinToggleRequested, this, &MainWindow::togglePin, Qt::QueuedConnection);
    connect(c, &BookmarkCard::copyRequested, this, &MainWindow::onCopy, Qt::QueuedConnection);
    m_cards.insert(b.id, c);
    return c;
}

void MainWindow::refresh(bool animate) {
    const QStringList raw = m_search->text().simplified().split(' ', Qt::SkipEmptyParts);
    QStringList tokens;
    for (const auto& r : raw) tokens << norm(r);
    const bool searching = !tokens.isEmpty();

    struct Row { int idx; int score; };
    QList<Row> pinned, others;
    int pinnedTotal = 0, problemTotal = 0, inScope = 0;
    for (const Bookmark& b : std::as_const(m_items))
        if (m_collectionFilter.isEmpty() || b.collection == m_collectionFilter) ++inScope;
    for (int i = 0; i < m_items.size(); ++i) {
        const Bookmark& b = m_items[i];
        if (!m_collectionFilter.isEmpty() && b.collection != m_collectionFilter) continue;
        if (b.pinned) ++pinnedTotal;
        if (b.health == HealthBroken || b.health == HealthRedirect) ++problemTotal;
        if (m_filterMode == 1 && !b.pinned) continue;
        if (m_filterMode == 2 && b.health != HealthBroken && b.health != HealthRedirect) continue;
        const int sc = matchScore(b, tokens);
        if (sc < 0) continue;
        (b.pinned ? pinned : others) << Row{i, sc};
    }
    auto less = [&](const Row& x, const Row& y) {
        if (searching && x.score != y.score) return x.score > y.score;
        const Bookmark& a = m_items[x.idx];
        const Bookmark& b = m_items[y.idx];
        switch (m_sort) {
        case 1: return a.created < b.created;
        case 2: return QString::localeAwareCompare(a.name, b.name) < 0;
        case 3: return a.visits != b.visits ? a.visits > b.visits : a.created > b.created;
        case 4: return a.lastOpened != b.lastOpened ? a.lastOpened > b.lastOpened : a.created > b.created;
        default: return a.created > b.created;
        }
    };
    std::stable_sort(pinned.begin(), pinned.end(), less);
    std::stable_sort(others.begin(), others.end(), less);

    QList<CardGrid::Item> items;
    m_visibleOrder.clear();
    auto addRows = [&](const QList<Row>& rows) {
        for (const Row& r : rows) {
            BookmarkCard* c = ensureCard(m_items[r.idx]);
            c->setHighlight(raw);
            const QString col = m_items[r.idx].collection;
            // the tag is redundant while you're already inside that collection
            if (col.isEmpty() || col == m_collectionFilter) c->setCollectionTag({}, {});
            else c->setCollectionTag(collectionLabel(col), collectionColor(col));
            items << CardGrid::Item{c, false};
            m_visibleOrder << m_items[r.idx].id;
        }
    };
    const bool sections = m_filterMode == 0 && !pinned.isEmpty();
    if (sections) {
        m_pinHeader->setText(QString("📌 Pinned · %1").arg(pinned.size()));
        items << CardGrid::Item{m_pinHeader, true};
    }
    addRows(pinned);
    if (sections && !others.isEmpty()) {
        m_allHeader->setText(QString("🔖 Other links · %1").arg(others.size()));
        items << CardGrid::Item{m_allHeader, true};
    }
    addRows(others);
    if (!searching && m_filterMode == 0 && inScope > 0) items << CardGrid::Item{m_addCard, false};

    m_grid->setItems(items, animate);

    const int shown = int(pinned.size() + others.size());
    if (shown == 0) {
        if (m_items.isEmpty()) {
            m_emptyTitle->setText("Your library is empty");
            m_emptyText->setText("Save your first link to start your library.");
        } else if (searching) {
            m_emptyTitle->setText(QString("No matches for “%1”").arg(m_search->text().trimmed()));
            m_emptyText->setText("Try another word or part of the URL.");
        } else if (inScope == 0) {
            m_emptyTitle->setText(QString("“%1” is empty").arg(collectionLabel(m_collectionFilter)));
            m_emptyText->setText(Platform::mobile ? "Use a bookmark’s menu to move it to this collection."
                                                 : "Drag a card here or choose Move to collection from its menu.");
        } else if (m_filterMode == 2) {
            m_emptyTitle->setText("All links are healthy");
            m_emptyText->setText("No broken or redirected links were found.");
        } else {
            m_emptyTitle->setText("No pinned links");
            m_emptyText->setText("Use the pin icon on a card to keep it at the top.");
        }
    }
    m_emptyBtn->setVisible(m_items.isEmpty());
    m_emptyBox->setVisible(shown == 0);
    m_grid->setVisible(shown > 0);

    m_resultLabel->setText(searching ? QString("Results: %1").arg(shown) : QString());
    m_count->setText(pinnedTotal > 0
        ? QString("Saved: %1 · Pinned: %2").arg(m_items.size()).arg(pinnedTotal)
        : QString("Saved: %1").arg(m_items.size()));
    m_filter->setItemText(0, QString("All %1").arg(inScope));
    m_filter->setItemText(1, QString("Pinned %1").arg(pinnedTotal));
    m_filter->setItemText(2, QString("Issues %1").arg(problemTotal));
}

// ============================== actions ==============================

MainWindow::AddResult MainWindow::addBookmarkData(const QString& rawUrl, const QString& name,
                                                  const QString& collection, QString* outName) {
    const QUrl u = parseUserUrl(rawUrl);
    if (!isWebUrl(u)) return AddResult::Invalid;
    const QString url = u.toString();
    for (const auto& ex : std::as_const(m_items)) {
        if (QUrl(ex.url).adjusted(QUrl::StripTrailingSlash) == QUrl(url).adjusted(QUrl::StripTrailingSlash)) {
            if (outName) *outName = ex.name;
            return AddResult::Duplicate;
        }
    }

    Bookmark b;
    b.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    b.url = url;
    b.created = QDateTime::currentSecsSinceEpoch();
    b.name = name;
    b.collection = collectionIndex(collection) >= 0 ? collection : QString();
    if (b.name.isEmpty()) { b.name = QUrl(b.url).host(); m_autoName.insert(b.id); }
    if (outName) *outName = b.name;

    m_items.prepend(b);
    persist();
    m_searchTimer->stop();
    m_search->blockSignals(true);
    m_search->clear();                         // make sure the new card is visible
    m_search->blockSignals(false);
    if (m_filterMode != 0) { m_filterMode = 0; m_filter->setCurrent(0); }
    if (!m_collectionFilter.isEmpty() && m_collectionFilter != b.collection) {
        m_collectionFilter.clear();
        m_colBar->setCurrent({});
    }
    m_loading.insert(b.id);
    refreshCollections();
    refresh(true);
    smoothScrollTo(0);
    startFetch(b.id);
    return AddResult::Added;
}

void MainWindow::addBookmark() {
    QString clip;
    if (m_useClipboard) {
        clip = QApplication::clipboard()->text().trimmed();
        if (!clip.startsWith("http") || clip.contains('\n')) clip.clear();
    }
    BookmarkDialog d(this, "Add link", clip);
    if (d.exec() != QDialog::Accepted) return;
    QString shown;
    // new links land in the collection you're looking at
    if (addBookmarkData(d.url(), d.name(), m_collectionFilter, &shown) == AddResult::Duplicate) {
        m_search->setText(shown);           // show the existing one instead of a duplicate
        showToast(QString("“%1” is already saved").arg(shown), 3000);
    }
}

void MainWindow::onOpen(const QString& id) {
    const int i = indexOf(id);
    if (i < 0) return;
    m_items[i].visits++;
    m_items[i].lastOpened = QDateTime::currentSecsSinceEpoch();
    persist();
    QDesktopServices::openUrl(QUrl(m_items[i].url));
    if (m_minimizeOnOpen) showMinimized();
    if (m_sort >= 3) QTimer::singleShot(400, this, [this] { refresh(true); });
}

void MainWindow::onCopy(const QString& id) {
    const int i = indexOf(id);
    if (i < 0) return;
    QApplication::clipboard()->setText(m_items[i].url);
    showToast("✓ Link copied");
}

void MainWindow::togglePin(const QString& id) {
    const int i = indexOf(id);
    if (i < 0) return;
    Bookmark& b = m_items[i];
    b.pinned = !b.pinned;
    b.pinnedAt = b.pinned ? QDateTime::currentSecsSinceEpoch() : 0;
    persist();
    if (auto* c = m_cards.value(id)) c->setBookmark(b);
    // let the pin "pop" play for a beat, then glide the card into its new section
    QTimer::singleShot(Theme::animations() ? 160 : 0, this, [this] { refresh(true); });
    if (m_items[i].pinned) smoothScrollTo(0);
}

void MainWindow::onEdit(const QString& id) {
    const int i = indexOf(id);
    if (i < 0) return;
    const Bookmark before = m_items[i];
    BookmarkDialog d(this, "Edit bookmark", before.url, before.name);
    if (d.exec() != QDialog::Accepted) return;

    const int j = indexOf(id);          // list may have changed while dialog was open
    if (j < 0) return;
    const bool urlChanged = d.url() != before.url;
    m_items[j].url = d.url();
    if (d.name().isEmpty()) { m_items[j].name = QUrl(d.url()).host(); m_autoName.insert(id); }
    else { m_items[j].name = d.name(); m_autoName.remove(id); }
    persist();
    if (auto* c = m_cards.value(id)) c->setBookmark(m_items[j]);
    refresh(true);
    if (urlChanged || d.name().isEmpty()) startFetch(id);
}

void MainWindow::onDelete(const QString& id) {
    const int i = indexOf(id);
    if (i < 0) return;
    commitPendingDelete();
    m_pending.b = m_items[i];
    m_pending.index = i;
    m_pending.active = true;
    m_items.removeAt(i);
    m_deleted.insert(id, QDateTime::currentSecsSinceEpoch());
    m_hashes.remove(id);
    persist();

    BookmarkCard* card = m_cards.take(id);
    auto finish = [this, card] {
        if (card) card->deleteLater();
        refreshCollections();
        refresh(true);                    // remaining cards glide into the gap
    };
    if (card && card->isVisible()) card->playRemove(finish);
    else finish();

    // no scary confirmation popup: delete instantly, offer undo
    m_toast->showMessage(QString("“%1” deleted").arg(m_pending.b.name), "↺ Undo", 5000);
}

void MainWindow::undoDelete() {
    if (!m_pending.active) return;
    m_pending.active = false;
    m_items.insert(qBound(0, m_pending.index, int(m_items.size())), m_pending.b);
    m_deleted.remove(m_pending.b.id);
    m_items[qBound(0, m_pending.index, int(m_items.size()) - 1)].modified = QDateTime::currentSecsSinceEpoch();
    persist();
    m_toast->dismiss();
    refreshCollections();
    refresh(true);
}

void MainWindow::commitPendingDelete() {
    if (!m_pending.active) return;
    m_pending.active = false;
    const Bookmark& b = m_pending.b;
    if (!b.imagePath.isEmpty()) QFile::remove(b.imagePath);
    if (!b.iconPath.isEmpty()) QFile::remove(b.iconPath);
    m_autoName.remove(b.id);
    m_loading.remove(b.id);
}

// ============================== import / export ==============================

void MainWindow::exportData() {
    const QUrl file = QFileDialog::getSaveFileUrl(this, "Save backup",
        QUrl::fromLocalFile(QDir::homePath() + "/LinkVault-backup.json"), "LinkVault (*.json)");
    if (file.isEmpty()) return;
    QJsonArray bookmarks;
    for (const auto& b : std::as_const(m_items)) {
        QJsonObject o = b.toJson();
        o.remove("image");
        o.remove("icon");
        bookmarks.append(o);
    }
    QJsonArray collections;
    for (const auto& collection : std::as_const(m_collections)) collections.append(collection.toJson());
    const QJsonDocument backup(QJsonObject{{"format", "LinkVault"}, {"version", 1},
                                           {"bookmarks", bookmarks}, {"collections", collections}});
    bool ok = false;
    if (file.isLocalFile()) {
        ok = Storage::writeJson(file.toLocalFile(), backup);
    } else if (file.scheme() == "content") {
        const QByteArray bytes = backup.toJson();
        QFile output(file.toString(QUrl::FullyEncoded));
        ok = output.open(QIODevice::WriteOnly | QIODevice::Truncate)
             && output.write(bytes) == bytes.size() && output.flush();
    }
    showToast(ok ? QString("✓ Exported bookmarks: %1").arg(m_items.size()) : QString("Could not save the file."), 3000);
}

void MainWindow::importData() {
    const QUrl file = QFileDialog::getOpenFileUrl(this, "Import bookmarks", QUrl::fromLocalFile(QDir::homePath()),
        "LinkVault or browser bookmarks (*.json *.html *.htm)");
    if (file.isEmpty()) return;
    QFile f(file.isLocalFile() ? file.toLocalFile() : file.toString(QUrl::FullyEncoded));
    if (!f.open(QIODevice::ReadOnly)) { showToast("Could not open the file.", 3000); return; }
    const QByteArray data = f.readAll();

    QList<Bookmark> incoming;
    QList<Collection> incomingCollections;
    const QByteArray start = data.left(4096).trimmed();
    if (file.path().endsWith(".json", Qt::CaseInsensitive) || start.startsWith('[') || start.startsWith('{')) {
        const QJsonDocument doc = QJsonDocument::fromJson(data);
        QJsonArray bookmarks;
        if (doc.isArray()) {
            bookmarks = doc.array();       // LinkVault 2.x and 3.x backups
        } else {
            const auto obj = doc.object();
            if (obj.value("format") != QJsonValue("LinkVault") || obj.value("version").toInt() != 1
                || !obj.value("bookmarks").isArray()) {
                showToast("This LinkVault backup is invalid or uses an unsupported format.", 3000);
                return;
            }
            bookmarks = obj.value("bookmarks").toArray();
            for (const auto& value : obj.value("collections").toArray())
                incomingCollections << Collection::fromJson(value.toObject());
        }
        for (const auto& v : bookmarks) incoming << Bookmark::fromJson(v.toObject());
    } else {
        // Netscape bookmark format (Chrome / Firefox / Edge export)
        static const QRegularExpression re(R"re(<a\s[^>]*href\s*=\s*"([^"]+)"[^>]*>(.*?)</a>)re",
            QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression dateRe(R"re(add_date\s*=\s*"(\d+)")re", QRegularExpression::CaseInsensitiveOption);
        auto it = re.globalMatch(QString::fromUtf8(data));
        while (it.hasNext()) {
            const auto m = it.next();
            Bookmark b;
            b.url = m.captured(1).trimmed();
            b.name = QTextDocumentFragment::fromHtml(m.captured(2)).toPlainText().simplified();
            const auto dm = dateRe.match(m.captured(0));
            if (dm.hasMatch()) b.created = dm.captured(1).toLongLong();
            incoming << b;
        }
    }

    QSet<QString> known;
    for (const auto& b : std::as_const(m_items)) known.insert(QUrl(b.url).adjusted(QUrl::StripTrailingSlash).toString());
    QStringList added;
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    int collectionsAdded = 0;
    for (auto collection : incomingCollections) {
        if (collection.id.isEmpty() || collection.name.isEmpty() || collectionIndex(collection.id) >= 0) continue;
        collection.modified = now;
        m_deleted.remove("col:" + collection.id);
        m_collections.append(collection);
        ++collectionsAdded;
    }
    for (Bookmark b : incoming) {
        const QUrl u(b.url);
        if (!(u.scheme() == "http" || u.scheme() == "https") || u.host().isEmpty()) continue;
        const QString key = u.adjusted(QUrl::StripTrailingSlash).toString();
        if (known.contains(key)) continue;
        known.insert(key);
        b.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        b.imagePath.clear();
        b.iconPath.clear();
        if (collectionIndex(b.collection) < 0) b.collection.clear();
        if (b.name.isEmpty()) b.name = u.host();
        if (b.created <= 0) b.created = now;
        m_items.append(b);
        added << b.id;
    }
    if (added.isEmpty() && !collectionsAdded) { showToast("No new bookmarks or collections to import.", 3000); return; }
    persist();
    for (const auto& id : std::as_const(added)) m_loading.insert(id);
    refreshCollections();
    refresh(false);
    for (const auto& id : std::as_const(added)) startFetch(id);
    showToast(QString("✓ Imported: %1 bookmarks · %2 collections").arg(added.size()).arg(collectionsAdded), 3000);
}

// ============================== thumbnails ==============================

void MainWindow::startFetch(const QString& id) {
    if (indexOf(id) < 0) return;
    m_loading.insert(id);
    if (auto* c = m_cards.value(id)) c->setLoading(true);
    if (!m_fetchQueue.contains(id)) m_fetchQueue << id;
    pumpFetches();
}

void MainWindow::pumpFetches() {
    while (m_activeFetches < kMaxParallelFetches && !m_fetchQueue.isEmpty()) {
        const QString id = m_fetchQueue.takeFirst();
        const int i = indexOf(id);
        if (i < 0) { m_loading.remove(id); continue; }
        ++m_activeFetches;
        m_fetcher->fetch(id, QUrl(m_items[i].url));
    }
}

void MainWindow::onFetched(const QString& id, const QString& cover, const QString& icon, const QString& title) {
    m_activeFetches = qMax(0, m_activeFetches - 1);
    pumpFetches();
    m_loading.remove(id);
    const int i = indexOf(id);
    if (i < 0) return;                  // deleted before the download finished
    if (!cover.isEmpty()) m_items[i].imagePath = cover;
    if (!icon.isEmpty()) m_items[i].iconPath = icon;
    if (m_autoName.contains(id) && !title.isEmpty()) m_items[i].name = title;
    m_autoName.remove(id);
    persist();
    // update only this card: rebuilding here could delete a card whose menu is open
    if (auto* c = m_cards.value(id)) {
        c->setBookmark(m_items[i]);
        c->setLoading(false);
    }
}
