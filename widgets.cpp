#include "widgets.h"
#include "theme.h"
#include "platform.h"
#include <QApplication>
#include <QAction>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QWheelEvent>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QFontMetricsF>
#include <QIcon>
#include <QLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QParallelAnimationGroup>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QResizeEvent>
#include <QSet>
#include <QVariantAnimation>
#include <QtMath>
#include <iterator>

namespace {
QVariantAnimation* makeAnim(QObject* owner, int ms, const QEasingCurve& curve) {
    auto* a = new QVariantAnimation(owner);
    a->setDuration(ms);
    a->setEasingCurve(curve);
    return a;
}
void runTo(QVariantAnimation* a, qreal from, qreal to, int ms) {
    a->stop();
    a->setDuration(Theme::animations() ? ms : 1);
    a->setStartValue(from);
    a->setEndValue(to);
    a->start();
}
} // namespace

// ============================== ToggleSwitch ==============================

ToggleSwitch::ToggleSwitch(QWidget* parent) : QAbstractButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFixedSize(sizeHint());
    m_anim = makeAnim(this, 320, Theme::easeSpring(1.2));
    m_hoverAnim = makeAnim(this, 200, Theme::easeOut());
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_pos = v.toReal(); update(); });
    connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_hover = v.toReal(); update(); });
    connect(this, &QAbstractButton::toggled, this, [this](bool on) {
        if (!isVisible()) { m_pos = on ? 1 : 0; update(); return; }
        runTo(m_anim, m_pos, on ? 1.0 : 0.0, 320);
    });
}

void ToggleSwitch::showEvent(QShowEvent* e) {
    if (m_anim->state() != QAbstractAnimation::Running) m_pos = isChecked() ? 1 : 0;
    QAbstractButton::showEvent(e);
}
void ToggleSwitch::enterEvent(QEnterEvent* e) { runTo(m_hoverAnim, m_hover, 1, 200); QAbstractButton::enterEvent(e); }
void ToggleSwitch::leaveEvent(QEvent* e)      { runTo(m_hoverAnim, m_hover, 0, 260); QAbstractButton::leaveEvent(e); }

void ToggleSwitch::paintEvent(QPaintEvent*) {
    const auto c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    const qreal rad = r.height() / 2;
    const qreal t = qBound<qreal>(0, m_pos, 1);

    QColor off = Theme::mix(c.border, c.muted, 0.25 * m_hover);
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::mix(off, c.accent, t));
    p.drawRoundedRect(r, rad, rad);

    const qreal d = r.height() - 6;
    const bool rtl = isRightToLeft();
    const qreal xOff = rtl ? r.right() - 3 - d : r.left() + 3;
    const qreal xOn  = rtl ? r.left() + 3 : r.right() - 3 - d;
    const qreal stretch = 4 * qSin(M_PI * qBound<qreal>(0, m_pos, 1));   // knob squishes while moving
    QRectF knob(xOff + (xOn - xOff) * m_pos - stretch / 2, r.top() + 3, d + stretch, d);
    p.setBrush(QColor(0, 0, 0, 40));
    p.drawRoundedRect(knob.translated(0, 1.2), d / 2, d / 2);
    p.setBrush(Qt::white);
    p.drawRoundedRect(knob, d / 2, d / 2);
}

// ============================== SegmentedControl ==============================

SegmentedControl::SegmentedControl(const QStringList& items, QWidget* parent) : QWidget(parent), m_items(items) {
    setCursor(Qt::PointingHandCursor);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_anim = makeAnim(this, 380, Theme::easeSpring(1.1));
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_pos = v.toReal(); update(); });
}

QSize SegmentedControl::sizeHint() const {
    QFont f = font(); f.setPixelSize(13); f.setWeight(QFont::DemiBold);
    const QFontMetricsF fm(f);
    qreal w = 0;
    for (const auto& s : m_items) w = qMax(w, fm.horizontalAdvance(s));
    const int itemPadding = Platform::mobile ? 16 : 34;
    return QSize(int((w + itemPadding) * qMax<qsizetype>(1, m_items.size())) + 8, Platform::mobile ? 44 : 40);
}

void SegmentedControl::setItemText(int i, const QString& t) {
    if (i < 0 || i >= m_items.size() || m_items[i] == t) return;
    m_items[i] = t;
    updateGeometry();
    update();
}

