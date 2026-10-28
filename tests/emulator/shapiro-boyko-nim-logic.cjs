#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');
const source = require('./fixtures/shapiro-boyko-nim-logic.json');
const root = path.resolve(__dirname, '../..');
const registerKeys = [...'0123456789', ',', '/-/', 'ВП', 'Cx', 'В↑'];
const manifest = fs.readFileSync(path.join(root, 'games/manifest.tsv'), 'utf8')
  .split('\n').slice(1).filter(Boolean).map(line => line.split('\t'));
const programs = new Map();
assert.equal(source.entries.length, 3);
assert.equal(source.source_sha256, require('./fixtures/shapiro-boyko-1989.json').source_sha256);
for (const entry of source.entries) {
  const listing = fs.readFileSync(path.join(root, 'games', `${entry.stem}.txt`), 'utf8');
  const card = fs.readFileSync(path.join(root, 'games', `${entry.stem}.md`), 'utf8');
  const parsed = parseProgramText(listing);
  assert.deepEqual(parsed.diagnostics, []);
  assert.equal(parsed.codes.length, entry.length);
  assert.equal(Buffer.from(parsed.codes).toString('hex').toUpperCase(), entry.hex);
  assert.equal(crypto.createHash('sha256').update(Buffer.from(parsed.codes)).digest('hex'), entry.sha256);
  const printed = [...Buffer.from(entry.printed_hex, 'hex')];
  for (const erratum of entry.errata) {
    assert.equal(printed[erratum.address], parseInt(erratum.printed, 16));
    printed[erratum.address] = parseInt(erratum.resolved, 16);
    assert.ok(card.includes(`**${erratum.address}**`));
  }
  assert.deepEqual(parsed.codes, printed, 'only the declared printed-code correction is allowed');
  const lines = listing.trimEnd().split('\n');
  assert.equal(lines.length, entry.length);
  lines.forEach((line, i) => assert.ok(line.startsWith(`${String(i).padStart(2, '0')}\t`)));
  const row = manifest.find(row => row[0] === `${entry.stem}.txt`);
  assert.ok(row);
  assert.deepEqual([row[1], row[2], row[3], row[6], row[7], row[8], row[9], row[10]],
    [`${entry.stem}.md`, entry.title, entry.author, 'game', 'logic', entry.tags,
      entry.collection, entry.series]);
  assert.ok(row[5].includes(source.source_url) && card.includes(source.source_url));
  assert.ok(!/\]\([^)]*(?:\/Users\/|output\/research\/)/.test(card));
  const preparation = card.split('## Подготовка\n')[1].split('\n## ')[0];
  const blocks = [...preparation.matchAll(/```text\n([\s\S]*?)\n```/g)];
  assert.equal(blocks.length, 1);
  programs.set(entry.id, { listing, card, setup: blocks[0][1] });
}

function numeric(text) {
  const [mantissa, exponent = '0'] = text.trim().replace(',', '.').split(/\s+/);
  return Number(mantissa) * 10 ** Number(exponent);
}
const reg = (c, r) => numeric(c.readRegister(r));
function settle(c) {
  assert.ok(c.runUntilStable({ maxFrames: 4000, stableFrames: 8 }).stopped,
    'the printed program must reach its next stop');
  c.runFrames(12);
  assert.ok(!c.displayText().includes('ЕГГ0Г'));
  return reg(c, 'X');
}
function resume(c) { c.press('С/П'); return settle(c); }
function input(c, n) {
  // The source Nim keeps its preceding move in the stack: Cx destroys that state.
  c.inputNumber(String(n)); return resume(c);
}
function keyboard(c, text) {
  for (const token of text.trim().split(/\s+/)) {
    if (/^\d+(?:[.,]\d+)?$/.test(token)) c.inputNumber(token);
    else if (/^хП[0-9a-e]$/i.test(token)) {
      c.press('П').press(registerKeys[parseInt(token.slice(2), 16)]);
    } else {
      c.press(token);
      if (token === 'С/П') settle(c);
    }
  }
  return reg(c, 'X');
}
function make(id, angle = 'rad') {
  const c = new MK61({ angleMode: angle });
  c.loadProgram(programs.get(id).listing);
  for (const r of [...'0123456789abcde', 'X', 'Y', 'Z', 'T', 'X1']) {
    c.setRegister(r, 173 + r.charCodeAt(0));
  }
  c.pressSequence(['F', '/-/']);
  return c;
}
function nimBegin(n, m, c = make('nim98')) {
  keyboard(c, programs.get('nim98').setup.replace('31 хПd', `${n} хПd`)
    .replace('12 хП6 хП3', `${m + 1} хП6 хП3`));
  return c;
}
function logicBegin(id, seed, c = make(id)) {
  keyboard(c, programs.get(id).setup.replace('9,75', String(seed)));
  assert.equal(reg(c, 'X'), 0);
  assert.equal(reg(c, '5'), 0);
  return c;
}

