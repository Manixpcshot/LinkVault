#include <cmath>
#include "card.h"
#include "theme.h"
#include "platform.h"
#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QMimeData>
#include <QDateTime>
#include <QEnterEvent>
#include <QHash>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTextLayout>
#include <QTextDocument>
#include <QTimer>
#include <QUrl>
#include <QVariantAnimation>
#include <QtMath>
#include <vector>

namespace {
struct Palette { QColor bg, border, text, muted, accent, chip; };
Palette pal(bool dark) {
    if (dark && Theme::isBlack()) {
        const auto c = Theme::colors(dark);
        return {c.card, c.border, c.text, c.muted, c.accent, QColor(255, 255, 255, 22)};
    }
    return dark ? Palette{QColor("#1B1D2B"), QColor("#2D3044"), QColor("#EEF0F7"),
                          QColor("#8A8FA6"), QColor("#8B7CFF"), QColor(255, 255, 255, 14)}
                : Palette{QColor("#FFFFFF"), QColor("#E8E6F3"), QColor("#1C1D29"),
                          QColor("#7C8093"), QColor("#6C5CE7"), QColor(40, 30, 110, 12)};
}
using Theme::mix;

QColor hashColor(const QString& s) { return QColor::fromHsl(int(qHash(s) % 360), 160, 130); }

// brand color from favicon: weighted average of saturated opaque pixels
QColor dominant(const QImage& src) {
    if (src.isNull()) return {};
    const QImage img = src.convertToFormat(QImage::Format_ARGB32)
                          .scaled(32, 32, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    double r = 0, g = 0, b = 0, n = 0;
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x) {
            const QRgb px = img.pixel(x, y);
            if (qAlpha(px) < 128) continue;
            const QColor c(px);
            const double w = c.hslSaturationF();
            if (w < 0.18 || c.lightnessF() > 0.94 || c.lightnessF() < 0.08) continue;
            r += c.redF() * w; g += c.greenF() * w; b += c.blueF() * w; n += w;
        }
    if (n < 0.5) return {};
    const QColor c = QColor::fromRgbF(r / n, g / n, b / n);
    if (c.hslHue() < 0) return {};
    return QColor::fromHsl(c.hslHue(), qBound(110, c.hslSaturation(), 210), qBound(95, c.lightness(), 150));
}

QString relTime(qint64 secs) {
    if (secs <= 0) return {};
    const int days = int(QDateTime::fromSecsSinceEpoch(secs).date().daysTo(QDate::currentDate()));
    if (days <= 0) return "Today";
    if (days == 1) return "Yesterday";
    if (days < 7) return QString("%1 days ago").arg(days);
    if (days < 14) return "1 week ago";
    if (days < 30) return QString("%1 weeks ago").arg(days / 7);
    if (days < 60) return "1 month ago";
    if (days < 365) return QString("%1 months ago").arg(days / 30);
    if (days < 730) return "1 year ago";
    return QString("%1 years ago").arg(days / 365);
}

QVariantAnimation* makeAnim(QWidget* w, qreal* value, int ms, const QEasingCurve& curve) {
    auto* a = new QVariantAnimation(w);
    a->setDuration(ms);
    a->setEasingCurve(curve);
    QObject::connect(a, &QVariantAnimation::valueChanged, w, [w, value](const QVariant& v) {
        *value = v.toReal();
        w->update();
    });
    return a;
}

void runAnim(QVariantAnimation* a, qreal from, qreal to, int ms) {
    a->stop();
    a->setDuration(Theme::animations() ? ms : 1);
    a->setStartValue(from);
    a->setEndValue(to);
    a->start();
}

// ---- soft shadow, painted (much cheaper than QGraphicsDropShadowEffect per card) ----
void boxBlurAlpha(std::vector<int>& a, int w, int h, int r) {
    if (r < 1) return;
    std::vector<int> tmp(a.size());
    const int d = 2 * r + 1;
    for (int pass = 0; pass < 3; ++pass) {
        for (int y = 0; y < h; ++y) {                       // horizontal
            const int* row = &a[size_t(y) * w];
            int* out = &tmp[size_t(y) * w];
            int sum = 0;
            for (int i = 0; i <= r && i < w; ++i) sum += row[i];
            for (int x = 0; x < w; ++x) {
                out[x] = sum / d;
                if (x + r + 1 < w) sum += row[x + r + 1];
                if (x - r >= 0) sum -= row[x - r];
            }
        }
        for (int x = 0; x < w; ++x) {                       // vertical
            int sum = 0;
            for (int i = 0; i <= r && i < h; ++i) sum += tmp[size_t(i) * w + x];
            for (int y = 0; y < h; ++y) {
                a[size_t(y) * w + x] = sum / d;
                if (y + r + 1 < h) sum += tmp[size_t(y + r + 1) * w + x];
                if (y - r >= 0) sum -= tmp[size_t(y - r) * w + x];
            }
        }
    }
}

QImage shadowImage(bool dark, bool hover, qreal dpr) {
    static QHash<QString, QImage> cache;
    const QString key = QString("%1-%2-%3").arg(dark).arg(hover).arg(dpr);
    auto it = cache.find(key);
    if (it != cache.end()) return it.value();

    const QSize sz = (QSizeF(BookmarkCard::kW, BookmarkCard::kH) * dpr).toSize();
    QImage img(sz, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.scale(dpr, dpr);
        p.setPen(Qt::NoPen);
        p.setBrush(Qt::black);
        const qreal dy = hover ? 9 : 4;
        const qreal lift = hover ? -5 : 0;
        p.drawRoundedRect(QRectF(BookmarkCard::kMargin + 6, BookmarkCard::kMargin + lift + dy,
                                 BookmarkCard::kInnerW - 12, BookmarkCard::kInnerH - 8), 18, 18);
    }
    const int w = img.width(), h = img.height();
    std::vector<int> alpha(size_t(w) * h);
    for (int y = 0; y < h; ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < w; ++x) alpha[size_t(y) * w + x] = qAlpha(line[x]);
    }
    boxBlurAlpha(alpha, w, h, int((hover ? 7 : 5) * dpr));
    const QColor col = dark ? QColor(0, 0, 0, hover ? 190 : 140) : QColor(60, 40, 150, hover ? 85 : 42);
    for (int y = 0; y < h; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int a = alpha[size_t(y) * w + x] * col.alpha() / 255;
            line[x] = qPremultiply(qRgba(col.red(), col.green(), col.blue(), a));
        }
    }
    img.setDevicePixelRatio(dpr);
    cache.insert(key, img);
    return img;
}

