#include "popups.h"
#include "dialogs.h"
#include "theme.h"
#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QCursor>
#include <QFontMetricsF>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QScreen>
#include <QTextLayout>
#include <QTimer>
#include <QVariantAnimation>
#include <QVBoxLayout>
#include <QtMath>
#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace {
QVariantAnimation* anim(QObject* owner, int ms, const QEasingCurve& curve) {
    auto* a = new QVariantAnimation(owner);
    a->setDuration(ms);
    a->setEasingCurve(curve);
    return a;
}
QString normText(QString s) {          // same rules as the main search
    s = s.toLower();
    s.replace(QChar(0x064A), QChar(0x06CC));
    s.replace(QChar(0x0643), QChar(0x06A9));
    s.remove(QChar(0x200C));
    return s;
}
QScreen* screenUnderCursor() {
    QScreen* s = QGuiApplication::screenAt(QCursor::pos());
    return s ? s : QGuiApplication::primaryScreen();
}
} // namespace

// ============================== FloatingPanel ==============================

FloatingPanel::FloatingPanel(QWidget* parent) : QWidget(parent) {
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(kShadow + 18, kShadow + 16, kShadow + 18, kShadow + 14);
    outer->setSpacing(10);
    m_body = outer;

    m_anim = anim(this, 260, Theme::easeOut());
    connect(m_anim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) {
        const qreal t = v.toReal();
        setWindowOpacity(qBound<qreal>(0, t, 1));
        move(m_base + QPoint(0, qRound(m_dy * (1.0 - t))));
    });
    connect(m_anim, &QAbstractAnimation::finished, this, [this] {
        if (m_closing) { hide(); m_closing = false; emit dismissed(); }
    });
}

void FloatingPanel::popup(const QPoint& topLeft, int slideDy) {
    m_closing = false;
    m_base = topLeft;
    m_dy = slideDy;
    m_anim->stop();
    if (!Theme::animations()) {
        setWindowOpacity(1);
        move(topLeft);
        show();
    } else {
        setWindowOpacity(0);
        move(topLeft + QPoint(0, slideDy));
        show();
        m_anim->setEasingCurve(Theme::easeOut());
        m_anim->setDuration(210);
        m_anim->setStartValue(0.0);
        m_anim->setEndValue(1.0);
        m_anim->start();
    }
    raise();
    activateWindow();
#ifdef Q_OS_WIN
    // a global hotkey grants foreground rights; make sure we actually take the keyboard
    SetForegroundWindow(reinterpret_cast<HWND>(winId()));
#endif
}

void FloatingPanel::dismiss() {
    if (!isVisible() || m_closing) return;
    m_closing = true;
    m_anim->stop();
    if (!Theme::animations()) { hide(); m_closing = false; emit dismissed(); return; }
    m_anim->setEasingCurve(Theme::easeIn());
    m_anim->setDuration(145);
    m_anim->setStartValue(windowOpacity());
    m_anim->setEndValue(0.0);
    m_dy = m_dy / 2;
    m_anim->start();
}

bool FloatingPanel::event(QEvent* e) {
    if (e->type() == QEvent::WindowDeactivate && !m_keepOnDeactivate) dismiss();
    return QWidget::event(e);
}

void FloatingPanel::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Escape) { dismiss(); return; }
    QWidget::keyPressEvent(e);
}

void FloatingPanel::paintEvent(QPaintEvent*) {
    const auto c = Theme::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF card = QRectF(rect()).adjusted(kShadow, kShadow, -kShadow, -kShadow);
    // soft shadow: stacked translucent rounded rects (cheap fake blur)
    p.setPen(Qt::NoPen);
    for (int i = kShadow; i > 0; i -= 2) {
        const qreal k = 1.0 - qreal(i) / kShadow;
        p.setBrush(QColor(0, 0, 0, int((Theme::isDark() ? 16 : 9) * k * k)));
        p.drawRoundedRect(card.adjusted(-i, -i + 6, i, i + 6), 18 + i, 18 + i);
    }
    QPainterPath shape;
    shape.addRoundedRect(card, 18, 18);
    p.fillPath(shape, Theme::isBlack() ? c.card : Theme::isDark() ? QColor(28, 30, 44, 250) : QColor(255, 255, 255, 252));
    // accent glow in the corner, like the main window
    QRadialGradient g(card.topRight(), card.width() * 0.7);
    QColor a = c.accent; a.setAlpha(Theme::isDark() ? 45 : 30);
    g.setColorAt(0, a);
    a.setAlpha(0);
    g.setColorAt(1, a);
    p.save();
    p.setClipPath(shape);
    p.fillRect(card, g);
    p.restore();
    p.setPen(QPen(c.border, 1));
    p.setBrush(Qt::NoBrush);
    p.drawRoundedRect(card.adjusted(0.5, 0.5, -0.5, -0.5), 18, 18);
    paintOverlay(p, card);
}

