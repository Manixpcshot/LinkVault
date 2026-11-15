#pragma once
#include <QNetworkAccessManager>
#include <QObject>
#include <QStringList>
#include <QUrl>

// Background link health check: HEAD first, GET fallback, max 4 requests in flight.
// 404/410/DNS failure = broken. A redirect to a different site/path = redirect.
// Timeouts, 5xx, 401/403 (bot protection) are NOT flagged, to avoid false alarms.
class LinkChecker : public QObject {
    Q_OBJECT
public:
    explicit LinkChecker(QObject* parent = nullptr);
    void enqueue(const QString& id, const QUrl& url);
    bool isBusy() const { return m_active > 0 || !m_queue.isEmpty(); }
    int pending() const { return m_active + int(m_queue.size()); }

signals:
    void checked(const QString& id, int health, const QString& redirectTo);
    void idle();

private:
    struct Job { QString id; QUrl url; };
    void pump();
    void run(const Job& job, bool useGet);
    void finish(const Job& job, int health, const QString& redirect);
    static bool sameTarget(const QUrl& a, const QUrl& b);
    QNetworkAccessManager m_nam;
    QList<Job> m_queue;
    QStringList m_queued;
    int m_active = 0;
};