void SegmentedControl::setCurrent(int i, bool animate) {
    if (i < 0 || i >= m_items.size()) return;
    m_cur = i;
    if (animate && isVisible() && Theme::animations()) runTo(m_anim, m_pos, i, 340);
    else { m_anim->stop(); m_pos = i; update(); }
}

QRectF SegmentedControl::segRect(qreal pos) const {
    const qreal pad = 4;
    const qreal w = (width() - 2 * pad) / qMax<qsizetype>(1, m_items.size());
    const qreal x = isRightToLeft() ? width() - pad - (pos + 1) * w : pad + pos * w;
    return QRectF(x, pad, w, height() - 2 * pad);
}

int SegmentedControl::indexAt(const QPointF& pt) const {
    for (int i = 0; i < m_items.size(); ++i)
        if (segRect(i).contains(pt)) return i;
    return -1;
}

void SegmentedControl::paintEvent(QPaintEvent*) {
    const auto c = Theme::colors();
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    const QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    p.setPen(QPen(c.border, 1));
    p.setBrush(c.glass);
    p.drawRoundedRect(r, 13, 13);

    // hover ghost
    if (m_hoverIdx >= 0 && m_hoverIdx != m_cur) {
        QColor h = c.accent; h.setAlpha(22);
        p.setPen(Qt::NoPen);
        p.setBrush(h);
        p.drawRoundedRect(segRect(m_hoverIdx), 10, 10);
    }
    // sliding indicator
    const QRectF ind = segRect(m_pos);
    QLinearGradient g(ind.topLeft(), ind.bottomRight());
    g.setColorAt(0, c.accentHover);
    g.setColorAt(1, c.accent);
    p.setPen(Qt::NoPen);
    QColor sh = c.accent; sh.setAlpha(60);
    p.setBrush(sh);
    p.drawRoundedRect(ind.translated(0, 2), 10, 10);
    p.setBrush(g);
    p.drawRoundedRect(ind, 10, 10);

    QFont f = font(); f.setPixelSize(13); f.setWeight(QFont::DemiBold);
    p.setFont(f);
    for (int i = 0; i < m_items.size(); ++i) {
        const qreal near = 1.0 - qMin<qreal>(1.0, qAbs(m_pos - i));
        p.setPen(Theme::mix(i == m_hoverIdx ? c.accent : c.text, Qt::white, near));
        p.drawText(segRect(i), Qt::AlignCenter, m_items[i]);
    }
}

void SegmentedControl::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    const int i = indexAt(e->position());
    if (i < 0 || i == m_cur) return;
    setCurrent(i, true);
    emit currentChanged(i);
}
void SegmentedControl::mouseMoveEvent(QMouseEvent* e) {
    const int i = indexAt(e->position());
    if (i != m_hoverIdx) { m_hoverIdx = i; update(); }
}
void SegmentedControl::leaveEvent(QEvent* e) { m_hoverIdx = -1; update(); QWidget::leaveEvent(e); }

// ============================== SearchBox ==============================

static QIcon searchIcon(const QColor& c) {
    QPixmap pm(40, 40);
    pm.setDevicePixelRatio(2);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(c, 2.0, Qt::SolidLine, Qt::RoundCap));
    p.drawEllipse(QRectF(3.5, 3.5, 10, 10));
    p.drawLine(QPointF(12, 12), QPointF(16.5, 16.5));
    return QIcon(pm);
}

SearchBox::SearchBox(QWidget* parent) : QLineEdit(parent) {
    setObjectName("search");
    setClearButtonEnabled(true);
    m_icon = addAction(searchIcon(Theme::colors().muted), QLineEdit::LeadingPosition);
    m_anim = makeAnim(this, 360, Theme::easeOut());
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { setFixedWidth(qRound(v.toReal())); });
    setFixedWidth(m_normal);
}

void SearchBox::setDark(bool dark) { m_icon->setIcon(searchIcon(Theme::colors(dark).muted)); }
void SearchBox::setWidths(int normal, int focused) {
    m_normal = normal; m_focused = focused;
    setFixedWidth(hasFocus() ? focused : normal);
}
void SearchBox::setStretchable() {
    m_stretchable = true;
    setMinimumWidth(0);
    setMaximumWidth(QWIDGETSIZE_MAX);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
}
void SearchBox::animateWidth(int to) {
    if (m_stretchable) return;
    m_anim->stop();
    if (!Theme::animations()) { setFixedWidth(to); return; }
    m_anim->setStartValue(qreal(width()));
    m_anim->setEndValue(qreal(to));
    m_anim->start();
}
void SearchBox::focusInEvent(QFocusEvent* e)  { animateWidth(m_focused); QLineEdit::focusInEvent(e); }
void SearchBox::focusOutEvent(QFocusEvent* e) {
    if (text().isEmpty()) animateWidth(m_normal);
    QLineEdit::focusOutEvent(e);
}

