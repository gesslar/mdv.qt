#pragma once

#include <QWidget>

class DocumentView;
class EditorGroup;
class QSplitter;
class QVBoxLayout;

// Manages the tree of QSplitters whose leaves are EditorGroups. Handles
// active-group tracking, file-open routing, and group lifecycle (split,
// collapse-empty).
class EditorArea : public QWidget {
  Q_OBJECT

public:
  enum SplitSide { Left, Right, Top, Bottom };

  explicit EditorArea(QWidget *parent = nullptr);

  // Open in the active group. If that group already has a tab for this
  // file (canonical path), focus the tab instead. Other groups may
  // independently have the same file open — dedup is per-group, matching
  // VS Code's side-by-side semantics.
  DocumentView *openFile(const QString &path);

  // Split the active group. The new group starts empty and becomes the
  // active group.
  void splitActive(SplitSide side);

  // Split `target` and place `doc` in the new group. Used by drag-and-drop
  // when the user drops a tab into a split zone.
  void splitWith(EditorGroup *target, SplitSide side, DocumentView *doc);

  // Pop the most-recently-closed file off the stack and open it in the
  // currently-active group. No-op if the stack is empty (or the file no
  // longer exists, in which case we discard and try the next entry).
  void reopenLastClosed();

  // Close every tab in the active group. The group is destroyed unless
  // it's the last surviving one.
  void closeAllInActive();

  // Close every unpinned tab in every group. Groups left empty collapse (the
  // root group is preserved as a placeholder); groups still holding pinned tabs
  // stay put.
  void closeAllEverywhere();

  EditorGroup *activeGroup() const { return m_active; }
  DocumentView *currentDocument() const;

  // True if any group holds at least one unpinned (bulk-closeable) tab.
  bool hasUnpinnedTabs() const;

  // True if the reopen-closed stack has any entry to restore.
  bool hasClosedTabs() const { return !m_closedStack.isEmpty(); }

signals:
  void currentDocumentChanged(DocumentView *doc);

  // Re-emission of an EditorGroup's filesDropped signal — the active
  // group has already been set before this fires, so subscribers can
  // route through their normal openFile path.
  void filesDropped(const QStringList &paths);

  // Re-emission of a group's openFileRequested — a local-file link was
  // clicked; subscribers open it through their normal openFile path.
  void openFileRequested(const QString &path);

  // Re-emission of a group's pinStateChanged — a tab was pinned/unpinned, which
  // can change whether bulk-close actions are no-ops.
  void pinStateChanged();

private slots:
  void onGroupActivated();
  void onGroupEmpty();
  void onGroupCurrentDocumentChanged(DocumentView *doc);
  void onFocusChanged(QWidget *old, QWidget *now);
  void onTabClosed(const QString &filePath);

private:
  void setRoot(QWidget *w);
  void setActive(EditorGroup *group);
  void registerGroup(EditorGroup *group);

  // Common split implementation. Returns the new (empty) group. Used by
  // both splitActive and splitWith.
  EditorGroup *splitInternal(EditorGroup *target, SplitSide side);

  // Replace `child` inside its parent with `replacement`. Walks one
  // level — caller is responsible for any cascading collapse.
  void replaceInParent(QWidget *child, QWidget *replacement);

  EditorGroup *makeGroup();
  QList<EditorGroup *> allGroups() const;

  QVBoxLayout *m_layout = nullptr;
  QWidget *m_root = nullptr;      // EditorGroup or QSplitter
  EditorGroup *m_active = nullptr;

  // Most-recently-closed-first list of file paths, fed by every group's
  // tabClosed signal. Capped to keep memory bounded.
  QStringList m_closedStack;
};
