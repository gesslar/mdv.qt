#include "DocumentView.h"

#include <algorithm>
#include <array>

#include <QAbstractTextDocumentLayout>
#include <QAction>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QKeySequence>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QRect>
#include <QRegularExpression>
#include <QScrollBar>
#include <QSettings>
#include <QSplitter>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextFragment>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "ContentTheme.h"
#include "EditorGroup.h"
#include "FindBar.h"
#include "Markdown.h"
#include "OutlinePanel.h"

DocumentView::DocumentView(QWidget *parent) : QWidget(parent) {
  auto *layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);

  m_browser = new QTextBrowser(this);
  // Handle every link click ourselves. With openLinks left on, QTextBrowser
  // navigates the view onto the target (setSource) — which for a local file
  // wipes the rendered document. onAnchorClicked() classifies instead.
  m_browser->setOpenLinks(false);
  connect(m_browser, &QTextBrowser::anchorClicked, this,
          &DocumentView::onAnchorClicked);
  m_browser->setContextMenuPolicy(Qt::CustomContextMenu);
  // Disable QTextBrowser's own drop handling so file URL drops bubble
  // up to the EditorGroup (which interprets them as "open this file")
  // instead of being inserted as text into the rendered document.
  m_browser->setAcceptDrops(false);
  // Ctrl+wheel zoom is intercepted on the viewport so we can re-size headings
  // after each zoom step (see eventFilter / applyHeadingSizes).
  m_browser->viewport()->installEventFilter(this);
  // Apply the active content theme's stylesheet. setDefaultStyleSheet
  // persists across subsequent setHtml() calls on the same document.
  m_browser->document()->setDefaultStyleSheet(ContentTheme::active().qss());
  // Prose font goes through the document's default QFont — CSS body
  // rules don't reliably set font properties on QTextDocument.
  applyDocumentFont();
  // Page background lives on the widget's QPalette::Base, not in the
  // document's CSS — see applyDocumentPalette() comment.
  applyDocumentPalette();
  // The view itself takes no focus; delegate to the browser so a plain
  // setFocus() on a DocumentView lands keyboard focus in the text (where
  // scrolling and the Ctrl+F shortcut need it). Without this, the view's
  // default NoFocus policy makes setFocus() a no-op.
  setFocusProxy(m_browser);
  connect(m_browser, &QWidget::customContextMenuRequested, this,
          &DocumentView::onContextMenuRequested);
  // Scroll-spy: track which section is at the top of the viewport so the
  // outline can highlight it. Heading y-positions move on reflow, so dirty
  // the cache when the document's layout resizes (width change, zoom, edit).
  connect(m_browser->verticalScrollBar(), &QAbstractSlider::valueChanged, this,
          &DocumentView::onScrolled);
  connect(m_browser->document()->documentLayout(),
          &QAbstractTextDocumentLayout::documentSizeChanged, this,
          [this](const QSizeF &) {
            // Heading positions moved; recompute so the outline highlight
            // tracks reflow (and lands on the top section after first layout).
            m_headingYDirty = true;
            onScrolled();
          });

  // Each document carries its own outline panel beside the browser, so split
  // groups can show or hide it independently. Initial visibility follows the
  // "show outline by default" preference; the side and width are remembered
  // globally so a new document inherits the last layout.
  QSettings s;
  m_outlineSide = s.value(QStringLiteral("outline/side"), 0).toInt() == 1
                      ? OutlineSide::Right
                      : OutlineSide::Left;
  m_outlineVisible =
      s.value(QStringLiteral("outline/showByDefault"), false).toBool();
  m_outlineWidth = s.value(QStringLiteral("outline/width"), 240).toInt();
  if(m_outlineWidth < 80) m_outlineWidth = 240;

  m_outline = new OutlinePanel(this);
  m_outline->setSide(m_outlineSide);
  m_splitter = new QSplitter(Qt::Horizontal, this);
  m_splitter->setChildrenCollapsible(false);
  m_splitter->setHandleWidth(1);

  // The panel talks only to its own document — no MainWindow plumbing.
  connect(m_outline, &OutlinePanel::headingActivated, this,
          &DocumentView::scrollToHeading);
  connect(m_outline, &OutlinePanel::hideRequested, this, [this] {
    setOutlineVisible(false);
  });
  connect(m_outline, &OutlinePanel::moveToOtherSideRequested, this, [this] {
    toggleOutlineSide();
  });
  connect(this, &DocumentView::headingsChanged, this, [this] {
    m_outline->setHeadings(m_headings);
  });
  connect(this, &DocumentView::currentHeadingChanged, m_outline,
          &OutlinePanel::setCurrentHeading);

  applyOutlineArrangement();
  m_outline->setVisible(m_outlineVisible);
  layout->addWidget(m_splitter);

  // Anchor-apply window: we keep re-applying on every rangeChanged for a
  // short window after scrollToAnchor() is called, because layout often
  // happens in multiple passes (initial estimate, then refinements once
  // tables/code blocks have measured themselves). When the timer fires
  // we disconnect and let the user own the scroll position.
  m_pendingAnchorTimer = new QTimer(this);
  m_pendingAnchorTimer->setSingleShot(true);
  connect(m_pendingAnchorTimer, &QTimer::timeout, this, [this]() {
    if(m_pendingScrollConn) {
      QObject::disconnect(m_pendingScrollConn);
      m_pendingScrollConn = {};
    }
    m_pendingAnchor = -1;
  });

  // Hot reload. fileChanged fires on external writes; the reload timer
  // debounces the burst a save produces before we re-render in place.
  m_watcher = new QFileSystemWatcher(this);
  connect(m_watcher, &QFileSystemWatcher::fileChanged, this,
          &DocumentView::onFileChanged);
  m_reloadTimer = new QTimer(this);
  m_reloadTimer->setSingleShot(true);
  connect(m_reloadTimer, &QTimer::timeout, this, &DocumentView::reloadFromDisk);

  // Find bar: parented to the browser so it floats over the text. Hidden until
  // Ctrl+F. Browser resize repositions it (handled in eventFilter); theme
  // refresh restyles it.
  m_findBar = new FindBar(m_browser);
  restyleFindBar();
  m_browser->installEventFilter(this);
  connect(m_findBar, &FindBar::queryChanged, this, &DocumentView::runFind);
  connect(m_findBar, &FindBar::findNext, this, [this] { stepMatch(+1); });
  connect(m_findBar, &FindBar::findPrevious, this, [this] { stepMatch(-1); });
  connect(m_findBar, &FindBar::closed, this, &DocumentView::clearFind);

  // Ctrl+F opens it; F3 / Shift+F3 step matches even while focus is in the
  // text. Widget-with-children context routes them to whichever split has
  // focus.
  const auto addFindShortcut = [this](QKeySequence::StandardKey key,
                                      auto handler) {
    auto *a = new QAction(this);
    a->setShortcut(key);
    a->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    connect(a, &QAction::triggered, this, handler);
    addAction(a);
  };
  addFindShortcut(QKeySequence::Find, [this] { showFindBar(); });
  addFindShortcut(QKeySequence::FindNext, [this] { stepMatch(+1); });
  addFindShortcut(QKeySequence::FindPrevious, [this] { stepMatch(-1); });
}

