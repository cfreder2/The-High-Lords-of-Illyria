// A small Markdown reader for the manual (docs/manual/*.md), with the few
// extensions a printed manual wants. Plain Markdown: headings, paragraphs,
// lists, pipe tables, block quotes, rules, code, images, links, bold and
// italics, and raw HTML blocks. The extensions:
//   # Title {#id}            an anchor on any heading
//   ::: name args ... :::    a block (tip, lore, figure, unit, stats, cover,
//                            contents, index, break, cols); see manual.js
//   {{term}}                 bold, and listed in the index
//   [[Esc]]                  a key
//   [](#id)                  a cross reference: "§4.2, p. 37"
//   ![caption](src "small")  a figure with its caption (title: a size class)
// No dependencies; the output is HTML for the paginator to lay out.
(function () {
  "use strict";

  const esc = (s) => s.replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");
  const attr = (s) => esc(s).replace(/"/g, "&quot;");
  // Inline HTML the text may use as is.
  const allowedTag = /^<\/?(kbd|br|small|sup|sub|span|b|i|em|strong|abbr)\b[^>]*>/i;

  function inline(text) {
    let out = "";
    let i = 0;
    while (i < text.length) {
      const rest = text.slice(i);
      let m;
      if ((m = /^`([^`]+)`/.exec(rest))) {
        out += `<code>${esc(m[1])}</code>`;
      } else if ((m = /^!\[([^\]]*)\]\(([^)\s]+)(?:\s+"([^"]*)")?\)/.exec(rest))) {
        const base = /^(https?:|\/|data:)/.test(m[2]) ? "" : window.IllyriaMarkdown.base || "";
        out += `<img src="${attr(base + m[2])}" alt="${attr(m[1])}"${m[3] ? ` class="${attr(m[3])}"` : ""}>`;
      } else if ((m = /^\[\]\(#([\w-]+)\)/.exec(rest))) {
        out += `<a class="xref" data-ref="${attr(m[1])}" href="#${attr(m[1])}">§0.0, p. 00</a>`;
      } else if ((m = /^\[\[([^\]]+)\]\]/.exec(rest))) {
        out += `<kbd>${esc(m[1])}</kbd>`;
      } else if ((m = /^\[((?:[^\[\]]|\[[^\]]*\])+)\]\(([^)\s]+)\)/.exec(rest))) {
        const internal = m[2].startsWith("#");
        out += internal ? `<a class="jump" data-ref="${attr(m[2].slice(1))}" href="${attr(m[2])}">${inline(m[1])}</a>`
                        : `<a href="${attr(m[2])}" target="_blank" rel="noopener">${inline(m[1])}</a>`;
      } else if ((m = /^\{\{([^}]+)\}\}/.exec(rest))) {
        const [shown, key] = m[1].split("|");
        out += `<b class="term" data-term="${attr((key || shown).trim())}">${inline(shown)}</b>`;
      } else if ((m = /^\*\*([^*]+(?:\*[^*]+\*[^*]*)*)\*\*/.exec(rest)) || (m = /^__([^_]+)__/.exec(rest))) {
        out += `<strong>${inline(m[1])}</strong>`;
      } else if ((m = /^\*([^*\s][^*]*)\*/.exec(rest)) || (m = /^_([^_\s][^_]*)_(?![A-Za-z0-9])/.exec(rest))) {
        out += `<em>${inline(m[1])}</em>`;
      } else if (rest[0] === "<" && (m = allowedTag.exec(rest))) {
        out += m[0];
      } else if (rest[0] === "&" && (m = /^&(#\d+|#x[0-9a-f]+|[a-z]+);/i.exec(rest))) {
        out += m[0];
      } else {
        // Plain text up to the next character that may start something.
        m = /^[^`!\[*_{<&]+/.exec(rest);
        const t = m ? m[0] : rest[0];
        // Typography: an en dash for " -- " and between numbers (1-9).
        out += esc(t).replace(/ -- /g, " – ").replace(/(\d)-(?=\d)/g, "$1–");
        i += t.length;
        continue;
      }
      i += m[0].length;
    }
    return out;
  }

  function heading(level, text, ctx) {
    let id = "";
    const m = /\s*\{#([\w-]+)\}\s*$/.exec(text);
    if (m) {
      id = m[1];
      text = text.slice(0, m.index);
    }
    return ctx.heading(level, text.trim(), id, inline(text.trim()));
  }

  function table(lines) {
    const cells = (l) => l.trim().replace(/^\||\|$/g, "").split("|").map((c) => c.trim());
    const head = cells(lines[0]);
    const align = cells(lines[1]).map((c) => (/^:-+:$/.test(c) ? "center" : /-+:$/.test(c) ? "right" : ""));
    const td = (tag, c, k) => `<${tag}${align[k] ? ` style="text-align:${align[k]}"` : ""}>${inline(c)}</${tag}>`;
    let h = `<div class="table"><table><thead><tr>${head.map((c, k) => td("th", c, k)).join("")}</tr></thead><tbody>`;
    for (const l of lines.slice(2)) h += `<tr>${cells(l).map((c, k) => td("td", c, k)).join("")}</tr>`;
    return h + "</tbody></table></div>";
  }

  // Block structure. ctx supplies heading() and block() for the extensions.
  function blocks(src, ctx) {
    const lines = src.replace(/\r/g, "").split("\n");
    let out = "";
    let i = 0;
    const isBlank = (l) => /^\s*$/.test(l);
    while (i < lines.length) {
      const line = lines[i];
      if (isBlank(line)) { ++i; continue; }
      let m;
      if ((m = /^(#{1,4})\s+(.*)$/.exec(line))) {
        out += heading(m[1].length, m[2], ctx);
        ++i;
      } else if ((m = /^:::\s*([\w-]+)\s*(.*)$/.exec(line))) {
        // A block, to the matching ::: (blocks may nest).
        let depth = 1, j = i + 1;
        const body = [];
        for (; j < lines.length; ++j) {
          if (/^:::\s*[\w-]+/.test(lines[j])) ++depth;
          else if (/^:::\s*$/.test(lines[j]) && --depth === 0) break;
          body.push(lines[j]);
        }
        out += ctx.block(m[1], m[2].trim(), body.join("\n"), (s) => blocks(s, ctx));
        i = j + 1;
      } else if (/^```/.test(line)) {
        let j = i + 1;
        const body = [];
        for (; j < lines.length && !/^```/.test(lines[j]); ++j) body.push(lines[j]);
        out += `<pre>${esc(body.join("\n"))}</pre>`;
        i = j + 1;
      } else if (/^\s*\|/.test(line) && i + 1 < lines.length && /^\s*\|?\s*:?-+/.test(lines[i + 1])) {
        const t = [];
        while (i < lines.length && /^\s*\|/.test(lines[i])) t.push(lines[i++]);
        out += table(t);
      } else if (/^>/.test(line)) {
        const q = [];
        while (i < lines.length && /^>/.test(lines[i])) q.push(lines[i++].replace(/^>\s?/, ""));
        out += `<blockquote>${blocks(q.join("\n"), ctx)}</blockquote>`;
      } else if (/^(---|\*\*\*)\s*$/.test(line)) {
        out += `<hr>`;
        ++i;
      } else if (/^\s*([-*]|\d+\.)\s+/.test(line)) {
        const ordered = /^\s*\d+\./.test(line);
        const items = [];
        while (i < lines.length && !isBlank(lines[i])) {
          if (/^\s*([-*]|\d+\.)\s+/.test(lines[i]) && !/^\s{2,}/.test(lines[i])) items.push(lines[i].replace(/^\s*([-*]|\d+\.)\s+/, ""));
          else items[items.length - 1] += " " + lines[i].trim();
          ++i;
        }
        const tag = ordered ? "ol" : "ul";
        out += `<${tag}>${items.map((t) => `<li>${inline(t)}</li>`).join("")}</${tag}>`;
      } else if (/^\s*</.test(line) && !allowedTag.test(line.trim())) {
        const h = [];
        while (i < lines.length && !isBlank(lines[i])) h.push(lines[i++]);
        out += h.join("\n");
      } else {
        const p = [];
        while (i < lines.length && !isBlank(lines[i]) && !/^(#{1,4}\s|:::|```|>|\s*\|)/.test(lines[i]) &&
               !(p.length && /^\s*([-*]|\d+\.)\s+/.test(lines[i]))) p.push(lines[i++].trim());
        const text = p.join(" ");
        // A paragraph that is only an image is a figure.
        const fig = /^!\[([^\]]*)\]\(([^)\s]+)(?:\s+"([^"]*)")?\)$/.exec(text);
        if (fig) out += ctx.figure(fig[2], fig[1], fig[3] || "", inline(fig[1]));
        else out += `<p>${inline(text)}</p>`;
      }
    }
    return out;
  }

  window.IllyriaMarkdown = { blocks, inline, esc, attr };
})();
