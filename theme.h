#pragma once
#include <QString>
#include <QColor>
#include <QEasingCurve>
#include <QPointF>

namespace Theme {
struct Colors { QColor bg, card, border, text, muted, accent, accentHover, input, glass; };
enum Mode { Light, Dark, System, Black };

// global UI state shared by custom-painted widgets
inline bool& darkRef()  { static bool d = false; return d; }
inline bool& blackRef() { static bool b = false; return b; }
inline bool& animRef()  { static bool a = true;  return a; }
inline bool isDark()     { return darkRef(); }
inline bool isBlack()    { return blackRef(); }
inline bool animations() { return animRef(); }

// ---- motion: cubic-bezier curves close to Windows 11 / Fluent motion ----
inline QEasingCurve bezier(qreal x1, qreal y1, qreal x2, qreal y2) {
    QEasingCurve c(QEasingCurve::BezierSpline);
    c.addCubicBezierSegment(QPointF(x1, y1), QPointF(x2, y2), QPointF(1, 1));
    return c;
}
inline QEasingCurve easeOut()    { return bezier(0.22, 1.0, 0.36, 1.0); }
inline QEasingCurve easeInOut()  { return bezier(0.40, 0.0, 0.20, 1.0); }
inline QEasingCurve easeIn()     { return bezier(0.55, 0.0, 0.90, 0.40); } // exits
inline QEasingCurve easeSpring(qreal overshoot = 1.15) {                   // gentle overshoot (was OutBack 1.70)
    QEasingCurve c(QEasingCurve::OutBack);
    c.setOvershoot(overshoot);
    return c;
}

inline Colors colors(bool dark) {
    if (dark && isBlack()) {
        return {QColor("#000000"), QColor("#101014"), QColor("#303038"), QColor("#F5F4FA"), QColor("#B0ADBF"),
                QColor("#7664EB"), QColor("#907EFF"), QColor("#16161C"), QColor("#101014")};
    }
    return dark
        ? Colors{QColor("#12131C"), QColor("#1C1E2A"), QColor("#2C2F3E"), QColor("#ECEDF4"), QColor("#8B90A3"),
                 QColor("#8B7CFF"), QColor("#9D90FF"), QColor("#1F2230"), QColor(30, 33, 48, 215)}
        : Colors{QColor("#F6F5FC"), QColor("#FFFFFF"), QColor("#E4E4F0"), QColor("#1E1F2B"), QColor("#767B8C"),
                 QColor("#6C5CE7"), QColor("#5A4BD6"), QColor("#FFFFFF"), QColor(255, 255, 255, 215)};
}
inline Colors colors() { return colors(isDark()); }

inline QColor mix(const QColor& a, const QColor& b, qreal t) {
    t = qBound<qreal>(0.0, t, 1.0);
    return QColor::fromRgbF(a.redF() + (b.redF() - a.redF()) * t, a.greenF() + (b.greenF() - a.greenF()) * t,
                            a.blueF() + (b.blueF() - a.blueF()) * t, a.alphaF() + (b.alphaF() - a.alphaF()) * t);
}

inline QString css(const QColor& c) {
    return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
}

inline QString styleSheet(bool dark) {
    const Colors p = colors(dark);
    QColor soft = p.accent; soft.setAlpha(dark ? 40 : 28);
    QString s = R"(
QWidget { color: @text; font-size: 14px; }
QMainWindow, QDialog, QMessageBox { background: @bg; }
QLabel, QCheckBox { background: transparent; }
QScrollArea, QScrollArea > QWidget, #gridHolder, #grid, #emptyBox, #page { background: transparent; border: none; }
#appTitle { font-size: 24px; font-weight: 700; }
#dialogTitle { font-size: 18px; font-weight: 700; }
#muted { color: @muted; font-size: 12px; }
#emptyTitle { font-size: 18px; font-weight: 700; }
#emptyText { color: @muted; font-size: 14px; }
#resultLabel { color: @muted; font-size: 12px; }
#gridSection { color: @muted; font-size: 13px; font-weight: 700; }
#error { color: #E5484D; font-size: 12px; }
#settingsCard { background: @glass; border: 1px solid @border; border-radius: 18px; }
#cardTitle { font-size: 15px; font-weight: 700; color: @accent; }
#rowTitle { font-size: 14px; font-weight: 600; }
#sep { background: @border; border: none; }
#kbd { background: @input; border: 1px solid @border; border-bottom: 2px solid @border; border-radius: 7px;
       padding: 3px 10px; font-size: 12px; font-weight: 600; }
QLineEdit { background: @input; border: 1px solid @border; border-radius: 12px; padding: 10px 14px;
            selection-background-color: @accent; selection-color: white; }
QLineEdit:focus { border: 1px solid @accent; }
QLineEdit#search { background: @glass; border-radius: 14px; padding: 9px 8px; }
QLineEdit#search:focus { border: 1px solid @accent; background: @input; }
QPushButton { background: @card; border: 1px solid @border; border-radius: 10px; padding: 9px 16px; }
QPushButton:hover { border-color: @accent; }
QPushButton:pressed { background: @soft; }
QPushButton#primary { background: qlineargradient(x1:0,y1:0,x2:1,y2:1, stop:0 @accentHover, stop:1 @accent);
                      color: white; border: none; font-weight: 600; border-radius: 12px; padding: 10px 18px; }
