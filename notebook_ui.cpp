#include "notebook.h"
#include "platform.h"
#include "theme.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScroller>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>
#include <algorithm>

namespace {
QLabel* plainLabel(const QString& text, const QString& style = {}) {
    auto* label = new QLabel(text);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    if (!style.isEmpty()) label->setObjectName(style);
    return label;
}

QPushButton* primaryButton(const QString& text) {
    auto* button = new QPushButton(text);
    button->setObjectName("primary");
    if (Platform::mobile) button->setMinimumHeight(44);
    return button;
}

class NoteEditor : public QDialog {
public:
    NoteEditor(Notebook* notebook, const Note& entry, QWidget* parent)
        : QDialog(parent), m_notebook(notebook), m_entry(entry) {
        setWindowTitle(entry.id.isEmpty() ? "New note" : "Edit note");
        resize(Platform::mobile ? 320 : 540, Platform::mobile ? 550 : 620);
        auto* root = new QVBoxLayout(this);
        auto* scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        root->addWidget(scroll, 1);
        auto* form = new QWidget;
        auto* layout = new QVBoxLayout(form);
        layout->setContentsMargins(8, 6, 8, 6);
        scroll->setWidget(form);

        layout->addWidget(plainLabel(windowTitle(), "dialogTitle"));
        m_title = new QLineEdit(entry.title);
        m_title->setPlaceholderText("Title");
        m_title->setAccessibleName("Note title");
        m_title->setMaxLength(150);
        layout->addWidget(m_title);
        m_text = new QPlainTextEdit(entry.body);
        m_text->setPlaceholderText("Write a note…");
        m_text->setAccessibleName("Note text");
        m_text->setMinimumHeight(110);
        layout->addWidget(m_text, 1);

        layout->addWidget(plainLabel("Category", "muted"));
        m_category = new QComboBox;
        m_category->setEditable(true);
        m_category->setInsertPolicy(QComboBox::NoInsert);
        m_category->addItems(m_notebook->categories());
        m_category->setEditText(entry.category);
        m_category->lineEdit()->setPlaceholderText("Choose or type a category");
        m_category->lineEdit()->setMaxLength(40);
        layout->addWidget(m_category);

        layout->addWidget(plainLabel("Accent color", "muted"));
        m_color = new QComboBox;
        m_color->addItem("Default", QString());
        m_color->addItem("Lavender", "#9577F2");
        m_color->addItem("Blue", "#56A0F0");
        m_color->addItem("Mint", "#37B893");
        m_color->addItem("Amber", "#EAB54E");
        m_color->addItem("Rose", "#E6759D");
        m_color->setCurrentIndex(qMax(0, m_color->findData(entry.color)));
        layout->addWidget(m_color);

        layout->addWidget(plainLabel("Countdown", "muted"));
        m_timerMode = new QComboBox;
        m_timerMode->addItems({"None", "Date and time", "Time from now"});
        m_timerMode->setCurrentIndex(entry.deadline > 0 ? 1 : 0);
        layout->addWidget(m_timerMode);
        m_date = new QDateTimeEdit(entry.deadline > 0 ? QDateTime::fromSecsSinceEpoch(entry.deadline)
                                                      : QDateTime::currentDateTime().addSecs(3600));
        m_date->setCalendarPopup(true);
        m_date->setDisplayFormat(QLocale().dateTimeFormat(QLocale::ShortFormat));
        m_date->setAccessibleName("Countdown deadline");
        layout->addWidget(m_date);
        m_durationRow = new QWidget;
        auto* duration = new QHBoxLayout(m_durationRow);
        duration->setContentsMargins(0, 0, 0, 0);
        m_amount = new QSpinBox;
        m_amount->setRange(1, 10000);
        m_amount->setValue(25);
        m_amount->setAccessibleName("Countdown amount");
        m_unit = new QComboBox;
        m_unit->addItem("Minutes", 60);
        m_unit->addItem("Hours", 3600);
        m_unit->addItem("Days", 86400);
        duration->addWidget(m_amount, 1);
        duration->addWidget(m_unit, 1);
        layout->addWidget(m_durationRow);
        auto updateTimerFields = [this](int mode) {
            m_date->setVisible(mode == 1);
            m_durationRow->setVisible(mode == 2);
        };
        connect(m_timerMode, &QComboBox::currentIndexChanged, this, updateTimerFields);
        updateTimerFields(m_timerMode->currentIndex());

        m_pinned = new QCheckBox("Pin in Notes");
        m_pinned->setChecked(entry.pinned);
        m_widget = new QCheckBox("Show this note in the widget");
        m_widget->setChecked(m_notebook->isWidgetNote(entry.id));
        layout->addWidget(m_pinned);
        layout->addWidget(m_widget);
        m_error = plainLabel({}, "error");
        m_error->hide();
        layout->addWidget(m_error);

        auto* actions = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
        root->addWidget(actions);
        connect(actions, &QDialogButtonBox::accepted, this, &NoteEditor::accept);
        connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
        if (!entry.id.isEmpty()) {
            auto* remove = actions->addButton("Delete", QDialogButtonBox::DestructiveRole);
            connect(remove, &QPushButton::clicked, this, [this] {
                if (QMessageBox::question(this, "Delete note", "Delete this note?",
                        QMessageBox::Yes | QMessageBox::Cancel, QMessageBox::Cancel) != QMessageBox::Yes) return;
                if (m_notebook->remove(m_entry.id)) QDialog::accept();
                else showError("Could not delete the note.");
            });
        }
    }

