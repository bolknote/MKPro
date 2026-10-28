#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');
const source = require('./fixtures/ochkov-kvant-games.json');
const root = path.resolve(__dirname, '../..');
const keys = [...'0123456789', ',', '/-/', 'ВП', 'Cx', 'В↑'];
const programs = new Map();
const manifest = fs.readFileSync(path.join(root, 'games/manifest.tsv'), 'utf8')
  .trimEnd().split('\n').slice(1).map(line => line.split('\t'));
for (const [i, entry] of source.entries.entries()) {
  const listing = fs.readFileSync(path.join(root, `games/${entry.stem}.txt`), 'utf8');
  const card = fs.readFileSync(path.join(root, `games/${entry.stem}.md`), 'utf8');
  const parsed = parseProgramText(listing);
  assert.deepEqual(parsed.diagnostics, []);
  assert.equal(parsed.codes.length, entry.length);
  assert.equal(Buffer.from(parsed.codes).toString('hex').toUpperCase(), entry.hex);
  if (entry.source_mnemonics) {
    const original = parseProgramText(entry.source_mnemonics.join(' '));
    assert.deepEqual(original.diagnostics, []);
    assert.deepEqual(parsed.codes, original.codes, 'unchanged mnemonic-only book table');
  }
  assert.equal(crypto.createHash('sha256').update(Buffer.from(parsed.codes)).digest('hex'), entry.sha256);
  const lines = listing.trimEnd().split('\n');
  assert.equal(lines.length, entry.length);
  lines.forEach((line, address) => assert.ok(line.startsWith(`${String(address).padStart(2, '0')}\t`)));
  const row = manifest.find(row => row[0] === `${entry.stem}.txt`);
  assert.ok(row);
  assert.deepEqual([row[1], row[2], row[3], row[6], row[7], row[8], row[9], row[10]],
    [`${entry.stem}.md`, entry.title, entry.author, 'game', entry.stem.split('/')[0],
      entry.tags, entry.collection, entry.series]);
  assert.ok(row[5].includes(entry.source_url) && card.includes(entry.source_url));
  assert.ok(!/\]\([^)]*(?:\/Users\/|output\/research\/)/.test(card));
  const preparation = card.split('## Подготовка\n')[1].split('\n## ')[0];
  const blocks = [...preparation.matchAll(/```text\n([\s\S]*?)\n```/g)].map(m => m[1]);
  assert.equal(blocks.length, 1);
  for (const erratum of entry.errata) {
    assert.equal(parsed.codes[erratum.address], parseInt(erratum.resolved, 16));
    assert.ok(card.includes(`**${String(erratum.address).padStart(2, '0')}**`));
  }
  if (entry.printed_hex) {
    const expected = [...Buffer.from(entry.printed_hex, 'hex')];
    for (const fix of entry.errata) {
      assert.equal(expected[fix.address], parseInt(fix.printed, 16));
      expected[fix.address] = parseInt(fix.resolved, 16);
    }
    assert.deepEqual(parsed.codes, expected, 'only declared printed-code corrections');
  }
  programs.set(['bridge', 'bashe', 'ballistic'][i], { entry, listing, card, setup: blocks[0] });
}
function numeric(text) {
  const [mantissa, exponent = '0'] = text.trim().replace(',', '.').split(/\s+/);
  return Number(mantissa) * 10 ** Number(exponent);
}
const reg = (c, r) => numeric(c.readRegister(r));
function settle(c) {
  assert.ok(c.runUntilStable({ maxFrames: 10000, stableFrames: 8 }).stopped,
    'source program must reach the next stop');
  c.runFrames(12);
  assert.ok(!c.displayText().includes('ЕГГ0Г'), c.displayText());
  return reg(c, 'X');
}
function keyboard(c, text) {
  for (const t of text.trim().split(/\s+/)) {
    if (/^\d+(?:[.,]\d+)?$/.test(t)) c.inputNumber(t);
    else if (/^хП[0-9a-e]$/i.test(t)) c.press('П').press(keys[parseInt(t.slice(2), 16)]);
    else if (/^Пх[0-9a-e]$/i.test(t)) c.press('ИП').press(keys[parseInt(t.slice(2), 16)]);
    else if (t === 'F1/x') c.press('F').press('÷');
    else {
      c.press(t);
      if (t === 'С/П') settle(c);
    }
  }
}
function make(name, angleMode = 'rad', dirty = true, bytes = null) {
  const c = new MK61({ angleMode });
  c.loadProgram(bytes || programs.get(name).listing);
  if (dirty) for (const r of [...'0123456789abcde', 'X', 'Y', 'Z', 'T', 'X1']) {
    c.setRegister(r, 233 + r.charCodeAt(0));
  }
  c.pressSequence(['F', '/-/']);
  return c;
}
function setup(name, c = make(name)) { keyboard(c, programs.get(name).setup); return c; }
function enter(c, value) {
  c.inputNumber(String(Math.abs(value)), { clear: true });
  if (value < 0) c.press('/-/');
  c.press('С/П'); return settle(c);
}
function resume(c) { c.press('С/П'); return settle(c); }
function store(c, r, value) {
  c.inputNumber(String(Math.abs(value)), { clear: true });
  if (value < 0) c.press('/-/');
  c.press('П').press(keys[parseInt(r, 16)]);
}
function near(value, expected) {
  assert.ok(Math.abs(value - expected) < 0.00001 * Math.max(1, Math.abs(expected)),
    `${value} should be near ${expected}`);
}

