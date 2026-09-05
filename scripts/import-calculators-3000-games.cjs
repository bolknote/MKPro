#!/usr/bin/env node
'use strict';

const fs = require('fs');
const path = require('path');

const SOURCE_ROOT = path.resolve(process.argv[2] || '');
const OUT = path.resolve(
  process.argv[3] || path.join(__dirname, '../tmp/imports/calculators-3000'),
);
const SOURCE_PAGE = 'http://old-dos.ru/index.php?page=files&mode=files&do=show&id=4952';
const BOOK = 'Я. К. Трохименко, «Игры с микро-ЭВМ». Киев: «Техніка», 1986.';
const DECODER = new TextDecoder('windows-1251');

const TROKHIMENKO_SOURCE =
  `Я. К. Трохименко, Игры с микро-ЭВМ, Киев: Техніка, 1986; ${SOURCE_PAGE}`;
const C3000_SOURCE = `Калькуляторы 3000 6.2: ${SOURCE_PAGE}`;

const ENTRIES = [
  game('Game2.c3', 'gonki-s-prepyatstviyami-c3000', 'Гонки с препятствиями',
    'sports', ['book', 'classic', 'racing']),
  game('Game3.c3', 'gonki-po-krugovomu-treku-c3000', 'Гонки по круговому треку',
    'sports', ['book', 'classic', 'racing']),
  game('Game4.c3', 'avtoprobeg-c3000', 'Автопробег',
    'sports', ['book', 'classic', 'racing', 'variant'], 'avtoprobeg'),
  game('Game5.c3', 'parusnaya-regata-c3000', 'Парусная регата',
    'sports', ['book', 'classic', 'naval', 'racing']),
  game('Game6.c3', 'aviatsionnyy-perelet-c3000', 'Авиационный перелет',
    'simulation', ['aviation', 'book', 'classic']),
  game('Game7.c3', 'navesnaya-strelba-c3000', 'Навесная стрельба',
    'action', ['book', 'classic', 'military', 'variant'], 'navesnaya-strelba'),
  game('Game8.c3', 'tankovaya-ataka-c3000', 'Танковая атака',
    'strategy', ['book', 'classic', 'military', 'variant'], 'tankovaya-ataka'),
  game('Game9.c3', 'artilleriyskiy-boy-c3000', 'Артиллерийский бой',
    'action', ['book', 'classic', 'military']),
  game('Game10.c3', 'morskoe-srazhenie-c3000', 'Морское сражение',
    'strategy', ['book', 'classic', 'military', 'naval']),
  game('Game12.c3', 'arkticheskiy-reys-c3000', 'Арктический рейс',
    'simulation', ['book', 'classic', 'naval', 'variant'], 'arkticheskiy-reys'),
  game('Game13.c3', 'gonki-po-pryamoy-na-katerakh-c3000',
    'Гонки по прямой на катерах', 'sports', ['book', 'classic', 'naval', 'racing']),
  game('Game14.c3', 'shkola-kapitanov-na-podvodnykh-krylyakh-c3000',
    'Школа капитанов для судов на подводных крыльях',
    'simulation', ['book', 'classic', 'naval']),
  game('Game15.c3', 'avtogonki-po-pryamoy-c3000', 'Автогонки по прямой',
    'sports', ['book', 'classic', 'racing']),
  game('Game16.c3', 'vysshie-morekhodnye-kursy-c3000', 'Высшие мореходные курсы',
    'simulation', ['book', 'classic', 'naval']),
  game('Game17.c3', 'vozhdenie-avtomobilya-c3000', 'Вождение автомобиля',
    'simulation', ['book', 'classic']),
  game('Game18.c3', 'podvodnoe-puteshestvie-c3000', 'Подводное путешествие',
    'simulation', ['book', 'classic', 'naval']),
  game('Game19.c3', 'posadka-na-lunu-c3000', 'Посадка на Луну',
    'simulation', ['book', 'classic', 'space', 'variant'], 'lunar-landing'),
  game('Game20.c3', 'upravlenie-raketoy-c3000', 'Управление ракетой',
    'simulation', ['book', 'classic', 'space']),
  game('Game21.c3', 'ispytateli-vertoletov-c3000', 'Испытатели вертолетов',
    'simulation', ['aviation', 'book', 'classic']),
  game('Game22.c3', 'gigantskiy-slalom-c3000', 'Гигантский слалом',
    'sports', ['book', 'classic', 'racing']),
  game('Game23.c3', 'shkola-vysshego-pilotazha-c3000', 'Школа высшего пилотажа',
    'simulation', ['aviation', 'book', 'classic']),
  game('Game24.c3', 'polet-na-avialaynere-c3000', 'Полет на авиалайнере',
    'simulation', ['aviation', 'book', 'classic']),
  game('Chess3.c3', 'odinokiy-ferz-c3000', 'Одинокий ферзь',
    'logic', ['book', 'classic', 'vs-calculator']),
  game('Chess4.c3', 'odinokaya-peshka-c3000', 'Одинокая пешка',
    'logic', ['book', 'classic', 'vs-calculator']),
  game('Logic.c3', 'grandi-s-10-c3000', 'Гранди при S=10',
    'logic', ['book', 'classic', 'vs-calculator']),
  game('Logic2.c3', 'trigeks-c3000', 'Тригекс',
    'logic', ['book', 'classic', 'vs-calculator']),
  game('Logic3.c3', 'mushketery-belymi-c3000', 'Игра белыми в «Мушкетеры»',
    'logic', ['book', 'classic', 'vs-calculator']),
  game('Logic4.c3', 'igra-bashe-c3000', 'Игра Баше',
    'logic', ['book', 'classic', 'variant', 'vs-calculator'], 'bashe'),
  game('Logic5.c3', 'vybiranie-kamney-c3000', 'Выбирание камней',
    'logic', ['book', 'classic', 'vs-calculator']),
  game('Logic6.c3', 'nim-tri-mnozhestva-c3000',
    'Ним с тремя множествами предметов', 'logic', ['book', 'classic', 'vs-calculator']),
  game('Logic7.c3', 'igra-so-spichkami-c3000', 'Игра со спичками',
    'logic', ['book', 'classic', 'vs-calculator']),
  game('Верю-не-верю.c3', 'veryu-ne-veryu-zapisal-1-c3000',
    'Верю — не верю: «записал 1» (вариант 1)',
    'logic', ['book', 'classic', 'variant', 'vs-calculator'], 'veryu-ne-veryu'),
  game('Верю-не-верю_2.c3', 'veryu-ne-veryu-zapisal-1-2-c3000',
    'Верю — не верю: «записал 1» (вариант 2)',
    'logic', ['book', 'classic', 'variant', 'vs-calculator'], 'veryu-ne-veryu'),
  game('Чет-нечет.c3', 'chet-nechet-predydushchie-khody-c3000',
    'Чет — нечет с учетом предыдущих ходов',
    'logic', ['book', 'classic', 'vs-calculator'], 'chet-nechet'),
  game('Чет-нечет_2.c3', 'chet-nechet-korrelyatsiya-c3000',
    'Чет — нечет с учетом корреляции ходов противника',
    'logic', ['book', 'classic', 'vs-calculator'], 'chet-nechet'),
  {
    sourceFile: 'DirectionFinding.c3',
    slug: 'pelengatsiya-c3000',
    title: 'Пеленгация',
    author: 'Н. А. Александров',
    section: 'logic',
    tags: ['vs-calculator'],
    source: C3000_SOURCE,
    sourceNote: 'Карточка программы в «Калькуляторах 3000» 6.2.',
  },
  {
    sourceFile: 'KosmPosadka.c3',
    slug: 'kosmicheskaya-posadka-c3000',
    title: 'Космическая посадка',
    author: '',
    section: 'simulation',
    tags: ['classic', 'space'],
    source: `Электроника БРП-4; ${C3000_SOURCE}`,
    sourceNote: '«Электроника БРП-4» (как указано в карточке C3000).',
  },
  {
    sourceFile: 'Шашки(Алексей Чувыров).C3',
    slug: 'shashki-chuvyrov-c3000',
    title: 'Шашки',
    author: 'Алексей Чувыров',
    section: 'logic',
    tags: ['variant', 'vs-calculator'],
    source: C3000_SOURCE,
    sourceNote: 'Программа и инструкция Алексея Чувырова в «Калькуляторах 3000» 6.2.',
  },
  {
    sourceFile: 'Шашки(Олег Баран).C3',
    slug: 'shashki-baran-c3000',
    title: 'Шашки',
    author: 'Олег Баран',
    section: 'logic',
    tags: ['classic', 'magazine', 'variant', 'vs-calculator'],
    source: `Техника — молодежи, 1988, № 4; ${C3000_SOURCE}`,
    sourceNote: '«Техника — молодежи», 1988, № 4 (как указано в карточке C3000).',
  },
];

