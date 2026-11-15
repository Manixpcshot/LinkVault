#pragma once
#include <QAbstractNativeEventFilter>
#include <QHash>
#include <QKeySequence>
#include <QObject>

// System-wide hotkeys (Windows RegisterHotKey). Works while the app is minimized / in the tray.
// On other platforms register() simply returns false.
class GlobalHotkeys : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    enum Id { QuickAdd = 1, Palette = 2 };
    explicit GlobalHotkeys(QObject* parent = nullptr);
    ~GlobalHotkeys() override;

    // empty sequence = just unregister. Returns false if the combo is taken / unsupported.
    bool setHotkey(int id, const QKeySequence& seq);
    static bool isSupported();

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

signals:
    void activated(int id);

private:
    void unregister(int id);
    QHash<int, bool> m_registered;
};
