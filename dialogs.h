#pragma once
#include <QDialog>
#include <QDateTime>
#include <QUrl>
class QLineEdit;
class QLabel;
class QPushButton;
class QDateTimeEdit;

// shared URL helpers (also used by Quick Add)
QUrl parseUserUrl(QString s);
bool isWebUrl(const QUrl& u);

class BookmarkDialog : public QDialog {
    Q_OBJECT
public:
    BookmarkDialog(QWidget* parent, const QString& title, const QString& url = {}, const QString& name = {});
    QString url() const;
    QString name() const;
    void accept() override;
private:
    QLineEdit* m_url;
    QLineEdit* m_name;
    QLabel* m_error;
};

// create / rename a collection: name + emoji picker
class CollectionDialog : public QDialog {
    Q_OBJECT
public:
    CollectionDialog(QWidget* parent, const QString& title, const QString& name = {}, const QString& emoji = {});
    QString name() const;
    QString emoji() const { return m_emoji; }
    void accept() override;
private:
    QLineEdit* m_name;
    QLabel* m_error;
    QString m_emoji;
    QList<QPushButton*> m_emojiBtns;
};

// custom date/time for a "read later" reminder
class ReminderDialog : public QDialog {
    Q_OBJECT
public:
    ReminderDialog(QWidget* parent, const QDateTime& initial);
    QDateTime dateTime() const;
private:
    QDateTimeEdit* m_edit;
};
