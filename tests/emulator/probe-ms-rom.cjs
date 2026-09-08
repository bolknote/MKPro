#!/usr/bin/env node
'use strict';

// Inspect the stock ROM's reserved K1/K2 dispatch and surviving M2 transfers.
// Isolated transfers deliberately set the INTERNAL instruction pointer and
// scratch data from the host. They are not calculator keyboard protocols.
const assert = require('node:assert/strict');
const ROM = require('./rom.cjs');
const { MK61, press, clone } = require('./probe-pustyshka.cjs');
const { seedMs, msPages, traceLane, hiddenWrites } = require('./probe-ms-usage.cjs');

const REGISTERS = ['x', 'y', 'z', 't', 'x1', ...'0123456789abcde'];
const TRANSFERS = [0xbb, 0xbc, 0xc7, 0xc8];
const hex = value => value.toString(16).toUpperCase().padStart(2, '0');

function checkKeyboardCodes() {
  const calc = new MK61();
  press(calc, ['F', 'ВП', 'K', '1', 'K', '2', 'K', '0']);
  const codes = calc.readProgramCodes().slice(0, 3);
  assert.deepEqual(codes, [0x55, 0x56, 0x54]);
  return { keys: ['K 1', 'K 2', 'K 0'], codes: codes.map(hex) };
}

function checkRomWords() {
  // Table 4.4, printed p.154 of Trohimenko et al. (1990).
  // These are IK1302 ROM addresses, NOT user-program opcodes.
  const expected = {
    BC: [0x3a, 0x3a, 0x1a, 0],
    C7: [0x34, 0x34, 0x1e, 0],
    C8: [0x38, 0x38, 0x19, 0],
    C9: [0x39, 0x39, 0x18, 0],
  };
  return Object.entries(expected).map(([address, fields]) => {
    const word = ROM.ИК1302.команды[parseInt(address, 16)];
    const actual = [word & 0x7f, (word >>> 7) & 0x7f,
      (word >>> 14) & 0xff, (word >>> 22) & 1];
    assert.deepEqual(actual, fields, `Book/ROM comparison at ${address}`);
    return { address, word: word.toString(16).toUpperCase().padStart(6, '0'),
      sync: actual.slice(0, 3).map(hex), mode: actual[3] };
  });
}

function traceDispatch(calc, action) {
  calc.syncMemoryPhase(1);
  const ips = [];
  const chip = calc.ik1302;
  const tick = chip.tick;
  chip.tick = function () {
    if (this.tickIndex === 0) ips.push(this.r[36] + 16 * this.r[39]);
    return tick.call(this);
  };
  let events;
  try {
    // traceLane wraps this observer and removes both wrappers on completion.
    events = traceLane(calc, action);
  } finally {
    delete chip.tick;
  }
  const start = ips.indexOf(0x1f);
  assert.notEqual(start, -1, 'Instruction dispatch was reached');
  const end = ips.indexOf(0xd3, start);
  assert.ok(end > start, 'Shared completion path was reached');
  assert.deepEqual(ips.filter(ip => TRANSFERS.includes(ip)), []);
  assert.deepEqual(hiddenWrites(events), []);
  return ips.slice(start, end + 1).map(hex);
}

function publicState(calc) {
  return { pc: calc.programCounter(), display: calc.displayText(),
    registers: REGISTERS.map(name => calc.readRegister(name)) };
}

