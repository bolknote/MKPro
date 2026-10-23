#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');

const base = path.resolve(__dirname,
  '../../games/logic/razdaem-karty-i-chisla-csakany-1981');
const listing = fs.readFileSync(`${base}.txt`, 'utf8');
const card = fs.readFileSync(`${base}.md`, 'utf8');
const parsed = parseProgramText(listing);
assert.deepEqual(parsed.diagnostics, []);
assert.equal(parsed.codes.length, 105);
// The parser ignores addresses beyond the machine's memory limit.
const cells = listing.trim().split(/\n/);
assert.equal(cells.length, 105);
cells.forEach((line, i) => assert.ok(line.startsWith(
  `${i < 100 ? String(i).padStart(2, '0') : `A${i - 100}`}\t`)));

const preparation = card.split('## Подготовка\n')[1].split('## Остановки программы\n')[0];
const setup = [...preparation.matchAll(/```text\n([\s\S]*?)\n```/g)].map(match => match[1]);
assert.equal(setup.length, 2, 'the card must document preparation and launch');
const registerKeys = [...'0123456789', ',', '/-/', 'ВП', 'Cx', 'В↑'];

function keyboard(calc, text) {
  for (const token of text.trim().split(/\s+/)) {
    if (/^-?\d+(?:[.,]\d+)?(?:e-?\d+)?$/i.test(token)) {
      // Enter the sign last; a leading sign key can act on the previous X.
      calc.inputNumber(token.replace(/^-/, ''));
      if (token.startsWith('-')) calc.press('/-/');
    } else if (/^хП[0-9a-e]$/i.test(token)) {
      calc.press('П').press(registerKeys[parseInt(token.slice(2), 16)]);
    } else calc.press(token);
  }
}
function number(text) {
  const [mantissa, exponent = '0'] = text.trim().replace(',', '.').split(/\s+/);
  return Number(mantissa) * 10 ** Number(exponent);
}
const register = (calc, name) => number(calc.readRegister(name));
const values = (calc, names) => names.map(name => register(calc, name));
function settle(calc) {
  assert.ok(calc.runUntilStable({ maxFrames: 10000, stableFrames: 8 }).stopped,
    'the program must reach its next documented stop');
  calc.runFrames(12);
  const result = number(calc.displayText());
  assert.ok(Number.isFinite(result), `unexpected display: ${calc.displayText()}`);
  return result;
}
function next(calc, expected, label, noisy = false) {
  if (noisy) calc.inputNumber('9876543', { clear: true });
  calc.press('С/П');
  assert.equal(settle(calc), expected, label);
}

// Build a complete permutation with a Set before playing. Unlike the ROM,
// this model has no floating point bit masks, indirect selectors, calculator
// loop counters or fractional tests for divisibility.
function shuffledDeck(seed, count = 40) {
  let state = seed;
  const seen = new Set(), ids = [], cards = [], states = [];
  while (ids.length < count) {
    state = (981 * state + 1) % 100000;
    const id = Math.floor(state / 2500);
    if (seen.has(id)) continue;
    seen.add(id);
    ids.push(id);
    cards.push(1 + Math.floor(id / 4));
    states.push(state);
  }
  return { ids, cards, states };
}
function masks(ids) {
  const result = [0, 0];
  for (const id of ids) result[Math.floor(id / 20)] += 2 ** (id % 20);
  return result;
}
function model(seed) {
  const deck = shuffledDeck(seed);
  const attempts = [], reports = [];
  let cursor = 0, triple = null, product = 0, removed = 0;
  const take = count => deck.cards.slice(cursor, cursor += count);
  function report(value, phase, pair) {
    reports.push({ value, phase, product, removed, consumed: cursor,
      masks: masks(deck.ids.slice(0, cursor)), state: deck.states[cursor - 1],
      groups: phase === 'end' && value === 0 ? 0 : Math.max(1, 7 - removed), pairs: 9 - pair });
  }
  for (let pair = 0; pair < 9; pair++) {
    if (triple === null) {
      triple = take(3);
      product = triple.reduce((a, b) => a * b, 1);
      report(-product, 'product', pair);
    }
    const divisors = take(2);
    report(100 * divisors[0] + divisors[1], 'pair', pair);
    const accepted = divisors.every(divisor => product % divisor === 0);
    if (accepted) removed++;
    attempts.push({ triple: [...triple], product, divisors, accepted, removed });
    if (removed === 7) break;
    if (accepted) triple = null;
  }
  const result = removed === 7 ? 0 : 1;
  report(result, 'end', result === 0 ? attempts.length - 1 : 8);
  return { deck, attempts, reports, result, removed, consumed: cursor };
}