// pushpin glyph; `tilt` 0 = upright (pinned), 1 = leaning (not pinned)
void drawPin(QPainter& p, const QPointF& c, qreal s, const QColor& col, qreal tilt) {
    p.save();
    p.translate(c);
    p.rotate(40 * tilt);
    p.setPen(Qt::NoPen);
    p.setBrush(col);
    p.drawRoundedRect(QRectF(-s * 0.30, -s * 0.52, s * 0.60, s * 0.20), s * 0.06, s * 0.06);
    QPolygonF body;
    body << QPointF(-s * 0.18, -s * 0.34) << QPointF(s * 0.18, -s * 0.34)
         << QPointF(s * 0.30, s * 0.06) << QPointF(-s * 0.30, s * 0.06);
    p.drawPolygon(body);
    p.drawRoundedRect(QRectF(-s * 0.40, s * 0.02, s * 0.80, s * 0.14), s * 0.05, s * 0.05);
    p.setPen(QPen(col, qMax<qreal>(1.4, s * 0.10), Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(0, s * 0.16), QPointF(0, s * 0.54));
    p.restore();
}
} // namespace

// ============================== BookmarkCard ==============================

BookmarkCard::BookmarkCard(const Bookmark& b, bool dark, QWidget* parent)
    : QWidget(parent), m_dark(dark) {
    setFixedSize(kW, kH);
    setAttribute(Qt::WA_Hover);
    setMouseTracking(true);
    setCursor(Qt::PointingHandCursor);

    m_anim = makeAnim(this, &m_hover, 280, Theme::easeOut());
    m_appearAnim = makeAnim(this, &m_appear, 560, Theme::easeOut());
    m_pressAnim = makeAnim(this, &m_press, 140, Theme::easeOut());
    m_pinAnim = makeAnim(this, &m_pinT, 460, Theme::easeSpring(1.35));

    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    m_timer->setInterval(16);                           // ~60 fps (was 30 ms = 33 fps)
    connect(m_timer, &QTimer::timeout, this, [this] {
        // time based, so the shimmer speed stays even if a frame is late
        m_phase = -0.5 + std::fmod(m_clock.elapsed() / 2700.0, 1.0) * 2.0;
        update();
    });

    setBookmark(b);
    m_initialized = true;
}

