#ifdef _WIN32
#include <windows.h>
#include <dwmapi.h>
#endif

#include "PlatformWindow.h"

#include <QWindow>

void PlatformWindow::applyTheme(QObject* window, bool dark)
{
#ifdef _WIN32
    if (auto* w = qobject_cast<QWindow*>(window))
    {
        BOOL useDark = dark ? TRUE : FALSE;
        DwmSetWindowAttribute(reinterpret_cast<HWND>(w->winId()),
                              20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &useDark, sizeof(useDark));
    }
#else
    Q_UNUSED(window);
    Q_UNUSED(dark);
#endif
}
