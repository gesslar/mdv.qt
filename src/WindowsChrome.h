#pragma once

class QWidget;

// Opt a native-frame Windows window into the modern Win10/11 chrome:
//   - DWMWA_USE_IMMERSIVE_DARK_MODE  (titlebar follows Qt color scheme)
//   - DWMWA_SYSTEMBACKDROP_TYPE      (Mica on Win11 22H2+)
//
// Applies the attributes immediately and also wires the widget to
// QStyleHints::colorSchemeChanged so the frame tracks live theme switches
// for the rest of the widget's lifetime. Call once per widget.
//
// Used for dialogs that keep native frames (Preferences, file pickers via
// our own re-parented dialogs). The main window uses QWindowKit's custom
// titlebar and doesn't need this.
//
// No-op on non-Windows builds. On Win10 < 2004 / pre-22H2 Win11 the
// underlying DWM calls return failure and the frame keeps its prior
// appearance — no fallback handling needed.
//
// Caller's responsibility: ensure the widget has a native HWND
// (e.g. (void)w->winId()) before calling.
void applyWindowsChrome(QWidget *w);
