#pragma once

#include <QHash>
#include <QList>
#include <QWidget>

#include "Markdown.h"

class QLabel;
class QToolButton;
class QTreeWidget;
class QTreeWidgetItem;

// Which side of its document the outline sits against. Only ever left or
// right — never floats, never top/bottom.
enum class OutlineSide { Left, Right };

// The outline body: a slim header (title + collapse button) over a tree of the
// owning document's headings. A pure view — it emits intent signals (a row was
// activated, hide me, move me) and the DocumentView that owns it wires them up.
class OutlinePanel : public QWidget {
  Q_OBJECT

public:
  explicit OutlinePanel(QWidget *parent = nullptr);

  // Rebuild the tree from a document's outline; an empty list shows the
  // "no headings" placeholder.
  void setHeadings(const QList<mdv::Heading> &headings);

  // Scroll-spy: highlight the row for this anchor id (empty clears the
  // highlight). Does not emit headingActivated().
  void setCurrentHeading(const QString &anchorId);

  // The collapse button's chevron points at whichever edge we're docked on.
  void setSide(OutlineSide side);

signals:
  void headingActivated(const QString &anchorId);
  void hideRequested();
  void moveToOtherSideRequested();

protected:
  // Re-pull the stylesheet when the system palette shifts (accent or
  // light/dark), so the panel keeps matching the surrounding chrome.
  void changeEvent(QEvent *e) override;
  // Tree-viewport tooltips: show the full heading text only when the row is too
  // narrow to display it (i.e. the label is elided).
  bool eventFilter(QObject *obj, QEvent *e) override;

private:
  void refreshTheme();

  QLabel *m_title = nullptr;
  QToolButton *m_collapseButton = nullptr;
  QTreeWidget *m_tree = nullptr;
  QLabel *m_placeholder = nullptr;
  OutlineSide m_side = OutlineSide::Left;
  QHash<QString, QTreeWidgetItem *> m_itemById;
};
