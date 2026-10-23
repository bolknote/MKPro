#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');

const registerKeys = [...'0123456789', ',', '/-/', 'ВП', 'Cx', 'В↑'];
function keyboard(calc, text) {
  for (const token of text.trim().split(/\s+/)) {
    if (/^\d+(?:[.,]\d+)?$/.test(token)) calc.inputNumber(token);
    else if (/^хП[0-9a-e]$/i.test(token)) {
      calc.press('П').press(registerKeys[parseInt(token.slice(2), 16)]);
    } else calc.press(token);
  }
}

function number(text) {
  const [mantissa, exponent = '0'] = text.trim().replace(',', '.').split(/\s+/);
  return Number(mantissa) * 10 ** Number(exponent);
}
const register = (calc, name) => number(calc.readRegister(name));
const screen = calc => register(calc, 'X');
function settle(calc) {
  assert.ok(calc.runUntilStable({ maxFrames: 5000, stableFrames: 8 }).stopped,
    'the delivered program must reach a stop');
  calc.runFrames(12); // Finish ROM error handling before reading ЕГГ0Г.
  return screen(calc);
}
function resume(calc) {
  calc.press('С/П');
  return settle(calc);
}
function input(calc, value) {
  calc.inputNumber(String(Math.abs(value)), { clear: true });
  if (value < 0) calc.press('/-/');
  return resume(calc);
}
function recover(calc) {
  calc.pressSequence(['Cx', 'В/О', 'С/П']);
  return settle(calc);
}
const snapshot = (calc, names) => names.map(name => calc.readRegister(name));

