# Content Theming

A **content theme** controls the *colors and spacing* of rendered Markdown —
prose, headings, tables, blockquotes, code blocks, and syntax highlighting.
It's a small JSON file; no rebuild is needed to switch between bundled themes.

What a theme does **not** control, by design:

- **Fonts** — the prose and monospace families/sizes live in app settings
  (File → Preferences), so a theme is portable across whatever fonts you pick.
- **Structure** — borders, radii, padding *shapes*, table layout, and the
  code-block frame are structural decisions baked into
  `resources/content/content.qss.template`. Themes fill in the values; they
  don't change the geometry.
- **App chrome** — the window/menu/tab styling is a separate concern (not yet
  themable).

## Where themes live and how they're selected

- Bundled themes are `resources/themes/<id>.content.json`, registered in
  `resources/resources.qrc`.
- The **id** is the filename minus `.content.json` (e.g. `blackboard`). The
  **display name** shown in Preferences is the JSON `name` field.
- The active theme is chosen in **File → Preferences** and stored in QSettings
  (`theme/content`, default `blackboard`). Changing it re-renders every open
  document immediately.

## File structure

```json
{
  "name": "Blackboard",
  "type": "dark",

  "spacing": {
    "paragraph":   "1em",
    "heading":     "1.5em",
    "list.indent": "1.5em",
    "table.cell":  "1em",
    "block.pad":   "1em",
    "block.indent": "1em"
  },

  "colors": {
    "text.foreground": "#bebebe",
    "text.background": "#0f0f0f"
    // … see the slot tables below
  }
}
```

- `name` — display name.
- `type` — `dark` or `light` (informational).
- `spacing` — CSS length values (see below).
- `colors` — hex colors and the `syntax.*` palette (see below).

## Color format

Write colors as **standard CSS hex**: `#RGB`, `#RRGGBB`, or `#RRGGBBAA` — the
8-digit form adds alpha (e.g. `#5d88af66` is the accent at 40%), handy for
translucent borders and backgrounds.

Keep `syntax.*` token colors **solid** (6-digit, no alpha) — they color text,
where transparency isn't meaningful.

Block-fill backgrounds (`code.background`, `blockquote.background`,
`table.header.background`) may use alpha, but they're flattened onto
`text.background` at load and render **opaque**. You author the tint normally;
it just resolves to the exact color it would show over the page.

## Spacing keys

| Key            | Applies to                                  |
| -------------- | ------------------------------------------- |
| `paragraph`    | vertical margin of paragraphs, lists, quotes |
| `heading`      | space above headings                         |
| `list.indent`  | left indent of `ul` / `ol`                   |
| `table.cell`   | padding of table cells                       |
| `block.pad`    | padding inside fenced code blocks and blockquotes |
| `block.indent` | horizontal recess of code blocks and blockquotes |

## Color keys

| Key                       | Styles                                  |
| ------------------------- | --------------------------------------- |
| `text.foreground`         | body text                               |
| `text.background`         | document background                     |
| `heading.foreground`      | all heading text (h1–h6)                |
| `heading.border`          | h1 underline rule                       |
| `link.foreground`         | links (always underlined)               |
| `code.foreground`         | inline-code text                        |
| `code.background`         | fenced code-block background            |
| `code.border`             | fenced code-block border                |
| `code.inline.background`  | inline-code background                  |
| `code.inline.outline`     | inline-code border                      |
| `blockquote.foreground`   | blockquote text                         |
| `blockquote.background`   | blockquote background                   |
| `blockquote.border`       | blockquote left bar                     |
| `table.border`            | cell separators                         |
| `table.header.background` | header-row background                   |
| `table.header.foreground` | header-row text and underline           |
| `hr.foreground`           | horizontal rule                         |

## Syntax highlighting

Code blocks are tokenized by KSyntaxHighlighting and colored from the
`syntax.*` palette. Each token is a color, with an optional **`<token>.style`**
sibling for weight/slant:

```json
"syntax.keyword":       "#739abd",
"syntax.keyword.style": "bold",
"syntax.comment":       "#636363",
"syntax.comment.style": "italic",
"syntax.alert":         "#cdb968",
"syntax.alert.style":   "bold italic"
```

`*.style` accepts `bold`, `italic`, or `bold italic` (omit for normal).

### Tokens

These are read directly:

| Key                | Covers                                            |
| ------------------ | ------------------------------------------------- |
| `syntax.keyword`   | keywords, imports                                 |
| `syntax.function`  | function/method names                             |
| `syntax.type`      | data types / classes                              |
| `syntax.variable`  | variables                                         |
| `syntax.constant`  | constants                                         |
| `syntax.string`    | strings, chars, verbatim strings                  |
| `syntax.number`    | decimals, hex/octal, floats                       |
| `syntax.operator`  | operators, escape chars                           |
| `syntax.comment`   | comments, docstrings, annotations                 |
| `syntax.regex`     | regex / interpolated string bits                  |
| `syntax.error`     | error tokens                                      |
| `syntax.warning`   | warning tokens                                    |
| `syntax.alert`     | in-comment markers (`TODO`, `FIXME`, `HACK`)      |

### Optional refinements (fallbacks)

A theme only needs to define what it wants — unset keys fall back to a base
token, so highlighting always works. Define these to draw finer distinctions:

| Key                   | Falls back to      |
| --------------------- | ------------------ |
| `syntax.controlflow`  | `syntax.keyword`   |
| `syntax.preprocessor` | `syntax.keyword`   |
| `syntax.builtin`      | `syntax.function`  |
| `syntax.attribute`    | `syntax.variable`  |
| `syntax.error` / `warning` / `alert` | `syntax.regex` |

`.style` follows the same fallback chain as the color.

### Unhighlighted blocks

Fences with no language — or labeled `text` / `none` — render plain. Use
` ```text ` to keep a block uncolored while satisfying linters (markdownlint
MD040).

## Adding a bundled theme

1. Copy an existing `resources/themes/*.content.json` to
   `resources/themes/<id>.content.json`.
2. Edit `name`, `type`, and the colors/spacing.
3. Add the file to `resources/resources.qrc`.
4. Rebuild (`make build`). The new theme appears in Preferences by its `name`.

See [`examples/`](examples/) for documents that exercise every element and
language — open them and switch themes to check your work.

Curious how these keys become the rendered stylesheet (placeholder resolution,
the alpha handling, font wiring)? That's developer internals in
[`docs/templating.md`](docs/templating.md).
