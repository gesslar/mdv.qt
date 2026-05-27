#include "MainWindow.h"

#include <QAction>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QSettings>
#include <QStatusBar>
#include <QUrl>

#include "ContentTheme.h"
#include "DocumentView.h"
#include "EditorArea.h"
#include "EditorGroup.h"
#include "PreferencesDialog.h"

#ifdef Q_OS_WIN
  #include <QGuiApplication>
  #include <QStyleHints>
  // Older MinGW SDKs predate these names. Values match the Microsoft DWM docs
  // and are stable across SDK versions, so the fallbacks are safe.
  #ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
    #define DWMWA_USE_IMMERSIVE_DARK_MODE 20
  #endif
  #ifndef DWMWA_SYSTEMBACKDROP_TYPE
    #define DWMWA_SYSTEMBACKDROP_TYPE 38
  #endif
  #include <windows.h>
  #include <dwmapi.h>
#endif

namespace {
constexpr int kRecentFilesLimit = 10;
constexpr const char *kRecentFilesKey = "recentFiles";

#ifdef Q_OS_WIN
// Qt windows on Windows don't opt into the Win10/11 dark-mode titlebar by
// default — the system frame stays in the legacy light style even when the
// rest of the desktop is dark. Opt in via DWM so the titlebar tracks the
// active Qt color scheme. Older Win10 builds return failure and the frame
// keeps its prior appearance, so no fallback handling is needed.
//
// Note: users who've enabled "Show accent color on title bars and window
// borders" in Personalization will still see their accent color here — that
// setting overrides immersive dark mode for traditional Win32 chrome and
// can't be bypassed without drawing a custom titlebar.
void applyWindowsChrome(QWidget *w) {
  const HWND hwnd = reinterpret_cast<HWND>(w->winId());
  if(!hwnd) return;

  const BOOL dark =
      QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark
          ? TRUE
          : FALSE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark,
                        sizeof(dark));

  // DWMSBT_MAINWINDOW (Mica). Pre-22H2 Win11 / Win10 return failure and the
  // frame keeps its solid background. Without making the client area
  // transparent the effect is confined to the titlebar/border, but that
  // alone is a noticeable upgrade over the flat solid color.
  const int backdrop = 2;
  DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop,
                        sizeof(backdrop));
}
#endif
}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(tr("mdv"));
  resize(900, 600);

  m_area = new EditorArea(this);
  setCentralWidget(m_area);
  connect(m_area, &EditorArea::currentDocumentChanged, this,
          &MainWindow::onCurrentDocumentChanged);
  connect(m_area, &EditorArea::filesDropped, this,
          [this](const QStringList &paths) {
            for(const QString &p : paths) openFile(p);
          });
  // A local-file link clicked in a document opens as a new tab (and lands in
  // Recent Files) through the same path as any other open.
  connect(m_area, &EditorArea::openFileRequested, this,
          [this](const QString &path) {
            openFile(path);
          });

  // Catch-all for drops that miss the EditorArea (menubar, status bar,
  // any chrome). Drops anywhere on the window get routed to the
  // currently active group via openFile().
  setAcceptDrops(true);

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

  m_closeTabAction = fileMenu->addAction(tr("&Close Tab"));
  m_closeTabAction->setShortcut(QKeySequence::Close);
  connect(m_closeTabAction, &QAction::triggered, this,
          &MainWindow::onCloseCurrentTab);

  m_closeGroupAction = fileMenu->addAction(tr("Close All in &Group"));
  m_closeGroupAction->setShortcut(QKeySequence(tr("Ctrl+Shift+W")));
  connect(m_closeGroupAction, &QAction::triggered, this,
          &MainWindow::onCloseAllInActive);

  m_closeAllAction = fileMenu->addAction(tr("Close &All Tabs"));
  m_closeAllAction->setShortcut(QKeySequence(tr("Ctrl+Alt+Shift+W")));
  connect(m_closeAllAction, &QAction::triggered, this,
          &MainWindow::onCloseAllEverywhere);

  m_reopenAction = fileMenu->addAction(tr("Reopen Closed &Tab"));
  m_reopenAction->setShortcut(QKeySequence(tr("Ctrl+Shift+T")));
  connect(m_reopenAction, &QAction::triggered, this,
          &MainWindow::onReopenClosed);

  fileMenu->addSeparator();

  auto *prefsAction = fileMenu->addAction(tr("&Preferences..."));
  prefsAction->setShortcut(QKeySequence::Preferences);
  connect(prefsAction, &QAction::triggered, this, &MainWindow::onPreferences);

  fileMenu->addSeparator();

  auto *quitAction = fileMenu->addAction(tr("&Quit"));
  quitAction->setShortcut(QKeySequence::Quit);
  connect(quitAction, &QAction::triggered, this, &QWidget::close);

  // Refresh the context-dependent actions (close family, reopen, recent) each
  // time the menu opens, so no-op items show greyed instead of clickable.
  connect(fileMenu, &QMenu::aboutToShow, this,
          &MainWindow::updateFileMenuState);
  // Also refresh on document changes: disabling a QAction disables its
  // shortcut too, so if we only updated on menu-open, Ctrl+W could stay dead
  // after opening a file until the File menu was next shown. This keeps the
  // shortcuts in step with the actual open/close/switch state.
  connect(m_area, &EditorArea::currentDocumentChanged, this,
          &MainWindow::updateFileMenuState);
  // Pin/unpin doesn't change the current document, but it flips whether the
  // bulk closes have anything to act on — refresh on it too.
  connect(m_area, &EditorArea::pinStateChanged, this,
          &MainWindow::updateFileMenuState);

  loadRecentFiles();
  rebuildRecentMenu();

  auto *viewMenu = menuBar()->addMenu(tr("&View"));

  auto *splitRightAction = viewMenu->addAction(tr("Split &Right"));
  splitRightAction->setShortcut(QKeySequence(tr("Ctrl+\\")));
  connect(splitRightAction, &QAction::triggered, this,
          &MainWindow::onSplitRight);

  auto *splitDownAction = viewMenu->addAction(tr("Split &Down"));
  splitDownAction->setShortcut(QKeySequence(tr("Ctrl+Shift+\\")));
  connect(splitDownAction, &QAction::triggered, this, &MainWindow::onSplitDown);

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

  // Persistent status text lives in a normal status-bar widget, not a
  // showMessage() temporary. Hovering a menu posts an (empty) status-tip
  // message that would overwrite a temporary and never restore it; a normal
  // widget is merely hidden under any temporary message and reappears once
  // the menu closes.
  m_statusLabel = new QLabel(tr("No file open."), this);
  statusBar()->addWidget(m_statusLabel);

