#include "Markdown.h"

#include <md4c-html.h>

#include <QByteArray>

namespace mdv {

namespace {

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

  md_html(utf8.constData(), static_cast<MD_SIZE>(utf8.size()), appendChunk,
          &html, parserFlags, rendererFlags);

  return QString::fromUtf8(html);
}

}  // namespace mdv
