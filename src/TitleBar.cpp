#include "TitleBar.h"

#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QStyle>
#include <QStyleHints>
#include <QToolButton>

#include "CodiconFont.h"

namespace {
// Standard Win11 titlebar metrics: 32px tall, system buttons ~46px wide.
constexpr int kTitleBarHeight = 32;
constexpr int kSystemButtonWidth = 46;
// Pixel size of the Codicon glyphs in chrome buttons. Roughly matches
// Win11's own glyph metric so visual weight reads as native.
constexpr int kCodiconPixelSize = 12;

QToolButton *makeChromeButton(const QString &objectName, char16_t glyph,
                              QWidget *parent) {
  auto *b = new QToolButton(parent);
  b->setObjectName(objectName);
  b->setFocusPolicy(Qt::NoFocus);
  b->setFixedSize(kSystemButtonWidth, kTitleBarHeight);
  if(const QString family = Codicon::family(); !family.isEmpty()) {
    QFont f(family);
    f.setPixelSize(kCodiconPixelSize);
    b->setFont(f);
    b->setText(QString(QChar(glyph)));
  }
  return b;
}
}  // namespace

TitleBar::TitleBar(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("TitleBar"));
  setFixedHeight(kTitleBarHeight);
  // QSS background colors only apply on a QWidget subclass when this is set;
  // without it, the system palette paints over our rules.
  setAttribute(Qt::WA_StyledBackground, true);

  m_iconLabel = new QLabel(this);
  m_iconLabel->setFixedSize(kTitleBarHeight, kTitleBarHeight);
  m_iconLabel->setAlignment(Qt::AlignCenter);
  m_iconLabel->setPixmap(
      QIcon(QStringLiteral(":/icons/mdv.png")).pixmap(QSize(16, 16)));

  // Menu buttons use InstantPopup so a single click drops the menu (the
  // dropdown arrow is hidden via the stylesheet — the menu reads as a normal
  // button label).
  m_fileBtn = new QToolButton(this);
  m_fileBtn->setText(tr("File"));
  m_fileBtn->setPopupMode(QToolButton::InstantPopup);
  m_fileBtn->setFocusPolicy(Qt::NoFocus);
  m_fileBtn->setFixedHeight(kTitleBarHeight);

  m_viewBtn = new QToolButton(this);
  m_viewBtn->setText(tr("View"));
  m_viewBtn->setPopupMode(QToolButton::InstantPopup);
  m_viewBtn->setFocusPolicy(Qt::NoFocus);
  m_viewBtn->setFixedHeight(kTitleBarHeight);

  m_settingsBtn =
      makeChromeButton(QStringLiteral("sysSettings"), Codicon::Settings, this);
  m_settingsBtn->setToolTip(tr("Preferences"));
  connect(m_settingsBtn, &QToolButton::clicked, this,
          &TitleBar::settingsClicked);

  m_minBtn =
      makeChromeButton(QStringLiteral("sysMin"), Codicon::ChromeMinimize, this);
  m_maxBtn =
      makeChromeButton(QStringLiteral("sysMax"), Codicon::ChromeMaximize, this);
  m_closeBtn =
      makeChromeButton(QStringLiteral("sysClose"), Codicon::ChromeClose, this);

  connect(m_minBtn, &QToolButton::clicked, this, &TitleBar::minimizeClicked);
  connect(m_maxBtn, &QToolButton::clicked, this, &TitleBar::maximizeClicked);
  connect(m_closeBtn, &QToolButton::clicked, this, &TitleBar::closeClicked);

  auto *layout = new QHBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(m_iconLabel);
  layout->addWidget(m_fileBtn);
  layout->addWidget(m_viewBtn);
  layout->addStretch(1);
  layout->addWidget(m_settingsBtn);
  layout->addWidget(m_minBtn);
  layout->addWidget(m_maxBtn);
  layout->addWidget(m_closeBtn);

  refreshTheme();
  connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
          [this](Qt::ColorScheme) {
            refreshTheme();
          });
}

void TitleBar::refreshTheme() {
  // Title bar tracks the active Qt color scheme so it doesn't fight the rest
  // of the chrome on a system-theme switch. Close button hover stays the
  // canonical Win11 red regardless of scheme; the menu-indicator chevron is
  // suppressed in both — the buttons read as labels, not dropdowns.
  const bool dark =
      QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
  const QString bg =
      dark ? QStringLiteral("#1f1f1f") : QStringLiteral("#f3f3f3");
  const QString fg =
      dark ? QStringLiteral("#f0f0f0") : QStringLiteral("#000000");
  const QString hover = dark ? QStringLiteral("rgba(255,255,255,0.08)")
                             : QStringLiteral("rgba(0,0,0,0.06)");
  const QString press = dark ? QStringLiteral("rgba(255,255,255,0.04)")
                             : QStringLiteral("rgba(0,0,0,0.04)");

  setStyleSheet(QStringLiteral(R"(
TitleBar { background: %1; }
TitleBar QToolButton {
    background: transparent;
    border: none;
    color: %2;
    padding: 0 10px;
}
TitleBar QToolButton:hover { background: %3; }
TitleBar QToolButton:pressed { background: %4; }
TitleBar QToolButton#sysClose:hover { background: #c42b1c; color: white; }
TitleBar QToolButton#sysClose:pressed { background: #b4271a; color: white; }
TitleBar QToolButton::menu-indicator { image: none; }
)")
                    .arg(bg, fg, hover, press));
}

void TitleBar::setFileMenu(QMenu *menu) { m_fileBtn->setMenu(menu); }
void TitleBar::setViewMenu(QMenu *menu) { m_viewBtn->setMenu(menu); }

void TitleBar::showEvent(QShowEvent *e) {
  QWidget::showEvent(e);
  // Watch the top-level window for WindowStateChange so the max/restore icon
  // tracks the actual state, including state changes driven by Win+arrow snap
  // or the snap-layout popout.
  if(auto *w = window(); w && w != this) {
    w->installEventFilter(this);
    refreshMaxIcon();
  }
}

bool TitleBar::eventFilter(QObject *obj, QEvent *e) {
  if(obj == window() && e->type() == QEvent::WindowStateChange) {
    refreshMaxIcon();
  }
  return QWidget::eventFilter(obj, e);
}

void TitleBar::refreshMaxIcon() {
  const bool maximized = window() && window()->isMaximized();
  m_maxBtn->setText(QString(
      QChar(maximized ? Codicon::ChromeRestore : Codicon::ChromeMaximize)));
}
