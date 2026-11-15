#include "titlebar.h"
#include <QEnterEvent>
#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QVariantAnimation>
#include <QWindow>
#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

// Going through WM_SYSCOMMAND (instead of showMinimized()/close()) lets Windows
// run its own minimize / maximize / restore / close animations.
static void systemCommand(QWidget* w, unsigned cmd) {
#ifdef Q_OS_WIN
    PostMessageW(reinterpret_cast<HWND>(w->winId()), WM_SYSCOMMAND, WPARAM(cmd), 0);
#else
    Q_UNUSED(w); Q_UNUSED(cmd);
#endif
}

WindowButton::WindowButton(Kind kind, QWidget* parent) : QAbstractButton(parent), m_kind(kind) {
    setFixedSize(sizeHint());
    setCursor(Qt::ArrowCursor);
    setFocusPolicy(Qt::NoFocus);
    setToolTip(kind == Minimize ? "کوچک کردن" : kind == Maximize ? "بزرگ کردن" : "بستن");
    m_anim = new QVariantAnimation(this);
    m_anim->setDuration(180);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_hover = v.toReal();
        update();
    });
}

void WindowButton::enterEvent(QEnterEvent* e) {
    m_anim->stop(); m_anim->setStartValue(m_hover); m_anim->setEndValue(1.0); m_anim->start();
    QAbstractButton::enterEvent(e);
}
void WindowButton::leaveEvent(QEvent* e) {
    if (m_ncHover) { QAbstractButton::leaveEvent(e); return; }   // Windows owns the hover now
    m_anim->stop(); m_anim->setStartValue(m_hover); m_anim->setEndValue(0.0); m_anim->start();
    QAbstractButton::leaveEvent(e);
}

void WindowButton::setNcHover(bool h) {
    if (h == m_ncHover) return;
    m_ncHover = h;
    if (!h) m_ncDown = false;
    m_anim->stop(); m_anim->setStartValue(m_hover); m_anim->setEndValue(h ? 1.0 : 0.0); m_anim->start();
}

void WindowButton::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const qreal t = m_hover;
    const QRectF r = QRectF(rect()).adjusted(2, 1, -2, -1);
    const bool down = isDown() || m_ncDown;

    // background pill
    QColor bg;
    if (m_kind == Close) bg = QColor(229, 72, 77, int(255 * t * (down ? 0.8 : 1.0)));
    else bg = m_dark ? QColor(255, 255, 255, int((down ? 34 : 22) * t))
                     : QColor(30, 20, 80, int((down ? 22 : 13) * t));
    p.setPen(Qt::NoPen);
    p.setBrush(bg);
    p.drawRoundedRect(r, 9, 9);

    // glyph
    QColor fg = m_dark ? QColor("#C9CCDA") : QColor("#4B4F63");
    if (m_kind == Close && t > 0) {
        const QColor w(Qt::white);
        fg = QColor::fromRgbF(fg.redF() + (w.redF() - fg.redF()) * t, fg.greenF() + (w.greenF() - fg.greenF()) * t,
                              fg.blueF() + (w.blueF() - fg.blueF()) * t);
    }
    QPen pen(fg, 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    const QPointF c = r.center();
    const qreal s = 5.0;
    switch (m_kind) {
    case Minimize:
        p.drawLine(QPointF(c.x() - s, c.y()), QPointF(c.x() + s, c.y()));
        break;
    case Maximize:
        if (!m_restore) {
            p.drawRoundedRect(QRectF(c.x() - s, c.y() - s, 2 * s, 2 * s), 2.2, 2.2);
        } else {
            const qreal k = 4.2;
            p.drawRoundedRect(QRectF(c.x() - s, c.y() - s + 2, 2 * k, 2 * k), 1.8, 1.8);
            QPainterPath back;
            back.moveTo(c.x() - s + 2.2, c.y() - s + 0.2);
            back.lineTo(c.x() + s - 1.8, c.y() - s + 0.2);
            back.quadTo(c.x() + s, c.y() - s + 0.2, c.x() + s, c.y() - s + 2.2);
            back.lineTo(c.x() + s, c.y() + s - 2.4);
            p.drawPath(back);
        }
        break;
    case Close:
        p.drawLine(QPointF(c.x() - s + 0.5, c.y() - s + 0.5), QPointF(c.x() + s - 0.5, c.y() + s - 0.5));
        p.drawLine(QPointF(c.x() + s - 0.5, c.y() - s + 0.5), QPointF(c.x() - s + 0.5, c.y() + s - 0.5));
        break;
    }
}

TitleBar::TitleBar(QWidget* window) : QWidget(window), m_window(window) {
    setFixedHeight(38);
    setLayoutDirection(Qt::LeftToRight);        // window controls always on the right
    setAttribute(Qt::WA_StyledBackground, false);

    auto* lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 4, 8, 0);
    lay->setSpacing(2);
    lay->addStretch();

    m_min = new WindowButton(WindowButton::Minimize, this);
    m_max = new WindowButton(WindowButton::Maximize, this);
    m_close = new WindowButton(WindowButton::Close, this);
    lay->addWidget(m_min);
    lay->addWidget(m_max);
    lay->addWidget(m_close);

#ifdef Q_OS_WIN
    connect(m_min, &QAbstractButton::clicked, this, [this] { systemCommand(m_window, SC_MINIMIZE); });
    connect(m_close, &QAbstractButton::clicked, this, [this] { systemCommand(m_window, SC_CLOSE); });
#else
    connect(m_min, &QAbstractButton::clicked, m_window, &QWidget::showMinimized);
    connect(m_close, &QAbstractButton::clicked, m_window, &QWidget::close);
#endif
    connect(m_max, &QAbstractButton::clicked, this, &TitleBar::toggleMax);
}

void TitleBar::setDark(bool d) { m_min->setDark(d); m_max->setDark(d); m_close->setDark(d); }

void TitleBar::setMaximized(bool m) {
    m_max->setRestore(m);
    m_max->setToolTip(m ? "برگرداندن اندازه" : "بزرگ کردن");
}

QRect TitleBar::maxButtonRect() const {
    return QRect(m_max->mapTo(m_window, QPoint(0, 0)), m_max->size());
}

void TitleBar::toggleMax() {
    if (m_window->isFullScreen()) {                          // leave full screen with its zoom animation
        QMetaObject::invokeMethod(m_window, "toggleFullScreen");
        return;
    }
#ifdef Q_OS_WIN
    systemCommand(m_window, m_window->isMaximized() ? SC_RESTORE : SC_MAXIMIZE);
#else
    if (m_window->isMaximized()) m_window->showNormal(); else m_window->showMaximized();
#endif
}

void TitleBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) {
        if (QWindow* w = m_window->windowHandle()) w->startSystemMove();   // native drag + Aero Snap
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton) toggleMax();
    QWidget::mouseDoubleClickEvent(e);
}
