#pragma once

#include <QtGlobal>

namespace Platform {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
inline constexpr bool mobile = true;
#else
inline constexpr bool mobile = false;
#endif

#ifdef Q_OS_WIN
inline constexpr bool customFrame = true;
#else
inline constexpr bool customFrame = false;
#endif
}
