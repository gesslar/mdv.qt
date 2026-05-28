#include "Markdown.h"

#include <md4c-html.h>

#include <QByteArray>
#include <QChar>
#include <QLoggingCategory>
#include <QRegularExpression>
#include <QSet>

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
  while(it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    out.append(QStringView(html).sliced(last, m.capturedStart() - last));
    last = m.capturedEnd();

    // md4c terminates code-block content with a newline, which renders as a
    // trailing blank line inside the <pre>; drop exactly one.
    QString inner = m.captured(2);
    if(inner.endsWith(QLatin1Char('\n'))) inner.chop(1);

    // md4c puts the whole info string in the class; the language is its first
    // whitespace-delimited token.
    const QString lang =
        m.captured(1).split(QLatin1Char(' '), Qt::SkipEmptyParts).value(0);
    // Wrap each block in a single-cell table. Qt's QTextDocument only renders
    // the CSS box model (border + padding) on table cells, never on block
    // elements like <pre>, so the cell is what carries the frame and the inset.
    // It also fills the background as one rectangle instead of the per-line
    // fill a <pre> background does — which is what caused the seam artifacts.
    if(lang.isEmpty()) {
      // Unlabeled / unknown blocks aren't highlighted; re-emit the already-
      // escaped content with the trailing newline trimmed.
      out.append(QStringLiteral(
          "<table class=\"codeblock\" width=\"100%\"><tr><td><pre><code>"));
      out.append(inner);
      out.append(QStringLiteral("</code></pre></td></tr></table>"));
      continue;
    }

    out.append(QStringLiteral("<table class=\"codeblock\" width=\"100%\">"
                              "<tr><td><pre><code class=\"language-%1\">")
                   .arg(lang.toHtmlEscaped()));
    out.append(highlightCode(unescapeHtml(inner), lang));
    out.append(QStringLiteral("</code></pre></td></tr></table>"));
  }
  out.append(QStringView(html).sliced(last));
  return out;
}

// Wrap each top-level <blockquote> in a single-cell table, mirroring the code
// block treatment: Qt only renders the CSS box model (border, padding) on table
// cells, never on block elements, so the cell is what carries the blockquote's
// border and inset. Only the outermost blockquote is wrapped — nested ones ride
// along inside the cell — and we find its matching close by tracking tag depth,
// which a regex can't do for arbitrary nesting.
QString wrapBlockquotes(const QString &html) {
  static const QString open = QStringLiteral("<blockquote>");
  static const QString close = QStringLiteral("</blockquote>");

  QString out;
  out.reserve(html.size() + 64);

  int depth = 0;
  qsizetype copied = 0;  // everything before this index is already in `out`
  qsizetype pos = 0;
  while(pos < html.size()) {
    const qsizetype nextOpen = html.indexOf(open, pos);
    const qsizetype nextClose = html.indexOf(close, pos);
    if(nextClose < 0) break;  // no more well-formed blockquotes

    if(nextOpen >= 0 && nextOpen < nextClose) {
      if(depth == 0) {
        // Flush the prose before this blockquote, then open the wrapper. The
        // blockquote's own markup is emitted when its matching close is found.
        out.append(QStringView(html).sliced(copied, nextOpen - copied));
        out.append(QStringLiteral(
            "<table class=\"blockquote\" width=\"100%\"><tr><td>"));
        copied = nextOpen;
      }
      ++depth;
      pos = nextOpen + open.size();
    } else {
      if(depth > 0 && --depth == 0) {
        const qsizetype end = nextClose + close.size();
        out.append(QStringView(html).sliced(copied, end - copied));
        out.append(QStringLiteral("</td></tr></table>"));
        copied = end;
      }
      pos = nextClose + close.size();
    }
  }
  out.append(QStringView(html).sliced(copied));
  return out;
}

// Strip HTML tags from a heading's inner markup so the outline label and the
// slug are computed from the plain text (md4c renders inline `**`, `code`,
// links etc. as nested tags inside the <hN>).
QString stripTags(QString s) {
  static const QRegularExpression tag(QStringLiteral("<[^>]*>"));
  return s.remove(tag);
}

// GitHub-flavored heading slug, so an anchor generated here matches the one
// GitHub renders for the same heading (the app already parses GFM): lowercase;
// letters, digits, underscores and existing hyphens are kept; each whitespace
// char becomes a hyphen; every other character is dropped. Deliberately does
// *not* collapse the resulting hyphens — "FAQ & Troubleshooting" slugs to
// "faq--troubleshooting" on GitHub, and we mirror that so cross-referenced
// links resolve identically in both places.
QString slugify(const QString &text) {
  QString out;
  out.reserve(text.size());
  for(const QChar ch : text) {
    if(ch.isLetterOrNumber()) out.append(ch.toLower());
    else if(ch == u'_' || ch == u'-') out.append(ch);
    else if(ch.isSpace()) out.append(u'-');
    // else: dropped
  }
  return out;
}

// Inject a unique `id` onto every <h1>..<h6> md4c emitted and collect the
// heading outline in document order. Single source of truth: the id written
// into the HTML is the same id recorded in the Heading, so a TOC click and an
// in-document #anchor link resolve identically. Duplicate slugs get a -1, -2,
// … suffix (GitHub semantics).
QString injectHeadingIds(const QString &html, QList<Heading> &headings) {
  static const QRegularExpression re(
      QStringLiteral("<h([1-6])>(.*?)</h\\1>"),
      QRegularExpression::DotMatchesEverythingOption);

  QString out;
  out.reserve(html.size() + 64);
  qsizetype last = 0;
  QSet<QString> used;

  QRegularExpressionMatchIterator it = re.globalMatch(html);
  while(it.hasNext()) {
    const QRegularExpressionMatch m = it.next();
    out.append(QStringView(html).sliced(last, m.capturedStart() - last));
    last = m.capturedEnd();

    const int level = m.captured(1).toInt();
    const QString inner = m.captured(2);
    const QString text = unescapeHtml(stripTags(inner)).trimmed();

    QString base = slugify(text);
    if(base.isEmpty()) base = QStringLiteral("section");
    QString id = base;
    for(int n = 1; used.contains(id); ++n)
      id = base + QLatin1Char('-') + QString::number(n);
    used.insert(id);

    headings.append({.level = level, .text = text, .id = id});
    out.append(QStringLiteral("<h%1 id=\"%2\">%3</h%1>")
                   .arg(QString::number(level), id.toHtmlEscaped(), inner));
  }
  out.append(QStringView(html).sliced(last));
  return out;
}

}  // namespace

RenderResult renderMarkdown(const QString &markdown) {
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
  if(rc != 0) {
    qCWarning(lcMarkdown) << "md4c parse failed (rc=" << rc
                          << "), falling back to plain-text rendering";
    return {.html =
                QStringLiteral("<pre>%1</pre>").arg(markdown.toHtmlEscaped())};
  }

  // Heading ids are injected first, on the clean md4c output, so the same
  // <hN> text the outline reads is what carries the slug. Blockquotes are
  // wrapped next (where <blockquote> only ever appears as a real tag); code-
  // block highlighting runs last and still finds any <pre><code> nested
  // inside a wrapped quote.
  RenderResult result;
  const QString withIds =
      injectHeadingIds(QString::fromUtf8(html), result.headings);
  result.html = highlightCodeBlocks(wrapBlockquotes(withIds));
  return result;
}

}  // namespace mdv
