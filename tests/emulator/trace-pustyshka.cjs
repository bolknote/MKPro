#!/usr/bin/env node
'use strict';

// Observe the original ROM. The explicitly labelled counterfactual intercepts
// one ALU input; it is a causal experiment, not a calculator key sequence.
const assert = require('node:assert/strict');
const { MK61, press, input, makeEmpty, write, read, clone, hiddenWords,
  prepare, runWorkload, programChanges } = require('./probe-pustyshka.cjs');

const REAL_MARKER = '000fffffffff';
// Packet arrival order at IK1302.M, starting at memory phase 1. These names
// describe circulating physical words, not a software stack of this depth.
const PACKETS = {
  extended: ['Y', 'Z', 'T', 'U', 'V', 'W', 'scratch6', 'scratch7',
    'scratch8', 'scratch9', 'scratch10', 'marker', 'control', 'X1', 'X'],
  basic: ['scratch0', 'marker', 'control', 'X1', 'X', 'Y', 'Z', 'T',
    'U', 'V', 'W', 'scratch11', 'scratch12', 'scratch13'],
};

function rawWord(memory, lane = 1) {
  return Array.from({ length: 12 }, (_, i) => memory[33 + lane - 3 * i]
    .toString(16)).join('');
}

function traceKey(calc, key, { hideBlankAfterDisplay = false } = {}) {
  calc.syncMemoryPhase(1);
  const packets = PACKETS[calc.extended ? 'extended' : 'basic'];
  const chip = calc.ik1302;
  const originalTick = chip.tick;
  const originalOrder = chip.executeMicroOrder;
  const scans = [], swaps = [], hiddenMarkers = [], formats = [];
  let n = 0, current, formatted = false, formatStart;
  let firstProgramWrite;

  chip.tick = function () {
    if (this.tickIndex === 0) {
      const ip = this.r[36] + 16 * this.r[39];
      current = { n: n++, ip, packet: packets[(n - 1) % packets.length],
        word: rawWord(this.memory) };
      if (current.n < 400) {
        if (ip === 0xe5) scans.push({ ...current, digit: this.memory[22] });
        if (ip === 0xe7) swaps.push({ ...current, write: rawWord(this.r, 0) });
        if (ip === 0xf2) formatStart = current.n;
        if (ip === 0xec && formatStart !== undefined) {
          formats.push({ start: formatStart, end: current.n,
            cycles: current.n - formatStart + 1 });
          formatStart = undefined;
          formatted = true;
        }
      }
    }
    return originalTick.call(this);
  };
  chip.executeMicroOrder = function (order, acc) {
    // E5 reads M[22] into S at tick 22, then calculates S+1 at tick 24.
    // Read the packet snapshot taken at its start: earlier M cells have already
    // been replaced by the serial input by the time tick 22 is reached.
    if (hideBlankAfterDisplay && formatted && current.ip === 0xe5
      && order === 1 && this.tickIndex === 22 && this.memory[22] === 15
      && current.word !== REAL_MARKER) {
      acc.alpha |= 14;
      hiddenMarkers.push({ ...current, observedByAlu: 14 });
      return;
    }
    if (!firstProgramWrite && order === 24 && this.tickIndex % 3 === 2
      && this.memory[this.tickIndex] !== this.s) {
      firstProgramWrite = { ...current, tick: this.tickIndex,
        before: this.memory[this.tickIndex], after: this.s };
    }
    return originalOrder.call(this, order, acc);
  };
  try {
    press(calc, [key]);
  } finally {
    delete chip.tick;
    delete chip.executeMicroOrder;
  }
  const groups = [];
  for (const scan of scans) {
    if (!groups.length || groups.at(-1).at(-1).n + 1 !== scan.n) groups.push([]);
    groups.at(-1).push(scan);
  }
  return { key, hideBlankAfterDisplay, formats,
    scans: groups.map(group => ({ start: group[0], last: group.at(-1),
      candidates: group })), swaps, hiddenMarkers, firstProgramWrite,
    display: calc.displayText(), hidden: hiddenWords(calc) };
}

