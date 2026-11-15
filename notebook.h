#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QVBoxLayout;

struct Note {
    QString id, title, body, category, color;
    qint64 updated = 0;
    qint64 deadline = 0;  // Unix seconds. Zero means there is no countdown.
    bool pinned = false;
    bool notified = false;

    QJsonObject toJson() const;
    static Note fromJson(const QJsonObject& obj);
    QString displayTitle() const;
    QString countdown(qint64 now) const;
};

class Notebook : public QObject {
    Q_OBJECT
public:
    explicit Notebook(QObject* parent = nullptr);
    const QList<Note>& notes() const { return m_notes; }
    QStringList categories() const;
    Note note(const QString& id) const;
    Note widgetNote() const;
    bool isWidgetNote(const QString& id) const { return !id.isEmpty() && m_widgetId == id; }
    bool save(Note note, bool showInWidget);
    bool remove(const QString& id);
    QJsonObject backup() const;
    bool restore(const QJsonObject& data);
    void synchronizeWidget() const;

signals:
    void changed();
    void tick(qint64 now);
    void countdownFinished(const QString& title);

private:
    bool commit(const QList<Note>& notes, const QString& widgetId, bool announce = true);
    void updateClock();
    QList<Note> m_notes;
    QString m_widgetId;
    QString m_file;
    QTimer m_clock;
};

class NotesWidget : public QWidget {
    Q_OBJECT
public:
    explicit NotesWidget(Notebook* notebook, bool external, QWidget* parent = nullptr);
    void reload();

signals:
    void openRequested(const QString& id);
    void notebookRequested();
    void closed();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void renderTime(qint64 now);
    Notebook* m_notebook;
    QLabel* m_title;
    QLabel* m_preview;
    QLabel* m_clock;
    QPushButton* m_open;
};

class NotesPage : public QWidget {
    Q_OBJECT
public:
    explicit NotesPage(Notebook* notebook, QWidget* parent = nullptr);
    void openEditor(const QString& id = {});

signals:
    void backRequested();
    void desktopWidgetRequested();

private:
    void reload();
    void rebuildCards();
    Notebook* m_notebook;
    QLineEdit* m_search;
    QComboBox* m_categories;
    QComboBox* m_filter;
    QVBoxLayout* m_cards;
};
