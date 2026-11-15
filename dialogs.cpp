#include "dialogs.h"
#include "platform.h"
#include <QDateTimeEdit>
#include <QGridLayout>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QDialogButtonBox>

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

BookmarkDialog::BookmarkDialog(QWidget* parent, const QString& title, const QString& url, const QString& name,
                               const QString& note)
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

    auto* noteLabel = new QLabel("Note (optional)");
    noteLabel->setObjectName("muted");
    m_note = new QLineEdit(note);
    m_note->setMaxLength(1000);
    m_note->setPlaceholderText("Why are you saving this link?");
    m_note->setAccessibleName("Bookmark note");
    lay->addWidget(noteLabel);
    lay->addWidget(m_note);

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
QString BookmarkDialog::note() const { return m_note->text().trimmed(); }

void BookmarkDialog::accept() {
    if (!isWebUrl(parseUserUrl(m_url->text()))) {
        m_error->setText("Enter a valid web address.");
        m_error->show();
        m_url->setFocus();
        return;
    }
    QDialog::accept();
}

SmartCollectionDialog::SmartCollectionDialog(QWidget* parent, const QString& name, const SmartRule& rule)
    : QDialog(parent) {
    setWindowTitle(name.isEmpty() ? "New smart collection" : "Edit smart collection");
    auto* layout = new QVBoxLayout(this);
    auto* title = new QLabel(windowTitle());
    title->setObjectName("dialogTitle");
    layout->addWidget(title);

    auto addText = [&](const QString& label, const QString& text, const QString& example) {
        layout->addWidget(new QLabel(label));
        auto* field = new QLineEdit(text);
        field->setAccessibleName(label);
        field->setPlaceholderText(example);
        layout->addWidget(field);
        return field;
    };
    m_name = addText("Name", name, "YouTube this week");
    m_name->setMaxLength(60);
    m_text = addText("Words in title, URL, or note", rule.text, "Optional");
    m_text->setMaxLength(200);
    m_domain = addText("Website", rule.domain, "youtube.com");
    m_domain->setMaxLength(255);
    layout->addWidget(new QLabel("Saved"));
    m_period = new QComboBox;
    m_period->addItems({"Any time", "Today", "This week", "Past 7 days", "Past 30 days"});
    m_period->setCurrentIndex(rule.period);
    layout->addWidget(m_period);
    m_error = new QLabel;
    m_error->setObjectName("error");
    m_error->setWordWrap(true);
    m_error->hide();
    layout->addWidget(m_error);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::accepted, this, &SmartCollectionDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    setMinimumWidth(Platform::mobile ? 270 : 410);
}

QString SmartCollectionDialog::name() const { return m_name->text().simplified(); }

SmartRule SmartCollectionDialog::rule() const {
    const QString input = m_domain->text().trimmed();
    const QUrl url = parseUserUrl(input);
    QString host = isWebUrl(url) ? url.host().toLower() : QString();
    if (host.startsWith("www.")) host.remove(0, 4);
    return {m_text->text().simplified(), host, SmartRule::Period(m_period->currentIndex())};
}

void SmartCollectionDialog::accept() {
    const auto filter = rule();
    QString error;
    if (name().isEmpty()) error = "Enter a name for the collection.";
    else if (!m_domain->text().trimmed().isEmpty() && filter.domain.isEmpty()) error = "Enter a website such as youtube.com.";
    else if (!filter.enabled()) error = "Add at least one filter.";
    if (error.isEmpty()) { QDialog::accept(); return; }
    m_error->setText(error);
    m_error->show();
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
