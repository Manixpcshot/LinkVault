#include "settingspage.h"
#include "widgets.h"
#include "platform.h"
#include "theme.h"
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QStyle>
#include <QVBoxLayout>

static const char* kQuickDesc = "Save the copied link from anywhere in Windows.";
static const char* kPaletteDesc = "Search and open links from anywhere in Windows. In LinkVault: Ctrl+K.";

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    setObjectName("page");
    auto* root = new QVBoxLayout(this);
    const int padding = Platform::mobile ? 12 : 20;
    root->setContentsMargins(padding, padding, padding, 4);
    root->setSpacing(16);

    // ---- header ----
    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 12, 0);
    header->setSpacing(10);
    auto* back = new QPushButton("← Back");
    back->setObjectName("ghost");
    back->setCursor(Qt::PointingHandCursor);
    back->setToolTip("Back (Esc)");
    connect(back, &QPushButton::clicked, this, &SettingsPage::backRequested);
    auto* titleBox = new QVBoxLayout;
    titleBox->setSpacing(0);
    auto* title = new QLabel("Settings");
    title->setObjectName("appTitle");
    titleBox->addWidget(title);
    header->addLayout(titleBox);
    header->addStretch();
    header->addWidget(back);
    root->addLayout(header);

    // ---- scrollable centered column ----
    auto* scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    if (Platform::mobile) QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    auto* holder = new QWidget;
    holder->setObjectName("gridHolder");
    auto* hl = new QHBoxLayout(holder);
    hl->setContentsMargins(0, 0, 0, 24);
    auto* column = new QWidget;
    column->setObjectName("gridHolder");
    column->setMaximumWidth(760);
    auto* cl = new QVBoxLayout(column);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(16);
    hl->addStretch(1);
    hl->addWidget(column, 100);
    hl->addStretch(1);
    scroll->setWidget(holder);
    root->addWidget(scroll, 1);

    auto toggle = [this](ToggleSwitch** out, void (SettingsPage::*sig)(bool)) {
        auto* t = new ToggleSwitch;
        connect(t, &QAbstractButton::toggled, this, [this, sig](bool on) { if (!m_syncing) emit (this->*sig)(on); });
        *out = t;
        return t;
    };
    auto button = [](const QString& text) {
        auto* b = new QPushButton(text);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    // ---- appearance ----
    QVBoxLayout* body = nullptr;
    cl->addWidget(makeCard("🎨 Appearance", &body));
    m_theme = new SegmentedControl({"Light", "Dark", "System", "Black"});
    connect(m_theme, &SegmentedControl::currentChanged, this, [this](int i) { if (!m_syncing) emit themeModeChanged(i); });
    addRow(body, "Theme", "Choose Light, Dark, Black, or match your system.", m_theme);
    m_micaDesc = addRow(body, "Mica background",
                        "Use blurred colors from your Windows wallpaper.", toggle(&m_mica, &SettingsPage::micaChanged));
    addRow(body, "Animations", "Animate cards, panels, and page transitions.",
           toggle(&m_anim, &SettingsPage::animationsChanged));

    // ---- behaviour ----
    cl->addWidget(makeCard("⚡ Behavior", &body));
    addRow(body, "Launch at login", "Start LinkVault when you sign in.",
           toggle(&m_auto, &SettingsPage::autoStartChanged));
    addRow(body, "Close to system tray", "Keep shortcuts and reminders active in the background.",
           toggle(&m_tray, &SettingsPage::closeToTrayChanged));
    addRow(body, "Minimize after opening a link", "Hide the window after opening a link in your browser.",
           toggle(&m_min, &SettingsPage::minimizeOnOpenChanged))->parentWidget()->setVisible(!Platform::mobile);
    addRow(body, "Fill from clipboard", "Prefill a copied web address when adding a link.",
           toggle(&m_clip, &SettingsPage::clipboardChanged));

    // ---- global hotkeys ----
    auto* hotkeyCard = makeCard("🌍 Global shortcuts", &body);
    cl->addWidget(hotkeyCard);
    hotkeyCard->setVisible(Platform::customFrame);
    m_quickDesc = addRow(body, "Quick add", kQuickDesc, makeKeyEditor(&m_quickKey, 1));
    m_paletteDesc = addRow(body, "Quick search (command palette)", kPaletteDesc, makeKeyEditor(&m_paletteKey, 2));

    // ---- links ----
    cl->addWidget(makeCard("🔗 Link health", &body));
    addRow(body, "Check links automatically", "Check for broken links and redirects once a day.",
           toggle(&m_check, &SettingsPage::linkCheckChanged));
    auto* checkNow = button("🔍 Check now");
    connect(checkNow, &QPushButton::clicked, this, &SettingsPage::checkLinksNowRequested);
    m_linkStatus = addRow(body, "Manual check", QString(), checkNow);

    // ---- sync ----
    auto* syncCard = makeCard("☁️ Folder sync", &body);
    cl->addWidget(syncCard);
    syncCard->setVisible(!Platform::mobile);
    auto* syncBtns = new QWidget;
    auto* sb = new QHBoxLayout(syncBtns);
    sb->setContentsMargins(0, 0, 0, 0);
    sb->setSpacing(8);
    m_syncChoose = button("Choose folder");
    m_syncNow = button("⟳");
    m_syncNow->setToolTip("Sync now");
    m_syncOff = button("Disconnect");
    connect(m_syncChoose, &QPushButton::clicked, this, &SettingsPage::syncChooseRequested);
    connect(m_syncNow, &QPushButton::clicked, this, &SettingsPage::syncNowRequested);
    connect(m_syncOff, &QPushButton::clicked, this, &SettingsPage::syncDisableRequested);
    sb->addWidget(m_syncNow);
    sb->addWidget(m_syncOff);
    sb->addWidget(m_syncChoose);
    m_syncDesc = addRow(body, "Sync folder",
                        "Choose a shared folder in OneDrive, Google Drive, or Dropbox.",
                        syncBtns);

    // ---- data ----
    cl->addWidget(makeCard("💾 Data", &body));
    auto* folder = button("Open folder");
    connect(folder, &QPushButton::clicked, this, &SettingsPage::openFolderRequested);
    m_stats = addRow(body, "Data location", QString(), folder);
    m_stats->parentWidget()->setVisible(!Platform::mobile);
    auto* exp = button("Export");
    connect(exp, &QPushButton::clicked, this, &SettingsPage::exportRequested);
    addRow(body, "Backup", "Save bookmarks, collections, and notes to a JSON file.", exp);
    auto* imp = button("Import");
    connect(imp, &QPushButton::clicked, this, &SettingsPage::importRequested);
    addRow(body, "Import bookmarks", "Import from a LinkVault backup or a browser bookmarks HTML file.", imp);

    // ---- shortcuts ----
    auto* shortcutCard = makeCard("⌨️ Shortcuts", &body);
    cl->addWidget(shortcutCard);
    shortcutCard->setVisible(!Platform::mobile);
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 10, 0, 6);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(10);
    const QList<QPair<QString, QString>> keys = {
        {"Ctrl + N", "Add link"}, {"Ctrl + F", "Search"},
        {"Ctrl + K", "Command palette"}, {"Enter", "Open the first result"},
        {"Esc", "Clear search or go back"}, {"Ctrl + ,", "Settings"},
        {"Ctrl + Z", "Undo delete"}, {"F11", "Full screen"},
        {"Right-click", "Bookmark menu"}, {"Drag a card", "Move to collection"},
    };
    for (int i = 0; i < keys.size(); ++i) {
        auto* k = new QLabel(keys[i].first);
        k->setObjectName("kbd");
        k->setLayoutDirection(Qt::LeftToRight);
        auto* d = new QLabel(keys[i].second);
        const int r = i / 2, c = (i % 2) * 3;
        grid->addWidget(k, r, c, Qt::AlignVCenter);
        grid->addWidget(d, r, c + 1, Qt::AlignVCenter);
    }
    grid->setColumnStretch(1, 1);
    grid->setColumnMinimumWidth(2, 24);
    grid->setColumnStretch(4, 1);
    body->addLayout(grid);

    // ---- about ----
    auto* about = new QLabel(QString("LinkVault %1 · C++ and Qt 6").arg(LINKVAULT_VERSION));
    about->setObjectName("muted");
    about->setAlignment(Qt::AlignCenter);
    cl->addSpacing(4);
    cl->addWidget(about);
    cl->addStretch();
}

