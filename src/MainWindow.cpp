#include "MainWindow.h"

#include <QAction>
#include <QClipboard>
#include <QDesktopServices>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QMimeData>
#include <QProcess>
#include <QSettings>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QStyleHints>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>

#include "ContentTheme.h"
#include "DocumentView.h"
#include "EditorArea.h"
#include "EditorGroup.h"
#include "PreferencesDialog.h"

#include <QWKWidgets/widgetwindowagent.h>

#include "TitleBar.h"

namespace {
constexpr int kRecentFilesLimit = 10;
constexpr const char *kRecentFilesKey = "recentFiles";

// Open the platform file manager with the file highlighted. Windows/macOS can
// select the file itself; elsewhere we fall back to opening its folder.
void revealInFileManager(const QString &path) {
  const QFileInfo info(path);
#if defined(Q_OS_WIN)
  QProcess::startDetached(QStringLiteral("explorer.exe"),
                          {QStringLiteral("/select,") +
                           QDir::toNativeSeparators(info.absoluteFilePath())});
#elif defined(Q_OS_MACOS)
  QProcess::startDetached(QStringLiteral("open"),
                          {QStringLiteral("-R"), info.absoluteFilePath()});
#else
  QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
#endif
}
}  // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
  setWindowTitle(tr("mdv"));
  resize(900, 600);

  // m_area is placed into a container central widget at the end of this
  // constructor, alongside the custom title bar.
  m_area = new EditorArea(this);
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

  // No QMenuBar in the window's chrome. The File and View QMenus get
  // attached to the title bar's QToolButtons later in this constructor.
  auto *fileMenu = new QMenu(tr("&File"), this);

  auto *openAction = fileMenu->addAction(tr("&Open..."));
  openAction->setShortcut(QKeySequence::Open);
  connect(openAction, &QAction::triggered, this, &MainWindow::onOpen);

  m_recentMenu = fileMenu->addMenu(tr("Open &Recent"));
  // QMenu suppresses per-action tooltips by default; opt in so each entry's
  // full path tooltip actually shows.
  m_recentMenu->setToolTipsVisible(true);
  // Rebuild on demand so the menu reflects the current list even if it
  // was changed via openFile / Clear since the menu was last shown.
  connect(m_recentMenu, &QMenu::aboutToShow, this,
          &MainWindow::rebuildRecentMenu);

  fileMenu->addSeparator();

  m_closeTabAction = fileMenu->addAction(tr("&Close Tab"));
  // Keep the platform-standard Close keys (Ctrl+F4 on Windows) and add Ctrl+W,
  // which Windows doesn't bind by default but everyone reaches for.
  QList<QKeySequence> closeKeys =
      QKeySequence::keyBindings(QKeySequence::Close);
  if(const QKeySequence ctrlW(Qt::CTRL | Qt::Key_W); !closeKeys.contains(ctrlW))
    closeKeys.append(ctrlW);
  m_closeTabAction->setShortcuts(closeKeys);
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

  // Preferences action isn't in the menu — the title bar's settings cog is
  // the entry point. The Ctrl+, shortcut still works because it's bound on
  // the action below (added directly to the window) without needing a menu
  // entry. (Pure-shortcut actions are how tab navigation is wired too — see
  // bindShortcut() further down.)
  auto *prefsAction = new QAction(tr("&Preferences..."), this);
  prefsAction->setShortcut(QKeySequence::Preferences);
  connect(prefsAction, &QAction::triggered, this, &MainWindow::onPreferences);
  addAction(prefsAction);

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

  auto *viewMenu = new QMenu(tr("&View"), this);

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
  m_statusButton = new QToolButton(this);
  m_statusButton->setText(tr("No file open."));
  m_statusButton->setEnabled(false);
  m_statusButton->setAutoRaise(true);
  m_statusButton->setFocusPolicy(Qt::NoFocus);
  m_statusButton->setCursor(Qt::PointingHandCursor);
  m_statusButton->setContextMenuPolicy(Qt::CustomContextMenu);
  // The Windows style draws a sunken frame around status-bar items; clear it
  // so the button sits flush. The button itself is flat with a theme-agnostic
  // grey hover wash (matching the tab buttons) and a little left pad.
  refreshStatusBarStyle();
  connect(QGuiApplication::styleHints(), &QStyleHints::colorSchemeChanged, this,
          [this](Qt::ColorScheme) {
            refreshStatusBarStyle();
            // When following the system scheme, the active content theme
            // tracks it — reload and re-render every open document.
            if(QSettings()
                   .value(QStringLiteral("theme/followSystem"), false)
                   .toBool()) {
              onPreferencesApplied();
            }
          });
  statusBar()->addWidget(m_statusButton);

  // Left-click copies the full path; the temporary status message reappears as
  // the button once it times out.
  connect(m_statusButton, &QToolButton::clicked, this, [this]() {
    if(m_currentPath.isEmpty()) return;
    QGuiApplication::clipboard()->setText(m_currentPath);
    statusBar()->showMessage(tr("Path copied to clipboard"), 1500);
  });
  connect(m_statusButton, &QWidget::customContextMenuRequested, this,
          [this](const QPoint &pos) {
            if(m_currentPath.isEmpty()) return;
            QMenu menu;
            QAction *copy = menu.addAction(tr("Copy Path"));
            QAction *reveal = menu.addAction(tr("Open Containing Folder"));
            QAction *chosen = menu.exec(m_statusButton->mapToGlobal(pos));
            if(chosen == copy) {
              QGuiApplication::clipboard()->setText(m_currentPath);
              statusBar()->showMessage(tr("Path copied to clipboard"), 1500);
            } else if(chosen == reveal) {
              revealInFileManager(m_currentPath);
            }
          });

  // Custom frameless chrome. QWindowKit handles the native bits: snap
  // layouts, snap zones, resize edges, Win11 immersive dark mode + Mica
  // backdrop, the maximize hover preview. We just supply the title widget
  // and tell QWK which children are the system buttons (so it knows where
  // the maximize hover popout anchors) and which are interactive (so they
  // get clicks instead of treating the whole strip as a drag zone).
  //
  // Set up the window agent FIRST (it reworks the native window into a
  // frameless one), then build the title bar and register it.
  auto *agent = new QWK::WidgetWindowAgent(this);
  agent->setup(this);

  auto *titleBar = new TitleBar(this);
  titleBar->setFileMenu(fileMenu);
  titleBar->setViewMenu(viewMenu);
  connect(titleBar, &TitleBar::settingsClicked, this,
          &MainWindow::onPreferences);
  connect(titleBar, &TitleBar::minimizeClicked, this, &QWidget::showMinimized);
  connect(titleBar, &TitleBar::maximizeClicked, this, [this]() {
    if(isMaximized()) showNormal();
    else showMaximized();
  });
  connect(titleBar, &TitleBar::closeClicked, this, &QWidget::close);

  agent->setTitleBar(titleBar);
  agent->setSystemButton(QWK::WindowAgentBase::Minimize, titleBar->minButton());
  agent->setSystemButton(QWK::WindowAgentBase::Maximize, titleBar->maxButton());
  agent->setSystemButton(QWK::WindowAgentBase::Close, titleBar->closeButton());
  agent->setHitTestVisible(titleBar->fileButton(), true);
  agent->setHitTestVisible(titleBar->viewButton(), true);
  agent->setHitTestVisible(titleBar->settingsButton(), true);

  // Install the title bar as the top of a container central widget rather than
  // via setMenuWidget(). On KDE the Plasma platform theme lazily creates a
  // QMenuBar and installs it in QMainWindow's menu-bar slot; that eviction
  // deletes a title bar parked there with setMenuWidget(), leaving the window
  // with no title bar (and dangling QWK button pointers). Keeping the menu-bar
  // slot empty and stacking [title bar | editor area] in the central widget
  // sidesteps the contention — QWK only needs setTitleBar() above for the drag
  // region, not a particular place in the layout.
  auto *container = new QWidget(this);
  auto *layout = new QVBoxLayout(container);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);
  layout->addWidget(titleBar);
  layout->addWidget(m_area, 1);
  setCentralWidget(container);

  // The outline lives inside each DocumentView (so split groups toggle it
  // independently); this menu item just flips the active document's copy and
  // mirrors its state. syncOutlineAction() keeps it pointed at the active doc.
  viewMenu->addSeparator();
  m_outlineAction = viewMenu->addAction(tr("Show &Outline"));
  m_outlineAction->setCheckable(true);
  connect(m_outlineAction, &QAction::toggled, this, [this](bool on) {
    if(auto *doc = m_area->currentDocument()) doc->setOutlineVisible(on);
  });
  syncOutlineAction(m_area->currentDocument());
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

