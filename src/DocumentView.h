#pragma once

#include <QList>
#include <QPair>
#include <QPoint>
#include <QString>
#include <QWidget>

#include "Markdown.h"
#include "OutlinePanel.h"  // OutlineSide

class OutlinePanel;
class QFileSystemWatcher;
class QSplitter;
class QTextBrowser;
class QTimer;
class QUrl;

class DocumentView : public QWidget {
  Q_OBJECT

public:
  explicit DocumentView(QWidget *parent = nullptr);

  bool loadFile(const QString &path);

  // Re-apply the active content theme's stylesheet and re-render the
  // current file's HTML so the new styles take effect. Used when the
  // Preferences dialog changes theme or fonts. No-op if no file is loaded.
  void refresh();

  // Read fonts/prose and fonts/prose.size from QSettings and apply via
  // QTextDocument::setDefaultFont. Mono font/size live in the CSS
  // (font-family on pre/code, font-size: 0.92em relative). Called on
  // construction and on Preferences-applied.
  void applyDocumentFont();

  // Push text.background / text.foreground from the active theme into
  // the QTextBrowser's QPalette. The viewport background is the
  // widget's palette Base role, not the document's CSS, so we have to
  // apply it here for the page color to take effect.
  void applyDocumentPalette();

  // Canonical (symlink-resolved) absolute path, or empty if no file is loaded.
  QString filePath() const { return m_filePath; }

  // Heading outline of the current document, in document order. Each id
  // matches an anchor in the rendered HTML, so the outline panel can jump
  // to it via the same path as an in-document #anchor click. Refreshed on
  // load and on hot-reload; headingsChanged() fires after each refresh.
  const QList<mdv::Heading> &headings() const { return m_headings; }

  // Scroll so the heading carrying `anchorId` sits at the top of the
  // viewport. No-op if the id isn't a known heading anchor.
  void scrollToHeading(const QString &anchorId);

  // Anchor id of the last heading at or above the top of the viewport — the
  // section the reader is currently in — or empty if scrolled above the first
  // heading. Drives the outline's scroll-spy highlight.
  QString currentHeadingId() const;

  // This document's own outline panel: each DocumentView owns one, so split
  // groups can independently show or hide their outline. The side and the
  // shown/hidden default are seeded from (and written back to) QSettings, so a
  // newly opened document inherits the last choice.
  bool outlineVisible() const { return m_outlineVisible; }
  void setOutlineVisible(bool on);
  void toggleOutline() { setOutlineVisible(!m_outlineVisible); }

  // Basename of the loaded file, or a placeholder if none.
  QString displayName() const;

  // Document-relative scroll anchor: the character offset at the top of
  // the visible viewport. Stable across viewport-size changes (a wider
  // or narrower group reflows the text, so raw scrollbar values aren't
  // meaningful — but the position of "this character" in the document
  // is). Used by "Split" actions to land the new group at the same place
  // in the document as the source, regardless of group width.
  int topAnchor() const;

  // Scroll so the given character position is at the top of the viewport.
  // Deferred until the document's layout has been computed.
  void scrollToAnchor(int position);

  // Tab metadata — toggled via the tab context menu.
  bool isPinned() const { return m_pinned; }
  // Emits pinnedChanged() so the owning group can mark the tab with its pin
  // sash and keep pinned tabs clustered. No-op if the state is unchanged.
  void setPinned(bool on);

  bool isWatching() const { return m_watching; }
  // Start/stop watching the file on disk. While watching, an external write
  // triggers an in-place reload that preserves zoom and scroll position.
  void setWatching(bool on);

signals:
  void fileLoaded(const QString &canonicalPath);

  // The heading outline was (re)built — on load and on every hot-reload.
  // headings() reflects the new outline by the time this fires.
  void headingsChanged();

  // The section the reader is in changed as the document scrolled. Carries
  // the anchor id of the current heading (empty above the first heading).
  void currentHeadingChanged(const QString &anchorId);

  // This document's outline was shown/hidden. Lets the View-menu toggle track
  // the active document's state.
  void outlineVisibilityChanged(bool visible);

