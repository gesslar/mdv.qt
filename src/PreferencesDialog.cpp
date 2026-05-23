#include "PreferencesDialog.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFontComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSettings>
#include <QSpinBox>
#include <QVBoxLayout>

#include "ContentTheme.h"

PreferencesDialog::PreferencesDialog(QWidget *parent) : QDialog(parent) {
  setWindowTitle(tr("Preferences"));
  resize(440, 280);

  auto *root = new QVBoxLayout(this);

  // === Theme ===
  auto *themeGroup = new QGroupBox(tr("Theme"), this);
  auto *themeLayout = new QFormLayout(themeGroup);
  m_themeCombo = new QComboBox(themeGroup);
  // Show the human-readable theme name, but store the id (filename
  // stem) as userData so QSettings persists the unambiguous handle.
  for (const QString &id : ContentTheme::availableThemes()) {
    m_themeCombo->addItem(ContentTheme::displayNameForBundled(id), id);
  }
  themeLayout->addRow(tr("Content theme:"), m_themeCombo);
  root->addWidget(themeGroup);

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

  // Note: mono size is intentionally absent — see header comment.
  auto *hint = new QLabel(
      tr("<small>Mono size tracks prose size (0.92em). Ctrl+wheel zooms "
         "both together.</small>"),
      fontsGroup);
  hint->setWordWrap(true);
  fontsLayout->addRow(QString(), hint);

  root->addWidget(fontsGroup);
  root->addStretch();

  // === Buttons ===
  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok |
                                       QDialogButtonBox::Apply |
                                       QDialogButtonBox::Cancel);
  root->addWidget(buttons);

  connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked,
          this, &PreferencesDialog::onApply);
  connect(buttons, &QDialogButtonBox::accepted, this,
          &PreferencesDialog::onAccepted);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

  loadFromSettings();
}

void PreferencesDialog::loadFromSettings() {
  QSettings s;

  const QString theme =
      s.value(QStringLiteral("theme/content"), QStringLiteral("blackboard"))
          .toString();
  if (const int idx = m_themeCombo->findData(theme); idx >= 0) {
    m_themeCombo->setCurrentIndex(idx);
  }

  const QString proseFamily = s.value(QStringLiteral("fonts/prose")).toString();
  if (!proseFamily.isEmpty()) m_proseFont->setCurrentFont(QFont(proseFamily));

  m_proseSize->setValue(
      s.value(QStringLiteral("fonts/prose.size"), 14).toInt());

  const QString monoFamily = s.value(QStringLiteral("fonts/mono")).toString();
  if (!monoFamily.isEmpty()) m_monoFont->setCurrentFont(QFont(monoFamily));
}

void PreferencesDialog::saveToSettings() {
  QSettings s;
  s.setValue(QStringLiteral("theme/content"),
             m_themeCombo->currentData().toString());
  s.setValue(QStringLiteral("fonts/prose"),
             m_proseFont->currentFont().family());
  s.setValue(QStringLiteral("fonts/prose.size"), m_proseSize->value());
  s.setValue(QStringLiteral("fonts/mono"),
             m_monoFont->currentFont().family());
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
