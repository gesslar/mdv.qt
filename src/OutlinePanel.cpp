#include "OutlinePanel.h"

#include <QAbstractItemView>
#include <QEvent>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHelpEvent>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPalette>
#include <QProxyStyle>
#include <QStyleHints>
#include <QStyleOption>
#include <QStyleOptionViewItem>
#include <QTimer>
#include <QToolButton>
#include <QToolTip>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "ChromeStyle.h"
#include "CodiconFont.h"

namespace {

// The Windows 11 style paints a vertical accent "pill" at the left edge of a
// selected/current item-view row. We already render selection as a full-row
// wash via the stylesheet; the native pill sits at the same left edge as the
// expand chevron (so it collides on flush/root rows) and doesn't exist on other
// platforms at all. Suppressing just this primitive makes selection read the
// same everywhere — the stylesheet still draws the row background, so nothing
// else changes.
class NoSelectionPillStyle : public QProxyStyle {
public:
  using QProxyStyle::QProxyStyle;

  void drawPrimitive(PrimitiveElement pe, const QStyleOption *opt, QPainter *p,
                     const QWidget *w) const override {
    if(pe == QStyle::PE_PanelItemViewItem || pe == QStyle::PE_PanelItemViewRow)
      return;
    QProxyStyle::drawPrimitive(pe, opt, p, w);
  }
};

}  // namespace

OutlinePanel::OutlinePanel(QWidget *parent) : QWidget(parent) {
  setObjectName(QStringLiteral("OutlinePanel"));
  setAttribute(Qt::WA_StyledBackground, true);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  // Header: a section label and a collapse button. Right-click offers the
  // dock-side switch and hide, so the header strip stays uncluttered.
  auto *header = new QWidget(this);
  header->setObjectName(QStringLiteral("outlineHeader"));
  header->setAttribute(Qt::WA_StyledBackground, true);
  header->setContextMenuPolicy(Qt::CustomContextMenu);
  auto *headerLayout = new QHBoxLayout(header);
  headerLayout->setContentsMargins(10, 4, 4, 4);
  headerLayout->setSpacing(0);

  m_title = new QLabel(tr("OUTLINE"), header);
  m_title->setObjectName(QStringLiteral("outlineTitle"));

  m_collapseButton = new QToolButton(header);
  m_collapseButton->setObjectName(QStringLiteral("outlineCollapse"));
  m_collapseButton->setFocusPolicy(Qt::NoFocus);
  m_collapseButton->setFixedSize(24, 24);
  m_collapseButton->setToolTip(tr("Hide outline"));
  if(const QString family = Codicon::family(); !family.isEmpty()) {
    QFont f(family);
    f.setPixelSize(12);
    m_collapseButton->setFont(f);
  }
  connect(m_collapseButton, &QToolButton::clicked, this,
          &OutlinePanel::hideRequested);

  headerLayout->addWidget(m_title);
  headerLayout->addStretch(1);
  headerLayout->addWidget(m_collapseButton);

  connect(header, &QWidget::customContextMenuRequested, this,
          [this, header](const QPoint &pos) {
            QMenu menu(this);
            QAction *move = menu.addAction(m_side == OutlineSide::Left
                                               ? tr("Dock on Right")
                                               : tr("Dock on Left"));
            QAction *hide = menu.addAction(tr("Hide Outline"));
            const QAction *chosen = menu.exec(header->mapToGlobal(pos));
            if(chosen == move) emit moveToOtherSideRequested();
            else if(chosen == hide) emit hideRequested();
          });

  m_tree = new QTreeWidget(this);
  m_tree->setObjectName(QStringLiteral("outlineTree"));
  m_tree->setHeaderHidden(true);
  m_tree->setIndentation(12);
  m_tree->setUniformRowHeights(true);
  m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
  m_tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  m_tree->setExpandsOnDoubleClick(false);
  // Drop the Windows 11 selection accent pill (see NoSelectionPillStyle); the
  // proxy is parented to the tree so it lives exactly as long.
  auto *treeStyle = new NoSelectionPillStyle;
  treeStyle->setParent(m_tree);
  m_tree->setStyle(treeStyle);
  // Watch the viewport so we can show a tooltip on elided rows only.
  m_tree->viewport()->installEventFilter(this);
  connect(m_tree, &QTreeWidget::itemClicked, this,
          [this](QTreeWidgetItem *item, int /*column*/) {
            const QString id = item->data(0, Qt::UserRole).toString();
            if(!id.isEmpty()) emit headingActivated(id);
          });

  m_placeholder = new QLabel(tr("No headings"), this);
  m_placeholder->setObjectName(QStringLiteral("outlinePlaceholder"));
  m_placeholder->setAlignment(Qt::AlignCenter);
  m_placeholder->hide();

  root->addWidget(header);
  root->addWidget(m_tree, 1);
  root->addWidget(m_placeholder, 1);

  setSide(m_side);
  refreshTheme();
  connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
          [this](Qt::ColorScheme) {
            // On Windows colorSchemeChanged fires *before* Qt swaps the
            // palette, so refreshing inline would re-read the old (light)
            // colors. Defer to the next event-loop turn, by which point the
            // palette swap has landed and the read is fresh.
            QTimer::singleShot(0, this, [this] {
              refreshTheme();
            });
          });
}

void OutlinePanel::setSide(OutlineSide side) {
  m_side = side;
  if(m_collapseButton) {
    // The chevron points at the seam we tuck into.
    const char16_t glyph = side == OutlineSide::Left ? Codicon::ChevronLeft
                                                     : Codicon::ChevronRight;
    m_collapseButton->setText(QString(QChar(glyph)));
  }
}