function beforeFirstEnter(value) {
  const calc = new MK61();
  input(calc, value);
  press(calc, ['X→П', '9']);
  makeEmpty(calc, 'logical');
  press(calc, ['В↑', 'П→X', '9']);
  return calc;
}

function secondAppendEnter(extended, oldValue) {
  const calc = new MK61({ extended });
  const recipe = extended ? 'logical' : 'classic';
  write(calc, oldValue, recipe);
  input(calc, extended ? '999' : '222222');
  press(calc, ['X→П', '9']);
  makeEmpty(calc, recipe);
  press(calc, ['В↑', 'П→X', '9', 'В↑']);
  return traceKey(calc, 'В↑');
}

function main() {
  const thresholds = [
    { model: 'MK-61', extended: true, value: '123', start: 123,
      match: 'marker', target: 'X', cycles: 28 },
    { model: 'MK-61', extended: true, value: '1234', start: 122,
      match: 'T', target: 'W', cycles: 27 },
    { model: 'MK-54', extended: false, value: '111111', start: 120,
      match: 'marker', target: 'X', cycles: 25 },
    { model: 'MK-54', extended: false, value: '1111111', start: 119,
      match: 'T', target: 'W', cycles: 24 },
  ].map(scenario => {
    const trace = secondAppendEnter(scenario.extended, scenario.value);
    const search = trace.scans.at(-1);
    assert.equal(search.start.n, scenario.start);
    assert.equal(search.last.packet, scenario.match);
    assert.equal(trace.swaps.at(-1).packet, scenario.target);
    assert.equal(trace.formats.at(-1).cycles, scenario.cycles);
    assert.equal(trace.firstProgramWrite, undefined);
    return { ...scenario, trace };
  });

  const before = beforeFirstEnter('12345678');
  const unchanged = traceKey(clone(before), 'В↑');
  const counterfactual = traceKey(clone(before), 'В↑', { hideBlankAfterDisplay: true });
  assert.equal(unchanged.scans.at(-1).last.packet, 'Z');
  assert.equal(unchanged.swaps.at(-1).packet, 'V');
  assert.deepEqual(unchanged.hidden, ['0,', '12345678,', '12345678,']);
  assert.equal(counterfactual.scans.at(-1).last.packet, 'marker');
  assert.equal(counterfactual.swaps.at(-1).packet, 'X');
  assert.deepEqual(counterfactual.hidden, ['0,', '12345678,', '0,']);
  assert.equal(counterfactual.hiddenMarkers.length, 1);
  assert.equal(counterfactual.hiddenMarkers[0].word, '9990fffffff0');
  assert.equal(unchanged.display, counterfactual.display);

  let corruption;
  if (process.argv.includes('--corruption')) {
    const calc = prepare({ values: ['123', '456', '789'] }).calc;
    runWorkload(calc);
    const beforeProgram = calc.readProgramCodes();
    read(calc, 'logical', 0);
    read(calc, 'logical', 1);
    makeEmpty(calc, 'logical');
    const trace = traceKey(calc, '↔');
    const changes = programChanges(beforeProgram, calc.readProgramCodes());
    assert.ok(changes.length > 0);
    assert.equal(trace.firstProgramWrite.ip, 0xc9);
    assert.equal(trace.firstProgramWrite.tick, 2);
    corruption = { trace, changes };
  }
  const report = { thresholds, unchanged, counterfactual, corruption };
  if (process.argv.includes('--json')) console.log(JSON.stringify(report, null, 2));
  else {
    for (const result of thresholds) {
      console.log(`${result.model}, ${result.value}: scan starts at ${result.start}, `
        + `matches ${result.match}, writes ${result.target}`);
    }
    console.log('12345678: false Z marker -> write V; hiding that marker only from '
      + 'the scan -> write X, no extra V copy. Display is identical.');
    if (corruption) console.log(`Uncleaned reads: ${corruption.changes.length} program bytes changed; `
      + 'first write is IK1302 ROM C9, tick 2, program lane.');
    console.log('All asserted ROM-trace observations reproduced.');
  }
}

if (require.main === module) main();
module.exports = { rawWord, traceKey };
