#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');

const base = path.resolve(__dirname,
  '../../games/sports/ralli-na-regulyarnost-romanovskis-1987');
const listing = fs.readFileSync(`${base}.txt`, 'utf8');
const card = fs.readFileSync(`${base}.md`, 'utf8');
const parsed = parseProgramText(listing);
assert.deepEqual(parsed.diagnostics, []);
assert.equal(parsed.codes.length, 100);

const preparation = card.split('## Подготовка\n')[1].split('## Прохождение участка\n')[0];
const setup = [...preparation.matchAll(/```text\n([\s\S]*?)\n```/g)].map(match => match[1]);
assert.equal(setup.length, 2, 'the card must document the entire route and launch');
const registerKeys = { А: ',', В: '/-/', С: 'ВП', Д: 'Cx', Е: 'В↑' };

function keyboard(calc, text) {
  for (const token of text.trim().split(/\s+/)) {
    if (/^\d+(?:[.,]\d+)?$/.test(token)) calc.inputNumber(token);
    else if (/^хП[0-9АВСДЕ]$/.test(token)) {
      const name = token.slice(2);
      calc.press('П').press(registerKeys[name] || name);
    } else calc.press(token);
  }
}

function numeric(text) {
  const [mantissa, exponent = '0'] = text.trim().replace(',', '.').split(/\s+/);
  return Number(mantissa) * 10 ** Number(exponent);
}

const register = (calc, name) => numeric(calc.readRegister(name));
function settle(calc) {
  assert.ok(calc.runUntilStable({ maxFrames: 450, stableFrames: 8 }).stopped,
    'the calculator must reach the next prompt');
  return numeric(calc.displayText());
}

// The book's three rows, each repeated on three successive stages. This model
// uses distances, elapsed time and the scoring rule, independent of listing
// addresses, indirect selectors and the implementation's register layout.
const groups = [
  { distance: 205, lower: 1.4, upper: 1.5, goodSpeed: 140 },
  { distance: 580, lower: 5.1, upper: 5.2, goodSpeed: 112 },
  { distance: 412, lower: 3.8, upper: 3.9, goodSpeed: 107 },
];
const routes = groups.flatMap(group => Array.from({ length: 3 }, () => group));

function prompt(calc, stage) {
  const route = routes[stage - 1];
  assert.equal(settle(calc), stage * 1000 + route.distance);
  assert.equal(register(calc, 0), stage);
  assert.equal(register(calc, 2), route.distance);
  assert.equal(register(calc, 3), route.lower);
  assert.equal(register(calc, 4), route.upper);
}

function prepare(angleMode = 'rad') {
  const calc = new MK61({ angleMode });
  calc.loadProgram(listing);
  assert.deepEqual(calc.readProgramCodes(parsed.codes.length), parsed.codes);
  calc.pressSequence(['F', '/-/']);
  setup.forEach(text => keyboard(calc, text));
  prompt(calc, 1);
  assert.equal(register(calc, 1), 0);
  assert.equal(register(calc, 5), 0);
  assert.equal(register(calc, 6), 0);
  return calc;
}

let stages = 0;
let races = 0;
function drive(calc, stage, speed, score, overridePenalty = null, inspect = false, retry = false) {
  if (retry) {
    assert.equal(settle(calc), -1, 'a corrected speed follows the error prompt');
    assert.equal(register(calc, 0), stage);
  } else prompt(calc, stage);
  const route = routes[stage - 1];
  const exactTime = route.distance / speed;
  const penalty = overridePenalty ?? (exactTime < route.lower || exactTime > route.upper ? 10 : 0);
  if (inspect) {
    for (const name of ['3', '4']) calc.press('П→X').press(name);
  }
  calc.inputNumber(speed).press('С/П');
  const time = settle(calc);
  assert.ok(Math.abs(time - exactTime) <= Math.max(1, Math.abs(exactTime)) * 1e-7,
    `stage ${stage}, speed ${speed}: unexpected elapsed time ${time}`);
  assert.equal(register(calc, 5), speed);
  assert.equal(register(calc, 6), time);
  assert.equal(register(calc, 0), stage, 'calculating time must not skip a stage');
  const total = score + penalty;
  assert.equal(register(calc, 1), total);
  if (inspect) calc.press('П→X').press('1');
  calc.press('С/П');
  assert.equal(settle(calc), total, 'the second display is cumulative penalties');
  if (inspect) calc.press('П→X').press('6');
  calc.press('С/П');
  if (stage < 9) prompt(calc, stage + 1);
  else {
    assert.equal(settle(calc), (total < 60 ? 1000 : 2000) + total);
    assert.equal(register(calc, 0), 10);
    assert.deepEqual(calc.readProgramCodes(parsed.codes.length), parsed.codes);
    races++;
  }
  stages++;
  return { total, time };
}

