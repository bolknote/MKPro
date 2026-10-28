#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');
const source = require('./fixtures/shapiro-boyko-1989.json');
const root = path.resolve(__dirname, '../..');
const registerKeys = [...'0123456789', ',', '/-/', 'ВП', 'Cx', 'В↑'];
const manifest = fs.readFileSync(path.join(root, 'games/manifest.tsv'), 'utf8')
  .trimEnd().split('\n').slice(1).map(line => line.split('\t'));
const programs = new Map();
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
assert.equal(source.entries.length, 3);
assert.match(source.source_sha256, /^[a-f0-9]{64}$/);
assert.ok(source.source_url.startsWith('https://nzdr.ru/'));
for (const entry of source.entries) {
  const listing = fs.readFileSync(path.join(root, `games/${entry.stem}.txt`), 'utf8');
  const card = fs.readFileSync(path.join(root, `games/${entry.stem}.md`), 'utf8');
  const parsed = parseProgramText(listing);
  assert.deepEqual(parsed.diagnostics, []);
  assert.equal(parsed.codes.length, entry.length);
  assert.ok(entry.length <= 105);
  const lines = listing.trimEnd().split('\n');
  assert.equal(lines.length, entry.length);
  lines.forEach((line, i) => assert.ok(line.startsWith(`${String(i).padStart(2, '0')}\t`)));
  // This fixture transcribes the independently printed hexadecimal columns,
  // including 85.К- in Game 5. No compatibility or game-rule patch is allowed.
  assert.equal(Buffer.from(parsed.codes).toString('hex').toUpperCase(), entry.hex);
  assert.equal(sha256(Buffer.from(parsed.codes)), entry.sha256);
  const row = manifest.find(row => row[0] === `${entry.stem}.txt`);
  assert.ok(row);
  assert.deepEqual([row[1], row[2], row[6], row[7], row[8], row[9]],
    [`${entry.stem}.md`, entry.title, entry.stem.includes('igra-5') ? 'utility' : 'game',
      'logic', 'book;classic;original', 'shapiro-boyko-1989']);
  assert.ok(row[5].includes(source.source_url));
  assert.ok(card.includes(source.source_url));
  assert.ok(!/\]\([^)]*(?:\/Users\/|output\/research\/)/.test(card));
  const preparation = card.split('## Подготовка\n')[1].split('\n## ')[0];
  const blocks = [...preparation.matchAll(/```text\n([\s\S]*?)\n```/g)]
    .map(m => m[1]).filter(text => /^\d/.test(text));
  assert.equal(blocks.length, 1, `${entry.stem}: one keyboard preparation`);
  programs.set(entry.stem.split('/')[1].split('-shapiro-')[0],
    { listing, setup: blocks[0] });
}

function keyboard(calc, text) {
  for (const token of text.trim().split(/\s+/)) {
    if (/^\d+(?:[.,]\d+)?$/.test(token)) calc.inputNumber(token);
    else if (/^хП[0-9a-e]$/i.test(token)) {
      calc.press('П').press(registerKeys[parseInt(token.slice(2), 16)]);
    } else calc.press(token);
  }
}
function numeric(text) {
  const [mantissa, exponent = '0'] = text.trim().replace(',', '.').split(/\s+/);
  return Number(mantissa) * 10 ** Number(exponent);
}
const reg = (calc, r) => numeric(calc.readRegister(r));
function settle(calc) {
  assert.ok(calc.runUntilStable({ maxFrames: 30000, stableFrames: 8 }).stopped,
    'original program must reach its next stop');
  calc.runFrames(12);
  return calc.displayText();
}
function resume(calc) { calc.press('С/П'); settle(calc); return reg(calc, 'X'); }
function make(name, angleMode = 'rad', dirty = false) {
  const calc = new MK61({ angleMode });
  calc.loadProgram(programs.get(name).listing);
  if (dirty) {
    for (const r of '0123456789abcdeXYZTX1'.match(/X1|./g)) {
      calc.setRegister(r, 137 + r.charCodeAt(0));
    }
  }
  calc.pressSequence(['F', '/-/']);
  return calc;
}
function setup(name, calc = make(name)) {
  keyboard(calc, programs.get(name).setup);
  if (name === 'bochonok') settle(calc);
  return calc;
}
function equalArray(actual, expected, label) { assert.deepEqual(actual, expected, label); }

