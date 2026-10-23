#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');
const provenance = require('./fixtures/brp4-games.json');
const root = path.resolve(__dirname, '../..');
const entries = new Map(provenance.entries.map(e => [e.number, e]));
const registerKeys = [...'0123456789', ',', '/-/', 'ВП', 'Cx', 'В↑'];
const manifest = fs.readFileSync(path.join(root, 'games/manifest.tsv'), 'utf8')
  .trimEnd().split('\n').slice(1).map(line => line.split('\t'));

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
const reg = (calc, r) => number(calc.readRegister(r));
function settle(calc) {
  assert.ok(calc.runUntilStable({ maxFrames: 30000, stableFrames: 8 }).stopped,
    'a source program must reach its next stop');
  calc.runFrames(12);
  return number(calc.displayText());
}
function resume(calc) { calc.press('С/П'); return settle(calc); }
function input(calc, value) {
  calc.inputNumber(String(Math.abs(value)), { clear: true });
  if (value < 0) calc.press('/-/');
  return resume(calc);
}
function near(actual, expected, tolerance = 0.0001) {
  assert.ok(Math.abs(actual - expected) <= tolerance, `${actual} ≈ ${expected}`);
}
function make(n, transform = text => text) {
  const e = entries.get(n);
  const text = fs.readFileSync(path.join(root, `games/${e.stem}.txt`), 'utf8');
  const card = fs.readFileSync(path.join(root, `games/${e.stem}.md`), 'utf8');
  const parsed = parseProgramText(text);
  assert.deepEqual(parsed.diagnostics, []);
  const original = Buffer.from(e.original_hex, 'hex');
  assert.equal(crypto.createHash('sha256').update(original).digest('hex'), e.original_sha256);
  const expected = [...original];
  for (const patch of e.patches) {
    assert.equal(expected[patch.address], parseInt(patch.before, 16));
    expected[patch.address] = parseInt(patch.after, 16);
  }
  assert.deepEqual(parsed.codes, expected, 'listing must retain ROM bytes and declared patches');
  assert.equal(parsed.codes.length, e.length);
  assert.ok(e.length <= 105);
  const lines = text.trim().split('\n');
  assert.equal(lines.length, e.length);
  lines.forEach((line, i) => assert.ok(line.startsWith(`${String(i).padStart(2, '0')}\t`)));
  const row = manifest.find(row => row[0] === `${e.stem}.txt`);
  assert.ok(row, 'new listing must be indexed');
  assert.equal(row[1], `${e.stem}.md`);
  assert.equal(row[6], e.kind);
  assert.equal(row[9], 'brp4');
  const preparation = card.split('## Подготовка\n')[1].split('\n## ')[0];
  const setup = preparation.match(/```text\n([\s\S]*?)\n```/)[1];
  const calc = new MK61({ angleMode: [45, 47, 48].includes(n) ? 'deg' : 'rad' });
  calc.loadProgram(text);
  calc.pressSequence(['F', '/-/']);
  keyboard(calc, transform(setup)); // The user's printed setup, without register injection.
  settle(calc);
  assert.deepEqual(calc.readProgramCodes(e.length), parsed.codes);
  return calc;
}

assert.equal(entries.size, 15);
assert.deepEqual(provenance.entries.filter(e => e.patches.length).map(e => e.number), [47]);

// Complete reference games from the BRP-4 manual, with explicit player/PMK stops.
let calc = make(32);
assert.equal(reg(calc, 'X'), 18);
for (const [take, human, computer] of [[null, null, -16], [4, 12, -11], [4, 7, -6], [1, 5, -1]]) {
  if (take !== null) assert.equal(input(calc, take), human);
  assert.equal(resume(calc), computer);
}
assert.equal(reg(calc, '7'), 1);
calc = make(32, text => text.replace('В/О 4 С/П', 'БП 12 С/П'));
assert.equal(reg(calc, 'X'), -21, 'PMK can make the first move');

calc = make(33);
near(reg(calc, 'X'), 43.86);
near(resume(calc), -43.70);
for (const [a, b, nextA, nextB] of [[42,70,42,68], [10,36,6,10], [6,8,3,5], [3,4,1,2], [1,1,0,0]]) {
  keyboard(calc, `${a} хП7 ${b} хП8`);
  near(resume(calc), a + b / 100);
  near(resume(calc), -(nextA + nextB / 100));
  assert.equal(reg(calc, '7'), nextA === 0 && nextB === 0 ? a : nextA);
  assert.equal(reg(calc, '8'), nextA === 0 && nextB === 0 ? b : nextB);
  const legal = (x, y) => (a === x && b > y) || (b === y && a > x) ||
    (a - x > 0 && a - x === b - y);
  assert.ok(legal(nextA, nextB) || legal(nextB, nextA),
    'legal Wythoff move, allowing sorted heap order');
}

