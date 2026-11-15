#include "settingspage.h"
#include "widgets.h"
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QVBoxLayout>

static const char* kQuickDesc = "از هر جای ویندوز، لینک کپی‌شده رو سریع ذخیره کن";
static const char* kPaletteDesc = "پنجره‌ی جستجو و باز کردن سریع لینک‌ها از هر جای ویندوز (داخل برنامه: Ctrl+K)";

SettingsPage::SettingsPage(QWidget* parent) : QWidget(parent) {
    setObjectName("page");
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 4, 12, 4);
    root->setSpacing(16);

    // ---- header ----
    auto* header = new QHBoxLayout;
    header->setContentsMargins(0, 0, 12, 0);
    header->setSpacing(10);
    auto* back = new QPushButton("→   بازگشت");
    back->setObjectName("ghost");
    back->setCursor(Qt::PointingHandCursor);
    back->setToolTip("بازگشت (Esc)");
    connect(back, &QPushButton::clicked, this, &SettingsPage::backRequested);
    auto* titleBox = new QVBoxLayout;
    titleBox->setSpacing(0);
    auto* title = new QLabel("تنظیمات");
    title->setObjectName("appTitle");
    auto* sub = new QLabel("LinkVault رو همون‌جوری که دوست داری کن");
    sub->setObjectName("muted");
    titleBox->addWidget(title);
    titleBox->addWidget(sub);
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
    auto* holder = new QWidget;
    holder->setObjectName("gridHolder");
    auto* hl = new QHBoxLayout(holder);
    hl->setContentsMargins(0, 0, 12, 24);
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
    cl->addWidget(makeCard("🎨  ظاهر", &body));
    m_theme = new SegmentedControl({"☀️  روز", "🌙  شب", "💻  سیستم"});
    connect(m_theme, &SegmentedControl::currentChanged, this, [this](int i) { if (!m_syncing) emit themeModeChanged(i); });
    addRow(body, "تم برنامه", "روشن، تیره یا هماهنگ با تم ویندوز", m_theme);
    m_micaDesc = addRow(body, "پس‌زمینه‌ی شیشه‌ای (Mica)",
                        "رنگ‌های والپیپر ویندوز، محو و نرم، پشت برنامه دیده میشه", toggle(&m_mica, &SettingsPage::micaChanged));
    addRow(body, "انیمیشن‌ها", "حرکت نرم کارت‌ها و صفحه‌ها. روی سیستم ضعیف می‌تونی خاموشش کنی",
           toggle(&m_anim, &SettingsPage::animationsChanged));

    // ---- behaviour ----
    cl->addWidget(makeCard("⚡  رفتار برنامه", &body));
    addRow(body, "اجرای خودکار", "وقتی کامپیوتر روشن شد، LinkVault خودش باز بشه",
           toggle(&m_auto, &SettingsPage::autoStartChanged));
    addRow(body, "با بستن، بره توی System Tray", "برنامه بسته نمیشه و میانبرها و یادآوری‌ها کار می‌کنن",
           toggle(&m_tray, &SettingsPage::closeToTrayChanged));
    addRow(body, "کوچک شدن بعد از باز کردن لینک", "بعد از باز کردن یه سایت، پنجره بره پایین",
           toggle(&m_min, &SettingsPage::minimizeOnOpenChanged));
    addRow(body, "پر کردن خودکار از کلیپ‌بورد", "اگه لینکی کپی کرده باشی، موقع افزودن خودش وارد میشه",
           toggle(&m_clip, &SettingsPage::clipboardChanged));

    // ---- global hotkeys ----
    cl->addWidget(makeCard("🌍  میانبرهای سراسری", &body));
    m_quickDesc = addRow(body, "افزودن سریع", kQuickDesc, makeKeyEditor(&m_quickKey, 1));
    m_paletteDesc = addRow(body, "جستجوی سریع (پالت فرمان)", kPaletteDesc, makeKeyEditor(&m_paletteKey, 2));

    // ---- links ----
    cl->addWidget(makeCard("🔗  سلامت لینک‌ها", &body));
    addRow(body, "بررسی خودکار لینک‌ها", "روزی یک بار، بی‌صدا چک میشه کدوم لینک‌ها خراب شدن یا منتقل شدن",
           toggle(&m_check, &SettingsPage::linkCheckChanged));
    auto* checkNow = button("🔍  الان بررسی کن");
    connect(checkNow, &QPushButton::clicked, this, &SettingsPage::checkLinksNowRequested);
    m_linkStatus = addRow(body, "بررسی دستی", QString(), checkNow);

    // ---- sync ----
    cl->addWidget(makeCard("☁️  سینک بین دستگاه‌ها", &body));
    auto* syncBtns = new QWidget;
    auto* sb = new QHBoxLayout(syncBtns);
    sb->setContentsMargins(0, 0, 0, 0);
    sb->setSpacing(8);
    m_syncChoose = button("📁  انتخاب پوشه");
    m_syncNow = button("⟳");
    m_syncNow->setToolTip("الان سینک کن");
    m_syncOff = button("قطع");
    connect(m_syncChoose, &QPushButton::clicked, this, &SettingsPage::syncChooseRequested);
    connect(m_syncNow, &QPushButton::clicked, this, &SettingsPage::syncNowRequested);
    connect(m_syncOff, &QPushButton::clicked, this, &SettingsPage::syncDisableRequested);
    sb->addWidget(m_syncNow);
    sb->addWidget(m_syncOff);
    sb->addWidget(m_syncChoose);
    m_syncDesc = addRow(body, "پوشه‌ی سینک",
                        "یه پوشه داخل OneDrive / Google Drive / Dropbox انتخاب کن تا لینک‌ها روی همه‌ی سیستم‌هات یکی باشن",
                        syncBtns);

    // ---- data ----
    cl->addWidget(makeCard("💾  داده‌ها", &body));
    auto* folder = button("📁  باز کردن پوشه");
    connect(folder, &QPushButton::clicked, this, &SettingsPage::openFolderRequested);
    m_stats = addRow(body, "محل ذخیره‌سازی", QString(), folder);
    auto* exp = button("⬆  خروجی گرفتن");
    connect(exp, &QPushButton::clicked, this, &SettingsPage::exportRequested);
    addRow(body, "پشتیبان‌گیری", "همه‌ی لینک‌ها توی یه فایل JSON ذخیره میشن", exp);
    auto* imp = button("⬇  وارد کردن");
    connect(imp, &QPushButton::clicked, this, &SettingsPage::importRequested);
    addRow(body, "وارد کردن لینک‌ها", "از فایل پشتیبان LinkVault یا بوکمارک‌های خروجی کروم / فایرفاکس (HTML)", imp);

    // ---- shortcuts ----
    cl->addWidget(makeCard("⌨️  میانبرها", &body));
    auto* grid = new QGridLayout;
    grid->setContentsMargins(0, 10, 0, 6);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(10);
    const QList<QPair<QString, QString>> keys = {
        {"Ctrl + N", "افزودن سایت"}, {"Ctrl + F", "جستجو"},
        {"Ctrl + K", "پالت فرمان"}, {"Enter", "باز کردن اولین نتیجه"},
        {"Esc", "پاک کردن جستجو / بازگشت"}, {"Ctrl + ,", "تنظیمات"},
        {"Ctrl + Z", "برگردوندن حذف"}, {"F11", "تمام صفحه"},
        {"کلیک راست", "منوی کارت"}, {"کشیدن کارت", "انتقال به کالکشن"},
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
    auto* about = new QLabel("LinkVault 3.0  ·  ساخته شده با C++ و Qt 6");
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
    clear->setToolTip("غیرفعال کردن این میانبر");
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
    v->setContentsMargins(22, 18, 22, 10);
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
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 12, 0, 12);
    h->setSpacing(18);
    auto* tv = new QVBoxLayout;
    tv->setSpacing(3);
    auto* t = new QLabel(title);
    t->setObjectName("rowTitle");
    auto* d = new QLabel(desc);
    d->setObjectName("muted");
    d->setWordWrap(true);
    tv->addWidget(t);
    tv->addWidget(d);
    h->addLayout(tv, 1);
    h->addWidget(control, 0, Qt::AlignVCenter);
    body->addWidget(row);
    return d;
}