function testBarrel() {
  const name = 'bochonok';
  const bottles = calc => [...'6789'].map(r => reg(calc, r));
  const finished = xs => xs.every(x => x === xs[0]);
  const unpack = n => String(n).split('').map(Number);
  const rotate = (xs, k) => xs.map((_, i) => xs[(i - k + 4) % 4]);
  const changeCount = (a, b) => a.filter((x, i) => x !== b[i]).length;
  function output(calc) {
    const result = reg(calc, 'X');
    assert.match(String(result), /^[69]{4}$/);
    equalArray(bottles(calc), unpack(result), 'indicator is the current physical position');
    return bottles(calc);
  }
  function begin(calc, xs) {
    xs.forEach((x, i) => keyboard(calc, `${x} хП${i + 6}`));
    keyboard(calc, 'В/О С/П'); settle(calc);
    const result = output(calc);
    assert.ok(changeCount(xs, result) <= 2, 'a legal move can affect at most two bottles');
    return result;
  }
  function next(calc, previous, k) {
    calc.inputNumber(k, { clear: true }); resume(calc);
    const result = output(calc);
    assert.ok(changeCount(rotate(previous, k), result) <= 2,
      'the chosen clockwise rotation precedes a move of at most two bottles');
    return result;
  }
  for (const angle of ['rad', 'deg', 'grad']) {
    const calc = setup(name, make(name, angle, true));
    let state = output(calc);
    assert.equal(reg(calc, 'X'), 6669);
    for (const [k, expected] of [[3, 6696], [1, 9669], [2, 9696], [4, 6666]]) {
      state = next(calc, state, k); assert.equal(reg(calc, 'X'), expected);
    }
    assert.ok(finished(state));
    setup(name, calc); assert.equal(reg(calc, 'X'), 6669, 'keyboard restart');
  }
  // All physical starting positions, with rotation patterns exercising both
  // adjacent/opposite bottle choices. The oracle only knows the puzzle rules.
  const patterns = [[1, 1, 1, 1], [2, 2, 2, 2], [3, 3, 3, 3],
    [4, 4, 4, 4], [1, 4, 2, 3], [3, 1, 2, 4]];
  let games = 0;
  for (let mask = 0; mask < 16; mask++) {
    const start = Array.from({ length: 4 }, (_, i) => mask & (1 << i) ? 9 : 6);
    for (const pattern of patterns) {
      const calc = make(name, 'rad', true);
      let state = begin(calc, start), moves = 1;
      for (const k of pattern) {
        if (finished(state)) break;
        state = next(calc, state, k); moves++;
      }
      assert.ok(finished(state), `${start}, rotations ${pattern}: solved within five moves`);
      assert.ok(moves <= 5); games++;
    }
  }
  console.log(`Bochonok OK: printed example in 3 angle modes, dirty setup/restarts, ${games} complete games`);
}

