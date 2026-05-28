#pragma once

#include <QList>
#include <QString>

// Markdown → HTML rendering helpers built on md4c-html.
//
// We don't use QTextDocument::setMarkdown because Qt's importer bakes
// explicit QTextCharFormat values into the document that override any
// CSS supplied via setDefaultStyleSheet (so prose / heading / code
// theming would silently no-op). Rendering markdown to clean HTML
// externally and then calling setHtml lets the document's stylesheet
// actually take effect.

namespace mdv {

// One heading in the document, in document order. Feeds both the outline
// panel and in-document anchor navigation; `id` is the slug injected onto
// the rendered <hN> tag, so a TOC click and a hand-written [x](#id) link
// resolve to the same anchor.
struct Heading {
  int level;     // 1..6
  QString text;  // plain-text label (inline markup stripped)
  QString id;    // slug, unique within the document
};

struct RenderResult {
  QString html;             // fragment suitable for QTextDocument::setHtml
  QList<Heading> headings;  // document order
};

// Convert GFM-flavored markdown to HTML and extract the heading outline.
// Headings in the returned HTML carry a unique `id` matching the
// corresponding Heading::id.
RenderResult renderMarkdown(const QString &markdown);

}  // namespace mdv
