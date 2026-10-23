#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');

const base = path.resolve(__dirname,
  '../../games/sports/gonka-cherepakhi-ulitki-i-mnogonozhki-romanovskis-1987');
const listing = fs.readFileSync(`${base}.txt`, 'utf8');
const card = fs.readFileSync(`${base}.md`, 'utf8');
const parsed = parseProgramText(listing);
assert.deepEqual(parsed.diagnostics, []);
assert.equal(parsed.codes.length, 103);

const preparation = card.split('## Подготовка\n')[1].split('## Проведение гонки\n')[0];
const setup = [...preparation.matchAll(/```text\n([\s\S]*?)\n```/g)].map(match => match[1]);
assert.equal(setup.length, 2, 'the card must document constants, seed and launch');

function keyboard(calc, text) {
  for (const token of text.trim().split(/\s+/)) {
    if (/^\d+(?:[.,]\d+)?$/.test(token)) calc.inputNumber(token);
    else if (/^хП[0-9А]$/.test(token)) calc.press('П').press(token.slice(2).replace('А', ','));
    else calc.press(token);
  }
}

function numeric(text) {
  const [mantissa, exponent = '0'] = text.trim().replace(',', '.').split(/\s+/);
  return Number(mantissa) * 10 ** Number(exponent);
}

function register(calc, name) {
  return numeric(calc.readRegister(name));
}

function settle(calc) {
  const status = calc.runUntilStable({ maxFrames: 600, stableFrames: 8 });
  assert.ok(status.stopped, 'the calculator must reach a stop');
  return numeric(calc.displayText());
}

function prepare(seed = '0,371825', finish = 27, angleMode = 'rad') {
  const calc = new MK61({ angleMode });
  calc.loadProgram(listing);
  assert.deepEqual(calc.readProgramCodes(parsed.codes.length), parsed.codes);
  calc.pressSequence(['F', '/-/']);
  keyboard(calc, setup[0]);
  keyboard(calc, `${finish} хП4`);
  keyboard(calc, setup[1].replace('0,371825', seed));
  assert.equal(settle(calc), 1000000);
  assert.deepEqual([1, 2, 3].map(name => register(calc, name)), [0, 0, 0]);
  return calc;
}

// Independent decimal model of the book's multiplication by 997. Eight
// significant digits are rounded before extracting the fractional part,
// matching the calculator's ordinary multiplication precision. Seven decimal
// places suffice for the documented six/seven-digit initial seeds.
const scale = 10000000n;
function model(seed, finish = 27) {
  return { seed: BigInt(Math.round(Number(seed.replace(',', '.')) * Number(scale))),
    finish, positions: [0, 0, 0], winner: 0 };
}

function advance(state) {
  let product = state.seed * 997n;
  const excess = product.toString().length - 8;
  if (excess > 0) {
    const unit = 10n ** BigInt(excess);
    product = ((product + unit / 2n) / unit) * unit;
  }
  state.seed = product % scale;
  assert.notEqual(state.seed, 0n, 'test seed must not enter the zero state');
  const digits = [1000000n, 100000n, 10000n].map(divisor => Number(state.seed / divisor % 10n));
  state.positions = state.positions.map((position, index) => position + digits[index]);
  state.winner = state.positions.reduce((flags, position, index) =>
    flags + (position >= state.finish ? [100, 10, 1][index] : 0), 0);
  return state.winner ? -(1000 + state.winner) : screen(state.positions);
}

function screen([turtle, snail, millipede]) {
  return 1000000 + turtle * 10000 + snail * 100 + millipede;
}

let turns = 0;
let races = 0;
function step(calc, state) {
  const expected = advance(state);
  calc.press('С/П');
  assert.equal(settle(calc), expected, 'screen/result must follow the book rules');
  assert.deepEqual([1, 2, 3].map(name => register(calc, name)), state.positions);
  assert.equal(register(calc, 'Д'), screen(state.positions), 'final positions must remain available');
  assert.equal(register(calc, 9), state.winner);
  assert.equal(Math.round(register(calc, 0) * Number(scale)), Number(state.seed));
  turns++;
  return expected;
}

