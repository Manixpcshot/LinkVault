#include "dialogs.h"
#include <QDateTimeEdit>
#include <QGridLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

QUrl parseUserUrl(QString s) {
    s = s.trimmed();
    if (s.isEmpty()) return {};
    if (!s.contains("://")) s.prepend("https://");
    return QUrl::fromUserInput(s);
}

bool isWebUrl(const QUrl& u) {
    return u.isValid() && (u.scheme() == "http" || u.scheme() == "https")
           && (u.host().contains('.') || u.host() == "localhost");
}

// ============================== BookmarkDialog ==============================

BookmarkDialog::BookmarkDialog(QWidget* parent, const QString& title, const QString& url, const QString& name)
    : QDialog(parent) {
    setWindowTitle(title);
    setMinimumWidth(480);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(26, 22, 26, 20);
    lay->setSpacing(8);

    auto* h = new QLabel(title); h->setObjectName("dialogTitle");
    lay->addWidget(h);
    lay->addSpacing(8);

    auto* l1 = new QLabel("آدرس سایت یا صفحه"); l1->setObjectName("muted");
    m_url = new QLineEdit(url);
    m_url->setPlaceholderText("https://example.com");
    m_url->setLayoutDirection(Qt::LeftToRight);
    lay->addWidget(l1); lay->addWidget(m_url);
    lay->addSpacing(6);

    auto* l2 = new QLabel("اسم (اختیاری)"); l2->setObjectName("muted");
    m_name = new QLineEdit(name);
    m_name->setPlaceholderText("مثلاً: پروژه فلان");
    lay->addWidget(l2); lay->addWidget(m_name);

    auto* hint = new QLabel("اگه اسم رو خالی بذاری، عنوان خود سایت رو می‌ذارم. عکس سایت هم خودکار گرفته میشه.");
    hint->setObjectName("muted"); hint->setWordWrap(true);
    lay->addWidget(hint);

    m_error = new QLabel; m_error->setObjectName("error"); m_error->hide();
    lay->addWidget(m_error);
    lay->addSpacing(10);

    auto* btns = new QHBoxLayout;
    btns->addStretch();
    auto* cancel = new QPushButton("انصراف");
    auto* ok = new QPushButton("ذخیره"); ok->setObjectName("primary"); ok->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &BookmarkDialog::accept);
    btns->addWidget(cancel); btns->addWidget(ok);
    lay->addLayout(btns);

    (url.isEmpty() ? m_url : m_name)->setFocus();
}

QString BookmarkDialog::url() const  { return parseUserUrl(m_url->text()).toString(); }
QString BookmarkDialog::name() const { return m_name->text().trimmed(); }

void BookmarkDialog::accept() {
    if (!isWebUrl(parseUserUrl(m_url->text()))) {
        m_error->setText("آدرس درست نیست، یه لینک معتبر وارد کن.");
        m_error->show();
        m_url->setFocus();
        return;
    }
    QDialog::accept();
}

// ============================== CollectionDialog ==============================

CollectionDialog::CollectionDialog(QWidget* parent, const QString& title, const QString& name, const QString& emoji)
    : QDialog(parent), m_emoji(emoji.isEmpty() ? QStringLiteral("📁") : emoji) {
    setWindowTitle(title);
    setMinimumWidth(440);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(26, 22, 26, 20);
    lay->setSpacing(8);

    auto* h = new QLabel(title); h->setObjectName("dialogTitle");
    lay->addWidget(h);
    lay->addSpacing(8);

    auto* l1 = new QLabel("اسم کالکشن"); l1->setObjectName("muted");
    m_name = new QLineEdit(name);
    m_name->setPlaceholderText("مثلاً: کار، یادگیری، پروژه فلان");
    m_name->setMaxLength(40);
    lay->addWidget(l1); lay->addWidget(m_name);
    lay->addSpacing(6);

    auto* l2 = new QLabel("آیکن"); l2->setObjectName("muted");
    lay->addWidget(l2);
    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    const QStringList emojis = {"📁", "💼", "📚", "🎓", "💡", "🛠", "🎨", "🎮", "🎬", "🎵", "🛒", "✈️",
                                "💰", "📰", "🧪", "❤️", "⭐", "🔥", "🚀", "🧠", "🏠", "⚽", "🍔", "📌"};
    for (int i = 0; i < emojis.size(); ++i) {
        auto* b = new QPushButton(emojis[i]);
        b->setObjectName("emojiBtn");
        b->setCheckable(true);
        b->setChecked(emojis[i] == m_emoji);
        b->setCursor(Qt::PointingHandCursor);
        b->setFixedSize(42, 42);
        connect(b, &QPushButton::clicked, this, [this, b, e = emojis[i]] {
            m_emoji = e;
            for (auto* o : std::as_const(m_emojiBtns)) o->setChecked(o == b);
        });
        m_emojiBtns << b;
        grid->addWidget(b, i / 8, i % 8);
    }
    lay->addLayout(grid);

    m_error = new QLabel; m_error->setObjectName("error"); m_error->hide();
    lay->addWidget(m_error);
    lay->addSpacing(10);

    auto* btns = new QHBoxLayout;
    btns->addStretch();
    auto* cancel = new QPushButton("انصراف");
    auto* ok = new QPushButton("ذخیره"); ok->setObjectName("primary"); ok->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &CollectionDialog::accept);
    btns->addWidget(cancel); btns->addWidget(ok);
    lay->addLayout(btns);
    m_name->setFocus();
}

QString CollectionDialog::name() const { return m_name->text().simplified(); }

void CollectionDialog::accept() {
    if (name().isEmpty()) {
        m_error->setText("یه اسم براش بنویس.");
        m_error->show();
        m_name->setFocus();
        return;
    }
    QDialog::accept();
}

// ============================== ReminderDialog ==============================

ReminderDialog::ReminderDialog(QWidget* parent, const QDateTime& initial) : QDialog(parent) {
    setWindowTitle("یادآوری");
    setMinimumWidth(380);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(26, 22, 26, 20);
    lay->setSpacing(8);
    auto* h = new QLabel("⏰  کِی یادت بندازم؟"); h->setObjectName("dialogTitle");
    lay->addWidget(h);
    lay->addSpacing(8);
    m_edit = new QDateTimeEdit(initial);
    m_edit->setCalendarPopup(true);
    m_edit->setDisplayFormat("yyyy/MM/dd   HH:mm");
    m_edit->setMinimumDateTime(QDateTime::currentDateTime());
    m_edit->setLayoutDirection(Qt::LeftToRight);
    lay->addWidget(m_edit);
    lay->addSpacing(10);
    auto* btns = new QHBoxLayout;
    btns->addStretch();
    auto* cancel = new QPushButton("انصراف");
    auto* ok = new QPushButton("ثبت یادآوری"); ok->setObjectName("primary"); ok->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    btns->addWidget(cancel); btns->addWidget(ok);
    lay->addLayout(btns);
}

QDateTime ReminderDialog::dateTime() const { return m_edit->dateTime(); }
