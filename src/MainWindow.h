#pragma once

#include <QList>
#include <QMainWindow>
#include <QStringList>

class DocumentView;
class EditorArea;
class PreferencesDialog;
class QAction;
class QMenu;
class QToolButton;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

  // Open in the active editor group. Returns the DocumentView the user is
  // now looking at, or nullptr on load failure.
  DocumentView *openFile(const QString &path);

protected:
  void dragEnterEvent(QDragEnterEvent *e) override;
  void dropEvent(QDropEvent *e) override;

private slots:
  void onOpen();
  void onCloseCurrentTab();
  void onCloseAllInActive();
  void onCloseAllEverywhere();
  void onReopenClosed();
  void onSplitRight();
  void onSplitDown();
  void onNextTab();
  void onPreviousTab();
  void onMoveTabRight();
  void onMoveTabLeft();
  void onCurrentDocumentChanged(DocumentView *doc);
  // Point the View ▸ Outline toggle at `doc`: reflect its outline state and
  // follow its later toggles, dropping the previous document's connection.
  void syncOutlineAction(DocumentView *doc);
  void rebuildRecentMenu();
  void onPreferences();
  void onPreferencesApplied();

  // Refresh enabled/disabled state of context-dependent File-menu actions
  // (close family, reopen, recent) right before the menu is shown.
  void updateFileMenuState();

private:
  // Rebuild the status-bar stylesheet for the current color scheme. The app
  // themes via QStyleHints, not the Qt palette, so the button's text colour
  // has to be re-applied on every scheme change (mirrors TitleBar).
  void refreshStatusBarStyle();

  void loadRecentFiles();
  void saveRecentFiles();
  void addToRecentFiles(const QString &canonical);
  void removeFromRecentFiles(const QString &path);

  EditorArea *m_area = nullptr;
  QToolButton *m_statusButton = nullptr;
  // Native-separator path of the current document, backing the status button's
  // copy / reveal actions. Empty when no file is open.
  QString m_currentPath;
  QMenu *m_recentMenu = nullptr;
  QStringList m_recentFiles;
  PreferencesDialog *m_preferencesDialog = nullptr;

  // Held so updateFileMenuState() can toggle them as context changes.
  QAction *m_closeTabAction = nullptr;
  QAction *m_closeGroupAction = nullptr;
  QAction *m_closeAllAction = nullptr;
  QAction *m_reopenAction = nullptr;

  // View ▸ Outline toggle, kept in sync with the active document's outline.
  // m_outlineConn follows that document's outlineVisibilityChanged.
  QAction *m_outlineAction = nullptr;
  QMetaObject::Connection m_outlineConn;
};