function testRiver() {
  const name = 'missionery-i-lyudoedy';
  const bank = calc => [...'2345'].map(r => reg(calc, r)); // right C/M, left C/M
  const safe = ([c, m]) => m === 0 || c <= m;
  const passengers = [[1, 0], [2, 0], [0, 1], [0, 2], [1, 1]];
  function human(calc, [c, m]) {
    keyboard(calc, `${c} В↑ ${m} В/О С/П`); settle(calc);
    assert.equal(reg(calc, 'X'), c * 10 + m);
  }
  function confirm(state) {
    assert.ok(state.every(x => Number.isInteger(x) && x >= 0 && x <= 3));
    assert.equal(state[0] + state[2], 3);
    assert.equal(state[1] + state[3], 3);
    assert.ok(safe(state.slice(0, 2)) && safe(state.slice(2)));
  }
  const route = [[1, 1], [2, 0], [0, 2], [0, 2], [2, 0], [2, 0]];
  for (const angle of ['rad', 'deg', 'grad']) {
    const calc = setup(name, make(name, angle, true));
    let state = [0, 0, 3, 3];
    for (const [i, pair] of route.entries()) {
      human(calc, pair);
      state = [state[0] + pair[0], state[1] + pair[1], state[2] - pair[0], state[3] - pair[1]];
      equalArray(bank(calc), state); confirm(state);
      const response = resume(calc);
      if (i === route.length - 1) {
        assert.equal(response, 3.1415926);
        equalArray(bank(calc), [3, 3, 0, 0]);
      } else {
        assert.equal(response, [1, 10, 11, 10, 10][i]);
        const c = Math.floor(response / 10), m = response % 10;
        state = [state[0] - c, state[1] - m, state[2] + c, state[3] + m];
        equalArray(bank(calc), state); confirm(state);
      }
    }
    setup(name, calc); human(calc, [1, 1]);
    equalArray(bank(calc), [1, 1, 2, 2], 'the documented setup restarts a completed crossing');
    assert.equal(resume(calc), 1);
  }
  // Enumerate the reachable legal bank graph independently. Only the human's
  // forward decisions are free; the return policy is specified in the book.
  const queue = [[0, 0, 3, 3]], seen = new Set();
  let transitions = 0, victories = 0;
  while (queue.length) {
    const state = queue.shift(), key = state.join(',');
    if (seen.has(key)) continue;
    seen.add(key);
    for (const [c, m] of passengers) {
      if (c > state[2] || m > state[3]) continue;
      const after = [state[0] + c, state[1] + m, state[2] - c, state[3] - m];
      if (!safe(after.slice(0, 2)) || !safe(after.slice(2))) continue;
      const calc = setup(name, make(name, 'rad', true));
      state.forEach((n, i) => keyboard(calc, `${n} хП${i + 2}`));
      human(calc, [c, m]); equalArray(bank(calc), after); confirm(after);
      const answer = resume(calc);
      if (after[2] + after[3] === 0) {
        assert.equal(answer, 3.1415926); equalArray(bank(calc), after); victories++;
      } else {
        const back = after[1] === 0 || after[3] === 0 ? [1, 0] :
          after[1] === 1 ? [0, 1] : [1, 1];
        assert.equal(answer, 10 * back[0] + back[1]);
        const result = [after[0] - back[0], after[1] - back[1], after[2] + back[0], after[3] + back[1]];
        confirm(result); equalArray(bank(calc), result); queue.push(result);
      }
      transitions++;
    }
  }
  assert.ok(victories > 0);
  console.log(`Missionaries OK: complete crossing/restarts in 3 modes, ${seen.size} reachable states and ${transitions} legal forward moves`);
}

