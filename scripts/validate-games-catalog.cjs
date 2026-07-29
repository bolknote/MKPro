#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');

const repo = path.resolve(__dirname, '..');
const gamesDir = path.join(repo, 'games');
const manifestPath = path.join(gamesDir, 'manifest.tsv');
const columns = [
  'program',
  'description',
  'title',
  'author',
  'real_name',
  'source_url',
  'kind',
  'section',
  'tags',
  'collection',
  'series',
];
const sections = new Set([
  'action',
  'adventure',
  'arcade',
  'educational',
  'gambling',
  'logic',
  'simulation',
  'sports',
  'strategy',
  'utilities',
]);
const kinds = new Set(['game', 'trainer', 'experiment', 'utility']);
const allowedTags = new Set([
  'aviation',
  'book',
  'classic',
  'economy',
  'fantasy',
  'horror',
  'magazine',
  'management',
  'manuscript',
  'military',
  'modernized',
  'naval',
  'original',
  'racing',
  'space',
  'two-player',
  'variant',
  'vs-calculator',
  'western',
]);
const identifierPattern = /^[a-z0-9]+(?:-[a-z0-9]+)*$/;
const errors = [];

function report(message) {
  errors.push(message);
}

function parseManifest() {
  const lines = fs.readFileSync(manifestPath, 'utf8').split(/\r?\n/);
  while (lines.length && lines.at(-1) === '') lines.pop();
  const actualColumns = (lines.shift() || '').split('\t');
  if (actualColumns.join('\t') !== columns.join('\t')) {
    throw new Error(
      `Unexpected manifest header:\n  expected: ${columns.join('\t')}\n  actual:   ${actualColumns.join('\t')}`,
    );
  }

  return lines.map((line, rowIndex) => {
    const cells = line.split('\t');
    if (cells.length !== columns.length) {
      report(`manifest row ${rowIndex + 2} has ${cells.length} columns, expected ${columns.length}`);
    }
    return Object.fromEntries(columns.map((column, index) => [column, cells[index] || '']));
  });
}

function validateRelativePath(relative, extension, rowNumber, field) {
  const label = `manifest row ${rowNumber} ${field}`;
  if (!relative || relative.includes('\\') || path.posix.isAbsolute(relative) ||
      path.posix.normalize(relative) !== relative) {
    report(`${label} is not a normalized relative POSIX path: ${JSON.stringify(relative)}`);
    return null;
  }

  const parts = relative.split('/');
  if (parts.length !== 2 || !sections.has(parts[0])) {
    report(`${label} must have the form <section>/<file>: ${relative}`);
  }
  if (path.posix.extname(relative) !== extension) {
    report(`${label} must end in ${extension}: ${relative}`);
  }

  const absolute = path.join(gamesDir, ...parts);
  if (!fs.existsSync(absolute)) report(`${label} does not exist: ${relative}`);
  return absolute;
}

