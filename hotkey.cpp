#include "hotkey.h"
#include <QCoreApplication>
#ifdef Q_OS_WIN
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#  ifndef MOD_NOREPEAT
#    define MOD_NOREPEAT 0x4000
#  endif
#endif

GlobalHotkeys::GlobalHotkeys(QObject* parent) : QObject(parent) {
    QCoreApplication::instance()->installNativeEventFilter(this);
}

GlobalHotkeys::~GlobalHotkeys() {
    const auto ids = m_registered.keys();
    for (int id : ids) unregister(id);
    if (QCoreApplication::instance()) QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool GlobalHotkeys::isSupported() {
#ifdef Q_OS_WIN
    return true;
#else
    return false;
#endif
}

#ifdef Q_OS_WIN
static UINT toVk(int key) {
    if (key >= Qt::Key_A && key <= Qt::Key_Z) return UINT('A' + (key - Qt::Key_A));
    if (key >= Qt::Key_0 && key <= Qt::Key_9) return UINT('0' + (key - Qt::Key_0));
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) return UINT(VK_F1 + (key - Qt::Key_F1));
    switch (key) {
    case Qt::Key_Space: return VK_SPACE;
    case Qt::Key_Insert: return VK_INSERT;
    case Qt::Key_Home: return VK_HOME;
    case Qt::Key_End: return VK_END;
    case Qt::Key_PageUp: return VK_PRIOR;
    case Qt::Key_PageDown: return VK_NEXT;
    case Qt::Key_Pause: return VK_PAUSE;
    case Qt::Key_Comma: return VK_OEM_COMMA;
    case Qt::Key_Period: return VK_OEM_PERIOD;
    case Qt::Key_Slash: return VK_OEM_2;
    case Qt::Key_Semicolon: return VK_OEM_1;
    case Qt::Key_BracketLeft: return VK_OEM_4;
    case Qt::Key_BracketRight: return VK_OEM_6;
    case Qt::Key_Minus: return VK_OEM_MINUS;
    case Qt::Key_Equal: return VK_OEM_PLUS;
    default: return 0;
    }
}
#endif

void GlobalHotkeys::unregister(int id) {
#ifdef Q_OS_WIN
    if (m_registered.value(id)) UnregisterHotKey(nullptr, id);
#endif
    m_registered.remove(id);
}

bool GlobalHotkeys::setHotkey(int id, const QKeySequence& seq) {
    unregister(id);
    if (seq.isEmpty()) return true;
#ifdef Q_OS_WIN
    const QKeyCombination combo = seq[0];
    const Qt::KeyboardModifiers m = combo.keyboardModifiers();
    UINT mods = MOD_NOREPEAT;
    if (m & Qt::ControlModifier) mods |= MOD_CONTROL;
    if (m & Qt::AltModifier) mods |= MOD_ALT;
    if (m & Qt::ShiftModifier) mods |= MOD_SHIFT;
    if (m & Qt::MetaModifier) mods |= MOD_WIN;
    const UINT vk = toVk(int(combo.key()));
    if (!vk || mods == MOD_NOREPEAT) return false;        // need at least one modifier
    if (!RegisterHotKey(nullptr, id, mods, vk)) return false;
    m_registered.insert(id, true);
    return true;
#else
    Q_UNUSED(id);
    return false;
#endif
}

bool GlobalHotkeys::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    Q_UNUSED(result);
#ifdef Q_OS_WIN
    if (eventType == "windows_generic_MSG" || eventType == "windows_dispatcher_MSG") {
        const MSG* msg = static_cast<const MSG*>(message);
        if (msg->message == WM_HOTKEY && m_registered.contains(int(msg->wParam))) {
            emit activated(int(msg->wParam));
            return true;
        }
    }
#else
    Q_UNUSED(eventType); Q_UNUSED(message);
#endif
    return false;
}
