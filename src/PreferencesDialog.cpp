#include "PreferencesDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>

#include "CodiconFont.h"
#include "ContentTheme.h"
#include "WindowsChrome.h"

namespace {

// Build a flat, focus-less toolbutton showing a codicon glyph. Falls back to
// fallbackText if the codicon font failed to load.
QToolButton *makeCodiconButton(char16_t glyph, const QString &fallbackText,
                               const QString &tip, QWidget *parent) {
  auto *b = new QToolButton(parent);
  b->setAutoRaise(true);
  b->setFocusPolicy(Qt::NoFocus);
  b->setToolTip(tip);
  if(const QString family = Codicon::family(); !family.isEmpty()) {
    QFont f(family);
    f.setPixelSize(14);
    b->setFont(f);
    b->setText(QString(QChar(glyph)));
  } else {
    b->setText(fallbackText);
  }
  return b;
}

}  // namespace

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Preferences"));
  resize(440, 340);

  // Native frame on Preferences (no QWindowKit on dialogs). Force native
  // window creation so the DWM dark-titlebar opt-in has an HWND to target;
  // the helper is a no-op on non-Windows builds.
  (void)winId();
  applyWindowsChrome(this);

  auto *root = new QVBoxLayout(this);

  // === Theme ===
  // Combo items show the human-readable name but carry the id (filename stem)
  // as userData so QSettings persists the unambiguous handle. All combos are
  // filled in by reload().
  auto *themeGroup = new QGroupBox(tr("Theme"), this);
  auto *themeOuter = new QVBoxLayout(themeGroup);

  // Follow-system toggle + the global actions (Import / Open) that aren't tied
  // to a single picker. Import/Open are codicon toolbuttons.
  m_followSystem = new QCheckBox(tr("Follow system color scheme"), themeGroup);
  m_importTheme = makeCodiconButton(
      Codicon::NewFile, tr("Import"),
      tr("Import a theme JSON file into your themes folder."), themeGroup);
  m_openThemesFolder = makeCodiconButton(
      Codicon::FolderOpened, tr("Folder"),
      tr("Open your themes folder in the file manager."), themeGroup);
  auto *followRow = new QHBoxLayout;
  followRow->addWidget(m_followSystem);
  followRow->addStretch(1);
  followRow->addWidget(m_importTheme);
  followRow->addWidget(m_openThemesFolder);
  themeOuter->addLayout(followRow);

  // Manual mode (follow off): one picker for any theme, with the per-theme
  // Refresh/Delete actions attached (shown only for user imports).
  m_singleWidget = new QWidget(themeGroup);
  auto *singleForm = new QFormLayout(m_singleWidget);
  singleForm->setContentsMargins(0, 0, 0, 0);
  m_themeCombo = new QComboBox(m_singleWidget);
  m_refreshTheme =
      makeCodiconButton(Codicon::Refresh, tr("Reload"),
                        tr("Reload this theme from disk and re-apply it."),
                        m_singleWidget);
  m_deleteTheme =
      makeCodiconButton(Codicon::Trash, tr("Delete"),
                        tr("Delete this imported theme."), m_singleWidget);
  auto *singleRow = new QHBoxLayout;
  singleRow->addWidget(m_themeCombo, /*stretch=*/1);
  singleRow->addWidget(m_refreshTheme);
  singleRow->addWidget(m_deleteTheme);
  singleForm->addRow(tr("Content theme:"), singleRow);
  themeOuter->addWidget(m_singleWidget);

  // Follow mode (follow on): preferred light + dark themes, each filtered to
  // its type; the OS scheme picks which renders (see ContentTheme::reload).
  m_pairWidget = new QWidget(themeGroup);
  auto *pairForm = new QFormLayout(m_pairWidget);
  pairForm->setContentsMargins(0, 0, 0, 0);
  m_lightCombo = new QComboBox(m_pairWidget);
  m_darkCombo = new QComboBox(m_pairWidget);
  pairForm->addRow(tr("Preferred light:"), m_lightCombo);
  pairForm->addRow(tr("Preferred dark:"), m_darkCombo);
  themeOuter->addWidget(m_pairWidget);

  root->addWidget(themeGroup);

  connect(m_followSystem, &QCheckBox::toggled, this,
          &PreferencesDialog::updateThemeMode);
  connect(m_themeCombo, &QComboBox::currentIndexChanged, this,
          &PreferencesDialog::updateThemeButtons);
  connect(m_refreshTheme, &QToolButton::clicked, this,
          &PreferencesDialog::onRefreshTheme);
  connect(m_importTheme, &QToolButton::clicked, this,
          &PreferencesDialog::onImportTheme);
  connect(m_deleteTheme, &QToolButton::clicked, this,
          &PreferencesDialog::onDeleteTheme);
  connect(m_openThemesFolder, &QToolButton::clicked, this,
          &PreferencesDialog::onOpenThemesFolder);

  // === Fonts ===
  auto *fontsGroup = new QGroupBox(tr("Fonts"), this);
  auto *fontsLayout = new QFormLayout(fontsGroup);

  m_proseFont = new QFontComboBox(fontsGroup);
  m_proseFont->setFontFilters(QFontComboBox::ProportionalFonts);
  m_proseSize = new QSpinBox(fontsGroup);
  m_proseSize->setRange(8, 48);
  m_proseSize->setSuffix(tr(" pt"));
  auto *proseRow = new QHBoxLayout;
  proseRow->addWidget(m_proseFont, /*stretch=*/1);
  proseRow->addWidget(m_proseSize);
  fontsLayout->addRow(tr("Prose:"), proseRow);

  m_monoFont = new QFontComboBox(fontsGroup);
  m_monoFont->setFontFilters(QFontComboBox::MonospacedFonts);
  fontsLayout->addRow(tr("Mono:"), m_monoFont);

  root->addWidget(fontsGroup);

  // === View ===
  auto *viewGroup = new QGroupBox(tr("View"), this);
  auto *viewLayout = new QVBoxLayout(viewGroup);
  m_outlineDefault = new QCheckBox(tr("Show outline by default"), viewGroup);
  m_outlineDefault->setToolTip(
      tr("Newly opened documents start with the outline panel shown. Each "
         "document can still be toggled individually."));
  viewLayout->addWidget(m_outlineDefault);
  root->addWidget(viewGroup);

  root->addStretch();

  // === Buttons ===
  auto *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply |
                           QDialogButtonBox::Cancel);
  root->addWidget(buttons);

  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
          &PreferencesDialog::onApply);
  connect(buttons, &QDialogButtonBox::accepted, this,
          &PreferencesDialog::onAccepted);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  reload();
}

