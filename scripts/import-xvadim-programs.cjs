#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');

const SOURCE_ROOT = path.resolve(process.argv[2] || '/private/tmp/pmk61-programms-xvadim');
const OUT = path.resolve(__dirname, '../games/xvadim');
const REPO_URL = 'https://github.com/xvadim/pmk61-programms';

const PAGE_EXTENDED_PHASE_1 = [10, 11, 6, 7, 2, 3, 4, 5, 0, 1, 14, 13, 12, 8, 9];

const SERIALIZED_POSITION_TO_PHYSICAL = [
  8, 10, 1, 14, 13, 12,
  5, 0, 7, 2, 3, 4,
  6, 11, 9,
];

const ENTRIES = [
  {
    slug: 'harvest-e',
    title: 'Урожай (экстенсивная модель)',
    sourcePmk: 'harvest-e.pmk',
    sourceHtml: 'harvest-e.html',
    sourceUrl: `${REPO_URL}/blob/master/harvest-e.pmk`,
    author: 'В. Алексеев; Android conversion by А. Лаврик',
    description: [
      'Экономический симулятор совхоза: игрок распоряжается деньгами, осваивает новые земли, распределяет пашню между культурами, организует сбор даров природы, охраняет урожай и платит ежегодные отчисления государству.',
      'Это экстенсивная модель: рост прибыли достигается расширением посевной площади.',
    ],
    setup: [
      'Set angle switch to Р.',
      'R7: display letter E; R8: 82; R9: 93.',
      'R3: 300; R4: 30; R5: 1.1; R6: 1.2; Ra: 35000; Rb: 3500; Rd: 1000.',
    ],
  },
  {
    slug: 'harvest-i',
    title: 'Урожай (интенсивная модель)',
    sourcePmk: 'harvest-i.pmk',
    sourceHtml: 'harvest-i.html',
    sourceUrl: `${REPO_URL}/blob/master/harvest-i.pmk`,
    author: 'В. Алексеев; Android conversion by А. Лаврик',
    description: [
      'Экономический симулятор совхоза с тем же годовым циклом, что и harvest-e: урожайность, посевные площади, сбор грибов и ягод, охрана урожая и выплаты в бюджет.',
      'Это интенсивная модель: прибыль повышается за счёт роста урожайности уже имеющейся пашни.',
    ],
    setup: [
      'Set angle switch to Р.',
      'R7: display letter E; R8: 82; R9: 93.',
      'R3: 300000; R4: 30; R5: 1.1; R6: 1.2; Ra: 35000; Rb: 3500; Rd: 1000.',
    ],
  },
  {
    slug: 'naval-battle',
    title: 'Морской бой',
    sourcePmk: 'naval_battle.pmk',
    sourceHtml: 'naval_battle.html',
    sourceUrl: `${REPO_URL}/blob/master/naval_battle.pmk`,
    author: 'Г. Горовой; Android conversion by А. Лаврик',
    description: [
      'Вариант игры "Морской бой" на поле 10x10. Каждый игрок расставляет десять одноклеточных кораблей; попадание даёт право на дополнительный ход.',
      'В начале партии игрок вводит любое положительное число как seed, затем В/О С/П. После расстановки кораблей calculator displays -10.',
    ],
  },
  {
    slug: 'rally',
    title: 'Rally',
    sourcePmk: 'rally.pmk',
    sourceUrl: `${REPO_URL}/blob/master/rally.pmk`,
    description: [
      'Program recovered from rally.pmk in xvadim/pmk61-programms.',
      'The source repository does not include an HTML instruction page for this listing.',
    ],
  },
  {
    slug: 'square-equation',
    title: 'Решение квадратного уравнения',
    sourcePmk: 'square-equation.pmk',
    sourceHtml: 'square-equation.html',
    sourceUrl: `${REPO_URL}/blob/master/square-equation.pmk`,
    description: [
      'Utility program for solving quadratic equations ax^2 + bx + c = 0.',
      'Store coefficients in Ra, Rb, Rc, then press В/О С/П. The two real roots are left in X and Y; equations without real roots display ЕГГ0Г.',
    ],
    pageOrder: ['mem0.1', 'mcu2.0', 'mcu1.0', 'mcu0.0'],
  },
  {
    slug: 'wolves-and-goat-9x9',
    title: 'Волки и козлик 9x9',
    sourcePmk: 'wolves_and_goat_9x9/wolves_and_goat_9x9.pmk',
    sourceHtml: 'wolves_and_goat_9x9/wolves_and_goat_9x9.hrml',
    sourceUrl: `${REPO_URL}/blob/master/wolves_and_goat_9x9.zip`,
    author: 'Дмитрий Кайков, Борис Греков, KEI; Android conversion by А. Лаврик',
    description: [
      'Original 9x9 "Wolves and a Goat" game. The calculator controls the goat; the player controls four wolves and tries to block all goat moves before it reaches the ninth rank.',
      'This is distinct from the later KEI game Winner, which is an improved MK-61 adaptation.',
    ],
    setup: [
      'R1..R4: wolf coordinates 91, 93, 97, 99.',
      'R5: goat coordinate 55; R6: previous goat move 11; R7: goat offset seed.',
      'R9: 9; Ra: 40; Rb: 41; Rc: 20; Rd: 56.',
    ],
  },
];

