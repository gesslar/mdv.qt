#include "EditorArea.h"

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QSplitter>
#include <QVBoxLayout>

#include "DocumentView.h"
#include "EditorGroup.h"

namespace {
constexpr int kClosedStackLimit = 50;
}

EditorArea::EditorArea(QWidget *parent) : QWidget(parent) {
  m_layout = new QVBoxLayout(this);
  m_layout->setContentsMargins(0, 0, 0, 0);

  // The per-group focusInEvent only fires when the QTabWidget itself is
  // focused. Click into a child QTextBrowser and focus lands there
  // instead, leaving the active group stale. Listening to the global
  // focus signal lets us find whichever group actually contains the
  // newly focused widget.
  connect(qApp, &QApplication::focusChanged, this, &EditorArea::onFocusChanged);

  auto *initial = makeGroup();
  setRoot(initial);
  setActive(initial);
}

DocumentView *EditorArea::openFile(const QString &path) {
  const QFileInfo info(path);
  QString canonical = info.canonicalFilePath();
  if(canonical.isEmpty()) canonical = info.absoluteFilePath();

  if(const int existing = m_active->indexOfFile(canonical); existing >= 0) {
    m_active->setCurrentIndex(existing);
    return m_active->documentAt(existing);
  }

  auto *doc = new DocumentView(m_active);
  if(!doc->loadFile(path)) {
    delete doc;
    return nullptr;
  }

  m_active->addDocument(doc);
  return doc;
}

void EditorArea::splitActive(SplitSide side) {
  EditorGroup *newGroup = splitInternal(m_active, side);
  if(newGroup) {
    setActive(newGroup);
    newGroup->setFocus();
  }
}

void EditorArea::splitWith(EditorGroup *target, SplitSide side,
                           DocumentView *doc) {
  EditorGroup *newGroup = splitInternal(target, side);
  if(!newGroup) {
    // Fall back to adding to the target so we don't drop the doc on the
    // floor.
    if(doc) target->addDocument(doc);
    return;
  }
  if(doc) newGroup->addDocument(doc);
  setActive(newGroup);
  newGroup->setFocus();
}