calc = make(34);
assert.equal(reg(calc, 'X'), 1293023);
assert.equal(resume(calc), -1093023);
for (const [human, computer] of [
  [[9,26,23], [9,26,19]], [[9,18,19], [1,18,19]], [[1,18,8], [1,9,8]],
  [[1,0,8], [1,0,1]], [[0,0,1], [0,0,0]],
]) {
  keyboard(calc, human.map((v, i) => `${v} хП${i + 7}`).join(' '));
  assert.equal(resume(calc), 1000000 + human[0] * 10000 + human[1] * 100 + human[2]);
  const shown = resume(calc);
  assert.equal(shown, -(1000000 + computer[0] * 10000 + computer[1] * 100 + computer[2]));
  assert.deepEqual([7,8,9].map(r => reg(calc, r)), computer);
  assert.equal(computer.reduce((a,b) => a ^ b, 0), 0, 'PMK leaves zero nim-sum');
  assert.equal(human.filter((v,i) => v !== computer[i]).length, 1);
}

for (const secret of [1, 10, 100]) {
  calc = make(35, () => `${secret % 3} хП7 ${secret % 5} хП8 ${secret % 7} хП9 В/О С/П`);
  assert.equal(reg(calc, 'X'), secret);
}

calc = make(36);
assert.equal(reg(calc, 'X'), 1099);
for (const [value, expected] of [[60,6099], [0,75], [null,6075], [65,6575], [0,68], [null,5.5555555]]) {
  assert.equal(value === null ? resume(calc) : input(calc, value), expected);
}
assert.equal(reg(calc, '4'), 4, 'asking for a hint does not count as an attempt');
for (const [guesses, grade] of [
  [[10,20,30,40,68],5.5555555], [[10,20,30,40,50,68],4.4444444],
  [[10,20,30,40,50,60,68],3.3333333], [[10,20,30,40,50,60,65,68],2.2222222],
]) {
  calc = make(36);
  for (const guess of guesses) input(calc, guess);
  assert.equal(reg(calc, 'X'), grade);
}

calc = make(38);
const tickets = [123105];
for (let i = 0; i < 5; i++) tickets.push(resume(calc));
assert.deepEqual(tickets, Array.from({ length: 51 }, (_, i) => 123105 + i).filter(n => {
  const digits = String(n).padStart(6, '0').split('').map(Number);
  return digits[0] + digits[1] + digits[2] === digits[3] + digits[4] + digits[5];
}));
assert.equal(resume(calc), 6);
calc = make(38, text => text.replace('42 хП8', '43 хП8'));
assert.equal(reg(calc, 'X'), 6);
calc = make(38, () => '1 хП4 хП7 43 хП8 В/О С/П');
assert.equal(reg(calc, 'X'), 0, 'empty result interval');

// Exercise all documented day seeds, including rejection of duplicate numbers.
for (const [n, count, maximum, reference] of [[39,5,36,[9,24,14,7,19]], [40,6,45,[11,29,17,9,23,15]]]) {
  for (let day = 1; day <= 31; day++) {
    calc = make(n, text => text.replace('В/О 4 С/П', `В/О ${day} С/П`));
    assert.equal(reg(calc, 'X'), count);
    const draw = Array.from({ length: count }, () => resume(calc));
    assert.equal(new Set(draw).size, count, `duplicate in day ${day}`);
    assert.ok(draw.every(x => Number.isInteger(x) && x >= 1 && x <= maximum));
    assert.deepEqual(draw, draw.map((_,i) => reg(calc, 9 - i)));
    if (day === 4) assert.deepEqual(draw, reference);
  }
}

calc = make(43);
assert.equal(reg(calc, 'X'), 1);
keyboard(calc, 'В/О 1,3 С/П'); assert.equal(settle(calc), 3);
keyboard(calc, 'В/О 3,9 С/П'); assert.equal(settle(calc), 0);
assert.equal(reg(calc, 'C'), 3);
assert.equal(reg(calc, 'D'), 1);

calc = make(44);
const lifeRegisters = ['A','9','8','7','6','5','4','3','2','1','0'];
const generations = [
  [1,1,0,0,0,0,0,1,1,0,1],
  [1,1,1,1,1,0,0,0,1,1,1],
  [0,1,1,1,1,0,1,0,1,0,1],
];
for (const [i, state] of generations.entries()) {
  if (i > 0) resume(calc);
  assert.deepEqual(lifeRegisters.map(r => reg(calc, r)), state);
  assert.equal(reg(calc, 'X'), state.reduce((a,b) => a + b, 0));
}
keyboard(calc, `0 ${lifeRegisters.map(r => `хП${r.toLowerCase()}`).join(' ')} В/О С/П`);
assert.equal(settle(calc), 0);
assert.deepEqual(lifeRegisters.map(r => reg(calc, r)), Array(11).fill(0));

