#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');

const game = path.resolve(__dirname, '../../games/simulation/podvodnaya-lodka-lazarev');
const parsed = parseProgramText(fs.readFileSync(`${game}.txt`, 'utf8'));
const description = fs.readFileSync(`${game}.md`, 'utf8');
const preparation = description.split('## Подготовка\n')[1]?.split('## Экран\n')[0];
assert.ok(preparation, 'the game card must contain keyboard preparation');
const setup = [...preparation.matchAll(/```text\n([\s\S]*?)\n```/g)].map(match => match[1]);
assert.equal(setup.length, 3, 'preparation, error recovery and launch must all be documented');
const initial = {
  0: '20', 1: '8,-----8-E2', 2: '1,------0E-3',
  3: '2,------0E-6', 4: '3,------0E-11', 5: '3,141592E-1',
  6: '0', 7: '1', 8: '10000000', 9: '-50',
  A: '-20', B: '-40000000', C: '0', D: '4001', E: 'Е0000082',
};
assert.deepEqual(parsed.diagnostics, []);
assert.equal(parsed.codes.length, 105);

// These checkpoints bound fragments of the delivered listing. Only checkpoint
// instructions are replaced with STOP; all arithmetic, branches, RNG and display
// work under test execute on the unchanged calculator ROM.
const entry = { frame: 0, control: 28, air: 45, random: 57, enemy: 63 };
const modes = { rad: 10, deg: 11, grad: 12 };
const mask = position => position === 0 ? 0 : position === 1 ? -40000000 : 4 * 10 ** (8 - position);
let checks = 0;

function calculator(overrides = {}, mode = 'grad', stop = null) {
  const calc = new MK61({ angleMode: mode });
  const codes = parsed.codes.slice();
  for (const address of stop === null ? [] : Array.isArray(stop) ? stop : [stop]) {
    codes[address] = 0x50;
  }
  calc.loadProgram(codes);
  for (const [register, value] of Object.entries({ ...initial, ...overrides })) {
    calc.setRegister(register, String(value));
  }
  assert.deepEqual(calc.readProgramCodes(codes.length), codes);
  return calc;
}

function run(calc, from) {
  calc.press('В/О').pressSequence(['БП', ...String(from).padStart(2, '0'), 'С/П']);
  const result = calc.runUntilStable({ maxFrames: 240, stableFrames: 8 });
  assert.equal(result.stopped, true, `fragment at ${from} did not settle`);
  // A stationary program counter can precede the end of ROM error handling.
  // Let the calculator finish the error before inspecting its display.
  calc.runFrames(12);
  checks++;
  return calc.displayText();
}

function number(calc, register) {
  const text = calc.readRegister(register).trim().replace(',', '.');
  const fields = text.split(/\s+/);
  return Number(fields[0]) * 10 ** Number(fields[1] || 0);
}

// RA holds minus the oxygen supply. R0 carries the positive supply into the
// next display loop, which consumes it. Both must agree at a frame boundary.
const oxygenState = air => ({ 0: air, A: -air });
const oxygen = calc => 0 - number(calc, 'A');

function pressOperations(calc, text) {
  const registers = ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ',', '/-/', 'ВП', 'Cx', 'В↑'];
  const operations = {
    КИНВ: ['K', 'Cx'], 'К∨': ['K', '/-/'], 'К{x}': ['K', '8'], 'К-': ['K', '-'],
  };
  const keys = [];
  for (const token of text.trim().split(/\s+/)) {
    if (/^\d+(?:,\d+)?$/.test(token)) keys.push(...token);
    else if (/^П[0-9A-E]$/.test(token)) keys.push('П', registers[parseInt(token[1], 16)]);
    else if (operations[token]) keys.push(...operations[token]);
    else {
      assert.ok(['В↑', 'ВП', '/-/', 'В/О', 'БП', 'С/П'].includes(token), `unsupported key: ${token}`);
      keys.push(token);
    }
  }
  calc.pressSequence(keys);
}

function prepareRegisters(calc) {
  calc.pressSequence(['F', '/-/']);
  pressOperations(calc, setup[0]);
  // The real keyboard must finish displaying the intentional error before ВП.
  calc.runFrames(20);
  assert.equal(calc.displayText(), 'ЕГГ0Г');
  pressOperations(calc, setup[1]);
  assert.deepEqual(calc.readProgramCodes(parsed.codes.length), parsed.codes,
    'manual preparation must not change any program cell');
}