// A complete generator cycle reaches every state and gives each candidate
// card the same number of proposals. Rejection still makes the shuffle a
// finite deterministic approximation, not a uniform choice among 40! decks.
let state = 0;
const visited = new Set(), proposals = Array(40).fill(0);
do {
  state = (981 * state + 1) % 100000;
  assert.ok(!visited.has(state));
  visited.add(state);
  proposals[Math.floor(state / 2500)]++;
} while (state !== 0);
assert.equal(visited.size, 100000);
assert.deepEqual(proposals, Array(40).fill(2500));

// Catch the old modulo-40 shuffle's excessive same-rank pairs. Across every
// seed, the first pair should be close to a fair deck's 3/39 baseline, rather
// than the old 12.5%. This is a deterministic regression check, not a claim of
// independence or a test of win rates conditioned on earlier successful pairs.
let sameRank = 0;
for (let seed = 0; seed < 100000; seed++) {
  const deal = shuffledDeck(seed, 5);
  if (deal.cards[3] === deal.cards[4]) sameRank++;
}
assert.ok(Math.abs(sameRank / 100000 - 3 / 39) < 0.01,
  `same-rank first pairs: ${sameRank}/100000`);
for (const seed of [0, 2, 10, 161, 878, 99999]) {
  const deal = shuffledDeck(seed);
  assert.equal(new Set(deal.ids).size, 40);
  for (let rank = 1; rank <= 10; rank++) {
    assert.equal(deal.cards.filter(value => value === rank).length, 4);
  }
}

function prepare(seed = 161, angleMode = 'rad', existing = null) {
  const calc = existing || new MK61({ angleMode });
  if (!existing) calc.loadProgram(listing);
  calc.pressSequence(['F', '/-/']);
  keyboard(calc, setup[0]);
  keyboard(calc, setup[1].replace(/^161\b/, String(seed)));
  settle(calc);
  assert.deepEqual(calc.readProgramCodes(105), parsed.codes);
  return calc;
}
let totalPairs = 0, completeGames = 0;
function play(calc, seed, noisy = false) {
  const expected = model(seed);
  for (const [index, report] of expected.reports.entries()) {
    if (index === 0) assert.equal(number(calc.displayText()), report.value);
    else next(calc, report.value, `seed ${seed}, ${report.phase} report ${index}`, noisy);
    assert.equal(register(calc, '8'), report.product, 'current triple product');
    assert.deepEqual(values(calc, ['3', '4']), report.masks,
      'the ROM must account for exactly the distinct cards exposed by the model');
    assert.equal(Math.round(register(calc, '5') * 100000), report.state);
    assert.deepEqual(values(calc, ['1', '2']), [report.groups, report.pairs]);
    if (report.phase === 'pair') {
      assert.equal(100 * register(calc, 'E') + register(calc, '7'), report.value);
      totalPairs++;
    }
  }
  const finalState = values(calc, [...'0123456789ABCDE']);
  for (let repeat = 0; repeat < 2; repeat++) next(calc, expected.result, 'terminal report ignores input', true);
  keyboard(calc, 'В/О С/П');
  assert.equal(settle(calc), expected.result, 'returning to 00 restores the terminal report');
  assert.deepEqual(values(calc, [...'0123456789ABCDE']), finalState,
    'continuing after completion must not deal any more cards');
  assert.deepEqual(calc.readProgramCodes(105), parsed.codes);
  completeGames++;
  return expected;
}