void OutlinePanel::setHeadings(const QList<mdv::Heading> &headings) {
  m_tree->clear();
  m_itemById.clear();

  // Nest by level using a running ancestor stack; skipped levels (h1 → h3)
  // just attach to the nearest shallower ancestor.
  QList<QPair<int, QTreeWidgetItem *>> stack;
  for(const mdv::Heading &h : headings) {
    while(!stack.isEmpty() && stack.last().first >= h.level) stack.removeLast();
    QTreeWidgetItem *parent = stack.isEmpty() ? nullptr : stack.last().second;
    auto *item =
        parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    item->setText(0, h.text);
    item->setData(0, Qt::UserRole, h.id);
    stack.append({h.level, item});
    m_itemById.insert(h.id, item);
  }
  m_tree->expandAll();

  const bool empty = headings.isEmpty();
  m_tree->setVisible(!empty);
  m_placeholder->setVisible(empty);
}

void OutlinePanel::setCurrentHeading(const QString &anchorId) {
  if(anchorId.isEmpty()) {
    m_tree->setCurrentItem(nullptr);
    return;
  }
  const auto it = m_itemById.constFind(anchorId);
  if(it == m_itemById.constEnd()) return;
  if(m_tree->currentItem() == it.value()) return;
  m_tree->setCurrentItem(it.value());
  m_tree->scrollToItem(it.value(), QAbstractItemView::EnsureVisible);
}

void OutlinePanel::refreshTheme() {
  // Colors come from the *application* palette, not this widget's palette():
  // setStyleSheet() below folds its colors back into the widget palette, so
  // reading our own palette would feed on the stylesheet we just set. The app
  // palette is the clean system source, so the panel matches the rest of the
  // chrome and follows accent/scheme changes: Window/WindowText for the surface
  // and rows, PlaceholderText for the muted title/placeholder, a translucent
  // WindowText wash for hover, and the real Highlight for the selected row so
  // selection tracks the accent.
  const QPalette pal = QGuiApplication::palette();
  const QColor txt = pal.color(QPalette::WindowText);
  const QColor place = pal.color(QPalette::PlaceholderText);
  const QString bg = pal.color(QPalette::Window).name();
  const QString fg = txt.name();
  const QString muted = cssRgba(place, place.alphaF());
  const QString hover = cssRgba(txt, 0.07);
  // Selection is a 50%-alpha accent tint, not a solid fill — so the row stays
  // softer and normal WindowText reads better on it than HighlightedText.
  const QString sel = cssRgba(pal.color(QPalette::Highlight), 0.5);

  const QString sheet = QStringLiteral(R"(
OutlinePanel, #outlineHeader { background: %1; }
#outlineTitle { color: %3; font-size: 11px; font-weight: 600; }
QToolButton#outlineCollapse {
    background: transparent; border: none; color: %2; border-radius: 4px;
}
QToolButton#outlineCollapse:hover { background: %4; }
#outlinePlaceholder { color: %3; }
QTreeWidget#outlineTree {
    background: %1; color: %2; border: none; outline: 0;
}
QTreeWidget#outlineTree::item { padding: 3px 2px; border: none; }
QTreeWidget#outlineTree::item:hover { background: %4; }
QTreeWidget#outlineTree::item:selected { background: %5; color: %2; }
)")
                          .arg(bg, fg, muted, hover, sel);
  // Re-applying an identical sheet re-polishes and re-emits PaletteChange,
  // which lands back in changeEvent() — skip the no-op to break that loop.
  if(sheet != styleSheet()) setStyleSheet(sheet);
}

void OutlinePanel::changeEvent(QEvent *e) {
  QWidget::changeEvent(e);
  // The explicit stylesheet would otherwise stay frozen at its old colors.
  // ApplicationPaletteChange is what Linux delivers when the app palette swaps;
  // Windows delivers PaletteChange to the widget instead. Both arrive *after*
  // the palette is updated, so refreshTheme() reads fresh colors either way.
  if(e->type() == QEvent::ApplicationPaletteChange ||
     e->type() == QEvent::PaletteChange)
    refreshTheme();
}

bool OutlinePanel::eventFilter(QObject *obj, QEvent *e) {
  if(obj == m_tree->viewport() && e->type() == QEvent::ToolTip) {
    auto *help = static_cast<QHelpEvent *>(e);
    const QModelIndex idx = m_tree->indexAt(help->pos());
    if(idx.isValid()) {
      // Replicate the view's own elision test so the tooltip appears exactly
      // when the label is truncated. The style gives the text sub-rect inside
      // the (indented, QSS-padded) cell; the delegate then trims a text margin
      // of PM_FocusFrameHMargin+1 per side before eliding. Measuring the full
      // label against that same width matches the draw decision to the pixel —
      // a plain visualRect/sizeHint estimate misses borderline rows.
      const QString text = idx.data(Qt::DisplayRole).toString();
      const QRect cell = m_tree->visualRect(idx);
      QStyleOptionViewItem opt;
      opt.initFrom(m_tree);
      opt.rect = cell;
      opt.features |= QStyleOptionViewItem::HasDisplay;
      opt.text = text;
      QStyle *st = m_tree->style();
      const QRect textRect =
          st->subElementRect(QStyle::SE_ItemViewItemText, &opt, m_tree);
      const int textMargin =
          st->pixelMetric(QStyle::PM_FocusFrameHMargin, &opt, m_tree) + 1;
      const int available = textRect.width() - 2 * textMargin;
      if(opt.fontMetrics.horizontalAdvance(text) > available)
        QToolTip::showText(help->globalPos(), text, m_tree, cell);
      else
        QToolTip::hideText();
    } else {
      QToolTip::hideText();
    }
    return true;  // we own tooltip behaviour for the tree
  }
  return QWidget::eventFilter(obj, e);
}