void BookmarkCard::setBookmark(const Bookmark& b) {
    m_id = b.id;
    m_url = b.url;
    m_name = b.name;
    m_host = QUrl(b.url).host();
    if (m_host.startsWith("www.")) m_host = m_host.mid(4);
    m_date = relTime(b.created);
    m_health = b.health;
    m_remindAt = b.remindAt;
    QString tip = b.name + "\n" + b.url;
    m_hasNote = !b.note.isEmpty();
    if (m_hasNote) tip += "\n\nNote: " + b.note;
    if (b.health == HealthBroken) tip += QString::fromUtf8("\n⚠️ This page could not be found.");
    else if (b.health == HealthRedirect) tip += QString::fromUtf8("\n↪ Redirected to: ") + b.redirectTo;
    if (b.remindAt > 0)
        tip += QString::fromUtf8("\n⏰ Reminder: ") + QDateTime::fromSecsSinceEpoch(b.remindAt).toString("yyyy/MM/dd  HH:mm");
    setToolTip(Qt::convertFromPlainText(tip));

    if (b.pinned != m_pinned || !m_initialized) {
        m_pinned = b.pinned;
        if (m_initialized && isVisible()) runAnim(m_pinAnim, m_pinT, m_pinned ? 1.0 : 0.0, 460);
        else m_pinT = m_pinned ? 1 : 0;
    }

    QImage cover(b.imagePath), icon(b.iconPath);
    if (!cover.isNull() && cover.width() < 200) {       // old data: small favicon stored as image
        if (icon.isNull()) icon = cover;
        cover = QImage();
    }

    const qreal dpr = qApp->devicePixelRatio();
    m_cover = QImage();
    if (!cover.isNull()) {
        const QSize target = QSizeF(kInnerW * 1.08 * dpr, kImgH * 1.08 * dpr).toSize();
        QImage s = cover.scaled(target, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        m_cover = s.copy((s.width() - target.width()) / 2, (s.height() - target.height()) / 2,
                         target.width(), target.height());
    }
    m_iconBig = icon.isNull() ? QImage() : icon.scaled(QSize(40, 40) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_iconSmall = icon.isNull() ? QImage() : icon.scaled(QSize(24, 24) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    m_accent = dominant(icon);
    if (!m_accent.isValid()) m_accent = dominant(cover);
    if (!m_accent.isValid()) m_accent = hashColor(m_host.isEmpty() ? m_name : m_host);
    update();
}

void BookmarkCard::setLoading(bool on) {
    if (on == m_loading) return;
    m_loading = on;
    if (on) { m_phase = -0.5; m_clock.start(); m_timer->start(); }
    else m_timer->stop();
    update();
}

void BookmarkCard::setCollectionTag(const QString& text, const QColor& color) {
    if (text == m_tag && color == m_tagColor) return;
    m_tag = text;
    m_tagColor = color;
    update();
}

void BookmarkCard::setDark(bool dark) { m_dark = dark; update(); }

void BookmarkCard::setHighlight(const QStringList& tokens) {
    if (tokens == m_tokens) return;
    m_tokens = tokens;
    update();
}

void BookmarkCard::playAppear(int delayMs) {
    m_appearAnim->stop();
    if (!Theme::animations()) { m_appear = 1; update(); return; }
    m_appear = 0;
    update();
    QTimer::singleShot(delayMs, this, [this] {
        if (!m_removing) runAnim(m_appearAnim, 0.0, 1.0, 560);
    });
}

void BookmarkCard::playRemove(std::function<void()> done) {
    m_removing = true;
    m_appearAnim->stop();
    if (!Theme::animations()) { done(); return; }
    auto* a = new QVariantAnimation(this);
    a->setDuration(260);
    a->setEasingCurve(Theme::easeIn());
    a->setStartValue(m_appear);
    a->setEndValue(0.0);
    connect(a, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_appear = v.toReal(); update(); });
    connect(a, &QVariantAnimation::finished, this, [done] { done(); });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void BookmarkCard::enterEvent(QEnterEvent* e) { runAnim(m_anim, m_hover, 1.0, 280); QWidget::enterEvent(e); }
void BookmarkCard::leaveEvent(QEvent* e) {
    runAnim(m_anim, m_hover, 0.0, 340);                 // leave a bit slower than enter = softer
    if (m_press > 0) runAnim(m_pressAnim, m_press, 0.0, 220);
    m_hoverBtn = 0;
    QWidget::leaveEvent(e);
}

void BookmarkCard::drawHighlighted(QPainter& p, const QRectF& r, const QFont& f, const QString& text,
                                   const QColor& color, Qt::TextElideMode mode) {
    const QString shown = QFontMetricsF(f).elidedText(text, mode, r.width());
    QTextLayout layout(shown, f);
    QTextOption opt(Qt::AlignLeft | Qt::AlignAbsolute);
    opt.setWrapMode(QTextOption::NoWrap);
    opt.setTextDirection(shown.isRightToLeft() ? Qt::RightToLeft : Qt::LeftToRight);
    layout.setTextOption(opt);

    QList<QTextLayout::FormatRange> ranges;
    QColor bg = pal(m_dark).accent; bg.setAlpha(m_dark ? 70 : 45);
    for (const QString& tok : m_tokens) {
        if (tok.isEmpty()) continue;
        int from = 0, idx;
        while ((idx = int(shown.indexOf(tok, from, Qt::CaseInsensitive))) >= 0) {
            QTextLayout::FormatRange fr;
            fr.start = idx;
            fr.length = int(tok.size());
            fr.format.setBackground(bg);
            fr.format.setForeground(m_dark ? QColor("#C9C1FF") : pal(false).accent.darker(115));
            ranges << fr;
            from = idx + int(tok.size());
        }
    }
    layout.setFormats(ranges);
    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (!line.isValid()) { layout.endLayout(); return; }
    line.setLineWidth(r.width());
    line.setPosition(QPointF(0, 0));
    layout.endLayout();
    p.setPen(color);
    layout.draw(&p, QPointF(r.left(), r.top() + (r.height() - line.height()) / 2));
}

void BookmarkCard::paintEvent(QPaintEvent*) {
    const Palette c = pal(m_dark);
    const qreal a = m_appear;
    if (a <= 0.002) return;

    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform | QPainter::TextAntialiasing);

    // appear / press transform: fade + rise + scale
    const qreal s = (0.95 + 0.05 * a) * (1.0 - 0.03 * m_press);
    const QPointF ctr = QRectF(rect()).center();
    p.translate(ctr.x(), ctr.y() + (1.0 - a) * 12);
    p.scale(s, s);
    p.translate(-ctr.x(), -ctr.y());

    const qreal t = m_hover;
    const qreal dpr = devicePixelRatioF();
    if (t < 0.999) { p.setOpacity(a * (1 - t)); p.drawImage(QPointF(0, 0), shadowImage(m_dark, false, dpr)); }
    if (t > 0.001) { p.setOpacity(a * t);       p.drawImage(QPointF(0, 0), shadowImage(m_dark, true, dpr)); }
    p.setOpacity(a);

    const qreal radius = 18;
    const QRectF card(kMargin, kMargin - 5 * t, kInnerW, kInnerH);
    QPainterPath shape;
    shape.addRoundedRect(card, radius, radius);
    p.fillPath(shape, c.bg);

    // ---- media ----
    const QRectF img(card.left(), card.top(), card.width(), kImgH);
    p.save();
    p.setClipPath(shape);
    p.setClipRect(img, Qt::IntersectClip);
    paintMedia(p, img, t);
    if (m_loading) paintShimmer(p, img);
    p.restore();

    // ---- border (glows to accent on hover, tinted when pinned) ----
    QPainterPath borderPath;
    borderPath.addRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(mix(c.border, c.accent, qMax(t, qBound<qreal>(0, m_pinT, 1) * 0.55)), 1.2));
    p.drawPath(borderPath);

    // ---- link health marker (bottom-left of the image) ----
    if (m_health == HealthBroken || m_health == HealthRedirect) {
        const bool broken = m_health == HealthBroken;
        const QPointF hc(img.left() + 20, img.bottom() - 20);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(0, 0, 0, 60));
        p.drawEllipse(hc + QPointF(0, 1.5), 12, 12);
        p.setBrush(Qt::white);
        p.drawEllipse(hc, 12, 12);
        p.setBrush(broken ? QColor("#E5484D") : QColor("#F5A524"));
        p.drawEllipse(hc, 10, 10);
        QFont gf = font(); gf.setPixelSize(13); gf.setBold(true);
        p.setFont(gf);
        p.setPen(Qt::white);
        p.drawText(QRectF(hc.x() - 10, hc.y() - 10, 20, 20), Qt::AlignCenter,
                   broken ? QStringLiteral("!") : QString::fromUtf8("↪"));
    }

    // ---- favicon badge overlapping the image ----
    const qreal badge = 46;
    paintBadge(p, QPointF(card.right() - 18 - badge / 2, img.bottom()));

    // ---- chips: date, reminder, collection ----
    const qreal pad = 16;
    {
        QFont df = font(); df.setPixelSize(11);
        p.setFont(df);
        const QFontMetricsF fm(df);
        qreal x = card.left() + pad - 2;
        const qreal limit = card.right() - 18 - badge - 8;          // keep clear of the favicon badge
        auto chip = [&](const QString& text, const QColor& bg, const QColor& fg) {
            if (text.isEmpty() || x >= limit - 30) return;
            const qreal w = qMin(fm.horizontalAdvance(text) + 18, limit - x);
            const QRectF r(x, img.bottom() + 10, w, 22);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(r, 11, 11);
            p.setPen(fg);
            p.drawText(r.adjusted(9, 0, -9, 0), Qt::AlignCenter, fm.elidedText(text, Qt::ElideRight, w - 18));
            x += w + 6;
        };
        chip(m_date, c.chip, c.muted);
        if (m_hasNote) chip(QStringLiteral("✎"), c.chip, c.accent);
        if (m_remindAt > 0) {
            QColor bg = c.accent; bg.setAlpha(m_dark ? 50 : 30);
            chip(QString::fromUtf8("⏰ ") + QDateTime::fromSecsSinceEpoch(m_remindAt).toString("HH:mm"), bg, c.accent);
        }
        if (!m_tag.isEmpty()) {
            const QColor tc = m_tagColor.isValid() ? m_tagColor : c.accent;
            QColor bg = tc; bg.setAlpha(m_dark ? 55 : 32);
            chip(m_tag, bg, m_dark ? tc.lighter(150) : tc.darker(130));
        }
    }

    // ---- name (with search highlight) ----
    const QRectF nameR(card.left() + pad, img.bottom() + 34, card.width() - 2 * pad, 24);
    QFont nf = font(); nf.setPixelSize(16); nf.setWeight(QFont::DemiBold);
    drawHighlighted(p, nameR, nf, m_name, c.text, Qt::ElideRight);

    // ---- host with accent dot ----
    QFont hf = font(); hf.setPixelSize(12);
    const QRectF hostR(card.left() + pad, nameR.bottom() + 3, card.width() - 2 * pad - 14, 18);
    p.setPen(Qt::NoPen);
    p.setBrush(m_accent);
    p.drawEllipse(QPointF(hostR.left() + 3.5, hostR.center().y()), 3.5, 3.5);
    drawHighlighted(p, hostR.adjusted(13, 0, 13, 0), hf, m_host, c.muted, Qt::ElideMiddle);

    // ---- pin button: always visible when pinned, otherwise on hover ----
    const int buttonSize = Platform::mobile ? 42 : 32;
    m_pinRect = QRectF(img.left() + 10, img.top() + 10, buttonSize, buttonSize);
    m_menuRect = QRectF(m_pinRect.right() + 8, m_pinRect.top(), buttonSize, buttonSize);
    const qreal pinT = qBound<qreal>(0, m_pinT, 1);
    const qreal pinVis = Platform::mobile ? 1.0 : qMax(t, pinT);
    if (pinVis > 0.01) {
        const qreal pop = 1.0 + 0.6 * m_pinT * (1.0 - m_pinT);       // bounce while toggling
        QRectF pr(0, 0, buttonSize * pop, buttonSize * pop);
        pr.moveCenter(m_pinRect.center());
        p.setOpacity(a * pinVis);
        p.setPen(Qt::NoPen);
        const QColor idle(15, 15, 30, m_hoverBtn == 1 ? 200 : 150);
        p.setBrush(mix(idle, c.accent, pinT));
        p.drawEllipse(pr);
        if (m_hoverBtn == 1) {
            p.setBrush(Qt::NoBrush);
            p.setPen(QPen(QColor(255, 255, 255, 160), 1.4));
            p.drawEllipse(pr.adjusted(1, 1, -1, -1));
        }
        drawPin(p, pr.center(), 15 * pop, Qt::white, 1.0 - m_pinT);
        p.setOpacity(a);
    }

    // ---- hover actions on the image ----
    if (t > 0.01 || Platform::mobile) {
        p.setOpacity(a * (Platform::mobile ? 1.0 : t));
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(15, 15, 30, m_hoverBtn == 2 ? 200 : 150));
        p.drawEllipse(m_menuRect);
        p.setBrush(Qt::white);
        for (int i = -1; i <= 1; ++i)
            p.drawEllipse(m_menuRect.center() + QPointF(i * 6.0, 0), 1.9, 1.9);

        p.setOpacity(a * t);
        QFont of = font(); of.setPixelSize(12); of.setWeight(QFont::DemiBold);
        p.setFont(of);
        const QString txt = QString::fromUtf8("Open ↗");
        const qreal w = QFontMetricsF(of).horizontalAdvance(txt) + 26;
        const QRectF pill(img.right() - 10 - w + 8 * (1 - t), img.top() + 10, w, 32);   // slides in
        p.setBrush(QColor(255, 255, 255, 238));
        p.drawRoundedRect(pill, 16, 16);
        p.setPen(QColor("#1C1D29"));
        p.drawText(pill, Qt::AlignCenter, txt);
        p.setOpacity(a);
    }
}

