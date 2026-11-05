#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');
const fixture = require('./fixtures/web-sources-games.json');
const root = path.resolve(__dirname, '../..');
const keys = [...'0123456789', ',', '/-/', 'ВП', 'Cx', 'В↑'];
const manifest = fs.readFileSync(root + '/games/manifest.tsv', 'utf8').trimEnd().split('\n')
  .slice(1).map(line => line.split('\t'));
const programs = new Map();

for (const e of fixture.entries) {
  const listing = fs.readFileSync(`${root}/games/${e.stem}.txt`, 'utf8');
  const card = fs.readFileSync(`${root}/games/${e.stem}.md`, 'utf8');
  const p = parseProgramText(listing);
  assert.deepEqual(p.diagnostics, [], e.stem);
  assert.equal(p.codes.length, e.length, e.stem);
  assert.equal(Buffer.from(p.codes).toString('hex').toUpperCase(), e.hex, e.stem);
  assert.equal(crypto.createHash('sha256').update(Buffer.from(p.codes)).digest('hex'), e.sha256);
  if (e.printed_hex) {
    const printed = [...Buffer.from(e.printed_hex, 'hex')];
    for (const patch of e.patches || []) {
      assert.equal(printed[patch.address], patch.printed);
      printed[patch.address] = patch.delivered;
      assert.ok(card.includes(`**${String(patch.address).padStart(2, '0')}`));
    }
    assert.deepEqual(p.codes, printed, 'only declared differences from the printed table');
  }
  const row = manifest.find(row => row[0] === e.stem + '.txt');
  assert.ok(row, e.stem);
  assert.deepEqual([row[2], row[3], row[6], row[8], row[9], row[10]],
    [e.title, e.author, e.kind, e.tags, e.collection, e.series]);
  if (e.source_url.startsWith('http')) {
    assert.ok(row[5].includes(e.source_url) && card.includes(e.source_url));
  }
  assert.ok(!/\]\([^)]*(?:\/Users\/|output\/research\/)/.test(card));
  const preparation = card.split('## Подготовка\n')[1].split('\n## ')[0];
  const blocks = [...preparation.matchAll(/```text\n([\s\S]*?)\n```/g)];
  assert.equal(blocks.length, 1, e.stem);
  programs.set(e.stem, { ...e, listing, card, codes: p.codes, setup: blocks[0][1] });
}
for (const old of Object.keys(fixture.deduplicated)) {
  assert.ok(!manifest.some(row => row[0] === old + '.txt'));
  for (const ext of ['txt', 'md']) assert.ok(!fs.existsSync(`${root}/games/${old}.${ext}`));
}