// The default win includes two failed pairs, which must keep the product
// while consuming stock, and a successful ninth pair, which must still win.
const first = prepare();
const reference = play(first, 161, true);
assert.equal(reference.result, 0);
assert.equal(reference.reports.length, 17);
assert.equal(reference.attempts.length, 9);
const printed = [...card.split('## Контрольная партия\n')[1].split('## Регистры\n')[0]
  .matchAll(/^\| (\d+) \| (\d+), (\d+), (\d+) \| (\d+) \| (\d+) \| (\d+), (\d+) \| ([^|]+) \|$/gm)];
assert.equal(printed.length, 9);
printed.forEach((match, index) => {
  const attempt = reference.attempts[index];
  assert.deepEqual(match.slice(1, 9).map(Number), [index + 1, ...attempt.triple,
    attempt.product, 100 * attempt.divisors[0] + attempt.divisors[1], ...attempt.divisors]);
  assert.equal(match[9].includes('не подошла'), !attempt.accepted);
});

// After the default game's 39 cards, call the unchanged deal routine once
// through the keyboard. This exercises all forty bit positions, including
// rounding of 2^j on the actual ROM, and fills both masks exactly.
assert.equal(reference.consumed, 39);
keyboard(first, 'БП 05 С/П');
assert.equal(settle(first), 0, 'the standalone deal returns through the persistent win report');
assert.equal(register(first, '6'), reference.deck.ids[39]);
assert.deepEqual(values(first, ['3', '4']), [1048575, 1048575]);

const loss = play(prepare(10, 'rad', first), 10);
assert.equal(loss.removed, 0);
const partial = play(prepare(2, 'grad'), 2);
assert.equal(partial.result, 1);
assert.equal(partial.attempts.at(-1).accepted, true);
assert.equal(partial.removed, 2);
const early = play(prepare(878, 'deg'), 878);
assert.equal(early.result, 0);
assert.equal(early.attempts.length, 7);

// No manual step or boot flag: fractional and negative seeds are normalized
// by the listing itself. Restart a used machine with the documented setup.
play(prepare('-161.75', 'rad', first), 161, true);
play(prepare('-0.75'), 0);
play(prepare('99999.8'), 99999);

// Very large input must still produce valid masks and a five-decimal state,
// even when eight-digit ROM arithmetic loses low seed digits. Do not promise
// exact modulo equivalence for numbers outside the documented seed interval.
for (const seed of ['99999999', '1e99']) {
  const calc = prepare(seed);
  assert.ok(number(calc.displayText()) < 0);
  const before = values(calc, ['3', '4']);
  const bitCount = mask => mask.toString(2).replace(/0/g, '').length;
  assert.equal(before.reduce((sum, mask) => sum + bitCount(mask), 0), 3);
  calc.press('С/П');
  assert.ok(settle(calc) >= 101);
  const after = values(calc, ['3', '4']);
  after.forEach(mask => assert.ok(Number.isInteger(mask) && mask >= 0 && mask <= 1048575));
  assert.equal(after.reduce((sum, mask) => sum + bitCount(mask), 0), 5);
  const rng = register(calc, '5');
  assert.ok(rng >= 0 && rng < 1);
  assert.ok(Math.abs(rng * 100000 - Math.round(rng * 100000)) < 1e-7);
}

// Feed the book's 270/(6,7), then 270/(2,6), through ordinary register-write
// keys at a pair report. This tests the real division and transition code
// without patching any instruction, branch, ROM or emulator function.
const book = prepare();
next(book, 309, 'first pair');
keyboard(book, '270 хП8 6 хПe 7 хП7');
next(book, 206, '270/7 rejects the pair and deals two more cards');
assert.equal(register(book, '1'), 7);
assert.equal(register(book, '8'), 270);
keyboard(book, '2 хПe 6 хП7');
assert.notEqual(270 % (2 * 6), 0, 'the product of divisors need not divide 270');
next(book, -120, '270/2 and 270/6 remove exactly one triple');
assert.equal(register(book, '1'), 6);
assert.deepEqual(book.readProgramCodes(105), parsed.codes);

console.log(`Csákány cards and numbers: 100000 generator states/seeds, ${sameRank} same-rank first pairs; ${completeGames} complete ROM games, ${totalPairs} pairs; all 40 physical cards, keyboard setup/restarts, seed normalization, stock boundaries, angle modes and the book example OK.`);