function testBridge() {
  const p = programs.get('bridge');
  // Read the published card's diagram as an 11x11 geometric board.
  const diagram = p.card.split('```text\n')[1].split('\n```')[0].split('\n');
  assert.equal(diagram.length, 11);
  const cells = [null];
  for (let r = 0; r < 11; r++) for (let c = 0; c < 11; c++) {
    const token = diagram[r].padEnd(44, ' ').slice(c * 4, c * 4 + 4).trim();
    if (r % 2 !== c % 2) assert.equal(token, r % 2 ? '○' : '×');
    else if (r > 0 && r < 10 && c > 0 && c < 10) {
      assert.equal(Number(token), cells.length); cells.push([r, c]);
    } else assert.equal(token, '');
  }
  assert.equal(cells.length, 42);
  const reply = n => n % 10 === 0 ? n + 1 : n % 10 === 1 ? n - 1 : n % 10 <= 5 ? n + 4 : n - 4;
  function endpoints(n, calculator = true) {
    const [r, c] = cells[n];
    if (calculator) return r % 2 ? [(r - 1) / 2 * 5 + (c - 1) / 2, (r + 1) / 2 * 5 + (c - 1) / 2] :
      [r / 2 * 5 + (c - 2) / 2, r / 2 * 5 + c / 2];
    return r % 2 ? [(r - 1) / 2 * 6 + (c - 1) / 2, (r - 1) / 2 * 6 + (c + 1) / 2] :
      [(r - 2) / 2 * 6 + c / 2, r / 2 * 6 + c / 2];
  }
  const parent = new Uint8Array(32);
  function find(n) { while (parent[n] !== n) { parent[n] = parent[parent[n]]; n = parent[n]; } return n; }
  function union(a, b) { parent[find(a)] = find(b); }
  function reset(calculator = true) {
    for (let i = 0; i < 32; i++) parent[i] = i;
    if (calculator) for (let i = 0; i < 5; i++) { union(i, 30); union(25 + i, 31); }
    else for (let i = 0; i < 5; i++) { union(i * 6, 30); union(i * 6 + 5, 31); }
  }
  function won(owners, who) {
    reset(who === 2);
    for (let n = 1; n <= 41; n++) if (owners[n] === who) union(...endpoints(n, who === 2));
    return find(30) === find(31);
  }
  let replies = 0;
  for (const angle of ['rad', 'deg', 'grad']) {
    const c = setup('bridge', make('bridge', angle));
    c.press('В/О');
    for (const board of [1, 17, 999999]) for (let n = 2; n <= 41; n++) {
      assert.equal(enter(c, 100 * board + n), 100 * board + reply(n)); replies++;
    }
    // Legal games stop at actual geometric connectivity, not at an iteration count.
    for (const choice of ['first', 'last', 'middle']) {
      const owners = new Uint8Array(42); owners[1] = 2;
      let turns = 0;
      while (!won(owners, 2)) {
        const free = Array.from({ length: 40 }, (_, i) => i + 2).filter(n => owners[n] === 0);
        const n = choice === 'first' ? free[0] : choice === 'last' ? free.at(-1) : free[free.length >> 1];
        assert.ok(n && turns++ < 20);
        owners[n] = 1;
        assert.ok(!won(owners, 1), 'the human cannot complete a crossing before the forced reply');
        const m = enter(c, 100 + n) - 100;
        assert.equal(owners[m], 0, 'reply is an unused crossing');
        owners[m] = 2;
      }
    }
    setup('bridge', c); c.press('В/О'); assert.equal(enter(c, 102), 106, 'keyboard restart');
  }
  // Exhaust all 2^20 final choices, one calculator edge from each pairing.
  // Monotonicity and planar duality also cover every partial legal game.
  const pairs = [];
  for (let n = 2; n <= 41; n++) {
    assert.equal(reply(reply(n)), n);
    if (n < reply(n)) pairs.push([endpoints(n), endpoints(reply(n))]);
  }
  assert.equal(pairs.length, 20);
  for (let mask = 0; mask < 2 ** 20; mask++) {
    reset(); union(...endpoints(1));
    for (let i = 0; i < 20; i++) union(...pairs[i][(mask >>> i) & 1]);
    assert.equal(find(30), find(31), `calculator must cross in assignment ${mask}`);
  }
  console.log(`Bridg-it OK: ${replies} ROM replies, 9 legal games, restarts, card diagram and all 1048576 final pairings`);
}

