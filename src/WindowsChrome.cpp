#include "WindowsChrome.h"

#include <QWidget>

#ifdef Q_OS_WIN
#include <cwchar>

#include <QAbstractNativeEventFilter>
#include <QApplication>
#include <QGuiApplication>
#include <QStyleHints>
#include <QTimer>
// clang-format off
// windows.h must precede dwmapi.h (and other Windows SDK headers) — they use
// types it defines. Fenced so the include sorter can't alphabetize them back
// into a broken order.
#include <windows.h>
#include <dwmapi.h>
// clang-format on
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
// Accent/colorization change broadcast. Defined in winuser.h on current SDKs;
// kept as a fallback for older MinGW headers that predate it.
#ifndef WM_DWMCOLORIZATIONCOLORCHANGED
#define WM_DWMCOLORIZATIONCOLORCHANGED 0x0320
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

// Re-send ApplicationPaletteChange to every widget so palette-derived
// stylesheets re-read QGuiApplication::palette() (which Qt has already updated
// from the system). Qt normally does this itself, but a QWindowKit frameless
// window can swallow the propagation to its children — this heals that gap.
void broadcastPaletteRefresh() {
  QEvent ev(QEvent::ApplicationPaletteChange);
  const auto widgets = QApplication::allWidgets();
  for(QWidget *w : widgets) QApplication::sendEvent(w, &ev);
}

// Watches the system color-scheme / accent change messages. WM_SETTINGCHANGE
// with "ImmersiveColorSet" covers light/dark (and accent on some builds);
// WM_DWMCOLORIZATIONCOLORCHANGED is the dedicated accent/colorization message.
// Both are broadcast to top-level windows, so the app window's filter sees them
// even when QWindowKit owns the frame.
class ColorSettingsFilter : public QAbstractNativeEventFilter {
public:
  bool nativeEventFilter(const QByteArray &type, void *message,
                         qintptr * /*result*/) override {
    if(type != "windows_generic_MSG" && type != "windows_dispatcher_MSG")
      return false;
    const auto *msg = static_cast<const MSG *>(message);

    const bool accent = msg->message == WM_DWMCOLORIZATIONCOLORCHANGED;
    const bool settings =
        msg->message == WM_SETTINGCHANGE && msg->lParam != 0 &&
        std::wcscmp(reinterpret_cast<const wchar_t *>(msg->lParam),
                    L"ImmersiveColorSet") == 0;

    if(accent || settings) {
      // Defer: let Qt's own handler update QGuiApplication::palette() on this
      // same message first, then re-broadcast so the chrome reads fresh colors.
      QTimer::singleShot(0, qApp, [] {
        broadcastPaletteRefresh();
      });
    }
    return false;  // never consume — Qt and QWindowKit still need the message
  }
};
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
                   &QStyleHints::colorSchemeChanged, w, [w](Qt::ColorScheme) {
                     writeDwmAttrs(w);
                   });
#else
  (void)w;
#endif
}

void installChromeThemeTracker() {
#ifdef Q_OS_WIN
  // Lives for the process lifetime; qApp outlives any single window, so the
  // filter is safe to leave installed until shutdown. Guard against a second
  // call (a future refactor calling this from two places): Qt doesn't
  // deduplicate native event filters, so installing twice would fire
  // broadcastPaletteRefresh() twice per color-change message.
  static bool installed = false;
  if(installed) return;
  installed = true;
  static ColorSettingsFilter filter;
  qApp->installNativeEventFilter(&filter);
#endif
}
