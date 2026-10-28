#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');
const source = require('./fixtures/shapiro-boyko-board-digits-hanoi.json');
const root = path.resolve(__dirname, '../..');
const registerKeys = [...'0123456789', ',', '/-/', 'ВП', 'Cx', 'В↑'];
const rows = fs.readFileSync(path.join(root, 'games/manifest.tsv'), 'utf8')
  .trimEnd().split('\n').slice(1).map(line => line.split('\t'));
const programs = new Map();
assert.equal(source.entries.length, 6);
assert.equal(source.entries.reduce((sum, e) => sum + e.length, 0), 369);
assert.equal(source.source_sha256, require('./fixtures/shapiro-boyko-1989.json').source_sha256);
for (const e of source.entries) {
  const listing = fs.readFileSync(path.join(root, 'games', `${e.stem}.txt`), 'utf8');
  const card = fs.readFileSync(path.join(root, 'games', `${e.stem}.md`), 'utf8');
  const parsed = parseProgramText(listing);
  assert.deepEqual(parsed.diagnostics, []);
  assert.equal(parsed.codes.length, e.length);
  assert.equal(Buffer.from(parsed.codes).toString('hex').toUpperCase(), e.hex);
  assert.equal(crypto.createHash('sha256').update(Buffer.from(parsed.codes)).digest('hex'), e.sha256);
  const printed = [...Buffer.from(e.printed_hex, 'hex')];
  assert.equal(printed.length, e.length);
  for (const correction of e.code_errata || []) {
    assert.equal(printed[correction.address], parseInt(correction.printed_code, 16));
    printed[correction.address] = parseInt(correction.resolved_code, 16);
    assert.deepEqual(parseProgramText(correction.resolved_mnemonic).codes,
      [printed[correction.address]]);
    assert.ok(card.includes(`**По адресу ${correction.address}**`));
  }
  assert.deepEqual(parsed.codes, printed, 'only declared source-code corrections');
  for (const correction of e.mnemonic_errata || []) {
    assert.equal(parsed.codes[correction.address], parseInt(correction.printed_code, 16));
    assert.deepEqual(parseProgramText(correction.resolved_mnemonic).codes,
      [parsed.codes[correction.address]]);
  }
  if (e.author_model_patch) {
    assert.equal(e.author_model_patch.applied_to_this_listing, false);
    for (const address of e.author_model_patch.addresses) assert.equal(parsed.codes[address], 0x52);
  }
  listing.trimEnd().split('\n').forEach((line, i) => {
    assert.ok(line.startsWith(`${String(i).padStart(2, '0')}\t`));
  });
  const row = rows.find(row => row[0] === `${e.stem}.txt`);
  assert.ok(row);
  assert.deepEqual([row[1], row[2], row[3], row[6], row[7], row[8], row[9], row[10]],
    [`${e.stem}.md`, e.title, e.author, e.kind, 'logic', e.tags, e.collection, e.series]);
  assert.ok(card.includes(source.source_url) && row[5].includes(source.source_url));
  assert.ok(!/\]\([^)]*(?:\/Users\/|output\/research\/)/.test(card));
  const preparation = card.split('## Подготовка\n')[1].split('\n## ')[0];
  const blocks = [...preparation.matchAll(/```text\n([\s\S]*?)\n```/g)];
  assert.equal(blocks.length, 1, 'exactly one executable card setup');
  programs.set(e.id, { listing, card, setup: blocks[0][1], printed: e.printed_hex });
}