// ============================== QuickAddPanel ==============================

QuickAddPanel::QuickAddPanel(QWidget* parent) : FloatingPanel(parent) {
    setFixedSize(420 + 2 * kShadow, 365 + 2 * kShadow);
    m_form = new QWidget;
    m_form->setObjectName("gridHolder");
    auto* v = new QVBoxLayout(m_form);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(8);

    auto* head = new QHBoxLayout;
    auto* title = new QLabel("⚡ Quick add");
    title->setObjectName("dialogTitle");
    auto* hint = new QLabel("Enter to save · Esc to close");
    hint->setObjectName("muted");
    head->addWidget(title);
    head->addStretch();
    head->addWidget(hint);
    v->addLayout(head);
    v->addSpacing(4);

    m_url = new QLineEdit;
    m_url->setPlaceholderText("https://example.com");
    m_url->setLayoutDirection(Qt::LeftToRight);
    m_name = new QLineEdit;
    m_name->setPlaceholderText("Title (optional)");
    m_note = new QLineEdit;
    m_note->setPlaceholderText("Note (optional)");
    m_note->setAccessibleName("Bookmark note");
    m_note->setMaxLength(1000);
    m_collection = new QComboBox;
    m_collection->setCursor(Qt::PointingHandCursor);
    v->addWidget(m_url);
    v->addWidget(m_name);
    v->addWidget(m_note);
    v->addWidget(m_collection);
    m_error = new QLabel;
    m_error->setObjectName("error");
    m_error->setMinimumHeight(18);
    v->addWidget(m_error);
    body()->addWidget(m_form);

    for (QWidget* w : QList<QWidget*>{m_url, m_name, m_note, m_collection})
        w->installEventFilter(this);

    m_successAnim = anim(this, 520, Theme::easeSpring(1.4));
    connect(m_successAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_success = v.toReal(); update(); });
}

void QuickAddPanel::open(const QString& url, const QList<QPair<QString, QString>>& collections, const QString& current) {
    m_success = 0;
    m_savedName.clear();
    m_form->show();
    m_error->clear();
    m_url->setText(url);
    m_name->clear();
    m_note->clear();
    m_collection->clear();
    for (const auto& c : collections) m_collection->addItem(c.second, c.first);
    const int idx = m_collection->findData(current);
    m_collection->setCurrentIndex(idx >= 0 ? idx : 0);
    m_collection->setVisible(collections.size() > 1);

    const QRect area = screenUnderCursor()->availableGeometry();
    const QPoint pos(area.right() - width() + kShadow - 14, area.bottom() - height() + kShadow - 14);
    popup(pos, 26);
    (url.isEmpty() ? m_url : m_name)->setFocus();
    if (url.isEmpty()) m_url->selectAll();
}

bool QuickAddPanel::eventFilter(QObject* o, QEvent* e) {
    if (e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        if (k->key() == Qt::Key_Return || k->key() == Qt::Key_Enter) {
            if (o == m_collection && m_collection->view() && m_collection->view()->isVisible()) return false;
            submit();
            return true;
        }
        if (k->key() == Qt::Key_Escape) { dismiss(); return true; }
    }
    return FloatingPanel::eventFilter(o, e);
}

void QuickAddPanel::submit() {
    if (m_success > 0) return;
    const QUrl u = parseUserUrl(m_url->text());
    if (!isWebUrl(u)) { showError("Enter a valid web address."); m_url->setFocus(); return; }
    emit submitted(u.toString(), m_name->text().trimmed(), m_collection->currentData().toString(),
                   m_note->text().trimmed());
}