void MainWindow::refreshStatusBarStyle() {
  const bool dark =
      QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
  const QString fg =
      dark ? QStringLiteral("#f0f0f0") : QStringLiteral("#000000");
  // Zero vertical padding so the button hugs the text height and the status
  // bar centers it; horizontal padding gives the hover pill some breathing
  // room while keeping the small left inset off the window edge. Hover/press
  // use a grey wash that reads on either scheme.
  statusBar()->setStyleSheet(
      QStringLiteral(
          "QStatusBar { color: %1; }"
          "QStatusBar::item { border: none; }"
          "QStatusBar QToolButton { border: none; background: transparent;"
          " color: %1; padding: 0px 8px 0px 4px; border-radius: 4px; }"
          "QStatusBar QToolButton:disabled { color: #888888; }"
          "QStatusBar QToolButton:enabled:hover { background: "
          "rgba(127,127,127,0.18); }"
          "QStatusBar QToolButton:enabled:pressed { background: "
          "rgba(127,127,127,0.30); }")
          .arg(fg));
}

void MainWindow::onCurrentDocumentChanged(DocumentView *doc) {
  syncOutlineAction(doc);

  if(!doc) {
    setWindowTitle(tr("mdv"));
    m_currentPath.clear();
    m_statusButton->setText(tr("No file open."));
    m_statusButton->setToolTip(QString());
    m_statusButton->setEnabled(false);
    return;
  }
  setWindowTitle(tr("%1 — mdv").arg(doc->displayName()));
  m_currentPath = QDir::toNativeSeparators(doc->filePath());
  m_statusButton->setText(m_currentPath);
  m_statusButton->setToolTip(tr("Click to copy path"));
  m_statusButton->setEnabled(true);
}

void MainWindow::syncOutlineAction(DocumentView *doc) {
  // Follow only the active document, so a toggle elsewhere doesn't move the
  // checkmark out from under the user.
  disconnect(m_outlineConn);
  m_outlineAction->setEnabled(doc != nullptr);
  {
    const QSignalBlocker block(m_outlineAction);  // don't echo back as a toggle
    m_outlineAction->setChecked(doc && doc->outlineVisible());
  }
  if(doc)
    m_outlineConn = connect(doc, &DocumentView::outlineVisibilityChanged,
                            m_outlineAction, &QAction::setChecked);
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
    action->setToolTip(QDir::toNativeSeparators(path));
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