void BookmarkCard::paintMedia(QPainter& p, const QRectF& img, qreal t) {
    if (!m_cover.isNull()) {
        const qreal z = 1.0 + 0.06 * t;                 // gentle zoom on hover
        QRectF tr(0, 0, img.width() * z, img.height() * z);
        tr.moveCenter(img.center());
        p.drawImage(tr, m_cover);

        QLinearGradient bottom(img.topLeft(), img.bottomLeft());
        bottom.setColorAt(0.55, QColor(0, 0, 0, 0));
        bottom.setColorAt(1.0, QColor(0, 0, 0, 80));
        p.fillRect(img, bottom);
        if (t > 0.01) {
            QLinearGradient top(img.topLeft(), img.bottomLeft());
            top.setColorAt(0.0, QColor(0, 0, 0, int(90 * t)));
            top.setColorAt(0.45, QColor(0, 0, 0, 0));
            p.fillRect(img, top);
        }
        return;
    }

    // no cover: brand-colored gradient + decorative circles + icon tile
    const QColor a = m_accent;
    QLinearGradient g(img.topLeft(), img.bottomRight());
    g.setColorAt(0, a.lighter(m_dark ? 105 : 130));
    g.setColorAt(1, a.darker(m_dark ? 165 : 125));
    p.fillRect(img, g);

    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 30));
    p.drawEllipse(QPointF(img.right() - 24, img.top() + 6), 78 + 6 * t, 78 + 6 * t);
    p.setBrush(QColor(255, 255, 255, 18));
    p.drawEllipse(QPointF(img.left() + 34, img.bottom() + 6), 96, 96);
    p.setBrush(QColor(255, 255, 255, 22));
    p.drawEllipse(QPointF(img.left() + 70, img.top() + 30), 10, 10);

    const qreal tile = 72 + 6 * t;
    QRectF tr(0, 0, tile, tile);
    tr.moveCenter(img.center() + QPointF(0, -4 * t));
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawRoundedRect(tr.translated(0, 4), 22, 22);
    p.setBrush(QColor(255, 255, 255, 245));
    p.drawRoundedRect(tr, 22, 22);
    if (!m_iconBig.isNull()) {
        QRectF ir(0, 0, 40, 40);
        ir.moveCenter(tr.center());
        p.drawImage(ir, m_iconBig);
    } else {
        QFont f = font(); f.setPixelSize(32); f.setBold(true);
        p.setFont(f);
        p.setPen(a.darker(115));
        p.drawText(tr, Qt::AlignCenter, m_name.isEmpty() ? "?" : m_name.left(1).toUpper());
    }
}