bool DocumentView::loadFile(const QString &path) {
  QFile file(path);
  if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::warning(
        this, tr("mdv"),
        tr("Couldn't open %1:\n%2").arg(path, file.errorString()));
    return false;
  }

  QTextStream in(&file);
  in.setEncoding(QStringConverter::Utf8);

  // Resolve inline images relative to the document's own directory. md4c emits
  // ![alt](pic.png) as <img src="pic.png">; QTextBrowser asks loadResource to
  // fetch each src, and for a relative path it joins it onto a search path. We
  // feed HTML via setHtml() (not setSource), so there's no document source to
  // resolve against — without this, relative image srcs would never load.
  // Absolute paths and file:// URLs are handled by Qt directly. Set before
  // setHtml() so the resources are findable during the layout pass it kicks off.
  const QFileInfo info(path);
  m_browser->setSearchPaths({info.absolutePath()});

  // Render markdown → HTML externally (via md4c) and feed setHtml, so
  // the document's default stylesheet actually applies. setMarkdown
  // would bypass CSS by baking in QTextCharFormats during import.
  const mdv::RenderResult rendered = mdv::renderMarkdown(in.readAll());
  m_browser->setHtml(rendered.html);
  applyHeadingSizes();
  m_zoomAccum = 0;  // new document; drop any banked sub-notch zoom scroll
  m_headings = rendered.headings;
  m_headingYDirty = true;
  m_lastHeadingId.clear();

  // QTextBrowser parks the text cursor wherever setMarkdown leaves it
  // and then scrolls to keep the cursor visible, which often lands at
  // the bottom of the document. Anchor the cursor at the start and
  // zero the scrollbars so a freshly loaded document opens at the top.
  QTextCursor cursor = m_browser->textCursor();
  cursor.movePosition(QTextCursor::Start);
  m_browser->setTextCursor(cursor);
  m_browser->verticalScrollBar()->setValue(0);
  m_browser->horizontalScrollBar()->setValue(0);

  // canonicalFilePath() returns "" for non-existent files; that shouldn't
  // happen here (we just read it) but fall back defensively.
  m_filePath = info.canonicalFilePath();
  if(m_filePath.isEmpty()) m_filePath = info.absoluteFilePath();

  armWatch();
  emit fileLoaded(m_filePath);
  emit headingsChanged();
  return true;
}

