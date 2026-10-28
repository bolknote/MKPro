#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');
const source = require('./fixtures/shapiro-boyko-road-queen-bulls.json');
const root = path.resolve(__dirname, '../..');
const registerKeys = [...'0123456789', ',', '/-/', 'ВП', 'Cx', 'В↑'];
const manifest = fs.readFileSync(path.join(root, 'games/manifest.tsv'), 'utf8')
  .split('\n').slice(1).filter(Boolean).map(line => line.split('\t'));
const programs = new Map();
assert.equal(source.entries.length, 4);
assert.equal(source.source_sha256, require('./fixtures/shapiro-boyko-1989.json').source_sha256);
for (const entry of source.entries) {
  const listing = fs.readFileSync(path.join(root, 'games', `${entry.stem}.txt`), 'utf8');
  const card = fs.readFileSync(path.join(root, 'games', `${entry.stem}.md`), 'utf8');
  const parsed = parseProgramText(listing);
  assert.deepEqual(parsed.diagnostics, []);
  assert.equal(parsed.codes.length, entry.length);
  assert.equal(Buffer.from(parsed.codes).toString('hex').toUpperCase(), entry.hex);
  assert.equal(crypto.createHash('sha256').update(Buffer.from(parsed.codes)).digest('hex'), entry.sha256);
  for (const erratum of entry.mnemonic_errata || []) {
    const mnemonic = parseProgramText(erratum.resolved_mnemonic);
    assert.deepEqual(mnemonic.diagnostics, []);
    assert.equal(parsed.codes[erratum.address], parseInt(erratum.printed_code, 16));
    assert.deepEqual(mnemonic.codes, [parsed.codes[erratum.address]]);
    assert.ok(card.includes(`**${erratum.address}**`));
  }
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
  c.inputNumber(String(Math.abs(n)));
  if (n < 0) c.press('/-/');
  return resume(c);
}
function keyboard(c, text) {
  for (const token of text.trim().split(/\s+/)) {
    if (/^\d+(?:[.,]\d+)?$/.test(token)) c.inputNumber(token);
    else if (/^хП[0-9a-e]$/i.test(token)) {
      c.press('П').press(registerKeys[parseInt(token.slice(2), 16)]);
    } else if (/^Пх[0-9a-e]$/i.test(token)) {
      c.press('ИП').press(registerKeys[parseInt(token.slice(2), 16)]);
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
const pi = x => Math.abs(x - Math.PI) < 1e-6;
function pair(c, a, b) { c.inputNumber(a).press('В↑').inputNumber(b); return resume(c); }
function roadBegin(id, n, m, c = make(id)) {
  const setup = programs.get(id).setup;
  keyboard(c, id === 'road55' ? setup.replace('25 хП2', `${n} хП2`).replace('3 хП5', `${m} хП5`) :
    setup.replace('3 В↑ 17', `${m} В↑ ${n}`));
  return c;
}
const gap = (n, s) => n - 2 - s.h - s.c;
function roadMoves(n, m, s, who) {
  return Array.from({ length: 2 * m + 1 }, (_, i) => i - m)
    .filter(q => q !== 0 && s[who] + q >= 0 && q <= gap(n, s));
}
const roadKey = (h, c, turn) => `${h},${c},${turn}`;
const roadOracles = new Map();
function roadOracle(n, m) {
  const cacheKey = `${n}/${m}`;
  if (roadOracles.has(cacheKey)) return roadOracles.get(cacheKey);
  const nodes = new Map(), queue = [];
  for (let h = 0; h <= n - 2; h++) for (let c = 0; c <= n - 2 - h; c++) {
    for (const turn of ['h', 'c']) nodes.set(roadKey(h, c, turn),
      { h, c, turn, parents: [], children: [], value: 0, rank: 0 });
  }
  for (const node of nodes.values()) {
    for (const q of roadMoves(n, m, node, node.turn)) {
      const next = { h: node.h, c: node.c, [node.turn]: node[node.turn] + q };
      const child = nodes.get(roadKey(next.h, next.c, node.turn === 'h' ? 'c' : 'h'));
      node.children.push({ q, child }); child.parents.push(node);
    }
    node.remaining = node.children.length;
    if (!node.remaining) { node.value = -1; queue.push(node); }
  }
  // Retrograde analysis handles backward moves and cycles without copying the book's strategy.
  for (let i = 0; i < queue.length; i++) {
    const child = queue[i];
    for (const parent of child.parents) {
      if (parent.value) continue;
      if (child.value === -1) {
        parent.value = 1; parent.rank = child.rank + 1; queue.push(parent);
      } else if (--parent.remaining === 0) {
        parent.value = -1;
        parent.rank = 1 + Math.max(...parent.children.map(e => e.child.rank));
        queue.push(parent);
      }
    }
  }
  const lookup = (s, turn) => nodes.get(roadKey(s.h, s.c, turn));
  roadOracles.set(cacheKey, lookup); return lookup;
}
function chooseRoad(n, m, state, policy) {
  const options = roadMoves(n, m, state, 'h');
  assert.ok(options.length);
  if (policy === 'retreat') return options[0];
  if (policy === 'optimal') {
    const node = roadOracle(n, m)(state, 'h');
    const winning = node.children.filter(e => e.child.value === -1)
      .sort((a, b) => a.child.rank - b.child.rank);
    if (winning.length) return winning[0].q;
  }
  return options.at(-1);
}

function testRoad55() {
  const id = 'road55';
  for (const angle of ['rad', 'deg', 'grad']) {
    const c = roadBegin(id, 25, 3, make(id, angle));
    assert.deepEqual([reg(c, 'X'), input(c, 1), input(c, 4), input(c, -1)], [3, 3, 3, 1]);
    roadBegin(id, 25, 3, c); assert.equal(reg(c, 'X'), 3);
  }
  let games = 0, moves = 0, humanFirst = 0;
  for (const [n, m] of [[9, 2], [14, 3], [25, 3], [13, 1], [7, 5]]) {
    const oracle = roadOracle(n, m);
    for (const policy of ['forward', 'retreat', 'optimal']) {
      const c = roadBegin(id, n, m), initial = reg(c, 'X');
      const s = { h: 0, c: 0 }; let response = initial, first = true, turns = 0;
      while (true) {
        const finished = pi(response);
        const p = finished ? resume(c) : response;
        if (p === 0 && first) humanFirst++;
        else {
          assert.ok(roadMoves(n, m, s, 'c').includes(p)); s.c += p; moves++;
        }
        assert.equal(reg(c, '2'), gap(n, s)); assert.equal(reg(c, '8'), s.h);
        assert.equal(oracle(s, 'h').value, -1, 'computer leaves a losing physical position');
        if (finished) { assert.deepEqual(roadMoves(n, m, s, 'h'), []); break; }
        const q = chooseRoad(n, m, s, policy); s.h += q; moves++;
        response = input(c, q); first = false; assert.ok(++turns < 2 * n);
      }
      roadBegin(id, n, m, c); assert.equal(reg(c, 'X'), initial); games++;
    }
  }
  assert.ok(humanFirst > 0);
  const c = roadBegin(id, 25, 3);
  for (const q of [0, 4, -1, 21]) {
    const before = ['2', '7', '8'].map(r => reg(c, r));
    assert.equal(input(c, q), 3);
    assert.deepEqual(['2', '7', '8'].map(r => reg(c, r)), before, 'invalid integer leaves the board');
  }
  input(c, 0.5); assert.equal(reg(c, '8'), 0.5, 'source does not reject fractional moves');
  roadBegin(id, 25, 3, c); assert.equal(reg(c, 'X'), 3);
  console.log(`Road55 OK: printed example in 3 modes, ${games} complete games/${moves} moves, first-player choice, independent board graph, rejection and restarts`);
}

function testRoad83() {
  const id = 'road83';
  for (const angle of ['rad', 'deg', 'grad']) {
    const c = roadBegin(id, 17, 3, make(id, angle)), got = [reg(c, 'X')];
    for (const q of [3, 3, 3, 3, 2, 1, 5, 1, 1]) got.push(input(c, q));
    assert.deepEqual(got, [0, 1, 1, 1, -1, -2, -1, -1, -1, 0]);
    assert.equal(reg(c, '4'), 2);
    roadBegin(id, 17, 3, c); assert.equal(reg(c, '4'), 0); assert.equal(reg(c, 'X'), 0);
  }
  let games = 0, moves = 0; const wins = { h: 0, c: 0 };
  for (const [n, m] of [[17, 3], [18, 3], [9, 2], [8, 2]]) {
    const oracle = roadOracle(n, m);
    for (const policy of ['forward', 'optimal']) {
      const c = roadBegin(id, n, m), s = { h: 0, c: 0 };
      let winner, turns = 0;
      while (true) {
        const q = chooseRoad(n, m, s, policy); s.h += q; moves++;
        const response = input(c, q);
        if (!roadMoves(n, m, s, 'c').length) {
          assert.equal(response, 0); winner = 'h'; break;
        }
        let p = response;
        if (pi(response)) {
          keyboard(c, 'Пх7'); p = reg(c, 'X');
        }
        assert.ok(roadMoves(n, m, s, 'c').includes(p), 'legal forward or backward PMK move');
        s.c += p; moves++;
        assert.equal(reg(c, '2'), gap(n, s));
        assert.equal(reg(c, '8'), s.c); assert.equal(reg(c, '9'), s.h);
        assert.equal(reg(c, '4'), 0);
        if (pi(response)) {
          assert.deepEqual(roadMoves(n, m, s, 'h'), []); winner = 'c'; break;
        }
        assert.notEqual(p, 0); assert.ok(++turns < 3 * n);
      }
      if (policy === 'optimal') {
        const value = oracle({ h: 0, c: 0 }, 'h').value;
        assert.notEqual(value, 0);
        assert.equal(winner, value === 1 ? 'h' : 'c', 'outcome agrees with independent game graph');
      }
      wins[winner]++; games++;
      roadBegin(id, n, m, c); assert.equal(reg(c, 'X'), 0); assert.equal(reg(c, '4'), 0);
    }
  }
  assert.ok(wins.h > 0 && wins.c > 0);
  const c = roadBegin(id, 17, 3), board = () => ['2', '8', '9'].map(r => reg(c, r));
  const before = board();
  assert.equal(input(c, 4), -2); assert.deepEqual(board(), before); assert.equal(reg(c, '4'), 1);
  assert.equal(input(c, -1), -1); assert.deepEqual(board(), before); assert.equal(reg(c, '4'), 2);
  assert.ok(pi(input(c, 16))); assert.deepEqual(board(), before); assert.equal(reg(c, '4'), 3);
  roadBegin(id, 17, 3, c);
  input(c, 0); assert.equal(reg(c, '9'), 0); assert.equal(reg(c, '4'), 0);
  assert.notEqual(reg(c, '8'), 0, 'source accepts a forbidden pass and moves the computer');
  roadBegin(id, 17, 3, c); assert.equal(reg(c, 'X'), 0);
  console.log(`Road83 OK: printed win in 3 modes, ${games} complete games/${moves} moves, both winners, board graph, 3-strike loss, pass limitation and restarts`);
}

function queenMoves([a, b]) {
  const out = [];
  for (let k = 1; k <= a; k++) out.push([a - k, b]);
  for (let k = 1; k <= b; k++) out.push([a, b - k]);
  for (let k = 1; k <= Math.min(a, b); k++) out.push([a - k, b - k]);
  return out;
}
function queenBegin(a, c = make('queen67')) {
  keyboard(c, programs.get('queen67').setup.replace('5 В↑', `${a} В↑`)); return c;
}
function testQueen() {
  const memo = new Map();
  function winning(pos) {
    const key = pos.join(',');
    if (!memo.has(key)) memo.set(key, queenMoves(pos).some(p => !winning(p)));
    return memo.get(key);
  }
  for (const angle of ['rad', 'deg', 'grad']) {
    const c = queenBegin(5, make('queen67', angle));
    assert.equal(reg(c, 'X'), 35); assert.equal(pair(c, 3, 1), 21); assert.ok(pi(pair(c, 1, 1)));
    queenBegin(4, c); assert.equal(reg(c, 'X'), 0);
  }
  let games = 0, moves = 0;
  for (const policy of ['first', 'last', 'winning']) {
    const c = make('queen67');
    for (let a = 0; a < 8; a++) {
      queenBegin(a, c); let pos = [a, 7], response = reg(c, 'X'), first = true;
      while (true) {
        if (pi(response)) {
          assert.ok(queenMoves(pos).some(p => p[0] === 0 && p[1] === 0)); moves++; break;
        }
        if (first && response === 0) assert.equal(a, 4);
        else {
          assert.ok(Number.isInteger(response));
          const next = [Math.floor(response / 10), response % 10];
          assert.ok(queenMoves(pos).some(p => p[0] === next[0] && p[1] === next[1]));
          pos = next; moves++;
        }
        assert.ok(!winning(pos), 'PMK hands the player a losing queen position');
        const choices = queenMoves(pos); assert.ok(choices.length);
        pos = policy === 'last' ? choices.at(-1) : policy === 'winning' ?
          choices.find(p => !winning(p)) || choices[0] : choices[0];
        moves++; response = pair(c, ...pos); first = false;
      }
      games++;
    }
  }
  console.log(`Queen OK: book example in 3 modes, ${games} complete games/${moves} moves, all 8 initial columns, independent legal-move graph and restarts`);
}

function cowBull(secret, guess) {
  const a = [...secret], b = [...guess]; let bulls = 0, cows = 0;
  for (let i = 0; i < 2; i++) if (a[i] === b[i]) { bulls++; a[i] = null; b[i] = undefined; }
  for (const d of b) {
    if (d === undefined) continue;
    const i = a.indexOf(d); if (i >= 0) { cows++; a[i] = null; }
  }
  return [cows, bulls];
}
function bullsBegin(c = make('bulls79')) {
  keyboard(c, programs.get('bulls79').setup); assert.equal(reg(c, 'X'), 10); return c;
}
function bullsGame(c, secret) {
  const history = []; let guess = reg(c, 'X');
  while (!pi(guess)) {
    assert.ok(Number.isInteger(guess) && guess >= 0 && guess <= 99);
    const g = String(guess).padStart(2, '0'), answer = cowBull(secret, g);
    history.push(g); guess = pair(c, ...answer);
    assert.ok(history.length <= 11);
    if (pi(guess)) {
      const [k, b] = answer;
      assert.equal(b === 2 ? g : k === 2 ? [...g].reverse().join('') : null, secret,
        'pi is justified by a complete match or a deduced reversal');
    }
  }
  return history;
}
function testBulls() {
  for (const angle of ['rad', 'deg', 'grad']) {
    const c = bullsBegin(make('bulls79', angle));
    assert.deepEqual(bullsGame(c, '53'), ['10', '21', '32', '24', '33', '43', '53']);
    bullsBegin(c); assert.deepEqual(bullsGame(c, '01'), ['10']);
  }
  const c = make('bulls79'); let questions = 0, maximum = 0;
  for (let n = 1; n <= 99; n++) {
    bullsBegin(c); const history = bullsGame(c, String(n).padStart(2, '0'));
    questions += history.length; maximum = Math.max(maximum, history.length);
    assert.equal(reg(c, '7'), 61); assert.equal(reg(c, '8'), 68); assert.equal(reg(c, '9'), 73);
  }
  assert.equal(maximum, 11);
  bullsBegin(c);
  assert.deepEqual(bullsGame(c, '19'), ['10', '11', '11', '12', '13', '14', '15', '16', '17', '18', '19']);
  assert.deepEqual(cowBull('31', '11'), [0, 1], 'one matching copy cannot be both bull and cow');
  console.log(`Reverse Bulls OK: all 99 secrets/${questions} questions, maximum ${maximum}, multiplicity, leading zeros, reversal completion, 3 modes and keyboard restarts`);
}

const group = process.argv[2];
if (!group || group === 'road') { testRoad55(); testRoad83(); }
if (!group || group === 'queen') testQueen();
if (!group || group === 'bulls') testBulls();
