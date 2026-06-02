#!/usr/bin/env node
'use strict';

const path = require('path');
const { MK61, parseProgramText } = require('./mk61.cjs');
const { parseMany } = require('./lordbss.cjs');

const root = path.resolve(__dirname, '../../games');
const games = parseMany(root, ['lunar-landing.txt', 'boxing.txt', 'kazino.txt']);

for (const game of games) {
  const calc = new MK61();
  const parsed = calc.loadProgram(game.programText);
  const expected = parseProgramText(game.programText).codes;
  const actual = calc.readProgramCodes(expected.length);
  const mismatches = expected
    .map((code, index) => ({ index, expected: code, actual: actual[index] }))
    .filter((entry) => entry.expected !== entry.actual);

  console.log(`${path.basename(game.file)} ${game.title}`);
  console.log(`  commands: ${expected.length}, loader mismatches: ${mismatches.length}`);
  if (parsed.diagnostics.length > 0) {
    console.log(`  diagnostics: ${parsed.diagnostics.join('; ')}`);
  }
}

const lunar = new MK61();
const lunarGame = games[0];
lunar.loadProgram(lunarGame.programText);
lunar
  .setRegister('1', '100')
  .setRegister('2', '140')
  .setRegister('3', '0')
  .setRegister('5', '10')
  .setRegister('6', '9,8')
  .setRegister('8', '1');

lunar.press('В/О');
lunar.press('С/П');
const heightStop = lunar.runUntilStable({ maxFrames: 120, stableFrames: 5 });
const height = lunar.displayText();
lunar.press('С/П');
const speedStop = lunar.runUntilStable({ maxFrames: 120, stableFrames: 5 });
const speed = lunar.displayText();
lunar.press('С/П');
const fuelStop = lunar.runUntilStable({ maxFrames: 120, stableFrames: 5 });
const fuel = lunar.displayText();

console.log('lunar run:');
console.log(`  height stop: ${heightStop.stopped ? 'stable' : 'not stable'} after ${heightStop.frames} frames, display ${JSON.stringify(height)}`);
console.log(`  speed stop: ${speedStop.stopped ? 'stable' : 'not stable'} after ${speedStop.frames} frames, display ${JSON.stringify(speed)}`);
console.log(`  fuel stop: ${fuelStop.stopped ? 'stable' : 'not stable'} after ${fuelStop.frames} frames, display ${JSON.stringify(fuel)}`);
console.log(`  program counter: ${lunar.programCounter()}`);

const demoPath = path.join(root, 'demo.txt');
const demoText = require('fs').readFileSync(demoPath, 'utf8');
const demo = new MK61();
const demoParsed = demo.loadProgram(demoText);
const demoActual = demo.readProgramCodes(demoParsed.codes.length);
const demoMismatches = demoParsed.codes
  .map((code, index) => ({ index, expected: code, actual: demoActual[index] }))
  .filter((entry) => entry.expected !== entry.actual);

demo
  .setRegister('4', '2')
  .setRegister('5', '10')
  .setRegister('6', 'ГE-02')
  .setRegister('7', '5E-1')
  .setRegister('8', '-52')
  .setRegister('9', '4,_3E-08');

demo.pressSequence(['БП', '4', '4', '4', '4', 'П→X', '9', 'F', '0', 'С/П']);
const demoStart = demo.runUntilStable({ maxFrames: 1000, stableFrames: 8 });
const demoStartDisplay = demo.displayText();
demo.inputNumber('2', { clear: true });
demo.press('С/П');
const demoMove = demo.runUntilStable({ maxFrames: 1000, stableFrames: 8 });

console.log('treasure cave demo run:');
console.log(`  commands: ${demoParsed.codes.length}, loader mismatches: ${demoMismatches.length}`);
console.log(`  start: ${demoStart.stopped ? 'stable' : 'not stable'} after ${demoStart.frames} frames, display ${JSON.stringify(demoStartDisplay)}`);
console.log(`  move 2: ${demoMove.stopped ? 'stable' : 'not stable'} after ${demoMove.frames} frames, display ${JSON.stringify(demo.displayText())}, Re ${JSON.stringify(demo.readRegister('e'))}`);
