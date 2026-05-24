#include "Markdown.h"

#include <md4c-html.h>

#include <QByteArray>
#include <QChar>
#include <QLoggingCategory>
#include <QRegularExpression>

#include "Highlighter.h"

namespace mdv {

namespace {

Q_LOGGING_CATEGORY(lcMarkdown, "mdv.markdown")

void appendChunk(const MD_CHAR *text, MD_SIZE size, void *userData) {
  auto *out = static_cast<QByteArray *>(userData);
  out->append(text, static_cast<int>(size));
}

// Reverse md4c's HTML escaping of code-block content so we can re-tokenize it.
// &amp; must come last so an already-decoded "&" isn't re-interpreted.
QString unescapeHtml(QString s) {
  s.replace(QLatin1String("&lt;"), QLatin1String("<"));
  s.replace(QLatin1String("&gt;"), QLatin1String(">"));
  s.replace(QLatin1String("&quot;"), QLatin1String("\""));
  s.replace(QLatin1String("&#39;"), QLatin1String("'"));
  s.replace(QLatin1String("&amp;"), QLatin1String("&"));
  return s;
}

// Run each fenced code block that carries a language through the syntax
// highlighter. Blocks without a language are left exactly as md4c emitted them
// (KSyntaxHighlighting has no content-based auto-detection like highlight.js).
QString highlightCodeBlocks(const QString &html) {
  static const QRegularExpression re(
      QStringLiteral(
          "<pre><code(?: class=\"language-([^\"]*)\")?>(.*?)</code></pre>"),
      QRegularExpression::DotMatchesEverythingOption);

  QString out;
  out.reserve(html.size());
  qsizetype last = 0;
  QRegularExpressionMatchIterator it = re.globalMatch(html);
  while (it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    out.append(QStringView(html).sliced(last, m.capturedStart() - last));
    last = m.capturedEnd();

    // md4c terminates code-block content with a newline, which renders as a
    // trailing blank line inside the <pre>; drop exactly one.
    QString inner = m.captured(2);
    if (inner.endsWith(QLatin1Char('\n'))) inner.chop(1);

    // md4c puts the whole info string in the class; the language is its first
    // whitespace-delimited token.
    const QString lang =
        m.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts).value(0);
    if (lang.isEmpty()) {
      // Unlabeled / unknown blocks aren't highlighted; re-emit the already-
      // escaped content with the trailing newline trimmed.
      out.append(QStringLiteral("<pre><code>"));
      out.append(inner);
      out.append(QStringLiteral("</code></pre>"));
      continue;
    }

    out.append(QStringLiteral("<pre><code class=\"language-%1\">")
                   .arg(lang.toHtmlEscaped()));
    out.append(highlightCode(unescapeHtml(inner), lang));
    out.append(QStringLiteral("</code></pre>"));
  }
  out.append(QStringView(html).sliced(last));
  return out;
}

}  // namespace

QString markdownToHtml(const QString &markdown) {
  const QByteArray utf8 = markdown.toUtf8();

  // GFM dialect — tables, task lists, strikethrough, autolinks — matches
  // what Qt's setMarkdown was doing.
  constexpr unsigned parserFlags = MD_DIALECT_GITHUB;
  // No special render flags — we want a clean HTML fragment.
  constexpr unsigned rendererFlags = 0;

  QByteArray html;
  html.reserve(utf8.size() * 2);

  const int rc = md_html(utf8.constData(), static_cast<MD_SIZE>(utf8.size()),
                         appendChunk, &html, parserFlags, rendererFlags);

  // md_html returns 0 on success, -1 on failure. On failure the output
  // is incomplete or empty, which would let DocumentView push a blank
  // setHtml and show nothing. Fall back to a plain-text rendering of the
  // raw markdown so the user at least sees the content (and the warning
  // tells them something went wrong).
  if (rc != 0) {
    qCWarning(lcMarkdown) << "md4c parse failed (rc=" << rc
                          << "), falling back to plain-text rendering";
    return QStringLiteral("<pre>%1</pre>").arg(markdown.toHtmlEscaped());
  }

  return highlightCodeBlocks(QString::fromUtf8(html));
}

}  // namespace mdv
