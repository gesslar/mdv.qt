#include "EditorPane.h"

#include <QAbstractButton>
#include <QAction>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEnterEvent>
#include <QFocusEvent>
#include <QIcon>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QStyle>
#include <QTabBar>
#include <QUrl>

#include "DocumentView.h"
#include "EditorArea.h"
#include "TabBar.h"

namespace {

// Translucent overlay painted on top of a pane to preview where a tab
// drop will land. Mouse-transparent so it never intercepts drag events.
// Color is sourced from QPalette::Highlight so it tracks the user's
// system accent / selection color.
class DropOverlay : public QWidget {
public:
  explicit DropOverlay(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_TransparentForMouseEvents);
    hide();
  }

protected:
  void paintEvent(QPaintEvent *) override {
    const QColor accent = palette().color(QPalette::Highlight);
    QColor fill = accent;
    fill.setAlpha(80);

    QPainter p(this);
    p.fillRect(rect(), fill);
    p.setPen(QPen(accent, 2));
    p.drawRect(rect().adjusted(1, 1, -1, -1));
  }
};

// A frameless icon button for a tab's trailing slot. It paints only its icon
// (plus a faint rounded hover), so none of QToolButton's style chrome leaks
// through — used as the close (×) / pin (Unpin) affordance.
class TabActionButton : public QAbstractButton {
public:
  explicit TabActionButton(QWidget *parent) : QAbstractButton(parent) {
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::ArrowCursor);
  }

  QSize sizeHint() const override {
    const int s = iconSize().width() + 6;
    return QSize(s, s);
  }

protected:
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    if (underMouse()) {
      p.setRenderHint(QPainter::Antialiasing, true);
      p.setPen(Qt::NoPen);
      p.setBrush(QColor(127, 127, 127, 64));
      p.drawRoundedRect(rect(), 3, 3);
    }
    QRect iconRect(QPoint(0, 0), iconSize());
    iconRect.moveCenter(rect().center());
    icon().paint(&p, iconRect);
  }

  void enterEvent(QEnterEvent *e) override {
    QAbstractButton::enterEvent(e);
    update();
  }
  void leaveEvent(QEvent *e) override {
    QAbstractButton::leaveEvent(e);
    update();
  }
};

EditorArea *findArea(QWidget *start) {
  for (QWidget *w = start; w; w = w->parentWidget()) {
    if (auto *a = qobject_cast<EditorArea *>(w)) return a;
  }
  return nullptr;
}

EditorArea::SplitSide sideForZone(EditorPane::DropZone z) {
  switch (z) {
    case EditorPane::ZoneLeft:   return EditorArea::Left;
    case EditorPane::ZoneRight:  return EditorArea::Right;
    case EditorPane::ZoneTop:    return EditorArea::Top;
    case EditorPane::ZoneBottom: return EditorArea::Bottom;
    default:                     return EditorArea::Right;  // unused
  }
}

}  // namespace

EditorPane::EditorPane(QWidget *parent) : QTabWidget(parent) {
  // Install our custom tab bar before any other setup so movability and
  // close buttons are bound to it.
  setTabBar(new TabBar(this));

  setTabsClosable(true);
  setMovable(true);
  setDocumentMode(true);
  setAcceptDrops(true);

  // ClickFocus so any click inside the pane (tab bar or document area)
  // routes a focusInEvent to us, which is how EditorArea learns which
  // pane is active.
  setFocusPolicy(Qt::ClickFocus);

  connect(this, &QTabWidget::tabCloseRequested, this,
          &EditorPane::onTabCloseRequested);
  connect(this, &QTabWidget::currentChanged, this,
          &EditorPane::onCurrentChanged);

  tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tabBar(), &QWidget::customContextMenuRequested, this,
          &EditorPane::onTabContextMenu);

  // Keep pinned tabs clustered at the left even after a manual drag reorders
  // them across the boundary.
  connect(tabBar(), &QTabBar::tabMoved, this,
          &EditorPane::enforcePinPartition);

  m_dropOverlay = new DropOverlay(this);
}

