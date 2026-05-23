#pragma once

#include <QMainWindow>
#include <QStringList>

class DocumentView;
class EditorArea;
class QMenu;

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  explicit MainWindow(QWidget *parent = nullptr);
  ~MainWindow() override;

  // Open in the active editor pane. Returns the DocumentView the user is
  // now looking at, or nullptr on load failure.
  DocumentView *openFile(const QString &path);

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

private:
  void loadRecentFiles();
  void saveRecentFiles();
  void addToRecentFiles(const QString &canonical);

  EditorArea *m_area = nullptr;
  QMenu *m_recentMenu = nullptr;
  QStringList m_recentFiles;
};
