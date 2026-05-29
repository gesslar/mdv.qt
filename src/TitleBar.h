#pragma once

#include <QWidget>

class QLabel;
class QMenu;
class QToolButton;

// Custom titlebar for the QWindowKit frameless window. Layout (l→r):
//   [icon] [File ▾] [View ▾]   …drag…   [⚙] [─] [▢] [✕]
// MainWindow constructs this, hands the File/View QMenus over via the setters,
// and registers the three rightmost buttons with the QWK agent as the system
// buttons so snap layouts, hover states and click handling work natively.
class TitleBar : public QWidget {
  Q_OBJECT

public:
  explicit TitleBar(QWidget *parent = nullptr);

  void setFileMenu(QMenu *menu);
  void setViewMenu(QMenu *menu);

  QToolButton *fileButton() const { return m_fileBtn; }
  QToolButton *viewButton() const { return m_viewBtn; }
  QToolButton *settingsButton() const { return m_settingsBtn; }
  QToolButton *minButton() const { return m_minBtn; }
  QToolButton *maxButton() const { return m_maxBtn; }
  QToolButton *closeButton() const { return m_closeBtn; }

signals:
  void settingsClicked();
  void minimizeClicked();
  void maximizeClicked();
  void closeClicked();

protected:
  bool eventFilter(QObject *obj, QEvent *e) override;
  void showEvent(QShowEvent *e) override;
  // Re-pull the stylesheet when the system palette shifts (accent or
  // light/dark), so the bar keeps belonging to the desktop around it.
  void changeEvent(QEvent *e) override;

private:
  void refreshMaxIcon();
  void refreshTheme();

  QLabel *m_iconLabel = nullptr;
  QToolButton *m_fileBtn = nullptr;
  QToolButton *m_viewBtn = nullptr;
  QToolButton *m_settingsBtn = nullptr;
  QToolButton *m_minBtn = nullptr;
  QToolButton *m_maxBtn = nullptr;
  QToolButton *m_closeBtn = nullptr;
};
