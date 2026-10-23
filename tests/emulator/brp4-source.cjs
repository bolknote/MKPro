#!/usr/bin/env node
'use strict';

// Verify the extracted fixtures and catalog against the independently supplied
// complete ROM. The ROM is downloaded separately; no network is used here.
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { parseProgramText } = require('./mk61.cjs');
const source = require('./fixtures/brp4-games.json');
const root = path.resolve(__dirname, '../..');
const filename = process.argv[2];
assert.ok(filename && process.argv.length === 3,
  'Usage: node tests/emulator/brp4-source.cjs /path/to/BRP4-2.BIN');
const rom = fs.readFileSync(filename);
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
assert.equal(rom.length, 2048);
assert.equal(sha256(rom), source.rom_sha256, 'the ROM must match the pinned source');
assert.match(source.source_commit, /^[a-f0-9]{40}$/);
assert.ok(source.source_url.includes(`/blob/${source.source_commit}/`));
const manifest = new Map(fs.readFileSync(path.join(root, 'games/manifest.tsv'), 'utf8')
  .trimEnd().split('\n').slice(1).map(line => {
    const row = line.split('\t'); return [row[0], row];
  }));
const all = [...source.entries, ...source.duplicates];
assert.equal(source.entries.length, 15);
assert.equal(source.duplicates.length, 5);
assert.deepEqual(source.entries.filter(e => e.patches.length).map(e => e.number), [48]);
assert.deepEqual(all.map(e => e.number).sort((a,b) => a-b),
  Array.from({length: 21}, (_,i) => 31+i).filter(n => n !== 49));
for (const entry of all) {
  assert.match(entry.rom_address, /^1\d{6}$/);
  const address = Number(entry.rom_address);
  const length = address % 100;
  const start = Math.floor(Math.floor(address / 100) % 10000 / 2);
  assert.equal(length, entry.original_length);
  const extracted = Array.from({length}, (_,i) => {
    const logical = start + i;
    const physical = 7 * Math.floor(logical / 7) + (logical + 6) % 7;
    assert.ok(physical < rom.length);
    return rom[physical];
  });
  assert.equal(Buffer.from(extracted).toString('hex').toUpperCase(), entry.original_hex,
    `program ${entry.number}: fixture bytes must come from the declared ROM address`);
  if (entry.original_sha256) assert.equal(sha256(Buffer.from(extracted)), entry.original_sha256);
  const listing = fs.readFileSync(path.join(root, 'games', `${entry.stem}.txt`), 'utf8');
  const parsed = parseProgramText(listing);
  assert.deepEqual(parsed.diagnostics, []);
  const row = manifest.get(`${entry.stem}.txt`);
  assert.ok(row, `program ${entry.number}: indexed listing`);
  assert.ok(row[5].includes(source.source_url));
  const card = fs.readFileSync(path.join(root, 'games', `${entry.stem}.md`), 'utf8');
  assert.ok(card.includes(source.source_url));
  if (entry.patches) {
    const expected = [...extracted];
    const patchedAddresses = new Set();
    for (const patch of entry.patches) {
      assert.ok(Number.isInteger(patch.address) && patch.address >= 0 && patch.address < entry.length);
      assert.ok(!patchedAddresses.has(patch.address), 'each changed address is declared once');
      patchedAddresses.add(patch.address);
      const before = expected[patch.address];
      assert.equal(patch.before, before === undefined ? null : before.toString(16).padStart(2, '0').toUpperCase());
      assert.match(patch.after, /^[A-F0-9]{2}$/);
      assert.ok(patch.reason);
      expected[patch.address] = parseInt(patch.after, 16);
    }
    assert.ok(entry.length <= 105);
    assert.equal(expected.length, entry.length);
    assert.deepEqual(parsed.codes, expected, `program ${entry.number}: only declared changes are allowed`);
    assert.deepEqual([row[1], row[2], row[6], row[7], row[8], row[9], row[10]],
      [`${entry.stem}.md`, entry.title, entry.kind, entry.stem.split('/')[0], entry.tags, 'brp4', entry.series]);
  } else {
    assert.deepEqual(parsed.codes, extracted.slice(0, parsed.codes.length),
      `program ${entry.number}: existing catalog entry must really be a duplicate`);
    assert.ok(extracted.slice(parsed.codes.length).every(byte => byte === 0),
      'only trailing zero padding may be omitted from duplicate listings');
    assert.ok(parsed.codes.length <= extracted.length);
  }
}
console.log('BRP-4 source OK: complete ROM hash, all 20 addresses, 14 exact listings, declared torpedo repair and five duplicates');