function main() {
  fs.mkdirSync(OUT, { recursive: true });

  const manifest = ['program\tdescription\ttitle\tauthor\tsource_url'];
  const readmeRows = [];

  for (const entry of ENTRIES) {
    const pmkPath = path.join(SOURCE_ROOT, entry.sourcePmk);
    if (!fs.existsSync(pmkPath)) {
      throw new Error(`Missing source file ${pmkPath}. For wolves_and_goat_9x9, unzip the archive in the source repo first.`);
    }

    const codes = decodeEntry(pmkPath, entry);
    const listing = formatListing(codes);
    const txtName = `${entry.slug}.txt`;
    const mdName = `${entry.slug}.md`;

    fs.writeFileSync(path.join(OUT, txtName), listing, 'utf8');
    fs.writeFileSync(path.join(OUT, mdName), buildMarkdown(entry, txtName), 'utf8');
    manifest.push([txtName, mdName, entry.title, entry.author || '', entry.sourceUrl].join('\t'));
    readmeRows.push({ txtName, title: entry.title, steps: codes.length });
  }

  fs.writeFileSync(path.join(OUT, 'manifest.tsv'), `${manifest.join('\n')}\n`, 'utf8');
  fs.writeFileSync(path.join(OUT, 'README.md'), buildReadme(readmeRows), 'utf8');
}

function decodeEntry(filePath, entry) {
  if (entry.pageOrder) return trimTrailingZeroes(decodeOrderedPages(filePath, entry.pageOrder));
  return trimTrailingZeroes(decodePhaseOne(filePath));
}

function decodePhaseOne(filePath) {
  const pages = decodePhysicalPages(filePath);
  const codes = [];
  for (const physicalPage of PAGE_EXTENDED_PHASE_1) codes.push(...pages[physicalPage]);
  return codes;
}

function decodeOrderedPages(filePath, pageOrder) {
  const named = decodeNamedPages(filePath);
  const codes = [];
  for (const name of pageOrder) {
    const page = named.get(name);
    if (!page) throw new Error(`Unknown page ${name} in ${filePath}`);
    codes.push(...page);
  }
  return codes;
}

function decodePhysicalPages(filePath) {
  const named = decodeNamedPages(filePath);
  const pages = Array(15);
  for (let position = 0; position < SERIALIZED_POSITION_TO_PHYSICAL.length; position += 1) {
    const physical = SERIALIZED_POSITION_TO_PHYSICAL[position];
    pages[physical] = named.get(serializedPositionName(position));
  }
  if (pages.some((page) => page === undefined)) throw new Error(`Cannot decode all pages from ${filePath}`);
  return pages;
}