int EditorPane::addDocument(DocumentView *doc) {
  // Relay link-driven opens up toward the window. UniqueConnection because a
  // tab can pass through addDocument again when dragged between panes.
  connect(doc, &DocumentView::openFileRequested, this,
          &EditorPane::openFileRequested, Qt::UniqueConnection);

  // Swap the tab's close/pin button when a contained doc's pin flips.
  // UniqueConnection requires a PMF slot — it asserts on a lambda.
  connect(doc, &DocumentView::pinnedChanged, this,
          &EditorPane::onDocPinnedChanged, Qt::UniqueConnection);

  const int idx = addTab(doc, doc->displayName());
  setTabToolTip(idx, doc->filePath());
  installTabButton(idx, doc);
  setCurrentIndex(idx);
  return idx;
}

DocumentView *EditorPane::takeDocument(int index) {
  if (index < 0 || index >= count()) return nullptr;
  auto *doc = qobject_cast<DocumentView *>(widget(index));
  if (!doc) return nullptr;
  removeTab(index);  // does not delete; caller takes ownership
  if (count() == 0) emit becameEmpty();
  return doc;
}

int EditorPane::indexOfFile(const QString &canonicalPath) const {
  for (int i = 0; i < count(); ++i) {
    auto *doc = documentAt(i);
    if (doc && doc->filePath() == canonicalPath) return i;
  }
  return -1;
}

DocumentView *EditorPane::documentAt(int index) const {
  return qobject_cast<DocumentView *>(widget(index));
}

DocumentView *EditorPane::currentDocument() const {
  return documentAt(currentIndex());
}

void EditorPane::focusInEvent(QFocusEvent *e) {
  QTabWidget::focusInEvent(e);
  emit activated();
}

void EditorPane::mousePressEvent(QMouseEvent *e) {
  // QTabWidget itself rarely receives mouse presses (its children do),
  // but if the user clicks the empty area to the right of the tab bar
  // we still want to claim focus.
  emit activated();
  QTabWidget::mousePressEvent(e);
}

void EditorPane::onTabCloseRequested(int index) {
  QWidget *w = widget(index);
  if (auto *doc = qobject_cast<DocumentView *>(w)) {
    const QString path = doc->filePath();
    if (!path.isEmpty()) emit tabClosed(path);
  }
  removeTab(index);  // does not delete the page widget
  if (w) w->deleteLater();
  if (count() == 0) emit becameEmpty();
}

void EditorPane::closeTab(int index) { onTabCloseRequested(index); }

void EditorPane::closeAll() {
  // Bulk close — pinned tabs survive (only an explicit Close/X removes one).
  // Walk from the end so each removal leaves lower indices valid; if any
  // pinned tabs remain, the pane stays alive instead of collapsing.
  for (int i = count() - 1; i >= 0; --i) {
    auto *doc = documentAt(i);
    if (doc && doc->isPinned()) continue;
    onTabCloseRequested(i);
  }
}

void EditorPane::closeOthers(int index) {
  QWidget *keep = widget(index);
  if (!keep) return;
  // Walk from the end so each removal leaves lower indices valid; skip the
  // kept tab (pointer-based, robust to shuffling) and any pinned tab.
  for (int i = count() - 1; i >= 0; --i) {
    if (widget(i) == keep) continue;
    auto *doc = documentAt(i);
    if (doc && doc->isPinned()) continue;
    onTabCloseRequested(i);
  }
}

void EditorPane::closeToRight(int index) {
  // Close from the last tab down to just past `index`. Closing a higher index
  // never shifts `index` or anything left of it. Pinned tabs survive.
  for (int i = count() - 1; i > index; --i) {
    auto *doc = documentAt(i);
    if (doc && doc->isPinned()) continue;
    onTabCloseRequested(i);
  }
}

bool EditorPane::hasUnpinnedTabs() const {
  for (int i = 0; i < count(); ++i) {
    auto *doc = documentAt(i);
    if (doc && !doc->isPinned()) return true;
  }
  return false;
}

void EditorPane::nextTab() {
  if (count() <= 1) return;
  setCurrentIndex((currentIndex() + 1) % count());
}

void EditorPane::previousTab() {
  if (count() <= 1) return;
  setCurrentIndex((currentIndex() - 1 + count()) % count());
}

void EditorPane::moveCurrentTabRight() {
  const int idx = currentIndex();
  if (idx < 0 || idx >= count() - 1) return;
  // QTabBar::moveTab emits tabMoved, which QTabWidget hooks to keep the
  // page widgets in lockstep with the bar — no separate widget shuffle
  // needed.
  tabBar()->moveTab(idx, idx + 1);
}

void EditorPane::moveCurrentTabLeft() {
  const int idx = currentIndex();
  if (idx <= 0) return;
  tabBar()->moveTab(idx, idx - 1);
}

