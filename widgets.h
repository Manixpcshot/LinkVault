#pragma once
#include <QElapsedTimer>
#include <QAbstractButton>
#include <QHash>
#include <QLineEdit>
#include <QList>
#include <QPointer>
#include <QStringList>
#include <QWidget>
#include <QColor>

class QVariantAnimation;
class QPropertyAnimation;
class QParallelAnimationGroup;
class QAction;

// Animated on/off switch (knob slides, track fades to accent)
class ToggleSwitch : public QAbstractButton {
    Q_OBJECT
public:
    explicit ToggleSwitch(QWidget* parent = nullptr);
    QSize sizeHint() const override { return {48, 28}; }
protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void showEvent(QShowEvent*) override;
private:
    qreal m_pos = 0, m_hover = 0;
    QVariantAnimation* m_anim;
    QVariantAnimation* m_hoverAnim;
};

// Pill-shaped segmented control with a sliding indicator
class SegmentedControl : public QWidget {
    Q_OBJECT
public:
    explicit SegmentedControl(const QStringList& items, QWidget* parent = nullptr);
    void setCurrent(int index, bool animate = true);   // does NOT emit currentChanged
    int current() const { return m_cur; }
    void setItemText(int index, const QString& text);
    QSize sizeHint() const override;
    QSize minimumSizeHint() const override { return sizeHint(); }
signals:
    void currentChanged(int index);   // user clicks only
protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
private:
    int indexAt(const QPointF& p) const;
    QRectF segRect(qreal pos) const;
    QStringList m_items;
    int m_cur = 0, m_hoverIdx = -1;
    qreal m_pos = 0;
    QVariantAnimation* m_anim;
};

// Search field with painted magnifier icon; widens smoothly when focused
class SearchBox : public QLineEdit {
    Q_OBJECT
public:
    explicit SearchBox(QWidget* parent = nullptr);
    void setDark(bool dark);
    void setWidths(int normal, int focused);
    void setStretchable();
protected:
    void focusInEvent(QFocusEvent*) override;
    void focusOutEvent(QFocusEvent*) override;
private:
    void animateWidth(int to);
    QAction* m_icon = nullptr;
    QVariantAnimation* m_anim;
    int m_normal = 280, m_focused = 380;
    bool m_stretchable = false;
};

// Bottom snackbar with optional action button (e.g. undo) and countdown bar
class Toast : public QWidget {
    Q_OBJECT
public:
    explicit Toast(QWidget* parent);
    void showMessage(const QString& text, const QString& action = {}, int msec = 4500);
    void dismiss();
signals:
    void actionTriggered();
    void timedOut();
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    bool eventFilter(QObject* obj, QEvent* ev) override;
private:
    void reposition();
    QString m_text, m_action;
    QRectF m_actionRect;
    bool m_hoverAction = false;
    qreal m_show = 0, m_life = 1;
    QVariantAnimation* m_showAnim;
    QVariantAnimation* m_lifeAnim;
};

// Page container with a horizontal push/slide transition
class SlideStack : public QWidget {
    Q_OBJECT
public:
    explicit SlideStack(QWidget* parent = nullptr);
    int addPage(QWidget* page);
    void slideTo(int index);
    int current() const { return m_cur; }
signals:
    void currentChanged(int index);
protected:
    void resizeEvent(QResizeEvent*) override;
private:
    void finishTransition();
    QList<QWidget*> m_pages;
    int m_cur = 0;
    QPointer<QVariantAnimation> m_anim;
    QPointer<QWidget> m_overlay;
};

// One-shot "zoom" used for full screen enter/exit: a snapshot of the old window
// content grows/shrinks from its old rect to the new one while fading out.
class ZoomOverlay : public QWidget {
public:
    ZoomOverlay(QWidget* parent, const QPixmap& shot, const QRectF& from, int ms = 420);
protected:
    void paintEvent(QPaintEvent*) override;
private:
    QPixmap m_shot;
    QRectF m_from;
    qreal m_t = 0;
    QVariantAnimation* m_anim;
};

// Responsive card grid. Cards glide to their new spot (FLIP style) when the
// list is filtered / sorted / pinned, and new ones fade in with a stagger.
class CardGrid : public QWidget {
    Q_OBJECT
public:
    struct Item { QPointer<QWidget> w; bool fullRow = false; };
    CardGrid(QSize tile, QWidget* parent = nullptr);
    void setItems(const QList<Item>& items, bool animate);
protected:
    void resizeEvent(QResizeEvent*) override;
private:
    void layoutItems(bool animate);
    void moveAnimated(QWidget* w, const QPoint& target);
    void stopAnim(QWidget* w);
    QList<Item> m_items;
    QSize m_tile;
    QHash<QWidget*, QPointer<QPropertyAnimation>> m_anims;
};

// Horizontal, scrollable chip bar for collections. The first chip ("all") has an empty id.
// Cards can be dragged onto a chip to move them into that collection.
class CollectionBar : public QWidget {
    Q_OBJECT
public:
    struct Chip { QString id; QString text; QColor color; int count = 0; };
    static constexpr const char* kMime = "application/x-linkvault-bookmark";

    explicit CollectionBar(QWidget* parent = nullptr);
    void setChips(const QList<Chip>& chips);
    void setCurrent(const QString& id, bool animate = true);
    QString current() const;
    QSize sizeHint() const override { return {480, 42}; }
    QSize minimumSizeHint() const override { return {120, 42}; }

signals:
    void currentChanged(const QString& id);
    void addRequested();
    void dropped(const QString& bookmarkId, const QString& collectionId);
    void menuRequested(const QString& collectionId, const QPoint& globalPos);

protected:
    void paintEvent(QPaintEvent*) override;
    void mousePressEvent(QMouseEvent*) override;
    void mouseReleaseEvent(QMouseEvent*) override;
    void mouseMoveEvent(QMouseEvent*) override;
    void leaveEvent(QEvent*) override;
    void wheelEvent(QWheelEvent*) override;
    void resizeEvent(QResizeEvent*) override;
    void contextMenuEvent(QContextMenuEvent*) override;
    void dragEnterEvent(QDragEnterEvent*) override;
    void dragMoveEvent(QDragMoveEvent*) override;
    void dragLeaveEvent(QDragLeaveEvent*) override;
    void dropEvent(QDropEvent*) override;

private:
    void relayout();
    QRectF visual(const QRectF& content) const;     // content (LTR, unscrolled) -> widget coords
    int hitTest(const QPointF& pos) const;          // chip index, -2 = "+" chip, -1 = nothing
    qreal maxScroll() const;
    void scrollTo(qreal x);
    void ensureVisible(int index);
    void animateIndicator(int toIndex, bool animate);

    QList<Chip> m_chips;
    QList<QRectF> m_rects;
    QRectF m_addRect;
    qreal m_contentW = 0;
    qreal m_scroll = 0;
    qreal m_pressX = 0, m_pressScroll = 0;
    QElapsedTimer m_touchPress;
    bool m_touchDrag = false;
    int m_cur = 0, m_hover = -1, m_drop = -1;
    QRectF m_indFrom, m_indTo;
    qreal m_indT = 1;
    QVariantAnimation* m_indAnim;
    QVariantAnimation* m_scrollAnim;
};