function numeric(text) {
  const [mantissa, exponent = '0'] = text.trim().replace(',', '.').split(/\s+/);
  return Number(mantissa) * 10 ** Number(exponent);
}
const reg = (c, r) => numeric(c.readRegister(r));
const pi = n => Math.abs(n - Math.PI) < 1e-6;
function settle(c) {
  assert.ok(c.runUntilStable({ maxFrames: 4000, stableFrames: 8 }).stopped,
    'the original program must reach its next stop');
  c.runFrames(12);
  return c.displayText() === 'ЕГГ0Г' ? 'win' : reg(c, 'X');
}
function resume(c) { c.press('С/П'); return settle(c); }
function input(c, n, enter = false) {
  c.inputNumber(n);
  if (enter) c.press('В↑');
  return resume(c);
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
  return c.displayText() === 'ЕГГ0Г' ? 'win' : reg(c, 'X');
}
function make(id, angle = 'deg', printed = false) {
  const c = new MK61({ angleMode: angle });
  c.loadProgram(printed ? [...Buffer.from(programs.get(id).printed, 'hex')] :
    programs.get(id).listing);
  for (const r of [...'0123456789abcde', 'X', 'Y', 'Z', 'T', 'X1']) {
    c.setRegister(r, 173 + r.charCodeAt(0));
  }
  c.pressSequence(['F', '/-/']);
  return c;
}
function setup(id, c = make(id), text = programs.get(id).setup) {
  keyboard(c, text); return c;
}
function begin(c, n) {
  if (n !== undefined) c.inputNumber(n);
  c.pressSequence(['В/О', 'С/П']); return settle(c);
}
const squareLines = labels => [
  [0, 1, 2], [3, 4, 5], [6, 7, 8], [0, 3, 6],
  [1, 4, 7], [2, 5, 8], [0, 4, 8], [2, 4, 6],
].map(line => line.map(i => labels[i]));
const classicLines = squareLines([1, 2, 3, 8, 9, 4, 7, 6, 5]);
const wildLines = squareLines([1, 2, 3, 5, 9, 4, 6, 7, 8]);
const trihexLines = [
  [1, 9, 4], [4, 3, 7], [7, 6, 1], [1, 8, 3], [4, 2, 6],
  [7, 5, 9], [2, 5, 3], [5, 8, 6], [8, 2, 9],
];
const empty = b => Array.from({ length: 9 }, (_, i) => i + 1).filter(i => b[i] === -1);
const won = (b, symbol, lines) => lines.some(line => line.every(i => b[i] === symbol));
function place(b, cell, symbol) {
  assert.ok(Number.isInteger(cell) && cell >= 1 && cell <= 9);
  assert.equal(b[cell], -1, `cell ${cell} is free`); b[cell] = symbol;
}
function winningCells(b, symbol, lines) {
  return empty(b).filter(cell => {
    const next = b.slice(); next[cell] = symbol; return won(next, symbol, lines);
  });
}

// These game-tree oracles know only the paper rules, not the book's strategy.
const classicCache = new Map();
function classicValue(b, who) {
  if (won(b, 1 - who, classicLines)) return -1;
  if (!empty(b).length) return 0;
  const key = `${b.slice(1).join(',')}/${who}`;
  if (classicCache.has(key)) return classicCache.get(key);
  let best = -1;
  for (const cell of empty(b)) {
    const next = b.slice(); next[cell] = who;
    best = Math.max(best, -classicValue(next, 1 - who));
  }
  classicCache.set(key, best); return best;
}
const wildCache = new Map();
function wildValue(b) {
  if (!empty(b).length) return 0;
  const key = b.slice(1).join(',');
  if (wildCache.has(key)) return wildCache.get(key);
  let best = -1;
  for (const cell of empty(b)) for (const symbol of [0, 1]) {
    const next = b.slice(); next[cell] = symbol;
    const value = won(next, symbol, wildLines) ? 1 : -wildValue(next);
    best = Math.max(best, value);
  }
  wildCache.set(key, best); return best;
}

