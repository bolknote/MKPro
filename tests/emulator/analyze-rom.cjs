#!/usr/bin/env node
'use strict';

const ROM = require('./rom.cjs');

const CHIPS = [
  ['ИК1302', ROM.ИК1302],
  ['ИК1303', ROM.ИК1303],
  ['ИК1306', ROM.ИК1306],
];

function decodeCommand(word) {
  return {
    raw: word,
    sync0: word & 0x7f,
    sync3: (word >>> 7) & 0x7f,
    sync4: (word >>> 14) & 0xff,
    hasUserOpcode: ((word >>> 16) & 0x3f) !== 0,
  };
}

function syncProgramFingerprint(syncRom, addr) {
  const cells = [];
  for (let j = 0; j < 9; j++) cells.push(syncRom[addr * 9 + j]);
  const nz = cells.filter((c) => c !== 0);
  return { addr, cells: cells.join(','), nonzero: nz.length, head: nz.slice(0, 4).join(',') };
}

// User opcode -> ROM command index = low_nibble + 16*high_nibble = opcode itself
// when r[36]=low, r[39]=high of program byte
for (const [chipName, chip] of CHIPS) {
  const cmds = chip.команды;
  const zeros = [];
  const dup = new Map();
  for (let op = 0; op < 256; op++) {
    const w = cmds[op];
    if (w === 0) zeros.push(op);
    const key = w.toString(16);
    if (!dup.has(key)) dup.set(key, []);
    dup.get(key).push(op);
  }
  const aliases = [...dup.values()].filter((g) => g.length > 1);
  console.log(`\n=== ${chipName} command ROM ===`);
  console.log(`  zero entries: ${zeros.length} ${zeros.slice(0, 20).map((x) => x.toString(16)).join(' ')}${zeros.length > 20 ? '...' : ''}`);
  console.log(`  duplicate micro-programs: ${aliases.length} groups`);
  for (const g of aliases.sort((a, b) => b.length - a.length).slice(0, 12)) {
    console.log(`    same ROM ${cmds[g[0]].toString(16)}: opcodes ${g.map((x) => x.toString(16).toUpperCase().padStart(2, '0')).join(' ')}`);
  }
}

// Cross-chip: MK-61 third CPU changes which opcodes?
console.log('\n=== Per-opcode: ИК1302 vs ИК1303 vs ИК1306 (first differing) ===');
const diffs = [];
for (let op = 0; op < 256; op++) {
  const w2 = ROM.ИК1302.команды[op];
  const w3 = ROM.ИК1303.команды[op];
  const w6 = ROM.ИК1306.команды[op];
  if (w2 !== w3 || w2 !== w6 || w3 !== w6) {
    diffs.push({
      op: op.toString(16).toUpperCase().padStart(2, '0'),
      w2: w2.toString(16),
      w3: w3.toString(16),
      w6: w6.toString(16),
    });
  }
}
console.log(`  ${diffs.length}/256 opcodes differ across at least one chip`);
for (const d of diffs.slice(0, 25)) console.log(`    ${d.op}: 302=${d.w2} 303=${d.w3} 306=${d.w6}`);
if (diffs.length > 25) console.log(`    ... +${diffs.length - 25} more`);

// ИК1306-only nonzero commands (MK-61 extensions)
const only306 = [];
for (let op = 0; op < 256; op++) {
  if (ROM.ИК1302.команды[op] === 0 && ROM.ИК1303.команды[op] === 0 && ROM.ИК1306.команды[op] !== 0) {
    only306.push(op);
  }
}
console.log(`\n=== Opcodes with real microcode only on ИК1306 ===`);
console.log(`  count: ${only306.length}`);
console.log(`  ${only306.map((x) => x.toString(16).toUpperCase().padStart(2, '0')).join(' ')}`);

// Special sync4 > 31 path (extended jump?) — which opcodes trigger wide sync4
console.log('\n=== Opcodes with sync4 > 31 (wide address path in emulator) ===');
for (const [chipName, chip] of CHIPS) {
  const hits = [];
  for (let op = 0; op < 256; op++) {
    const d = decodeCommand(chip.команды[op]);
    if (d.sync4 > 31) hits.push(`${op.toString(16).toUpperCase().padStart(2, '0')}(sync4=${d.sync4})`);
  }
  console.log(`  ${chipName}: ${hits.length} — ${hits.slice(0, 30).join(' ')}${hits.length > 30 ? '...' : ''}`);
}

