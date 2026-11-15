#pragma once
#include <QAbstractButton>
#include <QWidget>

class QVariantAnimation;

// Custom-painted window control (minimize / maximize-restore / close)
class WindowButton : public QAbstractButton {
    Q_OBJECT
public:
    enum Kind { Minimize, Maximize, Close };
    WindowButton(Kind kind, QWidget* parent = nullptr);
    void setDark(bool d) { m_dark = d; update(); }
    void setRestore(bool r) { m_restore = r; update(); }
    // driven from WM_NC* messages when Windows owns the mouse over this button (Snap Layouts)
    void setNcHover(bool h);
    void setNcDown(bool d) { if (d != m_ncDown) { m_ncDown = d; update(); } }
    bool ncHover() const { return m_ncHover; }
    QSize sizeHint() const override { return {42, 30}; }

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;

private:
    Kind m_kind;
    bool m_dark = false;
    bool m_restore = false;
    bool m_ncHover = false, m_ncDown = false;
    qreal m_hover = 0;
    QVariantAnimation* m_anim;
};

// Draggable title strip: double-click toggles maximize
class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget* window);
    void setDark(bool d);
    void setMaximized(bool m);
    void toggleMax();
    QRect maxButtonRect() const;                 // in window coordinates
    WindowButton* maxButton() const { return m_max; }

protected:
    void mousePressEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;

private:
    QWidget* m_window;
    WindowButton* m_min;
    WindowButton* m_max;
    WindowButton* m_close;
};