EditorGroup *EditorArea::splitInternal(EditorGroup *target, SplitSide side) {
  if(!target) return nullptr;

  const Qt::Orientation wanted =
      (side == Left || side == Right) ? Qt::Horizontal : Qt::Vertical;
  const bool newGroupAfter = (side == Right || side == Bottom);

  // Capture target's current dimension *before* any reparenting — the
  // new splitter won't have a valid width/height until Qt lays it out
  // again, which hasn't happened yet at this point in the call.
  const int targetDim =
      (wanted == Qt::Horizontal) ? target->width() : target->height();
  const int half = targetDim / 2;
  const int otherHalf = targetDim - half;

  auto *newGroup = makeGroup();

  // Same-orientation parent splitter → insert in place, no extra nesting.
  // Split target's slice between target and the new group so both end up
  // half the size target used to occupy.
  if(auto *parentSplit = qobject_cast<QSplitter *>(target->parentWidget());
     parentSplit && parentSplit->orientation() == wanted) {
    const int idx = parentSplit->indexOf(target);
    QList<int> sizes = parentSplit->sizes();

    parentSplit->insertWidget(newGroupAfter ? idx + 1 : idx, newGroup);

    sizes[idx] = otherHalf;                             // target's new size
    sizes.insert(newGroupAfter ? idx + 1 : idx, half);  // new group's size
    parentSplit->setSizes(sizes);
    return newGroup;
  }

  // Wrap target in a new splitter of the wanted orientation. The new
  // splitter takes target's old position in the layout (or replaces
  // root if target was root).
  auto *split = new QSplitter(wanted, this);
  split->setChildrenCollapsible(false);

  auto installChildren = [&]() {
    if(newGroupAfter) {
      split->addWidget(target);
      split->addWidget(newGroup);
    } else {
      split->addWidget(newGroup);
      split->addWidget(target);
    }
  };

  if(target == m_root) {
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
  // (target@0 + newGroup@1, or newGroup@0 + target@1) need half/half so
  // the literal pair {half, otherHalf} is correct either way.
  split->setSizes({half, otherHalf});

  return newGroup;
}

DocumentView *EditorArea::currentDocument() const {
  return m_active ? m_active->currentDocument() : nullptr;
}

void EditorArea::onGroupActivated() {
  auto *group = qobject_cast<EditorGroup *>(sender());
  if(group) setActive(group);
}

void EditorArea::onGroupEmpty() {
  auto *group = qobject_cast<EditorGroup *>(sender());
  if(!group) return;

  // The last surviving group is preserved as an empty placeholder; we
  // never end up with zero groups in the area.
  const auto groups = allGroups();
  if(groups.size() <= 1) return;

  auto *parentSplit = qobject_cast<QSplitter *>(group->parentWidget());
  if(!parentSplit) {
    // group is the root yet there's another group somewhere? Shouldn't
    // happen given our tree shape — bail safely.
    return;
  }

  group->setParent(nullptr);
  group->deleteLater();

  // If the splitter now has only one child, collapse it into its parent.
  if(parentSplit->count() == 1) {
    QWidget *survivor = parentSplit->widget(0);
    survivor->setParent(nullptr);  // detach from parentSplit
    replaceInParent(parentSplit, survivor);
    parentSplit->deleteLater();
  }

  // Promote some surviving group to active so File→Open has a target.
  const auto remaining = allGroups();
  if(!remaining.isEmpty()) {
    setActive(remaining.first());
    remaining.first()->setFocus();
  }
}

void EditorArea::onGroupCurrentDocumentChanged(DocumentView *doc) {
  // Only the active group's current-doc changes surface upward.
  if(sender() == m_active) emit currentDocumentChanged(doc);
}

void EditorArea::onFocusChanged(QWidget *, QWidget *now) {
  if(!now || !isAncestorOf(now)) return;

  for(QWidget *w = now; w && w != this; w = w->parentWidget()) {
    if(auto *group = qobject_cast<EditorGroup *>(w)) {
      setActive(group);
      return;
    }
  }
}

void EditorArea::setRoot(QWidget *w) {
  if(m_root && m_root != w) {
    m_layout->removeWidget(m_root);
  }
  m_root = w;
  if(w->parentWidget() != this) w->setParent(this);
  m_layout->addWidget(w);
}

void EditorArea::setActive(EditorGroup *group) {
  if(m_active == group) return;
  m_active = group;
  emit currentDocumentChanged(group ? group->currentDocument() : nullptr);
}

void EditorArea::registerGroup(EditorGroup *group) {
  connect(group, &EditorGroup::activated, this, &EditorArea::onGroupActivated);
  connect(group, &EditorGroup::becameEmpty, this, &EditorArea::onGroupEmpty);
  connect(group, &EditorGroup::currentDocumentChanged, this,
          &EditorArea::onGroupCurrentDocumentChanged);
  connect(group, &EditorGroup::tabClosed, this, &EditorArea::onTabClosed);
  connect(group, &EditorGroup::filesDropped, this, &EditorArea::filesDropped);
  connect(group, &EditorGroup::openFileRequested, this,
          &EditorArea::openFileRequested);
  connect(group, &EditorGroup::pinStateChanged, this,
          &EditorArea::pinStateChanged);
}

void EditorArea::onTabClosed(const QString &filePath) {
  if(filePath.isEmpty()) return;
  m_closedStack.removeAll(filePath);  // dedupe; most-recent wins
  m_closedStack.prepend(filePath);
  while(m_closedStack.size() > kClosedStackLimit) m_closedStack.removeLast();
}

void EditorArea::reopenLastClosed() {
  // Walk past any entries that no longer exist on disk.
  while(!m_closedStack.isEmpty()) {
    const QString path = m_closedStack.takeFirst();
    if(QFile::exists(path)) {
      openFile(path);
      return;
    }
  }
}

void EditorArea::closeAllInActive() {
  if(m_active) m_active->closeAll();
}

void EditorArea::closeAllEverywhere() {
  // Snapshot the groups before iterating — group destruction during the
  // loop would otherwise invalidate live iteration over allGroups().
  const auto groups = allGroups();
  for(EditorGroup *group : groups) group->closeAll();
}

void EditorArea::replaceInParent(QWidget *child, QWidget *replacement) {
  if(child == m_root) {
    setRoot(replacement);
    return;
  }
  auto *parentSplit = qobject_cast<QSplitter *>(child->parentWidget());
  if(!parentSplit) return;
  const int idx = parentSplit->indexOf(child);
  parentSplit->replaceWidget(idx, replacement);
}

EditorGroup *EditorArea::makeGroup() {
  auto *group = new EditorGroup(this);
  registerGroup(group);
  return group;
}

QList<EditorGroup *> EditorArea::allGroups() const {
  return findChildren<EditorGroup *>();
}

bool EditorArea::hasUnpinnedTabs() const {
  const auto groups = allGroups();
  for(EditorGroup *group : groups) {
    if(group->hasUnpinnedTabs()) return true;
  }
  return false;
}