void PreferencesDialog::reload() {
  // Called from the constructor and by MainWindow::onPreferences before
  // each exec(). Idempotent — pulls every widget back to the persisted
  // value so cancelled edits don't leak into the next opening.
  QSettings s;

  m_followSystem->setChecked(
      s.value(QStringLiteral("theme/followSystem"), false).toBool());

  // Repopulate every picker from disk each time so themes imported or deleted
  // in a previous session (or in this one) are reflected, then select the
  // persisted choices.
  populateThemeCombo(
      s.value(QStringLiteral("theme/content"), QStringLiteral("blackboard"))
          .toString());
  populateTypedCombo(
      m_lightCombo, QStringLiteral("light"),
      s.value(QStringLiteral("theme/light"), QStringLiteral("whiteboard"))
          .toString());
  populateTypedCombo(
      m_darkCombo, QStringLiteral("dark"),
      s.value(QStringLiteral("theme/dark"), QStringLiteral("blackboard"))
          .toString());
  updateThemeMode();

  const QString proseFamily = s.value(QStringLiteral("fonts/prose")).toString();
  if(!proseFamily.isEmpty()) m_proseFont->setCurrentFont(QFont(proseFamily));

  m_proseSize->setValue(
      s.value(QStringLiteral("fonts/prose.size"), 14).toInt());

  const QString monoFamily = s.value(QStringLiteral("fonts/mono")).toString();
  if(!monoFamily.isEmpty()) m_monoFont->setCurrentFont(QFont(monoFamily));

  m_outlineDefault->setChecked(
      s.value(QStringLiteral("outline/showByDefault"), false).toBool());
}

void PreferencesDialog::saveThemeSelection() {
  QSettings s;
  s.setValue(QStringLiteral("theme/followSystem"), m_followSystem->isChecked());
  s.setValue(QStringLiteral("theme/content"),
             m_themeCombo->currentData().toString());
  s.setValue(QStringLiteral("theme/light"),
             m_lightCombo->currentData().toString());
  s.setValue(QStringLiteral("theme/dark"),
             m_darkCombo->currentData().toString());
}

void PreferencesDialog::saveToSettings() {
  saveThemeSelection();
  QSettings s;
  s.setValue(QStringLiteral("fonts/prose"),
             m_proseFont->currentFont().family());
  s.setValue(QStringLiteral("fonts/prose.size"), m_proseSize->value());
  s.setValue(QStringLiteral("fonts/mono"), m_monoFont->currentFont().family());
  s.setValue(QStringLiteral("outline/showByDefault"),
             m_outlineDefault->isChecked());
}

void PreferencesDialog::onApply() {
  saveToSettings();
  emit preferencesApplied();
}

void PreferencesDialog::onAccepted() {
  saveToSettings();
  emit preferencesApplied();
  accept();
}

