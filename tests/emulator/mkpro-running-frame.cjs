#!/usr/bin/env node
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { spawnSync } = require('node:child_process');
const { MK61 } = require('./mk61.cjs');

const root = path.resolve(__dirname, '../..');
const compiler = process.env.MKPRO_NATIVE || path.join(root, 'native/build/mkpro-native');
const temporary = fs.mkdtempSync(path.join(os.tmpdir(), 'mkpro-running-frame-'));
let checks = 0;

function compile(name, frame, rejected = false, rules = '', finish = '0', useSetup = false) {
  const file = path.join(temporary, `${name}.mkpro`);
  fs.writeFileSync(file, `program LiveFrame {
  state {
    ${useSetup ? 'expected_mode("grd")' : ''}
    position: counter -1..9 = 5
    glyph: counter 0..9 = 3
    air: counter 0..99 = 20
  }
  preview(${frame})
  halt(${finish})
${rules}
}
`);
  const process = spawnSync(compiler, ['compile', file, '--out', 'json', '--analysis',
    '--budget', '105', '--disable-candidate-search'], { cwd: root, encoding: 'utf8' });
  if (process.error) throw process.error;
  if (rejected) {
    assert.notEqual(process.status, 0, `${name}: invalid frame was accepted`);
    checks++;
    return null;
  }
  assert.equal(process.status, 0, `${name}: ${process.stderr}`);
  const result = JSON.parse(process.stdout);
  assert.equal(result.diagnostics, 0);
  assert.ok(result.steps.length <= 105, `${name}: ROM program would be truncated`);
  return result;
}

function expectFrame(program, assignments, cells, order, useSetup = false) {
  const machine = new MK61({ angleMode: 'grad' });
  if (useSetup) {
    assert.ok(program.setupProgram?.steps.length > 0);
    assert.ok(program.setupProgram.steps.length <= 105, 'setup must fit in program memory');
    assert.deepEqual(machine.loadProgram(program.setupProgram.steps.map(step => step.opcode)).diagnostics, []);
    machine.pressSequence(['В/О', 'БП', '0', '0', 'С/П']);
    assert.ok(machine.runUntilStable({ maxFrames: 300, stableFrames: 6 }).stopped);
    const expected = new MK61({ angleMode: 'grad' });
    for (const preload of program.preloads) {
      expected.setRegister(preload.register, preload.value);
      assert.equal(machine.readRegister(preload.register), expected.readRegister(preload.register),
        `setup must preserve the exact preloaded word in R${preload.register}`);
    }
    checks++;
  }
  assert.deepEqual(machine.loadProgram(program.steps.map(step => step.opcode)).diagnostics, []);
  if (!useSetup)
    for (const preload of program.preloads) machine.setRegister(preload.register, preload.value);
  for (const [name, value] of Object.entries(assignments)) {
    assert.ok(program.registers[name], `missing observable input ${name}`);
    machine.setRegister(program.registers[name], String(value));
  }
  machine.pressSequence(['В/О', 'БП', '0', '0', 'С/П']);
  // preview() is a live indicator, not a STOP-normalized number. Observe ROM
  // display cycles directly; large orders need many VP/FL iterations.
  for (let quantum = 0; quantum < 80000; quantum++) {
    for (let tick = 0; tick < 42; tick++) machine.tick();
    machine.updateDisplay();
    const display = machine.displayCells();
    if (display.slice(1, 9).every((cell, index) => cell.digit === cells[index]) &&
        display[10].digit === Math.floor(order / 10) && display[11].digit === order % 10) {
      checks++;
      return machine;
    }
  }
  assert.fail(`missing frame ${JSON.stringify({ assignments, cells, order })}; ` +
    `last indicator: ${machine.displayText()}`);
}