function numeric(text) {
  const [m, e = '0'] = text.trim().replace(',', '.').split(/\s+/);
  return Number(m) * 10 ** Number(e);
}
const reg = (c, r = 'X') => numeric(c.readRegister(r));
function stop(c, maxFrames = 8000) {
  assert.ok(c.runUntilStable({ maxFrames, stableFrames: 8 }).stopped,
    `did not stop at ${c.programCounter()}: ${c.displayText()}`);
  c.runFrames(12);
  return c.displayText().includes('ЕГГ0Г') ? 'ERR' : reg(c);
}
function resume(c) { c.press('С/П'); return stop(c); }
function input(c, n) {
  // Preserve the source's stack between stops, including the island budget.
  c.inputNumber(String(Math.abs(n)));
  if (n < 0) c.press('/-/');
  return resume(c);
}
function keyboard(c, text) {
  const operations = {
    'F1/x': ['F', '÷'], 'Fπ': ['F', '+'], КИНВ: ['K', 'Cx'],
    'К∨': ['K', '/-/'], 'К{x}': ['K', '8'], 'К⊕': ['K', 'ВП'],
  };
  const tokens = text.trim().split(/\s+/);
  for (let i = 0; i < tokens.length; i++) {
    const t = tokens[i];
    if (t === 'БП' && /^[0-9A][0-9]$/.test(tokens[i + 1] || '')) {
      c.press('БП');
      for (const digit of tokens[++i]) c.press(keys[parseInt(digit, 16)]);
    } else if (/^\d+(?:[.,]\d+)?$/.test(t)) c.inputNumber(t);
    else if (/^хП[0-9a-e]$/i.test(t)) c.press('П').press(keys[parseInt(t.slice(2), 16)]);
    else if (/^Пх[0-9a-e]$/i.test(t)) c.press('ИП').press(keys[parseInt(t.slice(2), 16)]);
    else if (operations[t]) c.pressSequence(operations[t]);
    else c.press(t);
  }
  return c;
}
function make(stem, { angle, extended, dirty = true, bytes } = {}) {
  const p = programs.get(stem);
  assert.ok(p, stem);
  const c = new MK61({ angleMode: angle || p.angle,
    extended: extended === undefined ? p.model !== 'MK-54' : extended });
  assert.deepEqual(c.loadProgram(bytes || p.listing).diagnostics, []);
  if (dirty) for (const r of [...(c.extended ? '0123456789abcde' : '0123456789abcd'), 'X', 'Y', 'Z', 'T', 'X1']) {
    c.setRegister(r, 223 + r.charCodeAt(0));
  }
  c.pressSequence(['F', '/-/']);
  return c;
}
function prepare(stem, options = {}) {
  const c = make(stem, options);
  keyboard(c, programs.get(stem).setup);
  return c;
}
function begin(c) { c.press('В/О'); return resume(c); }
function near(actual, expected, relative = 0.000002) {
  assert.ok(Number.isFinite(actual) && Math.abs(actual - expected) <= relative * Math.max(1, Math.abs(expected)),
    `${actual} should be near ${expected}`);
}

function testNewGames() {
  const guess = 'logic/ugaday-chislo-woodstock';
  for (const angle of ['rad', 'deg', 'grad']) {
    const c = prepare(guess, { angle });
    for (let game = 0; game < 2; game++) {
      assert.equal(begin(c), 0);
      const secret = reg(c, '0');
      assert.ok(Number.isInteger(secret) && secret >= 0 && secret <= 99);
      let lo = 0, hi = 99, won = false;
      // Blind binary search uses only public feedback, not the secret register.
      for (let turn = 0; turn < 7; turn++) {
        const n = (lo + hi) >> 1, answer = input(c, n);
        assert.equal(answer, Math.sign(n - secret));
        if (answer === 0) { won = true; break; }
        if (answer < 0) lo = n + 1; else hi = n - 1;
      }
      assert.ok(won);
      assert.equal(input(c, secret), 0, 'a zero answer does not silently start a new game');
    }
  }
  const stem = 'logic/igra-bashe-eboshe';
  const p = programs.get(stem);
  const original = [...Buffer.from(p.original_hex, 'hex')];
  for (let i = 0; i < original.length; i++) if (original[i] !== p.codes[i]) {
    assert.ok([14, 20].includes(i) || (i >= 21 && i <= 45), `undeclared repair at ${i}`);
  }
  const broken = prepare(stem, { bytes: original });
  assert.equal(begin(broken), 9);
  assert.deepEqual([3, 3, 1, 3].map(n => input(broken, n)), [6, 3, 2, 1000]);
  for (const moves of [[2, 3, 1, 3], [3, 3, 3], [1, 1, 1, 1, 1, 1, 1, 1, 1]]) {
    const c = prepare(stem);
    assert.equal(begin(c), 9);
    let heap = 9;
    for (const take of moves) {
      assert.ok(take >= 1 && take <= Math.min(3, heap));
      heap -= take;
      assert.equal(input(c, take), heap === 0 ? 1000 : heap);
      assert.equal(reg(c, '0'), heap || take, 'victory leaves the previous heap in R0');
    }
    assert.equal(heap, 0);
    assert.equal(begin(c), 9, 'keyboard restart after victory');
  }
  for (let heap = 1; heap <= 9; heap++) for (const take of [1, 2, 3]) {
    const c = prepare(stem); begin(c); c.setRegister('0', heap);
    assert.equal(input(c, take), take > heap ? 'ERR' : take === heap ? 1000 : heap - take);
    assert.equal(reg(c, '0'), take >= heap ? heap : heap - take);
  }
  for (const bad of [-3, 0, 0.5, 1.5, 2.5, 3.5, 4, 100]) {
    const c = prepare(stem); begin(c);
    assert.equal(input(c, bad), 'ERR');
    assert.equal(reg(c, '0'), 9, 'invalid move cannot consume stones');
    c.press('Cx'); c.inputNumber('2'); keyboard(c, 'БП 09');
    assert.equal(resume(c), 7, 'card recovery repeats the same turn');
  }
  console.log('New games OK: six blind number games, all Bashe heap/move pairs, complete games and error recovery');
}