function testNim() {
  const legal = (n, m, last) => Array.from({ length: Math.min(n, m) }, (_, i) => i + 1)
    .filter(q => q !== last);
  const memo = new Map();
  function winning(n, m, last) {
    const key = `${n},${m},${last}`;
    if (!memo.has(key)) memo.set(key, legal(n, m, last).some(q => !winning(n - q, m, q)));
    return memo.get(key);
  }
  let games = 0, moves = 0, firstHuman = 0;
  for (const angle of ['rad', 'deg', 'grad']) {
    const c = nimBegin(31, 11, make('nim98', angle));
    assert.equal(reg(c, 'X'), 7);
    assert.deepEqual([input(c, 6), input(c, 2), input(c, 4)], [3, 1, 8]);
    nimBegin(31, 11, c); assert.equal(reg(c, 'X'), 7, 'full keyboard restart');
  }
  // Independent finite game graph, not the book's remainder/parity algorithm.
  for (const [n0, m] of [[31, 11], [24, 11], [6, 2], [17, 4], [43, 6], [31, 15], [15, 3]]) {
    for (const policy of ['min', 'max', 'optimal']) {
      const c = nimBegin(n0, m);
      let n = n0, last = 0, p = reg(c, 'X'), round = 0;
      if (p === 0) { assert.ok(!winning(n, m, 0)); firstHuman++; }
      while (true) {
        assert.equal(reg(c, 'd'), n, 'source N is before the displayed computer move');
        if (p !== 0 || round > 0) {
          assert.ok(legal(n, m, last).includes(p), `${n0}/${m}, ${policy}: legal PMK move`);
          if (winning(n, m, last)) assert.ok(!winning(n - p, m, p), 'preserves a winning move');
          n -= p; last = p; moves++;
        }
        const options = legal(n, m, last);
        if (!options.length) break;
        const q = policy === 'min' ? options[0] : policy === 'max' ? options.at(-1) :
          options.find(q => !winning(n - q, m, q)) ?? options[0];
        n -= q; last = q; moves++;
        assert.ok(legal(n, m, last).length, 'PMK wins these source-compatible game cases');
        p = input(c, q); round++;
        assert.ok(round < 100);
      }
      games++;
      assert.equal(reg(c, '7'), 68); assert.equal(reg(c, '8'), 83); assert.equal(reg(c, '9'), 91);
      nimBegin(n0, m, c); assert.equal(reg(c, 'X'), reg(nimBegin(n0, m), 'X'));
    }
  }
  assert.ok(firstHuman > 0);
  // Preserve and label actual author-algorithm failures; do not silently replace it.
  assert.equal(reg(nimBegin(6, 5), 'X'), 6, 'printed algorithm exceeds M');
  assert.ok(!legal(6, 5, 0).includes(6));
  const p = reg(nimBegin(31, 5), 'X'); assert.equal(p, 3);
  assert.ok(winning(31, 5, 0) && winning(31 - p, 5, p), 'missed winning position');
  assert.ok(programs.get('nim98').card.includes('N=6, M=5'));
  assert.ok(programs.get('nim98').card.includes('N=31, M=5'));
  console.log(`Nim OK: printed game in 3 modes, ${games} complete games, ${moves} legal moves, both first players, restarts and 2 source counterexamples`);
}

const flags = (secret, guess) => [...guess].map((d, i) =>
  d === secret[i] ? 2 : secret.includes(d) ? 1 : 0);