function testBashe() {
  const optimal = (n, k) => (n - 1) % (k + 1) || 1;
  function start(c, p, k) {
    c.press('В/О'); assert.equal(enter(c, p), p); assert.equal(enter(c, k), 0);
    assert.equal(reg(c, '0'), p); assert.equal(reg(c, '1'), k + 1);
  }
  function play(c, p, k, first, humanStrategy) {
    start(c, p, k); let n = p, machineFirst = first === 'machine';
    for (let turns = 0; turns < p + 1; turns++) {
      let m;
      if (machineFirst) { machineFirst = false; m = resume(c); }
      else {
        const h = humanStrategy(n, k);
        assert.ok(h >= 1 && h <= Math.min(n, k));
        if (h === n) return 'human-lost'; // Physical last match ends the source game.
        n -= h; m = enter(c, h);
      }
      assert.equal(m, optimal(n, k), 'independent integer misere strategy');
      assert.ok(m >= 1 && m <= Math.min(n, k)); n -= m;
      assert.equal(reg(c, '0'), n, 'remaining matches after each response');
      if (n === 0) return 'machine-lost';
    }
    assert.fail('crossing the finite pile must terminate');
  }
  let positions = 0;
  for (let k = 1; k <= 7; k++) {
    const c = setup('bashe', make('bashe'));
    assert.equal(reg(c, '7'), 10000000); near(reg(c, '8'), 1 / 1.8);
    for (let n = 1; n <= 40; n++) {
      start(c, n, k);
      const m = resume(c);
      assert.equal(m, optimal(n, k), `n=${n}, k=${k}`);
      assert.equal(reg(c, '0'), n - m); positions++;
    }
  }
  let games = 0;
  for (const angle of ['rad', 'deg', 'grad']) {
    const c = setup('bashe', make('bashe', angle));
    for (const [p, k, first, human, expected] of [
      [23, 3, 'machine', () => 1, 'human-lost'],
      [5, 3, 'machine', optimal, 'machine-lost'],
      [5, 3, 'human', (n, k) => Math.min(3, n, k), 'human-lost'],
      [6, 3, 'human', optimal, 'machine-lost'],
      [35, 4, 'machine', () => 1, 'human-lost'],
      [21, 4, 'machine', optimal, 'machine-lost'],
    ]) {
      assert.equal(play(c, p, k, first, human), expected); games++;
      assert.equal(reg(c, '7'), 10000000); near(reg(c, '8'), 1 / 1.8);
    }
  }
  console.log(`Bashe OK: printed-keyboard setup, ${positions} positions against integer rules, ${games} complete games, both first players and outcomes`);
}