    void accept() override {
        Note result = m_entry;
        result.title = m_title->text().trimmed();
        result.body = m_text->toPlainText().trimmed();
        if (result.title.isEmpty() && result.body.isEmpty()) { showError("Write a title or some text."); return; }
        if (result.body.size() > 100000) { showError("Keep the note under 100,000 characters."); return; }
        result.category = m_category->currentText().simplified();
        for (const auto& category : m_notebook->categories())
            if (result.category.compare(category, Qt::CaseInsensitive) == 0) { result.category = category; break; }
        result.color = m_color->currentData().toString();
        result.pinned = m_pinned->isChecked();
        const qint64 now = QDateTime::currentSecsSinceEpoch();
        const int mode = m_timerMode->currentIndex();
        result.deadline = mode == 0 ? 0 : mode == 1 ? m_date->dateTime().toSecsSinceEpoch()
            : now + qint64(m_amount->value()) * m_unit->currentData().toInt();
        if (result.deadline && result.deadline != m_entry.deadline && result.deadline <= now) {
            showError("Choose a deadline in the future."); return;
        }
        if (m_entry.deadline != result.deadline) result.notified = false;
        if (m_notebook->save(result, m_widget->isChecked())) QDialog::accept();
        else showError("Could not save the note.");
    }

private:
    void showError(const QString& text) { m_error->setText(text); m_error->show(); }
    Notebook* m_notebook;
    Note m_entry;
    QLineEdit* m_title;
    QPlainTextEdit* m_text;
    QComboBox* m_category;
    QComboBox* m_color;
    QComboBox* m_timerMode;
    QDateTimeEdit* m_date;
    QWidget* m_durationRow;
    QSpinBox* m_amount;
    QComboBox* m_unit;
    QCheckBox* m_pinned;
    QCheckBox* m_widget;
    QLabel* m_error;
};
}

