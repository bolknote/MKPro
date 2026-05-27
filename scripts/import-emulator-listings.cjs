'use strict';

const fs = require('fs');
const path = require('path');

const BASE = 'https://sergeanvarov.github.io/russian/mk61';
const OUT = path.resolve(__dirname, '../games/anvarov');

const CATALOG = [
  { slug: 'game-100-pig', section: 'games', rel: '100/100.html' },
  { slug: 'treasure-hunter-2', section: 'games', rel: 'кладоискатель/Кладоискатель-2.html' },
  { slug: 'tic-tac-toe-4x4', section: 'games', rel: 'крестики-нолики 4x4/крестики-нолики 4x4.html' },
  { slug: 'coop-cafe', section: 'games', rel: 'кооперативное кафе/кооперативное кафе.html' },
  { slug: 'coop-cafe-2', section: 'games', rel: 'кооперативное кафе 2/кооперативное кафе 2.html' },
  { slug: 'labyrinth777', section: 'games', rel: 'лабиринт777/лабиринт777.html' },
  { slug: 'lunolot-1', section: 'games', rel: 'лунолёт-1/лунолёт-1.html' },
  { slug: 'sea-battle', section: 'games', rel: 'морской бой/морской бой.html' },
  { slug: 'necromancer', section: 'games', rel: 'некромант/некромант.html' },
  { slug: 'dangerous-loading', section: 'games', rel: 'опасная погрузка/опасная погрузка.html' },
  { slug: 'fox-hunt', section: 'games', rel: 'охота на лис/охота на лис.html' },
  { slug: 'fox-hunt-38', section: 'games', rel: 'охота на лис 38/охота на лис 38.html' },
  { slug: 'fox-hunt-100', section: 'games', rel: 'охота на лис 100/охота на лис 100.html' },
  { slug: 'raja-yoga', section: 'games', rel: 'раджа-йога/раджа-йога.html' },
  { slug: 'minesweeper-9x9', section: 'games', rel: 'сапёр 9x9/сапёр 9x9.html' },
  { slug: 'teleport', section: 'games', rel: 'телепорт/телепорт.html' },
  { slug: 'tom-and-jerry-3', section: 'games', rel: 'том и джерри 3/том и джерри 3.html' },
  { slug: 'chess-rook-mate', section: 'games', rel: 'шахматы/мат ладьёй/мат ладьёй.html' },
  { slug: 'queens-placement', section: 'other', rel: 'расстановка ферзей/расстановка ферзей.html' },
  { slug: 'e-94-digits', section: 'other', rel: '94 цифры числа e/94 цифры числа e.html' },
];

const { normalizeLordBssCommand } = require('../tests/emulator/lordbss.cjs');