void BookmarkCard::paintShimmer(QPainter& p, const QRectF& img) {
    p.fillRect(img, m_dark ? QColor(10, 10, 20, 90) : QColor(255, 255, 255, 110));
    const qreal x = img.left() + m_phase * img.width();
    QLinearGradient g(x - 90, 0, x + 90, 0);
    g.setColorAt(0, QColor(255, 255, 255, 0));
    g.setColorAt(0.5, QColor(255, 255, 255, m_dark ? 45 : 150));
    g.setColorAt(1, QColor(255, 255, 255, 0));
    p.fillRect(img, g);

    QFont f = font(); f.setPixelSize(12); f.setWeight(QFont::DemiBold);
    p.setFont(f);
    const QString txt = QString::fromUtf8("Loading preview…");
    const qreal w = QFontMetricsF(f).horizontalAdvance(txt) + 24;
    QRectF pill(0, 0, w, 26);
    pill.moveCenter(QPointF(img.center().x(), img.bottom() - 26));
    p.setPen(Qt::NoPen);
    p.setBrush(m_dark ? QColor(20, 22, 35, 200) : QColor(255, 255, 255, 220));
    p.drawRoundedRect(pill, 13, 13);
    p.setPen(m_dark ? QColor("#D8DAE6") : QColor("#4A4D60"));
    p.drawText(pill, Qt::AlignCenter, txt);
}

