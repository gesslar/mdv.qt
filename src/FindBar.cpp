#include "FindBar.h"

#include <QEvent>
#include <QFont>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QSettings>
#include <QStyle>
#include <QToolButton>

#include "CodiconFont.h"

namespace {

// Flat, focus-less codicon toolbutton (shared shape with the rest of the app).
// Falls back to fallbackText if the codicon font failed to load.
QToolButton *codiconButton(char16_t glyph, const QString &fallbackText,
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

FindBar::FindBar(QWidget *parent) : QFrame(parent) {
  setObjectName(QStringLiteral("findBar"));
  // Floating overlay: stay on top of the content and size to its contents so
  // the owner can park it in a corner.
  setFrameShape(QFrame::StyledPanel);
  setAutoFillBackground(true);

  auto *row = new QHBoxLayout(this);
  row->setContentsMargins(6, 4, 6, 4);
  row->setSpacing(4);

  // The bordered input box: a frameless field plus the in-field toggles, all
  // sharing one border the way VS Code's search box does.
  m_inputFrame = new QFrame(this);
  m_inputFrame->setObjectName(QStringLiteral("findInputFrame"));
  auto *inputRow = new QHBoxLayout(m_inputFrame);
  inputRow->setContentsMargins(6, 1, 3, 1);
  inputRow->setSpacing(1);

  m_input = new QLineEdit(m_inputFrame);
  m_input->setObjectName(QStringLiteral("findInput"));
  m_input->setFrame(false);  // the frame lives on m_inputFrame, not the field
  m_input->setPlaceholderText(tr("Find"));
  m_input->setMinimumWidth(200);
  m_input->installEventFilter(this);
  inputRow->addWidget(m_input);

  // In-field toggles, mirroring VS Code's order: Match Case, Whole Word, Regex.
  m_case = makeToggle(Codicon::CaseSensitive, tr("Aa"), tr("Match Case"),
                      QStringLiteral("find/caseSensitive"));
  m_word = makeToggle(Codicon::WholeWord, tr("\\b"), tr("Match Whole Word"),
                      QStringLiteral("find/wholeWord"));
  m_regex = makeToggle(Codicon::Regex, tr(".*"), tr("Use Regular Expression"),
                       QStringLiteral("find/regex"));
  inputRow->addWidget(m_case);
  inputRow->addWidget(m_word);
  inputRow->addWidget(m_regex);
  row->addWidget(m_inputFrame);

  m_count = new QLabel(this);
  m_count->setObjectName(QStringLiteral("findCount"));
  // Fixed width keeps the nav buttons from shifting as the readout changes
  // between "No results" and "<n> of <m>"; wide enough to hold either.
  m_count->setMinimumWidth(88);
  m_count->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
  row->addWidget(m_count);

  m_prev = codiconButton(Codicon::ArrowUp, tr("Prev"),
                         tr("Previous Match (Shift+Enter)"), this);
  m_next = codiconButton(Codicon::ArrowDown, tr("Next"),
                         tr("Next Match (Enter)"), this);
  m_close =
      codiconButton(Codicon::Close, tr("X"), tr("Close (Esc)"), this);
  row->addWidget(m_prev);
  row->addWidget(m_next);
  row->addWidget(m_close);

  connect(m_input, &QLineEdit::textChanged, this, &FindBar::queryChanged);
  connect(m_input, &QLineEdit::returnPressed, this, &FindBar::findNext);
  connect(m_prev, &QToolButton::clicked, this, &FindBar::findPrevious);
  connect(m_next, &QToolButton::clicked, this, &FindBar::findNext);
  connect(m_close, &QToolButton::clicked, this, &FindBar::deactivate);

  hide();
}

QToolButton *FindBar::makeToggle(char16_t glyph, const QString &fallback,
                                 const QString &tip,
                                 const QString &settingsKey) {
  QToolButton *b = codiconButton(glyph, fallback, tip, this);
  b->setCheckable(true);
  b->setChecked(QSettings().value(settingsKey, false).toBool());
  connect(b, &QToolButton::toggled, this, [this, settingsKey](bool on) {
    QSettings().setValue(settingsKey, on);
    emit queryChanged();
  });
  return b;
}

void FindBar::activate(const QString &seed) {
  show();
  raise();
  if(!seed.isEmpty() && seed != m_input->text()) {
    // setText fires textChanged -> queryChanged, so the owner searches the
    // seeded term without us emitting twice.
    m_input->setText(seed);
  }
  m_input->setFocus();
  m_input->selectAll();
}

void FindBar::deactivate() {
  hide();
  emit closed();
}

QString FindBar::query() const { return m_input->text(); }
bool FindBar::caseSensitive() const { return m_case->isChecked(); }
bool FindBar::wholeWord() const { return m_word->isChecked(); }
bool FindBar::useRegex() const { return m_regex->isChecked(); }

void FindBar::setMatchInfo(int current, int total) {
  setRegexError(false);
  // Show "No results" for both an empty query and a query with no matches, so
  // the readout never collapses to blank space (matching VS Code).
  if(total <= 0) {
    m_count->setText(tr("No results"));
    return;
  }
  m_count->setText(tr("%1 of %2").arg(current).arg(total));
}

void FindBar::setRegexError(bool invalid) {
  if(m_regexError == invalid) return;

  m_regexError = invalid;
  if(invalid) m_count->setText(tr("Invalid regex"));

  updateFieldState();
}

void FindBar::updateFieldState() {
  // A dynamic property the owner's stylesheet keys on
  // (findInputFrame[error="true"]) so the error tint stays themed rather than
  // hard-coded here. Lives on the frame because that's what carries the border.
  m_inputFrame->setProperty("error", m_regexError);
  m_inputFrame->style()->unpolish(m_inputFrame);
  m_inputFrame->style()->polish(m_inputFrame);
}

bool FindBar::eventFilter(QObject *watched, QEvent *event) {
  if(watched == m_input && event->type() == QEvent::KeyPress) {
    auto *ke = static_cast<QKeyEvent *>(event);
    switch(ke->key()) {
      case Qt::Key_Escape:
        deactivate();
        return true;
      case Qt::Key_Return:
      case Qt::Key_Enter:
        if(ke->modifiers() & Qt::ShiftModifier) {
          emit findPrevious();
        } else {
          emit findNext();
        }
        return true;
      default:
        break;
    }
  }
  return QFrame::eventFilter(watched, event);
}
