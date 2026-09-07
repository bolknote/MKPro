#!/usr/bin/env node
'use strict';

// Keyboard experiments, not a compiler storage primitive. See
// docs/23-pustyshka-hidden-memory-experiment.md for the supported observations.
const assert = require('node:assert/strict');
const { MK61 } = require('./mk61.cjs');

const EMPTY_KEYS = {
  classic: ['Cx', 'K', '7', 'ВП', 'K', '0'],
  error: ['Cx', 'K', '-', 'ВП', 'K', '0'],
  logical: ['Cx', 'K', 'Cx', 'K', '8'], // Cx; К ИНВ; К {x}
};
const READ_KEYS = [Array(6).fill('В↑'), ['↔', '↔'], ['↔']];

function press(calc, keys, idleFrames = 12) {
  for (const key of keys) {
    calc.press(key);
    // press() only provides one release frame. In particular, an error takes
    // several more frames to finish. Sending ВП too early gives a false failure.
    calc.runFrames(idleFrames);
  }
}

function input(calc, value) {
  const match = String(value).match(/^(-?)(\d+(?:[.,]\d+)?)(?:[eE]([+-]?\d+))?$/);
  assert.ok(match, `Unsupported probe number: ${value}`);
  const keys = ['Cx', ...match[2].replace(',', '.')];
  if (match[1]) keys.push('/-/');
  if (match[3] !== undefined) {
    const exponent = Number(match[3]);
    keys.push('ВП', ...String(Math.abs(exponent)));
    if (exponent < 0) keys.push('/-/');
  }
  press(calc, keys);
}

function makeEmpty(calc, recipe) {
  press(calc, EMPTY_KEYS[recipe]);
  return calc.displayText();
}

function write(calc, value, recipe) {
  input(calc, value);
  press(calc, ['X→П', '9']);
  const entered = calc.readRegister('9');
  const empty = makeEmpty(calc, recipe);
  press(calc, ['В↑', 'П→X', '9', ...Array(5).fill('В↑')]);
  return { value, entered, empty, hidden: hiddenWords(calc) };
}

// Copy a stopped probe machine, including chip prototypes. This is used only
// to compare reads from identical states, never to install a hidden value.
function clone(value) {
  if (value === null || typeof value !== 'object') return value;
  const result = Array.isArray(value) ? [] : Object.create(Object.getPrototypeOf(value));
  for (const [key, child] of Object.entries(value)) result[key] = clone(child);
  return result;
}

function programChanges(before, after) {
  return after.flatMap((byte, address) => byte === before[address]
    ? [] : [{ address, before: before[address], after: byte }]);
}

function hiddenWords(calc) {
  // Physical locations only at memory phase 1. These are in the numeric-stack
  // lane, outside the five officially mapped X/Y/Z/T/X1 words in this phase.
  calc.syncMemoryPhase(1);
  const chip = calc.extended ? 2 : 1;
  const addresses = calc.extended ? [34, 202, 244] : [160, 202, 244];
  return addresses.map(address => calc.readNumber(chip, address));
}

function visibleRegisters(calc) {
  const names = ['x', 'y', 'z', 't', 'x1',
    ...Array.from({ length: calc.extended ? 15 : 14 }, (_, i) => i.toString(16))];
  return Object.fromEntries(names.map(name => [name, calc.readRegister(name)]));
}

function addressByte(value) {
  return ((Math.floor(value / 10)) << 4) | (value % 10);
}

function workloadProgram(calc) {
  const limit = calc.commandLimit();
  const program = Array(limit).fill(0x54);
  const body = [];
  // Overwrite every ordinary memory register, including the R9 input scratch.
  for (let r = 0; r < (calc.extended ? 15 : 14); r++) {
    const value = r === 0 ? '3' : String(70 + r);
    body.push(0x0d, ...Array.from(value, Number), 0x40 + r);
  }
  // Replace the visible stack, run a loop, and use two nested subroutines.
  body.push(0x0d, 0x01, 0x0e, 0x02, 0x0e, 0x03, 0x0e, 0x04);
  const loop = body.length;
  const outer = limit - 10;
  const inner = limit - 2;
  body.push(0x53, addressByte(outer), 0x5d, addressByte(loop), 0x50);
  assert.ok(body.length < outer);
  program.splice(0, body.length, ...body);
  program.splice(outer, 8, 0x0d, 0x02, 0x0e, 0x03, 0x10,
    0x53, addressByte(inner), 0x52);
  program.splice(inner, 2, 0x22, 0x52);
  return program;
}