try {
  const letter = compile('letter', 'frame("--------", 20, at(position, "E"))');
  const digit = compile('digit', 'frame("--------", 20, at(position, glyph))');
  assert.ok(letter.optimizations.some(item => item.name === 'running-frame-background-preload'),
    'a constant background should use the shared preload planner');
  for (let position = -1; position <= 9; position++) {
    const cells = Array(8).fill(0x0a);
    if (position >= 1 && position <= 8) cells[position - 1] = 0x0e;
    expectFrame(letter, { position }, cells, 20);
  }
  for (let glyph = 0; glyph <= 9; glyph++) {
    for (let position = 1; position <= 8; position++) {
      const cells = Array(8).fill(0x0a);
      cells[position - 1] = glyph;
      expectFrame(digit, { position, glyph }, cells, 20);
    }
  }
  const selectors = [
    ['first-cell', 'sign(position)', value => value > 0 ? 1 : 0],
    ['sparse-cell', '7 * (1 - sign(position))', value => value === 0 ? 7 : 0],
    ['two-visible-cells', '3 + 5 * sign(position)', value => value < 0 ? 0 : value === 0 ? 3 : 8],
    ['fractional-position', 'position / 2', value => Math.trunc(value / 2)],
  ];
  for (const [name, selector, visible] of selectors) {
    const selected = compile(name, `frame("--------", 20, at(${selector}, "E"))`);
    for (let position = -1; position <= 9; position++) {
      const cells = Array(8).fill(0x0a);
      const cell = visible(position);
      if (cell >= 1 && cell <= 8) cells[cell - 1] = 0x0e;
      expectFrame(selected, { position }, cells, 20);
    }
    if (name === 'first-cell' || name === 'sparse-cell')
      assert.ok(selected.steps.length < letter.steps.length,
        `${name}: finite-position specialization should save cells`);
  }
  const shadowed = compile('shadowed-sign', 'frame("--------", 20, at(sign(position), "E"))',
    false, '  fn sign(value) {\n    return 7\n  }');
  for (const position of [-1, 0, 1]) {
    const cells = Array(8).fill(0x0a);
    cells[6] = 0x0e;
    // The function may be inlined and its unused argument discarded.
    expectFrame(shadowed, shadowed.registers.position ? { position } : {}, cells, 20);
  }
  const tick = '  fn tick(value) {\n    glyph += 1\n    return value\n  }';
  const effectfulPosition = compile('effectful-position',
    'frame("--------", 20, at(sign(tick(position)), "E"))', false, tick, 'glyph');
  const effectfulGlyph = compile('effectful-glyph',
    'frame("--------", 20, at(sign(position), tick(glyph)))', false, tick, 'glyph');
  const nestedPosition = compile('nested-effectful-position',
    'frame("--------", 20, at(sign(10 + tick(position)), "E"))', false, tick, 'glyph');
  const nestedGlyph = compile('nested-effectful-glyph',
    'frame("--------", 20, at(sign(position), 1 + tick(glyph)))', false, tick, 'glyph');
  const effectfulOrder = compile('effectful-order',
    'frame("--------", tick(20), at(sign(position), tick(glyph)))', false, tick, 'glyph');
  for (const position of [-1, 0, 1]) {
    for (const [program, cell, finalValue, visible] of [
      [effectfulPosition, 0x0e, 4, position > 0],
      [effectfulGlyph, 3, position > 0 ? 4 : 3, position > 0],
      [nestedPosition, 0x0e, 4, true],
      [nestedGlyph, 4, position > 0 ? 4 : 3, position > 0],
      [effectfulOrder, 3, position > 0 ? 5 : 4, position > 0],
    ]) {
      const cells = Array(8).fill(0x0a);
      if (visible) cells[0] = cell;
      const machine = expectFrame(program, { position, glyph: 3 }, cells, 20);
      assert.ok(machine.runUntilStable({ maxFrames: 300, stableFrames: 6 }).stopped);
      assert.equal(machine.displayText(), `${finalValue},`,
        'evaluate the selector once, the visible glyph only, and the order last');
      checks++;
    }
  }
  const layers = compile('ordered-layers',
    'frame("--------", 20, at(sign(position), "E"), at(1, "8"))');
  for (const position of [-1, 0, 1])
    expectFrame(layers, { position }, [8, 10, 10, 10, 10, 10, 10, 10], 20);

  const alphabet = '0123456789-LCГE';
  for (const [name, background, glyph, operation] of [
    ['or-known-bits', '--------', 'E', 'OR'],
    ['and-known-bits', 'EEEEEEEE', '8', 'AND'],
    ['and-hex-mask', '--------', '0', 'AND'],
    ['or-mixed-subsets', '8-8-8-8-', 'E', 'OR'],
    ['and-mixed-supersets', 'E-E-E-E-', '8', 'AND'],
    ['unchanged-cell', '88888888', '8', 'OR'],
    ['incomparable-bits', '--------', '5', null],
    ['incomparable-cells', '81234567', '8', null],
  ]) {
    const program = compile(name, `frame("${background}", 20, at(position, "${glyph}"))`,
      false, '', '0', true);
    const reports = program.optimizations.filter(item => item.name === 'running-frame-known-cell-bits');
    if (operation === null)
      assert.equal(reports.length, 0, `${name}: no monotone overwrite proof`);
    else
      assert.ok(reports.some(item => item.detail.includes(operation)), `${name}: missing bit proof`);
    for (const position of [-1, 0, 1, 2, 5, 8, 9]) {
      const cells = [...background].map(cell => alphabet.indexOf(cell));
      if (position >= 1 && position <= 8) cells[position - 1] = alphabet.indexOf(glyph);
      const machine = expectFrame(program, { position }, cells, 20, position === 2);
      assert.ok(machine.runUntilStable({ maxFrames: 300, stableFrames: 6 }).stopped);
      assert.equal(machine.displayText(), '0,', `${name}: complete without a hidden error`);
      checks++;
    }
  }
  for (const [name, overlays, expected, proofs] of [
    ['clear-then-set', 'at(2, "8"), at(2, "E")', 14, 2],
    ['set-then-clear', 'at(2, "E"), at(2, "8")', 8, 2],
    ['repeated-glyph', 'at(2, "E"), at(2, "E")', 14, 2],
    ['nonmonotone-followup', 'at(2, "8"), at(2, "5")', 5, 1],
  ]) {
    const program = compile(name, `frame("--------", 20, ${overlays})`);
    assert.equal(program.optimizations.filter(item => item.name === 'running-frame-known-cell-bits').length,
      proofs, `${name}: update facts after every layer`);
    const machine = expectFrame(program, {}, [10, expected, 10, 10, 10, 10, 10, 10], 20);
    assert.ok(machine.runUntilStable({ maxFrames: 300, stableFrames: 6 }).stopped);
    assert.equal(machine.displayText(), '0,');
    checks++;
  }
  const conditionalBits = compile('conditional-prior-bits',
    'frame("--------", 20, at(7 * (1 - sign(position)), "8"), at(7, "E"))');
  for (const position of [-1, 0, 1])
    expectFrame(conditionalBits, { position }, [10, 10, 10, 10, 10, 10, 14, 10], 20);
  const variableOrder = compile('variable-order', 'frame("--------", air)');
  for (const order of [0, 1, 7, 8, 9, 10, 20, 50, 99]) {
    expectFrame(variableOrder, { air: order }, Array(8).fill(0x0a), order);
    const literalOrder = compile(`literal-order-${order}`, `frame("--------", ${order})`);
    expectFrame(literalOrder, {}, Array(8).fill(0x0a), order);
    assert.ok(literalOrder.steps.length < variableOrder.steps.length,
      'literal order should not retain the runtime order loop');
  }
  compile('short-background', 'frame("-------", 20)', true);
  compile('bad-order', 'frame("--------", 100)', true);
  compile('wide-glyph', 'frame("--------", 20, at(1, "EE"))', true);
  console.log(`MK-Pro running frame: ${checks} ROM/compiler checks passed`);
} finally {
  fs.rmSync(temporary, { recursive: true, force: true });
}