function game(sourceFile, slug, title, section, tags, series = '') {
  return {
    sourceFile,
    slug,
    title,
    author: 'Я. К. Трохименко',
    section,
    tags,
    series,
    source: TROKHIMENKO_SOURCE,
    sourceNote: BOOK,
  };
}

function main() {
  if (!process.argv[2]) {
    throw new Error(
      'Usage: node scripts/import-calculators-3000-games.cjs <Programs directory> [output directory]',
    );
  }
  if (!fs.existsSync(SOURCE_ROOT)) throw new Error(`Source directory not found: ${SOURCE_ROOT}`);

  const sourceNames = new Map(
    fs.readdirSync(SOURCE_ROOT).map((name) => [name.normalize('NFC').toLowerCase(), name]),
  );
  const rows = [];

  for (const entry of ENTRIES) {
    const actualName = sourceNames.get(entry.sourceFile.normalize('NFC').toLowerCase());
    if (!actualName) throw new Error(`Source program not found: ${entry.sourceFile}`);

    const card = decodeC3(path.join(SOURCE_ROOT, actualName));
    const listing = formatListing(card.codes);
    const sectionDir = path.join(OUT, entry.section);
    const txtName = `${entry.slug}.txt`;
    const mdName = `${entry.slug}.md`;
    fs.mkdirSync(sectionDir, { recursive: true });
    fs.writeFileSync(path.join(sectionDir, txtName), listing, 'utf8');
    fs.writeFileSync(
      path.join(sectionDir, mdName),
      buildMarkdown(entry, card, actualName, txtName),
      'utf8',
    );

    rows.push([
      `${entry.section}/${txtName}`,
      `${entry.section}/${mdName}`,
      entry.title,
      entry.author,
      '',
      entry.source,
      'game',
      entry.section,
      [...entry.tags].sort().join(';'),
      '',
      entry.series || '',
    ].join('\t'));
  }

  rows.sort((left, right) => left.localeCompare(right, 'en'));
  fs.mkdirSync(OUT, { recursive: true });
  fs.writeFileSync(path.join(OUT, 'manifest-rows.tsv'), `${rows.join('\n')}\n`, 'utf8');
  fs.writeFileSync(path.join(OUT, 'README.md'), buildReadme(), 'utf8');
  console.log(`Staged ${ENTRIES.length} entries in ${OUT}`);
}