// ============================== Toast ==============================

Toast::Toast(QWidget* parent) : QWidget(parent) {
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);
    hide();
    parent->installEventFilter(this);
    m_showAnim = makeAnim(this, 360, Theme::easeOut());
    connect(m_showAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        m_show = v.toReal(); reposition(); update();
    });
    connect(m_showAnim, &QVariantAnimation::finished, this, [this] { if (m_show <= 0.001) hide(); });
    m_lifeAnim = makeAnim(this, 4500, QEasingCurve::Linear);
    connect(m_lifeAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_life = v.toReal(); update(); });
    connect(m_lifeAnim, &QVariantAnimation::finished, this, [this] { emit timedOut(); dismiss(); });
}

void Toast::showMessage(const QString& text, const QString& action, int msec) {
    m_lifeAnim->stop();
    m_text = text;
    m_action = action;
    QFont f = font(); f.setPixelSize(14);
    QFont af = f; af.setWeight(QFont::Bold);
    const qreal tw = QFontMetricsF(f).horizontalAdvance(text);
    const qreal aw = action.isEmpty() ? 0 : QFontMetricsF(af).horizontalAdvance(action) + 28;
    resize(int(qMin<qreal>(tw + aw + 48, parentWidget()->width() - 40)), 52);
    show();
    raise();
    m_showAnim->setEasingCurve(Theme::easeOut());
    runTo(m_showAnim, m_show, 1.0, 360);
    m_life = 1;
    m_lifeAnim->setDuration(msec);
    m_lifeAnim->setStartValue(1.0);
    m_lifeAnim->setEndValue(0.0);
    m_lifeAnim->start();
}

void Toast::dismiss() {
    m_lifeAnim->stop();
    if (!isVisible()) return;
    m_showAnim->setEasingCurve(Theme::easeIn());
    runTo(m_showAnim, m_show, 0.0, 220);
}

void Toast::reposition() {
    if (!parentWidget()) return;
    const QSize ps = parentWidget()->size();
    move((ps.width() - width()) / 2, int(ps.height() - height() - 26 + (1.0 - m_show) * 26));
}

bool Toast::eventFilter(QObject* obj, QEvent* ev) {
    if (obj == parentWidget() && ev->type() == QEvent::Resize) reposition();
    return QWidget::eventFilter(obj, ev);
}

void Toast::paintEvent(QPaintEvent*) {
    const auto c = Theme::colors();
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    p.setOpacity(m_show);
    const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
    QPainterPath shape;
    shape.addRoundedRect(r, 16, 16);
    p.fillPath(shape, Theme::isDark() ? QColor(44, 47, 66, 245) : QColor(30, 31, 43, 240));

    // countdown bar
    p.save();
    p.setClipPath(shape);
    QColor bar = c.accent; bar.setAlpha(200);
    const qreal bw = r.width() * m_life;
    p.fillRect(QRectF(isRightToLeft() ? r.right() - bw : r.left(), r.bottom() - 3, bw, 3), bar);
    p.restore();

    QFont f = font(); f.setPixelSize(14);
    QFont af = f; af.setWeight(QFont::Bold);
    m_actionRect = QRectF();
    qreal aw = 0;
    if (!m_action.isEmpty()) {
        aw = QFontMetricsF(af).horizontalAdvance(m_action) + 22;
        m_actionRect = QRectF(r.left() + 10, r.top() + 9, aw, r.height() - 18);
        if (m_hoverAction) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 255, 255, 30));
            p.drawRoundedRect(m_actionRect, 10, 10);
        }
        p.setFont(af);
        p.setPen(QColor("#B9AFFF"));
        p.drawText(m_actionRect, Qt::AlignCenter, m_action);
    }
    p.setFont(f);
    p.setPen(Qt::white);
    const QRectF tr(r.left() + (aw > 0 ? aw + 18 : 18), r.top(), r.width() - (aw > 0 ? aw + 36 : 36), r.height());
    p.drawText(tr, Qt::AlignLeft | Qt::AlignVCenter,
               QFontMetricsF(f).elidedText(m_text, Qt::ElideRight, tr.width()));
}

