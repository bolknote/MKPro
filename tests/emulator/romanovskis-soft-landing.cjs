#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');

const base = path.resolve(__dirname,
  '../../games/simulation/myagkaya-posadka-romanovskis-1987');
const listing = fs.readFileSync(`${base}.txt`, 'utf8');
const card = fs.readFileSync(`${base}.md`, 'utf8');
const parsed = parseProgramText(listing);
assert.deepEqual(parsed.diagnostics, []);
assert.equal(parsed.codes.length, 105);

const preparation = card.split('## Подготовка\n')[1].split('## Управление\n')[0];
const setup = [...preparation.matchAll(/```text\n([\s\S]*?)\n```/g)].map(match => match[1]);
assert.equal(setup.length, 3, 'the card must document constants, initial state and launch');
const registerKeys = { А: ',', В: '/-/', С: 'ВП', Д: 'Cx', Е: 'В↑', a: ',', b: '/-/', c: 'ВП', d: 'Cx', e: 'В↑' };

function enter(calc, value) {
  calc.inputNumber(Math.abs(value));
  if (value < 0) calc.press('/-/');
}

function keyboard(calc, text) {
  for (const token of text.trim().split(/\s+/)) {
    if (/^-?\d+(?:[.,]\d+)?$/.test(token)) enter(calc, Number(token.replace(',', '.')));
    else if (/^хП[0-9АВСДЕa-e]$/.test(token)) {
      const name = token.slice(2);
      calc.press('П').press(registerKeys[name] || name);
    } else calc.press(token);
  }
}

function numeric(text) {
  const [mantissa, exponent = '0'] = text.trim().replace(',', '.').replace(/(\d)([+-]\d+)$/, '$1 $2').split(/\s+/);
  return Number(mantissa) * 10 ** Number(exponent);
}
const register = (calc, name) => numeric(calc.readRegister(name));
function settle(calc) {
  assert.ok(calc.runUntilStable({ maxFrames: 450, stableFrames: 8 }).stopped,
    'the calculator must reach the next display');
  return numeric(calc.displayText());
}
function close(actual, expected, description) {
  assert.ok(Number.isFinite(actual) &&
    Math.abs(actual - expected) <= Math.max(2e-6, Math.abs(expected) * 2e-7),
  `${description}: expected ${expected}, got ${actual}`);
}

// Independent event calculation: solve BOTH quadratic roots directly and
// select the first contact in [0, 1], including a dip below the surface followed
// by ascent. The listing instead uses a stable rationalized root.
function advance(state, requestedThrust) {
  const thrust = Math.min(requestedThrust, state.fuel / 0.15);
  const acceleration = 1.62 - 0.045 * thrust;
  const discriminant = state.velocity ** 2 + 2 * acceleration * state.height;
  let roots = [];
  if (Math.abs(acceleration) < 1e-12) {
    if (state.velocity > 0) roots = [state.height / state.velocity];
  } else if (discriminant >= 0) {
    const speed = Math.sqrt(discriminant);
    roots = [(-state.velocity + speed) / acceleration, (-state.velocity - speed) / acceleration];
  }
  const contacts = roots.filter(t => t >= 0 && t <= 1 + 1e-10);
  const landed = contacts.length > 0;
  const duration = landed ? Math.min(...contacts) : 1;
  return {
    height: landed ? 0 : state.height - state.velocity - acceleration / 2,
    velocity: state.velocity + acceleration * duration,
    fuel: Math.max(0, state.fuel - 0.15 * thrust * duration),
    seconds: state.seconds + duration,
    duration, thrust, acceleration,
  };
}
const outcome = velocity => Math.abs(velocity) < 2 ? 1 : Math.abs(velocity) <= 4 ? 2 : 3;
const book = [
  { height: 50, velocity: 8, fuel: 90 },
  { height: 150, velocity: 14, fuel: 130 },
  { height: 1000, velocity: 25, fuel: 350 },
];

function instruments(calc, state) {
  close(settle(calc), state.height, 'height display');
  calc.press('С/П');
  close(settle(calc), state.velocity, 'velocity display');
  calc.press('С/П');
  close(settle(calc), state.fuel, 'fuel display');
}

