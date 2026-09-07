#!/usr/bin/env node
'use strict';

// Executable protocols in the original MK-61 ROM. No hidden-memory writes,
// microcode patches, intermediate stops, or keyboard interventions in a run.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, press, programChanges } = require('./probe-pustyshka.cjs');

const WRITE_ADDRESS = 21;
const READ_ADDRESS = 6;
const WRITE = [0x49, 0x0d, 0x3a, 0x35, 0x14, 0x69, 0x25,
  0x0d, 0x0e, 0x0e, 0x0e, 0x10, 0x52];
const READ_TRIPLE = [0x0d, 0x3a, 0x35, 0x14, 0x0e, 0x0e, 0x0e,
  0x0e, 0x00, 0x10, 0x52];
const READ_SCALAR = [0x0d, 0x3a, 0x35, 0x25, 0x00, 0x10, 0x52];
const bcd = number => (Math.floor(number / 10) << 4) | number % 10;
const fixturePath = name => path.join(__dirname, 'fixtures', `pustyshka-${name}.hex`);

function pattern() {
  return Array.from({ length: 105 }, (_, i) => (37 * i + 19) & 255);
}

function withHelpers(profile) {
  const program = pattern();
  program.splice(READ_ADDRESS, profile === 'scalar' ? READ_SCALAR.length : READ_TRIPLE.length,
    ...(profile === 'scalar' ? READ_SCALAR : READ_TRIPLE));
  program.splice(WRITE_ADDRESS, WRITE.length, ...WRITE);
  // A second call level checks that an outstanding user return address survives.
  program.splice(34, 3, 0x53, bcd(WRITE_ADDRESS), 0x52);
  return program;
}

function makeProgram({ profile = 'triple', literal = false, repeats = 3,
  nested = true } = {}) {
  const program = withHelpers(profile);
  program.splice(0, 2, 0x51, 0x40);
  const body = [];
  for (let i = 0; i < 3; i++) {
    if (literal) body.push(0x0d, ...Array.from(['123', '456', '789'][i], Number));
    else body.push(0x6a + i);
    body.push(0x53, nested ? 0x34 : bcd(WRITE_ADDRESS));
  }
  // Erase every ordinary register, including all inputs and the R9 scratch.
  body.push(0x0d, ...Array.from({ length: 15 }, (_, i) => 0x40 + i));
  // Do unrelated work with the visible stack before retrieving any payload.
  body.push(0x02, 0x0e, 0x03, 0x10, 0x22); // (2+3)^2
  for (let i = 0; i < repeats; i++) {
    body.push(0x53, bcd(READ_ADDRESS));
    if (profile === 'scalar') body.push(0x40 + i);
    else body.push(0x40 + i * 3, 0x25, 0x41 + i * 3, 0x25, 0x42 + i * 3);
  }
  body.push(0x50);
  assert.ok(40 + body.length <= 105);
  program.splice(40, body.length, ...body);
  return { program, stop: 40 + body.length - 1, profile, repeats, literal };
}

function rawWord(calc, chip, end) {
  return Array.from({ length: 12 }, (_, i) => calc.memoryForChip(chip)[end - i * 3]
    .toString(16)).join('');
}

function hiddenRaw(calc) {
  calc.syncMemoryPhase(1);
  return [202, 244, 34].map(end => rawWord(calc, 2, end)); // U,V,W
}

function ordinaryRaw(calc, name) {
  calc.syncMemoryPhase(1);
  const index = parseInt(name, 16);
  const [chip, end] = calc.commandAddress(index * 7, 1);
  return rawWord(calc, chip, end - 8);
}

function run(calc, program, expectedStop) {
  assert.deepEqual(calc.loadProgram(program).diagnostics, []);
  press(calc, ['В/О', 'С/П']);
  const result = calc.runUntilStable({ maxFrames: 2000, stableFrames: 8 });
  assert.equal(result.stopped, true);
  if (expectedStop !== undefined) {
    assert.ok(expectedStop < 99);
    assert.equal(calc.programCounter(), String(expectedStop + 1).padStart(2, '0'));
  }
  // Instruction fetching itself temporarily exchanges program packets with
  // processor R via ROM C9. Compare the complete restored ring at the verified
  // final STOP, rather than incorrectly prohibiting ordinary fetch transfers.
  assert.deepEqual(programChanges(program, calc.readProgramCodes()), []);
  return result;
}

function seed(calc, values, dirtyStack = false) {
  for (let i = 0; i < 3; i++) calc.setRegister((10 + i).toString(16), values[i]);
  if (dirtyStack) {
    for (const [name, value] of Object.entries({ x: '-99999999', y: '1.2345678e99',
      z: '-1.2345678e-42', t: '98765432', x1: '-77777777' })) {
      calc.setRegister(name, value);
    }
  }
  return values.map((_, i) => ({ text: calc.readRegister((10 + i).toString(16)),
    raw: ordinaryRaw(calc, (10 + i).toString(16)) }));
}