function testClassic() {
  const c = setup('classic41');
  assert.equal(reg(c, 'X'), 9);
  assert.deepEqual([7, 2, 5, 8].map(q => input(c, q)), [6, 1, 4, 3.1415926]);
  begin(c); assert.equal(input(c, 4), 3); assert.equal(input(c, 7), 2);
  assert.equal(input(c, 1), 'win');
  keyboard(c, 'Cx В/О С/П'); assert.equal(reg(c, 'X'), 9, 'restart after error signal');
  let draws = 0, victories = 0;
  for (let first = 1; first <= 8; first++) {
    const c = setup('classic41'), b = Array(10).fill(-1);
    place(b, 9, 1);
    let q = first, ended = false;
    for (let turn = 0; turn < 5; turn++) {
      place(b, q, 0); assert.ok(!won(b, 0, classicLines));
      const answer = input(c, q);
      if (answer === 'win') {
        assert.equal(classicValue(b, 1), 1, 'early error signal is a forced PMK win');
        victories++; ended = true; break;
      }
      if (pi(answer)) {
        assert.equal(empty(b).length, 1); place(b, empty(b)[0], 1);
        assert.ok(!won(b, 1, classicLines) && !won(b, 0, classicLines));
        draws++; ended = true; break;
      }
      place(b, answer, 1);
      assert.ok(!won(b, 1, classicLines), 'source signals a win separately');
      const choices = empty(b).map(cell => {
        const next = b.slice(); next[cell] = 0;
        return { cell, value: -classicValue(next, 1) };
      }).sort((a, b) => b.value - a.value || a.cell - b.cell);
      assert.ok(choices.length); q = choices[0].cell;
    }
    assert.ok(ended);
  }
  assert.equal(draws, 4); assert.equal(victories, 4);
  console.log('Classic Tic-tac-toe OK: both book games, all 8 openings, 4 draws/4 forced wins, restart after ERR');
}

function testWild() {
  for (const angle of ['rad', 'deg', 'grad']) {
    const c = setup('wild71', make('wild71', angle));
    assert.equal(reg(c, 'X'), 91);
    assert.deepEqual([70, 40, 60].map(q => input(c, q)), [20, 50, 0]);
    assert.equal(begin(c, 0), 90, 'restart, retaining Rb');
    assert.equal(input(c, 11), 81);
  }
  let games = 0;
  for (const initial of [0, 1]) for (let cell = 1; cell <= 8; cell++) {
    for (const symbol of [0, 1]) {
      const c = make('wild71'), b = Array(10).fill(-1);
      setup('wild71', c, programs.get('wild71').setup.replace('1 В/О', `${initial} В/О`));
      place(b, 9, initial);
      let move = { cell, symbol }, ended = false;
      for (let turn = 0; turn < 4; turn++) {
        place(b, move.cell, move.symbol);
        assert.ok(!won(b, move.symbol, wildLines), 'human has not already won');
        const answer = input(c, 10 * move.cell + move.symbol);
        if (answer === 0 || answer === 1) {
          const finishers = winningCells(b, answer, wildLines);
          assert.ok(finishers.length, 'reported symbol has an actual winning cell');
          place(b, finishers[0], answer); assert.ok(won(b, answer, wildLines));
          ended = true; games++; break;
        }
        const p = Math.floor(answer / 10), s = answer % 10;
        assert.ok(s === 0 || s === 1); place(b, p, s);
        assert.equal(wildValue(b), -1, 'returned move leaves a losing position to the human');
        const choices = [];
        for (const p of empty(b)) for (const s of [0, 1]) {
          const next = b.slice(); next[p] = s;
          choices.push({ cell: p, symbol: s,
            value: won(next, s, wildLines) ? 1 : -wildValue(next) });
        }
        choices.sort((a, b) => b.value - a.value || a.cell - b.cell || a.symbol - b.symbol);
        assert.ok(choices.length); move = choices[0];
      }
      assert.ok(ended, 'source wins within four PMK moves');
    }
  }
  assert.equal(games, 32);
  console.log('Wild Tic-tac-toe OK: 32 complete games, every first cell/symbol, independent game tree, 3 modes/restarts');
}

