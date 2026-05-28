#include "OutlinePanel.h"

#include <QAbstractItemView>
#include <QFont>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QStyleHints>
#include <QToolButton>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QVBoxLayout>

#include "CodiconFont.h"

namespace {
bool isDark() {
  return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}
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
            refreshTheme();
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
  const bool dark = isDark();
  const QString bg =
      dark ? QStringLiteral("#1f1f1f") : QStringLiteral("#f3f3f3");
  const QString fg =
      dark ? QStringLiteral("#e8e8e8") : QStringLiteral("#1a1a1a");
  const QString muted =
      dark ? QStringLiteral("#9d9d9d") : QStringLiteral("#6e6e6e");
  const QString hover = dark ? QStringLiteral("rgba(255,255,255,0.07)")
                             : QStringLiteral("rgba(0,0,0,0.05)");
  const QString sel = dark ? QStringLiteral("rgba(255,255,255,0.12)")
                           : QStringLiteral("rgba(0,0,0,0.10)");

  setStyleSheet(QStringLiteral(R"(
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
                    .arg(bg, fg, muted, hover, sel));
}
