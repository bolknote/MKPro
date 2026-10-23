#!/usr/bin/env node
'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { MK61, parseProgramText } = require('./mk61.cjs');
const base = path.resolve(__dirname, '../../games/sports/ralli-novye-trassy-romanovskis-1987');
const listing = fs.readFileSync(`${base}.txt`, 'utf8');
const card = fs.readFileSync(`${base}.md`, 'utf8');
const parsed = parseProgramText(listing);
assert.deepEqual(parsed.diagnostics, []);
assert.equal(parsed.codes.length, 104);
const preparation = card.split('## Подготовка\n')[1].split('## Участок\n')[0];
const setup = [...preparation.matchAll(/```text\n([\s\S]*?)\n```/g)].map(m => m[1]);
assert.equal(setup.length, 2);
const keys = [...'0123456789', ',', '/-/', 'ВП', 'Cx', 'В↑'];
function keyboard(c, t) {
  for (const k of t.trim().split(/\s+/)) {
    if (/^\d+(?:[.,]\d+)?$/.test(k)) c.inputNumber(k);
    else if (/^хП[0-9a-e]$/.test(k)) c.press('П').press(keys[parseInt(k.slice(2), 16)]);
    else c.press(k);
  }
}
function number(text) {
  const [m, e = '0'] = text.trim().replace(',', '.').replace(/(\d)([+-]\d+)$/, '$1 $2').split(/\s+/);
  return Number(m) * 10 ** Number(e);
}
const register = (c, r) => number(c.readRegister(r));
function stop(c) {
  assert.ok(c.runUntilStable({ maxFrames: 600, stableFrames: 8 }).stopped);
  return number(c.displayText());
}
const scale = 10000000n;
function random(q) {
  let p = q * 997n;
  const excess = p.toString().length - 8;
  if (excess > 0) { const u = 10n ** BigInt(excess); p = (p + u / 2n) / u * u; }
  return p % scale;
}
function route(state) {
  const q1 = random(state.q), q2 = random(q1);
  state.q = q2;
  return { distance: 80 + Number(q1 * 200n / scale),
    lower: 1 + Number(q2 * 30n / scale) / 10 };
}
function prepare(seed = '0,371825', angleMode = 'rad') {
  const c = new MK61({ angleMode });
  c.loadProgram(listing);
  c.pressSequence(['F', '/-/']);
  keyboard(c, setup[0]);
  keyboard(c, setup[1].replace('0,371825', seed));
  const state = { q: BigInt(Math.round(Number(seed.replace(',', '.')) * Number(scale))) };
  return { c, state };
}
function prompt(c, stage, r) {
  assert.equal(stop(c), stage * 1000 + r.distance);
  c.press('С/П'); assert.equal(stop(c), r.lower);
  c.press('С/П');
  assert.ok(Math.abs(stop(c) - (r.lower + 0.1)) < 1e-8);
  assert.equal(register(c, '2'), r.distance);
  assert.equal(register(c, '0'), 10 - stage);
}
let stages = 0, races = 0;
function drive(flight, selector = () => null, retry = false) {
  const { c, state } = flight;
  let score = 0;
  const trace = [];
  for (let stage = 1; stage <= 9; stage++) {
    const r = route(state);
    prompt(c, stage, r);
    const q = register(c, '7');
    if (retry && stage === 2) {
      const before = ['0', '1', '2', '3', '4', '6', '7'].map(n => c.readRegister(n));
      for (const value of [0, -1, -140]) {
        c.inputNumber(Math.abs(value)); if (value < 0) c.press('/-/');
        c.press('С/П'); assert.equal(stop(c), -1);
        assert.deepEqual(['0', '1', '2', '3', '4', '6', '7'].map(n => c.readRegister(n)), before);
      }
    }
    const speed = selector(stage, r) ?? Number((r.distance / (r.lower + 0.05)).toPrecision(8));
    c.inputNumber(speed).press('С/П');
    const time = stop(c);
    assert.ok(Math.abs(time - r.distance / speed) < Math.max(1, time) * 2e-7);
    score += time < r.lower || time > r.lower + 0.1 + 1e-9 ? 10 : 0;
    assert.equal(register(c, '1'), score);
    assert.equal(register(c, '7'), q, 'speed cannot reseed the next stage');
    c.press('С/П'); assert.equal(stop(c), score);
    c.press('С/П');
    trace.push([r.distance, r.lower]);
    stages++;
  }
  assert.equal(stop(c), (score < 60 ? 1000 : 2000) + score);
  assert.equal(Math.round(register(c, '7') * Number(scale)), Number(state.q));
  assert.deepEqual(c.readProgramCodes(parsed.codes.length), parsed.codes);
  races++;
  return { trace, score };
}
let standard;
for (const angle of ['rad', 'deg', 'grad']) {
  const flight = prepare('0,371825', angle);
  const result = drive(flight, () => null, true);
  assert.equal(result.score, 0);
  assert.deepEqual(result.trace[0], [221, 2.2]);
  if (standard) assert.deepEqual(result.trace, standard);
  else standard = result.trace;
  // Single-key restart continues the state, instead of repeating the route.
  flight.c.press('С/П');
  const next = drive(flight);
  assert.notDeepEqual(next.trace, standard);
  assert.equal(next.score, 0);
}
for (const failures of [5, 6, 9]) {
  const result = drive(prepare(), stage => stage <= failures ? 1 : null);
  assert.equal(result.score, failures * 10);
  assert.deepEqual(result.trace, standard, 'the route must not depend on results');
}
const other = drive(prepare('0,5284163'));
assert.notDeepEqual(other.trace, standard);
const oldAnswers = drive(prepare(), stage => [140, 112, 107][Math.floor((stage - 1) / 3)]);
assert.ok(oldAnswers.score > 0, 'the old three speeds cannot solve every new route');
for (const bad of ['0', '1', '1.1', '-0.1']) {
  const { c } = prepare();
  c.inputNumber(bad.replace('-', '')); if (bad.startsWith('-')) c.press('/-/');
  c.press('П').press('7').press('В/О').press('С/П');
  c.runUntilStable({ maxFrames: 500, stableFrames: 8 }); c.runFrames(12);
  assert.equal(c.displayText(), 'ЕГГ0Г');
  c.press('Cx'); keyboard(c, setup[1]);
  assert.equal(stop(c), 1221);
}
console.log(`Variable rally: ${races} full rallies, ${stages} stages; independent seeded routes, new routes on restart, reproducible replay, 50/60-point threshold, old-answer rejection, invalid input and all angle modes OK.`);
