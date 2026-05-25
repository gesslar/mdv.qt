#pragma once

#include <QTabWidget>

class DocumentView;

// A single tabbed editor group. Leaves of the EditorArea splitter tree are
// EditorGroup instances. Owns its tabs, raises signals when its visible
// document changes, when it gains keyboard focus, or when it loses its
// last tab.
class EditorGroup : public QTabWidget {
  Q_OBJECT

public:
  explicit EditorGroup(QWidget *parent = nullptr);

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

  // Close every tab in this group. Emits becameEmpty() after the last
  // one, which lets the EditorArea collapse the group if it isn't the
  // last surviving one.
  void closeAll();

  // Close every tab except the one at `index`. Pinned tabs survive.
  void closeOthers(int index);

  // Close every tab positioned after `index` (to its right). Pinned tabs
  // survive.
  void closeToRight(int index);

  // True if this group holds at least one unpinned tab — i.e. a bulk close
  // (Others / to-the-Right / Group / All) would actually remove something.
  bool hasUnpinnedTabs() const;

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
  // moves (drag-out to another group goes through takeDocument()).
  void tabClosed(const QString &filePath);

  // Emitted when the user drops one or more file URLs onto this group
  // from the OS. The group has already focused itself before emitting.
  void filesDropped(const QStringList &paths);

  // Re-emission of a contained document's openFileRequested — a local-file
  // link was clicked and should open as a new tab.
  void openFileRequested(const QString &path);

  // A contained document's pinned state changed. Surfaced so the window can
  // refresh menu/shortcut enable-state (bulk closes skip pinned tabs).
  void pinStateChanged();

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

  // A contained document's pinned state flipped — swap its tab button
  // between the close (×) and pin (Unpin) roles.
  void onDocPinnedChanged();

private:
  QRect bodyRect() const;
  DropZone zoneAt(const QPoint &pos) const;
  QRect zoneRect(DropZone z) const;
  void showDropOverlay(DropZone z);
  void hideDropOverlay();

  // The tab's trailing button is ours, not QTabBar's auto close button, so we
  // can swap it between close (×, click closes) and pin (click unpins). Owning
  // it for every tab avoids QTabBar's inability to restore an auto close
  // button.
  void installTabButton(int index, DocumentView *doc);
  void refreshTabButton(DocumentView *doc);

  // Keep pinned tabs clustered at the left (stable order within each zone).
  // Runs on pin/unpin and after a manual reorder; the guard stops the moveTab
  // calls from re-entering via tabMoved.
  void enforcePinPartition();
  bool m_reordering = false;

  QWidget *m_dropOverlay = nullptr;
  EditorGroup *m_dragSource = nullptr;
};