function testIsland() {
  const stem = 'simulation/ostrov-kolkhoz';
  const c = prepare(stem);
  assert.equal(c.readRegister('3'), '-------,');
  assert.equal(c.readRegister('8'), '-0С0LЕL,');
  // An independent annual budget, crop-yield and pollution model.
  let cash = 50000, pollution = 0;
  const budget = (a,b) => assert.ok(Math.abs(a-b) < 0.02, `${a} should be within two cents of ${b}`);
  const decisions = [100, 40000, 900, 0, 2500, 1000];
  for (let year = 1; year <= 8; year++) {
    const [land, pay, crop, ads, cleaning, education] = decisions;
    pollution = Math.max(0, pollution + land - cleaning / 25);
    let available = cash + 37 * land;
    budget(input(c, land), available);
    available -= pay; budget(input(c, pay), available);
    available -= 25 * crop; budget(input(c, crop), available);
    available -= ads; budget(input(c, ads), available);
    available -= cleaning; budget(input(c, cleaning), available);
    available -= education;
    const people = Math.floor(pay / 180);
    const harvest = crop * (34 + Math.log(education + 1) - pollution / 500);
    const tourism = Math.min(cash, ads * (1.1 - pollution / 500));
    cash = available + harvest + tourism + people * 93;
    const result = input(c, education);
    near(reg(c, '0'), cash);
    near(reg(c, 'a'), pollution);
    assert.equal(reg(c, '1'), people);
    if (year < 8) assert.equal(result, 8 - year);
    else assert.equal(result, 'ERR', 'the source signals elections after eight complete years');
  }
  const d = prepare(stem); c.press('Cx');
  assert.equal(input(d, 200), 57400, '200 ha pay 7400, not 37*pi');
  assert.equal(input(d, 40000), 17400);
  assert.equal(input(d, 100), 14900);
  assert.equal(input(d, 1000), 13900);
  assert.equal(input(d, 500), 13400);
  assert.equal(reg(d, 'a'), 180);
  assert.equal(input(d, 1000), 7);
  near(reg(d, '0'), 37840.87548);
  console.log('Island OK: full eight-year budget model, pollution, first-year control figures and election signal');
}

function testParity() {
  for (const stem of ['logic/odd-even-1', 'logic/chet-nechet-3', 'logic/odd-even-who-can-outsmart-who']) {
    for (const win of [true, false]) {
      const c = prepare(stem); assert.equal(begin(c), 0);
      let score = 0;
      for (let turn = 0; turn < 10; turn++) {
        const predicted = reg(c, '9'); assert.ok([0, 1].includes(predicted));
        const bit = win ? 1 - predicted : predicted;
        assert.equal(input(c, bit), predicted);
        score += bit === predicted ? -1 : 1;
        const scale = stem.endsWith('who-can-outsmart-who') ? 1 : 1000;
        assert.equal(resume(c), scale * score);
        assert.equal(reg(c, '8'), score);
      }
      keyboard(c, programs.get(stem).setup); assert.equal(begin(c), 0);
    }
  }
  const c = prepare('logic/odd-even-2'); assert.equal(begin(c), 0);
  let score = 0, wins = 0;
  const bits = [0, 1, 1, 0, 1, 0, 0, 0, 1, 1, 0, 1];
  for (const [i, bit] of bits.entries()) {
    const value = input(c, bit) / 1000;
    assert.equal(Math.abs(value - score), 1, 'one score display and one point per input');
    if (value < score) wins++;
    score = value;
    assert.equal(reg(c, '6'), i + 1);
    assert.equal(reg(c, '5'), wins);
    assert.equal(score, i + 1 - 2 * wins);
  }
  console.log('Parity OK: learned predictions, both ten-point outcomes, three two-stop interfaces and the single-stop variant');
}

