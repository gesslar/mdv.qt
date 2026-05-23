#pragma once

#include <QPoint>
#include <QTabBar>

// QTabBar subclass that starts a cross-pane QDrag once the cursor leaves
// the bar's geometry. Within the bar, the parent QTabBar's built-in
// move-within-bar gesture continues to work normally.
class TabBar : public QTabBar {
  Q_OBJECT

public:
  explicit TabBar(QWidget *parent = nullptr);

  static const char *mimeType();  // "application/x-mdv-tab"

protected:
  void mousePressEvent(QMouseEvent *e) override;
  void mouseMoveEvent(QMouseEvent *e) override;
  void mouseReleaseEvent(QMouseEvent *e) override;
  void dragEnterEvent(QDragEnterEvent *e) override;
  void dragMoveEvent(QDragMoveEvent *e) override;
  void dropEvent(QDropEvent *e) override;

private:
  void startCrossPaneDrag(int tabIndex);

  int m_pressIndex = -1;
  QPoint m_pressPos;
  bool m_dragInFlight = false;
};