#ifdef Q_OS_WIN
  // winId() forces native HWND creation so the DWM calls have something to
  // target. Reapply on system theme switches so the titlebar tracks live.
  (void)winId();
  applyWindowsChrome(this);
  connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
          [this](Qt::ColorScheme) { applyWindowsChrome(this); });
#endif
}

MainWindow::~MainWindow() = default;

void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
  if(!e->mimeData()->hasUrls()) return;
  for(const QUrl &url : e->mimeData()->urls()) {
    if(url.isLocalFile()) {
      e->acceptProposedAction();
      return;
    }
  }
}

void MainWindow::dropEvent(QDropEvent *e) {
  if(!e->mimeData()->hasUrls()) return;
  // Only accept the drop if we actually opened something. Accepting on
  // a Move action when no local files were present can cause the drag
  // source to remove the originals — drag-source-side will think we
  // took ownership.
  bool accepted = false;
  for(const QUrl &url : e->mimeData()->urls()) {
    if(url.isLocalFile()) {
      openFile(url.toLocalFile());
      accepted = true;
    }
  }
  if(accepted) e->acceptProposedAction();
}

void MainWindow::onOpen() {
  const QStringList paths = QFileDialog::getOpenFileNames(
      this, tr("Open Markdown Files"), QString(),
      tr("Markdown files (*.md *.markdown *.mkd);;All files (*)"));

  for(const QString &path : paths) openFile(path);
}

DocumentView *MainWindow::openFile(const QString &path) {
  DocumentView *doc = m_area->openFile(path);
  if(doc) {
    addToRecentFiles(doc->filePath());
    return doc;
  }

  // Open failed. If the file is simply gone (vs. a transient read error on a
  // file that still exists), drop the dead entry from the recent list. Recent
  // entries are stored in the canonical-else-absolute form DocumentView uses,
  // so normalize `path` the same way before matching — a relative or CLI path
  // otherwise won't match the canonical stored string. canonicalFilePath() is
  // empty for a missing file, so the absolute form is what matches here.
  const QFileInfo info(path);
  if(!info.exists()) {
    QString stored = info.canonicalFilePath();
    if(stored.isEmpty()) stored = info.absoluteFilePath();
    removeFromRecentFiles(stored);
  }
  return doc;
}

void MainWindow::onCloseCurrentTab() {
  EditorGroup *group = m_area->activeGroup();
  if(!group || group->count() == 0) return;
  group->closeTab(group->currentIndex());
}

void MainWindow::onCloseAllInActive() { m_area->closeAllInActive(); }

void MainWindow::onCloseAllEverywhere() { m_area->closeAllEverywhere(); }

void MainWindow::onReopenClosed() { m_area->reopenLastClosed(); }