function decodeNamedPages(filePath) {
  const blocks = zBlocks(filePath);
  const memoryBlocks = blocks.filter((block) => block.length === 0x3fc).map((block) => readInts(block.bytes).slice(3));
  const mcuBlocks = blocks.filter((block) => block.length === 0x247).map((block) => readInts(block.bytes).slice(42, 84));
  if (memoryBlocks.length !== 2 || mcuBlocks.length !== 3) {
    throw new Error(`Unexpected PMK block layout in ${filePath}`);
  }

  const result = new Map();
  memoryBlocks.forEach((memory, blockIndex) => {
    irPages(memory).forEach((page, pageIndex) => {
      result.set(`mem${blockIndex}.${pageIndex}`, page);
    });
  });
  mcuBlocks.forEach((memory, blockIndex) => {
    result.set(`mcu${blockIndex}.0`, mcuPage(memory));
  });
  return result;
}

function serializedPositionName(position) {
  if (position < 12) return `mem${Math.floor(position / 6)}.${position % 6}`;
  return `mcu${position - 12}.0`;
}

function zBlocks(filePath) {
  const buffer = fs.readFileSync(filePath);
  const blocks = [];
  for (let offset = 0; offset < buffer.length - 5; offset += 1) {
    if (buffer[offset] !== 0x7a) continue;
    const length = buffer.readUInt32BE(offset + 1);
    if (offset + 5 + length > buffer.length) continue;
    blocks.push({ offset, length, bytes: buffer.subarray(offset + 5, offset + 5 + length) });
  }
  return blocks;
}

function readInts(bytes) {
  const result = [];
  for (let offset = 0; offset + 4 <= bytes.length; offset += 4) {
    result.push(bytes.readInt32BE(offset));
  }
  return result;
}

function irPages(memory) {
  return [41, 83, 125, 167, 209, 251].map((address) => readSevenCommands(memory, address));
}

function mcuPage(memory) {
  return readSevenCommands(memory, 41);
}

function readSevenCommands(memory, baseAddress) {
  const page = [];
  for (let rest = 0; rest < 7; rest += 1) {
    const address = rest === 0 ? baseAddress : baseAddress - 42 + rest * 6;
    page.push(memory[address] * 16 + memory[address - 3]);
  }
  return page;
}

function trimTrailingZeroes(codes) {
  let end = codes.length;
  while (end > 0 && codes[end - 1] === 0) end -= 1;
  return codes.slice(0, end);
}

function formatListing(codes) {
  return codes.map((code, index) => `${formatAddress(index)}\t${formatCommand(code)}`).join('\n') + '\n';
}

function formatAddress(index) {
  if (index >= 100) return `A${index - 100}`;
  return String(index).padStart(2, '0');
}

function formatCommand(code) {
  if (code >= 0 && code <= 9) return String(code);
  const direct = {
    0x0a: '.', 0x0b: '/-/', 0x0c: 'ВП', 0x0d: 'Cx', 0x0e: 'В↑', 0x0f: 'FВx',
    0x10: '+', 0x11: '-', 0x12: '×', 0x13: '÷', 0x14: '↔',
    0x15: 'F10^x', 0x16: 'Fe^x', 0x17: 'FLg', 0x18: 'FLn',
    0x19: 'Farcsin', 0x1a: 'Farccos', 0x1b: 'Farctg', 0x1c: 'Fsin',
    0x1d: 'Fcos', 0x1e: 'Ftg', 0x20: 'Fπ', 0x21: 'F√', 0x22: 'Fx^2',
    0x23: 'F1/x', 0x24: 'Fx^y', 0x25: 'F↻', 0x26: 'КМГ',
    0x27: 'К-', 0x28: 'К*', 0x29: 'К÷', 0x2a: 'КМЧ',
    0x30: 'КЧМ', 0x31: 'К|x|', 0x32: 'КЗН', 0x33: 'КГМ',
    0x34: 'К[x]', 0x35: 'К{x}', 0x36: 'Кmax', 0x37: 'КΛ',
    0x38: 'К∨', 0x39: 'К⊕', 0x3a: 'КИНВ', 0x3b: 'КСЧ',
    0x50: 'С/П', 0x51: 'БП', 0x52: 'В/О', 0x53: 'ПП',
    0x54: 'КНОП', 0x55: 'К1', 0x56: 'К2',
    0x57: 'Fx≠0', 0x58: 'FL2', 0x59: 'Fx≥0', 0x5a: 'FL3',
    0x5b: 'FL1', 0x5c: 'Fx<0', 0x5d: 'FL0', 0x5e: 'Fx=0',
  };
  if (direct[code]) return direct[code];
  if ((code >= 0x1f && code <= 0x1f) || (code >= 0x2b && code <= 0x2f) || (code >= 0x3c && code <= 0x3f) || code === 0x5f) {
    return hex(code);
  }

  const register = REGISTER_NAMES[code & 0x0f];
  if (!register) return hex(code);
  const high = code & 0xf0;
  if (high === 0x40) return code === 0x4f ? '4F' : `хП${register}`;
  if (high === 0x60) return code === 0x6f ? '6F' : `Пх${register}`;
  if (high === 0x70) return code === 0x7f ? '7F' : `Кx≠0${register}`;
  if (high === 0x80) return code === 0x8f ? '8F' : `КБП${register}`;
  if (high === 0x90) return code === 0x9f ? '9F' : `Кx≥0${register}`;
  if (high === 0xa0) return code === 0xaf ? 'AF' : `КПП${register}`;
  if (high === 0xb0) return code === 0xbf ? 'BF' : `КхП${register}`;
  if (high === 0xc0) return code === 0xcf ? 'CF' : `Кx<0${register}`;
  if (high === 0xd0) return code === 0xdf ? 'DF' : `КПх${register}`;
  if (high === 0xe0) return code === 0xef ? 'EF' : `Кx=0${register}`;
  return hex(code);
}