NotesWidget::NotesWidget(Notebook* notebook, bool external, QWidget* parent)
    : QWidget(parent), m_notebook(notebook) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    auto* surface = new QFrame;
    surface->setObjectName("settingsCard");
    auto* layout = new QVBoxLayout(surface);
    layout->setContentsMargins(14, 8, 14, 10);
    layout->setSpacing(4);
    auto* header = new QHBoxLayout;
    auto* notes = new QPushButton("Notes");
    notes->setObjectName("ghost");
    header->addWidget(notes);
    header->addStretch();
    m_open = new QPushButton("Open");
    m_open->setObjectName("ghost");
    header->addWidget(m_open);
    layout->addLayout(header);
    if (external) {
        setWindowTitle("LinkVault Notes");
        setWindowIcon(QIcon(":/icon.png"));
        setMinimumWidth(300);
        resize(350, 152);
    }
    m_title = plainLabel({}, "rowTitle");
    m_preview = plainLabel({}, "muted");
    m_clock = plainLabel({}, "noteClock");
    layout->addWidget(m_title);
    layout->addWidget(m_preview);
    layout->addWidget(m_clock);
    root->addWidget(surface);
    connect(m_open, &QPushButton::clicked, this, [this] { emit openRequested(m_notebook->widgetNote().id); });
    connect(notes, &QPushButton::clicked, this, &NotesWidget::notebookRequested);
    connect(m_notebook, &Notebook::changed, this, &NotesWidget::reload);
    connect(m_notebook, &Notebook::tick, this, &NotesWidget::renderTime);
    reload();
}

void NotesWidget::closeEvent(QCloseEvent* event) {
    QWidget::closeEvent(event);
    if (event->isAccepted()) emit closed();
}

void NotesWidget::reload() {
    const auto entry = m_notebook->widgetNote();
    m_title->setText(entry.id.isEmpty() ? "Keep a note or a countdown here" : entry.displayTitle().left(100));
    m_preview->setText(entry.body.simplified().left(130));
    m_preview->setVisible(!m_preview->text().isEmpty());
    m_open->setText(entry.id.isEmpty() ? "+ New note" : "Open note");
    renderTime(QDateTime::currentSecsSinceEpoch());
}

void NotesWidget::renderTime(qint64 now) {
    const QString countdown = m_notebook->widgetNote().countdown(now);
    m_clock->setText(countdown);
    m_clock->setVisible(!countdown.isEmpty());
}

