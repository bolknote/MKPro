#!/usr/bin/env node
'use strict';

const { MK61 } = require('./mk61.cjs');

function h(code) {
  return code.toString(16).toUpperCase().padStart(2, '0');
}

function run(codes, setup = {}, options = {}) {
  const calc = new MK61(options);
  calc.loadProgram(codes);
  for (const [register, value] of Object.entries(setup)) calc.setRegister(register, value);
  calc.press('В/О');
  calc.press('С/П');
  const status = calc.runUntilStable({
    maxFrames: options.maxFrames || 250,
    stableFrames: options.stableFrames || 5,
  });
  const registers = {};
  for (const register of ['x', 'y', 'z', 't', 'x1', '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e']) {
    try {
      registers[register] = calc.readRegister(register);
    } catch {
      registers[register] = '?';
    }
  }
  return {
    stopped: status.stopped,
    frames: status.frames,
    pc: calc.programCounter(),
    display: calc.displayText(),
    registers,
  };
}

function short(result) {
  return `disp=${JSON.stringify(result.display)} x=${JSON.stringify(result.registers.x)} y=${JSON.stringify(result.registers.y)} x1=${JSON.stringify(result.registers.x1)} r0=${JSON.stringify(result.registers['0'])} pc=${result.pc} frames=${result.frames} stop=${result.stopped}`;
}

function diffRegisters(a, b, keys) {
  return keys.filter((key) => a.registers[key] !== b.registers[key]);
}

console.log('== F0..FF single-op behavior compared with K НОП ==');
const fStates = [
  ['x=5', { x: '5', y: '2' }],
  ['x=0', { x: '0', y: '2' }],
  ['x=-1', { x: '-1', y: '2' }],
  ['x=1,5', { x: '1,5', y: '2' }],
];

for (const [label, setup] of fStates) {
  const nop = run([0x54, 0x50], setup);
  console.log(`\n-- state ${label}; NOP: ${short(nop)}`);
  for (let code = 0xf0; code <= 0xff; code += 1) {
    const result = run([code, 0x50], setup);
    const changed = diffRegisters(result, nop, ['x', 'y', 'z', 't', 'x1', '0']);
    if (
      result.display !== nop.display ||
      result.pc !== nop.pc ||
      result.stopped !== nop.stopped ||
      changed.length > 0
    ) {
      console.log(`${h(code)}: ${short(result)} changed=[${changed.join(',')}]`);
    }
  }
}

console.log('\n== F0..FF followed by decimal point: X2/display restoration probe ==');
for (let code = 0xf0; code <= 0xff; code += 1) {
  const result = run([code, 0x0a, 0x50], { x: '5', y: '2' });
  if (result.display !== '5,' || result.registers.x !== '5,') {
    console.log(`${h(code)} . : ${short(result)}`);
  }
}

console.log('\n== Direct *F aliases ==');
for (const code of [0x4f, 0x6f]) {
  const result = code === 0x4f
    ? run([code, 0x50], { x: '77' })
    : run([code, 0x50], { x: '0', '0': '77' });
  console.log(`${h(code)}: ${short(result)}`);
}

console.log('\n== Indirect *0 vs *F aliases through R0 ==');
const indirectPairs = [
  ['K x!=0', 0x70, 0x7f],
  ['K БП', 0x80, 0x8f],
  ['K x>=0', 0x90, 0x9f],
  ['K ПП', 0xa0, 0xaf],
  ['K X->П', 0xb0, 0xbf],
  ['K x<0', 0xc0, 0xcf],
  ['K П->X', 0xd0, 0xdf],
  ['K x=0', 0xe0, 0xef],
];

for (const [name, explicit, alias] of indirectPairs) {
  const setup = {
    x: name.includes('x<0') ? '-1' : name.includes('x=0') ? '0' : '7',
    '0': '4',
    '4': '44',
  };
  const programExplicit = new Array(20).fill(0x50);
  const programAlias = new Array(20).fill(0x50);
  programExplicit[0] = explicit;
  programAlias[0] = alias;
  // If a flow command jumps/calls through R0=4, make address 03/04/05 visible
  // even if the R0 transformation decrements before the target is used.
  for (const program of [programExplicit, programAlias]) {
    program[3] = 0x08;
    program[4] = 0x09;
    program[5] = 0x50;
  }
  const a = run(programExplicit, setup, { maxFrames: 400 });
  const b = run(programAlias, setup, { maxFrames: 400 });
  const changed = diffRegisters(a, b, ['x', 'y', 'x1', '0', '1', '2', '3', '4', '5']);
  console.log(`${name} ${h(explicit)} vs ${h(alias)}: explicit ${short(a)} | alias ${short(b)} | diff=[${changed.join(',')}]`);
}

console.log('\n== R0 fractional recall quirk probes ==');
for (const value of ['0,1', '0,5', '0,9', '1,1', '-0,5']) {
  const result = run([0xd0, 0x50], { x: '0', '0': value, '9': '99', e: '55' });
  console.log(`K П->X 0, R0=${value}: ${short(result)} re=${JSON.stringify(result.registers.e)} r9=${JSON.stringify(result.registers['9'])}`);
}

console.log('\n== Super-dark address one-command branch candidates ==');
for (const formal of [0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff]) {
  const actual = 48 + (formal - 0xfa);
  const extra = 1 + (formal - 0xfa);
  const program = new Array(105).fill(0x50);
  program[0] = 0x51;
  program[1] = formal;
  program[actual] = 0x08;
  program[actual + 1] = 0x09;
  program[extra] = 0x50;
  const result = run(program, {}, { maxFrames: 400 });
  console.log(`БП ${h(formal)} actual=${actual} extra=${extra}: ${short(result)}`);
}
