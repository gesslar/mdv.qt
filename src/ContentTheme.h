#pragma once

#include <QHash>
#include <QString>

// Loads a content theme JSON, resolves the structural QSS template against
// it (plus font values from QSettings), and exposes the final stylesheet
// for setDefaultStyleSheet on a QTextDocument.
//
// Placeholder syntax inside the template is `@{key}`. The resolver routes
// keys to one of three sources based on prefix:
//   fonts.*     → QSettings (e.g. fonts/prose, fonts/mono.size)
//   spacing.*   → the active theme's "spacing" section
//   <other>     → the active theme's "colors" section
//
// One global ContentTheme is exposed via active(); DocumentView constructs
// apply its qss() to their QTextDocument.
class ContentTheme {
public:
  ContentTheme() = default;

  // Load a theme bundled in the qrc by name. Looks at
  // :/themes/<name>.content.json. Returns true on success.
  bool loadBundled(const QString &name);

  // Load a theme from an arbitrary path on disk.
  bool loadFile(const QString &path);

  // Re-read the currently-selected theme name from QSettings
  // ("theme/content", default "blackboard") and reload the JSON. Used
  // by the Preferences dialog to react to changes.
  void reload();

  // All selectable theme ids. Bundled ids first (walks :/themes/), then any
  // user-imported ids whose stem doesn't shadow a bundled one (walks
  // userThemesDir()). Each group is sorted; the .content.json suffix is
  // stripped.
  static QStringList availableThemes();

  // The human-readable "name" field from a theme's JSON (e.g. "Blackboard"
  // for the "blackboard" id), resolving bundled or user themes. Falls back
  // to the id verbatim if the JSON is missing or doesn't set a name.
  static QString displayNameFor(const QString &id);

  // The "type" field ("light" or "dark") from a theme's JSON, bundled or
  // user. Empty if unset. Used to bucket themes into the light/dark pickers.
  static QString typeFor(const QString &id);

  // True if <id> is a bundled (qrc) theme. Bundled themes are read-only:
  // they cannot be imported over or deleted.
  static bool isBundled(const QString &id);

  // User-space themes directory (created lazily on import). On Linux this is
  // ~/.local/share/<org>/<app>/themes (QStandardPaths::AppDataLocation).
  static QString userThemesDir();

  // Absolute path of a user theme's JSON file, whether or not it exists.
  static QString userThemePath(const QString &id);

  // Copy a theme JSON from srcPath into userThemesDir(), normalizing its
  // name to <id>.content.json. Validates that it parses as a theme object
  // with a non-empty "colors" section and that its id doesn't collide with
  // a bundled theme. On success returns true and sets *outId to the imported
  // id; on failure returns false and sets *error to a user-facing message.
  // Overwrites an existing user theme of the same id.
  static bool importThemeFile(const QString &srcPath, QString *outId,
                              QString *error);

  // Delete a user theme by id. No-op returning false for bundled ids.
  static bool removeUserTheme(const QString &id);

  // Resolved stylesheet ready for QTextDocument::setDefaultStyleSheet.
  QString qss() const;

  // Individual accessors — useful for the KSyntaxHighlighting Theme
  // adapter we'll add later.
  QString color(const QString &key) const;
  QString spacing(const QString &key) const;

  QString name() const { return m_name; }
  QString type() const { return m_type; }

  // Process-wide active theme. Lazily loads "blackboard" on first call.
  static ContentTheme &active();

private:
  QString resolveKey(const QString &key) const;
  QString fontValue(const QString &key) const;

  QString m_name;
  QString m_type;
  QHash<QString, QString> m_colors;
  QHash<QString, QString> m_spacing;
  QHash<QString, QString> m_weights;
};
