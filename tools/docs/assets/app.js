// Detergine manual: client-side search, theme toggle, mobile navigation and table-of-contents highlighting.
// Plain script (no modules) so the site also works when opened straight from disk.
(function () {
  "use strict";
  const root = window.DG_ROOT || "./";
  const index = window.DG_SEARCH_INDEX || [];

  // -- theme --------------------------------------------------------------------------------------------------------
  const themeButton = document.querySelector(".theme-toggle");
  if (themeButton) {
    themeButton.addEventListener("click", () => {
      const current = document.documentElement.dataset.theme ||
        (matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "light");
      const next = current === "dark" ? "light" : "dark";
      document.documentElement.dataset.theme = next;
      try { localStorage.setItem("dg-theme", next); } catch (e) { /* storage may be unavailable */ }
    });
  }

  // -- mobile navigation --------------------------------------------------------------------------------------------
  const menu = document.querySelector(".menu-toggle");
  if (menu) {
    menu.addEventListener("click", () => {
      const open = document.body.classList.toggle("nav-open");
      menu.setAttribute("aria-expanded", String(open));
    });
  }

  // -- search -------------------------------------------------------------------------------------------------------
  const input = document.getElementById("search-input");
  const results = document.getElementById("search-results");
  let selected = -1;

  function escapeHtml(text) {
    return text.replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
  }

  function highlight(text, terms) {
    let out = escapeHtml(text);
    for (const term of terms) {
      if (term.length < 2) continue;
      out = out.replace(new RegExp("(" + term.replace(/[.*+?^${}()|[\]\\]/g, "\\$&") + ")", "ig"), "<mark>$1</mark>");
    }
    return out;
  }

  function score(entry, terms) {
    let total = 0;
    const title = entry.title.toLowerCase();
    const text = entry.text.toLowerCase();
    for (const term of terms) {
      let s = 0;
      if (title.includes(term)) s += title.startsWith(term) ? 12 : 8;
      for (const [heading] of entry.headings) if (heading.toLowerCase().includes(term)) s += 4;
      if (entry.summary.toLowerCase().includes(term)) s += 3;
      if (text.includes(term)) s += 1;
      if (s === 0) return 0; // every term must match somewhere
      total += s;
    }
    return total;
  }

  function bestTarget(entry, terms) {
    for (const [heading, id] of entry.headings) {
      if (terms.some((t) => heading.toLowerCase().includes(t))) return { url: entry.url + "#" + id, heading };
    }
    return { url: entry.url, heading: "" };
  }

  function snippet(entry, terms) {
    const lower = entry.text.toLowerCase();
    const at = Math.max(0, ...terms.map((t) => lower.indexOf(t)));
    const start = Math.max(0, at - 60);
    return (start > 0 ? "…" : "") + entry.text.slice(start, start + 160) + "…";
  }

  function render() {
    const terms = input.value.toLowerCase().split(/\s+/).filter(Boolean);
    if (!terms.length) { results.hidden = true; return; }
    const matches = index.map((e) => ({ e, s: score(e, terms) })).filter((m) => m.s > 0)
      .sort((a, b) => b.s - a.s).slice(0, 12);
    selected = -1;
    if (!matches.length) {
      results.innerHTML = '<a class="r-empty">No results</a>';
    } else {
      results.innerHTML = matches.map(({ e }) => {
        const target = bestTarget(e, terms);
        return `<a href="${root}${target.url}"><div class="r-title">${highlight(e.title, terms)}</div>` +
          `<div class="r-meta">${escapeHtml(e.section)}${target.heading ? " › " + highlight(target.heading, terms) : ""}</div>` +
          `<div class="r-snippet">${highlight(snippet(e, terms), terms)}</div></a>`;
      }).join("");
    }
    results.hidden = false;
  }

  if (input && results) {
    input.addEventListener("input", render);
    input.addEventListener("keydown", (event) => {
      const items = results.querySelectorAll("a[href]");
      if (event.key === "ArrowDown" || event.key === "ArrowUp") {
        event.preventDefault();
        if (!items.length) return;
        selected = (selected + (event.key === "ArrowDown" ? 1 : items.length - 1)) % items.length;
        items.forEach((a, k) => a.classList.toggle("selected", k === selected));
        items[selected].scrollIntoView({ block: "nearest" });
      } else if (event.key === "Enter" && items.length) {
        window.location.href = items[Math.max(0, selected)].href;
      } else if (event.key === "Escape") {
        results.hidden = true;
        input.blur();
      }
    });
    document.addEventListener("keydown", (event) => {
      if (event.key === "/" && document.activeElement !== input) { event.preventDefault(); input.focus(); }
    });
    document.addEventListener("click", (event) => {
      if (!results.contains(event.target) && event.target !== input) results.hidden = true;
    });
  }

  // -- table of contents highlight ----------------------------------------------------------------------------------
  const tocLinks = Array.from(document.querySelectorAll(".toc a"));
  const headings = tocLinks.map((a) => document.getElementById(a.getAttribute("href").slice(1))).filter(Boolean);
  if (headings.length && "IntersectionObserver" in window) {
    const observer = new IntersectionObserver((entries) => {
      for (const entry of entries) {
        if (!entry.isIntersecting) continue;
        tocLinks.forEach((a) => a.classList.toggle("active", a.getAttribute("href") === "#" + entry.target.id));
      }
    }, { rootMargin: "-60px 0px -70% 0px" });
    headings.forEach((h) => observer.observe(h));
  }
})();