function testPuzzle() {
  const name = 'igra-5';
  const target = '123450'; // clockwise cell order 1,2,3,4,5,6
  const edges = [[1, 5], [0, 2, 4], [1, 3], [2, 4], [1, 3, 5], [0, 4]];
  function moves(board, text) {
    assert.match(text, /^[1-6]+$/);
    for (const cell of text) {
      const from = Number(cell) - 1, empty = board.indexOf(0);
      assert.ok(edges[empty].includes(from), `${board}: cell ${cell} must border the empty cell`);
      assert.notEqual(board[from], 0);
      [board[empty], board[from]] = [board[from], board[empty]];
    }
  }
  function request(calc, code) {
    keyboard(calc, `${code} В/О С/П`); return settle(calc);
  }
  // A graph search, independent of the solver's move tables, establishes
  // solvability on the physical 3x2 board.
  const reachable = new Set([target]), queue = [[1, 2, 3, 4, 5, 0]];
  while (queue.length) {
    const board = queue.shift(), empty = board.indexOf(0);
    for (const from of edges[empty]) {
      const next = [...board]; [next[empty], next[from]] = [next[from], next[empty]];
      const key = next.join('');
      if (!reachable.has(key)) { reachable.add(key); queue.push(next); }
    }
  }
  assert.equal(reachable.size, 360);
  for (const angle of ['rad', 'deg', 'grad']) {
    const calc = setup(name, make(name, angle, true));
    assert.equal(numeric(request(calc, 14)), 54325612);
    assert.equal(resume(calc), 56);
    assert.equal(numeric(request(calc, 24)), 543256);
    assert.equal(numeric(request(calc, 34)), 12345216);
    assert.match(request(calc, 45), /ЕГГ0Г/);
    // Error is terminal for that board. A fresh query explicitly returns to 00.
    assert.equal(numeric(request(calc, 15)), 5216);
    const easy = [2, 5, 3, 4, 1, 0]; moves(easy, String(reg(calc, 'X')));
    assert.equal(easy.join(''), target);
    // The printed prose mentions only a nonempty cell 6 as a reason to
    // continue. Query 25 needs its second part even though cell 6 is empty.
    const twoPart = [1, 3, 4, 5, 2, 0];
    assert.equal(numeric(request(calc, 25)), 12543216);
    moves(twoPart, String(reg(calc, 'X')));
    assert.equal(twoPart.indexOf(0), 5);
    assert.notEqual(twoPart[1], 2);
    assert.equal(resume(calc), 523456);
    moves(twoPart, String(reg(calc, 'X')));
    equalArray(twoPart.slice(0, 2), [1, 2]);
    assert.equal(twoPart.indexOf(0), 5);
  }
  function* permutations(xs) {
    if (xs.length === 0) { yield []; return; }
    for (let i = 0; i < xs.length; i++) {
      for (const tail of permutations(xs.filter((_, j) => j !== i))) yield [xs[i], ...tail];
    }
  }
  // Reuse one calculator between boards to verify the documented reusable table.
  const calc = setup(name, make(name, 'rad', true));
  let solved = 0, rejected = 0;
  for (const order of permutations([1, 2, 3, 4, 5])) {
    const board = [...order, 0], start = board.join('');
    let impossible = false, stages = 0;
    while (board.join('') !== target) {
      assert.ok(stages++ < 5, 'each completed stage must fix the next tile');
      assert.equal(board.indexOf(0), 5, 'each new query starts with empty cell 6');
      const tile = [1, 2, 3, 4, 5].find(n => board[n - 1] !== n);
      const code = 10 * tile + board.indexOf(tile) + 1;
      const display = request(calc, code);
      if (display.includes('ЕГГ0Г')) { impossible = true; break; }
      let continuations = 0;
      do {
        assert.ok(continuations++ < 3, 'finite continuation of the current move sequence');
        moves(board, String(reg(calc, 'X')));
        if (board.join('') === target ||
          (board.indexOf(0) === 5 && board[tile - 1] === tile)) break;
        resume(calc);
        assert.ok(!calc.displayText().includes('ЕГГ0Г'));
      } while (true);
      for (let n = 1; n <= tile; n++) assert.equal(board[n - 1], n,
        `${start}, query ${code}: complete both parts before the next query`);
    }
    assert.equal(impossible, !reachable.has(start), `independent solvability for ${start}`);
    if (impossible) rejected++;
    else { solved++; assert.equal(board.join(''), target); }
    equalArray([...'12345678'].map(r => reg(calc, r)),
      [1256, 12345612, 54325612, 5216, 523456, 543256, 12543216, 12345216],
      'the original move table is not changed between requests or boards');
  }
  assert.equal(solved, 60); assert.equal(rejected, 60);
  console.log('Game 5 OK: printed example in 3 modes, all 120 boards, 60 solutions and 60 impossibility reports against an independent board graph');
}

const selected = process.argv.slice(2);
assert.ok(selected.every(x => ['barrel', 'river', 'puzzle'].includes(x)),
  'Usage: node tests/emulator/shapiro-boyko-1989.cjs [barrel|river|puzzle]');
for (const [name, run] of [['barrel', testBarrel], ['river', testRiver], ['puzzle', testPuzzle]]) {
  if (selected.length === 0 || selected.includes(name)) run();
}
