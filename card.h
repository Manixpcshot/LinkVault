#pragma once
#include <QWidget>
#include <QImage>
#include <QElapsedTimer>
#include <QStringList>
#include <functional>
#include "bookmark.h"

class QVariantAnimation;
class QTimer;
class QMenu;

// Fully custom-painted bookmark tile: cover image, favicon badge, hover lift & zoom,
// pin button, search highlight, loading shimmer, appear / remove animations.
class BookmarkCard : public QWidget {
    Q_OBJECT
public:
    static constexpr int kMargin = 18;                 // room for lift + painted shadow
    static constexpr int kInnerW = 256, kInnerH = 238, kImgH = 148;
    static constexpr int kW = kInnerW + 2 * kMargin;
    static constexpr int kH = kInnerH + 2 * kMargin;

    BookmarkCard(const Bookmark& b, bool dark, QWidget* parent = nullptr);
    QString id() const { return m_id; }
    void setBookmark(const Bookmark& b);
    void setLoading(bool on);
    void setDark(bool dark);
    void setHighlight(const QStringList& tokens);
    void playRemove(std::function<void()> done);
    QColor accent() const { return m_accent; }
    void setCollectionTag(const QString& text, const QColor& color);

    // lets MainWindow add its own items (collections, reminders, ...) to every card menu
    static std::function<void(QMenu&, const QString& id)>& menuHook() {
        static std::function<void(QMenu&, const QString&)> hook;
        return hook;
    }

public slots:
    void playAppear(int delayMs);

signals:
    void openRequested(const QString& id);
    void editRequested(const QString& id);
    void deleteRequested(const QString& id);
    void refreshRequested(const QString& id);
    void pinToggleRequested(const QString& id);
    void copyRequested(const QString& id);

protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void contextMenuEvent(QContextMenuEvent* e) override;

private:
    void showMenu(const QPoint& globalPos);
    void paintMedia(QPainter& p, const QRectF& img, qreal t);
    void paintShimmer(QPainter& p, const QRectF& img);
    void paintBadge(QPainter& p, const QPointF& c);
    void drawHighlighted(QPainter& p, const QRectF& r, const QFont& f, const QString& text,
                         const QColor& color, Qt::TextElideMode mode);

    QString m_id, m_url, m_name, m_host, m_date;
    QStringList m_tokens;
    bool m_dark;
    bool m_loading = false;
    bool m_pressed = false;
    bool m_pinned = false;
    bool m_initialized = false;
    bool m_removing = false;
    int m_hoverBtn = 0;                // 0 none, 1 pin, 2 menu
    int m_health = 0;
    qint64 m_remindAt = 0;
    QString m_tag;
    QColor m_tagColor;
    QPointF m_pressPos;
    void startDrag();
    qreal m_hover = 0, m_phase = 0, m_appear = 1, m_press = 0, m_pinT = 0;
    QColor m_accent;
    QImage m_cover, m_iconBig, m_iconSmall;
    QRectF m_menuRect, m_pinRect;
    QVariantAnimation* m_anim = nullptr;
    QVariantAnimation* m_appearAnim = nullptr;
    QVariantAnimation* m_pressAnim = nullptr;
    QVariantAnimation* m_pinAnim = nullptr;
    QTimer* m_timer = nullptr;
    QElapsedTimer m_clock;
};

// Dashed "add new site" tile shown at the end of the grid
class AddCard : public QWidget {
    Q_OBJECT
public:
    explicit AddCard(bool dark, QWidget* parent = nullptr);
    void setDark(bool dark) { m_dark = dark; update(); }
public slots:
    void playAppear(int delayMs);
signals:
    void clicked();
protected:
    void paintEvent(QPaintEvent*) override;
    void enterEvent(QEnterEvent*) override;
    void leaveEvent(QEvent*) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
private:
    bool m_dark;
    qreal m_hover = 0, m_appear = 1;
    QVariantAnimation* m_anim = nullptr;
    QVariantAnimation* m_appearAnim = nullptr;
};