function testArithmetic() {
  const stem = 'educational/arithmetic';
  for (const [address, operation] of [[86, (a,b) => a+b], [88, (a,b) => a-b],
    [90, (a,b) => a*b], [92, (a,b) => Math.trunc(a/b)]]) {
    const c = prepare(stem); keyboard(c, `${address} хП8`); begin(c);
    for (let n = 0; n < 10; n++) {
      const a = reg(c), b = reg(c, 'Y');
      assert.ok(Number.isInteger(a) && Number.isInteger(b) && a > 0 && b > 0 && a <= 100 && b <= 100);
      c.press('↔'); // Exercise the card's way of reading the second operand.
      near(input(c, operation(a,b)), 5/9);
      const next = resume(c);
      if (n === 9) assert.equal(next, 'ERR');
    }
    assert.equal(resume(c), 5);
    assert.equal(reg(c, '5'), 10);
  }
  const c = prepare(stem); begin(c);
  for (let n = 0; n < 10; n++) {
    near(input(c, reg(c) + reg(c, 'Y') + 1), 1/9);
    resume(c);
  }
  assert.equal(resume(c), 1);
  console.log('Arithmetic OK: complete games for all four operations, keyboard operand reads and both endpoint grades');
}

function testMemory() {
  for (const stem of ['educational/memory-training', 'educational/trenirovka-pamyati']) {
    const c = prepare(stem); assert.equal(begin(c), 0);
    let score = 0, previous = null;
    for (let attempt = 0; attempt < 20; attempt++) {
      near(resume(c), 1.1111111);
      const values = [...'123'].map(r => reg(c, r));
      values.forEach(n => assert.ok(Number.isInteger(n) && n >= 0));
      if (previous) assert.deepEqual(values, previous, 'a failed exercise repeats its three numbers');
      const answer = [...values];
      if (attempt === 2) { answer[0]++; answer[1]--; previous = values; }
      else { score++; previous = null; }
      keyboard(c, `${answer[0]} В↑ ${answer[1]} В↑ ${answer[2]}`);
      const out = resume(c);
      if (attempt === 19) assert.equal(out, 'ERR');
      else { assert.equal(out, score); assert.equal(reg(c, 'Y'), 19 - attempt); }
      assert.equal(reg(c, '5'), score);
    }
    keyboard(c, programs.get(stem).setup); assert.equal(begin(c), 0);
  }
  console.log('Memory OK: both complete twenty-attempt games, non-cancelling errors, repetition and full keyboard restarts');
}