void Toast::mouseMoveEvent(QMouseEvent* e) {
    const bool h = m_actionRect.contains(e->position());
    if (h != m_hoverAction) { m_hoverAction = h; setCursor(h ? Qt::PointingHandCursor : Qt::ArrowCursor); update(); }
}
void Toast::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::LeftButton && m_actionRect.contains(e->position())) {
        m_lifeAnim->stop();
        emit actionTriggered();
        dismiss();
    }
}
void Toast::enterEvent(QEnterEvent* e) {           // hovering pauses the countdown
    if (m_lifeAnim->state() == QAbstractAnimation::Running) m_lifeAnim->pause();
    QWidget::enterEvent(e);
}
void Toast::leaveEvent(QEvent* e) {
    if (m_lifeAnim->state() == QAbstractAnimation::Paused) m_lifeAnim->resume();
    m_hoverAction = false;
    update();
    QWidget::leaveEvent(e);
}

// ============================== SlideStack ==============================
// The transition no longer moves the real pages every frame (that re-laid-out and
// repainted every card each frame = stutter). Both pages are grabbed once into
// pixmaps and an overlay slides + fades those, which is GPU/blit cheap.

namespace {
class SlideOverlay : public QWidget {
public:
    SlideOverlay(QWidget* parent, QPixmap from, QPixmap to, int sign)
        : QWidget(parent), m_from(std::move(from)), m_to(std::move(to)), m_sign(sign) {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
    }
    void setProgress(qreal t) { m_t = t; update(); }
protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const qreal w = width(), t = m_t;
        // outgoing page: drifts a little and fades out quickly
        const qreal travel = qMin(w * 0.10, 52.0);
        p.setOpacity(1.0 - t);
        p.drawPixmap(QPointF(-m_sign * travel * 0.6 * t, 0), m_from);
        p.setOpacity(qMin<qreal>(1.0, t * 1.4));
        p.drawPixmap(QPointF(m_sign * travel * (1.0 - t), 0), m_to);
    }
private:
    QPixmap m_from, m_to;
    int m_sign;
    qreal m_t = 0;
};
} // namespace

SlideStack::SlideStack(QWidget* parent) : QWidget(parent) {}

int SlideStack::addPage(QWidget* page) {
    page->setParent(this);
    page->setGeometry(rect());
    page->setVisible(m_pages.isEmpty());
    m_pages << page;
    return int(m_pages.size()) - 1;
}

void SlideStack::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    finishTransition();
}

void SlideStack::finishTransition() {
    if (m_anim) {
        QVariantAnimation* a = m_anim.data();
        m_anim = nullptr;
        a->disconnect(this);
        a->stop();
    }
    if (m_overlay) {
        m_overlay->hide();
        m_overlay->deleteLater();
    }
    m_overlay = nullptr;
    for (int i = 0; i < m_pages.size(); ++i) {
        if (i == m_cur) { m_pages[i]->setGeometry(rect()); m_pages[i]->show(); }
        else { m_pages[i]->hide(); m_pages[i]->move(0, 0); }
    }
}

void SlideStack::slideTo(int index) {
    if (index == m_cur || index < 0 || index >= m_pages.size()) return;
    finishTransition();
    QWidget* from = m_pages[m_cur];
    QWidget* to = m_pages[index];
    const int dir = index > m_cur ? 1 : -1;
    m_cur = index;
    emit currentChanged(index);

    if (!Theme::animations() || !isVisible()) { finishTransition(); return; }

    // snapshot both pages (no paint happens in between, so nothing flickers)
    to->setGeometry(rect());
    to->show();
    if (to->layout()) to->layout()->activate();
    const QPixmap fromShot = from->grab();
    const QPixmap toShot = to->grab();
    from->hide();
    to->hide();

    const int sign = (isRightToLeft() ? -1 : 1) * dir;   // RTL: next page comes in from the left
    auto* ov = new SlideOverlay(this, fromShot, toShot, sign);
    ov->setGeometry(rect());
    ov->show();
    ov->raise();
    m_overlay = ov;

    auto* an = new QVariantAnimation(this);
    an->setDuration(260);
    an->setEasingCurve(Theme::easeOut());
    an->setStartValue(0.0);
    an->setEndValue(1.0);
    connect(an, &QVariantAnimation::valueChanged, ov, [ov](const QVariant& v) { ov->setProgress(v.toReal()); });
    connect(an, &QAbstractAnimation::finished, this, [this] { finishTransition(); });
    m_anim = an;
    an->start(QAbstractAnimation::DeleteWhenStopped);
}