void DocumentView::refresh() {
  // Re-pull the stylesheet (theme or font settings may have changed
  // since construction).
  m_browser->document()->setDefaultStyleSheet(ContentTheme::active().qss());
  applyDocumentFont();
  applyDocumentPalette();

  // Colors may have changed; restyle the bar and re-tint existing highlights.
  restyleFindBar();

  if(m_filePath.isEmpty()) return;

  // Capture position before re-rendering; restore via the same anchor
  // machinery the Split actions use.
  const int anchor = topAnchor();
  loadFile(m_filePath);
  scrollToAnchor(anchor);

  // The re-render replaced the document content, invalidating match offsets;
  // rebuild against the new layout if the bar is open.
  if(m_findBar->isVisible()) runFind();
}

void DocumentView::applyDocumentFont() {
  QSettings s;
  QFont f = m_browser->document()->defaultFont();

  const QString family = s.value(QStringLiteral("fonts/prose")).toString();
  if(!family.isEmpty()) f.setFamily(family);

  // Settings store an integer (the spinbox value). Use setPointSize so
  // Ctrl+wheel zoomIn/zoomOut — which works in points — scales it.
  const int size = s.value(QStringLiteral("fonts/prose.size"), 14).toInt();
  f.setPointSize(size);

  m_browser->document()->setDefaultFont(f);
}

void DocumentView::applyDocumentPalette() {
  // body { background-color } in the document's CSS doesn't paint the
  // viewport — QTextBrowser draws the viewport using the widget's
  // QPalette::Base. Pull the theme's text.background / text.foreground
  // through the palette so the page color shows. Element-level CSS
  // rules in the template still win for headings, code, etc.
  const QString bg =
      ContentTheme::active().color(QStringLiteral("text.background"));
  const QString fg =
      ContentTheme::active().color(QStringLiteral("text.foreground"));

  QPalette p = m_browser->palette();
  if(!bg.isEmpty()) p.setColor(QPalette::Base, QColor(bg));
  if(!fg.isEmpty()) p.setColor(QPalette::Text, QColor(fg));
  m_browser->setPalette(p);
}

void DocumentView::applyHeadingSizes() {
  QTextDocument *doc = m_browser->document();
  const qreal base = doc->defaultFont().pointSizeF();
  if(base <= 0.0) return;  // pixel-sized font; nothing to scale from

  // Descending scale, indexed by heading level 1..6. Without this, Qt's HTML
  // importer sizes headings via a FontSizeAdjustment that both overrides any
  // explicit size and renders h5 smaller than h6.
  static constexpr std::array<qreal, 7> kScale = {0.0,  2.2,  1.7, 1.35,
                                                  1.1,  0.95, 0.85};

  QTextCursor cur(doc);
  cur.beginEditBlock();
  for(QTextBlock block = doc->begin(); block.isValid(); block = block.next()) {
    const int level = block.blockFormat().headingLevel();
    if(level < 1 || level > 6) continue;
    const qreal pt = base * kScale[level];
    // Per fragment, so inline spans inside a heading (bold, code, links) keep
    // their other attributes; clearing the adjustment lets the absolute size
    // we set actually win.
    for(auto it = block.begin(); it != block.end(); ++it) {
      const QTextFragment frag = it.fragment();
      if(!frag.isValid()) continue;
      QTextCharFormat fmt = frag.charFormat();
      fmt.clearProperty(QTextFormat::FontSizeAdjustment);
      fmt.setFontPointSize(pt);
      cur.setPosition(frag.position());
      cur.setPosition(frag.position() + frag.length(), QTextCursor::KeepAnchor);
      cur.setCharFormat(fmt);
    }
  }
  cur.endEditBlock();
}

bool DocumentView::eventFilter(QObject *watched, QEvent *event) {
  if(watched == m_browser && event->type() == QEvent::Resize) {
    // Keep the floating find bar pinned to the top-right as the text area
    // resizes (group split/resize, outline toggle).
    if(m_findBar && m_findBar->isVisible()) positionFindBar();
    return false;  // observe only
  }
  if(watched == m_browser->viewport() && event->type() == QEvent::Wheel) {
    auto *wheel = static_cast<QWheelEvent *>(event);
    if(wheel->modifiers() & Qt::ControlModifier) {
      // Accumulate sub-notch deltas: trackpads and hi-res mice send |delta| <
      // 120 per event, so dividing each event by 120 would floor to 0 and never
      // zoom. Bank the remainder and act only once a full notch has built up.
      m_zoomAccum += wheel->angleDelta().y();
      const int steps = m_zoomAccum / 120;
      if(steps != 0) {
        m_zoomAccum -= steps * 120;
        if(steps > 0) m_browser->zoomIn(steps);
        else m_browser->zoomOut(-steps);
        // Zoom changes the document's default font; heading fragments carry
        // absolute sizes that don't follow it, so re-derive them.
        applyHeadingSizes();
      }
      return true;  // consume — we own Ctrl+wheel zoom
    }
  }
  return QWidget::eventFilter(watched, event);
}