{
  const reference = calculator();
  const expected = Object.fromEntries(Object.keys(initial).map(register => [register, reference.readRegister(register)]));
  for (const mode of Object.keys(modes)) {
    const calc = new MK61({ angleMode: mode });
    calc.loadProgram(parsed.codes);
    // No setRegister calls: both the first game and its restart use only the
    // key sequences printed in the card, including recovery of the E word.
    for (let restart = 0; restart < 2; restart++) {
      prepareRegisters(calc);
      for (const [register, value] of Object.entries(expected)) {
        assert.equal(calc.readRegister(register), value, `manual R${register}, mode=${mode}, restart=${restart}`);
      }
      pressOperations(calc, setup[2]);
      calc.runFrames(1500);
      assert.equal(calc.displayText(), 'ЕГГ0Г', `manual game: mode=${mode}, restart=${restart}`);
      assert.equal(number(calc, '7'), mode === 'rad' ? 4 : 1);
      if (mode === 'rad') assert.ok(oxygen(calc) <= 0);
      assert.deepEqual(calc.readProgramCodes(parsed.codes.length), parsed.codes);
      checks++;
    }
  }
}
console.log('Submarine: documented keyboard preparation and restart work in all angle modes.');

function screen(depth, position, air) {
  const horizon = Array(8).fill('-');
  if (depth > 0) horizon[0] = String(depth);
  if (depth === 0) horizon[6] = '8';
  if (position > 0) {
    // On the surface, the unreachable collision at cell 7 overlays 8 OR 4.
    horizon[position - 1] = depth === 0 && position === 7 ? 'С' : 'Е';
  }
  // STOP formats positive orders 0..7 as an ordinary number. The live В↑
  // display is checked separately below because it keeps the order field.
  if (air <= 7) return `${horizon.slice(0, air + 1).join('')},${horizon.slice(air + 1).join('')}`;
  return `${horizon[0]},${horizon.slice(1).join('')} ${String(air).padStart(2, '0')}`;
}

for (let depth = 0; depth <= 3; depth++) {
  for (let position = 0; position <= 8; position++) {
    for (const air of [1, 20, 50]) {
      const calc = calculator({ ...oxygenState(air), 6: mask(position), 7: 1 + depth }, 'grad', entry.control);
      assert.equal(run(calc, entry.frame), screen(depth, position, air),
        `render depth=${depth}, position=${position}, air=${air}`);
    }
  }
}
console.log('Submarine: all display cells, three depths and oxygen orders OK.');

for (const [depth, position, air] of [
  ...[1, 2, 7, 8, 20, 50].map(air => [0, 0, air]),
  [1, 0, 1], [2, 0, 7], [3, 0, 20], [2, 1, 20], [2, 2, 20], [3, 8, 20],
]) {
  const calc = calculator({ ...oxygenState(air), 6: mask(position), 7: 1 + depth }, 'grad', entry.control);
  const expectedHorizon = screen(depth, position, 20).split(' ')[0].replace(',', '');
  assert.equal(expectedHorizon.length, 8, 'depth must not shorten the horizon');
  calc.press('В/О').pressSequence(['БП', '0', '0', 'С/П']);
  let seen = false;
  for (let quantum = 0; quantum < 70000 && !seen; quantum++) {
    for (let tick = 0; tick < 42; tick++) calc.tick();
    calc.updateDisplay();
    const cells = calc.displayCells();
    seen = cells.slice(1, 9).map(cell => cell.symbol).join('') === expectedHorizon &&
      cells[10].digit === Math.trunc(air / 10) && cells[11].digit === air % 10;
  }
  assert.ok(seen, `live display: depth=${depth}, position=${position}, oxygen=${air}`);
  checks++;
}

for (const [mode, movement] of [['rad', 1], ['grad', 0], ['deg', -1]]) {
  for (let depth = 0; depth <= 3; depth++) {
    const next = Math.max(0, Math.min(3, depth + movement));
    const calc = calculator({ 7: 1 + depth }, mode, entry.air);
    run(calc, entry.control);
    assert.equal(number(calc, '7'), 1 + next);
    assert.equal(number(calc, 'C'), next - depth, 'a blocked step must count as stationary');
  }
}