// ============================== ZoomOverlay ==============================

ZoomOverlay::ZoomOverlay(QWidget* parent, const QPixmap& shot, const QRectF& from, int ms)
    : QWidget(parent), m_shot(shot), m_from(from) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_NoSystemBackground);
    setGeometry(parent->rect());
    m_anim = makeAnim(this, ms, QEasingCurve::Linear);
    m_anim->setStartValue(0.0);
    m_anim->setEndValue(1.0);
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_t = v.toReal(); update(); });
    connect(m_anim, &QAbstractAnimation::finished, this, &QObject::deleteLater);
    show();
    raise();
    m_anim->start();
}

void ZoomOverlay::paintEvent(QPaintEvent*) {
    const qreal e = Theme::easeOut().valueForProgress(m_t);
    const qreal fade = Theme::easeInOut().valueForProgress(m_t);
    const QRectF to = QRectF(rect());
    const QRectF r(m_from.left() + (to.left() - m_from.left()) * e, m_from.top() + (to.top() - m_from.top()) * e,
                   m_from.width() + (to.width() - m_from.width()) * e,
                   m_from.height() + (to.height() - m_from.height()) * e);
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setOpacity(1.0 - fade);
    p.drawPixmap(r, m_shot, QRectF(m_shot.rect()));
}

// ============================== CardGrid ==============================

CardGrid::CardGrid(QSize tile, QWidget* parent) : QWidget(parent), m_tile(tile) {
    setObjectName("grid");
    setMinimumWidth(tile.width());
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
}

void CardGrid::setItems(const QList<Item>& items, bool animate) {
    QSet<QWidget*> keep;
    for (const auto& it : items) if (it.w) keep.insert(it.w.data());
    for (const auto& old : m_items) {
        if (old.w && !keep.contains(old.w.data())) { stopAnim(old.w); old.w->hide(); }
    }
    m_items = items;
    layoutItems(animate);
}

void CardGrid::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    if (e->oldSize().width() != e->size().width()) layoutItems(false);
}

void CardGrid::stopAnim(QWidget* w) {
    if (auto a = m_anims.take(w)) a->stop();   // DeleteWhenStopped cleans it up
}

void CardGrid::moveAnimated(QWidget* w, const QPoint& target) {
    QPointer<QPropertyAnimation> a = m_anims.value(w);
    if (a && a->state() == QAbstractAnimation::Running && a->endValue().toPoint() == target) return;
    if (a) a->stop();
    auto* an = new QPropertyAnimation(w, "pos", w);
    an->setDuration(300);
    an->setEasingCurve(Theme::easeOut());
    an->setStartValue(w->pos());
    an->setEndValue(target);
    m_anims[w] = an;
    an->start(QAbstractAnimation::DeleteWhenStopped);
}

void CardGrid::layoutItems(bool animate) {
    for (auto it = m_anims.begin(); it != m_anims.end();) it = it.value() ? std::next(it) : m_anims.erase(it);

    const int W = width();
    const int tw = m_tile.width(), th = m_tile.height();
    const int cols = qMax(1, W / tw);
    const int gridW = cols * tw;
    const int left = qMax(0, (W - gridW) / 2);
    const bool rtl = isRightToLeft();
    const bool anim = animate && Theme::animations();

    int y = 0, col = 0, order = 0;
    for (const auto& it : m_items) {
        QWidget* w = it.w;
        if (!w) continue;
        QPoint target;
        if (it.fullRow) {
            if (col > 0) { y += th; col = 0; }
            const int h = qMax(w->sizeHint().height(), w->minimumHeight());
            w->resize(gridW, h);
            target = QPoint(left, y);
            y += h;
        } else {
            const int x = rtl ? left + gridW - (col + 1) * tw : left + col * tw;
            target = QPoint(x, y);
            if (++col == cols) { col = 0; y += th; }
        }

        if (w->isVisibleTo(this)) {
            if (anim) { if (w->pos() != target) moveAnimated(w, target); }
            else { stopAnim(w); w->move(target); }
        } else {
            stopAnim(w);
            w->move(target);
            w->show();
            if (w->metaObject()->indexOfMethod("playAppear(int)") >= 0)
                QMetaObject::invokeMethod(w, "playAppear", Q_ARG(int, animate ? qMin(order, 14) * 28 : 0));
        }
        ++order;
    }
    if (col > 0) y += th;
    setMinimumHeight(y + 16);
}

// ============================== CollectionBar ==============================