void DocumentView::onAnchorClicked(const QUrl &url) {
  const QString scheme = url.scheme();

  // External: any non-file scheme (http/https/mailto/…) → system handler.
  if(!scheme.isEmpty() && scheme != QLatin1String("file")) {
    QDesktopServices::openUrl(url);
    return;
  }

  // In-document anchor (#heading). Heading ids aren't generated yet (that
  // lands with the TOC work), so this currently finds nothing and no-ops —
  // wired here so it lights up once the ids exist.
  if(url.path().isEmpty() && url.hasFragment()) {
    m_browser->scrollToAnchor(url.fragment());
    return;
  }

  // Local file: resolve relative to this document's directory and open it as
  // a new tab. Any #fragment is dropped — we just open the target file.
  const QString rawPath =
      (scheme == QLatin1String("file")) ? url.toLocalFile() : url.path();
  if(rawPath.isEmpty()) return;

  const QString base = QFileInfo(m_filePath).absolutePath();
  const QString resolved =
      QDir::isAbsolutePath(rawPath)
          ? QDir::cleanPath(rawPath)
          : QDir::cleanPath(base + QLatin1Char('/') + rawPath);
  emit openFileRequested(resolved);
}

void DocumentView::scrollToHeading(const QString &anchorId) {
  if(anchorId.isEmpty()) return;
  // Same path an in-document #anchor click takes; QTextBrowser brings the
  // named anchor to the top of the viewport.
  m_browser->scrollToAnchor(anchorId);
}

void DocumentView::rebuildHeadingPositions() const {
  m_headingY.clear();
  QTextDocument *doc = m_browser->document();
  QAbstractTextDocumentLayout *lay = doc->documentLayout();

  for(QTextBlock b = doc->begin(); b != doc->end(); b = b.next()) {
    if(b.blockFormat().headingLevel() <= 0) continue;
    // The slug we injected lands on the heading's first text fragment as an
    // anchor name (verified against Qt's rich-text importer).
    QString id;
    for(QTextBlock::iterator it = b.begin(); !it.atEnd(); ++it) {
      const QStringList names = it.fragment().charFormat().anchorNames();
      if(!names.isEmpty()) {
        id = names.first();
        break;
      }
    }
    if(id.isEmpty()) continue;
    m_headingY.append({id, lay->blockBoundingRect(b).y()});
  }
  // Blocks iterate top-to-bottom, so m_headingY is already sorted by y.
  m_headingYDirty = false;
}

QString DocumentView::currentHeadingId() const {
  if(m_headingYDirty) rebuildHeadingPositions();
  if(m_headingY.isEmpty()) return {};

  // The scrollbar value maps to document-y, but laid-out blocks start at the
  // document's top margin — so the first heading sits at y == documentMargin,
  // not 0. Offset the probe by that margin (plus 1 for the exact-top case) so
  // the first heading registers as current at scroll 0 instead of leaving the
  // outline unhighlighted until the reader scrolls a little.
  const qreal probe = m_browser->verticalScrollBar()->value() +
                      m_browser->document()->documentMargin() + 1.0;
  QString current;
  for(const auto &[id, y] : m_headingY) {
    if(y > probe) break;
    current = id;
  }
  return current;
}

void DocumentView::onScrolled() {
  const QString id = currentHeadingId();
  if(id == m_lastHeadingId) return;
  m_lastHeadingId = id;
  emit currentHeadingChanged(id);
}

void DocumentView::applyOutlineArrangement() {
  // insertWidget moves an existing child, so calling it for both each time
  // re-establishes the left/right order. The browser stretches; the outline
  // keeps its width on resize (the user can still drag the handle).
  if(m_outlineSide == OutlineSide::Left) {
    m_splitter->insertWidget(0, m_outline);
    m_splitter->insertWidget(1, m_browser);
  } else {
    m_splitter->insertWidget(0, m_browser);
    m_splitter->insertWidget(1, m_outline);
  }
  const int outlineIdx = m_outlineSide == OutlineSide::Left ? 0 : 1;
  m_splitter->setStretchFactor(outlineIdx, 0);
  m_splitter->setStretchFactor(1 - outlineIdx, 1);
  m_splitter->setSizes(m_outlineSide == OutlineSide::Left
                           ? QList<int>{m_outlineWidth, 1 << 20}
                           : QList<int>{1 << 20, m_outlineWidth});
}