{
  // Keep the same machine state while diving to the bottom, holding there,
  // ascending to the surface and holding there. Between control updates, run
  // the actual enemy logic and complete display; an injected q keeps the sea
  // empty so that the entire itinerary can be checked deterministically.
  const calc = calculator(oxygenState(50), 'rad', entry.random);
  const itinerary = ['rad', 'rad', 'rad', 'rad', 'deg', 'deg', 'deg', 'deg', 'rad', 'grad', 'deg', 'deg'];
  let depth = 0;
  let air = 50;
  for (const [index, mode] of itinerary.entries()) {
    const movement = { rad: 1, grad: 0, deg: -1 }[mode];
    const next = Math.max(0, Math.min(3, depth + movement));
    air = Math.min(50, air + [2, -2, -5, -10][next]);
    calc.angleMode = modes[mode];
    if (index > 0) calc.setRegister('5', '0.75');
    assert.notEqual(run(calc, index === 0 ? entry.control : entry.enemy), 'ЕГГ0Г');
    assert.equal(number(calc, '7'), 1 + next);
    assert.equal(number(calc, 'C'), next - depth);
    assert.equal(oxygen(calc), air);
    assert.equal(number(calc, '0'), air);
    assert.equal(number(calc, '6'), 0);
    depth = next;
  }
}
console.log('Submarine: both depth limits, actual movement and repeated round trips OK.');

for (let depth = 0; depth <= 3; depth++) {
  // Check exact integer oxygen at every supply and every depth.
  for (let air = 1; air <= 50; air++) {
    const expected = Math.min(50, air + [2, -2, -5, -10][depth]);
    const calc = calculator({ ...oxygenState(air), 7: 1 + depth }, 'grad', entry.random);
    const display = run(calc, entry.control);
    assert.equal(oxygen(calc), expected);
    assert.equal(number(calc, '0'), expected);
    assert.equal(display === 'ЕГГ0Г', expected <= 0, `air=${air}, depth=${depth}`);
  }
}
console.log('Submarine: angle switch, oxygen consumption, recharge, cap and drowning OK.');

for (let depth = 0; depth <= 3; depth++) {
  for (const air of [12, 20, 48, 49, 50]) {
    const expected = Math.min(50, air + [2, -2, -5, -10][depth]);
    // Enter the air update with a deliberately stale display counter, then run
    // RNG, spawning and the complete next frame before the only patched STOP.
    const calc = calculator({ ...oxygenState(air), 0: 11, 7: 1 + depth }, 'grad', entry.control);
    const display = run(calc, entry.air);
    const position = number(calc, '6') === 0 ? 0 : 1;
    assert.equal(display, screen(depth, position, expected));
    assert.equal(oxygen(calc), expected);
    assert.equal(number(calc, '0'), 1, 'the frame must consume its refreshed counter');
  }
}
console.log('Submarine: oxygen update carries the correct counter into the next frame.');

for (const [mode, depth] of [['deg', 0], ['rad', 3], ['deg', 1], ['rad', 2], ['grad', 0], ['grad', 3]]) {
  for (const [position, q] of [[4, 0.125], [4, 0.375], [4, 0.625], [4, 0.875], [5, 0.625], [4, 1.625]]) {
    const requested = { rad: 1, grad: 0, deg: -1 }[mode];
    const nextDepth = Math.max(0, Math.min(3, depth + requested));
    const movement = nextDepth - depth;
    const nextPosition = position + 1 + Math.trunc(q);
    const vulnerable = movement !== 0 || nextDepth < 2;
    const hit = vulnerable && (nextPosition >= 6 ||
      nextPosition === 5 && q % 1 < (movement !== 0 ? 0.25 : 0.5));
    const context = `bounded detection: mode=${mode}, depth=${depth}, position=${position}, q=${q}`;
    const calc = calculator({ ...oxygenState(50), 6: mask(position), 7: 1 + depth },
      mode, [entry.frame, entry.random]);
    assert.notEqual(run(calc, entry.control), 'ЕГГ0Г', context);
    assert.equal(number(calc, '7'), 1 + nextDepth, context);
    assert.equal(number(calc, 'C'), movement, context);
    assert.equal(oxygen(calc), Math.min(50, 50 + [2, -2, -5, -10][nextDepth]), context);
    calc.setRegister('5', String(q));
    assert.equal(run(calc, entry.enemy) === 'ЕГГ0Г', hit, context);
    assert.equal(number(calc, '6'), mask(nextPosition), context);
  }
}
console.log('Submarine: detection distinguishes reaching a limit from a blocked step.');

