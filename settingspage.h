#pragma once
#include <QKeySequence>
#include <QWidget>

class QLabel;
class QVBoxLayout;
class QFrame;
class QPushButton;
class QKeySequenceEdit;
class ToggleSwitch;
class SegmentedControl;

struct SettingsState {
    int themeMode = 0;
    bool autoStart = false, animations = true, minimizeOnOpen = false, useClipboard = true;
    bool mica = false, micaSupported = false;
    bool closeToTray = false;
    bool linkCheck = true;
    bool hotkeysSupported = false;
    QKeySequence quickAddKey, paletteKey;
    QString syncFolder, syncStatus, linkStatus;
    int total = 0, pinned = 0;
};

// Full in-app settings page (replaces the old popup dialog)
class SettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit SettingsPage(QWidget* parent = nullptr);
    void sync(const SettingsState& s);
    void setHotkeyError(int which, const QString& error);   // 1 quick add, 2 palette; empty = ok
    void setLinkStatus(const QString& text);
    void setSyncStatus(const QString& folder, const QString& status);

signals:
    void backRequested();
    void themeModeChanged(int mode);          // 0 light, 1 dark, 2 system
    void autoStartChanged(bool on);
    void animationsChanged(bool on);
    void minimizeOnOpenChanged(bool on);
    void clipboardChanged(bool on);
    void micaChanged(bool on);
    void closeToTrayChanged(bool on);
    void linkCheckChanged(bool on);
    void checkLinksNowRequested();
    void quickAddKeyChanged(const QKeySequence& k);
    void paletteKeyChanged(const QKeySequence& k);
    void syncChooseRequested();
    void syncDisableRequested();
    void syncNowRequested();
    void openFolderRequested();
    void exportRequested();
    void importRequested();

private:
    QFrame* makeCard(const QString& title, QVBoxLayout** body);
    QLabel* addRow(QVBoxLayout* body, const QString& title, const QString& desc, QWidget* control);
    QWidget* makeKeyEditor(QKeySequenceEdit** edit, int which);
    SegmentedControl* m_theme;
    ToggleSwitch* m_anim;
    ToggleSwitch* m_mica;
    QLabel* m_micaDesc = nullptr;
    ToggleSwitch* m_auto;
    ToggleSwitch* m_min;
    ToggleSwitch* m_clip;
    ToggleSwitch* m_tray;
    ToggleSwitch* m_check;
    QLabel* m_linkStatus = nullptr;
    QKeySequenceEdit* m_quickKey = nullptr;
    QKeySequenceEdit* m_paletteKey = nullptr;
    QLabel* m_quickDesc = nullptr;
    QLabel* m_paletteDesc = nullptr;
    QLabel* m_syncDesc = nullptr;
    QPushButton* m_syncChoose = nullptr;
    QPushButton* m_syncOff = nullptr;
    QPushButton* m_syncNow = nullptr;
    QLabel* m_stats = nullptr;
    bool m_syncing = false;
};
