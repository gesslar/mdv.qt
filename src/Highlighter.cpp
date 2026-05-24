#include "Highlighter.h"

#include <initializer_list>

#include <QChar>
#include <QHash>
#include <QStringList>
#include <QStringView>

#include <KSyntaxHighlighting/AbstractHighlighter>
#include <KSyntaxHighlighting/Definition>
#include <KSyntaxHighlighting/Format>
#include <KSyntaxHighlighting/Repository>
#include <KSyntaxHighlighting/State>
#include <KSyntaxHighlighting/Theme>

#include "ContentTheme.h"

namespace mdv {

namespace {

namespace KSH = KSyntaxHighlighting;

// One repository for the whole process — constructing it loads every bundled
// syntax definition and theme, a cost we only want to pay once.
KSH::Repository &repository() {
  static KSH::Repository repo;
  return repo;
}

// Resolve a fence info string to a syntax definition. definitionForName is
// case-insensitive and matches alternative names (so "cpp" → C++), but a few
// common highlight.js-style short forms aren't alternative names; bridge those.
KSH::Definition definitionFor(const QString &language) {
  const QString lang = language.trimmed();
  if (lang.isEmpty()) return {};

  if (KSH::Definition def = repository().definitionForName(lang); def.isValid()) {
    return def;
  }

  static const QHash<QString, QString> aliases = {
      {QStringLiteral("js"), QStringLiteral("JavaScript")},
      {QStringLiteral("ts"), QStringLiteral("TypeScript")},
      {QStringLiteral("py"), QStringLiteral("Python")},
      {QStringLiteral("sh"), QStringLiteral("Bash")},
      {QStringLiteral("shell"), QStringLiteral("Bash")},
      {QStringLiteral("console"), QStringLiteral("Bash")},
      {QStringLiteral("yml"), QStringLiteral("YAML")},
      {QStringLiteral("rs"), QStringLiteral("Rust")},
      {QStringLiteral("rb"), QStringLiteral("Ruby")},
      {QStringLiteral("kt"), QStringLiteral("Kotlin")},
      {QStringLiteral("md"), QStringLiteral("Markdown")},
  };
  const QString mapped = aliases.value(lang.toLower());
  if (!mapped.isEmpty()) return repository().definitionForName(mapped);
  return {};
}

// A resolved token appearance: color plus optional weight/slant taken from the
// theme's "<key>.style" sibling ("bold" | "italic" | "bold italic").
struct TokenStyle {
  QString color;
  bool bold = false;
  bool italic = false;
  bool isEmpty() const { return color.isEmpty(); }
};

// Return the first candidate key the active theme defines, paired with its
// .style sibling. The candidate lists let a theme add richer keys (syntax.error,
// syntax.builtin, …) while falling back to the base palette.
TokenStyle resolve(std::initializer_list<const char *> keys) {
  const ContentTheme &theme = ContentTheme::active();
  for (const char *key : keys) {
    const QString name = QString::fromLatin1(key);
    const QString color = theme.color(name);
    if (color.isEmpty()) continue;

    TokenStyle ts;
    ts.color = color;
    const QString style = theme.color(name + QStringLiteral(".style"));
    ts.bold = style.contains(QLatin1String("bold"));
    ts.italic = style.contains(QLatin1String("italic"));
    return ts;
  }
  return {};
}

// Map a KSyntaxHighlighting default style onto a theme token. An empty result
// means "no style" — the run inherits the code block's foreground.
TokenStyle styleForTextStyle(KSH::Theme::TextStyle style) {
  using T = KSH::Theme;
  switch (style) {
    case T::Keyword:
    case T::Import:
      return resolve({"syntax.keyword"});
    case T::ControlFlow:
      return resolve({"syntax.controlflow", "syntax.keyword"});
    case T::Preprocessor:
      return resolve({"syntax.preprocessor", "syntax.keyword"});
    case T::Function:
      return resolve({"syntax.function"});
    case T::BuiltIn:
    case T::Extension:
      return resolve({"syntax.builtin", "syntax.function"});
    case T::Variable:
      return resolve({"syntax.variable"});
    case T::Attribute:
      return resolve({"syntax.attribute", "syntax.variable"});
    case T::DataType:
      return resolve({"syntax.type"});
    case T::Constant:
      return resolve({"syntax.constant"});
    case T::String:
    case T::Char:
    case T::VerbatimString:
      return resolve({"syntax.string"});
    case T::SpecialString:
      return resolve({"syntax.regex", "syntax.string"});
    case T::SpecialChar:
      return resolve({"syntax.operator", "syntax.string"});
    case T::DecVal:
    case T::BaseN:
    case T::Float:
      return resolve({"syntax.number"});
    case T::Operator:
      return resolve({"syntax.operator"});
    case T::Comment:
    case T::Documentation:
    case T::Annotation:
    case T::CommentVar:
    case T::RegionMarker:
    case T::Information:
      return resolve({"syntax.comment"});
    case T::Warning:
      return resolve({"syntax.warning", "syntax.regex"});
    case T::Alert:
      return resolve({"syntax.alert", "syntax.regex"});
    case T::Error:
      return resolve({"syntax.error", "syntax.regex"});
    case T::Normal:
    case T::Others:
    default:
      return {};
  }
}

// Walks a snippet line by line and emits an HTML fragment of color-styled
// <span> runs. KSyntaxHighlighting calls applyFormat() for each formatted run;
// we fill the gaps between runs (and any trailing text) as un-styled Normal.
class FragmentHighlighter : public KSH::AbstractHighlighter {
 public:
  QString run(const QString &code, const KSH::Definition &def) {
    setDefinition(def);
    // A theme must be set for the highlighter to function; we read styles via
    // Format::textStyle() and supply our own colors, so which theme is moot.
    setTheme(repository().defaultTheme(KSH::Repository::DarkTheme));

    m_html.clear();
    const QStringList lines = code.split(QLatin1Char('\n'));
    KSH::State state;
    for (qsizetype i = 0; i < lines.size(); ++i) {
      m_line = lines.at(i);
      m_pos = 0;
      state = highlightLine(m_line, state);
      if (m_pos < m_line.size()) {
        appendRun(m_line.sliced(m_pos), KSH::Theme::Normal);
      }
      if (i + 1 < lines.size()) m_html.append(QLatin1Char('\n'));
    }
    return m_html;
  }