// 5F hang: inspect ROM
for (const op of [0x5f, 0x1f, 0x2f, 0x3f, 0x4f, 0x3e, 0x3d, 0x2a]) {
  console.log(`\n=== Opcode ${op.toString(16).toUpperCase()} ROM decode ===`);
  for (const [chipName, chip] of CHIPS) {
    const d = decodeCommand(chip.команды[op]);
    const s0 = syncProgramFingerprint(chip.синхропрограммы, d.sync0);
    console.log(`  ${chipName}: cmd=0x${d.raw.toString(16)} sync0=${d.sync0} sync3=${d.sync3} sync4=${d.sync4} | sync[0] nz=${s0.nonzero} ${s0.head}`);
  }
}

// F0-FF cluster
console.log('\n=== F0-FF: identical across chips? ===');
let fClusterSame = true;
const ref = ROM.ИК1302.команды[0xf0];
for (let op = 0xf1; op <= 0xff; op++) {
  if (ROM.ИК1302.команды[op] !== ref) fClusterSame = false;
}
console.log(`  ИК1302: all F* same ROM word = ${fClusterSame} (word 0x${ref.toString(16)})`);
for (let op = 0xf0; op <= 0xff; op++) {
  const same306 = ROM.ИК1302.команды[op] === ROM.ИК1306.команды[op];
  if (!same306) console.log(`    ${op.toString(16)}: 302≠306`);
}

// Compare 3D vs 2A
console.log('\n=== 2A vs 3D alias in ROM ===');
for (const [chipName, chip] of CHIPS) {
  console.log(`  ${chipName}: 2A=0x${chip.команды[0x2a].toString(16)} 3D=0x${chip.команды[0x3d].toString(16)} ${chip.команды[0x2a] === chip.команды[0x3d] ? 'IDENTICAL' : 'DIFFER'}`);
}

// *F aliases vs *0..*E for indirect blocks
for (const base of [0x70, 0x80, 0x90, 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0x40, 0x60]) {
  const alias = base + 0xf;
  const reg0 = base + 0;
  const same2 = ROM.ИК1302.команды[alias] === ROM.ИК1302.команды[reg0];
  const same3 = ROM.ИК1303.команды[alias] === ROM.ИК1303.команды[reg0];
  if (!same2 || !same3) {
    console.log(`  block ${base.toString(16)}: *F vs *0 on 302 ${same2 ? '=' : '≠'} on 303 ${same3 ? '=' : '≠'}`);
  }
}

// PAGE permutation: physical cells beyond 105?
const PAGE_PERMUTATIONS_EXTENDED = [
  [1, 2, 3, 4, 5, 14, 13, 12, 6, 7, 8, 9, 10, 11, 0],
  [10, 11, 6, 7, 2, 3, 4, 5, 0, 1, 14, 13, 12, 8, 9],
  [14, 13, 12, 10, 11, 6, 7, 8, 9, 4, 5, 0, 1, 2, 3],
];
const MEMORY_PAGE_ADDRESSES = [
  [1, 41], [1, 83], [1, 125], [1, 167], [1, 209], [1, 251],
  [2, 41], [2, 83], [2, 125], [2, 167], [2, 209], [2, 251],
  [3, 41], [4, 41], [5, 41],
];
function commandAddress(index, phase) {
  const whole = (index / 7) | 0;
  const rest = index % 7;
  const base = MEMORY_PAGE_ADDRESSES[PAGE_PERMUTATIONS_EXTENDED[phase][whole]];
  if (rest === 0) return base;
  return [base[0], base[1] - 42 + rest * 6];
}
console.log('\n=== JS loader: official logical program indices 98..104 chip/addr ===');
for (let i = 98; i < 105; i++) {
  const [chip, addr] = commandAddress(i, 1);
  console.log(`  logical ${String(i).padStart(3)} -> chip ${chip} addr ${addr}`);
}
console.log('  Formal addresses above A4 are branch operands; see the dark-address space mapping.');
