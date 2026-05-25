#pragma once

#include <QString>

namespace mdv {

// Syntax-highlight a code snippet to an HTML fragment of <span style="...">
// runs, colored from the active ContentTheme's syntax.* palette.
//
// `language` is the markdown fence info string (e.g. "cpp", "python", "js").
// When it resolves to a known KSyntaxHighlighting definition the code is
// tokenized and colored; otherwise the snippet is returned HTML-escaped but
// uncolored. The returned fragment is meant to live inside <pre><code>...
QString highlightCode(const QString &code, const QString &language);

} // namespace mdv