const score = (secret, guess) => {
  const f = flags(secret, guess);
  return 10 * f.filter(x => x === 2).length + f.filter(x => x === 1).length;
};
const digits = n => String(n).padStart(5, '0');
function reveal(c) {
  const text = [...'9abcd'].map(r => reg(c, r)).join('');
  assert.match(text, /^[0-7]{5}$/); return text;
}
function setSecret(c, secret) {
  // Controlled scoring edge cases, independent of which secrets the generator visits.
  [...secret].forEach((d, i) => c.setRegister('9abcd'[i], Number(d)));
}
function guess71(c, guess) {
  assert.match(guess, /^[0-7]{5}$/);
  for (const d of guess) {
    const prompt = input(c, Number(d));
    assert.ok(Math.abs(prompt - Math.PI) < 1e-6, 'each of the five digit inputs stops at pi');
  }
  return resume(c); // Required sixth continuation, with no new number.
}

function testPositional() {
  const id = 'partner71', c = logicBegin(id, 9.75);
  assert.equal(reveal(c), '56055');
  for (const [i, [g, expected]] of [
    ['51046', 20201], ['56032', 22200], ['56075', 22202], ['56055', 22222],
  ].entries()) {
    assert.equal(guess71(c, g), expected);
    assert.equal(reg(c, '5'), i + 1);
  }
  let games = 0, guesses = 0;
  for (const seed of [0, 0.125, 9.75]) {
    logicBegin(id, seed, c); const secret = reveal(c), learned = Array(5).fill(null);
    // A blind player learns positions from uniform-color probes, using only feedback.
    let count = 0, solved = false;
    for (let d = 0; d < 8; d++) {
      const g = String(d).repeat(5), response = guess71(c, g); count++; guesses++;
      assert.equal(response, Number(flags(secret, g).join('')));
      assert.equal(reg(c, '5'), count);
      const f = digits(response);
      [...f].forEach((x, i) => { if (x === '2') learned[i] = String(d); });
      if (response === 22222) { solved = true; break; }
    }
    if (!solved) {
      assert.ok(learned.every(x => x !== null));
      assert.equal(guess71(c, learned.join('')), 22222); count++; guesses++;
    }
    assert.equal(reg(c, '5'), count); games++;
  }
  logicBegin(id, 9.75, c);
  assert.equal(guess71(c, '56065'), 22212, 'duplicate six remains present');
  assert.equal(guess71(c, '66666'), 12111);
  logicBegin(id, 0, c); setSecret(c, '00000');
  assert.equal(guess71(c, '00001'), 22220, 'absent digit and leading zeros');
  assert.equal(guess71(c, '00000'), 22222); assert.equal(reg(c, '5'), 2);
  console.log(`Positional Logic OK: printed game, ${games} blind complete games/${guesses} guesses, duplicates, leading zeros, 5 pi prompts, counts and restarts`);
}

function testSummed() {
  const id = 'partner88', c = logicBegin(id, 9.75);
  assert.equal(reveal(c), '56055');
  for (const [i, [g, expected]] of [
    [51046, 21], [56032, 30], [56045, 40], [56065, 41], [56055, 50],
  ].entries()) {
    assert.equal(input(c, g), expected); assert.equal(reg(c, '5'), i + 1);
  }
  const universe = Array.from({ length: 8 ** 5 }, (_, n) => n.toString(8).padStart(5, '0'));
  let games = 0, guesses = 0;
  for (const seed of [0, 0.125, 9.75]) {
    logicBegin(id, seed, c); const secret = reveal(c);
    let candidates = universe, count = 0;
    while (true) {
      assert.ok(candidates.length);
      const g = candidates[0], response = input(c, g); count++; guesses++;
      assert.equal(response, score(secret, g)); assert.equal(reg(c, '5'), count);
      if (response === 50) break;
      // Solver uses its own guesses and returned scores, never the stored secret.
      candidates = candidates.filter(s => score(s, g) === response);
      assert.ok(count < 32, 'blind source-rule solver reaches a complete game');
    }
    games++;
  }
  logicBegin(id, 9.75, c); assert.equal(input(c, 66666), 14);
  assert.equal(input(c, 56065), 41, 'source does not consume matched digit copies');
  logicBegin(id, 0, c); setSecret(c, '00000');
  assert.equal(input(c, 1), 40, '1 is padded to 00001');
  assert.equal(input(c, 0), 50); assert.equal(reg(c, '5'), 2);
  console.log(`Summed Logic OK: printed game, ${games} blind complete games/${guesses} guesses, source multiplicity rules, leading zeros, counts and restarts`);
}

const group = process.argv[2];
if (!group || group === 'nim') testNim();
if (!group || group === 'positional') testPositional();
if (!group || group === 'summed') testSummed();
