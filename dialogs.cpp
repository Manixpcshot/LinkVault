#include "dialogs.h"
#include "platform.h"
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
    setMinimumWidth(Platform::mobile ? 260 : 480);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(26, 22, 26, 20);
    lay->setSpacing(8);

    auto* h = new QLabel(title); h->setObjectName("dialogTitle");
    lay->addWidget(h);
    lay->addSpacing(8);

    auto* l1 = new QLabel("Page URL"); l1->setObjectName("muted");
    m_url = new QLineEdit(url);
    m_url->setPlaceholderText("https://example.com");
    m_url->setLayoutDirection(Qt::LeftToRight);
    lay->addWidget(l1); lay->addWidget(m_url);
    lay->addSpacing(6);

    auto* l2 = new QLabel("Title (optional)"); l2->setObjectName("muted");
    m_name = new QLineEdit(name);
    m_name->setPlaceholderText("Example: Project notes");
    lay->addWidget(l2); lay->addWidget(m_name);

    auto* hint = new QLabel("Leave the title blank to use the page title. LinkVault also tries to fetch a preview.");
    hint->setObjectName("muted"); hint->setWordWrap(true);
    lay->addWidget(hint);

    m_error = new QLabel; m_error->setObjectName("error"); m_error->hide();
    lay->addWidget(m_error);
    lay->addSpacing(10);

    auto* btns = new QHBoxLayout;
    btns->addStretch();
    auto* cancel = new QPushButton("Cancel");
    auto* ok = new QPushButton("Save"); ok->setObjectName("primary"); ok->setDefault(true);
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
        m_error->setText("Enter a valid web address.");
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
    setMinimumWidth(Platform::mobile ? 260 : 440);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(26, 22, 26, 20);
    lay->setSpacing(8);

    auto* h = new QLabel(title); h->setObjectName("dialogTitle");
    lay->addWidget(h);
    lay->addSpacing(8);

    auto* l1 = new QLabel("Collection name"); l1->setObjectName("muted");
    m_name = new QLineEdit(name);
    m_name->setPlaceholderText("Example: Work, Reading, Design");
    m_name->setMaxLength(40);
    lay->addWidget(l1); lay->addWidget(m_name);
    lay->addSpacing(6);

    auto* l2 = new QLabel("Icon"); l2->setObjectName("muted");
    lay->addWidget(l2);
    auto* grid = new QGridLayout;
    grid->setSpacing(6);
    const QStringList emojis = {"📁", "💼", "📚", "🎓", "💡", "🛠", "🎨", "🎮", "🎬", "🎵", "🛒", "✈️",
                                "💰", "📰", "🧪", "❤️", "⭐", "🔥", "🚀", "🧠", "🏠", "⚽", "🍔", "📌"};
    const int columns = Platform::mobile ? 5 : 8;
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
        grid->addWidget(b, i / columns, i % columns);
    }
    lay->addLayout(grid);

    m_error = new QLabel; m_error->setObjectName("error"); m_error->hide();
    lay->addWidget(m_error);
    lay->addSpacing(10);

    auto* btns = new QHBoxLayout;
    btns->addStretch();
    auto* cancel = new QPushButton("Cancel");
    auto* ok = new QPushButton("Save"); ok->setObjectName("primary"); ok->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &CollectionDialog::accept);
    btns->addWidget(cancel); btns->addWidget(ok);
    lay->addLayout(btns);
    m_name->setFocus();
}

QString CollectionDialog::name() const { return m_name->text().simplified(); }

void CollectionDialog::accept() {
    if (name().isEmpty()) {
        m_error->setText("Enter a collection name.");
        m_error->show();
        m_name->setFocus();
        return;
    }
    QDialog::accept();
}

// ============================== ReminderDialog ==============================

ReminderDialog::ReminderDialog(QWidget* parent, const QDateTime& initial) : QDialog(parent) {
    setWindowTitle("Reminder");
    setMinimumWidth(Platform::mobile ? 260 : 380);
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(26, 22, 26, 20);
    lay->setSpacing(8);
    auto* h = new QLabel("⏰ When should LinkVault remind you?"); h->setObjectName("dialogTitle");
    h->setWordWrap(true);
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
    auto* cancel = new QPushButton("Cancel");
    auto* ok = new QPushButton("Set reminder"); ok->setObjectName("primary"); ok->setDefault(true);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    connect(ok, &QPushButton::clicked, this, &QDialog::accept);
    btns->addWidget(cancel); btns->addWidget(ok);
    lay->addLayout(btns);
}

QDateTime ReminderDialog::dateTime() const { return m_edit->dateTime(); }
