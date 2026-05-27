#include "WindowsChrome.h"

#include <QWidget>

#ifdef Q_OS_WIN
  #include <QGuiApplication>
  #include <QStyleHints>
  #include <windows.h>
  #include <dwmapi.h>
  // Fallbacks for older MinGW SDKs that predate these names. Placed AFTER
  // the SDK headers so that when a future SDK ships the names — as a #define
  // or, more likely, as DWMWINDOWATTRIBUTE / DWM_SYSTEMBACKDROP_TYPE enum
  // entries — the SDK version wins. (A #define ahead of the SDK header would
  // textually replace the enum entry's identifier and break the enum.)
  // Values match the documented Microsoft DWM constants.
  #ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
    #define DWMWA_USE_IMMERSIVE_DARK_MODE 20
  #endif
  #ifndef DWMWA_SYSTEMBACKDROP_TYPE
    #define DWMWA_SYSTEMBACKDROP_TYPE 38
  #endif
  #ifndef DWMSBT_MAINWINDOW
    #define DWMSBT_MAINWINDOW 2
  #endif

namespace {
void writeDwmAttrs(QWidget *w) {
  const HWND hwnd = reinterpret_cast<HWND>(w->winId());
  if(!hwnd) return;

  const BOOL dark =
      QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
          ? TRUE
          : FALSE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                        sizeof(dark));

  // DWMSBT_MAINWINDOW (Mica). Pre-22H2 Win11 / Win10 return failure and the
  // frame keeps its solid background.
  const int backdrop = DWMSBT_MAINWINDOW;
  DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop,
                        sizeof(backdrop));
}
}  // namespace
#endif

void applyWindowsChrome(QWidget *w) {
#ifdef Q_OS_WIN
  writeDwmAttrs(w);
  // Re-apply on system theme switches so the frame tracks live. `w` is the
  // connection context — destroying the widget auto-disconnects, and the
  // lambda doesn't re-enter applyWindowsChrome so a single call wires the
  // tracker exactly once.
  QObject::connect(QGuiApplication::styleHints(),
                   &QStyleHints::colorSchemeChanged, w,
                   [w](Qt::ColorScheme) { writeDwmAttrs(w); });
#else
  (void)w;
#endif
}