function digitCounts(n) {
  const counts = Array(10).fill(0);
  for (const digit of String(n).padStart(4, '0')) counts[Number(digit)]++;
  return counts;
}
function testDigits() {
  const common = (a, b) => digitCounts(a).reduce((sum, count, i) =>
    sum + Math.min(count, digitCounts(b)[i]), 0);
  for (const angle of ['rad', 'deg', 'grad']) {
    const c = setup('digits28', make('digits28', angle));
    const got = [reg(c, 'X')];
    for (const q of [0, 1, 2, 3, 3, 4]) got.push(input(c, q));
    assert.deepEqual(got, [0, 1111, 1222, 1233, 1234, 1235, 0]);
    setup('digits28', c); assert.equal(reg(c, 'X'), 0);
    assert.equal(input(c, 4), 0, '0000 secret completes immediately');
  }
  const secrets = [...new Set([
    ...Array.from({ length: 10 }, (_, i) => 1111 * i),
    1, 11, 111, 9, 99, 999, 17, 117, 177, 777, 1122, 1123, 123,
    129, 237, 249, 1234, 1532, 2349, 2468, 3456, 1357, 5566, 6789, 8889, 8999,
  ])];
  let questions = 0;
  for (const secret of secrets) {
    const c = setup('digits28'), sorted = String(secret).padStart(4, '0').split('').sort().join('');
    let guess = reg(c, 'X'), done = false;
    for (let turn = 1; turn <= 10; turn++) {
      assert.ok(Number.isInteger(guess) && guess >= 0 && guess <= 9999);
      const count = common(secret, guess); questions++;
      const next = input(c, count);
      if (count === 4) {
        assert.equal(String(guess).padStart(4, '0').split('').sort().join(''), sorted);
        assert.equal(next, 0); done = true;
        assert.equal(turn, Number(sorted.at(-1)) + 1); break;
      }
      guess = next;
    }
    assert.ok(done, `secret ${secret} completed`);
  }
  console.log(`Digits OK: ${secrets.length} multisets/${questions} questions, multiplicity/leading zeros, 3 modes/restarts`);
}

function testHanoi() {
  const literal = make('hanoi49', 'deg', true);
  assert.equal(begin(literal, 5), 22, 'printed 6B error is reproduced');
  let moves = 0;
  for (let n = 1; n <= 6; n++) {
    const c = make('hanoi49'), poles = [Array.from({ length: n }, (_, i) => n - i), [], []];
    setup('hanoi49', c, programs.get('hanoi49').setup.replace('5 В/О', `${n} В/О`));
    let answer = reg(c, 'X'), count = 0;
    assert.equal(answer, n % 2 ? 12 : 13);
    while (true) {
      const a = Math.floor(answer / 10) - 1, b = answer % 10 - 1;
      assert.ok(a >= 0 && a < 3 && b >= 0 && b < 3 && a !== b);
      const disk = poles[a].pop(); assert.ok(disk);
      assert.ok(!poles[b].length || disk < poles[b].at(-1), 'never put a larger disk on a smaller one');
      poles[b].push(disk); count++; moves++;
      assert.deepEqual([1, 2, 3].map(r => reg(c, String(r))), poles.map(p => p.length));
      if (poles[1].length === n) break;
      const choices = [0, 1, 2].filter(i => i !== b && poles[i].length)
        .sort((a, b) => poles[a].at(-1) - poles[b].at(-1));
      const nextSource = choices[0] + 1;
      answer = input(c, nextSource);
      assert.equal(Math.floor(answer / 10), nextSource);
      assert.ok(count < 2 ** n);
    }
    assert.equal(count, 2 ** n - 1);
    setup('hanoi49', c); assert.equal(reg(c, 'X'), 12, 'restart after a complete tower');
  }
  console.log(`Hanoi OK: 6 complete physical towers/${moves} legal moves, parity, restarts, literal-code counterexample`);
}

function testTrihex() {
  const card = programs.get('trihex82').card;
  const block = card.match(/```text\n([\s\S]*?)\n```/)[1];
  const cardLines = [...block.matchAll(/(\d)–(\d)–(\d)/g)].map(m => m.slice(1).map(Number));
  assert.deepEqual(cardLines, trihexLines);
  const threat = b => winningCells(b, 1, trihexLines);
  function unpack(b, answer) {
    const cells = answer >= 10 ? [Math.floor(answer / 10), answer % 10] : [answer];
    for (let i = 0; i < cells.length; i++) {
      place(b, cells[i], 1);
      if (i + 1 < cells.length) {
        const forced = threat(b); assert.equal(forced.length, 1, 'unique implied defense');
        place(b, forced[0], 0); assert.ok(!won(b, 0, trihexLines));
      }
    }
  }
  let games = 0;
  for (const first of [1, 3, 4, 5, 6, 7, 8, 9]) {
    const nextMoves = first === 5 ? [1, 3, 4, 6] : first === 8 ? [4, 6, 7, 9] : [null];
    for (const next of nextMoves) {
      const c = setup('trihex82'), b = Array(10).fill(-1); place(b, 2, 1); place(b, first, 0);
      let answer = input(c, first, true);
      if (!pi(answer)) {
        unpack(b, answer); assert.ok(!won(b, 1, trihexLines));
        place(b, next, 0); assert.ok(!won(b, 0, trihexLines));
        answer = input(c, next, true);
      }
      assert.ok(pi(answer));
      c.press('↔'); unpack(b, reg(c, 'X'));
      assert.ok(won(b, 1, trihexLines) || threat(b).length >= 2, 'terminal display really creates a fork');
      assert.ok(!won(b, 0, trihexLines)); games++;
    }
  }
  const c = setup('trihex82'); assert.equal(input(c, 8, true), 51);
  assert.ok(pi(input(c, 4, true))); c.press('↔'); assert.equal(reg(c, 'X'), 7);
  setup('trihex82', c); assert.ok(pi(input(c, 7, true))); c.press('↔'); assert.equal(reg(c, 'X'), 85);
  console.log(`Trihex OK: ${games} complete strategy branches, paper line graph, implied moves, XY reads/restarts`);
}