function checkReservedDispatch() {
  const initial = new MK61();
  REGISTERS.forEach((name, i) => initial.setRegister(name, String(123 + i)));
  const expectedMs = seedMs(initial, { addressDigits: true });
  const reports = [];
  const expectedRoutes = [
    ['1F', '14', 'D3'],
    ['1F', '15', '0F', 'D3'],
    ['1F', '16', '3F', 'D3'],
  ];
  for (const mode of ['keyboard', 'program']) {
    let control;
    for (const digit of [0, 1, 2]) {
      const calc = clone(initial);
      const opcode = 0x54 + digit;
      // Always check all 105 program bytes, including the direct-key case.
      const program = [mode === 'program' ? opcode : 0x54, 0x50,
        ...Array(103).fill(0x54)];
      assert.deepEqual(calc.loadProgram(program).diagnostics, []);
      if (mode === 'keyboard') press(calc, ['K']);
      const route = traceDispatch(calc, q => {
        if (mode === 'keyboard') press(q, [String(digit)]);
        else {
          press(q, ['В/О', 'С/П']);
          assert.ok(q.runUntilStable({ maxFrames: 200, stableFrames: 8 }).stopped);
        }
      });
      assert.deepEqual(route, expectedRoutes[digit]);
      assert.deepEqual(msPages(calc), expectedMs);
      assert.deepEqual(calc.readProgramCodes(), program);
      const state = publicState(calc);
      if (digit === 0) control = state;
      else assert.deepEqual(state, control, `${mode}: K${digit} vs NOP`);
      if (mode === 'program') assert.equal(state.pc, '02');
      reports.push({ mode, opcode: hex(opcode), route, display: state.display,
        pc: state.pc, msWrites: 0, msTransferVisits: 0 });
    }
  }
  return reports;
}

function isolatedTransfer(ip, { cycles = 1, salt = 0 } = {}) {
  const calc = new MK61();
  calc.reset();
  const chip = calc.ik1302;
  const memory = Array.from({ length: 14 }, (_, i) => (i + 1 + salt) & 15);
  const register = Array.from({ length: 14 }, (_, i) => (i + 5 + salt) & 15);
  const registerLane = ip === 0xc7 ? 0 : 1;
  for (let i = 0; i < 14; i++) {
    chip.memory[1 + 3 * i] = memory[i];
    chip.r[registerLane + 3 * i] = register[i];
  }
  // Select a stock internal instruction; the ROM arrays remain unchanged.
  chip.r[36] = ip & 15;
  chip.r[39] = ip >>> 4;
  chip.s = (10 + salt) & 15;
  const outputs = [], addresses = [];
  for (let cycle = 0; cycle < cycles; cycle++) {
    addresses.push(chip.r[36] + 16 * chip.r[39]);
    const outgoing = [];
    for (let i = 0; i < 42; i++) {
      chip.tick();
      outgoing.push(chip.output);
    }
    // Inspect the outgoing serial cells, not M after replacement by input.
    outputs.push(outgoing.filter((_, i) => i % 3 === 1));
  }
  return { memory, register, outputs, addresses,
    r1: Array.from({ length: 12 }, (_, i) => chip.r[3 * i]),
    r2: Array.from({ length: 14 }, (_, i) => chip.r[1 + 3 * i]) };
}

function checkIsolatedTransfers() {
  for (let salt = 0; salt < 16; salt++) {
    const swap = isolatedTransfer(0xc7, { salt });
    assert.deepEqual(swap.r1, swap.memory.slice(0, 12));
    assert.deepEqual(swap.outputs[0], [...swap.register.slice(0, 12),
      ...swap.memory.slice(12)]);

    const read = isolatedTransfer(0xc8, { salt });
    assert.deepEqual(read.r2, read.memory);
    assert.deepEqual(read.outputs[0], read.memory);

    // BC streams through S with a one-digit delay. BB provides the necessary
    // R2 rotation and S preload; BC alone is NOT an aligned full-page store.
    const write = isolatedTransfer(0xbb, { cycles: 2, salt });
    assert.deepEqual(write.addresses, [0xbb, 0xbc]);
    assert.deepEqual(write.outputs[0], write.memory);
    assert.deepEqual(write.outputs[1], write.register);
  }
  return { hostSelectedInternalInstructions: true, patternsPerTransfer: 16,
    C7: 'swap the 12 numeric tetras of R1 and the current M2 page',
    C8: 'copy all 14 tetras of the current M2 page to R2',
    'BB -> BC': 'write all 14 original R2 tetras to the current M2 page' };
}

