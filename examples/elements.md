# Markdown Elements

This document shows every Markdown element mdv styles. Use it to check a theme
end to end.

## Headings

# Heading 1
## Heading 2
### Heading 3
#### Heading 4
##### Heading 5
###### Heading 6

## Text emphasis

Regular paragraph text with **bold**, *italic*, ***bold italic***, and
~~strikethrough~~. Inline `code spans` sit inline with surrounding prose, and a
[link to somewhere](https://example.com) carries the link styling. A bare
autolink: <https://example.com>.

A second paragraph follows the first to show paragraph spacing. Lorem ipsum
dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt
ut labore et dolore magna aliqua.

## Lists

Unordered, with nesting:

- First item
- Second item
  - Nested item
  - Another nested item
    - Deeper still
- Third item

Ordered:

1. First step
2. Second step
   1. Sub-step a
   2. Sub-step b
3. Third step

Task list:

- [x] Completed task
- [ ] Pending task
- [ ] Another pending task

## Tables

| Option        | Description                  | Default |
| ------------- | ---------------------------- | ------- |
| `--name`      | Package name (required)      | —       |
| `--version`   | Package version              | `1.0.0` |
| `--release`   | Release number               | `1`     |
| `--verbose`   | Print extra diagnostics      | `false` |

## Blockquotes

> A single-level blockquote. It should pick up the blockquote border, background
> and foreground from the active theme.
>
> > A nested blockquote, one level deeper.

## Inline vs block code

Inline code like `printf("%d\n", n)` is styled distinctly from a fenced block:

```c
#include <stdio.h>

int main(void) {
  printf("Hello, world!\n");
  return 0;
}
```

## Horizontal rule

Above the rule.

---

Below the rule.