function race(calc, speeds, expectedScore) {
  assert.equal(speeds.length, 9);
  let score = 0;
  speeds.forEach((speed, index) => { score = drive(calc, index + 1, speed, score).total; });
  assert.equal(score, expectedScore);
}

const goodSpeeds = routes.map(route => route.goodSpeed);
for (const angle of ['rad', 'deg', 'grad']) {
  const calc = prepare(angle);
  race(calc, goodSpeeds, 0);

  // One С/П after the result restarts the rally without re-entering constants.
  calc.press('С/П');
  prompt(calc, 1);
  for (const name of [1, 5, 6]) assert.equal(register(calc, name), 0);
  drive(calc, 1, 100, 0, null, true);
  calc.press('В/О').press('С/П');
  prompt(calc, 1);
  for (const name of [1, 5, 6]) assert.equal(register(calc, name), 0);
}

// The strict success threshold: 50 succeeds, exactly 60 fails. Check every
// stage scoring late and early as well, for both comparisons in all groups.
race(prepare(), [100, 100, 100, 100, 100, 112, 107, 107, 107], 50);
race(prepare(), [100, 100, 100, 100, 100, 100, 107, 107, 107], 60);
race(prepare(), Array(9).fill(100), 90);
race(prepare(), Array(9).fill(200), 90);

// These inputs land exactly on the displayed boundaries at calculator
// precision or just inside/outside them. Both boundaries are inclusive.
for (const [speed, expectedTime, penalty] of [
  [146.42857, 1.4, 0],
  [136.66666, 1.5, 0],
  [146.4286, 1.3999997, 10],
  [136.6666, 1.5000007, 10],
  [146.42856, 1.4000001, 0],
  [136.66668, 1.4999998, 0],
]) {
  const result = drive(prepare(), 1, speed, 0, penalty);
  assert.equal(result.time, expectedTime);
}

function invalid(calc, stage, score, lastTime, values) {
  prompt(calc, stage);
  for (const value of values) {
    calc.inputNumber(Math.abs(value));
    if (value < 0) calc.press('/-/');
    calc.press('С/П');
    assert.equal(settle(calc), -1);
    assert.equal(register(calc, 0), stage);
    assert.equal(register(calc, 1), score);
    assert.equal(register(calc, 6), lastTime);
    assert.equal(register(calc, 2), routes[stage - 1].distance);
    assert.equal(register(calc, 3), routes[stage - 1].lower);
    assert.equal(register(calc, 4), routes[stage - 1].upper);
  }
}

{
  const calc = prepare();
  invalid(calc, 1, 0, 0, [0, -140, 0]);
  drive(calc, 1, 140, 0, null, false, true);
  drive(calc, 2, 100, 0);
  const previousTime = register(calc, 6);
  invalid(calc, 3, 10, previousTime, [-1, 0]);
  drive(calc, 3, 140, 10, null, false, true);
}

console.log(`Romanovskis regularity rally: ${races} complete rallies, ${stages} stages; keyboard setup, all route groups, inclusive time limits, 50/60-point threshold, invalid-speed retry, restart and all angle modes OK.`);
