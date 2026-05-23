#include "MainWindow.h"

#include <QAction>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QStatusBar>

#include "DocumentView.h"
#include "EditorArea.h"
#include "EditorPane.h"

namespace {
constexpr int kRecentFilesLimit = 10;
constexpr const char *kRecentFilesKey = "recentFiles";
}

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(tr("mdv"));
  resize(900, 600);

  m_area = new EditorArea(this);
  setCentralWidget(m_area);
  connect(m_area, &EditorArea::currentDocumentChanged, this,
          &MainWindow::onCurrentDocumentChanged);

  auto *fileMenu = menuBar()->addMenu(tr("&File"));

  auto *openAction = fileMenu->addAction(tr("&Open..."));
  openAction->setShortcut(QKeySequence::Open);
  connect(openAction, &QAction::triggered, this, &MainWindow::onOpen);

  m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
  // Rebuild on demand so the menu reflects the current list even if it
  // was changed via openFile / Clear since the menu was last shown.
  connect(m_recentMenu, &QMenu::aboutToShow, this,
          &MainWindow::rebuildRecentMenu);

  fileMenu->addSeparator();

  auto *closeAction = fileMenu->addAction(tr("&Close Tab"));
  closeAction->setShortcut(QKeySequence::Close);
  connect(closeAction, &QAction::triggered, this,
          &MainWindow::onCloseCurrentTab);

  auto *closePaneAction = fileMenu->addAction(tr("Close All in &Pane"));
  closePaneAction->setShortcut(QKeySequence(tr("Ctrl+Shift+W")));
  connect(closePaneAction, &QAction::triggered, this,
          &MainWindow::onCloseAllInActive);

  auto *closeAllAction = fileMenu->addAction(tr("Close &All Tabs"));
  closeAllAction->setShortcut(QKeySequence(tr("Ctrl+Alt+Shift+W")));
  connect(closeAllAction, &QAction::triggered, this,
          &MainWindow::onCloseAllEverywhere);

  auto *reopenAction = fileMenu->addAction(tr("Reopen Closed &Tab"));
  reopenAction->setShortcut(QKeySequence(tr("Ctrl+Shift+T")));
  connect(reopenAction, &QAction::triggered, this, &MainWindow::onReopenClosed);

  fileMenu->addSeparator();

  auto *quitAction = fileMenu->addAction(tr("&Quit"));
  quitAction->setShortcut(QKeySequence::Quit);
  connect(quitAction, &QAction::triggered, this, &QWidget::close);

  loadRecentFiles();
  rebuildRecentMenu();

  auto *viewMenu = menuBar()->addMenu(tr("&View"));

  auto *splitRightAction = viewMenu->addAction(tr("Split &Right"));
  splitRightAction->setShortcut(QKeySequence(tr("Ctrl+\\")));
  connect(splitRightAction, &QAction::triggered, this,
          &MainWindow::onSplitRight);

  auto *splitDownAction = viewMenu->addAction(tr("Split &Down"));
  splitDownAction->setShortcut(QKeySequence(tr("Ctrl+Shift+\\")));
  connect(splitDownAction, &QAction::triggered, this,
          &MainWindow::onSplitDown);

  // Tab navigation / reordering shortcuts. No menu items — these are
  // pure keyboard affordances that everybody-expects. addAction() puts
  // them in scope for the window's shortcut context without showing them
  // anywhere visible.
  auto bindShortcut = [this](const QString &name, const QString &keys,
                             void (MainWindow::*slot)()) {
    auto *action = new QAction(name, this);
    action->setShortcut(QKeySequence(keys));
    connect(action, &QAction::triggered, this, slot);
    addAction(action);
  };
  bindShortcut(tr("Next Tab"), tr("Ctrl+PgDown"), &MainWindow::onNextTab);
  bindShortcut(tr("Previous Tab"), tr("Ctrl+PgUp"), &MainWindow::onPreviousTab);
  bindShortcut(tr("Move Tab Right"), tr("Ctrl+Shift+PgDown"),
               &MainWindow::onMoveTabRight);
  bindShortcut(tr("Move Tab Left"), tr("Ctrl+Shift+PgUp"),
               &MainWindow::onMoveTabLeft);

  statusBar()->showMessage(tr("No file open."));
}

