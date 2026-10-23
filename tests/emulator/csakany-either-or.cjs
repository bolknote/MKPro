#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');

const base = path.resolve(__dirname, '../../games/logic/ili-ili-csakany-1981');
const listing = fs.readFileSync(`${base}.txt`, 'utf8');
const card = fs.readFileSync(`${base}.md`, 'utf8');
const parsed = parseProgramText(listing);
assert.deepEqual(parsed.diagnostics, []);
assert.equal(parsed.codes.length, 104);
const cells = listing.trim().split(/\n/);
assert.equal(cells.length, 104, 'check addresses that the parser might ignore');
cells.forEach((line, i) => assert.ok(line.startsWith(
  `${i < 100 ? String(i).padStart(2, '0') : `A${i - 100}`}\t`)));
const preparation = card.split('## Подготовка\n')[1].split('## Выбор и ввод хода\n')[0];
const setup = [...preparation.matchAll(/```text\n([\s\S]*?)\n```/g)].map(match => match[1]);
assert.equal(setup.length, 3, 'common preparation and both mode launches');

const registerKeys = [...'0123456789', ',', '/-/', 'ВП', 'Cx', 'В↑'];
function keyboard(calc, text) {
  for (const token of text.trim().split(/\s+/)) {
    if (/^-?\d+(?:[.,]\d+)?(?:e-?\d+)?$/i.test(token)) {
      calc.inputNumber(token.replace(/^-/, ''));
      if (token.startsWith('-')) calc.press('/-/');
    } else if (/^хП[0-9a-e]$/i.test(token)) {
      calc.press('П').press(registerKeys[parseInt(token.slice(2), 16)]);
    } else calc.press(token);
  }
}
function numeric(text) {
  const [mantissa, exponent = '0'] = text.trim().replace(',', '.').split(/\s+/);
  return Number(mantissa) * 10 ** Number(exponent);
}
const register = (calc, name) => numeric(calc.readRegister(name));
const inventory = calc => [...'0123456789'].map(r => register(calc, r));
function settle(calc) {
  assert.ok(calc.runUntilStable({ maxFrames: 3000, stableFrames: 8 }).stopped,
    'a move must terminate, including retries for exhausted card ranks');
  calc.runFrames(12);
  const value = numeric(calc.displayText());
  assert.ok(Number.isFinite(value), `unexpected display ${calc.displayText()}`);
  return value;
}
function enter(calc, code) {
  calc.inputNumber(String(code).replace(/^-/, ''), { clear: true });
  if (String(code).startsWith('-')) calc.press('/-/');
  calc.press('С/П');
  return settle(calc);
}
function prepare(mode = 'solo', angleMode = 'rad', existing = null) {
  const calc = existing || new MK61({ angleMode });
  if (!existing) calc.loadProgram(listing);
  calc.pressSequence(['F', '/-/']);
  keyboard(calc, setup[0]);
  keyboard(calc, setup[mode === 'solo' ? 1 : 2]);
  assert.equal(settle(calc), 10000);
  assert.deepEqual(inventory(calc), Array(10).fill(4));
  assert.equal(register(calc, 'A'), 10000);
  assert.equal(register(calc, 'B'), 10);
  assert.equal(register(calc, 'E'), mode === 'solo' ? 100 : -101);
  return calc;
}
function model(mode) {
  return { mode, used: new Set(), remaining: Array(10).fill(4),
    scores: [0, 0], turn: 0 };
}
function packed(state) {
  return 10000 + state.scores[0] * 100 + state.scores[1];
}
function expectedInventory(state) {
  return state.remaining.map((count, digit) => count + (state.used.has(digit) ? 0.5 : 0));
}

