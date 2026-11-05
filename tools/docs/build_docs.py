#!/usr/bin/env python3
"""Detergine manual builder: docs/manual/**/*.md -> a static HTML site in build/docs/.

Standard library only. Pages are Markdown with a small front-matter block:

    ---
    title: Fixed-point numbers
    section: Systems
    order: 40
    status: implemented          # implemented | partial | design | planned
    summary: Exact Q16.16 and Q32.32 arithmetic for gameplay values.
    sources: engine/math/include/dg/math/Fixed.h, engine/math/src/Fixed.cpp
    ---

Run through the dg tool: `dg docs` (build), `dg docs --open`, `dg docs --check` (validate),
`dg docs --check --changed-since <git-ref>` (also require doc updates for changed sources).
"""

from __future__ import annotations

import html
import json
import re
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
MANUAL = ROOT / "docs" / "manual"
ASSETS = Path(__file__).resolve().parent / "assets"
OUTPUT = ROOT / "build" / "docs"
REPO_URL = "https://github.com/ugokce/Detergine-3D-ECS-Game-Engine/blob/main/"

# Navigation order. A page's `section` must be one of these.
SECTIONS = ["Start here", "Architecture", "Systems", "Tooling", "Planned systems", "Reference", "Contributing"]
STATUSES = {
    "implemented": "Implemented",
    "partial": "Partially implemented",
    "design": "Designed, not implemented",
    "planned": "Planned",
}
# Sections whose implemented/partial pages must follow the system page template.
TEMPLATE_SECTIONS = {"Systems", "Tooling"}
REQUIRED_HEADINGS = ["Overview", "How to use", "How to edit", "Testing"]
REQUIRED_KEYS = ["title", "section", "order", "status", "summary"]

# Everything listed here must be covered by some page's `sources` (a path or a directory prefix ending in "/").
COVERAGE_ROOTS = ["engine/*/", "apps/*/", "tools/dg.py", "tools/dg_launcher.py", "tools/docs/", "cmake/",
                  "CMakeLists.txt", "CMakePresets.json", "vcpkg.json", "tests/", ".github/workflows/"]
# Commit-message trailer that opts a change out of the doc-update check, e.g. "Docs: none (typo fix)".
DOCS_TRAILER = re.compile(r"^Docs:\s*none\b", re.IGNORECASE | re.MULTILINE)


@dataclass
class Page:
    source: Path                      # docs/manual/<rel>.md
    rel: str                          # e.g. "systems/fixed"
    meta: dict[str, str]
    body: str
    html: str = ""
    headings: list[tuple[int, str, str]] = field(default_factory=list)  # (level, text, id)
    links: list[str] = field(default_factory=list)
    text: str = ""

    @property
    def url(self) -> str:
        return self.rel + ".html"

    @property
    def sources(self) -> list[str]:
        return [s.strip() for s in self.meta.get("sources", "").split(",") if s.strip()]


# ---------------------------------------------------------------------------------------------------------------------
# Markdown subset: headings, paragraphs, emphasis, inline code, links, images, lists (nested), fenced code, tables,
# blockquotes / callouts, horizontal rules, and raw HTML blocks (lines starting with "<").


def slugify(text: str) -> str:
    text = re.sub(r"<[^>]+>", "", text)
    text = re.sub(r"[^\w\s-]", "", text.lower()).strip()
    return re.sub(r"[\s_]+", "-", text) or "section"


