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
#include <QSizePolicy>
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

QHBoxLayout *PreferencesDialog::makeThemeRow(QComboBox *combo,
                                             QToolButton *&refresh,
                                             QToolButton *&del) {
  QWidget *parent = combo->parentWidget();
  refresh = makeCodiconButton(Codicon::Refresh, tr("Reload"),
                              tr("Reload this theme from disk and re-apply it."),
                              parent);
  del = makeCodiconButton(Codicon::Trash, tr("Delete"),
                          tr("Delete this imported theme."), parent);
  // Keep the buttons' footprint even while hidden so every combo stays the
  // same width and the rows line up, bundled or not.
  for(QToolButton *b : {refresh, del}) {
    QSizePolicy sp = b->sizePolicy();
    sp.setRetainSizeWhenHidden(true);
    b->setSizePolicy(sp);
  }
  auto *row = new QHBoxLayout;
  row->addWidget(combo, /*stretch=*/1);
  row->addWidget(refresh);
  row->addWidget(del);
  return row;
}

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
  auto *singleRow = makeThemeRow(m_themeCombo, m_refreshTheme, m_deleteTheme);
  singleForm->addRow(tr("Content theme:"), singleRow);
  themeOuter->addWidget(m_singleWidget);

  // Follow mode (follow on): preferred light + dark themes, each filtered to
  // its type; the OS scheme picks which renders (see ContentTheme::reload).
  m_pairWidget = new QWidget(themeGroup);
  auto *pairForm = new QFormLayout(m_pairWidget);
  pairForm->setContentsMargins(0, 0, 0, 0);
  m_lightCombo = new QComboBox(m_pairWidget);
  m_darkCombo = new QComboBox(m_pairWidget);
  auto *lightRow = makeThemeRow(m_lightCombo, m_lightRefresh, m_lightDelete);
  auto *darkRow = makeThemeRow(m_darkCombo, m_darkRefresh, m_darkDelete);
  pairForm->addRow(tr("Preferred light:"), lightRow);
  pairForm->addRow(tr("Preferred dark:"), darkRow);
  themeOuter->addWidget(m_pairWidget);

  root->addWidget(themeGroup);

  connect(m_followSystem, &QCheckBox::toggled, this,
          &PreferencesDialog::updateThemeMode);
  connect(m_themeCombo, &QComboBox::currentIndexChanged, this,
          &PreferencesDialog::updateThemeButtons);
  connect(m_lightCombo, &QComboBox::currentIndexChanged, this,
          &PreferencesDialog::updateThemeButtons);
  connect(m_darkCombo, &QComboBox::currentIndexChanged, this,
          &PreferencesDialog::updateThemeButtons);
  connect(m_refreshTheme, &QToolButton::clicked, this, [this] {
    refreshThemeFromCombo(m_themeCombo, QStringLiteral("theme/content"));
  });
  connect(m_importTheme, &QToolButton::clicked, this,
          &PreferencesDialog::onImportTheme);
  connect(m_deleteTheme, &QToolButton::clicked, this,
          &PreferencesDialog::onDeleteTheme);
  connect(m_openThemesFolder, &QToolButton::clicked, this,
          &PreferencesDialog::onOpenThemesFolder);
  // Reload and Delete are each scoped to their own combo's selection, so
  // neither commits a sibling picker's in-flight change behind Cancel's back.
  connect(m_lightRefresh, &QToolButton::clicked, this, [this] {
    refreshThemeFromCombo(m_lightCombo, QStringLiteral("theme/light"));
  });
  connect(m_darkRefresh, &QToolButton::clicked, this, [this] {
    refreshThemeFromCombo(m_darkCombo, QStringLiteral("theme/dark"));
  });
  connect(m_lightDelete, &QToolButton::clicked, this,
          [this] { deleteThemeFromCombo(m_lightCombo); });
  connect(m_darkDelete, &QToolButton::clicked, this,
          [this] { deleteThemeFromCombo(m_darkCombo); });

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
  updateThemeButtons();
}

void PreferencesDialog::updateThemeButtons() {
  // Refresh and Delete are user-theme affordances: bundled themes live in the
  // qrc, so they can't be edited in place or removed. Each combo's pair shows
  // independently, keyed on that combo's own selection.
  const auto apply = [](QComboBox *combo, QToolButton *refresh,
                        QToolButton *del) {
    const QString id = combo->currentData().toString();
    const bool userTheme = !id.isEmpty() && !ContentTheme::isBundled(id);
    refresh->setVisible(userTheme);
    del->setVisible(userTheme);
  };
  apply(m_themeCombo, m_refreshTheme, m_deleteTheme);
  apply(m_lightCombo, m_lightRefresh, m_lightDelete);
  apply(m_darkCombo, m_darkRefresh, m_darkDelete);
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

void PreferencesDialog::onDeleteTheme() { deleteThemeFromCombo(m_themeCombo); }

void PreferencesDialog::deleteThemeFromCombo(QComboBox *combo) {
  const QString id = combo->currentData().toString();
  if(id.isEmpty() || ContentTheme::isBundled(id)) return;  // guard

  const auto choice = QMessageBox::question(
      this, tr("Delete Theme"),
      tr("Delete the imported theme \"%1\"? This removes the file from your "
         "themes folder.")
          .arg(combo->currentText()));
  if(choice != QMessageBox::Yes) return;

  if(!ContentTheme::removeUserTheme(id)) {
    QMessageBox::warning(this, tr("Delete Theme"),
                         tr("Could not delete the theme file."));
    return;
  }
  // Repopulate every picker, preserving each one's current selection. Wherever
  // the deleted id was selected, findData == -1 falls back to the first entry
  // (a bundled theme) — and if it was the persisted choice, ContentTheme::
  // reload() resolves the missing file to the default on next load anyway.
  populateThemeCombo(m_themeCombo->currentData().toString());
  populateTypedCombo(m_lightCombo, QStringLiteral("light"),
                     m_lightCombo->currentData().toString());
  populateTypedCombo(m_darkCombo, QStringLiteral("dark"),
                     m_darkCombo->currentData().toString());
}

void PreferencesDialog::refreshThemeFromCombo(QComboBox *combo,
                                              const QString &settingsKey) {
  // Reload one theme from disk and re-apply it. Persist ONLY this combo's own
  // key plus followSystem (which decides whether the renderer reads
  // theme/content or theme/light + theme/dark, and so which picker is even
  // live). Crucially we do NOT call saveThemeSelection(): writing the sibling
  // combos would commit their in-flight edits — and the font/outline widgets
  // are left untouched too — so Cancel still discards everything else.
  // preferencesApplied() drives the reload + re-render.
  QSettings s;
  s.setValue(QStringLiteral("theme/followSystem"), m_followSystem->isChecked());
  s.setValue(settingsKey, combo->currentData().toString());
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
