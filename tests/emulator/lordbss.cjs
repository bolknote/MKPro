'use strict';

const fs = require('fs');
const path = require('path');

function parseLordBssGame(filePath) {
  const source = fs.readFileSync(filePath, 'utf8');
  if (/\.(txt|m61)$/i.test(filePath)) return parseTextListing(filePath, source);

  const html = source;
  const title = extractTitle(html);
  const table = html.match(/<table[^>]*rules=groups[\s\S]*?<\/table>/i);
  if (!table) throw new Error(`Program table was not found in ${filePath}`);

  const entries = [];
  const rows = table[0].match(/<tr\b[\s\S]*?<\/tr>/gi) || [];
  for (const row of rows) {
    if (/<th\b/i.test(row)) continue;
    const cells = [...row.matchAll(/<t[dh]\b[^>]*>([\s\S]*?)<\/t[dh]>/gi)].map((match) => cleanCell(match[1]));
    if (cells.length < 3) continue;
    const rowBase = parseRowAddress(cells[0]);
    for (let column = 0; column < 10; column++) {
      const command = normalizeLordBssCommand(cells[column + 2] || '');
      if (!command) continue;
      entries.push(`${formatAddress(rowBase + column)}.${command}`);
    }
  }

  return {
    file: filePath,
    title,
    entries,
    programText: entries.join(' '),
  };
}

function parseMany(rootDir, files) {
  return files.map((file) => parseLordBssGame(path.join(rootDir, file)));
}

function parseTextListing(filePath, source) {
  const entries = [];
  for (const line of source.split(/\r?\n/)) {
    const match = line.match(/^\s*([0-9]{2}|A[0-4])\s+(.+?)\s*$/i);
    if (!match) continue;
    const command = normalizeLordBssCommand(match[2]);
    if (command) entries.push(`${match[1].toUpperCase()}.${command}`);
  }

  return {
    file: filePath,
    title: titleFromSidecar(filePath),
    entries,
    programText: entries.join(' '),
  };
}

function titleFromSidecar(filePath) {
  const markdownPath = filePath.replace(/\.[^.]+$/, '.md');
  if (!fs.existsSync(markdownPath)) return path.basename(filePath);
  const firstLine = fs.readFileSync(markdownPath, 'utf8').split(/\r?\n/, 1)[0] || '';
  return firstLine.replace(/^#\s*/, '').trim() || path.basename(filePath);
}

function extractTitle(html) {
  const header = html.match(/<p align=center><b><font face="Arial"><i>([\s\S]*?)<\/i>/i);
  if (header) return cleanCell(header[1]);
  const title = html.match(/<title>([\s\S]*?)<\/title>/i);
  return title ? cleanCell(title[1]) : '';
}

function cleanCell(cellHtml) {
  return decodeEntities(cellHtml)
    .replace(/<br\s*\/?>/gi, ' ')
    .replace(/<[^>]*>/g, '')
    .replace(/\u00A0/g, ' ')
    .replace(/\s+/g, ' ')
    .trim();
}

function normalizeLordBssCommand(command) {
  let text = command.trim();
  if (!text) return '';

  text = text
    .replace(/\s+/g, '')
    .replace(/[−–—]/g, '-')
    .replace(/&#8211;/g, '-')
    .replace(/<->/g, '↔')
    .replace(/Fx=о/gi, 'Fx=0')
    .replace(/Fx=o/gi, 'Fx=0')
    .replace(/Fx<о/gi, 'Fx<0')
    .replace(/Fx<o/gi, 'Fx<0')
    .replace(/Fx≠о/gi, 'Fx≠0')
    .replace(/Fx≠o/gi, 'Fx≠0')
    .replace(/Fx>=о/gi, 'Fx>=0')
    .replace(/Fx>=o/gi, 'Fx>=0')
    .replace(/Fx≥о/gi, 'Fx≥0')
    .replace(/Fx≥o/gi, 'Fx≥0')
    .replace(/Kx=о/gi, 'Kx=0')
    .replace(/Кx=о/gi, 'Кx=0')
    .replace(/Kx=o/gi, 'Kx=0')
    .replace(/Кx=o/gi, 'Кx=0')
    .replace(/Kx≠о/gi, 'Kx≠0')
    .replace(/Кx≠о/gi, 'Кx≠0')
    .replace(/Kx≠o/gi, 'Kx≠0')
    .replace(/Кx≠o/gi, 'Кx≠0')
    .replace(/Kx>=о/gi, 'Kx>=0')
    .replace(/Кx>=о/gi, 'Кx>=0')
    .replace(/Kx>=o/gi, 'Kx>=0')
    .replace(/Кx>=o/gi, 'Кx>=0')
    .replace(/Kx≥о/gi, 'Kx≥0')
    .replace(/Кx≥о/gi, 'Кx≥0')
    .replace(/Kx≥o/gi, 'Kx≥0')
    .replace(/Кx≥o/gi, 'Кx≥0')
    .replace(/Kx<о/gi, 'Kx<0')
    .replace(/Кx<о/gi, 'Кx<0')
    .replace(/Kx<o/gi, 'Kx<0')
    .replace(/Кx<o/gi, 'Кx<0')
    .replace(/Кноп/gi, 'КНОП')
    .replace(/Кинв/gi, 'КИНВ')
    .replace(/Кзн/gi, 'КЗН')
    .replace(/Ксч/gi, 'КСЧ')
    .replace(/^Сх$/i, 'Сx')
    .replace(/^Cx$/i, 'Cx');

  return text;
}

function parseRowAddress(label) {
  const text = String(label).trim().toUpperCase();
  if (/^[A.-]0$/.test(text)) return 100;
  if (/^\d0$/.test(text)) return parseInt(text, 10);
  throw new Error(`Unsupported Lord_BSS row address: ${label}`);
}

function formatAddress(address) {
  if (address >= 100) return `A${address - 100}`;
  return String(address).padStart(2, '0');
}

function decodeEntities(text) {
  return String(text)
    .replace(/&#x([0-9a-f]+);/gi, (_, hex) => String.fromCodePoint(parseInt(hex, 16)))
    .replace(/&#([0-9]+);/g, (_, decimal) => String.fromCodePoint(parseInt(decimal, 10)))
    .replace(/&nbsp;/gi, ' ')
    .replace(/&thinsp;/gi, ' ')
    .replace(/&lt;/gi, '<')
    .replace(/&gt;/gi, '>')
    .replace(/&amp;/gi, '&')
    .replace(/&divide;/gi, '÷');
}

module.exports = {
  parseLordBssGame,
  parseMany,
  normalizeLordBssCommand,
};