void DocumentView::setOutlineVisible(bool on) {
  if(m_outlineVisible == on) return;
  if(!on && m_outline->width() > 0) m_outlineWidth = m_outline->width();
  m_outlineVisible = on;
  m_outline->setVisible(on);
  if(on) applyOutlineArrangement();  // restore the panel's splitter share

  // Visibility is per-document runtime state — the "show outline by default"
  // preference governs new documents, so a toggle here doesn't move it. The
  // dragged width is still worth remembering globally.
  QSettings().setValue(QStringLiteral("outline/width"), m_outlineWidth);

  emit outlineVisibilityChanged(on);
}

void DocumentView::toggleOutlineSide() {
  // Capture the live (possibly user-dragged) width before re-arranging, so
  // applyOutlineArrangement() re-applies that width instead of snapping back
  // to the remembered default.
  if(m_outline->width() > 0) m_outlineWidth = m_outline->width();
  m_outlineSide = m_outlineSide == OutlineSide::Left ? OutlineSide::Right
                                                     : OutlineSide::Left;
  m_outline->setSide(m_outlineSide);
  applyOutlineArrangement();
  QSettings().setValue(QStringLiteral("outline/side"),
                       m_outlineSide == OutlineSide::Right ? 1 : 0);
}

int DocumentView::topAnchor() const {
  // cursorForPosition uses viewport coordinates; (0, 0) is the top-left
  // of the visible area, so its position is the character offset where
  // the user is currently scrolled to.
  return m_browser->cursorForPosition(QPoint(0, 0)).position();
}

void DocumentView::scrollToAnchor(int position) {
  m_pendingAnchor = position;

  if(m_pendingScrollConn) {
    QObject::disconnect(m_pendingScrollConn);
    m_pendingScrollConn = {};
  }

  // Listen to every rangeChanged until the window quiets — layout can
  // happen in multiple passes (initial estimate, then refinements once
  // tables/code blocks measure themselves), and the right block y is
  // whatever the *last* pass produces.
  m_pendingScrollConn =
      connect(m_browser->verticalScrollBar(), &QAbstractSlider::rangeChanged,
              this, [this](int, int) {
                tryApplyAnchor();
                m_pendingAnchorTimer->start(300);
              });

  tryApplyAnchor();
  m_pendingAnchorTimer->start(300);
}

bool DocumentView::tryApplyAnchor() {
  if(m_pendingAnchor < 0) return true;

  QTextDocument *doc = m_browser->document();
  const int charCount = doc->characterCount();
  if(charCount <= 1) return false;

  const int pos = std::min(m_pendingAnchor, std::max(0, charCount - 1));
  QTextCursor cursor(doc);
  cursor.setPosition(pos);
  QTextBlock block = cursor.block();
  if(!block.isValid()) return false;

  const QRectF rect = doc->documentLayout()->blockBoundingRect(block);
  const QSizeF docSize = doc->size();

  // QRectF::isValid accepts (0,0,0,0); explicitly require a non-zero
  // block height before trusting the geometry. An unlaid block returns
  // a zero rect and we'd otherwise snap to y=0.
  if(m_pendingAnchor > 0 && (docSize.height() <= 0 || rect.height() <= 0)) {
    return false;
  }

  // Move the text cursor to the anchor too, so Qt's "keep cursor
  // visible" behavior on focus / show events doesn't yank the scroll
  // back to wherever the cursor was previously parked.
  m_browser->setTextCursor(cursor);
  // blockBoundingRect is in document coordinates, where content starts at
  // y == documentMargin (the top margin sits above the first block). The
  // scrollbar value is the document-y shown at the top of the viewport, so a
  // block lands at the top when value == blockY - documentMargin — the same
  // relation currentHeadingId() inverts for scroll-spy. Without subtracting the
  // margin we overshoot by it on every restore; it's only visible at the very
  // top, where it scrolls the top margin out of view (restoring anchor 0 would
  // otherwise land at documentMargin instead of 0).
  const int target = int(rect.y() - doc->documentMargin());
  m_browser->verticalScrollBar()->setValue(target);

  // Do not clear m_pendingAnchor or disconnect here — the timer manages
  // the lifetime of the re-apply window so later layout passes can
  // refine the position.
  return true;
}

