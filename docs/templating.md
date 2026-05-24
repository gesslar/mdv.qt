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

(Code-block token colors are a fourth, separate path — inline `<span style>`
emitted by the KSyntaxHighlighting pass; see `src/Highlighter.cpp`.)

All three are applied in `DocumentView`'s constructor and re-applied in
`DocumentView::refresh()`, which Preferences calls after a theme/font change.

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

QTextDocument fills a block's background **per text-line**, and adjacent line
rectangles overlap by a sub-pixel — so a *translucent* block fill compounds at
the seams into faint horizontal lines. To avoid that, the block-fill background
keys are pre-composited onto the page color and resolve **opaque**:

- `code.background`
- `blockquote.background`
- `table.header.background`

(see the `blockBackgrounds` set in `ContentTheme::resolveKey`). The composite is
exact — it's the color the translucent tint would show over `text.background` —
so authors keep writing natural translucent tints. Opaque inputs pass through
unchanged.

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
- `src/DocumentView.cpp` — applies the three channels (stylesheet / font / palette)
