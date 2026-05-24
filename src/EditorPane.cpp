#include "EditorPane.h"

#include <QAction>
#include <QDataStream>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
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

  m_dropOverlay = new DropOverlay(this);
}

int EditorPane::addDocument(DocumentView *doc) {
  // Relay link-driven opens up toward the window. UniqueConnection because a
  // tab can pass through addDocument again when dragged between panes.
  connect(doc, &DocumentView::openFileRequested, this,
          &EditorPane::openFileRequested, Qt::UniqueConnection);

  const int idx = addTab(doc, doc->displayName());
  setTabToolTip(idx, doc->filePath());
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
  // Close from the end so indices stay valid as we walk; equivalent to
  // closing index 0 repeatedly but avoids the small cost of QTabWidget
  // recomputing current-tab on each close.
  while (count() > 0) onTabCloseRequested(count() - 1);
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
}