void DocumentView::onContextMenuRequested(const QPoint &pos) {
  // Start from QTextBrowser's standard menu (Copy / Select All / link
  // commands if pos is over a link).
  QMenu *menu = m_browser->createStandardContextMenu(pos);

  // Walk up to find our containing group and let it append the same
  // Pin/Watch/Split/Close items the tab context menu shows.
  EditorGroup *group = nullptr;
  for(QWidget *w = parentWidget(); w; w = w->parentWidget()) {
    group = qobject_cast<EditorGroup *>(w);
    if(group) break;
  }
  if(group) {
    menu->addSeparator();
    group->populateTabContextMenu(menu, this);
  }

  menu->exec(m_browser->mapToGlobal(pos));
  delete menu;
}

QString DocumentView::displayName() const {
  if(m_filePath.isEmpty()) return tr("(untitled)");
  return QFileInfo(m_filePath).fileName();
}

void DocumentView::setPinned(bool on) {
  if(m_pinned == on) return;
  m_pinned = on;
  emit pinnedChanged(on);
}

void DocumentView::setWatching(bool on) {
  if(m_watching == on) return;
  m_watching = on;
  if(!on) m_reloadTimer->stop();  // cancel any reload queued in the debounce
  armWatch();
}

void DocumentView::armWatch() {
  if(!m_watcher) return;
  // Clear the current path, then re-add if we should be watching. (removePaths
  // on an empty list is a no-op; an atomic save drops the path, so re-arming
  // after each reload re-points us at the new inode.)
  const QStringList watched = m_watcher->files();
  if(!watched.isEmpty()) m_watcher->removePaths(watched);
  if(m_watching && !m_filePath.isEmpty() && QFileInfo::exists(m_filePath))
    m_watcher->addPath(m_filePath);
}

void DocumentView::onFileChanged(const QString &) {
  if(!m_watching) return;
  // Coalesce the burst of events a save produces; reload once it settles.
  m_reloadTimer->start(120);
}

void DocumentView::reloadFromDisk() {
  // Guard m_watching too: a reload queued in the debounce window must not fire
  // after watch was switched off (setWatching stops the timer, but this also
  // covers any other path that could leave it pending).
  if(!m_watching || m_filePath.isEmpty()) return;

  QFile file(m_filePath);
  if(!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    // Mid-save the file can be briefly unreadable (atomic rename in flight) —
    // bail quietly and re-arm; another change event will bring us back.
    armWatch();
    return;
  }
  QTextStream in(&file);
  in.setEncoding(QStringConverter::Utf8);
  const mdv::RenderResult rendered = mdv::renderMarkdown(in.readAll());

  // Capture the view state before swapping. The anchor is a character offset
  // into the current rendered text; oldText is that same text (so the diff in
  // remapAnchor lives in the anchor's coordinate space). The font — including
  // any Ctrl+wheel zoom — is left untouched, so a content-only setHtml()
  // preserves the zoom for free.
  const int anchor = topAnchor();
  const QString oldText = m_browser->document()->toPlainText();

  // Restate the image search path before this setHtml(), mirroring loadFile().
  // The path persists on m_browser across reloads (the same m_filePath is
  // always reloaded), so this is belt-and-suspenders today — but it keeps the
  // hot-reload self-contained, so a future change that clears or repoints the
  // search paths can't silently break image loading on reload only.
  m_browser->setSearchPaths({QFileInfo(m_filePath).absolutePath()});
  m_browser->setHtml(rendered.html);
  applyHeadingSizes();
  m_zoomAccum = 0;  // content swap; drop any banked sub-notch zoom scroll
  m_headings = rendered.headings;
  m_headingYDirty = true;
  // Match loadFile: reset so the documentSizeChanged → onScrolled chain emits a
  // fresh current-heading instead of briefly clearing the outline selection.
  m_lastHeadingId.clear();
  emit headingsChanged();

  const QString newText = m_browser->document()->toPlainText();
  scrollToAnchor(remapAnchor(anchor, oldText, newText));

  // Re-point the watcher (an atomic save dropped the path).
  armWatch();

  // Match offsets are stale against the new content; rebuild if searching.
  if(m_findBar->isVisible()) runFind();
}

