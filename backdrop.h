#pragma once
#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QtMath>

// Soft gradient background with colored glows and a subtle dot grid (cached).
// Optional "Mica" material: a heavily blurred copy of the desktop wallpaper that stays fixed to the
// screen while the window moves (exactly how Windows 11 Mica behaves), tinted by the theme.
// It is painted by us, so it works on Windows 10 too and never fights Qt's frameless rendering.
class Backdrop : public QWidget {
public:
    explicit Backdrop(QWidget* parent = nullptr) : QWidget(parent) {
        setAttribute(Qt::WA_OpaquePaintEvent);
    }
    void setDark(bool d) { m_dark = d; m_cache = QPixmap(); update(); }
    void setFramed(bool f) { m_framed = f; m_cache = QPixmap(); update(); }

    // material = blurred wallpaper at the size of `screen` (logical px); null image = off
    void setMaterial(const QImage& material, const QRect& screen) {
        m_material = material.isNull() ? QPixmap() : QPixmap::fromImage(material);
        m_screen = screen;
        m_cache = QPixmap();
        update();
    }
    bool hasMaterial() const { return !m_material.isNull(); }
    void setWindowPos(const QPoint& globalTopLeft) {
        if (globalTopLeft == m_winPos) return;
        m_winPos = globalTopLeft;
        if (hasMaterial()) update();          // only the material moves, the overlay stays cached
    }

protected:
    void resizeEvent(QResizeEvent* e) override { m_cache = QPixmap(); QWidget::resizeEvent(e); }
    void paintEvent(QPaintEvent* e) override {
        const qreal dpr = devicePixelRatioF();
        if (m_cache.isNull() || m_cache.size() != size() * dpr) m_cache = render(dpr);
        QPainter p(this);
        if (hasMaterial()) {
            p.fillRect(e->rect(), m_dark ? QColor("#101119") : QColor("#F3F2F8"));
            const QPoint off = m_screen.topLeft() - m_winPos;     // screen origin in widget coords
            p.drawPixmap(QRect(off, m_screen.size()), m_material);
        }
        p.drawPixmap(0, 0, m_cache);
    }

private:
    QPixmap render(qreal dpr) const {
        QPixmap pm(size() * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        const qreal w = width(), h = height(), m = qMax(w, h);
        const bool mica = hasMaterial();

        if (mica) {
            // Mica tint: lets ~20% of the wallpaper colour through
            p.fillRect(rect(), m_dark ? QColor(17, 18, 29, 205) : QColor(248, 247, 253, 196));
        } else {
            QLinearGradient base(0, 0, w, h);
            base.setColorAt(0, m_dark ? QColor("#0D0E19") : QColor("#F8F6FF"));
            base.setColorAt(1, m_dark ? QColor("#141829") : QColor("#EEF3FF"));
            p.fillRect(rect(), base);
        }

        auto glow = [&](QPointF c, qreal r, QColor col) {
            QRadialGradient g(c, r);
            g.setColorAt(0, col);
            col.setAlpha(0);
            g.setColorAt(1, col);
            p.fillRect(rect(), g);
        };
        const qreal k = mica ? 0.55 : 1.0;          // calmer glows on top of the material
        glow({w * 0.05, h * 0.02}, m * 0.55, QColor(139, 108, 255, int((m_dark ? 75 : 70) * k)));
        glow({w * 0.98, h * 0.30}, m * 0.45, QColor(72, 140, 255, int((m_dark ? 60 : 55) * k)));
        glow({w * 0.55, h * 1.08}, m * 0.50, QColor(255, 120, 190, int((m_dark ? 32 : 40) * k)));

        p.setPen(Qt::NoPen);
        p.setBrush(m_dark ? QColor(255, 255, 255, 13) : QColor(50, 40, 110, 16));
        for (int y = 14; y < h; y += 26)
            for (int x = 14; x < w; x += 26)
                p.drawEllipse(QPointF(x, y), 1.1, 1.1);
        if (m_framed) {
            p.setRenderHint(QPainter::Antialiasing, false);
            p.setBrush(Qt::NoBrush);
            p.setPen(m_dark ? QColor(255, 255, 255, 28) : QColor(40, 30, 100, 40));
            p.drawRect(QRect(0, 0, width() - 1, height() - 1));
        }
        return pm;
    }
    bool m_dark = false;
    bool m_framed = true;
    QPixmap m_cache;
    QPixmap m_material;
    QRect m_screen;
    QPoint m_winPos;
};