namespace {
constexpr qreal kChipH = 34, kChipGap = 8, kChipPadX = 14;
QFont chipFont(const QFont& base) { QFont f = base; f.setPixelSize(13); f.setWeight(QFont::DemiBold); return f; }
}

CollectionBar::CollectionBar(QWidget* parent) : QWidget(parent) {
    setAcceptDrops(true);
    setMouseTracking(true);
    setFixedHeight(42);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_indAnim = makeAnim(this, 420, Theme::easeOut());
    connect(m_indAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_indT = v.toReal(); update(); });
    m_scrollAnim = makeAnim(this, 320, Theme::easeOut());
    connect(m_scrollAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_scroll = v.toReal(); update(); });
}

QString CollectionBar::current() const { return m_chips.value(m_cur).id; }

void CollectionBar::setChips(const QList<Chip>& chips) {
    const QString cur = current();
    m_chips = chips;
    m_cur = 0;
    for (int i = 0; i < m_chips.size(); ++i) if (m_chips[i].id == cur) m_cur = i;
    relayout();
    m_indFrom = m_indTo = m_rects.value(m_cur);
    m_indT = 1;
    update();
}

void CollectionBar::relayout() {
    m_rects.clear();
    const QFontMetricsF fm(chipFont(font()));
    qreal x = 2;
    for (const Chip& c : m_chips) {
        const QString label = c.count > 0 ? QString("%1  %2").arg(c.text).arg(c.count) : c.text;
        const qreal w = fm.horizontalAdvance(label) + 2 * kChipPadX + (c.id.isEmpty() ? 0 : 14);
        m_rects << QRectF(x, (height() - kChipH) / 2, w, kChipH);
        x += w + kChipGap;
    }
    const qreal aw = fm.horizontalAdvance(QStringLiteral("+ New collection")) + 2 * kChipPadX;
    m_addRect = QRectF(x, (height() - kChipH) / 2, aw, kChipH);
    m_contentW = x + aw + 2;
    m_scroll = qBound<qreal>(0, m_scroll, maxScroll());
}

void CollectionBar::resizeEvent(QResizeEvent* e) {
    QWidget::resizeEvent(e);
    relayout();
    m_indFrom = m_indTo = m_rects.value(m_cur);
}

qreal CollectionBar::maxScroll() const { return qMax<qreal>(0, m_contentW - width()); }

QRectF CollectionBar::visual(const QRectF& r) const {
    if (isRightToLeft()) return QRectF(width() - r.right() + m_scroll, r.top(), r.width(), r.height());
    return r.translated(-m_scroll, 0);
}

int CollectionBar::hitTest(const QPointF& pos) const {
    for (int i = 0; i < m_rects.size(); ++i) if (visual(m_rects[i]).contains(pos)) return i;
    if (visual(m_addRect).contains(pos)) return -2;
    return -1;
}

void CollectionBar::scrollTo(qreal x) {
    x = qBound<qreal>(0, x, maxScroll());
    m_scrollAnim->stop();
    if (!Theme::animations()) { m_scroll = x; update(); return; }
    m_scrollAnim->setStartValue(m_scroll);
    m_scrollAnim->setEndValue(x);
    m_scrollAnim->start();
}

void CollectionBar::ensureVisible(int i) {
    if (i < 0 || i >= m_rects.size()) return;
    const QRectF r = m_rects[i];
    if (r.left() - 20 < m_scroll) scrollTo(r.left() - 20);
    else if (r.right() + 20 > m_scroll + width()) scrollTo(r.right() + 20 - width());
}

void CollectionBar::animateIndicator(int to, bool animate) {
    const qreal t = Theme::easeOut().valueForProgress(m_indT);
    const QRectF now(m_indFrom.left() + (m_indTo.left() - m_indFrom.left()) * t, m_indTo.top(),
                     m_indFrom.width() + (m_indTo.width() - m_indFrom.width()) * t, m_indTo.height());
    m_indFrom = now;
    m_indTo = m_rects.value(to);
    m_indAnim->stop();
    if (!animate || !Theme::animations() || !isVisible()) { m_indFrom = m_indTo; m_indT = 1; update(); return; }
    m_indAnim->setEasingCurve(QEasingCurve::Linear);     // easing applied in paint (rect interpolation)
    m_indAnim->setStartValue(0.0);
    m_indAnim->setEndValue(1.0);
    m_indAnim->start();
}