void EditorPane::onCurrentChanged(int index) {
  emit currentDocumentChanged(documentAt(index));
}

void EditorPane::onDocPinnedChanged() {
  // Swap the affected tab's button between the close and pin roles, then slide
  // it into the pinned cluster (or back out to the unpinned region).
  refreshTabButton(qobject_cast<DocumentView *>(sender()));
  enforcePinPartition();
}

void EditorPane::enforcePinPartition() {
  if (m_reordering) return;
  m_reordering = true;
  // Stable partition: walk left-to-right, pulling each pinned tab to the next
  // pinned slot. Relative order within each zone is preserved, so a legal
  // reorder sticks while a tab dragged across the boundary snaps back.
  int target = 0;
  for (int i = 0; i < count(); ++i) {
    auto *doc = documentAt(i);
    if (doc && doc->isPinned()) {
      if (i != target) tabBar()->moveTab(i, target);
      ++target;
    }
  }
  m_reordering = false;
}

void EditorPane::installTabButton(int index, DocumentView *doc) {
  const auto side = QTabBar::ButtonPosition(style()->styleHint(
      QStyle::SH_TabBar_CloseButtonPosition, nullptr, tabBar()));
  const int sz =
      style()->pixelMetric(QStyle::PM_TabCloseIndicatorWidth, nullptr, tabBar());

  auto *btn = new TabActionButton(tabBar());
  btn->setIconSize(QSize(sz, sz));

  // One handler serves both roles: a pinned tab's button unpins, otherwise it
  // closes. refreshTabButton() sets the icon/tooltip to match the live role.
  connect(btn, &QAbstractButton::clicked, this, [this, doc]() {
    if (doc->isPinned()) {
      doc->setPinned(false);
    } else {
      const int i = indexOf(doc);
      if (i >= 0) closeTab(i);
    }
  });

  // QTabBar auto-created a native close button on insert (setTabsClosable);
  // setTabButton only *hides* the old one, so delete it first or it lingers as
  // an orphaned hidden child of the tab bar until the pane is destroyed.
  if (auto *old =
          qobject_cast<QAbstractButton *>(tabBar()->tabButton(index, side)))
    old->deleteLater();
  tabBar()->setTabButton(index, side, btn);
  refreshTabButton(doc);
}

void EditorPane::refreshTabButton(DocumentView *doc) {
  if (!doc) return;
  const int index = indexOf(doc);
  if (index < 0) return;  // not a tab of this pane (e.g. dragged away)

  const auto side = QTabBar::ButtonPosition(style()->styleHint(
      QStyle::SH_TabBar_CloseButtonPosition, nullptr, tabBar()));
  auto *btn = qobject_cast<QAbstractButton *>(tabBar()->tabButton(index, side));
  if (!btn) return;

  // Codicon glyphs (baked gray). Pinned tab → pin (click unpins); else close.
  if (doc->isPinned()) {
    static const QIcon pinIcon(QStringLiteral(":/icons/pin.svg"));
    btn->setIcon(pinIcon);
    btn->setToolTip(tr("Unpin"));
  } else {
    static const QIcon closeIcon(QStringLiteral(":/icons/close.svg"));
    btn->setIcon(closeIcon);
    btn->setToolTip(tr("Close"));
  }
}

void EditorPane::dragEnterEvent(QDragEnterEvent *e) {
  if (e->mimeData()->hasFormat(TabBar::mimeType())) {
    QByteArray payload = e->mimeData()->data(TabBar::mimeType());
    QDataStream ds(payload);
    qintptr ptr = 0;
    qint32 idx = -1;
    ds >> ptr >> idx;
    m_dragSource = reinterpret_cast<EditorPane *>(ptr);
    e->acceptProposedAction();
    return;
  }

  // OS file drag — accept anything that carries local file URLs. We
  // don't show the split-zone overlay for OS drops; the entire pane
  // is the drop target.
  if (e->mimeData()->hasUrls()) {
    for (const QUrl &url : e->mimeData()->urls()) {
      if (url.isLocalFile()) {
        e->acceptProposedAction();
        return;
      }
    }
  }
}

