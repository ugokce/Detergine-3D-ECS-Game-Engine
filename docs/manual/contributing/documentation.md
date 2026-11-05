---
title: Writing documentation
section: Contributing
order: 10
status: implemented
summary: How the manual is organized, the page template every system page follows, and the rule that keeps it current.
sources: tools/docs/, docs/manual/
---

## Overview

The manual is part of the engine. **A change is not done until the pages that describe it are updated.** That covers
every new system, changed behaviour, new option, and new tool command (CLAUDE.md → *Documentation*). The manual is
written in Markdown under `docs/manual/` and built into a static HTML site in `build/docs/` by
`tools/docs/build_docs.py`. The builder uses only the Python standard library.

| Folder | Section | Contains |
|---|---|---|
| `docs/manual/index.md`, `getting-started.md` | Start here | What Detergine is, first build |
| `docs/manual/architecture/` | Architecture | Layers, determinism model, decisions: the high-level picture |
| `docs/manual/systems/` | Systems | One page per implemented engine system: the low-level detail |
| `docs/manual/tooling/` | Tooling | Build system, `dg`, launcher, tests, CI |
| `docs/manual/planned/` | Planned systems | Designed systems that are not implemented yet, linking to `docs/DESIGN.md` |
| `docs/manual/reference/` | Reference | Glossary (the engine dictionary) and other lookup material |
| `docs/manual/contributing/` | Contributing | This page and other contributor guides |

`docs/DESIGN.md` remains the architecture *decision* record. The manual explains what exists, how to use it, and how to
change it.

## How to use

```bash
./dg docs            # build the site into build/docs/
./dg docs --open     # build and open it in a browser (works straight from disk)
./dg docs --check    # validate: front matter, required sections, links, source coverage
```

The site has a sidebar ordered by section, a per-page table of contents, a search box (press `/`), an
[A–Z index](../a-z.html) that combines page titles and glossary terms, light and dark themes, and status badges:

| Status | Meaning |
|---|---|
| `implemented` | Built and tested. The page must follow the system template below. |
| `partial` | Partly built. The page must follow the template and say clearly what is missing. |
| `design` | Designed in `docs/DESIGN.md`, not built yet. |
| `planned` | On the roadmap, not designed in detail yet. |

## How to edit

### Page front matter

Every page starts with a front-matter block:

```markdown
---
title: Fixed-point numbers
section: Systems
order: 40
status: implemented
summary: One sentence shown under the title, in search results and in the A–Z index.
sources: engine/math/include/dg/math/Fixed.h, engine/math/src/Fixed.cpp, tests/math/FixedTest.cpp
---
```

* `section` is one of: Start here, Architecture, Systems, Tooling, Planned systems, Reference, Contributing.
* `order` sorts pages within a section. Leave gaps (10, 20, 30) so pages can be inserted later.
* `sources` lists the repository files or directories (ending in `/`) the page documents. Every engine module, app, and
  tool must be covered by some page's `sources`, or `dg docs --check` fails. The `sources` list is also how the checker
  knows which page has to change when a file changes.

### The system page template

Pages in **Systems** and **Tooling** with status `implemented` or `partial` must contain these `##` sections, in this
order. You can add sections between them.

1. **Overview**: what the system is for, where it sits in the architecture, and its key guarantees (for example,
   determinism).
2. **How to use**: realistic, compilable examples of the public API or the commands, plus the common patterns.
3. **How to edit**: where the code lives, the invariants a change must keep, how to extend it, and what else must change
   with it (tests, golden hashes, other pages).
4. **Testing**: which suites cover it, how to run them, and what the golden values mean.

A **Reference** section (API tables) after *How to edit* is recommended for every system.

### Markdown that the builder understands

Headings (`##`, `###`), paragraphs, **bold**, *italic*, `inline code`, links, fenced code blocks with a language,
tables, nested lists, horizontal rules, and callouts:

> **Note:** a blockquote starting with **Note**, **Tip**, **Warning** or **Important** becomes a coloured callout.

* Link to another page by its Markdown path, for example [the glossary](../reference/glossary.md). Anchors work too:
  [status table](#how-to-use). The checker rejects links to missing pages or headings.
* A repository path written as a link, such as [Fixed.h](engine/math/include/dg/math/Fixed.h), links to GitHub.
* Lines that start with `<` pass through as raw HTML (for a diagram or a `<details>` block).
* Glossary terms are `###` headings on `reference/glossary.md`; each one appears in the A–Z index automatically.

### Keeping the manual current

* `dg docs --check --changed-since <ref>` fails when a file listed in some page's `sources` changed since `<ref>` but
  none of the pages documenting it changed. CI runs it on every push and pull request. The pre-push hook runs
  `dg docs --check` too.
* When a change genuinely needs no documentation (a typo fix, a comment, a refactor with no visible effect), add a
  line `Docs: none (<reason>)` to the commit message.
* New system → new page in `systems/`, plus a glossary entry for every new term, plus links from the architecture
  pages. Moving a system from *Planned systems* to *Systems* means rewriting its page in the template and setting
  `status: implemented`.

## Testing

`./dg docs --check` validates the whole manual. The CI `docs` job runs it together with `--changed-since` for the
commits being pushed. To preview a page while writing it, run `./dg docs --open` and reload the browser after each
build.
