# Content stylesheet templating

How a rendered Markdown document gets its styling. This is developer-facing
internals; for *authoring* a theme, see [`../THEMING.md`](../THEMING.md).

## Three channels (and why)

No single Qt mechanism styles the whole document, so the styling is assembled
across three, each chosen to work around a QTextDocument/QTextBrowser quirk:

1. **Element CSS** → `QTextDocument::setDefaultStyleSheet(ContentTheme::qss())`.
   Headings, code, tables, lists, blockquotes, rules, links. Persists across
   subsequent `setHtml()` calls.
2. **Prose font** → `QTextDocument::setDefaultFont()`. A CSS `body { font-family }`
   doesn't propagate: the md4c HTML fragment has no real `<body>` to bind to, and
   Qt routes the document font through the default `QFont`, not CSS. Set as a
   **point** size so `Ctrl`+wheel zoom (which works in points) scales it.
3. **Page background + default text color** → `QPalette::Base` / `QPalette::Text`
   on the browser. `body { background-color }` doesn't paint the widget viewport;
   QTextBrowser paints it from the palette.

All three are applied in `DocumentView`'s constructor and re-applied in
`DocumentView::refresh()`, which Preferences calls after a theme/font change.

## HTML post-processing

The HTML md4c emits is rewritten once before `setHtml()` — `mdv::markdownToHtml`
runs `wrapBlockquotes` then `highlightCodeBlocks` (`src/Markdown.cpp`):

1. **Code blocks and blockquotes are wrapped in a single-cell table** —
   `<table class="codeblock">` / `<table class="blockquote">`. This is the
   constraint behind the template's whole shape: **Qt renders the CSS box model
   (border + padding) only on table cells, never on block elements.** A `border`
   or `padding` on `<pre>` or `<blockquote>` is silently dropped, so those frames
   live on the wrapping cell instead (`table.codeblock td`, `table.blockquote
   td`). Full width comes from the HTML `width="100%"` attribute — Qt ignores CSS
   `width` on tables. Only the *outermost* blockquote is wrapped (found by
   tracking tag depth, since a regex can't match balanced nesting); nested quotes
   ride along inside the cell.
2. **Code-block tokens become inline `<span style>`** — emitted by the
   KSyntaxHighlighting pass and colored from the `syntax.*` palette
   (`src/Highlighter.cpp`). That's why there are no `syntax.*` rules in the
   template: those colors arrive inline, per token.

So the template's `pre` and `blockquote` rules are deliberately thin (font,
whitespace, text color); the visible frame is carried by the `…td` rules.

## The template

`resources/content/content.qss.template` holds the **structural** CSS — border
styles, padding shapes, radii, `white-space`, relative font sizing — with
`@{key}` placeholders for the tunable values. Structure is a decision and lives
here; colors/spacing/fonts are values and come from the theme or settings.

## Placeholder resolution

`ContentTheme::qss()` substitutes every `@{key}` via `resolveKey()`, which routes
by prefix:

| Prefix       | Source                              | Notes |
| ------------ | ----------------------------------- | ----- |
| `fonts.*`    | `QSettings` (`fontValue()`)         | Set in Preferences |
| `spacing.*`  | theme JSON `spacing` section        | CSS length strings |
| *(anything else)* | theme JSON `colors` section    | run through `coerceColor` / `compositeOver` |

### `fonts.*` → QSettings

`fontValue()` converts only the **first** dot to a QSettings group separator, so
`fonts.prose.size` maps to `fonts/prose.size` (group `fonts`, key `prose.size`)
— matching where `PreferencesDialog` writes it. A `.size` value stored as a bare
integer gets `px` appended. Fallbacks: `sans-serif` / `monospace` / `14px`.

Note the template sizes monospace as `font-size: 0.92em` (relative), so
`Ctrl`+wheel zoom scales code alongside prose.

### `coerceColor` — the 8-digit hex quirk

Qt's stylesheet parser reads 8-digit hex as `#AARRGGBB` (Android order), not
CSS3's `#RRGGBBAA`. So `coerceColor` rotates the trailing alpha byte to the
front, letting authors write standard CSS (`#5d88af66`). 3/6-digit hex, `rgba()`,
and named colors pass through untouched.

### `compositeOver` — block-background flattening

These three background keys all render on **table cells** now (code blocks and
blockquotes are table-wrapped; `th` is already a cell), and a cell fills its
background as one rectangle. `compositeOver` pre-flattens a translucent value to
the exact opaque color it would show over `text.background`:

- `code.background`
- `blockquote.background`
- `table.header.background`

(see the `blockBackgrounds` set in `ContentTheme::resolveKey`.) Authors keep
writing natural translucent tints; they resolve to the equivalent opaque color.
Opaque inputs pass through unchanged.

> Historically this flattening also dodged a seam artifact: before code blocks
> and blockquotes were table-wrapped, their translucent fills were painted *per
> text-line* on the `<pre>`/`<blockquote>` block, and the overlapping line
> rectangles compounded into faint horizontal lines. The single-rectangle cell
> fill avoids that structurally now; flattening is kept for the opaque guarantee
> and gives an identical result.

> Because `text.background` is the composite backdrop **and** is consumed raw via
> `QPalette` in `DocumentView::applyDocumentPalette()`, it should stay opaque.

## Adding a style slot

1. Add the CSS rule to `content.qss.template`, using `@{your.key}`.
2. Add `your.key` to the theme JSONs (`colors` or `spacing`).
3. If it's a **multi-line block background**, add the key to the
   `blockBackgrounds` set in `src/ContentTheme.cpp` so it gets flattened.
4. Document the new slot in `THEMING.md` (the author-facing tables).

## Where the pieces live

- `resources/content/content.qss.template` — structural CSS + `@{…}` slots
- `src/ContentTheme.{h,cpp}` — `resolveKey`, `coerceColor`, `compositeOver`, `qss()`
- `src/Markdown.cpp` — md4c render + HTML post-processing (table wrapping, highlight pass)
- `src/DocumentView.cpp` — applies the three channels (stylesheet / font / palette)
