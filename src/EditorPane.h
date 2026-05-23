#pragma once

#include <QTabWidget>

class DocumentView;

// A single tabbed editor group. Leaves of the EditorArea splitter tree are
// EditorPane instances. Owns its tabs, raises signals when its visible
// document changes, when it gains keyboard focus, or when it loses its
// last tab.
class EditorPane : public QTabWidget {
  Q_OBJECT

public:
  explicit EditorPane(QWidget *parent = nullptr);

  // Take ownership of doc, append it as a tab, and focus it. Returns the
  // tab's index.
  int addDocument(DocumentView *doc);

  // Remove the tab at index without deleting its DocumentView and return
  // the doc to the caller. Emits becameEmpty() if this was the last tab.
  DocumentView *takeDocument(int index);

  // -1 if no tab matches.
  int indexOfFile(const QString &canonicalPath) const;

  DocumentView *documentAt(int index) const;
  DocumentView *currentDocument() const;

  // Append the Pin/Watch/Split/Close actions for `doc` to `menu`. Used
  // both for the tab right-click menu and for the document body's
  // right-click menu. The caller controls where the menu is shown.
  void populateTabContextMenu(class QMenu *menu, DocumentView *doc);

  // Public wrapper for the tab-close handler — used by close-all
  // operations that walk the tab list externally.
  void closeTab(int index);

  // Close every tab in this pane. Emits becameEmpty() after the last
  // one, which lets the EditorArea collapse the pane if it isn't the
  // last surviving one.
  void closeAll();

  // Cycle through tabs with wrap-around. No-op when there's <= 1 tab.
  void nextTab();
  void previousTab();

  // Reorder the current tab. No-op at the right/left edge — does not
  // wrap (matches browser behavior for Ctrl+Shift+PageDown/Up).
  void moveCurrentTabRight();
  void moveCurrentTabLeft();

  enum DropZone { ZoneNone, ZoneTab, ZoneLeft, ZoneRight, ZoneTop, ZoneBottom };

signals:
  void activated();
  void currentDocumentChanged(DocumentView *doc);
  void becameEmpty();

  // Emitted when a tab is actually closed (deleted). Not emitted for
  // moves (drag-out to another pane goes through takeDocument()).
  void tabClosed(const QString &filePath);

protected:
  void focusInEvent(QFocusEvent *e) override;
  void mousePressEvent(QMouseEvent *e) override;
  void dragEnterEvent(QDragEnterEvent *e) override;
  void dragMoveEvent(QDragMoveEvent *e) override;
  void dragLeaveEvent(QDragLeaveEvent *e) override;
  void dropEvent(QDropEvent *e) override;

private slots:
  void onTabCloseRequested(int index);
  void onCurrentChanged(int index);
  void onTabContextMenu(const QPoint &pos);

private:
  QRect bodyRect() const;
  DropZone zoneAt(const QPoint &pos) const;
  QRect zoneRect(DropZone z) const;
  void showDropOverlay(DropZone z);
  void hideDropOverlay();

  QWidget *m_dropOverlay = nullptr;
  EditorPane *m_dragSource = nullptr;
};