void EditorPane::dragMoveEvent(QDragMoveEvent *e) {
  if (e->mimeData()->hasFormat(TabBar::mimeType())) {
    // Pinned tabs can't change groups — reject so the OS shows the forbidden
    // cursor and the drop no-ops.
    if (TabBar::draggedTabIsPinned(e->mimeData())) {
      hideDropOverlay();
      e->ignore();
      return;
    }
    const DropZone zone = zoneAt(e->position().toPoint());
    // Same-pane center: tab is already here. No overlay; ignore the
    // move so the OS shows a "no-drop" cursor.
    const bool noop = zone == ZoneNone ||
                      (m_dragSource == this && zone == ZoneTab);
    if (noop) {
      hideDropOverlay();
      e->ignore();
      return;
    }
    showDropOverlay(zone);
    e->acceptProposedAction();
    return;
  }

  if (e->mimeData()->hasUrls()) {
    e->acceptProposedAction();
  }
}

void EditorPane::dragLeaveEvent(QDragLeaveEvent *) {
  hideDropOverlay();
  m_dragSource = nullptr;
}

void EditorPane::dropEvent(QDropEvent *e) {
  hideDropOverlay();

  // OS file drop — handled separately from internal tab drags.
  if (e->mimeData()->hasUrls() &&
      !e->mimeData()->hasFormat(TabBar::mimeType())) {
    QStringList paths;
    for (const QUrl &url : e->mimeData()->urls()) {
      if (url.isLocalFile()) paths << url.toLocalFile();
    }
    if (!paths.isEmpty()) {
      // Take focus so this pane becomes active before the open routes
      // through openFile() — that way the file opens in the pane the
      // user actually dropped on.
      setFocus();
      emit filesDropped(paths);
      e->acceptProposedAction();
    }
    return;
  }

  if (!e->mimeData()->hasFormat(TabBar::mimeType())) return;
  // Pinned tabs never cross groups (dragMoveEvent already rejects, but guard
  // the drop too).
  if (TabBar::draggedTabIsPinned(e->mimeData())) return;

  QByteArray payload = e->mimeData()->data(TabBar::mimeType());
  QDataStream ds(payload);
  qintptr ptr = 0;
  qint32 idx = -1;
  ds >> ptr >> idx;
  if (ds.status() != QDataStream::Ok) return;

  auto *source = reinterpret_cast<EditorPane *>(ptr);
  if (!source) return;

  const DropZone zone = zoneAt(e->position().toPoint());
  if (zone == ZoneNone) return;
  if (source == this && zone == ZoneTab) return;  // tab already here

  DocumentView *doc = source->takeDocument(idx);
  if (!doc) return;

  if (zone == ZoneTab) {
    addDocument(doc);
  } else {
    EditorArea *area = findArea(this);
    if (area) {
      area->splitWith(this, sideForZone(zone), doc);
    } else {
      // Defensive — shouldn't happen given how panes are constructed.
      addDocument(doc);
    }
  }

  m_dragSource = nullptr;
  e->acceptProposedAction();
}

QRect EditorPane::bodyRect() const {
  QRect r = rect();
  if (tabBar()->isVisible()) {
    r.setTop(tabBar()->geometry().bottom() + 1);
  }
  return r;
}

EditorPane::DropZone EditorPane::zoneAt(const QPoint &pos) const {
  const QRect body = bodyRect();
  if (!body.contains(pos)) return ZoneNone;

  const int x = pos.x() - body.left();
  const int y = pos.y() - body.top();
  const int w = body.width();
  const int h = body.height();

  // Left/right strips: outer ~25% of width, full body height.
  if (x < w * 25 / 100) return ZoneLeft;
  if (x > w * 75 / 100) return ZoneRight;

  // Central column (~middle 50% of width): top/bottom 25% of height
  // are vertical splits; middle 50% is the tab-append zone.
  if (y < h * 25 / 100) return ZoneTop;
  if (y > h * 75 / 100) return ZoneBottom;
  return ZoneTab;
}

QRect EditorPane::zoneRect(DropZone z) const {
  const QRect body = bodyRect();
  const int w = body.width();
  const int h = body.height();
  switch (z) {
    case ZoneLeft:
      return QRect(body.left(), body.top(), w / 2, h);
    case ZoneRight:
      return QRect(body.left() + w / 2, body.top(), w - w / 2, h);
    case ZoneTop:
      return QRect(body.left(), body.top(), w, h / 2);
    case ZoneBottom:
      return QRect(body.left(), body.top() + h / 2, w, h - h / 2);
    case ZoneTab:
      // Inset slightly so the indicator reads as "land in this group"
      // rather than "occupy the whole pane."
      return body.adjusted(w / 10, h / 10, -w / 10, -h / 10);
    default:
      return QRect();
  }
}

