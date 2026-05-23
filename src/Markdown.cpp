#include "Markdown.h"

#include <md4c-html.h>

#include <QByteArray>
#include <QLoggingCategory>

namespace mdv {

namespace {

Q_LOGGING_CATEGORY(lcMarkdown, "mdv.markdown")

void appendChunk(const MD_CHAR *text, MD_SIZE size, void *userData) {
  auto *out = static_cast<QByteArray *>(userData);
  out->append(text, static_cast<int>(size));
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

  return QString::fromUtf8(html);
}

}  // namespace mdv