void SettingsPage::sync(const SettingsState& s) {
    m_syncing = true;
    m_theme->setCurrent(s.themeMode, false);
    m_auto->setChecked(s.autoStart);
    m_anim->setChecked(s.animations);
    m_min->setChecked(s.minimizeOnOpen);
    m_clip->setChecked(s.useClipboard);
    m_mica->setChecked(s.mica && s.micaSupported);
    m_mica->setEnabled(s.micaSupported);
    if (!s.micaSupported) m_micaDesc->setText("فقط روی ویندوز، وقتی والپیپر تصویری داری در دسترسه");
    m_tray->setChecked(s.closeToTray);
    m_check->setChecked(s.linkCheck);
    m_quickKey->setKeySequence(s.quickAddKey);
    m_paletteKey->setKeySequence(s.paletteKey);
    m_quickKey->setEnabled(s.hotkeysSupported);
    m_paletteKey->setEnabled(s.hotkeysSupported);
    if (m_stats) m_stats->setText(QString("%1 لینک ذخیره شده  ·  %2 سنجاق‌شده").arg(s.total).arg(s.pinned));
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
    if (m_linkStatus) m_linkStatus->setText(text.isEmpty() ? QString("همه‌ی لینک‌ها رو همین الان چک کن") : text);
}

void SettingsPage::setSyncStatus(const QString& folder, const QString& status) {
    const bool on = !folder.isEmpty();
    m_syncOff->setVisible(on);
    m_syncNow->setVisible(on);
    m_syncChoose->setText(on ? "📁  تغییر پوشه" : "📁  انتخاب پوشه");
    if (on) m_syncDesc->setText(QString("%1\n%2").arg(folder, status));
    else m_syncDesc->setText("یه پوشه داخل OneDrive / Google Drive / Dropbox انتخاب کن تا لینک‌ها روی همه‌ی سیستم‌هات یکی باشن");
}