function decodeEntities(text) {
  return String(text)
    .replace(/&#x([0-9a-f]+);/gi, (_, hex) => String.fromCodePoint(parseInt(hex, 16)))
    .replace(/&#([0-9]+);/g, (_, decimal) => String.fromCodePoint(parseInt(decimal, 10)))
    .replace(/&nbsp;/gi, ' ')
    .replace(/&thinsp;/gi, '')
    .replace(/&lt;/gi, '<')
    .replace(/&gt;/gi, '>')
    .replace(/&amp;/gi, '&')
    .replace(/&divide;/gi, '÷')
    .replace(/&times;/gi, '×')
    .replace(/&minus;/gi, '-')
    .replace(/&rarr;/gi, '→')
    .replace(/&uarr;/gi, '↑')
    .replace(/&and;/gi, '∧')
    .replace(/&or;/gi, '∨')
    .replace(/&ne;/gi, '≠')
    .replace(/&ge;/gi, '≥')
    .replace(/&le;/gi, '≤')
    .replace(/&pi;/gi, 'π')
    .replace(/<sup>[^<]*<\/sup>/gi, '')
    .replace(/<[^>]*>/g, '');
}

function cleanText(html) {
  return decodeEntities(html).replace(/\s+/g, ' ').trim();
}

function spanInnerText(html) {
  return html
    .replace(/10\s*<sup>\s*x\s*<\/sup>/gi, '10^x')
    .replace(/x\s*<sup>\s*2\s*<\/sup>/gi, 'x^2')
    .replace(/x\s*<sup>\s*y\s*<\/sup>/gi, 'x^y')
    .replace(/<sup>[^<]*<\/sup>/gi, '');
}

function cellCommand(cellHtml) {
  if (!cellHtml || /colspan/i.test(cellHtml)) return '';
  const parts = [];
  for (const match of cellHtml.matchAll(/<span[^>]*class="([^"]*)"[^>]*>([\s\S]*?)<\/span>/gi)) {
    parts.push({ cls: match[1], text: cleanText(spanInnerText(match[2])) });
  }
  if (!parts.length) return cleanText(cellHtml);

  let out = '';
  for (const part of parts) {
    const t = part.text;
    if (part.cls.includes('but_f') && t === 'F') {
      out += 'F';
      continue;
    }
    if (part.cls.includes('but_k') && t === 'К') {
      out += 'К';
      continue;
    }
    if (part.cls.includes('but_cx')) {
      out += t === 'Cx' || t === 'Сx' ? 'Cx' : t;
      continue;
    }
    if (part.cls.includes('but_b')) {
      out += t
        .replace(/x→П/gi, 'хП')
        .replace(/П→x/gi, 'Пх')
        .replace(/x<0/gi, 'x<0')
        .replace(/x=0/gi, 'x=0')
        .replace(/x≠0/gi, 'x≠0')
        .replace(/x>=0/gi, 'x>=0')
        .replace(/x≥0/gi, 'x≥0')
        .replace(/x!=0/gi, 'x!=0')
        .replace(/С\/П/g, 'С/П')
        .replace(/В\/О/g, 'В/О');
      continue;
    }
    if (part.cls.includes('op_f')) {
      out += t
        .replace(/10x/gi, '10^x')
        .replace(/L(\d)/gi, 'L$1')
        .replace(/x=0/gi, 'x=0')
        .replace(/x!=0/gi, 'x!=0')
        .replace(/x>=0/gi, 'x>=0')
        .replace(/x≥0/gi, 'x≥0')
        .replace(/x<0/gi, 'x<0')
        .replace(/x≠0/gi, 'x≠0')
        .replace(/sin/gi, 'sin')
        .replace(/cos/gi, 'cos')
        .replace(/tg/gi, 'tg')
        .replace(/π/g, 'π')
        .replace(/Вx/g, ' Вx');
      continue;
    }
    if (part.cls.includes('op_k')) {
      out += t
        .replace(/НОП/g, 'НОП')
        .replace(/∨/g, '∨')
        .replace(/∧/g, '∧')
        .replace(/⊕/g, '⊕')
        .replace(/\{x\}/g, '{x}')
        .replace(/\[x\]/g, '[x]')
        .replace(/max/gi, 'max');
      continue;
    }
    if (part.cls.includes('reg')) {
      out += t;
      continue;
    }
    if (part.cls.includes('but')) {
      out += t.replace(/↑/g, '↑');
      continue;
    }
    out += t;
  }

  return normalizeLordBssCommand(
    out
      .replace(/К∧/g, 'KΛ')
      .replace(/K∧/g, 'KΛ')
      .replace(/К&and;/g, 'KΛ')
      .replace(/K&and;/g, 'KΛ')
      .replace(/\s+/g, ''),
  );
}

function parseRowLabel(label) {
  const text = label.replace(/\|/g, '').trim().toUpperCase();
  if (text === '#') return null;
  if (/^A\d$/.test(text)) return 100 + parseInt(text.slice(1), 10);
  if (/^\d\d$/.test(text)) return parseInt(text, 10);
  if (/^\d0$/.test(text)) return parseInt(text, 10);
  throw new Error(`Bad row label: ${label}`);
}

function formatAddress(address) {
  if (address >= 100) return `A${address - 100}`;
  return String(address).padStart(2, '0');
}

function parseProgramTable(html) {
  const heading = html.match(/<h[23][^>]*>\s*Текст программы\s*<\/h[23]>/i);
  if (!heading) throw new Error('Program table heading not found');
  const slice = html.slice(heading.index);
  const tableMatch = slice.match(/<table[^>]*class="center"[^>]*>([\s\S]*?)<\/table>/i);
  if (!tableMatch) throw new Error('Program table not found');

  const rows = [...tableMatch[1].matchAll(/<tr\b[\s\S]*?<\/tr>/gi)];
  const lines = [];

  for (const row of rows) {
    const cells = [...row[0].matchAll(/<(th|td)\b[^>]*>([\s\S]*?)<\/\1>/gi)];
    if (cells.length < 2) continue;
    const rowLabel = cleanText(cells[0][2]).replace(/\u200B/g, '');
    const rowBase = parseRowLabel(rowLabel);
    if (rowBase == null) continue;

    for (let col = 0; col < 10; col++) {
      const cell = cells[col + 1];
      if (!cell) break;
      const command = cellCommand(cell[2]);
      if (!command) continue;
      lines.push(`${formatAddress(rowBase + col)}\t${command}`);
    }
  }

  if (!lines.length) throw new Error('No program steps parsed');
  return lines.join('\n') + '\n';
}

function extractTitle(html) {
  const h1 = html.match(/<h1>([\s\S]*?)<\/h1>/i);
  if (h1) return cleanText(h1[1]);
  const title = html.match(/<title>([\s\S]*?)<\/title>/i);
  return title ? cleanText(title[1]) : 'Untitled';
}

function extractAuthor(html) {
  const patterns = [
    /<(?:p|li|td)[^>]*>\s*Автор(?:ы)?[:\s]*([^<]{2,120})<\//i,
    /Автор(?:ы)?[:\s]*([A-ZА-ЯЁ][^<\n]{2,80})<\//i,
  ];
  for (const pattern of patterns) {
    const match = html.match(pattern);
    if (!match) continue;
    const author = cleanText(match[1]).replace(/\.$/, '');
    if (author.length > 80) continue;
    if (/историч|предполаг|программ/i.test(author)) continue;
    return author;
  }
  return '';
}

function extractDescription(html) {
  const headerEnd = html.indexOf('</header>');
  const programStart = html.search(/<h[23][^>]*>\s*Текст программы/i);
  const slice = html.slice(headerEnd > 0 ? headerEnd : 0, programStart > 0 ? programStart : html.length);
  const chunks = [];
  for (const match of slice.matchAll(/<(p|h2|li)\b[^>]*>([\s\S]*?)<\/\1>/gi)) {
    const text = cleanText(match[2]);
    if (text && text.length > 2) chunks.push(text);
  }
  return chunks.slice(0, 12);
}

function buildMarkdown(entry, title, author, sourceUrl, description) {
  const lines = [`# ${title}`, ''];
  if (author) lines.push(`- Author: ${author}`);
  lines.push(`- Source: [Serge Anvarov](${sourceUrl})`);
  lines.push(`- Program: [${entry.slug}.txt](./${entry.slug}.txt)`);
  lines.push('');
  lines.push('## Description');
  lines.push('');
  for (const paragraph of description) {
    lines.push(paragraph);
    lines.push('');
  }
  return lines.join('\n').trimEnd() + '\n';
}

async function fetchText(url) {
  const res = await fetch(url);
  if (!res.ok) throw new Error(`HTTP ${res.status} for ${url}`);
  return res.text();
}

async function main() {
  const manifestRows = [
    'program\tdescription\ttitle\tauthor\tsource_url',
    'demo.txt\tdemo.md\tTreasure Cave Demo\tSerge Anvarov\thttps://sergeanvarov.github.io/russian/mk61/uf/demo.html',
  ];

  for (const entry of CATALOG) {
    const url = `${BASE}/${entry.section}/${entry.rel.split('/').map(encodeURIComponent).join('/')}`;
    process.stdout.write(`Fetching ${entry.slug} ... `);
    const html = await fetchText(url);
    const title = extractTitle(html);
    const author = extractAuthor(html);
    const listing = parseProgramTable(html);
    const description = extractDescription(html);
    const md = buildMarkdown(entry, title, author, url, description);

    fs.writeFileSync(path.join(OUT, `${entry.slug}.txt`), listing, 'utf8');
    fs.writeFileSync(path.join(OUT, `${entry.slug}.md`), md, 'utf8');
    manifestRows.push(
      `${entry.slug}.txt\t${entry.slug}.md\t${title.replace(/\t/g, ' ')}\t${author.replace(/\t/g, ' ')}\t${url}`,
    );
    console.log(`ok (${listing.split('\n').filter(Boolean).length} steps)`);
  }

  fs.writeFileSync(path.join(OUT, 'manifest.tsv'), manifestRows.join('\n') + '\n', 'utf8');
  console.log(`Wrote ${CATALOG.length} programs to ${OUT}`);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