class Converter:
    def __init__(self, page_rel: str) -> None:
        self.page_rel = page_rel
        self.headings: list[tuple[int, str, str]] = []
        self.links: list[str] = []
        self.used_ids: set[str] = set()

    # -- inline ---------------------------------------------------------------------------------------------------

    def inline(self, text: str) -> str:
        codes: list[str] = []

        def stash(match: re.Match) -> str:
            codes.append(f"<code>{html.escape(match.group(1))}</code>")
            return f"\x00{len(codes) - 1}\x00"

        text = re.sub(r"`([^`]+)`", stash, text)
        text = html.escape(text, quote=False)

        def image(match: re.Match) -> str:
            return f'<img alt="{match.group(1)}" src="{self.link_target(match.group(2))}">'

        def link(match: re.Match) -> str:
            return f'<a href="{self.link_target(match.group(2))}">{match.group(1)}</a>'

        text = re.sub(r"!\[([^\]]*)\]\(([^)\s]+)\)", image, text)
        text = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)", link, text)
        text = re.sub(r"\*\*(.+?)\*\*", r"<strong>\1</strong>", text)
        text = re.sub(r"(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", r"<em>\1</em>", text)
        text = re.sub(r"(?<![\w])_(?!\s)(.+?)(?<!\s)_(?![\w])", r"<em>\1</em>", text)
        return re.sub(r"\x00(\d+)\x00", lambda m: codes[int(m.group(1))], text)

    def link_target(self, target: str) -> str:
        if re.match(r"^[a-z]+:", target) or target.startswith("#"):
            return target
        path, _, anchor = target.partition("#")
        if path.endswith(".md"):
            self.links.append(target)
            path = path[:-3] + ".html"
        elif not path.startswith(("../", "./")) and "/" in path and not path.endswith((".html", ".png", ".svg")):
            # A repository path such as engine/math/src/Fixed.cpp: link to GitHub.
            return REPO_URL + target
        return path + ("#" + anchor if anchor else "")

    # -- blocks ---------------------------------------------------------------------------------------------------

    def convert(self, markdown: str) -> str:
        return self.blocks(markdown.split("\n"))

    def heading_id(self, text: str) -> str:
        base = slugify(text)
        candidate, n = base, 2
        while candidate in self.used_ids:
            candidate, n = f"{base}-{n}", n + 1
        self.used_ids.add(candidate)
        return candidate

    def blocks(self, lines: list[str]) -> str:
        out: list[str] = []
        i = 0
        while i < len(lines):
            line = lines[i]
            stripped = line.strip()
            if not stripped:
                i += 1
                continue
            fence = re.match(r"^(\s*)```\s*([\w+-]*)", line)
            if fence:
                lang, body = fence.group(2), []
                i += 1
                while i < len(lines) and not lines[i].strip().startswith("```"):
                    body.append(lines[i])
                    i += 1
                i += 1
                cls = f' class="language-{lang}"' if lang else ""
                out.append(f"<pre><code{cls}>{html.escape(chr(10).join(body))}</code></pre>")
                continue
            heading = re.match(r"^(#{1,4})\s+(.*?)\s*#*$", line)
            if heading:
                level, text = len(heading.group(1)), heading.group(2)
                hid = self.heading_id(text)
                rendered = self.inline(text)
                self.headings.append((level, re.sub(r"<[^>]+>", "", rendered), hid))
                out.append(f'<h{level} id="{hid}"><a class="anchor" href="#{hid}">#</a>{rendered}</h{level}>')
                i += 1
                continue
            if re.match(r"^(-{3,}|\*{3,})$", stripped):
                out.append("<hr>")
                i += 1
                continue
            if stripped.startswith("|") and i + 1 < len(lines) and re.match(r"^\s*\|?[\s:|-]+\|[\s:|-]*$", lines[i + 1]):
                rows = []
                while i < len(lines) and lines[i].strip().startswith("|"):
                    rows.append(lines[i])
                    i += 1
                out.append(self.table(rows))
                continue
            if stripped.startswith(">"):
                quote = []
                while i < len(lines) and lines[i].strip().startswith(">"):
                    quote.append(re.sub(r"^\s*>\s?", "", lines[i]))
                    i += 1
                inner = self.blocks(quote)
                kind = re.match(r"<p><strong>(Note|Warning|Tip|Important)", inner)
                cls = f' class="callout callout-{kind.group(1).lower()}"' if kind else ""
                out.append(f"<blockquote{cls}>{inner}</blockquote>")
                continue
            if re.match(r"^\s*([-*+]|\d+[.)])\s+", line):
                block = []
                while i < len(lines):
                    current = lines[i]
                    if not current.strip():
                        nxt = lines[i + 1] if i + 1 < len(lines) else ""
                        if nxt.startswith((" ", "\t")) or re.match(r"^\s*([-*+]|\d+[.)])\s+", nxt):
                            block.append("")
                            i += 1
                            continue
                        break
                    if block and not current.startswith((" ", "\t")) and \
                            not re.match(r"^\s*([-*+]|\d+[.)])\s+", current):
                        break
                    block.append(current)
                    i += 1
                out.append(self.list_block(block))
                continue
            if stripped.startswith("<"):
                raw = []
                while i < len(lines) and lines[i].strip():
                    raw.append(lines[i])
                    i += 1
                out.append("\n".join(raw))
                continue
            para = []
            while i < len(lines) and lines[i].strip() and not re.match(
                    r"^(#{1,4}\s|```|>|\s*([-*+]|\d+[.)])\s+|\|)", lines[i].strip() if not lines[i].startswith("    ")
                    else "x"):
                para.append(lines[i].strip())
                i += 1
            if not para:  # a line the paragraph rule rejected but no block rule took; emit it as text
                para.append(lines[i].strip())
                i += 1
            out.append(f"<p>{self.inline(' '.join(para))}</p>")
        return "\n".join(out)

    def list_block(self, lines: list[str]) -> str:
        indent = len(lines[0]) - len(lines[0].lstrip())
        ordered = bool(re.match(r"^\s*\d+[.)]\s+", lines[0]))
        items: list[list[str]] = []
        for line in lines:
            match = re.match(r"^(\s*)([-*+]|\d+[.)])\s+(.*)$", line)
            if match and len(match.group(1)) <= indent:
                items.append([match.group(3)])
            elif items:
                items[-1].append(line[indent + 2:] if line.startswith(" " * (indent + 2)) else line.strip())
        parts = []
        for item in items:
            first, rest = item[0], item[1:]
            while rest and not rest[-1].strip():
                rest.pop()
            if rest:
                nested_start = next((k for k, l in enumerate(rest) if re.match(r"^\s*([-*+]|\d+[.)])\s+", l)), None)
                text_lines = [first] + (rest[:nested_start] if nested_start is not None else rest)
                body = self.inline(" ".join(t.strip() for t in text_lines if t.strip()))
                if nested_start is not None:
                    body += self.blocks(rest[nested_start:])
                parts.append(f"<li>{body}</li>")
            else:
                parts.append(f"<li>{self.inline(first)}</li>")
        tag = "ol" if ordered else "ul"
        return f"<{tag}>{''.join(parts)}</{tag}>"

    def table(self, rows: list[str]) -> str:
        def cells(row: str) -> list[str]:
            row = row.strip()
            if row.startswith("|"):
                row = row[1:]
            if row.endswith("|") and not row.endswith("\\|"):
                row = row[:-1]
            return [c.strip().replace("\\|", "|") for c in re.split(r"(?<!\\)\|", row)]

        head = cells(rows[0])
        aligns = []
        for spec in cells(rows[1]):
            aligns.append("center" if spec.startswith(":") and spec.endswith(":") else
                          "right" if spec.endswith(":") else "")
        out = ["<div class=\"table-wrap\"><table><thead><tr>"]
        for k, cell in enumerate(head):
            style = f' style="text-align:{aligns[k]}"' if k < len(aligns) and aligns[k] else ""
            out.append(f"<th{style}>{self.inline(cell)}</th>")
        out.append("</tr></thead><tbody>")
        for row in rows[2:]:
            out.append("<tr>" + "".join(f"<td>{self.inline(c)}</td>" for c in cells(row)) + "</tr>")
        out.append("</tbody></table></div>")
        return "".join(out)