void BookmarkCard::paintBadge(QPainter& p, const QPointF& center) {
    const Palette c = pal(m_dark);
    const qreal R = 23;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(0, 0, 0, m_dark ? 90 : 28));
    p.drawEllipse(center + QPointF(0, 2.5), R + 1, R + 1);
    p.setBrush(c.bg);
    p.drawEllipse(center, R, R);
    p.setBrush(m_dark ? QColor("#FFFFFF") : QColor("#F5F4FB"));
    p.drawEllipse(center, R - 4, R - 4);
    if (!m_iconSmall.isNull()) {
        QRectF ir(0, 0, 22, 22);
        ir.moveCenter(center);
        p.drawImage(ir, m_iconSmall);
    } else {
        QFont f = font(); f.setPixelSize(17); f.setBold(true);
        p.setFont(f);
        p.setPen(m_accent.darker(110));
        QRectF r(0, 0, 2 * R, 2 * R);
        r.moveCenter(center);
        p.drawText(r, Qt::AlignCenter, m_name.isEmpty() ? "?" : m_name.left(1).toUpper());
    }
}

void BookmarkCard::mouseMoveEvent(QMouseEvent* e) {
    const QPointF pos = e->position();
    if (m_pressed && (e->buttons() & Qt::LeftButton)
        && (pos - m_pressPos).manhattanLength() >= QApplication::startDragDistance()) {
        startDrag();
        return;
    }
    int b = 0;
    if (m_pinRect.adjusted(-3, -3, 3, 3).contains(pos)) b = 1;
    else if (m_menuRect.adjusted(-3, -3, 3, 3).contains(pos)) b = 2;
    if (b != m_hoverBtn) { m_hoverBtn = b; update(); }
    QWidget::mouseMoveEvent(e);
}