function testFox() {
  for (const n of [2, 3, 4]) for (const poli of n === 2 ? [false] : [false, true]) {
    const stem = n === 2 ? 'logic/okhota-na-lis-lordbss' : `logic/okhota-na-lis-${n}-${poli ? 'poli' : 'hramov'}`;
    const c = prepare(stem); assert.equal(begin(c), 0);
    const registers = n === 4 ? [...'89abcde0'] : [...'6789abcd'];
    const board = registers.map(r => Math.round(Math.sqrt(reg(c, r)) * 10))
      .map(n => [Math.floor(n/10), n%10]);
    board.forEach(([r,s]) => assert.ok(r >= 0 && r <= 9 && s >= 0 && s <= 9));
    if (n === 4) assert.equal(new Set(board.map(p => p.join(','))).size, 8);
    const caught = new Set();
    const expected = (r,s) => board.filter(([a,b], i) => (poli || !caught.has(i)) &&
      (a === r || b === s || Math.abs(a-r) === Math.abs(b-s))).length;
    let attempts = 0;
    for (const [r,s] of [[0,0],[0,9],[9,0],[9,9],[3,6]]) {
      if (board.some(([a,b]) => a === r && b === s)) continue;
      assert.equal(input(c, r+s/10), expected(r,s)); attempts++;
    }
    for (const [i, [r,s]] of board.entries()) {
      const value = input(c, r+s/10); attempts++; caught.add(i);
      if (n === 2) assert.ok(c.displayText().includes(`-${8-caught.size}`), c.displayText());
      else if (caught.size === 8) assert.equal(value, 'ERR');
      else { near(value, 1/9); assert.equal(reg(c, '2'), 8-caught.size); }
      if (caught.size === 1) {
        const [r0,s0] = [[0,0],[0,9],[9,0],[9,9]].find(p => !board.some(q => p.join() === q.join()));
        assert.equal(input(c, r0+s0/10), expected(r0,s0)); attempts++;
      }
    }
    assert.equal(reg(c, '5'), attempts);
  }
  console.log('Fox hunt OK: five complete games against independent board geometry, removal/Poli rules and attempt counts');
}

function testLanding() {
  for (const n of [1,2]) {
    const c = prepare(`simulation/posadka-na-lunu-${n}`);
    assert.equal(begin(c), 0); assert.equal(resume(c), 1000);
    let h = 1000, v = 150, fuel = 500, oxygen = 25;
    for (const proposal of [0, 0, 100, 100, 100, 999, ...(n === 2 ? [-10] : [0])]) {
      const u = Math.abs(proposal) <= fuel ? proposal : 0;
      fuel -= Math.abs(u); v += 1.62 - u/(2+fuel/1000); h -= v; oxygen--;
      near(input(c, proposal), h); near(reg(c, 'a'), h); near(reg(c, 'b'), v);
      near(reg(c, 'c'), fuel); if (n === 2) assert.equal(reg(c, '0'), oxygen);
    }
    assert.equal(begin(c), 0); resume(c);
    let landed = false;
    for (let t = 0; t < 10; t++) {
      const out = input(c, 0);
      if (reg(c, 'a') <= 0) {
        const grade = Math.floor(Math.abs(reg(c, 'a')) + Math.abs(reg(c, 'b')) + 1);
        near(out, (n === 2 ? Math.min(9, grade) : grade) + 1/9);
        landed = true; break;
      }
    }
    assert.ok(landed);
  }
  const c = prepare('simulation/posadka-na-lunu-2'); begin(c);
  keyboard(c, '1.2 хПa 0 хПb'); assert.equal(resume(c), 1.2);
  input(c, 3); near(input(c, 3), 1.1111111);
  console.log('Landing OK: analytical height/velocity/fuel model, oxygen, excess and negative thrust, crashes and a soft landing');
}

function testTank() {
  for (const mode of ['matematicheskiy', 'realnyy']) {
    const stem = `strategy/tankovaya-bataliya-${mode}-boy`;
    const address = mode === 'realnyy' ? 'A4' : '98';
    const c = prepare(stem); keyboard(c, '0 хПd 300 хПb'); keyboard(c, `БП ${address}`);
    assert.equal(resume(c), 1);
    keyboard(c, '45 В↑ 0'); near(resume(c), 1/9);
    near(reg(c, 'a'), 2500/9.81);
    assert.equal(resume(c), 2); assert.equal(reg(c, '1'), 19); assert.equal(reg(c, '7'), 4000);
    keyboard(c, '0 В↑ 5'); assert.equal(resume(c), 1);
    assert.equal(reg(c, '8'), 3975); assert.equal(reg(c, '2'), 20); assert.equal(reg(c, 'b'), 295);
    // A full one-hit game with known weather, and source-prescribed stack reset.
    c.pressSequence(['В/О', 'ПП']); keyboard(c, programs.get(stem).setup);
    keyboard(c, '0 хПd 254 хПb 1 хП4'); keyboard(c, `БП ${address}`);
    assert.equal(resume(c), 1);
    keyboard(c, '45 В↑ 0'); near(resume(c), Math.PI);
    assert.equal(resume(c), 1000000);
  }
  const stem = 'strategy/tank-battle', c = prepare(stem);
  keyboard(c, 'БП A1'); assert.equal(resume(c), 300);
  const weather = reg(c, '0');
  near(input(c, 45), 1/9);
  near(reg(c, 'a'), 2500/9.81/(1+weather/1000)**2);
  resume(c); assert.equal(reg(c, '1'), 19); assert.equal(reg(c, '4'), 1);
  console.log('Tanks OK: correct entry addresses, ballistic ranges, mines/fuel, alternating turns, stack reset and complete wins');
}

