#!/usr/bin/env node
'use strict';

// Observe the stock ROM's use of M2/Ms. Marker payloads are explicitly
// HOST-INJECTED controls, not new calculator write/read protocols.
const assert = require('node:assert/strict');
const { MK61, press, clone, runWorkload } = require('./probe-pustyshka.cjs');
const { EXTRA, extraWords } = require('./probe-pustyshka-capacity.cjs');
const { withHelpers, run } = require('./probe-pustyshka-program.cjs');

const PACKETS = ['Y', 'Z', 'T', 'U', 'V', 'W', 'Q', 'P7', 'P8', 'P9', 'P10',
  'marker', 'control', 'X1', 'X'];
const LOCATIONS = [[3, 34], [2, 118], [2, 160], [2, 202], [2, 244], [2, 34],
  [2, 76], [1, 118], [1, 160], [1, 202], [1, 244], [1, 34], [1, 76], [5, 34], [4, 34]];
// At the reset transport phase, before powerOn() advances the first frame.
// These locations circulate into EXTRA at phase 1 after that frame.
const BOOT_MS = [[1, 76], [1, 118], [1, 160], [1, 202], [1, 244],
  [5, 34], [4, 34], [3, 34]];
const REGISTERS = ['x', 'y', 'z', 't', 'x1', ...'0123456789abcde'];

function laneWords(calc) {
  calc.syncMemoryPhase(1);
  return Object.fromEntries(PACKETS.map((name, i) => {
    const [chip, end] = LOCATIONS[i];
    return [name, Array.from({ length: 14 }, (_, digit) =>
      calc.memoryForChip(chip)[end + 6 - digit * 3].toString(16)).join('')];
  }));
}

function msPages(calc) {
  const words = laneWords(calc);
  return Object.fromEntries(Object.keys(EXTRA).map(name => [name, words[name]]));
}

function seedMs(calc, { boot = false, addressDigits = false } = {}) {
  if (!boot) calc.syncMemoryPhase(1);
  const expected = {};
  for (const [i, [chip, end]] of (boot ? BOOT_MS : Object.values(EXTRA)).entries()) {
    const raw = `${addressDigits ? `${i + 1}${8 - i}` : '00'}0020${901 + i}00000`;
    expected[Object.keys(EXTRA)[i]] = raw;
    for (let digit = 0; digit < 14; digit++) {
      calc.memoryForChip(chip)[end + 6 - digit * 3] = parseInt(raw[digit], 16);
    }
  }
  return expected;
}

function traceLane(calc, action, { boot = false } = {}) {
  if (!boot) calc.syncMemoryPhase(1);
  const events = new Map();
  const chips = [calc.ik1302, calc.ik1303, calc.ik1306];
  for (const [index, chip] of chips.entries()) {
    const tick = chip.tick;
    const order = chip.executeMicroOrder;
    let cycles = 0, packet, ip;
    chip.tick = function () {
      if (this.tickIndex === 0) {
        packet = PACKETS[(cycles++ + (boot ? 10 : 0) + 15 - index) % 15];
        ip = this.r[36] + 16 * this.r[39];
      }
      return tick.call(this);
    };
    chip.executeMicroOrder = function (op, acc) {
      // Order 24 writes S into the outgoing M cell. Ordinary serial motion at
      // the end of tick() is not a logical write and is deliberately excluded.
      if ((op === 1 || op === 24) && this.tickIndex % 3 === 1) {
        const key = [index, packet, ip.toString(16), op === 1 ? 'read' : 'write',
          this.tickIndex].join('/');
        const event = events.get(key) || { key, count: 0, changed: 0 };
        event.count++;
        if (op === 24 && this.memory[this.tickIndex] !== this.s) event.changed++;
        events.set(key, event);
      }
      return order.call(this, op, acc);
    };
  }
  try {
    action(calc);
  } finally {
    for (const chip of chips) {
      delete chip.tick;
      delete chip.executeMicroOrder;
    }
  }
  return [...events.values()];
}

function hiddenWrites(events) {
  return events.filter(event => {
    const [, packet, , kind] = event.key.split('/');
    return Object.hasOwn(EXTRA, packet) && kind === 'write';
  });
}

function publicState(calc) {
  return { pc: calc.programCounter(), display: calc.displayText(),
    registers: Object.fromEntries(REGISTERS.map(name => [name, calc.readRegister(name)])),
    program: calc.readProgramCodes() };
}

function stoppedProgram(body) {
  assert.ok(body.length <= 105);
  return [...body, ...Array(105 - body.length).fill(0x54)];
}

function execute(calc, program) {
  assert.deepEqual(calc.loadProgram(program).diagnostics, []);
  press(calc, ['В/О', 'С/П']);
  const result = calc.runUntilStable({ maxFrames: 200, stableFrames: 8 });
  assert.ok(result.stopped);
  assert.deepEqual(calc.readProgramCodes(), program);
}

function checkStartup() {
  const normal = new MK61();
  assert.deepEqual(Object.values(extraWords(normal)), Array(8).fill('000000000000'));
  const seeded = new MK61();
  seeded.powerOff();
  const expected = seedMs(seeded, { boot: true, addressDigits: true });
  const events = traceLane(seeded, calc => calc.powerOn().runFrames(4), { boot: true });
  assert.deepEqual(msPages(seeded), expected);
  assert.deepEqual(hiddenWrites(events), []);
  assert.equal(seeded.displayText(), '0,');
  return { defaultMs: extraWords(normal), hostSeededBeforeBoot: msPages(seeded),
    writesToMsDuringBoot: 0 };
}