QPushButton#primary:hover { background: @accentHover; }
QPushButton#ghost { background: transparent; border: 1px solid transparent; color: @accent; font-weight: 600;
                    padding: 8px 14px; border-radius: 12px; }
QPushButton#ghost:hover { background: @soft; }
QPushButton#iconBtn { background: @glass; padding: 0; min-width: 42px; max-width: 42px; min-height: 42px;
                      max-height: 42px; border-radius: 12px; font-size: 18px; }
QPushButton#sortBtn { background: @glass; border-radius: 12px; padding: 8px 14px; font-size: 13px; }
QPushButton::menu-indicator { image: none; width: 0; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 4px; }
QScrollBar::handle:vertical { background: @border; border-radius: 5px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: @accent; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: transparent; }
QMenu { background: @card; border: 1px solid @border; border-radius: 10px; padding: 6px; }
QMenu::item { padding: 8px 20px; border-radius: 6px; }
QMenu::item:selected { background: @accent; color: white; }
QMenu::item:checked { color: @accent; font-weight: 600; }
QMenu::item:checked:selected { color: white; }
QMenu::indicator { width: 0; }
QMenu::separator { height: 1px; background: @border; margin: 4px 8px; }
QToolTip { background: @card; color: @text; border: 1px solid @border; padding: 6px; }
QComboBox, QDateTimeEdit { background: @input; border: 1px solid @border; border-radius: 12px; padding: 9px 14px; }
QComboBox:hover, QDateTimeEdit:hover { border-color: @accent; }
QComboBox::drop-down { border: none; width: 26px; }
QComboBox QAbstractItemView { background: @card; border: 1px solid @border; border-radius: 10px; padding: 4px;
                              selection-background-color: @accent; selection-color: white; outline: none; }
QLineEdit#paletteInput { font-size: 17px; padding: 12px 16px; border-radius: 14px; }
QPushButton#emojiBtn { padding: 0; font-size: 18px; border-radius: 10px; background: @glass; }
QPushButton#emojiBtn:checked { background: @soft; border: 2px solid @accent; }
QPushButton:disabled { color: @muted; }
)";
    // longest keys first so "@accent" doesn't eat "@accentHover"
    s.replace("@accentHover", css(p.accentHover));
    s.replace("@accent", css(p.accent));
    s.replace("@border", css(p.border));
    s.replace("@muted", css(p.muted));
    s.replace("@input", css(p.input));
    s.replace("@glass", css(p.glass));
    s.replace("@soft", css(soft));
    s.replace("@text", css(p.text));
    s.replace("@card", css(p.card));
    s.replace("@bg", css(p.bg));
    return s;
}
}