function finishRace(calc, state) {
  const trace = [];
  for (let count = 0; count < 100 && !state.winner; count++) trace.push(step(calc, state));
  assert.ok(state.winner, 'a race must reach the finish');
  assert.deepEqual(calc.readProgramCodes(parsed.codes.length), parsed.codes);
  races++;
  return trace;
}

const bookTrace = [1070009, 1110010, 1130015, 1140517, 1160818, 1181122, -1001];
for (const angle of ['rad', 'deg', 'grad']) {
  const calc = prepare('0,371825', 27, angle);
  const state = model('0,371825');
  assert.deepEqual(finishRace(calc, state), bookTrace);
  assert.deepEqual(state.positions, [23, 11, 29]);

  // Restart by the documented single key. This clears the race, retaining
  // the generated seed rather than restoring the first race's sequence.
  const lastSeed = state.seed;
  calc.press('С/П');
  assert.equal(settle(calc), 1000000);
  assert.equal(Math.round(register(calc, 0) * Number(scale)), Number(lastSeed));
  assert.deepEqual([1, 2, 3, 9].map(name => register(calc, name)), [0, 0, 0, 0]);
  state.positions = [0, 0, 0];
  state.winner = 0;
  assert.equal(step(calc, state), 1080407);

  // A manual restart during a running race likewise preserves its seed.
  const interruptedSeed = state.seed;
  calc.press('В/О').press('С/П');
  assert.equal(settle(calc), 1000000);
  assert.equal(Math.round(register(calc, 0) * Number(scale)), Number(interruptedSeed));
}

// Cold replay with the same explicit seed is reproducible. Different seeds
// exercise entire races against the independent decimal model.
assert.deepEqual(finishRace(prepare(), model('0,371825')), bookTrace);
for (const seed of ['0,5284163', '0,123456', '0,987654']) {
  const trace = finishRace(prepare(seed), model(seed));
  assert.notEqual(trace[0], bookTrace[0]);
}
finishRace(prepare('0,123456', 90), model('0,123456', 90));

// All seven winner combinations. Finish 1 gives an exact arrival for every
// selected animal; the canonical race above checks overshooting the finish.
for (const [seed, winner, positions] of [
  ['0,000101', 100, [1, 0, 0]],
  ['0,001014', 10, [0, 1, 0]],
  ['0,001005', 1, [0, 0, 1]],
  ['0,000111', 110, [1, 1, 0]],
  ['0,000102', 101, [1, 0, 1]],
  ['0,001015', 11, [0, 1, 1]],
  ['0,000112', 111, [1, 1, 1]],
]) {
  const calc = prepare(seed, 1);
  const state = model(seed, 1);
  assert.equal(step(calc, state), -(1000 + winner));
  assert.deepEqual(state.positions, positions);
  races++;
}

// Inspecting registers, as suggested by the card, cannot advance or reseed the
// generator and does not change what the next С/П computes.
{
  const calc = prepare();
  const state = model('0,371825');
  for (const key of ['1', '2', '3']) calc.press('П→X').press(key);
  assert.equal(step(calc, state), bookTrace[0]);
}

// Bad input is a real calculator error; recover using only keyboard actions.
for (const seed of ['0', '-0.125', '1', '1.125']) {
  const calc = prepare();
  calc.press('Cx').inputNumber(seed.replace('-', ''));
  if (seed.startsWith('-')) calc.press('/-/');
  calc.press('П').press('0').press('В/О').press('С/П');
  calc.runUntilStable({ maxFrames: 300, stableFrames: 8 });
  calc.runFrames(12);
  assert.equal(calc.displayText(), 'ЕГГ0Г', `invalid seed ${seed}`);
  calc.press('Cx');
  keyboard(calc, setup[1]);
  assert.equal(settle(calc), 1000000);
  assert.equal(step(calc, model('0,371825')), bookTrace[0]);
}

console.log(`Romanovskis race: ${races} complete races, ${turns} turns; explicit seeds, decimal RNG, replay/restart, seven winner combinations, invalid-input recovery and all angle modes OK.`);
