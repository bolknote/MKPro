#!/usr/bin/env node
'use strict';

// Capacity observations, not a general four-value read/write ABI.
// Real writes below use the original ROM. Host-injected canaries are confined
// to the explicitly labelled retention experiment at the end.
const assert = require('node:assert/strict');
const { MK61, press, clone, runWorkload } = require('./probe-pustyshka.cjs');
const { withHelpers, ordinaryRaw, run } = require('./probe-pustyshka-program.cjs');

// Physical locations at memory phase 1, in arrival order after T.
// Q and P7..P10 are local names, not documented calculator registers.
const EXTRA = {
  U: [2, 202], V: [2, 244], W: [2, 34], Q: [2, 76],
  P7: [1, 118], P8: [1, 160], P9: [1, 202], P10: [1, 244],
};

function verifyLayout() {
  const calc = new MK61();
  const codeCells = new Set();
  const registerCells = new Set();
  for (let command = 0; command < 105; command++) {
    const [chip, end] = calc.commandAddress(command, 1);
    for (const position of [end, end - 3]) codeCells.add(`${chip}:${position}`);
  }
  for (let register = 0; register < 15; register++) {
    const [chip, end] = calc.commandAddress(register * 7, 1);
    for (let digit = 0; digit < 12; digit++) {
      registerCells.add(`${chip}:${end - 8 - digit * 3}`);
    }
  }
  assert.equal(codeCells.size, 210);
  assert.equal(registerCells.size, 180);
  const extraCells = new Set();
  for (const [chip, end] of Object.values(EXTRA)) {
    for (let digit = 0; digit < 12; digit++) {
      const cell = `${chip}:${end - digit * 3}`;
      assert.ok(!codeCells.has(cell), `Hidden word overlaps program cell ${cell}`);
      assert.ok(!registerCells.has(cell), `Hidden word overlaps register cell ${cell}`);
      assert.ok(!extraCells.has(cell), `Hidden words overlap at ${cell}`);
      extraCells.add(cell);
    }
  }
  assert.equal(extraCells.size, 96);
}

function extraWords(calc) {
  calc.syncMemoryPhase(1);
  return Object.fromEntries(Object.entries(EXTRA).map(([name, [chip, end]]) => [name,
    Array.from({ length: 12 }, (_, i) => calc.memoryForChip(chip)[end - i * 3]
      .toString(16)).join('')]));
}

function storeAndErase(values) {
  assert.ok(values.length >= 4 && values.length <= 5);
  const calc = new MK61();
  const expected = values.map((value, i) => {
    const register = (10 + i).toString(16);
    calc.setRegister(register, value);
    return { text: calc.readRegister(register), raw: ordinaryRaw(calc, register) };
  });
  const program = withHelpers('triple');
  program.splice(0, 2, 0x51, 0x40);
  const body = values.flatMap((_, i) => [0x6a + i, 0x53, 0x21]);
  body.push(0x0d, ...Array.from({ length: 15 }, (_, i) => 0x40 + i));
  body.push(0x02, 0x0e, 0x03, 0x10, 0x22, 0x50);
  program.splice(40, body.length, ...body);
  run(calc, program, 40 + body.length - 1);
  for (let i = 0; i < 15; i++) assert.equal(calc.readRegister(i.toString(16)), '0,');
  assert.equal(calc.readRegister('x'), '25,');
  const words = extraWords(calc);
  assert.deepEqual(Object.values(words).slice(0, 4), expected.slice(-4)
    .reverse().map(value => value.raw));
  assert.deepEqual(Object.values(words).slice(4), Array(4).fill('000000000000'));

  // Replace the entire program, all ordinary registers and the visible stack;
  // execute arithmetic, a loop, and nested subroutine calls.
  runWorkload(calc);
  assert.deepEqual(extraWords(calc), words);
  return { calc, expected, words };
}