void QuickAddPanel::showError(const QString& text) {
    m_error->setText(text);
    shake();
}

void QuickAddPanel::shake() {
    if (!Theme::animations()) return;
    const QPoint base = pos();
    auto* a = anim(this, 360, QEasingCurve::Linear);
    a->setStartValue(0.0);
    a->setEndValue(1.0);
    connect(a, &QVariantAnimation::valueChanged, this, [this, base](const QVariant& v) {
        const qreal t = v.toReal();
        move(base + QPoint(qRound(qSin(t * M_PI * 6) * 9 * (1 - t)), 0));   // damped wiggle
    });
    connect(a, &QAbstractAnimation::finished, this, [this, base] { move(base); });
    a->start(QAbstractAnimation::DeleteWhenStopped);
}

void QuickAddPanel::showSaved(const QString& name) {
    m_savedName = name;
    m_form->hide();
    m_successAnim->stop();
    if (Theme::animations()) {
        m_successAnim->setStartValue(0.0);
        m_successAnim->setEndValue(1.0);
        m_successAnim->start();
    } else {
        m_success = 1;
        update();
    }
    QTimer::singleShot(1000, this, [this] { dismiss(); });
}

void QuickAddPanel::paintOverlay(QPainter& p, const QRectF& card) {
    if (m_success <= 0) return;
    const auto c = Theme::colors();
    const qreal s = qMax<qreal>(0, m_success);
    const QPointF ctr = card.center() + QPointF(0, -18);
    const qreal R = 34 * s;
    QLinearGradient g(ctr - QPointF(R, R), ctr + QPointF(R, R));
    g.setColorAt(0, c.accentHover);
    g.setColorAt(1, c.accent);
    p.setPen(Qt::NoPen);
    p.setBrush(g);
    p.drawEllipse(ctr, R, R);
    // check mark draws itself as the circle lands
    const qreal t = qBound<qreal>(0, (m_success - 0.35) / 0.65, 1);
    if (t > 0) {
        QPainterPath path;
        path.moveTo(ctr + QPointF(-14, 1));
        path.lineTo(ctr + QPointF(-4, 11));
        path.lineTo(ctr + QPointF(15, -10));
        QPen pen(Qt::white, 4.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        const qreal len = path.length();
        pen.setDashPattern({len / 4.2 * t, 1000});
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawPath(path);
    }
    QFont f = font(); f.setPixelSize(16); f.setWeight(QFont::DemiBold);
    p.setFont(f);
    p.setOpacity(qBound<qreal>(0, m_success, 1));
    p.setPen(c.text);
    const QString line = m_savedName.isEmpty() ? QString("Saved") : QString("Saved “%1”").arg(m_savedName);
    p.drawText(QRectF(card.left() + 20, ctr.y() + 46, card.width() - 40, 26), Qt::AlignCenter,
               QFontMetricsF(f).elidedText(line, Qt::ElideMiddle, card.width() - 40));
    p.setOpacity(1);
}

// ============================== PaletteList ==============================

class PaletteList : public QWidget {
public:
    static constexpr int kRow = 52;
    explicit PaletteList(QWidget* parent = nullptr) : QWidget(parent) {
        setMouseTracking(true);
        m_sel = anim(this, 150, Theme::easeOut());
        connect(m_sel, &QVariantAnimation::valueChanged, this, [this](const QVariant& v) { m_selY = v.toReal(); update(); });
    }
    std::function<void(int)> onClick;

    void setItems(const QList<PaletteItem>& items, const QStringList& tokens) {
        m_items = items;
        m_tokens = tokens;
        m_cur = items.isEmpty() ? -1 : 0;
        m_sel->stop();
        m_selY = 0;
        setFixedHeight(qMax(1, int(items.size())) * kRow);
        update();
    }
    int current() const { return m_cur; }
    int count() const { return int(m_items.size()); }
    const PaletteItem& item(int i) const { return m_items[i]; }
    void setCurrent(int i) {
        if (m_items.isEmpty()) return;
        i = (i + int(m_items.size())) % int(m_items.size());       // wrap around
        if (i == m_cur) return;
        m_cur = i;
        m_sel->stop();
        if (!Theme::animations()) { m_selY = i * kRow; update(); return; }
        m_sel->setStartValue(m_selY);
        m_sel->setEndValue(qreal(i * kRow));
        m_sel->start();
    }

protected:
    void mouseMoveEvent(QMouseEvent* e) override {
        const int i = int(e->position().y()) / kRow;
        if (i >= 0 && i < m_items.size()) setCurrent(i);
    }
    void mouseReleaseEvent(QMouseEvent* e) override {
        const int i = int(e->position().y()) / kRow;
        if (e->button() == Qt::LeftButton && i >= 0 && i < m_items.size() && onClick) onClick(i);
    }
    void paintEvent(QPaintEvent*) override {
        const auto c = Theme::colors();
        QPainter p(this);
        p.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
        if (m_items.isEmpty()) {
            p.setPen(c.muted);
            p.drawText(rect(), Qt::AlignCenter, QString("No results"));
            return;
        }
        // selection highlight glides between rows
        if (m_cur >= 0) {
            const QRectF sel(0, m_selY + 2, width(), kRow - 4);
            QColor bg = c.accent; bg.setAlpha(Theme::isDark() ? 52 : 30);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(sel, 12, 12);
            p.setBrush(c.accent);
            const qreal bx = isRightToLeft() ? sel.right() - 4 : sel.left() + 1;
            p.drawRoundedRect(QRectF(bx, sel.top() + 12, 3, sel.height() - 24), 1.5, 1.5);
        }
        const bool rtl = isRightToLeft();
        for (int i = 0; i < m_items.size(); ++i) {
            const PaletteItem& it = m_items[i];
            const QRectF row(0, i * kRow, width(), kRow);
            const QRectF iconR(rtl ? row.right() - 14 - 32 : row.left() + 14, row.center().y() - 16, 32, 32);
            // icon tile
            const QColor tint = it.color.isValid() ? it.color : c.accent;
            QColor tile = tint; tile.setAlpha(Theme::isDark() ? 60 : 36);
            p.setPen(Qt::NoPen);
            p.setBrush(it.iconPath.isEmpty() ? tile : QColor(255, 255, 255, 240));
            p.drawRoundedRect(iconR, 9, 9);
            const QImage icon = iconFor(it.iconPath);
            if (!icon.isNull()) {
                p.drawImage(iconR.adjusted(6, 6, -6, -6), icon);
            } else {
                QFont gf = font(); gf.setPixelSize(16); gf.setBold(true);
                p.setFont(gf);
                p.setPen(Theme::isDark() ? tint.lighter(150) : tint.darker(120));
                p.drawText(iconR, Qt::AlignCenter, it.glyph.isEmpty() ? it.title.left(1).toUpper() : it.glyph);
            }
            // text
            const qreal tx = rtl ? row.left() + 14 : iconR.right() + 12;
            const qreal tw = row.width() - 14 - 32 - 12 - 14;
            QFont tf = font(); tf.setPixelSize(14); tf.setWeight(QFont::DemiBold);
            QFont sf = font(); sf.setPixelSize(12);
            const bool two = !it.subtitle.isEmpty();
            drawHighlighted(p, QRectF(tx, row.top() + (two ? 7 : 14), tw, 22), tf, it.title, c.text);
            if (two) {
                p.setFont(sf);
                p.setPen(c.muted);
                p.drawText(QRectF(tx, row.top() + 28, tw, 18), Qt::AlignVCenter | (rtl ? Qt::AlignRight : Qt::AlignLeft),
                           QFontMetricsF(sf).elidedText(it.subtitle, Qt::ElideMiddle, tw));
            }
        }
    }

private:
    QImage iconFor(const QString& path) {
        if (path.isEmpty()) return {};
        auto it = m_icons.find(path);
        if (it != m_icons.end()) return it.value();
        QImage img(path);
        if (!img.isNull()) {
            const qreal dpr = devicePixelRatioF();
            img = img.scaled(QSize(20, 20) * dpr, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        m_icons.insert(path, img);
        return img;
    }
    void drawHighlighted(QPainter& p, const QRectF& r, const QFont& f, const QString& text, const QColor& color) {
        const QString shown = QFontMetricsF(f).elidedText(text, Qt::ElideRight, r.width());
        QTextLayout layout(shown, f);
        QTextOption opt(isRightToLeft() ? Qt::AlignRight : Qt::AlignLeft);
        opt.setWrapMode(QTextOption::NoWrap);
        opt.setTextDirection(shown.isRightToLeft() ? Qt::RightToLeft : Qt::LeftToRight);
        layout.setTextOption(opt);
        QList<QTextLayout::FormatRange> ranges;
        const QString low = normText(shown);
        QColor bg = Theme::colors().accent; bg.setAlpha(Theme::isDark() ? 80 : 50);
        for (const QString& tok : m_tokens) {
            if (tok.isEmpty() || low.size() != shown.size()) continue;
            int from = 0, idx;
            while ((idx = int(low.indexOf(tok, from))) >= 0) {
                QTextLayout::FormatRange fr;
                fr.start = idx;
                fr.length = int(tok.size());
                fr.format.setBackground(bg);
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

    QList<PaletteItem> m_items;
    QStringList m_tokens;
    QHash<QString, QImage> m_icons;
    int m_cur = -1;
    qreal m_selY = 0;
    QVariantAnimation* m_sel;
};

// ============================== CommandPalette ==============================

CommandPalette::CommandPalette(QWidget* parent) : FloatingPanel(parent) {
    setFixedWidth(620 + 2 * kShadow);
    m_input = new QLineEdit;
    m_input->setObjectName("paletteInput");
    m_input->setPlaceholderText("Search by title or URL. Type > for commands.");
    m_input->installEventFilter(this);
    connect(m_input, &QLineEdit::textChanged, this, &CommandPalette::requery);
    m_list = new PaletteList;
    m_list->onClick = [this](int row) { activate(row, false); };
    m_hint = new QLabel("↑↓ Select · Enter Open · Ctrl+Enter Copy · Esc Close");
    m_hint->setObjectName("muted");
    m_hint->setAlignment(Qt::AlignCenter);
    body()->addWidget(m_input);
    body()->addWidget(m_list);
    body()->addWidget(m_hint);
}

void CommandPalette::open() {
    m_input->clear();
    requery();
    const QRect area = screenUnderCursor()->availableGeometry();
    const QPoint pos(area.center().x() - width() / 2, area.top() + area.height() / 5);
    popup(pos, -12);
    m_input->setFocus();
}

void CommandPalette::requery() {
    const QString q = m_input->text();
    QStringList tokens;
    QString t = q.startsWith('>') ? q.mid(1) : q;
    for (const QString& w : t.simplified().split(' ', Qt::SkipEmptyParts)) tokens << normText(w);
    m_list->setItems(m_provider ? m_provider(q) : QList<PaletteItem>{}, tokens);
    adjustSize();
    setFixedHeight(sizeHint().height());
}

void CommandPalette::activate(int row, bool copyOnly) {
    if (row < 0 || row >= m_list->count()) return;
    const PaletteItem item = m_list->item(row);
    dismiss();
    emit activated(item, copyOnly);
}

bool CommandPalette::eventFilter(QObject* o, QEvent* e) {
    if (o == m_input && e->type() == QEvent::KeyPress) {
        auto* k = static_cast<QKeyEvent*>(e);
        switch (k->key()) {
        case Qt::Key_Down: m_list->setCurrent(m_list->current() + 1); return true;
        case Qt::Key_Up: m_list->setCurrent(m_list->current() - 1); return true;
        case Qt::Key_Tab: m_list->setCurrent(m_list->current() + 1); return true;
        case Qt::Key_Return:
        case Qt::Key_Enter: activate(m_list->current(), k->modifiers().testFlag(Qt::ControlModifier)); return true;
        case Qt::Key_Escape:
            if (!m_input->text().isEmpty()) m_input->clear(); else dismiss();
            return true;
        default: break;
        }
    }
    return FloatingPanel::eventFilter(o, e);
}