 protected:
  void applyFormat(int offset, int length, const KSH::Format &format) override {
    if (length <= 0) return;
    if (offset > m_pos) {
      appendRun(m_line.sliced(m_pos, offset - m_pos), KSH::Theme::Normal);
    }
    appendRun(m_line.sliced(offset, length), format.textStyle());
    m_pos = offset + length;
  }

 private:
  void appendRun(QStringView text, KSH::Theme::TextStyle style) {
    const QString escaped = text.toString().toHtmlEscaped();
    const TokenStyle ts = styleForTextStyle(style);
    if (ts.isEmpty()) {
      m_html.append(escaped);
      return;
    }
    QString css = QStringLiteral("color:%1").arg(ts.color);
    if (ts.bold) css.append(QLatin1String(";font-weight:bold"));
    if (ts.italic) css.append(QLatin1String(";font-style:italic"));
    m_html.append(QStringLiteral("<span style=\"%1\">%2</span>").arg(css, escaped));
  }

  QString m_html;
  QStringView m_line;
  int m_pos = 0;
};

}  // namespace

QString highlightCode(const QString &code, const QString &language) {
  const KSH::Definition def = definitionFor(language);
  if (!def.isValid()) return code.toHtmlEscaped();

  FragmentHighlighter highlighter;
  return highlighter.run(code, def);
}

}  // namespace mdv