calc = make(45);
assert.equal(reg(calc, 'X'), 3);
for (const [delta, angle, distance, speed] of [[1,0,2.8180926,1], [1,3,1.7128566,2], [1,45,2.0244867,3]]) {
  keyboard(calc, `${delta} В↑ ${angle} С/П`);
  near(settle(calc), distance);
  assert.equal(reg(calc, '9'), speed);
}
calc = make(45);
keyboard(calc, '2 В↑ 0 С/П'); settle(calc);
assert.equal(calc.displayText(), 'ЕГГ0Г');
near(resume(calc), 3.1415926);
assert.equal(reg(calc, '5'), 1);
assert.equal(resume(calc), 3, 'return to the ordinary input stop after the penalty');
keyboard(calc, '1 В↑ 0 С/П'); near(settle(calc), 2.8180926);

calc = make(47);
assert.equal(reg(calc, 'X'), 0);
keyboard(calc, '5 ПП 45 ПП 100 С/П');
near(settle(calc), 843.4673);
near(reg(calc, '4'), 843.4673);
near(reg(calc, '5'), 843.4673);
near(reg(calc, '8'), 717);
calc = make(47);
keyboard(calc, '0 хП6 хП7 0 ПП 0 ПП 0 С/П');
assert.equal(settle(calc), -2, 'numeric target-found marker from the card');
calc = make(47);
keyboard(calc, '3100 хП9 0 ПП 0 ПП 0 С/П');
assert.equal(settle(calc), -1, 'numeric emergency marker from the card');
assert.equal(reg(calc, 'A'), 88);

calc = make(48);
assert.equal(reg(calc, 'X'), 10700028);
for (const [value, expected] of [[86,5.5555555], [null,11900028], [98,101], [80,5.5555555]]) {
  assert.equal(value === null ? resume(calc) : input(calc, value), expected);
}
assert.equal(reg(calc, '4'), 2);
assert.equal(reg(calc, '7'), 7);
calc = make(48);
for (let shot = 1; shot <= 9; shot++) {
  const bearing = reg(calc, '8'), speed = reg(calc, '9');
  // Interception angle from the sine rule; independent of the ROM's next-bearing formula.
  const aim = bearing - Math.asin(speed / 75 * Math.sin(bearing * Math.PI / 180)) * 180 / Math.PI;
  assert.equal(input(calc, aim), 5.5555555);
  assert.equal(reg(calc, '4'), shot);
  assert.equal(reg(calc, '7'), 10 - shot);
  resume(calc);
}
assert.equal(input(calc, 0), 9, 'the tenth input ends the original game before another shot');
assert.equal(reg(calc, '7'), 0);
assert.equal(reg(calc, '4'), 9);

calc = make(50);
const fuel = 2000 - (500 / 5 + 12000 / 100);
const lift = 12000 * Math.exp(1 / 5) + 600;
near(reg(calc, '4'), fuel);
near(reg(calc, '5'), lift, 0.001);
near(reg(calc, '7'), 1000 * Math.exp((lift - 10000 - fuel) / 10000) + 2, 0.001);
near(reg(calc, '8'), 5 * Math.sqrt(500) - 10, 0.001);
near(reg(calc, 'A'), reg(calc, '8'));
assert.equal(reg(calc, 'B'), 1);
keyboard(calc, '1 хП4 С/П');
assert.equal(settle(calc), -1, 'fuel-end marker');
assert.equal(reg(calc, '0'), 0);
assert.equal(reg(calc, '4'), 0);
assert.ok(Number.isFinite(resume(calc)));
assert.equal(reg(calc, '8'), -10, 'unpowered drift follows the wind');

calc = make(51);
near(reg(calc, 'X'), 172278.95, 0.01);
near(reg(calc, 'A'), 2544.2105);
assert.equal(reg(calc, 'D'), 6000);
const capped = make(51, text => text.replace('В/О 100 С/П', 'В/О 150 С/П'));
for (const requested of [151, 999, -150]) {
  calc = make(51, text => text.replace('В/О 100 С/П',
    `В/О ${Math.abs(requested)} ${requested < 0 ? '/-/ ' : ''}С/П`));
  assert.deepEqual(['A','B','C','D'].map(r => calc.readRegister(r)),
    ['A','B','C','D'].map(r => capped.readRegister(r)));
}
calc = make(51, text => text.replace('3000 хПa', '20 хПa').replace('200000 хПb', '100 хПb')
  .replace('В/О 100 С/П', 'В/О 0 С/П'));
assert.equal(calc.displayText(), 'ЕГГ0Г');
assert.ok(reg(calc, 'B') < 0, 'impact preserves a negative crater depth');

console.log('BRP-4: 15 ROM listings, card keyboard setups, complete reference games, 62 lottery seeds, Life generations, simulator scenarios and original edge cases OK');