# ---------------------------------------------------------------------------------------------------------------------
# Loading and validation


def parse_page(path: Path) -> Page:
    raw = path.read_text(encoding="utf-8")
    meta: dict[str, str] = {}
    body = raw
    match = re.match(r"^---\n(.*?)\n---\n", raw, re.S)
    if match:
        for line in match.group(1).splitlines():
            key, sep, value = line.partition(":")
            if sep:
                meta[key.strip()] = re.sub(r"\s+#.*$", "", value.strip())
        body = raw[match.end():]
    rel = path.relative_to(MANUAL).with_suffix("").as_posix()
    return Page(path, rel, meta, body)


def load_pages() -> list[Page]:
    pages = [parse_page(p) for p in sorted(MANUAL.rglob("*.md"))]
    for page in pages:
        converter = Converter(page.rel)
        page.html = converter.convert(page.body)
        page.headings = converter.headings
        page.links = converter.links
        page.text = re.sub(r"\s+", " ", re.sub(r"<[^>]+>", " ", page.html)).strip()
    return pages


def order_key(page: Page) -> tuple[int, float, str]:
    section = page.meta.get("section", "")
    try:
        order = float(page.meta.get("order", "1000"))
    except ValueError:
        order = 1000.0
    return (SECTIONS.index(section) if section in SECTIONS else len(SECTIONS), order, page.meta.get("title", ""))