let moves = 0, games = 0, errors = 0;
function move(calc, state, code) {
  const digit = Math.abs(code) - 1;
  assert.ok(Number.isInteger(code) && digit >= 0 && digit <= 9 && !state.used.has(digit));
  const before = inventory(calc).map(Math.trunc);
  const screen = enter(calc, code);
  const after = inventory(calc).map(Math.trunc);
  // Infer the dealt rank from the finite deck, independently of the ROM's
  // selected-rank scratch register and its score calculation. Exactly one
  // copy of one rank must disappear, even when the player takes a digit.
  const changed = before.map((count, rank) => count - after[rank]);
  assert.equal(changed.reduce((sum, delta) => sum + delta, 0), 1);
  assert.equal(changed.filter(delta => delta !== 0).length, 1);
  const rank = changed.indexOf(1);
  assert.ok(rank >= 0 && rank < 10);
  const dealt = rank + 1;
  assert.equal(register(calc, 'D'), dealt, 'last dealt card remains inspectable');
  assert.ok(state.remaining[rank] > 0, 'never deal a fifth copy');
  state.remaining[rank]--;

  // Independent rules model: select one of the two values and give the
  // other to the opponent only in the two-player mode. No packed weights,
  // signed countdown arithmetic or indirect register addressing here.
  const player = state.mode === 'solo' ? 0 : state.turn % 2;
  state.scores[player] += code > 0 ? digit : dealt;
  if (state.mode === 'two') state.scores[1 - player] += code > 0 ? dealt : digit;
  state.used.add(digit);
  state.turn++;
  assert.deepEqual(inventory(calc), expectedInventory(state));
  assert.equal(register(calc, 'A'), packed(state));
  const remaining = 10 - state.turn;
  assert.equal(register(calc, 'B'), state.mode === 'two' && state.turn % 2 ? -remaining : remaining);
  assert.equal(screen, state.turn === 10 ? -packed(state) : packed(state));
  assert.equal(register(calc, 'E'), state.mode === 'solo' ? 100 : -101);
  assert.ok(state.scores.every(score => score >= 0 && score < 100));
  moves++;
  return { screen, dealt };
}
function finish(calc, state, codes) {
  const trace = codes.map(code => move(calc, state, code));
  assert.equal(state.turn, 10);
  assert.equal(state.used.size, 10);
  assert.equal(state.remaining.reduce((a, b) => a + b), 30);
  if (state.mode === 'two') {
    const drawnTotal = state.remaining.reduce((sum, count, rank) => sum + (4 - count) * (rank + 1), 0);
    assert.equal(state.scores[0] + state.scores[1], 45 + drawnTotal);
  } else assert.equal(state.scores[1], 0, 'discarded cards must not leak into the solo score');
  const finalState = [...'0123456789ABCDE'].map(r => register(calc, r));
  for (const noise of [null, 9876543, -1]) {
    if (noise === null) calc.press('С/П');
    else {
      calc.inputNumber(String(Math.abs(noise)), { clear: true });
      if (noise < 0) calc.press('/-/');
      calc.press('С/П');
    }
    assert.equal(settle(calc), -packed(state));
    assert.deepEqual([...'0123456789ABCDE'].map(r => register(calc, r)), finalState);
  }
  // The documented return to 00 must also keep a completed game completed.
  keyboard(calc, 'В/О С/П');
  assert.equal(settle(calc), -packed(state));
  assert.equal(register(calc, 'B'), 0);
  assert.equal(enter(calc, 1), -packed(state), 'a completed game ignores choices after a return to 00');
  assert.deepEqual([...'0123456789ABCDE'].map(r => register(calc, r)), finalState);
  assert.deepEqual(calc.readProgramCodes(104), parsed.codes);
  games++;
  return trace;
}

// This keyboard control game has an exact answer independent of the RNG.
const controlCalc = prepare();
const control = finish(controlCalc, model('solo'), [1, 2, 3, 4, 5, 6, 7, 8, 9, 10]);
assert.deepEqual(control.map(turn => turn.screen),
  [10000, 10100, 10300, 10600, 11000, 11500, 12100, 12800, 13600, -14500]);
assert.ok(card.includes('10000 10100 10300 10600 11000\n11500 12100 12800 13600 −14500'));

// Different choices, key orders and every angle mode. Reuse a completed
// machine as well as cold machines; the native RNG has no user seed promise.
for (const [mode, angle, codes, existing] of [
  ['solo', 'grad', [-1, -2, -3, -4, -5, -6, -7, -8, -9, -10], null],
  ['solo', 'deg', [10, -1, 8, -3, 6, -5, 4, -7, 2, -9], null],
  ['two', 'rad', [1, 2, 3, 4, 5, 6, 7, 8, 9, 10], controlCalc],
  ['two', 'grad', [-1, -2, -3, -4, -5, -6, -7, -8, -9, -10], null],
  ['two', 'deg', [10, -9, 8, -7, 6, -5, 4, -3, 2, -1], null],
]) finish(prepare(mode, angle, existing), model(mode), codes);

// Continue the native RNG across several complete resets. Fixed and changing
// inputs previously caused retry cycles once a rank was exhausted; exercise
// different orders/signs without replacing the calculator's random function.
let orderState = 20261023;
const orderRandom = limit => {
  orderState = (Math.imul(orderState, 1664525) + 1013904223) >>> 0;
  return orderState % limit;
};
for (let game = 0; game < 6; game++) {
  const codes = Array.from({ length: 10 }, (_, i) => i + 1);
  for (let i = 9; i > 0; i--) {
    const j = orderRandom(i + 1);
    [codes[i], codes[j]] = [codes[j], codes[i]];
  }
  for (let i = 0; i < 10; i++) if (orderRandom(2)) codes[i] *= -1;
  const mode = game % 2 ? 'two' : 'solo';
  finish(prepare(mode, 'rad', controlCalc), model(mode), codes);
}