function launch(calc, initial = book[0], fromCard = false) {
  if (fromCard) keyboard(calc, setup[1]);
  else keyboard(calc,
    `${initial.height} хП0\n${initial.velocity} хП1\n${initial.fuel} хП2`);
  keyboard(calc, setup[2]);
  const state = { ...initial, seconds: 0 };
  instruments(calc, state);
  assert.equal(register(calc, 5), 0, 'launch resets the elapsed time');
  return { calc, state, landed: false };
}

function prepare(initial = book[0], angleMode = 'rad') {
  const calc = new MK61({ angleMode });
  calc.loadProgram(listing);
  assert.deepEqual(calc.readProgramCodes(parsed.codes.length), parsed.codes);
  calc.pressSequence(['F', '/-/']);
  keyboard(calc, setup[0]);
  return launch(calc, initial, initial === book[0]);
}

let steps = 0;
let landings = 0;
function step(flight, thrust, inspect = false) {
  assert.equal(flight.landed, false, 'no burn after touchdown');
  const { calc } = flight;
  const next = advance(flight.state, thrust);
  if (inspect) {
    for (const name of [0, 1, 2, 5]) calc.press('П→X').press(String(name));
  }
  enter(calc, thrust);
  calc.press('С/П');
  const display = settle(calc);
  for (const [name, field] of [[0, 'height'], [1, 'velocity'], [2, 'fuel'],
    [3, 'thrust'], [4, 'acceleration'], [5, 'seconds']]) {
    close(register(calc, name), next[field], `${field} after step ${next.seconds}`);
  }
  assert.ok(register(calc, 2) >= 0, 'fuel never becomes negative');
  flight.state = next;
  flight.landed = next.height <= 0;
  if (flight.landed) {
    assert.equal(display, 0);
    close(register(calc, 'a'), next.duration, 'partial contact interval');
    assert.deepEqual(calc.readProgramCodes(parsed.codes.length), parsed.codes);
    landings++;
  } else instruments(calc, next);
  steps++;
  return flight;
}

function terminal(flight) {
  assert.ok(flight.landed);
  const { calc, state } = flight;
  const before = [0, 1, 2, 3, 4, 5].map(name => register(calc, name));
  for (let cycle = 0; cycle < 2; cycle++) {
    for (const value of [state.velocity, state.fuel, 0]) {
      // Entries at the terminal displays cannot accidentally restart motion.
      if (cycle) calc.inputNumber(100);
      calc.press('С/П');
      close(settle(calc), value, 'terminal display cycle');
      assert.deepEqual([0, 1, 2, 3, 4, 5].map(name => register(calc, name)), before);
    }
  }
}

function fly(flight, controls, expectedCode, expectedSeconds) {
  for (const thrust of controls) step(flight, thrust);
  assert.ok(flight.landed, 'the documented control sequence must reach the surface');
  assert.equal(settle(flight.calc), 0);
  assert.equal(1000 + outcome(register(flight.calc, 1)), expectedCode);
  if (expectedSeconds !== null) close(register(flight.calc, 5), expectedSeconds, 'physical flight time');
  terminal(flight);
  return flight;
}

// The card's complete 12-input flight, including its first-step example and
// shortened last interval with unspent fuel. The same card setup works in all angular modes.
for (const angle of ['rad', 'deg', 'grad']) {
  const flight = prepare(book[0], angle);
  step(flight, 50, true);
  close(register(flight.calc, 0), 42.315, 'the book panel first-step altitude');
  close(register(flight.calc, 1), 7.37, 'the book panel first-step velocity');
  close(register(flight.calc, 2), 82.5, 'the book panel first-step fuel');
  fly(flight, Array(11).fill(50), 1001, 100 / 9);
  close(register(flight.calc, 1), 1, 'the card touchdown speed');
  close(register(flight.calc, 2), 20 / 3, 'fuel is charged only until contact');

  // Re-enter only the initial state: constants survive terminal displays and
  // В/О starts a new flight both after landing and during an unfinished flight.
  step(launch(flight.calc, book[0], true), 0);
  const restarted = launch(flight.calc, book[0], true);
  assert.equal(restarted.state.seconds, 0);
}
console.log('Soft landing: keyboard setup, card example, restarts and angle modes OK.');

