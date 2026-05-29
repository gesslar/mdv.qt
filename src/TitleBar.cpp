#include "TitleBar.h"

#include <QApplication>
#include <QEvent>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPalette>
#include <QStyle>
#include <QStyleHints>
#include <QTimer>
#include <QToolButton>

#include "ChromeStyle.h"
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
            // On Windows colorSchemeChanged fires *before* Qt swaps the
            // palette, so refreshing inline would re-read the old (light)
            // colors. Defer to the next event-loop turn, by which point the
            // palette swap has landed and the read is fresh.
            QTimer::singleShot(0, this, [this] {
              refreshTheme();
            });
          });
}

void TitleBar::refreshTheme() {
  // Colors come from the *application* palette, not this widget's palette():
  // setStyleSheet() below folds its colors back into the widget palette, so
  // reading our own palette would feed on the stylesheet we just set. The app
  // palette is the clean system source — Window/WindowText for the surface,
  // hover/press as translucent WindowText washes that read on any scheme. Close
  // button hover stays the canonical Win11 red regardless of scheme; the
  // menu-indicator chevron is suppressed so the buttons read as labels.
  const QPalette pal = QGuiApplication::palette();
  const QString bg = pal.color(QPalette::Window).name();
  const QString fg = pal.color(QPalette::WindowText).name();
  const QColor txt = pal.color(QPalette::WindowText);
  const QString hover = cssRgba(txt, 0.10);
  const QString press = cssRgba(txt, 0.16);

  const QString sheet = QStringLiteral(R"(
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
                          .arg(bg, fg, hover, press);
  // Re-applying an identical sheet re-polishes and re-emits PaletteChange,
  // which lands back in changeEvent() — skip the no-op to break that loop.
  if(sheet != styleSheet()) setStyleSheet(sheet);
}

void TitleBar::changeEvent(QEvent *e) {
  QWidget::changeEvent(e);
  // The explicit stylesheet would otherwise stay frozen at its old colors.
  // ApplicationPaletteChange is what Linux delivers when the app palette swaps;
  // Windows delivers PaletteChange to the widget instead. Both arrive *after*
  // the palette is updated, so refreshTheme() reads fresh colors either way.
  if(e->type() == QEvent::ApplicationPaletteChange ||
     e->type() == QEvent::PaletteChange)
    refreshTheme();
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
