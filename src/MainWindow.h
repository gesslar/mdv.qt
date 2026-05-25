#pragma once

#include <QMainWindow>
#include <QStringList>

class DocumentView;
class EditorArea;
class PreferencesDialog;
class QAction;
class QLabel;
class QMenu;

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
  void rebuildRecentMenu();
  void onPreferences();
  void onPreferencesApplied();

  // Refresh enabled/disabled state of context-dependent File-menu actions
  // (close family, reopen, recent) right before the menu is shown.
  void updateFileMenuState();

private:
  void loadRecentFiles();
  void saveRecentFiles();
  void addToRecentFiles(const QString &canonical);
  void removeFromRecentFiles(const QString &path);

  EditorArea *m_area = nullptr;
  QLabel *m_statusLabel = nullptr;
  QMenu *m_recentMenu = nullptr;
  QStringList m_recentFiles;
  PreferencesDialog *m_preferencesDialog = nullptr;

  // Held so updateFileMenuState() can toggle them as context changes.
  QAction *m_closeTabAction = nullptr;
  QAction *m_closeGroupAction = nullptr;
  QAction *m_closeAllAction = nullptr;
  QAction *m_reopenAction = nullptr;
};