void BookmarkCard::mousePressEvent(QMouseEvent* e) {
    m_pressed = (e->button() == Qt::LeftButton) && !m_removing;
    m_pressPos = e->position();
    if (m_pressed) runAnim(m_pressAnim, m_press, 1.0, 110);
    QWidget::mousePressEvent(e);
}

void BookmarkCard::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton || !m_pressed || m_removing) { QWidget::mouseReleaseEvent(e); return; }
    m_pressed = false;
    runAnim(m_pressAnim, m_press, 0.0, 220);
    const QPointF pos = e->position();
    if (m_pinRect.adjusted(-4, -4, 4, 4).contains(pos)) { emit pinToggleRequested(m_id); return; }
    if (m_menuRect.adjusted(-4, -4, 4, 4).contains(pos)) {
        showMenu(mapToGlobal(m_menuRect.bottomLeft().toPoint() + QPoint(0, 6)));
        return;
    }
    if (rect().contains(pos.toPoint())) emit openRequested(m_id);
}

// drag the card onto a collection chip
void BookmarkCard::startDrag() {
    m_pressed = false;
    runAnim(m_pressAnim, m_press, 0.0, 160);
    auto* mime = new QMimeData;
    mime->setData("application/x-linkvault-bookmark", m_id.toUtf8());
    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    // small translucent ghost of the card
    const qreal dpr = devicePixelRatioF();
    const QPixmap full = grab(QRect(kMargin, kMargin, kInnerW, kInnerH));
    QPixmap ghost(QSize(int(kInnerW * 0.55 * dpr), int(kInnerH * 0.55 * dpr)));
    ghost.setDevicePixelRatio(dpr);
    ghost.fill(Qt::transparent);
    {
        QPainter gp(&ghost);
        gp.setRenderHint(QPainter::SmoothPixmapTransform);
        gp.setOpacity(0.85);
        gp.drawPixmap(QRectF(0, 0, kInnerW * 0.55, kInnerH * 0.55), full, QRectF(full.rect()));
    }
    drag->setPixmap(ghost);
    drag->setHotSpot(QPoint(int(kInnerW * 0.275), int(kInnerH * 0.2)));
    drag->exec(Qt::MoveAction);
}

void BookmarkCard::contextMenuEvent(QContextMenuEvent* e) { if (!m_removing) showMenu(e->globalPos()); }

