#include "ContentTheme.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>

namespace {

QString readResourceText(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
  return QString::fromUtf8(f.readAll());
}

// Section objects in the JSON use dotted flat keys ("text.foreground"),
// so this is just a string-only copy into a QHash.
QHash<QString, QString> flatten(const QJsonObject &obj) {
  QHash<QString, QString> out;
  for (auto it = obj.begin(); it != obj.end(); ++it) {
    if (it->isString()) out.insert(it.key(), it->toString());
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
  if (!m.hasMatch()) return value;
  return QStringLiteral("#%1%2").arg(m.captured(2), m.captured(1));
}

}  // namespace

bool ContentTheme::loadBundled(const QString &name) {
  return loadFile(QStringLiteral(":/themes/%1.content.json").arg(name));
}

bool ContentTheme::loadFile(const QString &path) {
  QFile f(path);
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return false;

  QJsonParseError err{};
  const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) return false;

  const QJsonObject root = doc.object();
  m_name = root.value("name").toString();
  m_type = root.value("type").toString();
  m_colors = flatten(root.value("colors").toObject());
  m_spacing = flatten(root.value("spacing").toObject());
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
      (firstDot < 0)
          ? key
          : QStringLiteral("%1/%2").arg(key.left(firstDot),
                                        key.mid(firstDot + 1));

  QString stored = QSettings().value(settingsKey).toString();
  if (!stored.isEmpty()) {
    // The size dialog stores a bare integer (e.g. "14"); the CSS needs
    // a unit. Append px if missing.
    if (key.endsWith(QStringLiteral(".size")) &&
        !stored.endsWith(QStringLiteral("px")) &&
        !stored.endsWith(QStringLiteral("pt"))) {
      stored += QStringLiteral("px");
    }
    return stored;
  }

  if (key == "fonts.prose")      return QStringLiteral("sans-serif");
  if (key == "fonts.mono")       return QStringLiteral("monospace");
  if (key == "fonts.prose.size") return QStringLiteral("14px");
  // fonts.mono.size is no longer referenced by the template (mono
  // inherits relative size in em) but keep the fallback in case any
  // future stylesheet wants it.
  if (key == "fonts.mono.size")  return QStringLiteral("13px");
  return {};
}

QString ContentTheme::resolveKey(const QString &key) const {
  if (key.startsWith(QStringLiteral("fonts."))) return fontValue(key);

  if (key.startsWith(QStringLiteral("spacing."))) {
    return m_spacing.value(key.mid(8));  // strip "spacing."
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
  while (it.hasNext()) {
    const auto m = it.next();
    out.append(tpl.mid(pos, m.capturedStart() - pos));
    out.append(resolveKey(m.captured(1)));
    pos = m.capturedEnd();
  }
  out.append(tpl.mid(pos));
  return out;
}

void ContentTheme::reload() {
  const QString name =
      QSettings().value(QStringLiteral("theme/content"), QStringLiteral("blackboard"))
          .toString();
  loadBundled(name);
}

QStringList ContentTheme::availableThemes() {
  QStringList names;
  QDir bundled(QStringLiteral(":/themes"));
  const auto files =
      bundled.entryInfoList({QStringLiteral("*.content.json")}, QDir::Files);
  for (const QFileInfo &fi : files) names << fi.baseName();
  names.sort();
  return names;
}

QString ContentTheme::displayNameForBundled(const QString &id) {
  QFile f(QStringLiteral(":/themes/%1.content.json").arg(id));
  if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return id;

  QJsonParseError err{};
  const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
  if (err.error != QJsonParseError::NoError || !doc.isObject()) return id;

  const QString name = doc.object().value(QStringLiteral("name")).toString();
  return name.isEmpty() ? id : name;
}

ContentTheme &ContentTheme::active() {
  static ContentTheme instance;
  static bool initialized = false;
  if (!initialized) {
    instance.reload();
    initialized = true;
  }
  return instance;
}