function localMarkdownTargets(markdownPath) {
  const markdown = fs.readFileSync(markdownPath, 'utf8');
  const targets = new Set();
  for (const match of markdown.matchAll(/!?\[[^\]]*\]\(([^)]+)\)/g)) {
    const rawTarget = match[1].trim();
    if (!rawTarget || /^(?:[a-z][a-z0-9+.-]*:|#|\/)/i.test(rawTarget)) continue;
    const pathPart = rawTarget.replace(/[?#].*$/, '');
    if (!pathPart || /\s/.test(pathPart)) continue;
    const absolute = path.resolve(path.dirname(markdownPath), pathPart);
    targets.add(absolute);
    if (!fs.existsSync(absolute)) {
      report(
        `${path.relative(repo, markdownPath)} has a broken local link: ${rawTarget}`,
      );
    }
  }
  return targets;
}

function catalogFiles() {
  const files = [];
  for (const entry of fs.readdirSync(gamesDir, { withFileTypes: true })) {
    if (entry.isDirectory()) {
      if (!sections.has(entry.name)) report(`unexpected directory under games/: ${entry.name}`);
      const directory = path.join(gamesDir, entry.name);
      for (const child of fs.readdirSync(directory, { withFileTypes: true })) {
        const relative = `${entry.name}/${child.name}`;
        if (child.isDirectory()) {
          report(`nested directory is not supported in the catalog: ${relative}`);
        } else if (child.isFile() && /\.(?:md|txt)$/.test(child.name)) {
          files.push(relative);
        }
      }
      continue;
    }
    if (entry.isFile() && /\.(?:md|txt)$/.test(entry.name) && entry.name !== 'README.md') {
      report(`catalog file remains directly under games/: ${entry.name}`);
      files.push(entry.name);
    }
  }
  return files;
}

function validate() {
  const rows = parseManifest();
  const indexed = new Map();
  const programPaths = new Set();
  const descriptionPaths = new Set();
  const counts = Object.fromEntries([...sections].sort().map((section) => [section, 0]));

  rows.forEach((row, index) => {
    const rowNumber = index + 2;
    const programPath = validateRelativePath(row.program, '.txt', rowNumber, 'program');
    const descriptionPath = validateRelativePath(row.description, '.md', rowNumber, 'description');

    for (const [field, value, seen] of [
      ['program', row.program, programPaths],
      ['description', row.description, descriptionPaths],
    ]) {
      if (seen.has(value)) report(`duplicate ${field} path in manifest: ${value}`);
      seen.add(value);
      indexed.set(value, (indexed.get(value) || 0) + 1);
    }

    if (!row.title) report(`manifest row ${rowNumber} has an empty title`);
    if (!kinds.has(row.kind)) report(`manifest row ${rowNumber} has invalid kind: ${row.kind}`);
    if (!sections.has(row.section)) {
      report(`manifest row ${rowNumber} has invalid section: ${row.section}`);
    } else {
      counts[row.section] += 1;
      if (!row.program.startsWith(`${row.section}/`) ||
          !row.description.startsWith(`${row.section}/`)) {
        report(`manifest row ${rowNumber} paths disagree with section ${row.section}`);
      }
    }

    const programStem = path.posix.basename(row.program, '.txt');
    const descriptionStem = path.posix.basename(row.description, '.md');
    if (programStem !== descriptionStem) {
      report(`manifest row ${rowNumber} pairs different slugs: ${row.program}, ${row.description}`);
    }

    const tags = row.tags ? row.tags.split(';') : [];
    if (new Set(tags).size !== tags.length) {
      report(`manifest row ${rowNumber} has duplicate tags: ${row.tags}`);
    }
    if (tags.join(';') !== [...tags].sort().join(';')) {
      report(`manifest row ${rowNumber} tags are not sorted: ${row.tags}`);
    }
    for (const tag of tags) {
      if (!identifierPattern.test(tag)) {
        report(`manifest row ${rowNumber} has invalid tag identifier: ${tag}`);
      } else if (!allowedTags.has(tag)) {
        report(`manifest row ${rowNumber} has undocumented tag: ${tag}`);
      }
    }
    for (const field of ['collection', 'series']) {
      if (row[field] && !identifierPattern.test(row[field])) {
        report(`manifest row ${rowNumber} has invalid ${field} identifier: ${row[field]}`);
      }
    }

    if (descriptionPath && fs.existsSync(descriptionPath)) {
      const targets = localMarkdownTargets(descriptionPath);
      if (programPath && fs.existsSync(programPath) && !targets.has(programPath)) {
        report(`${row.description} does not link to its program ${row.program}`);
      }
    }
  });

  const files = catalogFiles();
  for (const relative of files) {
    const occurrences = indexed.get(relative) || 0;
    if (occurrences !== 1) {
      report(`${relative} is represented ${occurrences} times in the manifest`);
    }
    const extension = path.posix.extname(relative);
    const companion = relative.slice(0, -extension.length) + (extension === '.txt' ? '.md' : '.txt');
    if (!files.includes(companion)) report(`${relative} has no companion ${companion}`);
  }
  for (const relative of indexed.keys()) {
    if (!files.includes(relative)) report(`manifest path is outside the catalog inventory: ${relative}`);
  }

  if (errors.length) {
    console.error(`Catalog validation failed with ${errors.length} error(s):`);
    for (const error of errors) console.error(`- ${error}`);
    process.exitCode = 1;
    return;
  }

  console.log(`Catalog OK: ${rows.length} entries, ${files.length} files`);
  console.log(
    Object.entries(counts)
      .map(([section, count]) => `${section}=${count}`)
      .join(', '),
  );
}

try {
  validate();
} catch (error) {
  console.error(error.stack || error);
  process.exitCode = 1;
}
