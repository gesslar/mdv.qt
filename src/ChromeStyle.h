#pragma once

#include <QColor>
#include <QString>

// Helpers for building palette-derived stylesheets for the window chrome
// (title bar, outline panel, status bar). The chrome reads its colors from
// the live QPalette instead of hard-coded hex so it tracks the system theme
// and accent — and shifts with them at runtime on an ApplicationPaletteChange.

// Format a QColor as a Qt-stylesheet rgba() string with an explicit alpha
// (0.0-1.0). Used for translucent hover/selection washes layered over a
// palette color so they read on both light and dark schemes.
inline QString cssRgba(const QColor &c, qreal alpha) {
  return QStringLiteral("rgba(%1,%2,%3,%4)")
      .arg(c.red())
      .arg(c.green())
      .arg(c.blue())
      .arg(alpha, 0, 'f', 3);
}