def covered(path: str, pages: list[Page]) -> bool:
    for page in pages:
        for source in page.sources:
            if path == source or (source.endswith("/") and path.startswith(source)):
                return True
    return False


def coverage_targets() -> list[str]:
    targets = []
    for pattern in COVERAGE_ROOTS:
        if pattern.endswith("*/"):
            base = ROOT / pattern[:-2]
            targets += [f"{d.relative_to(ROOT).as_posix()}/" for d in sorted(base.iterdir()) if d.is_dir()]
        elif (ROOT / pattern).exists():
            targets.append(pattern)
    return targets


def target_covered(target: str, pages: list[Page]) -> bool:
    if not target.endswith("/"):
        return covered(target, pages)
    # A directory counts as covered when the directory itself, or every file in it, is covered.
    if covered(target, pages):
        return True
    files = [f for f in project_files() if f.startswith(target)]
    return bool(files) and all(covered(f, pages) for f in files)


def project_files() -> list[str]:
    """Files git tracks or would track (gitignored files and OS clutter such as .DS_Store are not project files)."""
    try:
        out = subprocess.run(["git", "ls-files", "--cached", "--others", "--exclude-standard"], cwd=ROOT,
                             capture_output=True, text=True, check=True).stdout
        return [line for line in out.splitlines() if line and not line.rsplit("/", 1)[-1].startswith(".")]
    except (OSError, subprocess.CalledProcessError):
        return [f.relative_to(ROOT).as_posix() for f in ROOT.rglob("*")
                if f.is_file() and not f.name.startswith(".") and "build" not in f.parts]


def validate(pages: list[Page]) -> list[str]:
    errors = []
    by_rel = {p.rel: p for p in pages}
    for page in pages:
        where = page.source.relative_to(ROOT).as_posix()
        for key in REQUIRED_KEYS:
            if not page.meta.get(key):
                errors.append(f"{where}: missing front-matter key '{key}'")
        if page.meta.get("section") and page.meta["section"] not in SECTIONS:
            errors.append(f"{where}: unknown section '{page.meta['section']}' (one of: {', '.join(SECTIONS)})")
        if page.meta.get("status") and page.meta["status"] not in STATUSES:
            errors.append(f"{where}: unknown status '{page.meta['status']}' (one of: {', '.join(STATUSES)})")
        if page.meta.get("section") in TEMPLATE_SECTIONS and page.meta.get("status") in ("implemented", "partial"):
            present = {text for level, text, _ in page.headings if level == 2}
            for required in REQUIRED_HEADINGS:
                if required not in present:
                    errors.append(f"{where}: system pages need a '## {required}' section")
            if not page.sources:
                errors.append(f"{where}: implemented system pages must list their `sources`")
        for source in page.sources:
            if not (ROOT / source).exists():
                errors.append(f"{where}: source '{source}' does not exist")
        for link in page.links:
            path, _, anchor = link.partition("#")
            target_rel = (Path(page.rel).parent / path[:-3]).as_posix()
            target_rel = re.sub(r"(^|/)\./", r"\1", target_rel)
            while "/../" in "/" + target_rel:
                target_rel = re.sub(r"(^|/)[^/]+/\.\./", r"\1", target_rel, count=1)
                if target_rel.startswith("../"):
                    break
            target = by_rel.get(target_rel)
            if target is None:
                errors.append(f"{where}: broken link '{link}'")
            elif anchor and anchor not in {hid for _, _, hid in target.headings}:
                errors.append(f"{where}: link '{link}' points to a missing heading")
    for target in coverage_targets():
        if not target_covered(target, pages):
            errors.append(f"coverage: '{target}' is not documented; add it to a page's `sources`")
    return errors