void MainWindow::updateFileMenuState() {
  EditorGroup *active = m_area->activeGroup();

  // Close Tab closes the current tab explicitly — works even if it's pinned,
  // so it only needs a tab present.
  m_closeTabAction->setEnabled(active && active->count() > 0);

  // The bulk closes skip pinned tabs, so they're no-ops (greyed) unless there's
  // an unpinned tab to act on — in the active group, and anywhere,
  // respectively.
  m_closeGroupAction->setEnabled(active && active->hasUnpinnedTabs());
  m_closeAllAction->setEnabled(m_area->hasUnpinnedTabs());

  // Reopen needs something on the closed stack; Recent needs a non-empty list
  // (the submenu greys out as a whole rather than opening onto nothing).
  m_reopenAction->setEnabled(m_area->hasClosedTabs());
  m_recentMenu->menuAction()->setEnabled(!m_recentFiles.isEmpty());
}

void MainWindow::onPreferences() {
  if(!m_preferencesDialog) {
    m_preferencesDialog = new PreferencesDialog(this);
    connect(m_preferencesDialog, &PreferencesDialog::preferencesApplied, this,
            &MainWindow::onPreferencesApplied);
  }
  // Always reload from settings before opening. Without this, edits the
  // user made and then Cancelled would persist in the dialog's widgets
  // and surface again next time it opens.
  m_preferencesDialog->reload();
  m_preferencesDialog->exec();
}

void MainWindow::onPreferencesApplied() {
  // Theme name may have changed in QSettings; pull the new values into
  // the singleton, then push the resulting stylesheet to every open
  // DocumentView (they each re-render to pick up the new rules).
  ContentTheme::active().reload();
  const auto docs = findChildren<DocumentView *>();
  for(DocumentView *doc : docs) doc->refresh();
}

void MainWindow::onSplitRight() { m_area->splitActive(EditorArea::Right); }

void MainWindow::onSplitDown() { m_area->splitActive(EditorArea::Bottom); }

void MainWindow::onNextTab() {
  if(auto *group = m_area->activeGroup()) group->nextTab();
}

void MainWindow::onPreviousTab() {
  if(auto *group = m_area->activeGroup()) group->previousTab();
}

void MainWindow::onMoveTabRight() {
  if(auto *group = m_area->activeGroup()) group->moveCurrentTabRight();
}

void MainWindow::onMoveTabLeft() {
  if(auto *group = m_area->activeGroup()) group->moveCurrentTabLeft();
}

void MainWindow::onCurrentDocumentChanged(DocumentView *doc) {
  if(!doc) {
    setWindowTitle(tr("mdv"));
    m_statusLabel->setText(tr("No file open."));
    return;
  }
  setWindowTitle(tr("%1 — mdv").arg(doc->displayName()));
  m_statusLabel->setText(doc->filePath());
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
  if(canonical.isEmpty()) return;
  m_recentFiles.removeAll(canonical);
  m_recentFiles.prepend(canonical);
  while(m_recentFiles.size() > kRecentFilesLimit) m_recentFiles.removeLast();
  saveRecentFiles();
  // No explicit rebuildRecentMenu() here — the submenu's aboutToShow
  // handler rebuilds on demand.
}

void MainWindow::removeFromRecentFiles(const QString &path) {
  // aboutToShow rebuilds the submenu, so a save is all that's needed here.
  if(m_recentFiles.removeAll(path) > 0) saveRecentFiles();
}

void MainWindow::rebuildRecentMenu() {
  m_recentMenu->clear();

  // When empty, the "Open Recent" parent is greyed by updateFileMenuState(),
  // so there's nothing to populate here — no "(none)" placeholder needed.
  if(m_recentFiles.isEmpty()) return;

  auto *reopenAllAction = m_recentMenu->addAction(tr("Reopen &All"));
  connect(reopenAllAction, &QAction::triggered, this, [this]() {
    // Snapshot before iterating; openFile() can mutate m_recentFiles via
    // addToRecentFiles(). Walk in reverse (oldest → most-recent) so the
    // most-recent file ends up as the last tab opened, and therefore
    // the active one.
    const QStringList files = m_recentFiles;
    for(auto it = files.rbegin(); it != files.rend(); ++it) {
      if(QFileInfo::exists(*it)) openFile(*it);
    }
  });

  m_recentMenu->addSeparator();

  for(const QString &path : std::as_const(m_recentFiles)) {
    const QFileInfo info(path);
    auto *action = m_recentMenu->addAction(info.fileName());
    action->setToolTip(path);
    connect(action, &QAction::triggered, this, [this, path]() {
      openFile(path);
    });
  }

  m_recentMenu->addSeparator();
  auto *clearAction = m_recentMenu->addAction(tr("&Clear Recently Opened"));
  connect(clearAction, &QAction::triggered, this, [this]() {
    m_recentFiles.clear();
    saveRecentFiles();
    rebuildRecentMenu();
  });
}