void BookmarkCard::showMenu(const QPoint& globalPos) {
    QMenu m(this);
    auto* open = m.addAction("🌐 Open");
    auto* pin  = m.addAction(m_pinned ? "📍 Unpin" : "📌 Pin");
    auto* edit = m.addAction("✏️ Edit");
    auto* copy = m.addAction("📋 Copy link");
    auto* refr = m.addAction("🔄 Refresh preview");
    if (menuHook()) { m.addSeparator(); menuHook()(m, m_id); }
    m.addSeparator();
    auto* del = m.addAction("🗑 Delete");
    QAction* act = m.exec(globalPos);
    if (act == open) emit openRequested(m_id);
    else if (act == pin) emit pinToggleRequested(m_id);
    else if (act == edit) emit editRequested(m_id);
    else if (act == copy) emit copyRequested(m_id);
    else if (act == refr) emit refreshRequested(m_id);
    else if (act == del) emit deleteRequested(m_id);
}

// ============================== AddCard ==============================

AddCard::AddCard(bool dark, QWidget* parent) : QWidget(parent), m_dark(dark) {
    setFixedSize(BookmarkCard::kW, BookmarkCard::kH);
    setAttribute(Qt::WA_Hover);
    setCursor(Qt::PointingHandCursor);
    setToolTip("Add a bookmark (Ctrl+N)");
    m_anim = makeAnim(this, &m_hover, 280, Theme::easeOut());
    m_appearAnim = makeAnim(this, &m_appear, 560, Theme::easeOut());
}

void AddCard::playAppear(int delayMs) {
    m_appearAnim->stop();
    if (!Theme::animations()) { m_appear = 1; update(); return; }
    m_appear = 0;
    update();
    QTimer::singleShot(delayMs, this, [this] { runAnim(m_appearAnim, 0.0, 1.0, 560); });
}

void AddCard::enterEvent(QEnterEvent* e) { runAnim(m_anim, m_hover, 1.0, 280); QWidget::enterEvent(e); }
void AddCard::leaveEvent(QEvent* e)      { runAnim(m_anim, m_hover, 0.0, 340); QWidget::leaveEvent(e); }

void AddCard::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) emit clicked();
    QWidget::mouseReleaseEvent(e);
}

void AddCard::paintEvent(QPaintEvent*) {
    const Palette c = pal(m_dark);
    const qreal t = m_hover, a = m_appear;
    if (a <= 0.002) return;
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    p.setOpacity(a);
    const QPointF ctr = QRectF(rect()).center();
    const qreal s = 0.95 + 0.05 * a;
    p.translate(ctr.x(), ctr.y() + (1.0 - a) * 12);
    p.scale(s, s);
    p.translate(-ctr.x(), -ctr.y());

    const QRectF card(BookmarkCard::kMargin + 0.5, BookmarkCard::kMargin + 0.5 - 3 * t,
                      BookmarkCard::kInnerW - 1, BookmarkCard::kInnerH - 1);
    QColor fill = c.accent; fill.setAlpha(int(10 + 22 * t));
    QColor line = c.accent; line.setAlpha(int(110 + 120 * t));
    QPen pen(line, 1.6, Qt::CustomDashLine);
    pen.setDashPattern({5, 4});
    pen.setDashOffset(-t * 9);                         // dashes "march" on hover
    p.setPen(pen);
    p.setBrush(fill);
    p.drawRoundedRect(card, 18, 18);

    const qreal d = 58 * (1 + 0.08 * t);
    QRectF circle(0, 0, d, d);
    circle.moveCenter(card.center() + QPointF(0, -22));
    QLinearGradient g(circle.topLeft(), circle.bottomRight());
    g.setColorAt(0, c.accent.lighter(120));
    g.setColorAt(1, c.accent.darker(110));
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(circle);
    p.save();
    p.translate(circle.center());
    p.rotate(90 * t);                                  // plus sign spins a quarter turn
    p.setPen(QPen(Qt::white, 3.2, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(-11, 0), QPointF(11, 0));
    p.drawLine(QPointF(0, -11), QPointF(0, 11));
    p.restore();

    QFont f = font(); f.setPixelSize(15); f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setPen(c.accent);
    p.drawText(QRectF(card.left(), circle.bottom() + 14, card.width(), 24), Qt::AlignCenter, "Add a bookmark");
    QFont sf = font(); sf.setPixelSize(12);
    p.setFont(sf);
    p.setPen(c.muted);
    p.drawText(QRectF(card.left(), circle.bottom() + 40, card.width(), 20), Qt::AlignCenter, "Ctrl + N");
}
