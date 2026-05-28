#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QFontComboBox;
class QSpinBox;
class QToolButton;

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
  void onImportTheme();
  void onDeleteTheme();
  void onRefreshTheme();
  void onOpenThemesFolder();

private:
  void saveToSettings();

  // Persist just the theme selection keys (followSystem + the three theme
  // ids). Shared by saveToSettings() and onRefreshTheme() — the latter must
  // NOT commit the font/outline widgets, or Refresh would bypass Cancel.
  void saveThemeSelection();

  // Clear and refill the manual theme combo from ContentTheme::
  // availableThemes() (all themes), selecting <selectId> (or the first entry
  // if it's gone). Signals are blocked while repopulating; ends by calling
  // updateThemeButtons().
  void populateThemeCombo(const QString &selectId);

  // Clear and refill a preferred-light/dark combo with only the themes whose
  // ContentTheme::typeFor() matches <type>, selecting <selectId> if present.
  void populateTypedCombo(QComboBox *combo, const QString &type,
                          const QString &selectId);

  // Show the Refresh/Delete buttons only when the manual combo's selection is
  // a user import (bundled themes are read-only).
  void updateThemeButtons();

  // Toggle between the manual single-picker and the light/dark pair pickers
  // based on the "follow system" checkbox.
  void updateThemeMode();

  QCheckBox *m_followSystem = nullptr;
  QWidget *m_singleWidget = nullptr;
  QWidget *m_pairWidget = nullptr;
  QComboBox *m_themeCombo = nullptr;
  QComboBox *m_lightCombo = nullptr;
  QComboBox *m_darkCombo = nullptr;
  QToolButton *m_refreshTheme = nullptr;
  QToolButton *m_importTheme = nullptr;
  QToolButton *m_deleteTheme = nullptr;
  QToolButton *m_openThemesFolder = nullptr;
  QFontComboBox *m_proseFont = nullptr;
  QSpinBox *m_proseSize = nullptr;
  QFontComboBox *m_monoFont = nullptr;
  QCheckBox *m_outlineDefault = nullptr;
};