def check_changed(pages: list[Page], base: str) -> list[str]:
    """Every changed source file must come with a change to a page that documents it (or a `Docs: none` trailer)."""
    def git(*args: str) -> str:
        return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True, check=True).stdout

    messages = git("log", "--format=%B", f"{base}..HEAD")
    if DOCS_TRAILER.search(messages):
        return []
    changed = [line for line in git("diff", "--name-only", f"{base}...HEAD").splitlines() if line]
    changed_docs = {c for c in changed if c.startswith("docs/manual/")}
    errors = []
    for path in changed:
        if path.startswith(("docs/", "build/")) or path.endswith(".md"):
            continue
        documenting = [p for p in pages if any(path == s or (s.endswith("/") and path.startswith(s))
                                               for s in p.sources)]
        if documenting and not any(p.source.relative_to(ROOT).as_posix() in changed_docs for p in documenting):
            names = ", ".join(p.source.relative_to(ROOT).as_posix() for p in documenting)
            errors.append(f"'{path}' changed but its documentation did not ({names}). Update the page, or add a "
                          "'Docs: none (<reason>)' line to the commit message.")
    return errors


# ---------------------------------------------------------------------------------------------------------------------
# Rendering


def relative_root(page_rel: str) -> str:
    depth = page_rel.count("/")
    return "../" * depth if depth else "./"


def render_nav(pages: list[Page], current: Page | None, root: str) -> str:
    out = []
    for section in SECTIONS:
        members = [p for p in pages if p.meta.get("section") == section]
        if not members:
            continue
        out.append(f'<div class="nav-section"><div class="nav-title">{html.escape(section)}</div><ul>')
        for p in members:
            active = ' class="active" aria-current="page"' if current is p else ""
            badge = f'<span class="dot dot-{p.meta.get("status", "")}" title="{STATUSES.get(p.meta.get("status", ""), "")}"></span>'
            out.append(f'<li><a href="{root}{p.url}"{active}>{badge}{html.escape(p.meta.get("title", p.rel))}</a></li>')
        out.append("</ul></div>")
    out.append(f'<div class="nav-section"><ul><li><a href="{root}a-z.html"{" class=active" if current is None else ""}>'
               f'A–Z index</a></li></ul></div>')
    return "".join(out)


def layout(title: str, content: str, nav: str, toc: str, root: str, description: str) -> str:
    template = (ASSETS / "template.html").read_text(encoding="utf-8")
    return (template.replace("{{title}}", html.escape(title))
            .replace("{{description}}", html.escape(description))
            .replace("{{root}}", root)
            .replace("{{nav}}", nav)
            .replace("{{toc}}", toc)
            .replace("{{content}}", content))


def render_page(page: Page, pages: list[Page]) -> str:
    root = relative_root(page.rel)
    status = page.meta.get("status", "")
    header = [f'<div class="page-meta"><span class="crumb">{html.escape(page.meta.get("section", ""))}</span>']
    if status:
        header.append(f'<span class="badge badge-{status}">{STATUSES.get(status, status)}</span>')
    header.append("</div>")
    header.append(f'<h1>{html.escape(page.meta.get("title", page.rel))}</h1>')
    if page.meta.get("summary"):
        header.append(f'<p class="lead">{html.escape(page.meta["summary"])}</p>')
    footer = ['<footer class="page-footer">']
    if page.sources:
        links = ", ".join(f'<a href="{REPO_URL}{s}"><code>{html.escape(s)}</code></a>' for s in page.sources)
        footer.append(f"<p><strong>Sources:</strong> {links}</p>")
    edit = page.source.relative_to(ROOT).as_posix()
    footer.append(f'<p class="muted">Edit this page: <a href="{REPO_URL}{edit}"><code>{edit}</code></a></p>')
    ordered = sorted(pages, key=order_key)
    k = ordered.index(page)
    nav = []
    if k > 0:
        nav.append(f'<a class="prev" href="{root}{ordered[k - 1].url}">← {html.escape(ordered[k - 1].meta.get("title", ""))}</a>')
    if k + 1 < len(ordered):
        nav.append(f'<a class="next" href="{root}{ordered[k + 1].url}">{html.escape(ordered[k + 1].meta.get("title", ""))} →</a>')
    footer.append(f'<nav class="pager">{"".join(nav)}</nav></footer>')
    toc_items = [f'<li class="toc-{lvl}"><a href="#{hid}">{html.escape(text)}</a></li>'
                 for lvl, text, hid in page.headings if lvl in (2, 3)]
    toc = f'<div class="toc-title">On this page</div><ul>{"".join(toc_items)}</ul>' if toc_items else ""
    content = "".join(header) + page.html + "".join(footer)
    return layout(page.meta.get("title", page.rel), content, render_nav(pages, page, root), toc, root,
                  page.meta.get("summary", ""))


