# Content Theming

A **content theme** controls the *colors and spacing* of rendered Markdown —
prose, headings, tables, blockquotes, code blocks, and syntax highlighting.
It's a small JSON file; no rebuild is needed to switch themes or to add your
own (import a JSON file — see below).

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

- **Bundled** themes are `resources/themes/<id>.content.json`, registered in
  `resources/resources.qrc` and compiled into the binary.
- **Imported** themes live in a per-user folder, created on first import (open
  it from **File → Preferences → Open themes folder**):
  - Linux: `~/.local/share/gesslar/mdv/themes/`
  - macOS: `~/Library/Application Support/gesslar/mdv/themes/`
  - Windows: `%LOCALAPPDATA%\gesslar\mdv\themes\`
- The **id** is the filename minus `.content.json` (e.g. `blackboard`); the
  **display name** in Preferences is the JSON `name` field. A bundled id wins
  over an imported one of the same name.
- Selection lives in **File → Preferences** and persists in QSettings, and any
  change re-renders every open document immediately:
  - **Follow system color scheme** *off* → a single picker; that theme is used
    always (`theme/content`, default `blackboard`).
  - Follow system *on* → separate **Preferred light** / **Preferred dark**
    pickers (each filtered to themes of that `type`); the OS scheme decides
    which one renders (`theme/light` / `theme/dark`).

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

  "weights": {
    "heading": "300"
  },

  "colors": {
    "text.foreground": "#bebebe",
    "text.background": "#0f0f0f"
    // … see the slot tables below
  }
}
```

- `name` — display name.
- `type` — `dark` or `light`. Drives the Preferences light/dark pickers and
  the follow-system selection, so it must be set.
- `spacing` — CSS length values (see below).
- `weights` — CSS `font-weight` values (strings). Optional; only `heading` is
  read today, defaulting to `200`. The thin default reads well on dark
  backgrounds but washes out on light ones, so light themes set it heavier
  (e.g. `"300"`).
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

## Installing your own theme

No rebuild needed:

1. Write a `.json` file with the structure above. It **must** set `type` to
   `light` or `dark` — Import rejects a theme that doesn't, because the pickers
   bucket by it.
2. **File → Preferences → Import** and pick the file. It's copied into your
   themes folder (above) and selected.
3. To iterate, edit the file in place (**Open themes folder** jumps there) and
   hit **Refresh** to re-read and re-apply it — no re-import, no dialog.
   **Delete** removes the selected imported theme (bundled ones can't be
   deleted, so they show no Delete).

Imported themes appear in the pickers next to the bundled ones, filtered into
the light/dark slots by their `type`.

## Adding a bundled theme (contributors)

To ship a theme with the app instead of importing it:

1. Copy an existing `resources/themes/*.content.json` to
   `resources/themes/<id>.content.json`.
2. Edit `name`, `type`, and the colors/spacing.
3. Add the file to `resources/resources.qrc`.
4. Rebuild (`make build`). The new theme appears in Preferences by its `name`.

See [`examples/`](../examples/) for documents that exercise every element and
language — open them and switch themes to check your work.

Curious how these keys become the rendered stylesheet (placeholder resolution,
the alpha handling, font wiring)? That's developer internals in
[`templating.md`](templating.md).
