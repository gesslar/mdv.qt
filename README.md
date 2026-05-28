# mdv

A fast, minimal desktop Markdown viewer built with Qt 6 and C++.

## Features

- **Markdown rendering** — CommonMark + GitHub-flavored (tables, task lists,
  strikethrough, autolinks) via [md4c](https://github.com/mity/md4c)
- **Automatic syntax highlighting** for fenced code blocks via
  [KSyntaxHighlighting](https://invent.kde.org/frameworks/syntax-highlighting),
  colored from the active theme
- **Content themes** — swappable color/spacing themes for the rendered
  document; six bundled as three dark/light pairs, plus follow-the-system
  light/dark switching, and you can author your own
  (see [THEMING.md](docs/THEMING.md))
- **Tabs and splits** — open documents in tabs, split a group right or down,
  drag tabs between groups
- **Drag-and-drop** — drop markdown files onto the window to open them
- **Recent files** — with reopen-last-closed and reopen-all
- **Familiar shortcuts** — `Ctrl+W` close, `Ctrl+Shift+T` reopen, `Ctrl+\`
  split, `Ctrl+PgUp`/`PgDn` to cycle tabs, and more
- **Preferences** — pick or import content themes (or follow the system
  light/dark scheme), set the prose/monospace fonts; `Ctrl`+scroll to zoom
- **CLI** — pass one or more file paths: `mdv a.md b.md`

## Building

See **[DEVELOPMENT.md](DEVELOPMENT.md)** for prerequisites (Qt 6, CMake,
Ninja; KSyntaxHighlighting on Linux/macOS — fetched in-tree on Windows).
Then:

```bash
make            # dev build → build/mdv
make run        # build, then launch
make dist       # OS-native installer → dist/
                #   Linux:   .deb + .rpm via CPack
                #   Windows: NSIS .exe with per-user / per-machine choice
```

## Usage

```bash
mdv                      # empty window
mdv README.md            # open a file
mdv ~/Documents/*.md     # one tab per file
```

Or drag a markdown file onto the window.

## Theming

Content themes control the **colors and spacing** of the rendered document
(fonts come from Preferences, so a theme is portable across font choices).
Six themes ship with mdv as three dark/light pairs (Blackboard/Whiteboard,
Corporate/Sky diving, Bubblegum Goth/Cherry adjacent), and Preferences can
follow the system light/dark scheme; authoring your own is a small JSON
file — see **[THEMING.md](docs/THEMING.md)**.

## License

`mdv` is released under the [0BSD](LICENSE.txt) license.

It builds on these components, under their own licenses:

| Dependency | License |
| --- | --- |
| [Qt 6](https://www.qt.io/) (Widgets) | LGPL-3.0 |
| [md4c](https://github.com/mity/md4c) | MIT |
| [KSyntaxHighlighting](https://invent.kde.org/frameworks/syntax-highlighting) | MIT |
| [QWindowKit](https://github.com/stdware/qwindowkit) (custom title bar / frameless window) | Apache-2.0 |
| [Codicons](https://github.com/microsoft/vscode-codicons) (bundled UI icons) | MIT |
