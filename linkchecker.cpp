#include "linkchecker.h"
#include "bookmark.h"
#include <QNetworkReply>
#include <QNetworkRequest>

static constexpr int kMaxParallel = 4;
static const QByteArray kUA =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0 Safari/537.36";

LinkChecker::LinkChecker(QObject* parent) : QObject(parent) {}

void LinkChecker::enqueue(const QString& id, const QUrl& url) {
    if (m_queued.contains(id) || !url.isValid()) return;
    m_queued << id;
    m_queue << Job{id, url};
    pump();
}

void LinkChecker::pump() {
    while (m_active < kMaxParallel && !m_queue.isEmpty()) {
        ++m_active;
        run(m_queue.takeFirst(), false);
    }
}

// http->https, "www." and trailing slash differences are not real redirects
bool LinkChecker::sameTarget(const QUrl& a, const QUrl& b) {
    auto key = [](const QUrl& u) {
        QString h = u.host().toLower();
        if (h.startsWith("www.")) h = h.mid(4);
        QString p = u.path();
        while (p.endsWith('/')) p.chop(1);
        return h + p + "?" + u.query();
    };
    return key(a) == key(b);
}

void LinkChecker::run(const Job& job, bool useGet) {
    QNetworkRequest r(job.url);
    r.setHeader(QNetworkRequest::UserAgentHeader, kUA);
    r.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    r.setMaximumRedirectsAllowed(8);
    r.setTransferTimeout(15000);
    QNetworkReply* reply = useGet ? m_nam.get(r) : m_nam.head(r);
    if (useGet) {
        // we only need the status line, not the page: stop as soon as the final headers arrive
        connect(reply, &QNetworkReply::metaDataChanged, reply, [reply] {
            const QVariant st = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
            if (!st.isValid() || reply->attribute(QNetworkRequest::RedirectionTargetAttribute).isValid()) return;
            if (reply->property("lvStatus").isValid()) return;
            reply->setProperty("lvStatus", st);
            reply->setProperty("lvUrl", reply->url());
            QMetaObject::invokeMethod(reply, [reply] { reply->abort(); }, Qt::QueuedConnection);
        });
    }
    connect(reply, &QNetworkReply::finished, this, [this, reply, job, useGet] {
        reply->deleteLater();
        const bool early = reply->property("lvStatus").isValid();    // GET we aborted on purpose
        const int code = early ? reply->property("lvStatus").toInt()
                               : reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QNetworkReply::NetworkError err = early ? QNetworkReply::NoError : reply->error();
        const QUrl finalUrl = early ? reply->property("lvUrl").toUrl() : reply->url();

        if (!useGet && (code == 405 || code == 403 || code == 501 || code == 400 || code == 404
                        || err == QNetworkReply::ContentOperationNotPermittedError
                        || err == QNetworkReply::UnknownContentError)) {
            run(job, true);                                   // many sites reject HEAD; try GET
            return;
        }
        if (err == QNetworkReply::HostNotFoundError) { finish(job, HealthBroken, {}); return; }
        if (code == 404 || code == 410) { finish(job, HealthBroken, {}); return; }
        if (code >= 200 && code < 400) {
            if (!sameTarget(finalUrl, job.url)) { finish(job, HealthRedirect, finalUrl.toString()); return; }
            finish(job, HealthOk, {});
            return;
        }
        finish(job, HealthUnknown, {});                       // inconclusive: don't scare the user
    });
}

void LinkChecker::finish(const Job& job, int health, const QString& redirect) {
    m_active = qMax(0, m_active - 1);
    m_queued.removeAll(job.id);
    emit checked(job.id, health, redirect);
    pump();
    if (m_active == 0 && m_queue.isEmpty()) emit idle();
}