// Coordinates are doubled to keep the two offset grids integral. This oracle
// checks physical edges/connectivity instead of reimplementing the source formulas.
function point(n, id, human) {
  const a = (id - 1) % n, b = Math.floor((id - 1) / n);
  return human ? [2 * b, 2 * a + 1] : [2 * a + 1, 2 * b];
}
const edgeKey = ([a, b]) => [a, b].sort((a, b) => a - b).join(',');
function crosses(n, h, p) {
  const [a, b] = h.map(id => point(n, id, true)), [c, d] = p.map(id => point(n, id, false));
  if (a[0] === b[0] && c[1] === d[1]) {
    return Math.min(c[0], d[0]) < a[0] && a[0] < Math.max(c[0], d[0]) &&
      Math.min(a[1], b[1]) < c[1] && c[1] < Math.max(a[1], b[1]);
  }
  if (a[1] === b[1] && c[0] === d[0]) {
    return Math.min(a[0], b[0]) < c[0] && c[0] < Math.max(a[0], b[0]) &&
      Math.min(c[1], d[1]) < a[1] && a[1] < Math.max(c[1], d[1]);
  }
  return false;
}
function edges(n, human) {
  const count = n * (n + 1), result = [];
  for (let a = 1; a <= count; a++) for (let b = a + 1; b <= count; b++) {
    const p = point(n, a, human), q = point(n, b, human);
    if (Math.abs(p[0] - q[0]) + Math.abs(p[1] - q[1]) !== 2) continue;
    if (human && p[0] === q[0] && (p[0] === 0 || p[0] === 2 * n)) continue;
    result.push([a, b]);
  }
  return result;
}
function connected(n, es) {
  const seen = new Set([1]), pending = [1];
  while (pending.length) {
    const a = pending.pop();
    for (const [x, y] of es) {
      const b = x === a ? y : y === a ? x : null;
      if (b !== null && !seen.has(b)) { seen.add(b); pending.push(b); }
    }
  }
  return seen.has(n * (n + 1));
}
function borderCompletion(n) {
  return edges(n, false).filter(edge => edge.every(id => {
    const y = point(n, id, false)[1]; return y === 0 || y === 2 * n;
  }));
}
const bridgeForced = (n, ps) => connected(n, [...ps, ...borderCompletion(n)]);
function bridgeSetup(n, angle = 'rad', c = make('bridge98', angle)) {
  const setupText = programs.get('bridge98').setup
    .replace('4 хП8', `${n} хП8`).replace('15 хП2', `${n * n - 1} хП2`)
    .replace('3 хП3', `${n - 1} хП3`).replace('5 хП4', `${n + 1} хП4`);
  setup('bridge98', c, setupText);
  assert.equal(reg(c, 'X'), 1); assert.equal(resume(c), n + 1); return c;
}
function bridgeMove(c, n, h) {
  const distance = id => { const [x, y] = point(n, id, true); return Math.abs(y - x - 1); };
  const [a, b] = distance(h[0]) > distance(h[1]) ? h : [h[1], h[0]];
  c.inputNumber(a).press('В↑');
  const raw = [input(c, b), resume(c)];
  assert.ok(raw.every(Number.isInteger));
  const [y, x] = raw.some(q => q < 0) ? raw.map(q => q + a) : raw;
  return { edge: [x, y], raw, input: [a, b] };
}
function testBridge() {
  const book = [
    [[5, 6], [2, 6]], [[9, 10], [3, 7]], [[13, 14], [4, 8]],
    [[18, 14], [12, 11]], [[14, 10], [11, 10]], [[10, 6], [9, 5]],
    [[7, 6], [13, 9]], [[4, 8], [13, 14]], [[8, 12], [14, 15]],
    [[12, 16], [20, 16]], [[15, 16], [-3, 1]], [[15, 11], [14, 10]],
  ];
  function accept(n, hs, ps, h, p) {
    assert.ok(edges(n, true).some(e => edgeKey(e) === edgeKey(h)));
    assert.ok(edges(n, false).some(e => edgeKey(e) === edgeKey(p)));
    assert.ok(!hs.some(e => edgeKey(e) === edgeKey(h)));
    assert.ok(!ps.some(e => edgeKey(e) === edgeKey(p)));
    assert.ok(!ps.some(e => crosses(n, h, e)));
    assert.ok(![...hs, h].some(e => crosses(n, e, p)));
    hs.push(h); ps.push(p);
  }
  for (const angle of ['rad', 'deg', 'grad']) {
    const c = bridgeSetup(4, angle), hs = [], ps = [[1, 5]];
    for (const [h, raw] of book) {
      const answer = bridgeMove(c, 4, h); assert.deepEqual(answer.input, h);
      assert.deepEqual(answer.raw, raw); accept(4, hs, ps, h, answer.edge);
      assert.ok(!connected(4, hs));
    }
    assert.ok(connected(4, ps), 'printed party actually connects both PMK corners');
    keyboard(c, '5 хПc Cx В/О С/П');
    assert.equal(reg(c, 'X'), 1); assert.equal(resume(c), 5, 'minimal card restart');
  }
  let replies = 0, games = 0;
  for (const n of [2, 3, 4, 5]) {
    const c = bridgeSetup(n), used = new Set();
    for (const h of edges(n, true).filter(h => !crosses(n, h, [1, n + 1]))) {
      const answer = bridgeMove(c, n, h), key = edgeKey(answer.edge);
      assert.ok(!used.has(key), 'different human edges have different paired replies');
      used.add(key); accept(n, [], [[1, n + 1]], h, answer.edge); replies++;
    }
    for (const pattern of [0, 1, 2]) {
      const c = bridgeSetup(n), hs = [], ps = [[1, n + 1]];
      const all = edges(n, true);
      while (!bridgeForced(n, ps)) {
        const legal = all.filter(h => !hs.some(e => edgeKey(e) === edgeKey(h)) &&
          !ps.some(p => crosses(n, h, p)));
        assert.ok(legal.length, 'a game reaches an unpreventable PMK connection');
        const h = legal[pattern === 0 ? 0 : pattern === 1 ? legal.length - 1 :
          (7 * hs.length + n) % legal.length];
        const answer = bridgeMove(c, n, h); accept(n, hs, ps, h, answer.edge);
        assert.ok(!connected(n, hs), 'no human win before the PMK connection');
      }
      // The paper rules allow the harmless horizontal boundary extensions;
      // the calculator does not emit them. Prove the resulting corner path
      // independently, rather than assuming every reply reaches a corner.
      for (const p of borderCompletion(n)) {
        assert.ok(!edges(n, true).some(h => crosses(n, h, p)));
      }
      assert.ok(connected(n, [...ps, ...borderCompletion(n)]));
      games++;
    }
  }
  console.log(`Bridg-it OK: complete book game in 3 modes, ${replies} distinct pairings, ${games} games to forced connection on 4 sizes/restarts`);
}

const tests = { classic: testClassic, wild: testWild, digits: testDigits,
  hanoi: testHanoi, trihex: testTrihex, bridge: testBridge };
const selected = process.argv.slice(2);
for (const name of selected.length ? selected : Object.keys(tests)) {
  assert.ok(tests[name], `unknown test group ${name}`); tests[name]();
}