MainWindow::~MainWindow() = default;

void MainWindow::onOpen() {
  const QStringList paths = QFileDialog::getOpenFileNames(
      this, tr("Open Markdown Files"), QString(),
      tr("Markdown files (*.md *.markdown *.mkd);;All files (*)"));

  for (const QString &path : paths) openFile(path);
}

DocumentView *MainWindow::openFile(const QString &path) {
  DocumentView *doc = m_area->openFile(path);
  if (doc) addToRecentFiles(doc->filePath());
  return doc;
}

void MainWindow::onCloseCurrentTab() {
  EditorPane *pane = m_area->activePane();
  if (!pane || pane->count() == 0) return;
  pane->closeTab(pane->currentIndex());
}

void MainWindow::onCloseAllInActive() { m_area->closeAllInActive(); }

void MainWindow::onCloseAllEverywhere() { m_area->closeAllEverywhere(); }

void MainWindow::onReopenClosed() { m_area->reopenLastClosed(); }

void MainWindow::onSplitRight() {
  m_area->splitActive(EditorArea::Right);
}

void MainWindow::onSplitDown() {
  m_area->splitActive(EditorArea::Bottom);
}

void MainWindow::onNextTab() {
  if (auto *pane = m_area->activePane()) pane->nextTab();
}

void MainWindow::onPreviousTab() {
  if (auto *pane = m_area->activePane()) pane->previousTab();
}

void MainWindow::onMoveTabRight() {
  if (auto *pane = m_area->activePane()) pane->moveCurrentTabRight();
}

void MainWindow::onMoveTabLeft() {
  if (auto *pane = m_area->activePane()) pane->moveCurrentTabLeft();
}

void MainWindow::onCurrentDocumentChanged(DocumentView *doc) {
  if (!doc) {
    setWindowTitle(tr("mdv"));
    statusBar()->showMessage(tr("No file open."));
    return;
  }
  setWindowTitle(tr("%1 — mdv").arg(doc->displayName()));
  statusBar()->showMessage(doc->filePath());
}

void MainWindow::loadRecentFiles() {
  QSettings s;
  m_recentFiles = s.value(kRecentFilesKey).toStringList();
}

void MainWindow::saveRecentFiles() {
  QSettings s;
  s.setValue(kRecentFilesKey, m_recentFiles);
}

void MainWindow::addToRecentFiles(const QString &canonical) {
  if (canonical.isEmpty()) return;
  m_recentFiles.removeAll(canonical);
  m_recentFiles.prepend(canonical);
  while (m_recentFiles.size() > kRecentFilesLimit) m_recentFiles.removeLast();
  saveRecentFiles();
  // No explicit rebuildRecentMenu() here — the submenu's aboutToShow
  // handler rebuilds on demand.
}

void MainWindow::rebuildRecentMenu() {
  m_recentMenu->clear();

  if (m_recentFiles.isEmpty()) {
    auto *empty = m_recentMenu->addAction(tr("(none)"));
    empty->setEnabled(false);
    return;
  }

  auto *reopenAllAction = m_recentMenu->addAction(tr("Reopen &All"));
  connect(reopenAllAction, &QAction::triggered, this, [this]() {
    // Snapshot before iterating; openFile() can mutate m_recentFiles via
    // addToRecentFiles(). Walk in reverse (oldest → most-recent) so the
    // most-recent file ends up as the last tab opened, and therefore
    // the active one.
    const QStringList files = m_recentFiles;
    for (auto it = files.rbegin(); it != files.rend(); ++it) {
      if (QFileInfo::exists(*it)) openFile(*it);
    }
  });

  m_recentMenu->addSeparator();

  for (const QString &path : std::as_const(m_recentFiles)) {
    const QFileInfo info(path);
    auto *action = m_recentMenu->addAction(info.fileName());
    action->setToolTip(path);
    connect(action, &QAction::triggered, this,
            [this, path]() { openFile(path); });
  }

  m_recentMenu->addSeparator();
  auto *clearAction = m_recentMenu->addAction(tr("&Clear Recently Opened"));
  connect(clearAction, &QAction::triggered, this, [this]() {
    m_recentFiles.clear();
    saveRecentFiles();
    rebuildRecentMenu();
  });
}
