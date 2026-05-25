#pragma once

#include <QPoint>
#include <QWidget>

class QFileSystemWatcher;
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

  // The pinned state changed — the owning EditorGroup repaints the tab's pin
  // sash and (next step) repositions it within the pinned cluster.
  void pinnedChanged(bool pinned);

  // A local-file link was clicked. The path is resolved (absolute, cleaned)
  // against this document's directory; the owner opens it as a new tab.
  void openFileRequested(const QString &path);

private slots:
  void onContextMenuRequested(const QPoint &pos);
  void onAnchorClicked(const QUrl &url);
  // The watched file changed on disk — debounced before reloadFromDisk().
  void onFileChanged(const QString &path);

private:
  QTextBrowser *m_browser = nullptr;
  QString m_filePath;
  bool m_pinned = false;
  bool m_watching = true;

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

  // (Re)point the watcher at m_filePath when watching is on; clears it
  // otherwise. Safe to call repeatedly (used after load and after each reload,
  // since an atomic save drops the watched path).
  void armWatch();

  // Re-render the file in place, preserving zoom (the font is left untouched)
  // and scroll position (remapped across the edit via remapAnchor).
  void reloadFromDisk();

  // Map a character offset from the old rendered plain text to the new one
  // using a common prefix/suffix diff: unchanged if the edit was below it,
  // shifted by the net length delta if above, best-effort positional if the
  // offset falls inside the changed region.
  int remapAnchor(int pos, const QString &oldText, const QString &newText) const;
};