function composeExchange(kind, ordinary, reserve) {
  // Laboratory composition only: the HOST selects internal entry addresses
  // and presents the target page at each transfer. This deliberately does not
  // implement or validate the missing page-selection and eight-page loop.
  const calc = new MK61();
  calc.reset();
  const chip = calc.ik1302;
  const setPage = (lane, word) => word.forEach((tetra, i) => {
    chip.memory[lane + 3 * i] = tetra;
  });
  const step = ip => {
    if (ip !== undefined) {
      chip.r[36] = ip & 15;
      chip.r[39] = ip >>> 4;
    }
    const out = [];
    for (let i = 0; i < 42; i++) {
      chip.tick();
      out.push(chip.output);
    }
    return [0, 1, 2].map(lane => out.filter((_, i) => i % 3 === lane));
  };
  if (kind === 'data') {
    setPage(0, ordinary);
    const read = step(0xd6);
    assert.deepEqual(read[0], ordinary);
    setPage(1, reserve);
    const exchange = step(0xc7);
    // Like BB before BC, DD aligns R1 and preloads S before D4 writes M1.
    chip.s1 = 4; // Input parameter selecting the D4 continuation, not D6.
    step(0xdd);
    assert.equal(chip.r[36] + 16 * chip.r[39], 0xd4);
    setPage(0, ordinary);
    const write = step();
    return { ordinary: write[0], reserve: exchange[1] };
  }
  assert.equal(kind, 'program');
  setPage(1, reserve);
  const read = step(0xc8);
  assert.deepEqual(read[1], reserve);
  setPage(2, ordinary);
  const exchange = step(0xc9);
  step(0xbb);
  assert.equal(chip.r[36] + 16 * chip.r[39], 0xbc);
  const write = step();
  return { ordinary: exchange[2], reserve: write[1] };
}

function checkExchangeComposition() {
  for (const kind of ['data', 'program']) {
    for (let salt = 0; salt < 16; salt++) {
      const ordinary = Array.from({ length: 14 }, (_, i) => (i + salt) & 15);
      const reserve = Array.from({ length: 14 }, (_, i) => (i + salt + 5) & 15);
      const result = composeExchange(kind, ordinary, reserve);
      if (kind === 'data') {
        assert.deepEqual(result.ordinary, [...reserve.slice(0, 12), ...ordinary.slice(12)]);
        assert.deepEqual(result.reserve, [...ordinary.slice(0, 12), ...reserve.slice(12)]);
      } else {
        assert.deepEqual(result.ordinary, reserve);
        assert.deepEqual(result.reserve, ordinary);
      }
      assert.deepEqual(composeExchange(kind, result.ordinary, result.reserve),
        { ordinary, reserve }, `${kind}: repeating the exchange restores both words`);
    }
  }
  return { hostSelectedPagesAndEntryAddresses: true, patternsPerExchange: 16,
    data: { transfers: ['D6', 'C7', 'DD -> D4'], swappedTetrasPerPage: 12,
      topTwoTetrasPreserved: true },
    program: { transfers: ['C8', 'C9', 'BB -> BC'], swappedTetrasPerPage: 14 },
    repeatedExchangeRestoresBothWords: true };
}

function main() {
  const report = { keyboard: checkKeyboardCodes(), rom: checkRomWords(),
    dispatch: checkReservedDispatch(), isolated: checkIsolatedTransfers(),
    composition: checkExchangeComposition() };
  console.log(JSON.stringify(report, null, 2));
  console.log('PASS: reserved-key dispatch and stock-ROM M2 transfer primitives');
}

if (require.main === module) main();

module.exports = { checkKeyboardCodes, checkRomWords, checkReservedDispatch,
  checkIsolatedTransfers, isolatedTransfer, composeExchange, checkExchangeComposition };
