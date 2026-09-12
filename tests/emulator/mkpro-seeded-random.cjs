#!/usr/bin/env node
'use strict';

// ROM checks for the explicitly seeded scalar RNG ABI. No game-specific
// lowering or numerical replacement for the calculator's RNG is used here.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { MK61 } = require('./mk61.cjs');

const root = path.resolve(__dirname, '../..');
const compiler = process.env.MKPRO_NATIVE || path.join(root, 'native/build/mkpro-native');
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'mkpro-seeded-random-'));
let checks = 0;

function compile(name, source, rejected = false) {
  const file = path.join(temporary, `${name}.mkpro`);
  fs.writeFileSync(file, source);
  const process = spawnSync(compiler, ['compile', file, '--out', 'json', '--analysis',
    '--budget', '105', '--disable-candidate-search'], { cwd: root, encoding: 'utf8' });
  if (process.error) throw process.error;
  if (rejected) {
    assert.notEqual(process.status, 0, `${name}: invalid initializer was accepted`);
    assert.match(process.stderr, /setup-initializer/, `${name}: missing setup diagnostic`);
    checks++;
    return null;
  }
  assert.equal(process.status, 0, `${name}: ${process.stderr}`);
  const result = JSON.parse(process.stdout);
  assert.equal(result.diagnostics, 0, `${name}: diagnostics`);
  assert.ok(result.steps.length <= 105, `${name}: main does not fit`);
  if (result.setupProgram)
    assert.ok(result.setupProgram.steps.length <= 105, `${name}: setup does not fit`);
  return result;
}

function run(machine, steps) {
  const loaded = machine.loadProgram(steps.map(step => step.opcode));
  assert.deepEqual(loaded.diagnostics, [], 'the ROM test must not truncate a program');
  machine.pressSequence(['В/О', 'БП', '0', '0', 'С/П']);
  assert.ok(machine.runUntilStable({ maxFrames: 300, stableFrames: 6 }).stopped);
}

function value(machine) {
  const text = machine.displayText().replace(',', '.');
  const match = /^(-?\d+(?:\.\d*)?)\s*([+-]\d+)?$/.exec(text);
  assert.ok(match, `not a numeric result: ${text}`);
  return Number(match[1]) * 10 ** Number(match[2] || 0);
}

function execute(result) {
  const machine = new MK61({ angleMode: 'grad' });
  if (result.setupProgram) {
    run(machine, result.setupProgram.steps);
  } else {
    for (const preload of result.preloads) machine.setRegister(preload.register, preload.value);
  }
  run(machine, result.steps);
  return value(machine);
}

try {
  // Independent ROM listing: seed -> Y, one K RANDOM, multiply the draw by 2.
  const reference = new MK61({ angleMode: 'grad' });
  reference.setRegister('0', '0.3141592');
  run(reference, [0x60, 0x0e, 0x3b, 0x02, 0x12, 0x50].map(opcode => ({ opcode })));
  const draw = value(reference);
  const cases = [
    ['add', '10 + random(0, 2, seed)', 10 + draw],
    ['subtract', '10 - random(0, 2, seed)', 10 - draw],
    ['multiply', '10 * random(0, 2, seed)', 10 * draw],
    ['divide', '10 / random(0, 2, seed)', 10 / draw],
    ['first-operand', 'random(0, 2, seed) + 10', draw + 10],
    ['nested', '10 + (20 - (30 / random(0, 2, seed)))', 30 - 30 / draw],
    ['wrapper', '10 + sampled(seed)', 10 + draw],
    ['max', 'max(10, random(0, 2, seed))', 10],
    ['pow', 'pow(random(0, 2, seed), 2)', draw * draw],
  ];
  for (const [name, expression, expected] of cases) {
    const result = compile(name, `program SeededExpression {
  state {
    seed: packed = 0.3141592
  }
  halt(${expression})
  fn sampled(s) {
    return random(0, 2, s)
  }
}
`);
    const actual = execute(result);
    assert.ok(Math.abs(actual - expected) < 1e-5,
      `${name}: got ${actual}, expected ${expected}`);
    checks++;
  }

  for (const [name, initializer, expected] of [
    ['setup-draw', 'random(0, 2, 0.3141592)', draw],
    ['setup-nested', '10 + (20 - random(0, 2, 0.3141592))', 30 - draw],
  ]) {
    const result = compile(name, `program SeededSetup {
  state {
    sample: packed = ${initializer}
  }
  halt(sample)
}
`);
    assert.ok(result.setupProgram, `${name}: executable setup missing`);
    assert.ok(Math.abs(execute(result) - expected) < 1e-5, `${name}: incorrect draw`);
    checks++;
  }

  for (const [name, initializer] of [
    ['reversed-bounds', 'random(2, 0, 0.3141592)'],
    ['nonconstant-bounds', 'random(0, seed, 0.3141592)'],
  ]) {
    compile(name, `program InvalidSeededSetup {
  state {
    seed: packed = 2
    sample: packed = ${initializer}
  }
  halt(sample)
}
`, true);
  }
  console.log(`MK-Pro seeded random: ${checks} ROM/compiler checks passed`);
} finally {
  fs.rmSync(temporary, { recursive: true, force: true });
}
