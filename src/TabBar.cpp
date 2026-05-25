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
#include "EditorPane.h"

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
  auto *pane = reinterpret_cast<EditorPane *>(ptr);
  if (!pane) return false;
  auto *doc = pane->documentAt(idx);
  return doc && doc->isPinned();
}

void TabBar::mousePressEvent(QMouseEvent *e) {
  if (e->button() == Qt::LeftButton) {
    m_pressIndex = tabAt(e->pos());
    m_pressPos = e->pos();
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

  startCrossPaneDrag(m_pressIndex);
}

void TabBar::mouseReleaseEvent(QMouseEvent *e) {
  m_pressIndex = -1;
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

  auto *source = reinterpret_cast<EditorPane *>(ptr);
  auto *target = qobject_cast<EditorPane *>(parentWidget());
  if (!source || !target) return;
  if (source == target) return;  // same-pane tab-bar drop is a no-op

  DocumentView *doc = source->takeDocument(idx);
  if (!doc) return;

  target->addDocument(doc);
  e->acceptProposedAction();
}

void TabBar::startCrossPaneDrag(int tabIndex) {
  auto *pane = qobject_cast<EditorPane *>(parentWidget());
  if (!pane) return;

  QByteArray payload;
  {
    QDataStream ds(&payload, QIODevice::WriteOnly);
    ds << qintptr(pane) << qint32(tabIndex);
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
}