int DocumentView::remapAnchor(int pos, const QString &oldText,
                              const QString &newText) const {
  const int oldLen = oldText.size();
  const int newLen = newText.size();

  // Common prefix and (non-overlapping) common suffix bound the changed region
  // to [prefix, oldLen - suffix) in the old text's coordinates.
  int prefix = 0;
  const int maxPrefix = std::min(oldLen, newLen);
  while(prefix < maxPrefix && oldText[prefix] == newText[prefix]) ++prefix;

  int suffix = 0;
  const int maxSuffix = std::min(oldLen, newLen) - prefix;
  while(suffix < maxSuffix &&
        oldText[oldLen - 1 - suffix] == newText[newLen - 1 - suffix]) {
    ++suffix;
  }

  const int changeStart = prefix;
  const int changeEnd = oldLen - suffix;  // exclusive

  int mapped;
  if(pos <= changeStart) {
    mapped = pos;  // edit was below the anchor — offset unchanged
  } else if(pos >= changeEnd) {
    mapped = pos + (newLen - oldLen);  // edit was above — shift by net delta
  } else {
    mapped =
        changeStart;  // anchor sat inside the edit — best-effort positional
  }
  return std::clamp(mapped, 0, newLen);
}

void DocumentView::showFindBar() {
  // Seed from the current selection (single-line, reasonably short) the way VS
  // Code does, so "select a word, Ctrl+F" searches it immediately.
  QString seed;
  if(const QTextCursor cur = m_browser->textCursor(); cur.hasSelection()) {
    const QString sel = cur.selectedText();
    if(!sel.contains(QChar::ParagraphSeparator) && sel.length() <= 200) {
      seed = sel;
    }
  }
  m_findBar->activate(seed);
  positionFindBar();
  // activate() only emits queryChanged() when the text actually changes, so
  // run explicitly to (re)highlight when reopening on an unchanged query.
  runFind();
}

void DocumentView::runFind() {
  const QString text = m_findBar->query();
  m_matches.clear();
  m_currentMatch = -1;

  if(text.isEmpty()) {
    applyFindHighlights();
    m_findBar->setMatchInfo(0, 0);
    return;
  }

  QTextDocument *doc = m_browser->document();
  const bool cs = m_findBar->caseSensitive();
  const bool ww = m_findBar->wholeWord();
  const bool useRe = m_findBar->useRegex();

  // Plain-text path uses QTextDocument's find flags; regex path drives a
  // QRegularExpression (case + whole-word folded into pattern/options).
  QTextDocument::FindFlags flags;
  QRegularExpression re;
  if(useRe) {
    QString pattern = ww ? QStringLiteral("\\b(?:%1)\\b").arg(text) : text;
    re.setPattern(pattern);
    re.setPatternOptions(cs ? QRegularExpression::NoPatternOption
                            : QRegularExpression::CaseInsensitiveOption);
    if(!re.isValid()) {
      m_findBar->setRegexError(true);
      applyFindHighlights();  // clears
      return;
    }
  } else {
    if(cs) flags |= QTextDocument::FindCaseSensitively;
    if(ww) flags |= QTextDocument::FindWholeWords;
  }

  QTextCursor c(doc);  // starts at position 0
  const int charCount = doc->characterCount();
  while(true) {
    const QTextCursor found =
        useRe ? doc->find(re, c, flags) : doc->find(text, c, flags);
    if(found.isNull()) break;

    const int start = found.selectionStart();
    const int end = found.selectionEnd();
    if(end <= start) {
      // Zero-width regex match (^, $, \b): advance one char so we don't spin.
      if(start + 1 >= charCount) break;
      c.setPosition(start + 1);
      continue;
    }
    m_matches.append({start, end - start});
    c.setPosition(end);
  }

  if(m_matches.isEmpty()) {
    applyFindHighlights();
    m_findBar->setMatchInfo(0, 0);
    return;
  }

  // Current match = the first at or after the top of the viewport, so the
  // selection lands on something the reader can already (almost) see.
  const int anchorPos = topAnchor();
  m_currentMatch = 0;
  for(int i = 0; i < m_matches.size(); ++i) {
    if(m_matches[i].first >= anchorPos) {
      m_currentMatch = i;
      break;
    }
  }
  applyFindHighlights();
  m_findBar->setMatchInfo(m_currentMatch + 1, static_cast<int>(m_matches.size()));
  revealMatch(m_currentMatch);
}

void DocumentView::stepMatch(int delta) {
  // F3 / Shift+F3 reach here even when the bar is closed; ignore them then.
  if(!m_findBar->isVisible() || m_matches.isEmpty()) return;

  const int n = static_cast<int>(m_matches.size());
  m_currentMatch = (m_currentMatch + delta + n) % n;
  applyFindHighlights();
  m_findBar->setMatchInfo(m_currentMatch + 1, n);
  revealMatch(m_currentMatch);
}