  // The pinned state changed — the owning EditorGroup repaints the tab's pin
  // sash and (next step) repositions it within the pinned cluster.
  void pinnedChanged(bool pinned);

  // A local-file link was clicked. The path is resolved (absolute, cleaned)
  // against this document's directory; the owner opens it as a new tab.
  void openFileRequested(const QString &path);

protected:
  // Intercept Ctrl+wheel on the browser viewport to zoom and then re-apply
  // heading sizes (which don't scale with the default font on their own).
  bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
  void onContextMenuRequested(const QPoint &pos);
  void onAnchorClicked(const QUrl &url);
  // The watched file changed on disk — debounced before reloadFromDisk().
  void onFileChanged(const QString &path);
  // Vertical scroll moved — recompute the current section and emit
  // currentHeadingChanged() only when it actually changes.
  void onScrolled();

private:
  QTextBrowser *m_browser = nullptr;
  QSplitter *m_splitter = nullptr;
  OutlinePanel *m_outline = nullptr;
  bool m_outlineVisible = true;
  OutlineSide m_outlineSide = OutlineSide::Left;
  int m_outlineWidth = 240;

  QString m_filePath;
  QList<mdv::Heading> m_headings;

  // Scroll-spy cache: each heading anchor id paired with its laid-out y, in
  // document order. Rebuilt lazily (positions move on reflow/zoom) — dirtied
  // on re-render and whenever the document layout resizes. m_lastHeadingId
  // debounces currentHeadingChanged() to actual transitions.
  mutable QList<QPair<QString, qreal>> m_headingY;
  mutable bool m_headingYDirty = true;
  QString m_lastHeadingId;

  bool m_pinned = false;
  bool m_watching = true;

  // Banked Ctrl+wheel delta so sub-notch (<120) scroll events from trackpads
  // and hi-res mice accumulate into whole zoom steps instead of flooring to 0.
  int m_zoomAccum = 0;

  // Hot reload: m_watcher fires on external writes; m_reloadTimer debounces
  // the burst of events editors emit before we re-render in place.
  QFileSystemWatcher *m_watcher = nullptr;
  QTimer *m_reloadTimer = nullptr;

  // Pending scroll anchor state used by scrollToAnchor() to defer until
  // QTextDocument has laid out enough to know each block's geometry.
  // -1 = no pending anchor.
  int m_pendingAnchor = -1;
  QMetaObject::Connection m_pendingScrollConn;
  class QTimer *m_pendingAnchorTimer = nullptr;

  bool tryApplyAnchor();

  // Walk the laid-out document and record (anchor id, y) for every heading
  // block, sorted by y. Fills m_headingY and clears m_headingYDirty.
  void rebuildHeadingPositions() const;

  // (Re)order the outline/browser splitter for m_outlineSide and restore the
  // outline's share of the width.
  void applyOutlineArrangement();
  // Flip the dock side and remember it as the new default.
  void toggleOutlineSide();

  // (Re)point the watcher at m_filePath when watching is on; clears it
  // otherwise. Safe to call repeatedly (used after load and after each reload,
  // since an atomic save drops the watched path).
  void armWatch();

  // Re-render the file in place, preserving zoom (the font is left untouched)
  // and scroll position (remapped across the edit via remapAnchor).
  void reloadFromDisk();

  // Size <h1>..<h6> from the document's default font. Qt's HTML importer
  // stamps a relative FontSizeAdjustment on headings that overrides any
  // explicit/CSS font-size (and orders h5 < h6); this clears that and sets an
  // absolute point size per level. Re-run after every setHtml and after zoom,
  // since absolute sizes don't track the default font the way the adjustment
  // did.
  void applyHeadingSizes();

  // Map a character offset from the old rendered plain text to the new one
  // using a common prefix/suffix diff: unchanged if the edit was below it,
  // shifted by the net length delta if above, best-effort positional if the
  // offset falls inside the changed region.
  int remapAnchor(int pos, const QString &oldText,
                  const QString &newText) const;
};