void EditorPane::showDropOverlay(DropZone z) {
  const QRect r = zoneRect(z);
  if (!r.isValid()) {
    hideDropOverlay();
    return;
  }
  m_dropOverlay->setGeometry(r);
  m_dropOverlay->raise();
  m_dropOverlay->show();
}

void EditorPane::hideDropOverlay() { m_dropOverlay->hide(); }

void EditorPane::onTabContextMenu(const QPoint &pos) {
  const int index = tabBar()->tabAt(pos);
  if (index < 0) return;

  auto *doc = documentAt(index);
  if (!doc) return;

  QMenu menu(this);
  populateTabContextMenu(&menu, doc);
  menu.exec(tabBar()->mapToGlobal(pos));
}

void EditorPane::populateTabContextMenu(QMenu *menu, DocumentView *doc) {
  if (!doc) return;
  const int index = indexOf(doc);
  if (index < 0) return;  // doc isn't a tab of this pane

  auto *pinAction = menu->addAction(tr("&Pin"));
  pinAction->setCheckable(true);
  pinAction->setChecked(doc->isPinned());
  connect(pinAction, &QAction::toggled, doc, &DocumentView::setPinned);

  auto *watchAction = menu->addAction(tr("&Watch for changes"));
  watchAction->setCheckable(true);
  watchAction->setChecked(doc->isWatching());
  connect(watchAction, &QAction::toggled, doc, &DocumentView::setWatching);

  menu->addSeparator();

  // "Split" entries open a fresh DocumentView for the same file in a
  // new pane on the chosen side, with the source pane's scroll position
  // carried over — useful for side-by-side viewing of a single document.
  auto splitOpen = [this, doc](EditorArea::SplitSide side) {
    EditorArea *area = findArea(this);
    if (!area) return;
    // Capture a document-relative anchor (character offset) rather than
    // the raw scrollbar value — the new pane has a different width and
    // the markdown reflows, so absolute scrollbar values don't carry
    // meaning across panes. Both source and clone need to re-anchor
    // after the split because both panes' widths change.
    const int anchor = doc->topAnchor();
    auto *clone = new DocumentView;
    if (!clone->loadFile(doc->filePath())) {
      delete clone;
      return;
    }
    clone->scrollToAnchor(anchor);
    area->splitWith(this, side, clone);
    // Source's layout changed too (it just got narrower) — re-anchor it
    // so both panes show the same character at the top.
    doc->scrollToAnchor(anchor);
  };

  auto *splitRightAction = menu->addAction(tr("Split &Right"));
  connect(splitRightAction, &QAction::triggered, this,
          [splitOpen]() { splitOpen(EditorArea::Right); });

  auto *splitDownAction = menu->addAction(tr("Split &Down"));
  connect(splitDownAction, &QAction::triggered, this,
          [splitOpen]() { splitOpen(EditorArea::Bottom); });

  menu->addSeparator();

  auto *closeAction = menu->addAction(tr("&Close"));
  connect(closeAction, &QAction::triggered, this,
          [this, index]() { onTabCloseRequested(index); });

  // Pin-aware enable states: the bulk closes skip pinned tabs, so each greys
  // out when nothing it would touch is unpinned. One pass computes both.
  bool unpinnedOther = false;
  bool unpinnedRight = false;
  for (int i = 0; i < count(); ++i) {
    auto *d = documentAt(i);
    if (!d || d->isPinned()) continue;
    if (i != index) unpinnedOther = true;
    if (i > index) unpinnedRight = true;
  }

  auto *closeOthersAction = menu->addAction(tr("Close &Others"));
  closeOthersAction->setEnabled(unpinnedOther);
  connect(closeOthersAction, &QAction::triggered, this,
          [this, index]() { closeOthers(index); });

  auto *closeRightAction = menu->addAction(tr("Close to the &Right"));
  closeRightAction->setEnabled(unpinnedRight);
  connect(closeRightAction, &QAction::triggered, this,
          [this, index]() { closeToRight(index); });

  // Closes every unpinned tab in this group; the group collapses if nothing
  // is left (unless it's the last one). Greyed when all tabs are pinned.
  auto *closeGroupAction = menu->addAction(tr("Close &Group"));
  closeGroupAction->setEnabled(hasUnpinnedTabs());
  connect(closeGroupAction, &QAction::triggered, this,
          [this]() { closeAll(); });
}