QWidget* SettingsPage::makeKeyEditor(QKeySequenceEdit** edit, int which) {
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    auto* e = new QKeySequenceEdit;
    e->setLayoutDirection(Qt::LeftToRight);
    e->setFixedWidth(170);
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    e->setMaximumSequenceLength(1);
#endif
    auto* clear = new QPushButton("✕");
    clear->setToolTip("Clear this shortcut");
    clear->setCursor(Qt::PointingHandCursor);
    clear->setFixedWidth(40);
    auto emitChange = [this, e, which] {
        if (m_syncing) return;
        QKeySequence k = e->keySequence();
        if (k.count() > 1) { k = QKeySequence(k[0]); e->setKeySequence(k); }   // older Qt: keep first chord
        if (which == 1) emit quickAddKeyChanged(k); else emit paletteKeyChanged(k);
    };
    connect(e, &QKeySequenceEdit::editingFinished, this, emitChange);
    connect(clear, &QPushButton::clicked, this, [e, emitChange] { e->clear(); emitChange(); });
    h->addWidget(e);
    h->addWidget(clear);
    *edit = e;
    return w;
}

QFrame* SettingsPage::makeCard(const QString& title, QVBoxLayout** body) {
    auto* f = new QFrame;
    f->setObjectName("settingsCard");
    auto* v = new QVBoxLayout(f);
    const int margin = Platform::mobile ? 12 : 22;
    v->setContentsMargins(margin, 18, margin, 10);
    v->setSpacing(0);
    auto* t = new QLabel(title);
    t->setObjectName("cardTitle");
    v->addWidget(t);
    v->addSpacing(4);
    *body = v;
    return f;
}

