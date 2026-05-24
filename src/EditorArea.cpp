#include "EditorArea.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QSplitter>
#include <QVBoxLayout>

#include "DocumentView.h"
#include "EditorPane.h"

namespace {
constexpr int kClosedStackLimit = 50;
}

EditorArea::EditorArea(QWidget *parent) : QWidget(parent) {
  m_layout = new QVBoxLayout(this);
  m_layout->setContentsMargins(0, 0, 0, 0);

  // The per-pane focusInEvent only fires when the QTabWidget itself is
  // focused. Click into a child QTextBrowser and focus lands there
  // instead, leaving the active pane stale. Listening to the global
  // focus signal lets us find whichever pane actually contains the
  // newly focused widget.
  connect(qApp, &QApplication::focusChanged, this,
          &EditorArea::onFocusChanged);

  auto *initial = makePane();
  setRoot(initial);
  setActive(initial);
}

DocumentView *EditorArea::openFile(const QString &path) {
  const QFileInfo info(path);
  QString canonical = info.canonicalFilePath();
  if (canonical.isEmpty()) canonical = info.absoluteFilePath();

  if (const int existing = m_active->indexOfFile(canonical); existing >= 0) {
    m_active->setCurrentIndex(existing);
    return m_active->documentAt(existing);
  }

  auto *doc = new DocumentView(m_active);
  if (!doc->loadFile(path)) {
    delete doc;
    return nullptr;
  }

  m_active->addDocument(doc);
  return doc;
}

void EditorArea::splitActive(SplitSide side) {
  EditorPane *newPane = splitInternal(m_active, side);
  if (newPane) {
    setActive(newPane);
    newPane->setFocus();
  }
}

void EditorArea::splitWith(EditorPane *target, SplitSide side,
                           DocumentView *doc) {
  EditorPane *newPane = splitInternal(target, side);
  if (!newPane) {
    // Fall back to adding to the target so we don't drop the doc on the
    // floor.
    if (doc) target->addDocument(doc);
    return;
  }
  if (doc) newPane->addDocument(doc);
  setActive(newPane);
  newPane->setFocus();
}

EditorPane *EditorArea::splitInternal(EditorPane *target, SplitSide side) {
  if (!target) return nullptr;

  const Qt::Orientation wanted =
      (side == Left || side == Right) ? Qt::Horizontal : Qt::Vertical;
  const bool newPaneAfter = (side == Right || side == Bottom);

  // Capture target's current dimension *before* any reparenting — the
  // new splitter won't have a valid width/height until Qt lays it out
  // again, which hasn't happened yet at this point in the call.
  const int targetDim =
      (wanted == Qt::Horizontal) ? target->width() : target->height();
  const int half = targetDim / 2;
  const int otherHalf = targetDim - half;

  auto *newPane = makePane();

  // Same-orientation parent splitter → insert in place, no extra nesting.
  // Split target's slice between target and the new pane so both end up
  // half the size target used to occupy.
  if (auto *parentSplit = qobject_cast<QSplitter *>(target->parentWidget());
      parentSplit && parentSplit->orientation() == wanted) {
    const int idx = parentSplit->indexOf(target);
    QList<int> sizes = parentSplit->sizes();

    parentSplit->insertWidget(newPaneAfter ? idx + 1 : idx, newPane);

    sizes[idx] = otherHalf;  // target's new size
    sizes.insert(newPaneAfter ? idx + 1 : idx, half);  // new pane's size
    parentSplit->setSizes(sizes);
    return newPane;
  }

  // Wrap target in a new splitter of the wanted orientation. The new
  // splitter takes target's old position in the layout (or replaces
  // root if target was root).
  auto *split = new QSplitter(wanted, this);
  split->setChildrenCollapsible(false);

  auto installChildren = [&]() {
    if (newPaneAfter) {
      split->addWidget(target);
      split->addWidget(newPane);
    } else {
      split->addWidget(newPane);
      split->addWidget(target);
    }
  };

  if (target == m_root) {
    m_layout->removeWidget(target);
    installChildren();
    setRoot(split);
  } else {
    auto *parentSplit = qobject_cast<QSplitter *>(target->parentWidget());
    Q_ASSERT(parentSplit);
    const int idx = parentSplit->indexOf(target);
    QWidget *displaced = parentSplit->replaceWidget(idx, split);
    Q_ASSERT(displaced == target);
    installChildren();
  }

  // Sizes are positional in the splitter's child list; both arrangements
  // (target@0 + newPane@1, or newPane@0 + target@1) need half/half so
  // the literal pair {half, otherHalf} is correct either way.
  split->setSizes({half, otherHalf});

  return newPane;
}

