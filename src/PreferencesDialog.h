#pragma once

#include <QDialog>

class QComboBox;
class QFontComboBox;
class QSpinBox;

// Modal dialog accessed via File → Preferences (Ctrl+,). Owns the user
// choices that are not "structural": the active content theme, the
// prose / mono font families, and the prose font size.
//
// Mono size is intentionally not exposed: the stylesheet sets it
// relatively (0.92em) so that Ctrl+wheel zoom scales prose and code
// together. If a future toolbar gets explicit size presets, they can
// scale the same single "prose size" setting.
class PreferencesDialog : public QDialog {
  Q_OBJECT

public:
  explicit PreferencesDialog(QWidget *parent = nullptr);

  // Re-populate the widgets from QSettings. Called by MainWindow before
  // each exec() so the dialog always opens reflecting the persisted
  // state — Cancel discards in-flight edits, no stale values survive
  // into the next opening.
  void reload();

signals:
  // Emitted after the user clicks Apply or OK and the new values have
  // been persisted to QSettings. MainWindow listens to this and
  // refreshes every open DocumentView.
  void preferencesApplied();

private slots:
  void onApply();
  void onAccepted();

private:
  void saveToSettings();

  QComboBox *m_themeCombo = nullptr;
  QFontComboBox *m_proseFont = nullptr;
  QSpinBox *m_proseSize = nullptr;
  QFontComboBox *m_monoFont = nullptr;
};
