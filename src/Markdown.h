#pragma once

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

// Convert GFM-flavored markdown to HTML. Output is a fragment (no
// <html>/<head>/<body> wrappers) suitable for QTextDocument::setHtml.
QString markdownToHtml(const QString &markdown);

} // namespace mdv