function game(relative) {
  const base = path.resolve(__dirname, '../../games', relative);
  const listing = fs.readFileSync(`${base}.txt`, 'utf8');
  const card = fs.readFileSync(`${base}.md`, 'utf8');
  const parsed = parseProgramText(listing);
  assert.deepEqual(parsed.diagnostics, []);
  // Count physical lines too: the parser ignores out-of-range addresses.
  const cells = listing.trim().split(/\n/);
  const count = Number(card.match(/МК-61, (\d+) команд/)[1]);
  assert.ok(count <= 105);
  assert.equal(cells.length, count);
  assert.equal(parsed.codes.length, count);
  cells.forEach((line, index) => assert.ok(line.startsWith(
    `${index < 100 ? String(index).padStart(2, '0') : `A${index - 100}`}\t`)));
  const preparation = card.split('## Подготовка\n')[1].split(/\n## /)[0];
  const blocks = [...preparation.matchAll(/```text\n([\s\S]*?)\n```/g)]
    .map(match => match[1]);
  return { card, codes: parsed.codes, blocks };
}

function prepare(data, middle = '', angleMode = 'rad') {
  const calc = new MK61({ angleMode });
  assert.deepEqual(calc.loadProgram(data.codes).diagnostics, []);
  calc.pressSequence(['F', '/-/']);
  // No host register injection: use the setup and launch printed in the card.
  keyboard(calc, data.blocks[0]);
  if (middle) keyboard(calc, middle);
  keyboard(calc, data.blocks.at(-1));
  settle(calc);
  assert.deepEqual(calc.readProgramCodes(data.codes.length), data.codes);
  return calc;
}

const only = process.argv[2];
assert.ok(!only || ['solitaire', 'hamurabi', 'trek'].includes(only));
const solitaire = game('logic/solitaire-classen-1983');
const hamurabi = game('simulation/hamurabi-poloczek-1983');
const trek = game('simulation/star-trek-classen-1983');
assert.equal(solitaire.blocks.length, 2);
assert.equal(hamurabi.blocks.length, 2);
assert.equal(trek.blocks.length, 3);

if (!only || only === 'solitaire') {
const rows = calc => Array.from({ length: 7 }, (_, i) => register(calc, i + 1));
function move(calc, coordinate, direction) {
  input(calc, coordinate);
  return input(calc, direction);
}

// Invalid directions, occupied targets, empty sources, corners and both
// coordinate boundaries must preserve the whole board, including the counter.
for (const [coordinate, direction] of [
  [2.3, 0.1], [4.4, 0.1], [4.2, -0.1], [1.4, -1], [1.1, 0.1],
  [3.7, 0.1], [2.45, 1], [2.4, 0], [2.4, 0.2], [2.4, 1.1],
  [1.898, 1], [2.8959569, 1], [3.2, -0.1], [3.6, 0.1],
  [3, 0.1], [2.4000001, 1], [0.4, 1], [8.4, -1],
]) {
  const calc = prepare(solitaire);
  const before = rows(calc);
  move(calc, coordinate, direction);
  const nativeError = calc.displayText() === 'ЕГГ0Г';
  assert.ok(nativeError || screen(calc) === -1, `invalid move ${coordinate}, ${direction}`);
  assert.deepEqual(rows(calc), before, 'rejected move must not remove any peg');
  assert.equal(register(calc, 0), 31);
  assert.equal(nativeError ? recover(calc) : resume(calc), 31);
  assert.equal(move(calc, 2.4, 1), 30, 'a legal move must work after recovery');
}

{
  const calc = prepare(solitaire);
  assert.equal(move(calc, 2.4, 1), 30);
  const before = rows(calc);
  for (let retry = 0; retry < 2; retry++) {
    assert.equal(move(calc, 2.4, 1), -1);
    assert.deepEqual(rows(calc), before, 'retries must preserve an ongoing game');
    assert.equal(resume(calc), 30);
  }
  assert.equal(move(calc, 3.2, 0.1), 29);
}

// Replay the actual card against an independent, unpacked board model.
{
  const calc = prepare(solitaire);
  const solution = solitaire.card.split('## Контрольное решение\n')[1]
    .match(/```text\n([\s\S]*?)\n```/)[1].trim().split(/\s+/)
    .map(token => Number(token.replace(',', '.')));
  assert.equal(solution.length, 62);
  const board = Array.from({ length: 7 }, (_, r) => Array.from({ length: 7 }, (_, c) =>
    (r < 2 || r > 4) && (c < 2 || c > 4) ? 9 : r === 3 && c === 3 ? 0 : 1));
  for (let i = 0; i < solution.length; i += 2) {
    const [coordinate, direction] = solution.slice(i, i + 2);
    const r = Math.trunc(coordinate) - 1;
    const c = Math.round((coordinate % 1) * 10) - 1;
    const dr = Math.abs(direction) === 1 ? Math.sign(direction) : 0;
    const dc = dr ? 0 : Math.sign(direction);
    assert.deepEqual([board[r][c], board[r + dr][c + dc], board[r + 2 * dr][c + 2 * dc]], [1, 1, 0]);
    board[r][c] = board[r + dr][c + dc] = 0;
    board[r + 2 * dr][c + 2 * dc] = 1;
    assert.equal(move(calc, coordinate, direction), 30 - i / 2);
    const packed = board.map(row => 90000000 + row.reduce((sum, cell, column) =>
      sum + cell * 10 ** column, 0));
    assert.deepEqual(rows(calc), packed, `board after move ${i / 2 + 1}`);
  }
  assert.equal(board.flat().filter(cell => cell === 1).length, 1);
  assert.equal(board[3][3], 1);
  assert.equal(resume(calc), 0, 'completed game must remain completed');
  assert.deepEqual(calc.readProgramCodes(solitaire.codes.length), solitaire.codes);
}
console.log('Solitaire: atomic rejection, recovery, independent board model and complete 31-move solution OK.');

}

// The integer oracle rounds ordinary multiplication to eight significant
// digits, then extracts the fraction. It does not execute listing addresses.
const scale = 10000000n;
function randomWord(q) {
  let product = q * 997n;
  const excess = product.toString().length - 8;
  if (excess > 0) {
    const unit = 10n ** BigInt(excess);
    product = (product + unit / 2n) / unit * unit;
  }
  return product % scale;
}
function events(q) {
  let work = q;
  const digits = Array.from({ length: 5 }, () => {
    work *= 6n;
    const digit = Number(work / scale);
    work %= scale;
    return digit;
  });
  return { digits, work };
}
const close = (actual, expected, label) => assert.ok(
  Number.isFinite(actual) && Math.abs(actual - expected) <= Math.max(0.002, Math.abs(expected) * 3e-7),
  `${label}: ${actual} != ${expected}`);

if (!only || only === 'hamurabi') {
const kingdomState = calc => snapshot(calc, ['1', '2', '4', '6', '7', '8']);
function year(calc, trade, fed, planted) {
  assert.equal(resume(calc), register(calc, 1));
  assert.equal(resume(calc), register(calc, 4));
  input(calc, trade);
  input(calc, fed);
  if (calc.displayText() !== 'ЕГГ0Г') input(calc, planted);
  return screen(calc);
}
for (const [trade, fed, planted] of [
  [-1001, 100, 0], [1000, 100, 1], [0, -1, 0], [0, 101, 0],
  [0, 100, -1], [0, 100, 1001], [100, 100, 1001], [30, 100, 1000],
  [0, 50, 501],
]) {
  const calc = prepare(hamurabi);
  const before = kingdomState(calc);
  year(calc, trade, fed, planted);
  assert.equal(calc.displayText(), 'ЕГГ0Г', `invalid orders ${trade}, ${fed}, ${planted}`);
  assert.deepEqual(kingdomState(calc), before);
  assert.equal(recover(calc), 100);
  assert.equal(year(calc, 0, 100, 1000), 101);
  assert.equal(register(calc, 0), 5, 'invalid input cannot advance annual events');
}

function annualModel(state, trade, fed, planted) {
  const q = randomWord(state.q);
  const { digits: d, work } = events(q);
  let population = Math.trunc(fed) + d[1];
  if (d[2] === 0) population = Math.trunc(population / 2);
  return {
    q, work, digits: d, population, land: state.land + trade,
    grain: (state.grain - trade * state.price - Math.trunc(fed) * 20 - planted / 2 +
      planted * (d[0] + 1)) * (20 - d[3]) / 20,
    price: 20 + d[4],
  };
}
let years = 0;
const sample = [...hamurabi.card.split('Контрольная партия')[1]
  .matchAll(/^\| (\d+) \| ([−-]?\d+) \| (\d+) \| (\d+) \| (\d+) \|$/gm)]
  .map(match => match.slice(1).map(token => Number(token.replace('−', '-'))));
assert.equal(sample.length, 10);
for (const angleMode of ['rad', 'deg', 'grad']) {
  const calc = prepare(hamurabi, '', angleMode);
  let state = { q: 3718250n, population: 100, grain: 2800, land: 1000, price: 23 };
  for (const [index, trade, fed, planted, harvest] of sample) {
    assert.equal(screen(calc), fed);
    const next = annualModel(state, trade, fed, planted);
    assert.equal(next.digits[0] + 1, harvest);
    year(calc, trade, fed, planted);
    close(register(calc, 1), next.grain, 'grain');
    assert.equal(register(calc, 4), next.land);
    assert.equal(register(calc, 6), next.population);
    assert.equal(register(calc, 7), next.price);
    assert.equal(Math.round(register(calc, 8) * Number(scale)), Number(next.q));
    assert.equal(Math.round(register(calc, 9) * Number(scale)), Number(next.work));
    assert.equal(screen(calc), index === 10 ? 0 : next.population);
    assert.equal(register(calc, 2), Math.max(10 - index, 1));
    state = next;
    years++;
  }
  assert.equal(state.population, 117);
  assert.equal(state.land, 865);
  close(register(calc, 1), 2505.87135, 'card final grain');
  calc.press('П→X').press('6');
  assert.equal(resume(calc), 0, 'terminal result survives inspection');
  assert.deepEqual(calc.readProgramCodes(hamurabi.codes.length), hamurabi.codes);
}

// Choose seeds with and without plague for the SAME harvest. This would fail
// when a low harvest automatically triggered plague in the former adaptation.
const selected = new Map();
for (let value = 100001; value < 110000 && selected.size < 4; value++) {
  const q = BigInt(value) * 10n;
  const d = events(randomWord(q)).digits;
  const key = `${d[0]}:${d[2] === 0}`;
  if ([0, 4].includes(d[0]) && !selected.has(key)) selected.set(key, q);
}
assert.equal(selected.size, 4);
for (const q of selected.values()) {
  const calc = prepare(hamurabi, `${Number(q) / Number(scale)} хП8`);
  const next = annualModel({ q, grain: 2800, land: 1000, price: 23 }, 0, 100, 1000);
  year(calc, 0, 100, 1000);
  assert.equal(register(calc, 6), next.population);
  assert.equal(register(calc, 0), next.digits[0] + 1);
  assert.equal(register(calc, 7), next.price);
  close(register(calc, 1), next.grain, 'independent rat losses');
  years++;
}
for (const fed of [49, 50]) {
  const calc = prepare(hamurabi);
  year(calc, 0, fed, 0);
  assert.equal(screen(calc), fed + 1, 'famine changes population rather than declaring an invented instant defeat');
}
console.log(`Hamurabi: ${years} modeled years, three complete card games, separate event digits, plague independence, variable rats/prices, worker and resource limits, recovery OK.`);

}

if (!only || only === 'trek') {
const levels = [...trek.card.matchAll(/^\| ([1-4]) \| [^|]+ \| (\d+) \| (\d+) \|$/gm)]
  .map(match => match.slice(1).map(Number));
assert.equal(levels.length, 4);
function enterprise(level = 1, seed = '0,371825') {
  const [, damage, enemies] = levels[level - 1];
  return prepare(trek, `${damage} хП1 ${enemies} хП3 ${seed} хПa`);
}
const fleetState = calc => snapshot(calc, ['0', '1', '2', '3', '4', '5', '6', 'a']);
{
  const calc = enterprise();
  const offsets = new Set();
  let q = 3718250n;
  for (let scan = 0; scan < 12; scan++) {
    q = randomWord(q);
    const offset = Number(q * 3n / scale) - 1;
    const before = snapshot(calc, ['0', '1', '2', '3', '4', '5', '6']);
    assert.equal(input(calc, 0), 34 + offset);
    offsets.add(offset);
    assert.deepEqual(snapshot(calc, ['0', '1', '2', '3', '4', '5', '6']), before);
    assert.equal(Math.round(register(calc, 'a') * Number(scale)), Number(q));
    assert.equal(resume(calc), 3);
  }
  assert.deepEqual([...offsets].sort(), [-1, 0, 1], 'all three aiming errors must actually occur');
}
for (const [command, torpedoes, enemies, energy, shields] of [
  [0.335, 9, 3, 3000, 1350], [0.345, 9, 2, 3000, 1350],
  [-989, 10, 3, 2011, 1350], [-990, 10, 2, 2010, 1350],
  [400, 10, 3, 2600, 1900],
]) {
  const calc = enterprise();
  input(calc, command);
  assert.deepEqual(['2', '3', '5', '6'].map(name => register(calc, name)),
    [torpedoes, enemies, energy, shields]);
  if (enemies === 2) {
    const q1 = randomWord(3718250n), q2 = randomWord(q1);
    assert.equal(register(calc, 0), Number(q1 * 100n / scale));
    close(register(calc, 4), 500 * (1 + 3 * Number(q2) / Number(scale)), 'new distance');
  }
}
for (const command of [2601, -2601]) {
  const calc = enterprise();
  input(calc, 400);
  const before = fleetState(calc);
  input(calc, command);
  assert.equal(calc.displayText(), 'ЕГГ0Г');
  assert.deepEqual(fleetState(calc), before, 'overspending preserves resources and random state');
  assert.equal(recover(calc), 3);
  input(calc, 1);
  assert.equal(register(calc, 5), 2599);
}
{
  const calc = enterprise();
  keyboard(calc, '0 хП2');
  const before = fleetState(calc);
  input(calc, 0.345);
  assert.equal(calc.displayText(), 'ЕГГ0Г');
  assert.deepEqual(fleetState(calc), before);
  assert.equal(recover(calc), 3);
  assert.equal(input(calc, -990), 2);
}
for (const angle of [0, 99]) {
  const calc = enterprise();
  keyboard(calc, `${angle} хП0`);
  assert.equal(input(calc, (angle + 0.5) / 100), 2);
}
{
  const calc = enterprise();
  input(calc, 3000);
  assert.equal(register(calc, 5), 0);
  assert.equal(input(calc, 0.345), 2);
  assert.equal(register(calc, 5), 0, 'a kill cannot conjure up energy');
}
{
  const calc = enterprise();
  keyboard(calc, '450 хП6');
  assert.equal(input(calc, 0.335), 3);
  assert.equal(register(calc, 6), 300);
  assert.equal(input(calc, 0.335), -3);
  calc.press('П→X').press('5');
  assert.equal(resume(calc), -3);
}

// Play every level with PUBLIC scan displays only. Repeated observations
// intersect [reading-1, reading+1]; no read of R0 chooses a shot.
for (const [level, damage, enemies] of levels) {
  const calc = enterprise(level);
  let scans = 0;
  for (let ship = 0; ship < enemies; ship++) {
    let lower = 0, upper = 99;
    for (let attempt = 0; lower !== upper && attempt < 60; attempt++) {
      const reading = input(calc, 0);
      lower = Math.max(lower, reading - 1);
      upper = Math.min(upper, reading + 1);
      assert.equal(resume(calc), enemies - ship);
      scans++;
    }
    assert.equal(lower, upper, 'public observations must determine a bearing');
    if (ship < enemies - 1) {
      const deficit = 300 + damage - register(calc, 6);
      if (deficit > 0) input(calc, deficit);
    }
    assert.equal(input(calc, (lower + 0.5) / 100), ship === enemies - 1 ? 0 : enemies - ship - 1);
  }
  assert.equal(register(calc, 2), 10 - enemies);
  assert.ok(scans >= enemies * 2, 'the scanner must not silently become exact');
  calc.press('П→X').press('5');
  assert.equal(resume(calc), 0);
  assert.deepEqual(calc.readProgramCodes(trek.codes.length), trek.codes);
}
console.log('Star Trek: reproducible software RNG, all three scan errors, public-information victories on all four levels, atomic spending, empty launchers and terminal boundaries OK.');

}