void CollectionBar::setCurrent(const QString& id, bool animate) {
    for (int i = 0; i < m_chips.size(); ++i) {
        if (m_chips[i].id != id) continue;
        if (i == m_cur) return;
        m_cur = i;
        animateIndicator(i, animate);
        ensureVisible(i);
        return;
    }
}

void CollectionBar::paintEvent(QPaintEvent*) {
    const auto c = Theme::colors();
    QPainter p(this);
    p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
    const QFont f = chipFont(font());
    p.setFont(f);

    // chips
    for (int i = 0; i < m_chips.size(); ++i) {
        const Chip& ch = m_chips[i];
        QRectF r = visual(m_rects[i]);
        if (r.right() < -2 || r.left() > width() + 2) continue;
        if (i == m_drop) {                                   // drop target "breathes" bigger
            r = r.adjusted(-3, -2, 3, 2);
            QColor fill = ch.color.isValid() ? ch.color : c.accent; fill.setAlpha(60);
            p.setPen(QPen(ch.color.isValid() ? ch.color : c.accent, 1.6));
            p.setBrush(fill);
        } else {
            p.setPen(QPen(i == m_hover ? Theme::mix(c.border, c.accent, 0.6) : c.border, 1));
            p.setBrush(c.glass);
        }
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), kChipH / 2, kChipH / 2);
    }

    // sliding selection pill
    if (m_cur >= 0 && m_cur < m_rects.size()) {
        const qreal t = Theme::easeOut().valueForProgress(m_indT);
        const QRectF ind(m_indFrom.left() + (m_indTo.left() - m_indFrom.left()) * t, m_indTo.top(),
                         m_indFrom.width() + (m_indTo.width() - m_indFrom.width()) * t, m_indTo.height());
        const QRectF vr = visual(ind);
        const QColor base = m_chips[m_cur].color.isValid() ? m_chips[m_cur].color : c.accent;
        QLinearGradient g(vr.topLeft(), vr.bottomRight());
        g.setColorAt(0, base.lighter(115));
        g.setColorAt(1, base);
        QColor sh = base; sh.setAlpha(60);
        p.setPen(Qt::NoPen);
        p.setBrush(sh);
        p.drawRoundedRect(vr.translated(0, 2), kChipH / 2, kChipH / 2);
        p.setBrush(g);
        p.drawRoundedRect(vr, kChipH / 2, kChipH / 2);
    }

    // labels
    const QRectF curVis = visual(m_rects.value(m_cur));
    for (int i = 0; i < m_chips.size(); ++i) {
        const Chip& ch = m_chips[i];
        const QRectF r = visual(m_rects[i]);
        if (r.right() < -2 || r.left() > width() + 2) continue;
        // white text while the pill is under this chip
        const qreal overlap = qMax<qreal>(0, qMin(r.right(), curVis.right()) - qMax(r.left(), curVis.left())) / r.width();
        const bool selected = i == m_cur && m_indT >= 0.999;
        const qreal on = selected ? 1.0 : (i == m_cur ? qBound<qreal>(0, m_indT, 1) : 0.0) * overlap;
        QRectF tr = r.adjusted(kChipPadX, 0, -kChipPadX, 0);
        if (!ch.id.isEmpty()) {                                    // color dot
            const qreal dx = isRightToLeft() ? tr.right() - 4 : tr.left() + 4;
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::mix(ch.color.isValid() ? ch.color : c.accent, Qt::white, on));
            p.drawEllipse(QPointF(dx, r.center().y()), 4, 4);
            tr = isRightToLeft() ? tr.adjusted(0, 0, -14, 0) : tr.adjusted(14, 0, 0, 0);
        }
        const QString label = ch.count > 0 ? QString("%1  %2").arg(ch.text).arg(ch.count) : ch.text;
        p.setPen(Theme::mix(i == m_hover ? c.accent : c.text, Qt::white, on));
        p.drawText(tr, Qt::AlignCenter, label);
    }

    // "+ new" chip (dashed)
    const QRectF ar = visual(m_addRect);
    QPen dash(m_hover == -2 ? c.accent : c.muted, 1.2, Qt::CustomDashLine);
    dash.setDashPattern({4, 3});
    p.setPen(dash);
    p.setBrush(m_hover == -2 ? QColor(c.accent.red(), c.accent.green(), c.accent.blue(), 24) : QColor(Qt::transparent));
    p.drawRoundedRect(ar.adjusted(0.5, 0.5, -0.5, -0.5), kChipH / 2, kChipH / 2);
    p.setPen(m_hover == -2 ? c.accent : c.muted);
    p.drawText(ar, Qt::AlignCenter, QStringLiteral("+ New collection"));
}

