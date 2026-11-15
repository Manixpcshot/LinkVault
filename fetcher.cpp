#include "fetcher.h"
#include "bookmark.h"
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QImage>

static const QByteArray kUA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36";

static const auto CI = QRegularExpression::CaseInsensitiveOption;

static QString attr(const QString& tag, const char* name) {
    const QRegularExpression re(QString(R"(\b%1\s*=\s*["']([^"']*)["'])").arg(QLatin1String(name)), CI);
    const auto m = re.match(tag);
    return m.hasMatch() ? m.captured(1).trimmed().replace("&amp;", "&") : QString();
}

static QString metaContent(const QString& html, const QString& key) {
    static const QRegularExpression tagRe(R"(<meta\b[^>]*>)", CI);
    auto it = tagRe.globalMatch(html);
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        QString k = attr(tag, "property");
        if (k.isEmpty()) k = attr(tag, "name");
        if (k.compare(key, Qt::CaseInsensitive) == 0) {
            const QString c = attr(tag, "content");
            if (!c.isEmpty()) return c;
        }
    }
    return {};
}

static QList<QUrl> linkIcons(const QString& html, const QUrl& base) {
    static const QRegularExpression tagRe(R"(<link\b[^>]*>)", CI);
    QList<QUrl> apple, other;
    auto it = tagRe.globalMatch(html);
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        const QString rel = attr(tag, "rel").toLower();
        const QString href = attr(tag, "href");
        if (!rel.contains("icon") || href.isEmpty()) continue;
        if (href.startsWith("data:") || href.contains(".svg", Qt::CaseInsensitive)) continue;
        const QUrl u = base.resolved(QUrl(href));
        (rel.contains("apple-touch") ? apple : other) << u;
    }
    return apple + other;
}

static QString pageTitle(const QString& html) {
    static const QRegularExpression re(R"(<title[^>]*>(.*?)</title>)",
        QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
    const auto m = re.match(html);
    if (!m.hasMatch()) return {};
    return QTextDocumentFragment::fromHtml(m.captured(1)).toPlainText().simplified();
}

ThumbnailFetcher::ThumbnailFetcher(QObject* parent) : QObject(parent) {}

QNetworkRequest ThumbnailFetcher::makeRequest(const QUrl& url) {
    QNetworkRequest r(url);
    r.setHeader(QNetworkRequest::UserAgentHeader, kUA);
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setTransferTimeout(12000);
    return r;
}

void ThumbnailFetcher::fetch(const QString& id, const QUrl& url) {
    QNetworkReply* reply = m_nam.get(makeRequest(url));
    connect(reply, &QNetworkReply::finished, this, [this, reply, id, url] {
        reply->deleteLater();
        auto job = std::make_shared<Job>();
        job->id = id;
        QList<QUrl> covers, icons;
        if (reply->error() == QNetworkReply::NoError) {
            const QString html = QString::fromUtf8(reply->readAll().left(1536 * 1024));
            const QUrl base = reply->url();
            job->title = pageTitle(html);
            for (const char* key : {"og:image", "og:image:url", "og:image:secure_url", "twitter:image"}) {
                const QString c = metaContent(html, QString::fromLatin1(key));
                if (!c.isEmpty()) covers << base.resolved(QUrl(c));
            }
            icons << linkIcons(html, base);
        }
        const QString host = url.host();
        icons << QUrl("https://www.google.com/s2/favicons?domain=" + host + "&sz=128")
              << QUrl("https://icons.duckduckgo.com/ip3/" + host + ".ico")
              << QUrl(url.scheme() + "://" + host + "/favicon.ico");
        job->pending = 2;
        download(job, covers, true);
        download(job, icons, false);
    });
}

void ThumbnailFetcher::download(JobPtr job, QList<QUrl> candidates, bool isCover) {
    if (candidates.isEmpty()) { done(job); return; }
    const QUrl u = candidates.takeFirst();
    QNetworkReply* reply = m_nam.get(makeRequest(u));
    connect(reply, &QNetworkReply::finished, this, [this, reply, job, candidates, isCover] {
        reply->deleteLater();
        QImage img;
        if (reply->error() == QNetworkReply::NoError) img.loadFromData(reply->readAll());
        const bool ok = !img.isNull() && (isCover ? (img.width() >= 200 && img.height() >= 100)
                                                  : img.width() >= 16);
        if (!ok) { download(job, candidates, isCover); return; }

        if (isCover) {
            if (img.width() > 900) img = img.scaledToWidth(900, Qt::SmoothTransformation);
            const QString path = Storage::thumbsDir() + "/" + job->id + "_cover.jpg";
            QImage rgb = img.convertToFormat(QImage::Format_RGB32);
            if (rgb.save(path, "JPG", 88)) job->cover = path;
        } else {
            if (img.width() > 128) img = img.scaled(128, 128, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            const QString path = Storage::thumbsDir() + "/" + job->id + "_icon.png";
            if (img.save(path, "PNG")) job->icon = path;
        }
        done(job);
    });
}

void ThumbnailFetcher::done(JobPtr job) {
    if (--job->pending == 0) emit finished(job->id, job->cover, job->icon, job->title);
}