function testKaufsten() {
  for (const mode of ['heavy', 'light']) {
    const stem = 'logic/kaufsten-' + mode, c = prepare(stem);
    assert.equal(c.readRegister('d'), '-1,');
    c.press('Cx'); assert.equal(begin(c), 0);
    const price = mode === 'light' ? 10 : 12, initial = mode === 'light' ? 15000 : 20000;
    assert.equal(input(c, 10), 1010);
    assert.equal(reg(c, '9'), initial - price*10);
    keyboard(c, '0 В↑ 3000 В↑ 100');
    let result = resume(c), events = 0;
    while ((Math.abs(result - Math.PI) < 0.000001 || Math.abs(result - reg(c, '6')) < 0.000001) && events++ < 10) {
      result = resume(c);
    }
    assert.notEqual(result, 'ERR');
    assert.equal(reg(c, '4'), 1); assert.ok(reg(c, '7') > 0);
    assert.ok(reg(c, '8') > 0 && reg(c, '9') > 0);
    // Buying and selling have different published prices.
    const land = reg(c, '8'), grain = reg(c, '9');
    assert.equal(input(c, -10), land-10);
    assert.equal(reg(c, '9'), grain + 10*price/(mode === 'light' ? 2 : 3));
  }
  console.log('Kaufsten OK: both published difficulties, special address construction, trade accounting and complete annual event processing');
}

function testCalendar() {
  const c = prepare('utilities/vechnyy-kalendar');
  // Preserve and document the published utility; it is not a correct Gregorian
  // algorithm. Do not silently turn a source audit into a new calendar port.
  for (const [d,m,y,sourceWeekday,weekday] of [[1,1,2000,5,6],[5,11,2026,3,4]]) {
    const actualWeekday = new Date(Date.UTC(y,m-1,d)).getUTCDay();
    assert.equal(actualWeekday, weekday);
    keyboard(c, `${d} В↑ ${m} В↑ ${y} В/О`); assert.equal(resume(c), sourceWeekday);
    assert.notEqual(sourceWeekday, actualWeekday, 'known source defect, stated in the card');
    keyboard(c, `${d+1} В↑ ${m} В↑ ${y}`); assert.equal(resume(c), (sourceWeekday+1)%7);
    assert.equal(reg(c, 'Y'), 1);
  }
  console.log('Calendar audit OK: both documented Gregorian counterexamples and consecutive-day intervals');
}