def render_az(pages: list[Page]) -> str:
    """Dictionary-style index: every page plus every term defined as a heading on the glossary page."""
    entries: list[tuple[str, str, str]] = []
    for page in pages:
        entries.append((page.meta.get("title", page.rel), page.url, page.meta.get("section", "")))
        if page.rel.endswith("glossary"):
            entries += [(text, f"{page.url}#{hid}", "Glossary") for level, text, hid in page.headings if level == 3]
    entries.sort(key=lambda e: e[0].lower())
    groups: dict[str, list[tuple[str, str, str]]] = {}
    for entry in entries:
        letter = entry[0][0].upper() if entry[0][0].isalpha() else "#"
        groups.setdefault(letter, []).append(entry)
    letters = "".join(f'<a href="#az-{l}">{l}</a>' for l in groups)
    body = [f'<h1>A–Z index</h1><p class="lead">Every manual page and glossary term, alphabetically.</p>'
            f'<p class="az-letters">{letters}</p>']
    for letter, items in groups.items():
        body.append(f'<h2 id="az-{letter}">{letter}</h2><ul class="az">')
        body += [f'<li><a href="{url}">{html.escape(name)}</a> <span class="muted">— {html.escape(kind)}</span></li>'
                 for name, url, kind in items]
        body.append("</ul>")
    return layout("A–Z index", "".join(body), render_nav(pages, None, "./"), "", "./", "Alphabetical index")


def build(pages: list[Page]) -> Path:
    OUTPUT.mkdir(parents=True, exist_ok=True)
    pages = sorted(pages, key=order_key)
    for page in pages:
        target = OUTPUT / page.url
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(render_page(page, pages), encoding="utf-8")
    (OUTPUT / "a-z.html").write_text(render_az(pages), encoding="utf-8")
    index = [{"title": p.meta.get("title", p.rel), "url": p.url, "section": p.meta.get("section", ""),
              "summary": p.meta.get("summary", ""), "headings": [[t, h] for lvl, t, h in p.headings if lvl in (2, 3)],
              "text": p.text[:6000]} for p in pages]
    # A script (not JSON + fetch) so search also works when the site is opened from disk (file://).
    (OUTPUT / "search-index.js").write_text("window.DG_SEARCH_INDEX = " + json.dumps(index) + ";\n",
                                            encoding="utf-8")
    for asset in ("style.css", "app.js"):
        (OUTPUT / asset).write_text((ASSETS / asset).read_text(encoding="utf-8"), encoding="utf-8")
    return OUTPUT / "index.html"


def main(argv: list[str]) -> int:
    import argparse
    parser = argparse.ArgumentParser(description="Build or check the Detergine manual")
    parser.add_argument("--check", action="store_true", help="validate pages, links and coverage (no output)")
    parser.add_argument("--changed-since", metavar="REF", help="with --check: require doc updates for changed sources")
    args = parser.parse_args(argv)
    pages = load_pages()
    if args.check:
        errors = validate(pages)
        if args.changed_since:
            errors += check_changed(pages, args.changed_since)
        for error in errors:
            print(f"[docs] error: {error}", file=sys.stderr)
        print(f"[docs] {len(pages)} pages, {len(errors)} problems")
        return 1 if errors else 0
    errors = validate(pages)
    for error in errors:
        print(f"[docs] warning: {error}", file=sys.stderr)
    index = build(pages)
    print(f"[docs] built {len(pages)} pages -> {index}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
