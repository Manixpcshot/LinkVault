#pragma once
#include <QColor>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QWidget>
#include <functional>

class QLabel;
class QPainter;
class QLineEdit;
class QComboBox;
class QVariantAnimation;
class QVBoxLayout;

// Frameless always-on-top floating card (used by Quick Add and the Command Palette).
// Fades + slides in, closes on Esc or when it loses focus.
class FloatingPanel : public QWidget {
    Q_OBJECT
public:
    explicit FloatingPanel(QWidget* parent = nullptr);
    void popup(const QPoint& topLeft, int slideDy);
    void dismiss();
    bool isClosing() const { return m_closing; }
    static constexpr int kShadow = 22;           // transparent margin used for the soft shadow

signals:
    void dismissed();

protected:
    void paintEvent(QPaintEvent*) override;
    bool event(QEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    virtual void paintOverlay(QPainter&, const QRectF&) {}
    QVBoxLayout* body() const { return m_body; }
    bool m_keepOnDeactivate = false;

private:
    QVBoxLayout* m_body;
    QVariantAnimation* m_anim;
    QPoint m_base;
    int m_dy = 0;
    bool m_closing = false;
};

// ---------------------------------------------------------------------------------------------
class QuickAddPanel : public FloatingPanel {
    Q_OBJECT
public:
    explicit QuickAddPanel(QWidget* parent = nullptr);
    // collections: (id, label) pairs; first entry should be ("", "No collection")
    void open(const QString& url, const QList<QPair<QString, QString>>& collections, const QString& currentCollection);
    void showSaved(const QString& name);
    void showError(const QString& text);

signals:
    void submitted(const QString& url, const QString& name, const QString& collectionId);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;
    void paintOverlay(QPainter& p, const QRectF& card) override;

private:
    void submit();
    void shake();
    QWidget* m_form;
    QLineEdit* m_url;
    QLineEdit* m_name;
    QComboBox* m_collection;
    QLabel* m_error;
    QString m_savedName;
    qreal m_success = 0;
    QVariantAnimation* m_successAnim;
};

// ---------------------------------------------------------------------------------------------
struct PaletteItem {
    QString key;          // bookmark id, or command id when `command` is true
    QString title;
    QString subtitle;
    QString iconPath;
    QString glyph;        // emoji / letter when there is no icon
    QColor color;
    bool command = false;
};

class PaletteList;

class CommandPalette : public FloatingPanel {
    Q_OBJECT
public:
    explicit CommandPalette(QWidget* parent = nullptr);
    void setProvider(std::function<QList<PaletteItem>(const QString&)> f) { m_provider = std::move(f); }
    void open();

signals:
    void activated(const PaletteItem& item, bool copyOnly);

protected:
    bool eventFilter(QObject* o, QEvent* e) override;

private:
    void requery();
    void activate(int row, bool copyOnly);
    QLineEdit* m_input;
    PaletteList* m_list;
    QLabel* m_hint;
    std::function<QList<PaletteItem>(const QString&)> m_provider;
};