NotesPage::NotesPage(Notebook* notebook, QWidget* parent) : QWidget(parent), m_notebook(notebook) {
    setObjectName("page");
    auto* root = new QVBoxLayout(this);
    const int margin = Platform::mobile ? 12 : 20;
    root->setContentsMargins(margin, margin, margin, 8);
    root->setSpacing(12);
    auto* header = new QHBoxLayout;
    auto* back = new QPushButton("← Library");
    back->setObjectName("ghost");
    header->addWidget(back);
    header->addWidget(plainLabel("Notes", "appTitle"), 1);
    if (!Platform::mobile) {
        auto* widget = new QPushButton("Desktop widget");
        header->addWidget(widget);
        connect(widget, &QPushButton::clicked, this, &NotesPage::desktopWidgetRequested);
    }
    auto* newNote = primaryButton("+ New note");
    header->addWidget(newNote);
    root->addLayout(header);

    auto* filters = new QBoxLayout(Platform::mobile ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
    m_search = new QLineEdit;
    m_search->setPlaceholderText("Search notes");
    m_search->setAccessibleName("Search notes");
    filters->addWidget(m_search, 1);
    auto* choices = new QHBoxLayout;
    m_categories = new QComboBox;
    m_categories->setAccessibleName("Filter by category");
    m_categories->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_categories->setMinimumContentsLength(12);
    choices->addWidget(m_categories, 1);
    m_filter = new QComboBox;
    m_filter->addItems({"All notes", "Countdowns", "Pinned"});
    m_filter->setAccessibleName("Filter by note type");
    choices->addWidget(m_filter);
    filters->addLayout(choices);
    root->addLayout(filters);

    auto* scroll = new QScrollArea;
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    if (Platform::mobile) QScroller::grabGesture(scroll->viewport(), QScroller::TouchGesture);
    auto* cards = new QWidget;
    m_cards = new QVBoxLayout(cards);
    m_cards->setContentsMargins(0, 2, 0, 8);
    m_cards->setSpacing(10);
    scroll->setWidget(cards);
    root->addWidget(scroll, 1);

    connect(back, &QPushButton::clicked, this, &NotesPage::backRequested);
    connect(newNote, &QPushButton::clicked, this, [this] { openEditor(); });
    connect(m_notebook, &Notebook::changed, this, &NotesPage::reload);
    connect(m_search, &QLineEdit::textChanged, this, &NotesPage::rebuildCards);
    connect(m_categories, &QComboBox::currentIndexChanged, this, &NotesPage::rebuildCards);
    connect(m_filter, &QComboBox::currentIndexChanged, this, &NotesPage::rebuildCards);
    reload();
}

void NotesPage::openEditor(const QString& id) {
    NoteEditor editor(m_notebook, m_notebook->note(id), this);
    editor.exec();
}

void NotesPage::reload() {
    const QString current = m_categories->currentData().toString();
    const QSignalBlocker block(m_categories);
    m_categories->clear();
    m_categories->addItem("All categories", QString());
    for (const auto& category : m_notebook->categories()) m_categories->addItem(category, category);
    m_categories->setCurrentIndex(qMax(0, m_categories->findData(current)));
    rebuildCards();
}

void NotesPage::rebuildCards() {
    while (auto* item = m_cards->takeAt(0)) {
        if (auto* widget = item->widget()) { widget->hide(); widget->deleteLater(); }
        delete item;
    }
    const QString category = m_categories->currentData().toString();
    const auto search = m_search->text().simplified().split(' ', Qt::SkipEmptyParts);
    const int filter = m_filter->currentIndex();
    auto notes = m_notebook->notes();
    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        return a.pinned != b.pinned ? a.pinned : a.updated > b.updated;
    });
    for (const auto& entry : notes) {
        if (!category.isEmpty() && entry.category != category) continue;
        if ((filter == 1 && entry.deadline <= 0) || (filter == 2 && !entry.pinned)) continue;
        const QString content = entry.title + "\n" + entry.body + "\n" + entry.category;
        if (std::any_of(search.cbegin(), search.cend(), [&content](const QString& word) {
                return !content.contains(word, Qt::CaseInsensitive);
            })) continue;

        auto* card = new QFrame;
        card->setObjectName("settingsCard");
        const QColor accent(entry.color);
        if (accent.isValid())
            card->setStyleSheet(QString("#settingsCard { border-left: 4px solid %1; }").arg(accent.name()));
        auto* layout = new QVBoxLayout(card);
        layout->setContentsMargins(14, 9, 14, 12);
        layout->setSpacing(5);
        auto* row = new QHBoxLayout;
        row->addWidget(plainLabel((entry.pinned ? QStringLiteral("📌 ") : QString()) + entry.displayTitle(), "rowTitle"), 1);
        auto* edit = new QPushButton("Edit");
        edit->setObjectName("ghost");
        edit->setAccessibleName(QString("Edit %1").arg(entry.displayTitle()));
        row->addWidget(edit);
        layout->addLayout(row);
        if (!entry.category.isEmpty()) layout->addWidget(plainLabel(entry.category, "cardTitle"));
        if (!entry.body.isEmpty()) {
            const QString body = entry.body.simplified();
            layout->addWidget(plainLabel(body.size() > 220 ? body.left(220) + "…" : body, "muted"));
        }
        if (entry.deadline > 0) {
            auto* clock = plainLabel(entry.countdown(QDateTime::currentSecsSinceEpoch()), "noteClock");
            clock->setToolTip(QLocale().toString(QDateTime::fromSecsSinceEpoch(entry.deadline), QLocale::LongFormat));
            layout->addWidget(clock);
            connect(m_notebook, &Notebook::tick, clock, [clock, entry](qint64 now) {
                clock->setText(entry.countdown(now));
            });
        }
        connect(edit, &QPushButton::clicked, this, [this, id = entry.id] { openEditor(id); });
        m_cards->addWidget(card);
    }
    if (m_cards->count() == 0) {
        const QString message = notes.isEmpty() ? "Create a note to start your notebook." : "No notes match these filters.";
        m_cards->addWidget(plainLabel(message, "muted"));
    }
    m_cards->addStretch();
}