function checkCase({ profile = 'triple', values, literal = false, dirtyStack = false,
  angleMode = 'rad', nested = true, repeats = 3 }) {
  const calc = new MK61({ angleMode });
  const expected = seed(literal ? new MK61() : calc, values, dirtyStack);
  const specification = makeProgram({ profile, literal, nested, repeats });
  const result = run(calc, specification.program, specification.stop);
  for (let round = 0; round < repeats; round++) {
    for (let i = 0; i < (profile === 'scalar' ? 1 : 3); i++) {
      const sink = (profile === 'scalar' ? round : round * 3 + i).toString(16);
      const source = expected[profile === 'scalar' ? 2 : i];
      assert.equal(calc.readRegister(sink), source.text, `${profile}: R${sink}`);
      assert.equal(ordinaryRaw(calc, sink), source.raw, `${profile}: raw R${sink}`);
    }
  }
  assert.deepEqual(hiddenRaw(calc), expected.map(value => value.raw).reverse());
  assert.equal(calc.readRegister('x1'), profile === 'scalar' ? '0,' : expected[1].text);
  return { profile, values, dirtyStack, angleMode, nested, repeats,
    frames: result.frames, finalPc: calc.programCounter(), changedProgramBytes: 0,
    hidden: hiddenRaw(calc) };
}

function checkAbi() {
  const calc = new MK61();
  const source = ['123', '456', '789'];
  seed(calc, source);
  const setup = makeProgram({ repeats: 1 });
  run(calc, setup.program, setup.stop);
  const hidden = hiddenRaw(calc);
  for (let i = 0; i < 15; i++) calc.setRegister(i.toString(16), String(270 + i));
  seedStackOnly(calc);
  const before = Array.from({ length: 15 }, (_, i) => ordinaryRaw(calc, i.toString(16)));
  let program = withHelpers('triple');
  program.splice(0, 4, 0x6e, 0x53, bcd(WRITE_ADDRESS), 0x50);
  run(calc, program, 3);
  for (let i = 0; i < 15; i++) {
    assert.equal(ordinaryRaw(calc, i.toString(16)), before[i === 9 ? 14 : i]);
  }
  assert.deepEqual(hiddenRaw(calc), [before[14], hidden[0], hidden[1]]);
  for (const name of ['x', 'y', 'z', 't', 'x1']) assert.equal(calc.readRegister(name), '0,');

  for (const profile of ['triple', 'scalar']) {
    seedStackOnly(calc);
    const beforeRead = Array.from({ length: 15 }, (_, i) => ordinaryRaw(calc, i.toString(16)));
    const beforeHidden = hiddenRaw(calc);
    program = withHelpers(profile);
    program.splice(0, 3, 0x53, bcd(READ_ADDRESS), 0x50);
    run(calc, program, 2);
    for (let i = 0; i < 15; i++) assert.equal(ordinaryRaw(calc, i.toString(16)), beforeRead[i]);
    assert.deepEqual(hiddenRaw(calc), beforeHidden);
    assert.equal(calc.readRegister('x1'), '0,');
    if (profile === 'triple') {
      assert.deepEqual(['x', 'y', 'z', 't'].map(name => calc.readRegister(name)),
        ['456,', '789,', '284,', '284,']);
    } else {
      assert.equal(calc.readRegister('x'), '284,');
    }
  }
}

function seedStackOnly(calc) {
  for (const [name, value] of Object.entries({ x: '-99999999', y: '1.2345678e99',
    z: '-1.2345678e-42', t: '98765432', x1: '-77777777' })) calc.setRegister(name, value);
}

function main() {
  if (process.argv.includes('--write-fixtures')) {
    fs.mkdirSync(path.dirname(fixturePath('triple')), { recursive: true });
    for (const [name, options] of [['triple', {}], ['scalar', { profile: 'scalar' }],
      ['demo', { literal: true, repeats: 1 }]]) {
      const { program } = makeProgram(options);
      fs.writeFileSync(fixturePath(name), program.map((op, i) => op.toString(16)
        .padStart(2, '0') + (i % 7 === 6 ? '\n' : ' ')).join(''));
    }
    console.log('Wrote triple, scalar, and self-contained demo fixtures.');
    return;
  }
  for (const [name, options] of [['triple', {}], ['scalar', { profile: 'scalar' }],
    ['demo', { literal: true, repeats: 1 }]]) {
    const bytes = fs.readFileSync(fixturePath(name), 'utf8').trim().split(/\s+/)
      .map(token => parseInt(token, 16));
    assert.deepEqual(bytes, makeProgram(options).program, `${name}: stale fixture`);
  }
  const cases = [
    { values: ['123', '456', '789'] },
    { values: ['0', '100', '999'], dirtyStack: true },
    { values: ['999', '0', '1'], angleMode: 'deg', nested: false },
    { values: ['9', '99', '100'], angleMode: 'grad', dirtyStack: true },
    { values: ['123', '456', '789'], literal: true, repeats: 1 },
    ...['12345678', '-12345678', '1.2345678e99', '-1.2345678e-42',
      '1e-99', '-9.9999999e99', '0'].map(value => ({ profile: 'scalar',
      values: ['11111111', '-22222222', value], dirtyStack: true })),
  ];
  for (const scenario of cases) {
    const result = checkCase(scenario);
    console.log(process.argv.includes('--json') ? JSON.stringify(result)
      : `${result.profile}: ${result.values.join(' / ')}; `
        + `${result.repeats} reads, program unchanged`);
  }
  checkAbi();
  console.log('Register/stack contracts and all 105 program bytes verified.');
}

if (require.main === module) main();
module.exports = { WRITE, READ_TRIPLE, READ_SCALAR, WRITE_ADDRESS, READ_ADDRESS,
  pattern, withHelpers, makeProgram, run, seed, hiddenRaw, ordinaryRaw, checkCase };