void DocumentView::applyFindHighlights() {
  QList<QTextEdit::ExtraSelection> sels;
  if(!m_matches.isEmpty()) {
    // Both colors derive from the theme accent; the current match gets a
    // stronger alpha so it stands out among the dimmer all-match highlights.
    QColor accent(
        ContentTheme::active().color(QStringLiteral("heading.foreground")));
    if(!accent.isValid()) {
      accent = m_browser->palette().color(QPalette::Highlight);
    }
    QColor allBg = accent;
    allBg.setAlpha(0x40);
    QColor curBg = accent;
    curBg.setAlpha(0xB0);

    QTextDocument *doc = m_browser->document();
    for(int i = 0; i < m_matches.size(); ++i) {
      QTextEdit::ExtraSelection sel;
      sel.cursor = QTextCursor(doc);
      sel.cursor.setPosition(m_matches[i].first);
      sel.cursor.setPosition(m_matches[i].first + m_matches[i].second,
                             QTextCursor::KeepAnchor);
      sel.format.setBackground(i == m_currentMatch ? curBg : allBg);
      sels.append(sel);
    }
  }
  m_browser->setExtraSelections(sels);
}

void DocumentView::revealMatch(int i) {
  if(i < 0 || i >= m_matches.size()) return;

  QTextCursor cur(m_browser->document());
  cur.setPosition(m_matches[i].first);
  // cursorRect is viewport-relative to the current scroll offset.
  const QRect r = m_browser->cursorRect(cur);
  const int viewH = m_browser->viewport()->height();
  if(r.top() < 0 || r.bottom() > viewH) {
    QScrollBar *vbar = m_browser->verticalScrollBar();
    vbar->setValue(vbar->value() + r.top() - (viewH / 3));
  }
}

void DocumentView::clearFind() {
  m_matches.clear();
  m_currentMatch = -1;
  m_browser->setExtraSelections({});
  m_browser->setFocus();
}

void DocumentView::positionFindBar() {
  m_findBar->adjustSize();
  constexpr int margin = 12;
  QScrollBar *vbar = m_browser->verticalScrollBar();
  const int sbw = (vbar && vbar->isVisible()) ? vbar->width() : 0;
  const int x =
      std::max(margin, m_browser->width() - m_findBar->width() - sbw - margin);
  m_findBar->move(x, margin);
}

void DocumentView::restyleFindBar() {
  const ContentTheme &t = ContentTheme::active();
  const auto colorOr = [&t](const QString &key, const QString &fallback) {
    const QString v = t.color(key);
    return v.isEmpty() ? fallback : v;
  };
  const QString bg =
      colorOr(QStringLiteral("text.background"), QStringLiteral("#222"));
  const QString fg =
      colorOr(QStringLiteral("text.foreground"), QStringLiteral("#ddd"));
  const QString fieldBg = colorOr(QStringLiteral("blockquote.background"), bg);
  const QString err =
      colorOr(QStringLiteral("syntax.error"), QStringLiteral("#e06c75"));

  // Borders derive from the foreground at low alpha so they read as subtle
  // dividers on either a light or dark page; the colorful accent is reserved
  // for active toggle/hover states so the checked toggles pop.
  QColor fgc(fg);
  if(!fgc.isValid()) fgc = QColor(0xDD, 0xDD, 0xDD);
  // The find bar is application chrome, not document content, so its accent
  // follows the system/app palette rather than the markdown theme, with a
  // defensive Highlight fallback if a platform leaves Accent unset.
  const QPalette pal = m_findBar->palette();
  QColor accent = pal.color(QPalette::Accent);
  if(!accent.isValid()) accent = pal.color(QPalette::Highlight);

  const auto rgba = [](const QColor &c, int alphaPct) {
    return QStringLiteral("rgba(%1,%2,%3,%4%)")
        .arg(c.red())
        .arg(c.green())
        .arg(c.blue())
        .arg(alphaPct);
  };
  const QString outerBorder = rgba(fgc, 18);   // faint container edge
  const QString fieldBorder = rgba(fgc, 32);   // visible input box edge
  const QString tint = rgba(accent, 40);       // toggle hover
  const QString tintStrong = rgba(accent, 70); // toggle checked

  m_findBar->setStyleSheet(
      QStringLiteral(
          "#findBar { background-color: %1; border: 1px solid %2; "
          "border-radius: 6px; }"
          "#findInputFrame { background-color: %3; border: 1px solid %4; "
          "border-radius: 4px; }"
          "#findInputFrame[error=\"true\"] { border: 1px solid %5; }"
          "#findInputFrame QLineEdit { background: transparent; color: %6; "
          "border: none; }"
          "#findBar QLabel { color: %6; }"
          "#findBar QToolButton { color: %6; border: none; border-radius: 3px; "
          "padding: 2px; }"
          "#findBar QToolButton:hover { background-color: %7; }"
          "#findBar QToolButton:checked { background-color: %8; }")
          .arg(bg, outerBorder, fieldBg, fieldBorder, err, fg, tint,
               tintStrong));
}