const REGISTER_NAMES = ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e'];

function hex(code) {
  return code.toString(16).toUpperCase().padStart(2, '0');
}

function buildMarkdown(entry, txtName) {
  const lines = [`# ${entry.title}`, ''];
  if (entry.author) lines.push(`- Author/source credit: ${entry.author}`);
  lines.push(`- Source: [xvadim/pmk61-programms](${entry.sourceUrl})`);
  if (entry.sourceHtml) lines.push(`- Source description: \`${entry.sourceHtml}\``);
  lines.push(`- Program: [${txtName}](./${txtName})`);
  lines.push('');
  lines.push('## Description');
  lines.push('');
  for (const paragraph of entry.description) {
    lines.push(paragraph);
    lines.push('');
  }
  if (entry.setup?.length) {
    lines.push('## Setup');
    lines.push('');
    for (const item of entry.setup) lines.push(`- ${item}`);
    lines.push('');
  }
  lines.push('## Import Note');
  lines.push('');
  lines.push('The original source file is serialized Android emulator state. This repository keeps only the decoded clean MK-61 listing.');
  return lines.join('\n').trimEnd() + '\n';
}

function buildReadme(rows) {
  const lines = [
    '# xvadim PMK61 Programs',
    '',
    `Source: [xvadim/pmk61-programms](${REPO_URL})`,
    '',
    'This folder contains decoded clean listings from Android emulator `.pmk` saves. The serialized emulator state files are not kept.',
    '',
    '- `*.txt` - plain program listings, one command per addressed step.',
    '- `*.md` - short descriptions and setup notes.',
    '- `manifest.tsv` - tab-separated index with program file, description file, title, author/source credit, and source URL.',
    '',
    '| Program | Title | Steps |',
    '| --- | --- | ---: |',
  ];
  for (const row of rows) lines.push(`| \`${row.txtName}\` | ${row.title} | ${row.steps} |`);
  lines.push('', `Programs: **${rows.length}**.`);
  lines.push('', 'Re-import:');
  lines.push('');
  lines.push('```sh');
  lines.push('git clone --depth 1 https://github.com/xvadim/pmk61-programms.git /tmp/pmk61-programms-xvadim');
  lines.push('unzip /tmp/pmk61-programms-xvadim/wolves_and_goat_9x9.zip -d /tmp/pmk61-programms-xvadim/wolves_and_goat_9x9');
  lines.push('node scripts/import-xvadim-programs.cjs /tmp/pmk61-programms-xvadim');
  lines.push('```');
  return lines.join('\n') + '\n';
}

main();