function decodeC3(filePath) {
  const buffer = fs.readFileSync(filePath);
  if (buffer.subarray(0, 14).toString('ascii') === 'group Document') {
    return decodeTextC3(buffer, filePath);
  }
  if (buffer.subarray(0, 16).toString('ascii') === 'Calculators 3000') {
    return decodeBinaryC3(buffer, filePath);
  }
  throw new Error(`Unsupported C3 format: ${filePath}`);
}

function decodeTextC3(buffer, filePath) {
  const source = DECODER.decode(buffer);
  const field = (name) => {
    const match = source.match(new RegExp(`^  ${name} = "(.*)"\\r?$`, 'm'));
    return match ? decodeDelphiString(match[1]) : '';
  };
  const machine = source.match(/^MachineFileName = "([^"]*)"\r?$/m)?.[1] || '';
  const hex = source.match(/^    Data = ([0-9A-F]+)\r?$/m)?.[1];
  if (!hex) throw new Error(`ProgramMemory.Data not found in ${filePath}`);
  return {
    title: field('Title'),
    author: field('Author'),
    documentAuthor: field('DocumentAuthor'),
    description: normalizeDescription(field('Description')),
    machine,
    codes: trimUnusedCells([...Buffer.from(hex, 'hex')]),
  };
}

function decodeDelphiString(value) {
  return value
    .replace(/#13#10/g, '\r\n')
    .replace(/#(\d+)/g, (_, decimal) => String.fromCharCode(Number(decimal)));
}

function decodeBinaryC3(buffer, filePath) {
  const programMemory = binaryMarkerOffset(buffer, 'ProgramMemory');
  const dataOffset = binaryMarkerOffset(buffer, 'Data', programMemory);
  const dataLengthOffset = dataOffset + 1 + 'Data'.length;
  const dataLength = buffer.readUInt32LE(dataLengthOffset);
  const dataStart = dataLengthOffset + 4;
  if (dataStart + dataLength > buffer.length) {
    throw new Error(`Invalid ProgramMemory.Data length in ${filePath}`);
  }
  return {
    title: binaryString(buffer, 'Title'),
    author: binaryString(buffer, 'Author'),
    documentAuthor: binaryString(buffer, 'DocumentAuthor'),
    description: normalizeDescription(binaryString(buffer, 'Description')),
    machine: binaryString(buffer, 'MachineFileName'),
    codes: trimUnusedCells([...buffer.subarray(dataStart, dataStart + dataLength)]),
  };
}

function binaryString(buffer, name) {
  const offset = binaryMarkerOffset(buffer, name);
  const lengthOffset = offset + 1 + name.length;
  const length = buffer.readUInt32LE(lengthOffset);
  const start = lengthOffset + 4;
  if (start + length > buffer.length) throw new Error(`Invalid ${name} field length`);
  return DECODER.decode(buffer.subarray(start, start + length));
}

function binaryMarkerOffset(buffer, name, from = 0) {
  const nameBytes = Buffer.from(name, 'ascii');
  const marker = Buffer.concat([Buffer.from([nameBytes.length]), nameBytes]);
  const offset = buffer.indexOf(marker, from);
  if (offset < 0) throw new Error(`C3 field not found: ${name}`);
  return offset;
}

function trimUnusedCells(codes) {
  while (codes.length && codes.at(-1) === 0) codes.pop();
  return codes;
}

function normalizeDescription(value) {
  return value
    .replace(/\r\n?/g, '\n')
    .replace(/[ \t]+$/gm, '')
    .replace(/\n{3,}/g, '\n\n')
    .trim();
}

function formatListing(codes) {
  return codes
    .map((code, index) => `${formatAddress(index)}\t${formatCommand(code)}`)
    .join('\n') + '\n';
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
    0x54: 'КНОП',
    0x57: 'Fx≠0', 0x58: 'FL2', 0x59: 'Fx≥0', 0x5a: 'FL3',
    0x5b: 'FL1', 0x5c: 'Fx<0', 0x5d: 'FL0', 0x5e: 'Fx=0',
  };
  if (direct[code]) return direct[code];

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

function buildMarkdown(entry, card, actualName, txtName) {
  const lines = [`# ${entry.title}`, ''];
  if (entry.author) lines.push(`- Автор: ${entry.author}`);
  else lines.push('- Автор: не указан');
  lines.push(`- Первоисточник: ${entry.sourceNote}`);
  lines.push(`- Архивная копия: [«Калькуляторы 3000» 6.2](${SOURCE_PAGE})`);
  if (card.documentAuthor) lines.push(`- Автор карточки C3000: ${card.documentAuthor}`);
  lines.push(`- Исходный файл: \`${actualName.normalize('NFC')}\``);
  lines.push(`- Модель: ${friendlyMachine(card.machine)}`);
  lines.push(`- Программа: [${txtName}](${txtName})`);
  lines.push('', '## Описание из карточки C3000', '');
  lines.push(card.description || 'Описание в исходной карточке отсутствует.');
  lines.push('', '## Примечание об импорте', '');
  lines.push(
    'Листинг декодирован из файла `.c3`; состояние стека, регистров и индикатора эмулятора не переносилось.',
  );
  return `${lines.join('\n').trimEnd()}\n`;
}

function friendlyMachine(machine) {
  const normalized = machine.toUpperCase();
  if (normalized.includes('MK61')) return 'МК-61';
  if (normalized.includes('MK52')) return 'МК-52';
  if (normalized.includes('B334')) return 'Б3-34';
  return machine || 'не указана';
}

function buildReadme() {
  return [
    '# Calculators 3000 game import',
    '',
    `Source archive: [Old-DOS.ru](${SOURCE_PAGE})`,
    '',
    'The generated program/description pairs are already arranged by the repository genre taxonomy.',
    'Review them and merge the rows from `manifest-rows.tsv` into `games/manifest.tsv`.',
    'The source corpus is provenance only: the manifest collection field is intentionally empty.',
    '',
  ].join('\n');
}

if (require.main === module) main();

module.exports = {
  ENTRIES,
  decodeC3,
  formatListing,
};