function checkEnemy(position, depth, movement, q, render = false) {
  // At cell 5, RD=4001 makes the square-root argument
  // -1 + 2*frac(q) + abs(movement)/2: zero is safe, negative means a hit.
  const calc = calculator({ C: movement, 5: q, 6: mask(position), 7: 1 + depth },
    'grad', render ? entry.control : entry.frame);
  let next = position === 0 ? (q < 0.5 ? 1 : 0) : position + 1 + Math.trunc(q);
  if (next > 8) next = 0;
  const vulnerable = movement !== 0 || depth < 2;
  const hit = position !== 0 && vulnerable &&
    (next >= 6 || next === 5 && q % 1 < (movement !== 0 ? 0.25 : 0.5));
  const display = run(calc, entry.enemy);
  const context = `position=${position}, depth=${depth}, movement=${movement}, q=${q}`;
  assert.equal(display === 'ЕГГ0Г', hit, context);
  assert.equal(number(calc, '6'), mask(next), context);
  assert.equal(number(calc, '8'), 10000000, 'flow selector must remain constant');
  assert.equal(calc.readRegister('E'), `${initial.E},`, 'glyph/address word must survive indirect flow');
  if (!hit) {
    assert.equal(calc.programCounter(), render ? '29' : '01', context);
    if (render) assert.equal(display, screen(depth, next, 20), context);
  }
}

for (let position = 0; position <= 8; position++) {
  for (const q of [0.125, 0.375, 0.625, 0.875, 1.125, 1.375, 1.625, 1.875]) {
    checkEnemy(position, 1, 0, q);
  }
  for (const depth of [2, 3]) {
    for (const q of [0.125, 1.125]) checkEnemy(position, depth, 0, q, true);
  }
}
// Check both sides of every hit threshold separately for slow and fast arrival.
for (const [position, speedPart] of [[4, 0], [3, 1]]) {
  for (const fraction of [0, 0.249999, 0.25, 0.499999, 0.5, 0.999999]) {
    checkEnemy(position, 0, 0, speedPart + fraction);
    for (const movement of [-1, 1]) {
      checkEnemy(position, 2, movement, speedPart + fraction);
    }
  }
}
for (const q of [0, 0.499999, 0.5, 1.999999]) checkEnemy(0, 0, 0, q, true);
// Exercise the compound E word through a moving-boat jump, then render again.
for (const movement of [-1, 1]) checkEnemy(1, 2, movement, 0.75, true);
console.log('Submarine: spawning, both speeds, conditional hit chances and complete exits OK.');

{
  // Retain the real RNG state across updates at constant oxygen: spawning and
  // both speeds must remain possible even when the supply no longer changes.
  const calc = calculator(oxygenState(50), 'grad', entry.enemy);
  const samples = [];
  for (let frame = 0; frame < 24; frame++) {
    calc.angleMode = modes.grad;
    run(calc, entry.control);
    assert.equal(oxygen(calc), 50);
    assert.equal(number(calc, '0'), 50);
    samples.push(number(calc, '5'));
  }
  assert.ok(samples.every(q => q >= 0 && q < 2));
  assert.ok(new Set(samples).size >= 12, 'RNG must keep advancing at constant oxygen');
  assert.ok(samples.some(q => q < 0.5), 'spawning must remain possible at full oxygen');
  assert.ok(samples.some(q => q >= 1), 'fast movement must remain possible');
}

{
  // No patched STOPs: the entire delivered program must continue cycling from
  // a full tank and an empty sea until the idle surface boat is eventually hit.
  const calc = calculator(oxygenState(50));
  calc.press('В/О').pressSequence(['БП', '0', '0', 'С/П']);
  calc.runFrames(1500);
  assert.equal(calc.displayText(), 'ЕГГ0Г');
  assert.deepEqual(calc.readProgramCodes(parsed.codes.length), parsed.codes);
  checks++;
}

for (const [mode, depth, finalAir] of [['deg', 0, 50], ['rad', 3, 0]]) {
  // Unmodified listing, no injected random values: holding outward at the
  // surface eventually loses to a destroyer; holding at the bottom exhausts
  // oxygen without indexing beyond the last depth row or becoming visible.
  const calc = calculator({ ...oxygenState(50), 7: 1 + depth }, mode);
  calc.press('В/О').pressSequence(['БП', '0', '0', 'С/П']);
  calc.runFrames(1500);
  assert.equal(calc.displayText(), 'ЕГГ0Г');
  assert.equal(number(calc, '7'), 1 + depth);
  assert.equal(number(calc, 'C'), 0);
  assert.equal(oxygen(calc), finalAir);
  assert.deepEqual(calc.readProgramCodes(parsed.codes.length), parsed.codes);
  checks++;
}

console.log(`Submarine verification OK: ${parsed.codes.length} cells, ${checks} ROM scenarios.`);