function runWorkload(calc) {
  const program = workloadProgram(calc);
  assert.deepEqual(calc.loadProgram(program).diagnostics, []);
  press(calc, ['В/О', 'С/П']);
  const run = calc.runUntilStable({ maxFrames: 2000, stableFrames: 8 });
  assert.equal(run.stopped, true);
  assert.equal(calc.displayText(), '25,');
  assert.deepEqual(calc.readProgramCodes(), program);
  return { frames: run.frames, registers: visibleRegisters(calc), program };
}

function prepare({ extended = true, recipe = 'logical', values }) {
  const calc = new MK61({ extended });
  // A nonuniform code pattern catches changes that all-zero program memory
  // could conceal. This pattern is never executed.
  const program = Array.from({ length: calc.commandLimit() }, (_, i) => (37 * i + 19) & 255);
  calc.loadProgram(program);
  const writes = values.map(value => write(calc, value, recipe));
  const changes = programChanges(program, calc.readProgramCodes());
  return { calc, writes, changes };
}

function read(calc, recipe, slot) {
  const empty = makeEmpty(calc, recipe);
  press(calc, READ_KEYS[slot]);
  return { empty, value: calc.displayText() };
}

function isolatedReads(calc, recipe) {
  const program = calc.readProgramCodes();
  return READ_KEYS.map((_, slot) => {
    const copy = clone(calc);
    const result = read(copy, recipe, slot);
    return { ...result, hidden: hiddenWords(copy),
      changes: programChanges(program, copy.readProgramCodes()) };
  });
}

