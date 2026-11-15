#pragma once
#include <QObject>
#include <QUrl>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <memory>

// Downloads a cover (og:image) and a favicon for a site, in parallel.
class ThumbnailFetcher : public QObject {
    Q_OBJECT
public:
    explicit ThumbnailFetcher(QObject* parent = nullptr);
    void fetch(const QString& id, const QUrl& url);

signals:
    void finished(const QString& id, const QString& coverPath, const QString& iconPath, const QString& pageTitle);

private:
    struct Job { QString id, title, cover, icon; int pending = 0; };
    using JobPtr = std::shared_ptr<Job>;
    void download(JobPtr job, QList<QUrl> candidates, bool isCover);
    void done(JobPtr job);
    static QNetworkRequest makeRequest(const QUrl& url);
    QNetworkAccessManager m_nam;
};
