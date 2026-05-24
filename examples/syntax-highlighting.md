# Syntax Highlighting

Each fenced block below carries a language label, so mdv tokenizes it with
KSyntaxHighlighting and colors it from the active theme's `syntax.*` palette.
Comment markers like `TODO`, `FIXME`, and `HACK` are colored with
`syntax.alert`.

## Bash

```bash
#!/usr/bin/env bash
# TODO: make the prefix configurable
set -euo pipefail

PREFIX="${1:-/usr/local}"
count=0

for file in ./src/*.c; do
  echo "Compiling ${file}..."
  gcc -O2 -Wall -o "${file%.c}.o" "$file" && count=$((count + 1))
done

printf 'Built %d objects\n' "$count"
```

## Python

```python
import sys
from dataclasses import dataclass


@dataclass
class Point:
    """A 2D point."""
    x: float = 0.0
    y: float = 0.0

    def distance_to(self, other: "Point") -> float:
        # FIXME: handle the None case
        return ((self.x - other.x) ** 2 + (self.y - other.y) ** 2) ** 0.5


if __name__ == "__main__":
    p = Point(3, 4)
    print(f"distance = {p.distance_to(Point()):.2f}", file=sys.stderr)
```

## C++

```cpp
#include <string>
#include <vector>

namespace mdv {

// HACK: replace with a real parser
template <typename T>
constexpr T clamp(T value, T low, T high) {
  return value < low ? low : (value > high ? high : value);
}

class Greeter {
 public:
  explicit Greeter(std::string name) : name_(std::move(name)) {}
  std::string hello() const { return "Hello, " + name_ + "!"; }

 private:
  std::string name_;
};

}  // namespace mdv
```

## JavaScript

```javascript
const greet = async (name = "world") => {
  // TODO: localize the greeting
  const re = /^[a-z]+$/i;
  if (!re.test(name)) {
    throw new Error(`invalid name: ${name}`);
  }
  await Promise.resolve();
  return `Hello, ${name}!`;
};

greet("mdv").then(console.log);
```

## Rust

```rust
use std::collections::HashMap;

/// Counts word frequencies.
fn word_counts(text: &str) -> HashMap<&str, u32> {
    let mut counts = HashMap::new();
    for word in text.split_whitespace() {
        *counts.entry(word).or_insert(0) += 1;
    }
    counts
}

fn main() {
    let counts = word_counts("the quick brown fox the");
    println!("{counts:?}");
}
```

## JSON

```json
{
  "name": "mdv",
  "version": "0.1.0",
  "private": true,
  "keywords": ["markdown", "viewer", "qt"],
  "engines": { "node": ">=22" },
  "enabled": true,
  "ratio": 0.92
}
```

## YAML

```yaml
name: build
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Configure
        run: cmake -S . -B build  # configure step
```

## HTML

```html
<!DOCTYPE html>
<html lang="en">
  <head>
    <meta charset="utf-8" />
    <title>Example</title>
  </head>
  <body>
    <h1 class="title">Hello</h1>
    <!-- a comment -->
    <a href="https://example.com">link</a>
  </body>
</html>
```

## SQL

```sql
-- top customers by revenue
SELECT c.name, SUM(o.total) AS revenue
FROM customers AS c
JOIN orders AS o ON o.customer_id = c.id
WHERE o.created_at >= '2026-01-01'
GROUP BY c.name
HAVING SUM(o.total) > 1000
ORDER BY revenue DESC;
```

## No highlighting

A fence labeled `text` (handy for silencing markdownlint MD040) renders plain:

```text
This block has no syntax coloring.
  Indentation and spacing are preserved verbatim.
```

A `none` fence is also deliberately plain:

```none
Also no coloring here.
```

And an unlabeled fence is left exactly as-is:

```
Nothing is highlighted in here either.
$ just preformatted text
```