function main() {
  const report = [];
  const cases = [
    { name: 'mk54-classic-three-six-digit', extended: false, recipe: 'classic',
      values: ['111111', '222222', '333333'], expected: ['111111,', '222222,', '333333,'] },
    { name: 'mk61-unmodified-classic', recipe: 'classic',
      values: ['111111', '222222', '333333'], expected: ['1,', '1,', '333333,'], skipWorkload: true },
    { name: 'mk61-error-replacement', recipe: 'error',
      values: ['111111', '222222', '333333'], expected: ['0,', '0,', '0,'] },
    { name: 'mk61-logical-three-three-digit', recipe: 'logical',
      values: ['123', '456', '789'], expected: ['123,', '456,', '789,'] },
    { name: 'mk61-logical-zero-and-boundary', recipe: 'logical',
      values: ['0', '100', '999'], expected: ['0,', '100,', '999,'] },
    { name: 'mk61-logical-one-eight-digit', recipe: 'logical',
      values: ['12345678'], expected: ['12345678,', '12345678,', '12345678,'] },
    { name: 'mk61-logical-positive-exponent', recipe: 'logical',
      values: ['1.2345678e99'], expected: Array(3).fill('1,2345678 99') },
    { name: 'mk61-logical-negative-fraction-not-recovered', recipe: 'logical',
      values: ['-1.2345678e-42'], expected: ['0,', '0,', ',       -01'] },
    { name: 'mk61-logical-three-four-digit-not-independent', recipe: 'logical',
      values: ['1000', '2000', '3000'], expected: ['2000,', '2000,', '3000,'] },
    { name: 'mk61-logical-three-six-digit-not-independent', recipe: 'logical',
      values: ['111111', '222222', '333333'], expected: ['111111,', '111111,', '111111,'] },
  ];
  for (const scenario of cases) {
    const { calc, writes, changes } = prepare(scenario);
    assert.deepEqual(changes, [], `${scenario.name}: program changed while writing`);
    let workload;
    if (!scenario.skipWorkload) {
      workload = runWorkload(calc);
      const baseline = runWorkload(new MK61({ extended: scenario.extended !== false }));
      assert.deepEqual(workload.registers, baseline.registers,
        `${scenario.name}: hidden data affected ordinary computation`);
    }
    const reads = isolatedReads(calc, scenario.recipe);
    assert.deepEqual(reads.map(result => result.value), scenario.expected, scenario.name);
    for (const result of reads) assert.deepEqual(result.changes, [], scenario.name);
    report.push({ name: scenario.name, writes, workload, reads });
    if (!process.argv.includes('--json')) {
      console.log(`${scenario.name}: ${reads.map(result => result.value).join(' | ')}`);
    }
    if (scenario.name === 'mk61-logical-three-three-digit') {
      const program = calc.readProgramCodes();
      const originalHidden = hiddenWords(calc);

      // A correct display is insufficient: the following store normalizes a
      // remaining empty stack word and writes the oldest value instead.
      const misleading = clone(calc);
      const shown = read(misleading, 'logical', 1);
      press(misleading, ['X→П', '9']);
      const stored = misleading.readRegister('9');
      assert.equal(shown.value, '456,');
      assert.equal(stored, '123,');
      report.push({ name: 'short-read-display-is-not-a-usable-recall', shown, stored });

      // Reproduce the reported code corruption instead of merely assuming it.
      const unsafe = clone(calc);
      const unsafeReads = [0, 1, 2].map(slot => read(unsafe, 'logical', slot));
      const corruption = programChanges(program, unsafe.readProgramCodes());
      assert.ok(corruption.length > 0, 'Uncleaned short reads should reproduce code corruption');
      report.push({ name: 'uncleaned-sequential-reads-corrupt-program',
        reads: unsafeReads, changes: corruption });

      // The full six-enter read gives a usable X and removes the empty words.
      // Appending that value rotates the three short numbers as a FIFO. R8 is
      // a verification sink; R9 is the actual write scratch.
      const queue = clone(calc);
      const rotations = [];
      for (let i = 0; i < 6; i++) {
        const result = read(queue, 'logical', 0);
        press(queue, ['X→П', '8', 'X→П', '9']);
        assert.equal(queue.readRegister('8'), scenario.expected[i % 3]);
        assert.equal(result.value, scenario.expected[i % 3]);
        makeEmpty(queue, 'logical');
        press(queue, ['В↑', 'П→X', '9', ...Array(5).fill('В↑')]);
        assert.deepEqual(queue.readProgramCodes(), program);
        rotations.push({ value: result.value, hidden: hiddenWords(queue) });
        if (i % 3 === 2) assert.deepEqual(hiddenWords(queue), originalHidden);
      }
      report.push({ name: 'three-short-numbers-two-fifo-cycles', rotations,
        changedProgramBytes: 0 });
      if (!process.argv.includes('--json')) {
        console.log(`short reads: wrong stored value and ${corruption.length} changed program bytes reproduced`);
        console.log(`FIFO reads: ${rotations.map(result => result.value).join(' | ')}; program unchanged`);
      }

      // Calculator-mode key sequences must not be assumed equivalent to a
      // continuous program: X2 synchronization and normalization differ.
      const automatic = clone(calc);
      automatic.loadProgram([0x0d, 0x3a, 0x35,
        0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x0e, 0x48, 0x50]);
      press(automatic, ['В/О', 'С/П']);
      const run = automatic.runUntilStable({ maxFrames: 200, stableFrames: 8 });
      const sink = automatic.readRegister('8');
      assert.equal(run.stopped, false, 'Straight program-mode substitution unexpectedly stopped');
      assert.notEqual(sink, '123,', 'Straight program-mode substitution unexpectedly recalled 123');
      report.push({ name: 'automatic-oldest-read-does-not-complete', run, sink });
    }
    if (scenario.name === 'mk61-logical-one-eight-digit') {
      const repeat = clone(calc);
      const program = repeat.readProgramCodes();
      for (let i = 0; i < 3; i++) {
        assert.equal(read(repeat, 'logical', 0).value, '12345678,');
        press(repeat, ['X→П', '8']);
        assert.equal(repeat.readRegister('8'), '12345678,');
        assert.deepEqual(repeat.readProgramCodes(), program);
      }
      report.push({ name: 'one-eight-digit-number-three-usable-reads',
        value: '12345678,', changedProgramBytes: 0 });
    }
  }
  if (process.argv.includes('--json')) console.log(JSON.stringify(report, null, 2));
  else console.log('All asserted observations reproduced. Emulator results do not establish a general compiler storage contract.');
}

if (require.main === module) main();
module.exports = { MK61, press, input, makeEmpty, write, read, clone, hiddenWords,
  programChanges, visibleRegisters, prepare, isolatedReads, runWorkload };