fly(prepare(book[1]), [...Array(4).fill(0), ...Array(7).fill(95), 36, 36], 1001, 12 + 0.8325 / 1.895);
fly(prepare(book[2]), [...Array(15).fill(0), ...Array(17).fill(100), 0, ...Array(11).fill(36)],
  1001, 43 + 0.06 / 1.96);
const freefall = fly(prepare(), Array(5).fill(0), 1003, (Math.sqrt(226) - 8) / 1.62);
close(register(freefall.calc, 1), Math.sqrt(226), 'freefall impact velocity');
assert.equal(register(freefall.calc, 2), 90);
console.log('Soft landing: complete flights from all three book scenarios and freefall OK.');

// Exact touchdown, strict 2 m/s success limit and inclusive 4 m/s damage limit.
for (const [velocity, code] of [[1, 1001], [1.9999999, 1001], [2, 1002],
  [4, 1002], [4.0000001, 1003]]) {
  fly(prepare({ height: 1, velocity, fuel: 90 }), [36], code, 1 / velocity);
}
// Do not reward braking after contact, or allow a ship to tunnel through the
// surface and emerge again during the same second.
fly(prepare({ height: 1, velocity: 4, fuel: 90 }), [100], 1002, 5 / 18);
const early = fly(prepare({ height: 0.01, velocity: 0.5, fuel: 90 }), [100], 1001, null);
assert.ok(register(early.calc, 5) < 0.1);
const clearTurn = prepare({ height: 1, velocity: 0.5, fuel: 90 });
step(clearTurn, 100);
assert.equal(clearTurn.landed, false);
assert.ok(register(clearTurn.calc, 0) > 1);

{
  const flight = prepare();
  step(flight, 100);
  step(flight, 100);
  step(flight, 100);
  assert.ok(flight.state.velocity < 0, 'sustained thrust can reverse the descent');
  const height = register(flight.calc, 0);
  step(flight, 100);
  assert.ok(register(flight.calc, 0) > height, 'upward flight gains height');
  assert.equal(flight.landed, false);
}
{
  const flight = prepare({ height: 20, velocity: -2, fuel: 90 });
  step(flight, 36);
  close(register(flight.calc, 0), 22, 'negative velocity increases height');
  step(flight, 0);
  step(flight, 0);
  assert.ok(flight.state.velocity > 0, 'gravity eventually restores downward motion');
}

for (const fuel of [0, 1, 15]) {
  const flight = prepare({ height: 100, velocity: 8, fuel });
  step(flight, 100);
  close(register(flight.calc, 2), 0, 'empty tank at calculator precision');
  close(register(flight.calc, 3), Math.min(100, fuel / 0.15), 'fuel-limited thrust');
  step(flight, 100);
  close(register(flight.calc, 3), 0, 'no effective thrust after exhaustion');
  close(register(flight.calc, 4), 1.62, 'empty tank gives freefall');
}

{
  const flight = prepare();
  step(flight, 33.25);
  step(flight, 36);
  const before = [0, 1, 2, 4, 5].map(name => register(flight.calc, name));
  for (const invalid of [-1, 100.01, -0.25, 101]) {
    enter(flight.calc, invalid);
    flight.calc.press('С/П');
    assert.equal(settle(flight.calc), -1);
    assert.deepEqual([0, 1, 2, 4, 5].map(name => register(flight.calc, name)), before,
      'invalid input cannot change flight state or elapsed time');
  }
  step(flight, 50, true);
  assert.equal(register(flight.calc, 5), 3, 'correction resumes the same next second');
}

console.log(`Romanovskis soft landing: ${landings} complete landings, ${steps} accepted steps; first-contact physics, speed boundaries, ascent, fractional fuel/time, fuel limits, invalid-input retry and terminal displays OK.`);