function readFourth(calc) {
  // A deliberately phase-pinned laboratory recipe. Its incomplete domain and
  // dirty hidden state prevent presenting it as a usable storage primitive.
  calc.syncMemoryPhase(1);
  press(calc, ['Cx', 'K', 'Cx', 'K', '8', 'П→X', '9', 'В↑', 'X→П', '8']);
  return calc.readRegister('8');
}

function fourValueExample() {
  const { calc, expected } = storeAndErase(['111', '222', '333', '444']);
  const program = withHelpers('triple');
  program.splice(0, 2, 0x51, 0x40);
  program.splice(40, 8, 0x53, 0x06, 0x40, 0x25, 0x41, 0x25, 0x42, 0x50);
  run(calc, program, 47);
  calc.syncMemoryPhase(1);
  press(calc, ['Cx', 'K', 'Cx', 'K', '8', 'П→X', '9', 'В↑', 'X→П', '8',
    'В↑', '+', '+', '+']);
  for (const [i, register] of ['8', '0', '1', '2'].entries()) {
    assert.equal(calc.readRegister(register), expected[i].text);
    assert.equal(ordinaryRaw(calc, register), expected[i].raw);
  }
  assert.deepEqual(calc.readProgramCodes(), program);
  assert.deepEqual(Object.values(extraWords(calc)), Array(8).fill('000000000000'));
  runWorkload(calc);
}

function retentionOnlyCanaries() {
  const normal = new MK61();
  normal.setRegister('e', '908');
  const labelled = clone(normal);
  // HOST INJECTION: this tests whether ordinary operations overwrite these
  // fields. It is explicitly not evidence of a keyboard writer for eight words.
  for (const [i, [chip, end]] of Object.values(EXTRA).entries()) {
    const raw = `0020${901 + i}00000`;
    for (let digit = 0; digit < 12; digit++) {
      labelled.memoryForChip(chip)[end - digit * 3] = parseInt(raw[digit], 16);
    }
  }
  const before = extraWords(labelled);
  for (const calc of [normal, labelled]) runWorkload(calc);
  assert.deepEqual(extraWords(labelled), before);
  const math = Array(105).fill(0x54);
  math.splice(0, 10, 0x0d, 0x02, 0x21, 0x22, 0x18, 0x16, 0x1c, 0x1d, 0x1b, 0x50);
  for (const calc of [normal, labelled]) run(calc, math, 9);
  assert.deepEqual(extraWords(labelled), before);
  for (const name of ['x', 'y', 'z', 't', 'x1', ...'0123456789abcde']) {
    assert.equal(labelled.readRegister(name), normal.readRegister(name));
  }
  return before;
}

function main() {
  verifyLayout();
  console.log('All 96 hidden numeric nibbles are disjoint from the 105 program bytes '
    + 'and 15 ordinary numeric registers.');
  for (const values of [
    ['111', '222', '333', '444'], ['123', '456', '789', '999'],
    ['0', '1', '100', '999'], ['999', '100', '10', '0'],
    ['12345678', '-12345678', '1.2345678e99', '-1.2345678e-42'],
    ['111', '222', '333', '444', '555'],
  ]) {
    const result = storeAndErase(values);
    console.log(`${values.join(' / ')} -> ${JSON.stringify(result.words)}`);
  }
  fourValueExample();
  console.log('111 / 222 / 333 / 444: all four recovered, hidden fields cleared, '
    + 'program unchanged, subsequent workload passed.');
  for (const oldest of ['0', '1', '9', '10', '123', '12345678']) {
    const { calc, expected } = storeAndErase([oldest, '456', '789', '999']);
    const value = readFourth(calc);
    assert.equal(value, Number(oldest) < 10 ? '789,' : expected[0].text);
    console.log(`Experimental fourth read ${oldest} -> ${value} `
      + (value === expected[0].text ? '(retrieved; dirty hidden state)' : '(COUNTEREXAMPLE)'));
  }
  console.log('HOST-INJECTED retention canaries:', retentionOnlyCanaries());
}

if (require.main === module) main();
module.exports = { EXTRA, verifyLayout, extraWords, storeAndErase, readFourth, fourValueExample };
