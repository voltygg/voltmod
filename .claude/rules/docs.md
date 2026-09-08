---
paths:
  - "**/*.md"
  - "include/**/*.hpp"
  - "templates/**"
---

# Comments and docs

- Comment only for contracts, ownership, lifetime, concurrency, module boundaries, build behavior, or compatibility. One line inside code; a public contract may take a few Doxygen lines. Delete narration and essays.
- Plain names. If a reader needs the comment to decode the identifier, rename the identifier.
- Doxygen for non-obvious public contracts; keep symbols and tags exact.
- `docs/` is task-first: command, path, expected result. Plain English, sentence-case headings. VoltMod is "the framework".
- Keep examples and `templates/` in sync with public headers and generated output.
