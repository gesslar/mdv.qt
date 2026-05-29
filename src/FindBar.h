#pragma once

#include <QFrame>
#include <QString>

class QLabel;
class QLineEdit;
class QToolButton;

// VS Code-style find overlay: a floating bar (search field, match count,
// prev/next, and Match Case / Whole Word / Use Regex toggles) that the owning
// view positions over the top-right of its content. The bar is purely a UI
// surface — it owns no document and runs no search. It announces intent via
// queryChanged()/findNext()/findPrevious()/closed(); the owner does the actual
// searching and reports results back through setMatchInfo()/setRegexError().
//
// Toggle states persist in QSettings (find/caseSensitive, find/wholeWord,
// find/regex) so the bar reopens the way the user left it.
class FindBar : public QFrame {
  Q_OBJECT

public:
  explicit FindBar(QWidget *parent = nullptr);

  // Show the bar and focus the field, selecting any existing text so a fresh
  // query overtypes it. If <seed> is non-empty it replaces the query first
  // (used to seed from the document's current selection). Emits queryChanged()
  // when the text ends up different from before.
  void activate(const QString &seed);

  // Hide the bar and emit closed() so the owner can clear its highlights and
  // return focus to the content.
  void deactivate();

  QString query() const;
  bool caseSensitive() const;
  bool wholeWord() const;
  bool useRegex() const;

  // Update the "<current> of <total>" readout. current is 1-based; total == 0
  // renders "No results" (and, for a non-empty query, flags the field). An
  // empty query blanks the label instead.
  void setMatchInfo(int current, int total);

  // Tint the field to signal the regex pattern failed to compile. Cleared on
  // the next successful setMatchInfo().
  void setRegexError(bool invalid);

signals:
  // The query text or any toggle changed; the owner should re-run the search.
  void queryChanged();
  void findNext();
  void findPrevious();
  void closed();

protected:
  // Field-level keys: Enter -> next, Shift+Enter -> previous, Esc -> close.
  bool eventFilter(QObject *watched, QEvent *event) override;

private:
  // Build a checkable codicon toggle wired to <settingsKey>, seeded from and
  // written back to QSettings, emitting queryChanged() on toggle.
  QToolButton *makeToggle(char16_t glyph, const QString &fallback,
                          const QString &tip, const QString &settingsKey);

  // Recolor the field per m_regexError without clobbering the themed stylesheet
  // the owner applies to the bar.
  void updateFieldState();

  // The bordered box that wraps the (frameless) field and the in-field
  // toggles, VS Code-style. The match count, nav, and close sit outside it.
  QFrame *m_inputFrame = nullptr;
  QLineEdit *m_input = nullptr;
  QLabel *m_count = nullptr;
  QToolButton *m_prev = nullptr;
  QToolButton *m_next = nullptr;
  QToolButton *m_case = nullptr;
  QToolButton *m_word = nullptr;
  QToolButton *m_regex = nullptr;
  QToolButton *m_close = nullptr;
  bool m_regexError = false;
};
