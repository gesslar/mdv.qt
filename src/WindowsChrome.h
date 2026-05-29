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

// Install an application-wide tracker that re-broadcasts a palette refresh to
// every widget whenever Windows changes its color scheme or accent color.
//
// Why this is needed: Qt keeps QGuiApplication::palette() in sync with the
// system accent/scheme, but inside a QWindowKit frameless window the follow-up
// ApplicationPaletteChange doesn't reliably reach the child widgets — so the
// chrome's palette-derived stylesheets freeze at their load-time colors (most
// visibly, accent changes do nothing). The accent has no QStyleHints signal at
// all, so colorSchemeChanged can't cover it. This watches the native
// WM_SETTINGCHANGE / WM_DWMCOLORIZATIONCOLORCHANGED messages and, once Qt has
// updated its palette, re-sends ApplicationPaletteChange so the chrome re-reads
// the fresh colors. Call once, after QApplication is constructed.
//
// No-op on non-Windows builds (those platforms deliver the event normally).
void installChromeThemeTracker();