void CollectionBar::mousePressEvent(QMouseEvent* e) {
    if (e->button() != Qt::LeftButton) return;
    if (Platform::mobile) {
        m_scrollAnim->stop();
        m_pressX = e->position().x();
        m_pressScroll = m_scroll;
        m_touchDrag = false;
        m_touchPress.start();
        return;
    }
    const int i = hitTest(e->position());
    if (i == -2) { emit addRequested(); return; }
    if (i < 0 || i == m_cur) return;
    m_cur = i;
    animateIndicator(i, true);
    ensureVisible(i);
    emit currentChanged(m_chips[i].id);
}

void CollectionBar::mouseReleaseEvent(QMouseEvent* e) {
    if (!Platform::mobile || e->button() != Qt::LeftButton || !m_touchPress.isValid()) return;
    const qint64 held = m_touchPress.elapsed();
    m_touchPress.invalidate();
    if (m_touchDrag || !rect().contains(e->position().toPoint())) return;
    const int i = hitTest(e->position());
    if (i == -2) emit addRequested();
    else if (i > 0 && held >= 550) emit menuRequested(m_chips[i].id, e->globalPosition().toPoint());
    else if (i >= 0 && i != m_cur) {
        const QString id = m_chips[i].id;
        setCurrent(id);
        emit currentChanged(id);
    }
}

void CollectionBar::mouseMoveEvent(QMouseEvent* e) {
    if (Platform::mobile && m_touchPress.isValid() && e->buttons().testFlag(Qt::LeftButton)) {
        const qreal dx = e->position().x() - m_pressX;
        m_touchDrag = m_touchDrag || qAbs(dx) >= QApplication::startDragDistance();
        if (m_touchDrag) {
            m_scroll = qBound<qreal>(0, m_pressScroll - dx, maxScroll());
            update();
        }
        return;
    }
    const int i = hitTest(e->position());
    if (i != m_hover) {
        m_hover = i;
        setCursor(i == -1 ? Qt::ArrowCursor : Qt::PointingHandCursor);
        update();
    }
}

void CollectionBar::leaveEvent(QEvent* e) { m_hover = -1; update(); QWidget::leaveEvent(e); }

void CollectionBar::wheelEvent(QWheelEvent* e) {
    if (maxScroll() <= 0) { e->ignore(); return; }
    const QPoint d = e->angleDelta();
    const int delta = qAbs(d.x()) > qAbs(d.y()) ? d.x() : d.y();
    const qreal target = (m_scrollAnim->state() == QAbstractAnimation::Running ? m_scrollAnim->endValue().toReal() : m_scroll)
                         - delta / 120.0 * 90.0;
    scrollTo(target);
    e->accept();
}

void CollectionBar::contextMenuEvent(QContextMenuEvent* e) {
    const int i = hitTest(e->pos());
    if (i > 0) emit menuRequested(m_chips[i].id, e->globalPos());   // not for "all" / "+"
}

void CollectionBar::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData()->hasFormat(kMime)) e->acceptProposedAction();
}

void CollectionBar::dragMoveEvent(QDragMoveEvent* e) {
    if (!e->mimeData()->hasFormat(kMime)) return;
    const int i = hitTest(e->position());
    const bool canDrop = i >= 0 && m_chips[i].acceptsDrop;
    const int target = canDrop ? i : -1;
    if (target != m_drop) { m_drop = target; update(); }
    // auto-scroll when dragging near the edges
    const qreal x = e->position().x();
    const qreal dir = isRightToLeft() ? -1 : 1;
    if (x < 30) scrollTo(m_scroll - 60 * dir);
    else if (x > width() - 30) scrollTo(m_scroll + 60 * dir);
    if (canDrop) e->acceptProposedAction(); else e->ignore();
}

void CollectionBar::dragLeaveEvent(QDragLeaveEvent* e) { m_drop = -1; update(); QWidget::dragLeaveEvent(e); }

void CollectionBar::dropEvent(QDropEvent* e) {
    const int i = hitTest(e->position());
    m_drop = -1;
    update();
    if (i < 0 || !m_chips[i].acceptsDrop || !e->mimeData()->hasFormat(kMime)) return;
    const QString id = QString::fromUtf8(e->mimeData()->data(kMime));
    e->acceptProposedAction();
    emit dropped(id, m_chips[i].id);
}