void PreferencesDialog::populateThemeCombo(const QString &selectId) {
  // Block signals so the clear()/addItem() churn doesn't fire
  // updateThemeButtons() mid-rebuild; we call it explicitly at the end.
  const QSignalBlocker blocker(m_themeCombo);
  m_themeCombo->clear();
  for(const QString &id : ContentTheme::availableThemes()) {
    m_themeCombo->addItem(ContentTheme::displayNameFor(id), id);
  }
  // findData == -1 (theme was deleted) leaves the combo on index 0, the
  // first bundled theme — a safe fallback.
  if(const int idx = m_themeCombo->findData(selectId); idx >= 0) {
    m_themeCombo->setCurrentIndex(idx);
  }
  updateThemeButtons();
}

void PreferencesDialog::populateTypedCombo(QComboBox *combo,
                                           const QString &type,
                                           const QString &selectId) {
  const QSignalBlocker blocker(combo);
  combo->clear();
  for(const QString &id : ContentTheme::availableThemes()) {
    if(ContentTheme::typeFor(id) == type) {
      combo->addItem(ContentTheme::displayNameFor(id), id);
    }
  }
  if(const int idx = combo->findData(selectId); idx >= 0) {
    combo->setCurrentIndex(idx);
  }
}

void PreferencesDialog::updateThemeButtons() {
  // Refresh and Delete are user-theme affordances: bundled themes live in the
  // qrc, so they can't be edited in place or removed.
  const QString id = m_themeCombo->currentData().toString();
  const bool userTheme = !id.isEmpty() && !ContentTheme::isBundled(id);
  m_refreshTheme->setVisible(userTheme);
  m_deleteTheme->setVisible(userTheme);
}

void PreferencesDialog::updateThemeMode() {
  const bool follow = m_followSystem->isChecked();
  m_singleWidget->setVisible(!follow);
  m_pairWidget->setVisible(follow);
}

void PreferencesDialog::onImportTheme() {
  const QString src = QFileDialog::getOpenFileName(
      this, tr("Import Theme"), QString(),
      tr("Theme files (*.json *.content.json);;All files (*)"));
  if(src.isEmpty()) return;

  // Importing reuses the source's filename stem as the id; confirm before
  // clobbering an existing user theme of the same name. (Authors iterating
  // on a theme should edit in place + Refresh rather than re-import.)
  const QString id = QFileInfo(src).baseName();
  if(!ContentTheme::isBundled(id) &&
     QFile::exists(ContentTheme::userThemePath(id))) {
    const auto choice = QMessageBox::question(
        this, tr("Import Theme"),
        tr("A theme named \"%1\" is already imported. Replace it?").arg(id));
    if(choice != QMessageBox::Yes) return;
  }

  QString importedId;
  QString error;
  if(!ContentTheme::importThemeFile(src, &importedId, &error)) {
    QMessageBox::warning(this, tr("Import Theme"), error);
    return;
  }
  // Refresh every picker so the new theme lands in the right one(s); show it
  // selected in the manual picker.
  populateThemeCombo(importedId);
  populateTypedCombo(m_lightCombo, QStringLiteral("light"),
                     m_lightCombo->currentData().toString());
  populateTypedCombo(m_darkCombo, QStringLiteral("dark"),
                     m_darkCombo->currentData().toString());
}

void PreferencesDialog::onDeleteTheme() {
  const QString id = m_themeCombo->currentData().toString();
  if(id.isEmpty() || ContentTheme::isBundled(id)) return;  // guard

  const auto choice = QMessageBox::question(
      this, tr("Delete Theme"),
      tr("Delete the imported theme \"%1\"? This removes the file from your "
         "themes folder.")
          .arg(m_themeCombo->currentText()));
  if(choice != QMessageBox::Yes) return;

  if(!ContentTheme::removeUserTheme(id)) {
    QMessageBox::warning(this, tr("Delete Theme"),
                         tr("Could not delete the theme file."));
    return;
  }
  // Fall back to the default selection. If the deleted theme was the
  // persisted choice, ContentTheme::reload() resolves the missing file to
  // the default on next load, so leaving the setting untouched is safe.
  populateThemeCombo(QStringLiteral("blackboard"));
  populateTypedCombo(m_lightCombo, QStringLiteral("light"),
                     m_lightCombo->currentData().toString());
  populateTypedCombo(m_darkCombo, QStringLiteral("dark"),
                     m_darkCombo->currentData().toString());
}

void PreferencesDialog::onRefreshTheme() {
  // Persist ONLY the theme selection (not the font/outline widgets, which
  // would otherwise be committed behind Cancel's back), then re-read it from
  // disk and re-apply: preferencesApplied() drives the reload + re-render.
  saveThemeSelection();
  emit preferencesApplied();
}

void PreferencesDialog::onOpenThemesFolder() {
  const QString dir = ContentTheme::userThemesDir();
  // Create on first use so there's a destination to land in. If that fails
  // (AppDataLocation unwritable), warn rather than open a path that isn't there.
  if(!QDir().mkpath(dir)) {
    QMessageBox::warning(this, tr("Open Themes Folder"),
                         tr("Couldn't create the themes folder:\n%1").arg(dir));
    return;
  }
  QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}