function testBallistic() {
  const position = c => [...'abcd'].map(r => reg(c, r));
  const oracle = (s, t) => [s.x1 + s.v1 * Math.cos(s.a1 * Math.PI / 180) * t,
    s.y1 + s.v1 * Math.sin(s.a1 * Math.PI / 180) * t - s.g * t * t / 2,
    s.x2 - s.v2 * Math.cos(s.a2 * Math.PI / 180) * t,
    s.y2 + s.v2 * Math.sin(s.a2 * Math.PI / 180) * t - s.g * t * t / 2];
  function compare(c, expected) { position(c).forEach((x, i) => near(x, expected[i])); }
  function prepare(c, s) {
    for (const [r, value] of Object.entries({'1':s.y1,'2':s.v1,'3':s.a1,'4':s.y2,'5':s.v2,'6':s.a2,
      '8':s.dt,'9':-s.g / 2,'a':s.x1,'c':s.x2})) store(c, r, value);
    c.press('В/О'); resume(c);
  }
  const c = setup('ballistic', make('ballistic', 'deg'));
  const example = { x1:0, y1:0, v1:20, a1:30, x2:100, y2:0, v2:25, a2:60, dt:1, g:10 };
  compare(c, oracle(example, 1)); assert.equal(reg(c, '0'), 1);
  for (const r of 'abcd') { keyboard(c, `Пх${r}`); near(reg(c, 'X'), oracle(example, 1)['abcd'.indexOf(r)]); }
  resume(c); compare(c, oracle(example, 2)); near(reg(c, 'b'), 0, 'first projectile lands at t=2');
  resume(c); assert.ok(reg(c, 'b') < 0, 'source does not stop at terrain or ground');
  setup('ballistic', c); compare(c, oracle(example, 1)); assert.equal(reg(c, '0'), 1, 'full keyboard restart');
  let steps = 0;
  for (const dt of [0.25, 0.5, 1, 2]) for (const g of [0, 1.62, 10]) {
    const s = {x1:-13,y1:5,v1:33,a1:20,x2:177,y2:17,v2:17,a2:40,dt,g};
    prepare(c, s);
    for (let step = 1; step <= 6; step++) {
      if (step > 1) resume(c);
      const t = dt * step; near(reg(c, '0'), t); compare(c, oracle(s, t)); steps++;
    }
  }
  // Both extreme angles are interpreted in the source's required degree mode.
  prepare(c, {...example, a1:90, a2:0});
  compare(c, oracle({...example, a1:90, a2:0}, 1));
  // The literal printed 13 (=division) cannot be accepted silently as correct.
  const literal = [...Buffer.from(programs.get('ballistic').entry.printed_hex, 'hex')];
  const bad = make('ballistic', 'deg', true, literal);
  const fractional = {...example, dt:0.5};
  prepare(bad, fractional);
  assert.ok(Math.abs(reg(bad, 'a') - oracle(fractional, .5)[0]) > 1);
  near(reg(bad, 'c'), oracle(fractional, .5)[2]);
  // At dt=1 the error is hidden; regression coverage must include other steps.
  prepare(bad, example); compare(bad, oracle(example, 1));
  console.log(`Ballistic OK: ${steps} paired trajectory steps, four time intervals, three gravities, coordinate reads, ground, restarts and printed-typo counterexample`);
}

const requested = process.argv.slice(2);
assert.ok(requested.every(x => ['bridge', 'bashe', 'ballistic'].includes(x)),
  'Usage: node tests/emulator/ochkov-kvant-games.cjs [bridge|bashe|ballistic]');
for (const [name, run] of [['bridge', testBridge], ['bashe', testBashe], ['ballistic', testBallistic]]) {
  if (requested.length === 0 || requested.includes(name)) run();
}