DocumentView *EditorArea::currentDocument() const {
  return m_active ? m_active->currentDocument() : nullptr;
}

void EditorArea::onPaneActivated() {
  auto *pane = qobject_cast<EditorPane *>(sender());
  if (pane) setActive(pane);
}

void EditorArea::onPaneEmpty() {
  auto *pane = qobject_cast<EditorPane *>(sender());
  if (!pane) return;

  // The last surviving pane is preserved as an empty placeholder; we
  // never end up with zero panes in the area.
  const auto panes = allPanes();
  if (panes.size() <= 1) return;

  auto *parentSplit = qobject_cast<QSplitter *>(pane->parentWidget());
  if (!parentSplit) {
    // pane is the root yet there's another pane somewhere? Shouldn't
    // happen given our tree shape — bail safely.
    return;
  }

  pane->setParent(nullptr);
  pane->deleteLater();

  // If the splitter now has only one child, collapse it into its parent.
  if (parentSplit->count() == 1) {
    QWidget *survivor = parentSplit->widget(0);
    survivor->setParent(nullptr);  // detach from parentSplit
    replaceInParent(parentSplit, survivor);
    parentSplit->deleteLater();
  }

  // Promote some surviving pane to active so File→Open has a target.
  const auto remaining = allPanes();
  if (!remaining.isEmpty()) {
    setActive(remaining.first());
    remaining.first()->setFocus();
  }
}

void EditorArea::onPaneCurrentDocumentChanged(DocumentView *doc) {
  // Only the active pane's current-doc changes surface upward.
  if (sender() == m_active) emit currentDocumentChanged(doc);
}

void EditorArea::onFocusChanged(QWidget *, QWidget *now) {
  if (!now || !isAncestorOf(now)) return;

  for (QWidget *w = now; w && w != this; w = w->parentWidget()) {
    if (auto *pane = qobject_cast<EditorPane *>(w)) {
      setActive(pane);
      return;
    }
  }
}

void EditorArea::setRoot(QWidget *w) {
  if (m_root && m_root != w) {
    m_layout->removeWidget(m_root);
  }
  m_root = w;
  if (w->parentWidget() != this) w->setParent(this);
  m_layout->addWidget(w);
}

void EditorArea::setActive(EditorPane *pane) {
  if (m_active == pane) return;
  m_active = pane;
  emit currentDocumentChanged(pane ? pane->currentDocument() : nullptr);
}

void EditorArea::registerPane(EditorPane *pane) {
  connect(pane, &EditorPane::activated, this, &EditorArea::onPaneActivated);
  connect(pane, &EditorPane::becameEmpty, this, &EditorArea::onPaneEmpty);
  connect(pane, &EditorPane::currentDocumentChanged, this,
          &EditorArea::onPaneCurrentDocumentChanged);
  connect(pane, &EditorPane::tabClosed, this, &EditorArea::onTabClosed);
  connect(pane, &EditorPane::filesDropped, this, &EditorArea::filesDropped);
  connect(pane, &EditorPane::openFileRequested, this,
          &EditorArea::openFileRequested);
}

void EditorArea::onTabClosed(const QString &filePath) {
  if (filePath.isEmpty()) return;
  m_closedStack.removeAll(filePath);  // dedupe; most-recent wins
  m_closedStack.prepend(filePath);
  while (m_closedStack.size() > kClosedStackLimit) m_closedStack.removeLast();
}

void EditorArea::reopenLastClosed() {
  // Walk past any entries that no longer exist on disk.
  while (!m_closedStack.isEmpty()) {
    const QString path = m_closedStack.takeFirst();
    if (QFile::exists(path)) {
      openFile(path);
      return;
    }
  }
}

void EditorArea::closeAllInActive() {
  if (m_active) m_active->closeAll();
}

void EditorArea::closeAllEverywhere() {
  // Snapshot the panes before iterating — pane destruction during the
  // loop would otherwise invalidate live iteration over allPanes().
  const auto panes = allPanes();
  for (EditorPane *pane : panes) pane->closeAll();
}

void EditorArea::replaceInParent(QWidget *child, QWidget *replacement) {
  if (child == m_root) {
    setRoot(replacement);
    return;
  }
  auto *parentSplit = qobject_cast<QSplitter *>(child->parentWidget());
  if (!parentSplit) return;
  const int idx = parentSplit->indexOf(child);
  parentSplit->replaceWidget(idx, replacement);
}

EditorPane *EditorArea::makePane() {
  auto *pane = new EditorPane(this);
  registerPane(pane);
  return pane;
}

QList<EditorPane *> EditorArea::allPanes() const {
  return findChildren<EditorPane *>();
}
