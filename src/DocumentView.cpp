#include "DocumentView.h"

#include <algorithm>

#include <QAbstractTextDocumentLayout>
#include <QColor>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFont>
#include <QMenu>
#include <QMessageBox>
#include <QPalette>
#include <QScrollBar>
#include <QSettings>
#include <QTextBlock>
#include <QTextBrowser>
#include <QTextCursor>
#include <QTextStream>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include "ContentTheme.h"
#include "EditorGroup.h"
#include "Markdown.h"

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
  // Apply the active content theme's stylesheet. setDefaultStyleSheet
  // persists across subsequent setHtml() calls on the same document.
  m_browser->document()->setDefaultStyleSheet(ContentTheme::active().qss());
  // Prose font goes through the document's default QFont — CSS body
  // rules don't reliably set font properties on QTextDocument.
  applyDocumentFont();
  // Page background lives on the widget's QPalette::Base, not in the
  // document's CSS — see applyDocumentPalette() comment.
  applyDocumentPalette();
  connect(m_browser, &QWidget::customContextMenuRequested, this,
          &DocumentView::onContextMenuRequested);
  layout->addWidget(m_browser);

  // Anchor-apply window: we keep re-applying on every rangeChanged for a
  // short window after scrollToAnchor() is called, because layout often
  // happens in multiple passes (initial estimate, then refinements once
  // tables/code blocks have measured themselves). When the timer fires
  // we disconnect and let the user own the scroll position.
  m_pendingAnchorTimer = new QTimer(this);
  m_pendingAnchorTimer->setSingleShot(true);
  connect(m_pendingAnchorTimer, &QTimer::timeout, this, [this]() {
    if (m_pendingScrollConn) {
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
}

bool DocumentView::loadFile(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    QMessageBox::warning(
        this, tr("mdv"),
        tr("Couldn't open %1:\n%2").arg(path, file.errorString()));
    return false;
  }

  QTextStream in(&file);
  in.setEncoding(QStringConverter::Utf8);
  // Render markdown → HTML externally (via md4c) and feed setHtml, so
  // the document's default stylesheet actually applies. setMarkdown
  // would bypass CSS by baking in QTextCharFormats during import.
  m_browser->setHtml(mdv::markdownToHtml(in.readAll()));

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
  const QFileInfo info(path);
  m_filePath = info.canonicalFilePath();
  if (m_filePath.isEmpty()) m_filePath = info.absoluteFilePath();

  armWatch();
  emit fileLoaded(m_filePath);
  return true;
}

void DocumentView::refresh() {
  // Re-pull the stylesheet (theme or font settings may have changed
  // since construction).
  m_browser->document()->setDefaultStyleSheet(ContentTheme::active().qss());
  applyDocumentFont();
  applyDocumentPalette();

  if (m_filePath.isEmpty()) return;

  // Capture position before re-rendering; restore via the same anchor
  // machinery the Split actions use.
  const int anchor = topAnchor();
  loadFile(m_filePath);
  scrollToAnchor(anchor);
}

void DocumentView::applyDocumentFont() {
  QSettings s;
  QFont f = m_browser->document()->defaultFont();

  const QString family = s.value(QStringLiteral("fonts/prose")).toString();
  if (!family.isEmpty()) f.setFamily(family);

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
  const QString bg = ContentTheme::active().color(QStringLiteral("text.background"));
  const QString fg = ContentTheme::active().color(QStringLiteral("text.foreground"));

  QPalette p = m_browser->palette();
  if (!bg.isEmpty()) p.setColor(QPalette::Base, QColor(bg));
  if (!fg.isEmpty()) p.setColor(QPalette::Text, QColor(fg));
  m_browser->setPalette(p);
}

void DocumentView::onAnchorClicked(const QUrl &url) {
  const QString scheme = url.scheme();

  // External: any non-file scheme (http/https/mailto/…) → system handler.
  if (!scheme.isEmpty() && scheme != QLatin1String("file")) {
    QDesktopServices::openUrl(url);
    return;
  }

  // In-document anchor (#heading). Heading ids aren't generated yet (that
  // lands with the TOC work), so this currently finds nothing and no-ops —
  // wired here so it lights up once the ids exist.
  if (url.path().isEmpty() && url.hasFragment()) {
    m_browser->scrollToAnchor(url.fragment());
    return;
  }

  // Local file: resolve relative to this document's directory and open it as
  // a new tab. Any #fragment is dropped — we just open the target file.
  const QString rawPath =
      (scheme == QLatin1String("file")) ? url.toLocalFile() : url.path();
  if (rawPath.isEmpty()) return;

  const QString base = QFileInfo(m_filePath).absolutePath();
  const QString resolved =
      QDir::isAbsolutePath(rawPath)
          ? QDir::cleanPath(rawPath)
          : QDir::cleanPath(base + QLatin1Char('/') + rawPath);
  emit openFileRequested(resolved);
}

int DocumentView::topAnchor() const {
  // cursorForPosition uses viewport coordinates; (0, 0) is the top-left
  // of the visible area, so its position is the character offset where
  // the user is currently scrolled to.
  return m_browser->cursorForPosition(QPoint(0, 0)).position();
}

void DocumentView::scrollToAnchor(int position) {
  m_pendingAnchor = position;

  if (m_pendingScrollConn) {
    QObject::disconnect(m_pendingScrollConn);
    m_pendingScrollConn = {};
  }

  // Listen to every rangeChanged until the window quiets — layout can
  // happen in multiple passes (initial estimate, then refinements once
  // tables/code blocks measure themselves), and the right block y is
  // whatever the *last* pass produces.
  m_pendingScrollConn = connect(
      m_browser->verticalScrollBar(), &QAbstractSlider::rangeChanged, this,
      [this](int, int) {
        tryApplyAnchor();
        m_pendingAnchorTimer->start(300);
      });

  tryApplyAnchor();
  m_pendingAnchorTimer->start(300);
}

bool DocumentView::tryApplyAnchor() {
  if (m_pendingAnchor < 0) return true;

  QTextDocument *doc = m_browser->document();
  const int charCount = doc->characterCount();
  if (charCount <= 1) return false;

  const int pos = std::min(m_pendingAnchor, std::max(0, charCount - 1));
  QTextCursor cursor(doc);
  cursor.setPosition(pos);
  QTextBlock block = cursor.block();
  if (!block.isValid()) return false;

  const QRectF rect = doc->documentLayout()->blockBoundingRect(block);
  const QSizeF docSize = doc->size();

  // QRectF::isValid accepts (0,0,0,0); explicitly require a non-zero
  // block height before trusting the geometry. An unlaid block returns
  // a zero rect and we'd otherwise snap to y=0.
  if (m_pendingAnchor > 0 && (docSize.height() <= 0 || rect.height() <= 0)) {
    return false;
  }

  // Move the text cursor to the anchor too, so Qt's "keep cursor
  // visible" behavior on focus / show events doesn't yank the scroll
  // back to wherever the cursor was previously parked.
  m_browser->setTextCursor(cursor);
  m_browser->verticalScrollBar()->setValue(int(rect.y()));

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
  for (QWidget *w = parentWidget(); w; w = w->parentWidget()) {
    group = qobject_cast<EditorGroup *>(w);
    if (group) break;
  }
  if (group) {
    menu->addSeparator();
    group->populateTabContextMenu(menu, this);
  }

  menu->exec(m_browser->mapToGlobal(pos));
  delete menu;
}

QString DocumentView::displayName() const {
  if (m_filePath.isEmpty()) return tr("(untitled)");
  return QFileInfo(m_filePath).fileName();
}

void DocumentView::setPinned(bool on) {
  if (m_pinned == on) return;
  m_pinned = on;
  emit pinnedChanged(on);
}

void DocumentView::setWatching(bool on) {
  if (m_watching == on) return;
  m_watching = on;
  if (!on) m_reloadTimer->stop();  // cancel any reload queued in the debounce
  armWatch();
}

void DocumentView::armWatch() {
  if (!m_watcher) return;
  // Clear the current path, then re-add if we should be watching. (removePaths
  // on an empty list is a no-op; an atomic save drops the path, so re-arming
  // after each reload re-points us at the new inode.)
  const QStringList watched = m_watcher->files();
  if (!watched.isEmpty()) m_watcher->removePaths(watched);
  if (m_watching && !m_filePath.isEmpty() && QFileInfo::exists(m_filePath))
    m_watcher->addPath(m_filePath);
}

void DocumentView::onFileChanged(const QString &) {
  if (!m_watching) return;
  // Coalesce the burst of events a save produces; reload once it settles.
  m_reloadTimer->start(120);
}

void DocumentView::reloadFromDisk() {
  // Guard m_watching too: a reload queued in the debounce window must not fire
  // after watch was switched off (setWatching stops the timer, but this also
  // covers any other path that could leave it pending).
  if (!m_watching || m_filePath.isEmpty()) return;

  QFile file(m_filePath);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    // Mid-save the file can be briefly unreadable (atomic rename in flight) —
    // bail quietly and re-arm; another change event will bring us back.
    armWatch();
    return;
  }
  QTextStream in(&file);
  in.setEncoding(QStringConverter::Utf8);
  const QString html = mdv::markdownToHtml(in.readAll());

  // Capture the view state before swapping. The anchor is a character offset
  // into the current rendered text; oldText is that same text (so the diff in
  // remapAnchor lives in the anchor's coordinate space). The font — including
  // any Ctrl+wheel zoom — is left untouched, so a content-only setHtml()
  // preserves the zoom for free.
  const int anchor = topAnchor();
  const QString oldText = m_browser->document()->toPlainText();

  m_browser->setHtml(html);

  const QString newText = m_browser->document()->toPlainText();
  scrollToAnchor(remapAnchor(anchor, oldText, newText));

  // Re-point the watcher (an atomic save dropped the path).
  armWatch();
}

int DocumentView::remapAnchor(int pos, const QString &oldText,
                              const QString &newText) const {
  const int oldLen = oldText.size();
  const int newLen = newText.size();

  // Common prefix and (non-overlapping) common suffix bound the changed region
  // to [prefix, oldLen - suffix) in the old text's coordinates.
  int prefix = 0;
  const int maxPrefix = std::min(oldLen, newLen);
  while (prefix < maxPrefix && oldText[prefix] == newText[prefix]) ++prefix;

  int suffix = 0;
  const int maxSuffix = std::min(oldLen, newLen) - prefix;
  while (suffix < maxSuffix &&
         oldText[oldLen - 1 - suffix] == newText[newLen - 1 - suffix]) {
    ++suffix;
  }

  const int changeStart = prefix;
  const int changeEnd = oldLen - suffix;  // exclusive

  int mapped;
  if (pos <= changeStart) {
    mapped = pos;  // edit was below the anchor — offset unchanged
  } else if (pos >= changeEnd) {
    mapped = pos + (newLen - oldLen);  // edit was above — shift by net delta
  } else {
    mapped = changeStart;  // anchor sat inside the edit — best-effort positional
  }
  return std::clamp(mapped, 0, newLen);
}