function testCompatibility() {
  const stem = 'logic/odd-even-1';
  const basic = prepare(stem); assert.equal(begin(basic), 0);
  assert.equal(reg(basic, '8'), 0);
  // Exercise the actual clearing opcode without running an incompatible game's
  // full loop: a dirty RE can otherwise make the MK-61 overwrite program cells.
  for (const extended of [false, true]) {
    const c = new MK61({ extended });
    c.loadProgram('1 3 хП0 Cx КхПE С/П');
    c.setRegister('2', 222); c.setRegister('d', 333);
    if (extended) c.setRegister('e', 2);
    c.pressSequence(['F', '/-/']); begin(c);
    assert.equal(reg(c, '2'), extended ? 0 : 222);
    assert.equal(reg(c, 'd'), extended ? 333 : 0);
  }
  for (const p of programs.values()) if (p.model === 'MK-54') {
    assert.ok(p.card.includes('несовместим') && p.card.includes('МК-54'), p.stem);
  }
  const c = prepare('logic/byki-i-korovy'); assert.equal(begin(c), 0);
  const secret = [...'1234'].map(r => Math.round(reg(c, r)*10));
  const guess = [1,2,3,4];
  const bulls = guess.filter((n,i) => n === secret[i]).length;
  const pairs = guess.reduce((sum,n) => sum+secret.filter(m => m === n).length, 0);
  assert.equal(input(c, 1234), 10*bulls+pairs-bulls, 'published pairwise feedback with repetitions');
  near(input(c, Number(secret.join(''))), 2/0.99);
  keyboard(c, '0.31415926'); assert.equal(begin(c), 4, 'source restart keeps the previous four-bull count');
  const fox = prepare('logic/okhota-na-lis-1-danilov-slavin'); assert.equal(begin(fox), 0);
  const cells = [...'12345'].map(r => Math.round(reg(fox,r)*10));
  assert.equal(cells.length, 5);
  for (const cell of cells) {
    near(input(fox,cell), 1/9);
    near(reg(fox,'c')*10,cell);
  }
  console.log('Compatibility OK: actual MK-54/MK-61 divergence and original repeated-digit feedback');
}

function testQueens() {
  const c = prepare('logic/vosem-ferzey-tarasov'); c.pressSequence(['В/О', 'С/П']);
  assert.equal(stop(c, 30000), 0);
  const solution = [...'12345678'].map(r => reg(c, r));
  assert.deepEqual(solution, [8,4,1,3,6,2,7,5]);
  for (let i = 0; i < 8; i++) {
    assert.ok(Number.isInteger(solution[i]) && solution[i] >= 1 && solution[i] <= 8);
    for (let j = 0; j < i; j++) {
      assert.notEqual(solution[i], solution[j]);
      assert.notEqual(Math.abs(solution[i]-solution[j]), i-j, 'queens must not share diagonals');
    }
  }
  console.log('Eight Queens OK: complete original 67-step ROM search and independent attack checks');
}

function testFlight() {
  const stem = 'simulation/uchebnyy-polyot-modernized';
  const c = prepare(stem); c.pressSequence(['В/О', 'С/П']);
  let distance = 0, height = 0;
  for (const [mode, clock, angle] of [[11,3,30], [12,5,30], [10,7,0]]) {
    c.angleMode = mode;
    let frames = 0;
    while (reg(c,'e') < clock && frames++ < 1200) {
      c.runFrames(3);
      assert.ok(!c.displayText().includes('ЕГГ0Г'));
    }
    assert.equal(reg(c,'e'), clock, 'the published stopwatch is advancing');
    assert.equal(reg(c,'b'), angle, 'Г climbs, ГРД holds and Р lowers the trajectory');
    assert.ok(reg(c,'c') > distance && reg(c,'d') > height);
    distance = reg(c,'c'); height = reg(c,'d');
  }
  c.press('С/П'); stop(c);
  const paused = reg(c,'e'); c.runFrames(20);
  assert.equal(reg(c,'e'), paused, 'card pause key stops the real-time loop');
  assert.deepEqual(c.readProgramCodes(105), programs.get(stem).codes);
  console.log('Flight OK: dirty-register card setup, three physical switch positions, stopwatch and pause');
}

const groups = { new: testNewGames, island: testIsland, parity: testParity, arithmetic: testArithmetic,
  memory: testMemory, fox: testFox, landing: testLanding, tank: testTank,
  kaufsten: testKaufsten, calendar: testCalendar, compatibility: testCompatibility,
  flight: testFlight, queens: testQueens };
const selected = process.argv.slice(2);
if (selected.length === 1 && selected[0] === '--source-only') {
  console.log(`Source checks OK: ${programs.size} listings, card setups, provenance and duplicate removal`);
  process.exit(0);
}
for (const name of selected.length ? selected : Object.keys(groups)) {
  assert.ok(groups[name], `unknown test group: ${name}`);
  groups[name]();
}