function reject(calc, state, code) {
  const before = [...inventory(calc), register(calc, 'A'), register(calc, 'B'), register(calc, 'E')];
  assert.equal(enter(calc, code), -1, `reject ${code}`);
  assert.deepEqual([...inventory(calc), register(calc, 'A'), register(calc, 'B'), register(calc, 'E')], before);
  // The recovery stop ignores an accidental numeric entry and shows the
  // score again; no new choice or random card is consumed by recovery.
  calc.inputNumber('999', { clear: true }).press('С/П');
  assert.equal(settle(calc), packed(state));
  assert.deepEqual(inventory(calc), expectedInventory(state));
  errors++;
}
const guarded = prepare('two');
const guardedState = model('two');
for (const bad of [0, '0.5', '-0.5', '1.5', '-9.5', 11, -11, '99999999', '-1e99', '1e-99', '-0.99999999', '10.000001']) {
  reject(guarded, guardedState, bad);
}
// Simply continuing at the score is not a move.
guarded.press('С/П');
assert.equal(settle(guarded), -1);
guarded.press('С/П');
assert.equal(settle(guarded), 10000);
move(guarded, guardedState, -10);
reject(guarded, guardedState, 10);
reject(guarded, guardedState, -10);
finish(guarded, guardedState, [1, -2, 3, -4, 5, -6, 7, -8, 9]);

// Restarting in the middle must clear both the used-pair flags and all
// depleted card counts, and permit the same digit again.
const interrupted = prepare();
const interruptedState = model('solo');
move(interrupted, interruptedState, -3);
const pausedState = [...inventory(interrupted), register(interrupted, 'A'), register(interrupted, 'B')];
keyboard(interrupted, 'В/О С/П');
assert.equal(settle(interrupted), packed(interruptedState));
assert.deepEqual([...inventory(interrupted), register(interrupted, 'A'), register(interrupted, 'B')], pausedState,
  'returning to 00 during play must preserve the pending player and resources');
const restarted = prepare('solo', 'rad', interrupted);
assert.equal(move(restarted, model('solo'), 3).screen, 10200);

// Probe the unchanged deal code with an ordinary keyboard value in place of
// its first RNG result. This tests accepted copy-number boundaries and an
// exhausted rank with a used-pair fraction, without patching any instruction,
// branch, RNG function or ROM. Subsequent rejected proposals use real К СЧ.
const randomAddress = parsed.codes.indexOf(0x3b);
assert.equal(randomAddress, 51);
for (const [rank, count, copy, flag] of [
  [0, 1, 0, 0.5], [9, 2, 1, 0], [9, 3, 2, 0.5], [9, 4, 3, 0.5],
  [0, 0, 0, 0.5], [9, 0, 0, 0.5],
]) {
  const calc = prepare('two');
  keyboard(calc, `${count + flag} хП${rank} 100 хПc`);
  const before = inventory(calc);
  const proposal = (rank * 4 + copy + 0.25) / 40;
  keyboard(calc, `${proposal} БП ${randomAddress + 1} С/П`);
  settle(calc);
  const actualRank = register(calc, 'D') - 1;
  if (count > copy) assert.equal(actualRank, rank, 'accept every remaining copy number');
  else assert.notEqual(actualRank, rank, 'a rank with zero whole copies must be rejected');
  const expected = [...before];
  expected[actualRank]--;
  assert.deepEqual(inventory(calc), expected);
  assert.equal(register(calc, 'A'), 10000 + actualRank + 1);
}

// Exact bin edges, zero and the largest eight-digit fraction must all map
// to the intended rank/copy. This catches rounded ten-valued selectors and
// a missing integer-part operation in the shorter proposal conversion.
for (const proposal of [0, 0.025, 0.05, 0.075, 0.09999999, 0.1, 0.99999999]) {
  const calc = prepare('two');
  keyboard(calc, `100 хПc ${proposal} БП ${randomAddress + 1} С/П`);
  settle(calc);
  const rank = Math.floor(proposal * 10);
  assert.equal(register(calc, 'D'), rank + 1);
  assert.equal(register(calc, 'A'), 10000 + rank + 1);
  const expected = Array(10).fill(4);
  expected[rank]--;
  assert.deepEqual(inventory(calc), expected);
}

console.log(`Csákány either/or: ${games} complete ROM games, ${moves} moves, ${errors} rejected codes; solo/two-player rules, finite 40-card deck, copy boundaries, both choices, all angle modes, hidden solo discards, terminal reports and keyboard restarts OK.`);
