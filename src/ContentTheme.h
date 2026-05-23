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

  // List of bundled theme ids (e.g. "blackboard", "bubblegum-goth").
  // Walks :/themes/ and strips the .content.json suffix.
  static QStringList availableThemes();

  // The human-readable "name" field from the bundled theme's JSON
  // (e.g. "Blackboard" for the "blackboard" id). Falls back to the id
  // verbatim if the JSON is missing or doesn't set a name.
  static QString displayNameForBundled(const QString &id);

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
};
