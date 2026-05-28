#include "ContentTheme.h"

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QStyleHints>

namespace {

QString readResourceText(const QString &path) {
  QFile f(path);
  if(!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  return QString::fromUtf8(f.readAll());
}

// Section objects in the JSON use dotted flat keys ("text.foreground"),
// so this is just a string-only copy into a QHash.
QHash<QString, QString> flatten(const QJsonObject &obj) {
  QHash<QString, QString> out;
  for(auto it = obj.begin(); it != obj.end(); ++it) {
    if(it->isString()) out.insert(it.key(), it->toString());
  }
  return out;
}

// Qt's CSS parser reads 8-digit hex colors as #AARRGGBB (Android style)
// instead of CSS3's #RRGGBBAA. So a theme author writing the standard
// "#f472b633" (pink at 20%) actually gets parsed as alpha=0xf4, R=0x72,
// G=0xb6, B=0x33 — olive green. Rotate the trailing alpha byte to the
// front so what Qt parses matches what the author meant. Pass through
// anything that's not an 8-digit hex (#RGB, #RRGGBB, rgba(), named).
QString coerceColor(const QString &value) {
  static const QRegularExpression re(
      QStringLiteral("^#([0-9a-fA-F]{6})([0-9a-fA-F]{2})$"));
  const auto m = re.match(value);
  if(!m.hasMatch()) return value;
  return QStringLiteral("#%1%2").arg(m.captured(2), m.captured(1));
}

// Flatten a (possibly translucent) color onto an opaque backdrop, returning an
// opaque "#rrggbb". QTextDocument fills a block background per text-line, and a
// translucent fill compounds at the line overlaps into faint seams — so block
// backgrounds are pre-composited over the page color rather than left with
// alpha. Authors still write the natural tint; this is the exact color it would
// render as on the page.
QString compositeOver(const QString &fg, const QString &bg) {
  const QColor top(coerceColor(fg));
  if(!top.isValid()) return fg;

  const qreal a = top.alphaF();
  if(a >= 1.0) return top.name(QColor::HexRgb);  // already opaque

  const QColor base(coerceColor(bg));
  if(!base.isValid()) {
    // No backdrop to flatten onto (the theme omitted text.background). Keep the
    // author's translucent tint — the correct hue, even if it can't dodge the
    // per-line seam — rather than silently dropping alpha to a saturated shade.
    // coerceColor (not raw fg) so Qt still parses the #RRGGBBAA ordering right.
    qWarning("ContentTheme: block background '%s' is translucent but "
             "text.background is missing; leaving it translucent",
             qPrintable(fg));
    return coerceColor(fg);
  }

  const auto mix = [a](int t, int b) {
    return qRound(b * (1.0 - a) + t * a);
  };
  return QColor(mix(top.red(), base.red()), mix(top.green(), base.green()),
                mix(top.blue(), base.blue()))
      .name(QColor::HexRgb);
}

// Parse a theme's JSON object (bundled qrc or user dir). Returns an empty
// object if the file is missing or unparseable.
QJsonObject themeJson(const QString &id) {
  const QString path = ContentTheme::isBundled(id)
                           ? QStringLiteral(":/themes/%1.content.json").arg(id)
                           : ContentTheme::userThemePath(id);
  QFile f(path);
  if(!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};

  QJsonParseError err{};
  const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
  if(err.error != QJsonParseError::NoError || !doc.isObject()) return {};
  return doc.object();
}

}  // namespace

bool ContentTheme::loadBundled(const QString &name) {
  return loadFile(QStringLiteral(":/themes/%1.content.json").arg(name));
}

bool ContentTheme::loadFile(const QString &path) {
  QFile f(path);
  if(!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

  QJsonParseError err{};
  const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
  if(err.error != QJsonParseError::NoError || !doc.isObject()) return false;

  const QJsonObject root = doc.object();
  m_name = root.value("name").toString();
  m_type = root.value("type").toString();
  m_colors = flatten(root.value("colors").toObject());
  m_spacing = flatten(root.value("spacing").toObject());
  m_weights = flatten(root.value("weights").toObject());
  return true;
}

QString ContentTheme::color(const QString &key) const {
  return m_colors.value(key);
}

QString ContentTheme::spacing(const QString &key) const {
  return m_spacing.value(key);
}

QString ContentTheme::fontValue(const QString &key) const {
  // Placeholder keys arrive as "fonts.prose" or "fonts.prose.size".
  // QSettings treats '/' as a group separator, so we only convert the
  // *first* dot (the section/key boundary) — leaving any further dots
  // intact lets a key like "fonts.prose.size" map to QSettings path
  // "fonts/prose.size" (group "fonts", key "prose.size"), which is
  // where PreferencesDialog::saveToSettings actually writes it.
  // Blindly replacing every dot would mis-route it to a non-existent
  // "fonts/prose/size" subgroup.
  const int firstDot = key.indexOf(QLatin1Char('.'));
  const QString settingsKey =
      (firstDot < 0) ? key
                     : QStringLiteral("%1/%2").arg(key.left(firstDot),
                                                   key.mid(firstDot + 1));

  QString stored = QSettings().value(settingsKey).toString();
  if(!stored.isEmpty()) {
    // The size dialog stores a bare integer (e.g. "14"); the CSS needs
    // a unit. Append px if missing.
    if(key.endsWith(QStringLiteral(".size")) &&
       !stored.endsWith(QStringLiteral("px")) &&
       !stored.endsWith(QStringLiteral("pt"))) {
      stored += QStringLiteral("px");
    }
    return stored;
  }

  if(key == "fonts.prose") return QStringLiteral("sans-serif");
  if(key == "fonts.mono") return QStringLiteral("monospace");
  if(key == "fonts.prose.size") return QStringLiteral("14px");
  // fonts.mono.size is no longer referenced by the template (mono
  // inherits relative size in em) but keep the fallback in case any
  // future stylesheet wants it.
  if(key == "fonts.mono.size") return QStringLiteral("13px");
  return {};
}

QString ContentTheme::resolveKey(const QString &key) const {
  if(key.startsWith(QStringLiteral("fonts."))) return fontValue(key);

  if(key.startsWith(QStringLiteral("spacing."))) {
    return m_spacing.value(key.mid(8));  // strip "spacing."
  }

  if(key.startsWith(QStringLiteral("weights."))) {
    QString v = m_weights.value(key.mid(8));  // strip "weights."
    if(!v.isEmpty()) return v;
    // Default so the stylesheet never emits `font-weight: ;`. 200 keeps the
    // app's thin-heading look — fine on dark; light themes set it heavier.
    return QStringLiteral("200");
  }

  // Block-fill backgrounds get flattened onto the page color: the document
  // engine paints them per text-line and a translucent fill seams at the
  // overlaps. Authors can still use alpha here; it just renders opaque.
  static const QSet<QString> blockBackgrounds = {
      QStringLiteral("code.background"),
      QStringLiteral("blockquote.background"),
      QStringLiteral("table.header.background"),
  };
  if(blockBackgrounds.contains(key)) {
    return compositeOver(m_colors.value(key),
                         m_colors.value(QStringLiteral("text.background")));
  }

  // Anything else is a color key. Bare ("text.foreground") in the
  // template — looked up directly in the flattened colors map, then
  // run through coerceColor so 8-digit hex authored as #RRGGBBAA gets
  // rotated to Qt's #AARRGGBB ordering.
  return coerceColor(m_colors.value(key));
}

QString ContentTheme::qss() const {
  const QString tpl = readResourceText(":/content/content.qss.template");
  static const QRegularExpression re(QStringLiteral(R"(@\{([^}]+)\})"));

  QString out;
  out.reserve(tpl.size());
  qsizetype pos = 0;
  auto it = re.globalMatch(tpl);
  while(it.hasNext()) {
    const auto m = it.next();
    out.append(tpl.mid(pos, m.capturedStart() - pos));
    out.append(resolveKey(m.captured(1)));
    pos = m.capturedEnd();
  }
  out.append(tpl.mid(pos));
  return out;
}

void ContentTheme::reload() {
  QSettings s;

  // When following the system, the active theme is chosen by the OS color
  // scheme (Unknown counts as dark). Otherwise it's the single manual pick.
  QString name;
  if(s.value(QStringLiteral("theme/followSystem"), false).toBool()) {
    const bool light = QGuiApplication::styleHints()->colorScheme() ==
                       Qt::ColorScheme::Light;
    name = light ? s.value(QStringLiteral("theme/light"),
                           QStringLiteral("whiteboard"))
                       .toString()
                 : s.value(QStringLiteral("theme/dark"),
                           QStringLiteral("blackboard"))
                       .toString();
  } else {
    name = s.value(QStringLiteral("theme/content"),
                   QStringLiteral("blackboard"))
               .toString();
  }

  // Bundled ids resolve from the qrc; everything else is a user import on
  // disk (e.g. a theme the user deleted out from under the setting falls
  // through to the fallback below).
  const bool ok =
      isBundled(name) ? loadBundled(name) : loadFile(userThemePath(name));
  if(ok) return;

  qWarning("ContentTheme: failed to load theme '%s'", qPrintable(name));

  // Fall back to the canonical default so the document still renders
  // with a sensible theme rather than an empty stylesheet (which would
  // emit invalid CSS like `color: ;` for every @{...} placeholder).
  if(name != QStringLiteral("blackboard")) {
    loadBundled(QStringLiteral("blackboard"));
  }
}

QStringList ContentTheme::availableThemes() {
  QStringList bundledNames;
  QDir bundled(QStringLiteral(":/themes"));
  const auto bundledFiles =
      bundled.entryInfoList({QStringLiteral("*.content.json")}, QDir::Files);
  for(const QFileInfo &fi : bundledFiles) bundledNames << fi.baseName();
  bundledNames.sort();

  const QSet<QString> bundledSet(bundledNames.begin(), bundledNames.end());

  QStringList userNames;
  QDir userDir(userThemesDir());
  if(userDir.exists()) {
    const auto userFiles =
        userDir.entryInfoList({QStringLiteral("*.content.json")}, QDir::Files);
    for(const QFileInfo &fi : userFiles) {
      // A user file whose stem shadows a bundled id is hidden — the bundled
      // theme is authoritative (importThemeFile rejects such collisions, so
      // this only fires for files dropped in by hand).
      if(!bundledSet.contains(fi.baseName())) userNames << fi.baseName();
    }
  }
  userNames.sort();

  return bundledNames + userNames;
}

QString ContentTheme::displayNameFor(const QString &id) {
  const QString name = themeJson(id).value(QStringLiteral("name")).toString();
  return name.isEmpty() ? id : name;
}

QString ContentTheme::typeFor(const QString &id) {
  return themeJson(id).value(QStringLiteral("type")).toString();
}

bool ContentTheme::isBundled(const QString &id) {
  return QFile::exists(QStringLiteral(":/themes/%1.content.json").arg(id));
}

QString ContentTheme::userThemesDir() {
  return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) +
         QStringLiteral("/themes");
}

QString ContentTheme::userThemePath(const QString &id) {
  return QStringLiteral("%1/%2.content.json").arg(userThemesDir(), id);
}

bool ContentTheme::importThemeFile(const QString &srcPath, QString *outId,
                                   QString *error) {
  const auto fail = [&](const QString &msg) {
    if(error) *error = msg;
    return false;
  };

  QFile in(srcPath);
  if(!in.open(QIODevice::ReadOnly | QIODevice::Text))
    return fail(QCoreApplication::translate(
        "ContentTheme", "Could not read the selected file."));

  QJsonParseError perr{};
  const auto doc = QJsonDocument::fromJson(in.readAll(), &perr);
  if(perr.error != QJsonParseError::NoError || !doc.isObject())
    return fail(QCoreApplication::translate(
        "ContentTheme", "Not a valid theme: expected a JSON object."));
  const QJsonObject obj = doc.object();
  if(obj.value(QStringLiteral("colors")).toObject().isEmpty())
    return fail(QCoreApplication::translate(
        "ContentTheme", "Not a valid theme: missing a \"colors\" section."));
  const QString type = obj.value(QStringLiteral("type")).toString();
  if(type != QStringLiteral("light") && type != QStringLiteral("dark"))
    return fail(QCoreApplication::translate(
        "ContentTheme", R"(Theme must set "type" to "light" or "dark".)"));

  const QString id = QFileInfo(srcPath).baseName();
  if(id.isEmpty())
    return fail(QCoreApplication::translate(
        "ContentTheme", "Could not derive a theme name from the file."));
  if(isBundled(id))
    return fail(QCoreApplication::translate(
        "ContentTheme",
        "A bundled theme already uses the name \"%1\". Rename the file and "
        "try again.")
                    .arg(id));

  QDir dir(userThemesDir());
  if(!dir.exists() && !dir.mkpath(QStringLiteral(".")))
    return fail(QCoreApplication::translate(
        "ContentTheme", "Could not create the themes directory."));

  const QString dest = userThemePath(id);
  // If the source already IS the destination — e.g. the user opened the themes
  // folder and re-imported a file straight from there — it's already in place.
  // Skip the remove+copy: removing dest would delete srcPath (same file), then
  // the copy would fail, destroying the theme.
  if(QFileInfo(srcPath).canonicalFilePath() !=
     QFileInfo(dest).canonicalFilePath()) {
    if(QFile::exists(dest) && !QFile::remove(dest))
      return fail(QCoreApplication::translate(
          "ContentTheme", "Could not overwrite the existing theme."));
    if(!QFile::copy(srcPath, dest))
      return fail(QCoreApplication::translate(
          "ContentTheme", "Could not copy the theme into place."));
  }

  if(outId) *outId = id;
  return true;
}

bool ContentTheme::removeUserTheme(const QString &id) {
  if(isBundled(id)) return false;  // bundled themes are read-only
  return QFile::remove(userThemePath(id));
}

ContentTheme &ContentTheme::active() {
  static ContentTheme instance;
  static bool initialized = false;
  if(!initialized) {
    instance.reload();
    initialized = true;
  }
  return instance;
}
