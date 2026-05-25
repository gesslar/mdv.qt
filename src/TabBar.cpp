#include "TabBar.h"

#include <QApplication>
#include <QDataStream>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPixmap>

#include "DocumentView.h"
#include "EditorGroup.h"

TabBar::TabBar(QWidget *parent) : QTabBar(parent) { setAcceptDrops(true); }

const char *TabBar::mimeType() { return "application/x-mdv-tab"; }

bool TabBar::draggedTabIsPinned(const QMimeData *mime) {
  if (!mime || !mime->hasFormat(mimeType())) return false;
  QByteArray payload = mime->data(mimeType());
  QDataStream ds(payload);
  qintptr ptr = 0;
  qint32 idx = -1;
  ds >> ptr >> idx;
  if (ds.status() != QDataStream::Ok) return false;
  auto *group = reinterpret_cast<EditorGroup *>(ptr);
  if (!group) return false;
  auto *doc = group->documentAt(idx);
  return doc && doc->isPinned();
}

void TabBar::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton) {
    m_pressIndex = tabAt(e->pos());
    m_pressPos = e->pos();
    // Capture the pressed tab's page widget (identity) — within-bar reordering
    // can shift its index before a cross-group drag begins, so we recompute the
    // live index from this rather than trusting the press-time index.
    auto *group = qobject_cast<EditorGroup *>(parentWidget());
    m_pressTab = group ? group->widget(m_pressIndex) : nullptr;
  }
  QTabBar::mousePressEvent(e);
}

void TabBar::mouseMoveEvent(QMouseEvent *e) {
  if (m_dragInFlight) return;

  // Defer to built-in handling unless we're tracking a left-button drag
  // that started on a real tab.
  if (!(e->buttons() & Qt::LeftButton) || m_pressIndex < 0) {
    QTabBar::mouseMoveEvent(e);
    return;
  }

  // Inside the bar — let QTabBar handle move-within-bar reordering.
  if (rect().contains(e->pos())) {
    QTabBar::mouseMoveEvent(e);
    return;
  }

  // Outside, but only past the start-drag threshold.
  if ((e->pos() - m_pressPos).manhattanLength() <
      QApplication::startDragDistance()) {
    return;
  }

  // Recompute the live index from the captured tab — QTabBar may have reordered
  // it within the bar since the press, leaving m_pressIndex stale.
  auto *group = qobject_cast<EditorGroup *>(parentWidget());
  const int idx = group ? group->indexOf(m_pressTab) : -1;
  if (idx < 0) return;
  startCrossGroupDrag(idx);
}

void TabBar::mouseReleaseEvent(QMouseEvent *e) {
  m_pressIndex = -1;
  m_pressTab = nullptr;
  QTabBar::mouseReleaseEvent(e);
}

void TabBar::dragEnterEvent(QDragEnterEvent *e) {
  if (e->mimeData()->hasFormat(mimeType())) e->acceptProposedAction();
}

void TabBar::dragMoveEvent(QDragMoveEvent *e) {
  if (!e->mimeData()->hasFormat(mimeType())) return;
  if (draggedTabIsPinned(e->mimeData())) {
    e->ignore();  // pinned tabs can't change groups — forbidden cursor
    return;
  }
  e->acceptProposedAction();
}

void TabBar::dropEvent(QDropEvent *e) {
  if (!e->mimeData()->hasFormat(mimeType())) return;
  if (draggedTabIsPinned(e->mimeData())) return;  // pinned: locked to its group

  QByteArray payload = e->mimeData()->data(mimeType());
  QDataStream ds(payload);
  qintptr ptr = 0;
  qint32 idx = -1;
  ds >> ptr >> idx;
  if (ds.status() != QDataStream::Ok) return;

  auto *source = reinterpret_cast<EditorGroup *>(ptr);
  auto *target = qobject_cast<EditorGroup *>(parentWidget());
  if (!source || !target) return;
  if (source == target) return;  // same-group tab-bar drop is a no-op

  DocumentView *doc = source->takeDocument(idx);
  if (!doc) return;

  target->addDocument(doc);
  e->acceptProposedAction();
}

void TabBar::startCrossGroupDrag(int tabIndex) {
  auto *group = qobject_cast<EditorGroup *>(parentWidget());
  if (!group) return;

  QByteArray payload;
  {
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds << qintptr(group) << qint32(tabIndex);
  }

  auto *mime = new QMimeData;
  mime->setData(mimeType(), payload);

  auto *drag = new QDrag(this);
  drag->setMimeData(mime);

  // A small pixmap of the tab being dragged — gives the cursor something
  // visually meaningful to carry.
  const QRect r = tabRect(tabIndex);
  if (r.isValid()) {
    QPixmap pix = grab(r);
    drag->setPixmap(pix);
    drag->setHotSpot(QPoint(pix.width() / 2, pix.height() / 2));
  }

  m_dragInFlight = true;
  drag->exec(Qt::MoveAction);
  m_dragInFlight = false;
  m_pressIndex = -1;
  m_pressTab = nullptr;
}