QLabel* SettingsPage::addRow(QVBoxLayout* body, const QString& title, const QString& desc, QWidget* control) {
    if (body->count() > 2) {                       // separator between rows (title + spacing come first)
        auto* sep = new QFrame;
        sep->setObjectName("sep");
        sep->setFixedHeight(1);
        body->addWidget(sep);
    }
    auto* row = new QWidget;
    auto* h = new QBoxLayout(Platform::mobile ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight, row);
    h->setContentsMargins(0, 12, 0, 12);
    h->setSpacing(Platform::mobile ? 8 : 18);
    auto* tv = new QVBoxLayout;
    tv->setSpacing(3);
    auto* t = new QLabel(title);
    t->setObjectName("rowTitle");
    t->setWordWrap(true);
    auto* d = new QLabel(desc);
    d->setObjectName("muted");
    d->setWordWrap(true);
    tv->addWidget(t);
    tv->addWidget(d);
    h->addLayout(tv, 1);
    h->addWidget(control, 0, Platform::mobile ? Qt::AlignLeft : Qt::AlignVCenter);
    body->addWidget(row);
    return d;
}

void SettingsPage::sync(const SettingsState& s) {
    m_syncing = true;
    m_theme->setCurrent(s.themeMode, false);
    m_auto->setChecked(s.autoStart);
    m_auto->parentWidget()->setVisible(s.autoStartSupported);
    m_anim->setChecked(s.animations);
    m_min->setChecked(s.minimizeOnOpen);
    m_clip->setChecked(s.useClipboard);
    m_mica->setChecked(s.mica && s.micaSupported);
    m_micaDesc->parentWidget()->setVisible(s.micaSupported);
    m_micaDesc->setText(s.themeMode == Theme::Black ? "The Black theme uses a solid black background." : "Use blurred colors from your Windows wallpaper.");
    m_mica->setEnabled(s.micaSupported && s.themeMode != Theme::Black);
    m_tray->setChecked(s.closeToTray);
    m_tray->parentWidget()->setVisible(s.traySupported);
    m_check->setChecked(s.linkCheck);
    m_quickKey->setKeySequence(s.quickAddKey);
    m_paletteKey->setKeySequence(s.paletteKey);
    m_quickKey->setEnabled(s.hotkeysSupported);
    m_paletteKey->setEnabled(s.hotkeysSupported);
    if (m_stats) m_stats->setText(QString("Saved: %1 · Pinned: %2").arg(s.total).arg(s.pinned));
    setLinkStatus(s.linkStatus);
    setSyncStatus(s.syncFolder, s.syncStatus);
    m_syncing = false;
}

void SettingsPage::setHotkeyError(int which, const QString& error) {
    QLabel* d = which == 1 ? m_quickDesc : m_paletteDesc;
    if (!d) return;
    d->setObjectName(error.isEmpty() ? "muted" : "error");
    d->setText(error.isEmpty() ? QString::fromUtf8(which == 1 ? kQuickDesc : kPaletteDesc) : error);
    d->style()->unpolish(d);
    d->style()->polish(d);
}

void SettingsPage::setLinkStatus(const QString& text) {
    if (m_linkStatus) m_linkStatus->setText(text.isEmpty() ? QString("Check all links for issues.") : text);
}

void SettingsPage::setSyncStatus(const QString& folder, const QString& status) {
    const bool on = !folder.isEmpty();
    m_syncOff->setVisible(on);
    m_syncNow->setVisible(on);
    m_syncChoose->setText(on ? "Change folder" : "Choose folder");
    if (on) m_syncDesc->setText(QString("%1\n%2").arg(folder, status));
    else m_syncDesc->setText("Choose a shared folder in OneDrive, Google Drive, or Dropbox.");
}
