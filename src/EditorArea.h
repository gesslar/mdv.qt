#pragma once

#include <QWidget>

class DocumentView;
class EditorPane;
class QSplitter;
class QVBoxLayout;

// Manages the tree of QSplitters whose leaves are EditorPanes. Handles
// active-pane tracking, file-open routing, and pane lifecycle (split,
// collapse-empty).
class EditorArea : public QWidget {
  Q_OBJECT

public:
  enum SplitSide { Left, Right, Top, Bottom };

  explicit EditorArea(QWidget *parent = nullptr);

  // Open in the active pane. If that pane already has a tab for this
  // file (canonical path), focus the tab instead. Other panes may
  // independently have the same file open — dedup is per-pane, matching
  // VS Code's side-by-side semantics.
  DocumentView *openFile(const QString &path);

  // Split the active pane. The new pane starts empty and becomes the
  // active pane.
  void splitActive(SplitSide side);

  // Split `target` and place `doc` in the new pane. Used by drag-and-drop
  // when the user drops a tab into a split zone.
  void splitWith(EditorPane *target, SplitSide side, DocumentView *doc);

  // Pop the most-recently-closed file off the stack and open it in the
  // currently-active pane. No-op if the stack is empty (or the file no
  // longer exists, in which case we discard and try the next entry).
  void reopenLastClosed();

  // Close every tab in the active pane. The pane is destroyed unless
  // it's the last surviving one.
  void closeAllInActive();

  // Close every tab in every pane. All non-root panes are destroyed;
  // the root pane is preserved as an empty placeholder.
  void closeAllEverywhere();

  EditorPane *activePane() const { return m_active; }
  DocumentView *currentDocument() const;

signals:
  void currentDocumentChanged(DocumentView *doc);

private slots:
  void onPaneActivated();
  void onPaneEmpty();
  void onPaneCurrentDocumentChanged(DocumentView *doc);
  void onFocusChanged(QWidget *old, QWidget *now);
  void onTabClosed(const QString &filePath);

private:
  void setRoot(QWidget *w);
  void setActive(EditorPane *pane);
  void registerPane(EditorPane *pane);

  // Common split implementation. Returns the new (empty) pane. Used by
  // both splitActive and splitWith.
  EditorPane *splitInternal(EditorPane *target, SplitSide side);

  // Replace `child` inside its parent with `replacement`. Walks one
  // level — caller is responsible for any cascading collapse.
  void replaceInParent(QWidget *child, QWidget *replacement);

  EditorPane *makePane();
  QList<EditorPane *> allPanes() const;

  QVBoxLayout *m_layout = nullptr;
  QWidget *m_root = nullptr;      // EditorPane or QSplitter
  EditorPane *m_active = nullptr;

  // Most-recently-closed-first list of file paths, fed by every pane's
  // tabClosed signal. Capped to keep memory bounded.
  QStringList m_closedStack;
};