function readOnlyProgram(profile) {
  const program = withHelpers(profile);
  program.splice(0, 3, 0x53, 0x06, 0x50);
  return program;
}

function checkReads() {
  const reports = [];
  for (const history of ['power-on', 'ordinary-workload', 'host-seeded-before-boot']) {
    const initial = new MK61();
    if (history === 'ordinary-workload') runWorkload(initial);
    if (history === 'host-seeded-before-boot') {
      initial.powerOff();
      seedMs(initial, { boot: true });
      initial.powerOn();
    }
    const before = msPages(initial);
    for (const profile of ['scalar', 'triple']) {
      const calc = clone(initial);
      const outputs = [];
      for (let repeat = 0; repeat < 2; repeat++) {
        const events = traceLane(calc, q => run(q, readOnlyProgram(profile), 2));
        assert.deepEqual(hiddenWrites(events), []);
        outputs.push(['x', 'y', 'z'].map(name => calc.readRegister(name)));
        assert.deepEqual(msPages(calc), before);
      }
      const wanted = history === 'host-seeded-before-boot'
        ? profile === 'triple' ? ['903,', '902,', '901,'] : ['901,']
        : ['0,', '0,', '0,'];
      // The scalar ABI promises only X, so do not impose its incidental Y/Z.
      for (const output of outputs) assert.deepEqual(output.slice(0, profile === 'scalar' ? 1 : 3),
        wanted.slice(0, profile === 'scalar' ? 1 : 3));
      reports.push({ history, profile, outputs, msUnchanged: true, msWrites: 0 });
    }
  }
  return reports;
}

function checkOrdinaryOperations() {
  const reports = [];
  for (const x of ['0.5', '-2']) {
    for (let opcode = 0; opcode <= 0x3e; opcode++) {
      const normal = new MK61();
      for (const name of REGISTERS) normal.setRegister(name, name === 'x' ? x : '2');
      const seeded = clone(normal);
      const before = seedMs(seeded, { addressDigits: true });
      const program = stoppedProgram([opcode, 0x50]);
      execute(normal, program);
      const events = traceLane(seeded, calc => execute(calc, program));
      assert.deepEqual(publicState(seeded), publicState(normal), `opcode ${opcode.toString(16)}, X=${x}`);
      assert.deepEqual(msPages(seeded), before, `Ms retention: ${opcode.toString(16)}, X=${x}`);
      assert.deepEqual(hiddenWrites(events), [], `Ms writes: ${opcode.toString(16)}, X=${x}`);
      reports.push({ opcode: opcode.toString(16).padStart(2, '0'), x,
        pc: seeded.programCounter(), display: seeded.displayText() });
    }
  }
  return reports;
}

function checkControlAndWriter() {
  const calc = new MK61();
  const before = seedMs(calc, { addressDigits: true });
  const events = traceLane(calc, runWorkload);
  assert.deepEqual(msPages(calc), before);
  assert.deepEqual(hiddenWrites(events), []);
  const changedPackets = [...new Set(events.filter(e => e.changed).map(e => e.key.split('/')[1]))];
  assert.ok(changedPackets.includes('control'));

  const program = withHelpers('triple');
  program.splice(0, 4, 0x6e, 0x53, 0x21, 0x50);
  calc.setRegister('e', '123');
  const writeEvents = traceLane(calc, q => run(q, program, 3));
  const after = extraWords(calc);
  const afterPages = msPages(calc);
  for (const name of Object.keys(EXTRA)) assert.equal(afterPages[name].slice(0, 2), before[name].slice(0, 2));
  assert.deepEqual(Object.values(after), ['002012300000', '002090100000',
    '002090200000', '002090300000', '002090500000', '002090600000',
    '002090700000', '002090800000']);
  return { ordinaryChangedPackets: changedPackets, ordinaryMsWrites: 0,
    writerAfter: after, writerMsWrites: hiddenWrites(writeEvents) };
}

function checkChannel() {
  const calc = new MK61();
  calc.setRegister('x', '3');
  calc.setRegister('y', '2');
  calc.syncMemoryPhase(1);
  const chip = calc.ik1302, tick = chip.tick, changes = [];
  let cycles = 0;
  chip.tick = function () {
    if (this.tickIndex === 0) {
      if (PACKETS[cycles % 15] === 'control') {
        const value = this.memory[34] * 16 + this.memory[31];
        if (changes.at(-1)?.value !== value) changes.push({ cycles, value });
      }
      cycles++;
    }
    return tick.call(this);
  };
  try {
    press(calc, ['+']);
  } finally {
    delete chip.tick;
  }
  assert.deepEqual(changes.map(change => change.value), [0xff, 0x10, 0x1f]);
  assert.equal(calc.displayText(), '5,');
  return changes.map(change => ({ ...change, value: change.value.toString(16) }));
}

function main() {
  console.log('Startup:', JSON.stringify(checkStartup()));
  console.log('Read without a calculator write:', JSON.stringify(checkReads()));
  const operations = checkOrdinaryOperations();
  console.log(`${operations.length} opcode/input cases: same visible state and program; `
    + 'all 56 Ms bytes retained; no ROM writes to Ms.');
  if (process.argv.includes('--json')) console.log(JSON.stringify(operations));
  console.log('Control lane and real writer:', JSON.stringify(checkControlAndWriter()));
  console.log('Addition channel handshake:', JSON.stringify(checkChannel()));
}

if (require.main === module) main();
module.exports = { PACKETS, LOCATIONS, BOOT_MS, laneWords, msPages, seedMs, traceLane,
  hiddenWrites, checkStartup, checkReads, checkOrdinaryOperations, checkControlAndWriter, checkChannel };
