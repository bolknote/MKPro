'use strict';

const ROM = require('./rom.cjs');

const DIGIT_SYMBOLS = ['0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '-', 'L', 'С', 'Г', 'Е', ' '];

const MEMORY_PAGE_ADDRESSES = [
  [1, 41], [1, 83], [1, 125], [1, 167], [1, 209], [1, 251],
  [2, 41], [2, 83], [2, 125], [2, 167], [2, 209], [2, 251],
  [3, 41], [4, 41], [5, 41],
];

const PAGE_PERMUTATIONS_EXTENDED = [
  [1, 2, 3, 4, 5, 14, 13, 12, 6, 7, 8, 9, 10, 11, 0],
  [10, 11, 6, 7, 2, 3, 4, 5, 0, 1, 14, 13, 12, 8, 9],
  [14, 13, 12, 10, 11, 6, 7, 8, 9, 4, 5, 0, 1, 2, 3],
];

const PAGE_PERMUTATIONS_BASIC = [
  [1, 2, 3, 4, 5, 13, 12, 6, 7, 8, 9, 10, 11, 0],
  [3, 4, 5, 0, 1, 13, 12, 8, 9, 10, 11, 6, 7, 2],
  [5, 0, 1, 2, 3, 13, 12, 10, 11, 6, 7, 8, 9, 4],
];

const STACK_ADDRESSES = [
  [1, 34], [1, 76], [1, 118], [1, 160], [1, 202], [1, 244],
  [2, 34], [2, 76], [2, 118], [2, 160], [2, 202], [2, 244],
  [3, 34], [4, 34], [5, 34],
];

const STACK_PERMUTATIONS_EXTENDED = [
  [8, 9, 10, 11, 0],
  [14, 13, 12, 8, 9],
  [5, 0, 1, 2, 3],
];

const STACK_PERMUTATIONS_BASIC = [
  [8, 9, 10, 11, 0],
  [10, 11, 6, 7, 2],
  [6, 7, 8, 9, 4],
];

const KEY_CODES = {
  '0': [2, 1],
  '1': [3, 1],
  '2': [4, 1],
  '3': [5, 1],
  '4': [6, 1],
  '5': [7, 1],
  '6': [8, 1],
  '7': [9, 1],
  '8': [10, 1],
  '9': [11, 1],
  ',': [7, 8],
  '.': [7, 8],
  '/-/': [8, 8],
  '+/-': [8, 8],
  'ВП': [9, 8],
  'EXP': [9, 8],
  'CX': [10, 8],
  'СХ': [10, 8],
  'Сx': [10, 8],
  'Cx': [10, 8],
  'В↑': [11, 8],
  'ENTER': [11, 8],
  '+': [2, 8],
  '-': [3, 8],
  '*': [4, 8],
  '×': [4, 8],
  '/': [5, 8],
  '÷': [5, 8],
  '↔': [6, 8],
  'X↔Y': [6, 8],
  'F': [11, 9],
  'K': [10, 9],
  'К': [10, 9],
  'П→X': [8, 9],
  'ПX': [8, 9],
  'ИП': [8, 9],
  'X→П': [6, 9],
  'XП': [6, 9],
  'П': [6, 9],
  'БП': [3, 9],
  'ПП': [5, 9],
  'В/О': [4, 9],
  'С/П': [2, 9],
  'ШГ→': [7, 9],
  'ШГ←': [9, 9],
};

const MNEMONICS = [
  [0x15, ['10^x', '10x', 'F10^x', 'F10x'], 0],
  [0x54, ['НОП', 'KНОП', 'КНОП'], 0],
  [0x16, ['e^x', 'ex', 'Fe^x', 'Fex'], 0],
  [0x17, ['lg', 'Flg'], 0],
  [0x18, ['ln', 'Fln'], 0],
  [0x30, ['ЧМ', 'KЧМ', 'КЧМ', 'K°←′"', 'К°←′"'], 0],
  [0x19, ['arcsin', 'Farcsin'], 0],
  [0x31, ['|x|', 'K|x|', 'К|x|'], 0],
  [0x1A, ['arccos', 'Farccos'], 0],
  [0x32, ['ЗН', 'KЗН', 'КЗН'], 0],
  [0x1B, ['arctg', 'Farctg'], 0],
  [0x33, ['ГМ', 'KГМ', 'КГМ', 'K°←′', 'К°←′'], 0],
  [0x1C, ['sin', 'Fsin'], 0],
  [0x34, ['[x]', 'K[x]', 'К[x]'], 0],
  [0x1D, ['cos', 'Fcos'], 0],
  [0x35, ['{x}', '(x)', 'K{x}', 'К{x}', 'K(x)', 'К(x)'], 0],
  [0x1E, ['tg', 'Ftg'], 0],
  [0x36, ['max', 'Kmax', 'Кmax'], 0],
  [0x10, ['+'], 0],
  [0x11, ['-', '–'], 0],
  [0x12, ['*', 'x', 'х', '×', '⋅'], 0],
  [0x13, ['/', ':', '÷'], 0],
  [0x20, ['пи', 'π', 'pi', 'Fпи', 'Fπ', 'Fpi'], 0],
  [0x26, ['МГ', 'KМГ', 'КМГ', 'K+', 'К+', 'K°→′', 'К°→′'], 0],
  [0x21, ['КвКор', 'квкор', 'корень', '√', 'FКвКор', 'Fквкор', 'Fкорень', 'F√'], 0],
  [0x22, ['x^2', 'x2', 'x²', 'Fx^2', 'Fx2', 'Fx²'], 0],
  [0x23, ['1/x', 'F1/x'], 0],
  [0x14, ['<->', 'XY', '↔', 'X↔Y'], 0],
  [0x0E, ['^', 'В^', '↑', 'В↑'], 0],
  [0x24, ['x^y', 'xy', 'Fx^y', 'Fxy'], 0],
  [0x27, ['K-', 'К-'], 0],
  [0x28, ['Kx', 'Кх', 'K*', 'К*'], 0],
  [0x29, ['K/', 'К/', 'K:', 'К:', 'K÷', 'К÷'], 0],
  [0x2A, ['МЧ', 'KМЧ', 'КМЧ', 'K°→′"', 'К°→′"'], 0],
  [0x0F, ['Вx', 'Bx', 'FВx', 'FBx'], 0],
  [0x3B, ['СЧ', 'KСЧ', 'КСЧ'], 0],
  [0x0A, [',', '.'], 0],
  [0x0B, ['/-/', '+/-'], 0],
  [0x0C, ['ВП'], 0],
  [0x0D, ['Сx', 'Cx'], 0],
  [0x25, ['->', '↻', '→', 'F->', 'F↻', 'F→'], 0],
  [0x37, ['/\\', '⋀', '∧', 'Λ', 'K/\\', 'К/\\', 'K⋀', 'К⋀', 'K∧', 'К∧', 'KΛ', 'КΛ'], 0],
  [0x38, ['\\/', '⋁', 'V', '∨', 'K\\/', 'К\\/', 'K⋁', 'К⋁', 'KV', 'КV', 'K∨', 'К∨'], 0],
  [0x39, ['(+)', '⊕', 'K(+)', 'К(+)', 'K⊕', 'К⊕'], 0],
  [0x3A, ['ИНВ', 'KИНВ', 'КИНВ'], 0],
  [0x52, ['В/О', 'В/0'], 0],
  [0x50, ['С/П'], 0],
  [0x59, ['x>=0', 'x≥0', 'x⩾0', 'Fx>=0', 'Fx≥0', 'Fx⩾0'], 2],
  [0x57, ['x#0', 'x!=0', 'x<>0', 'x≠0', 'Fx#0', 'Fx!=0', 'Fx<>0', 'Fx≠0'], 2],
  [0x51, ['БП'], 2],
  [0x53, ['ПП'], 2],
  [0x58, ['L2', 'FL2'], 2],
  [0x5A, ['L3', 'FL3'], 2],
  [0x5C, ['x<0', 'Fx<0'], 2],
  [0x5E, ['x=0', 'Fx=0'], 2],
  [0x5D, ['L0', 'FL0'], 2],
  [0x5B, ['L1', 'FL1'], 2],
  [0x40, ['П', 'XП', 'X->П'], 1],
  [0x60, ['ИП', 'ПX', 'Пx', 'П->X'], 1],
  [0x70, ['Kx#0', 'Кx#0', 'Kx!=0', 'Кx!=0', 'Kx<>0', 'Кx<>0', 'Kx≠0', 'Кx≠0'], 1],
  [0x80, ['KБП', 'КБП'], 1],
  [0x90, ['Kx>=0', 'Кx>=0', 'Kx≥0', 'Кx≥0', 'Kx⩾0', 'Кx⩾0'], 1],
  [0xA0, ['KПП', 'КПП'], 1],
  [0xB0, ['KП', 'КП', 'KXП', 'КXП', 'KX->П', 'КX->П'], 1],
  [0xC0, ['Kx<0', 'Кx<0'], 1],
  [0xD0, ['KИП', 'КИП', 'KПX', 'КПX', 'KП->X', 'КП->X'], 1],
  [0xE0, ['Kx=0', 'Кx=0'], 1],
];

class IR2 {
  constructor() {
    this.reset();
  }

  reset() {
    this.memory = new Array(252).fill(0);
    this.input = 0;
    this.output = 0;
    this.tickIndex = 0;
  }

  tick() {
    this.output = this.memory[this.tickIndex];
    this.memory[this.tickIndex] = this.input;
    this.tickIndex++;
    if (this.tickIndex === 252) this.tickIndex = 0;
  }
}

class IK13 {
  constructor(rom) {
    this.microcommandRom = rom.микрокоманды;
    this.syncRom = rom.синхропрограммы;
    this.commandRom = rom.команды;
    this.reset();
  }

  reset() {
    this.memory = new Array(42).fill(0);
    this.r = new Array(42).fill(0);
    this.stack = new Array(42).fill(0);
    this.s = 0;
    this.s1 = 0;
    this.l = 0;
    this.t = 0;
    this.carry = 0;
    this.j = new Array(42).fill(0).map((_, i) => (i < 6 ? i : i < 21 ? i % 3 + 3 : i % 9));
    this.tickIndex = 0;
    this.command = 0;
    this.syncAddress = 0;
    this.input = 0;
    this.output = 0;
    this.keyX = 0;
    this.keyY = 0;
    this.commas = new Array(14).fill(false);
  }

  executeMicroOrder(number, acc) {
    switch (number) {
      case 0: acc.alpha |= this.r[this.tickIndex]; break;
      case 1: acc.alpha |= this.memory[this.tickIndex]; break;
      case 2: acc.alpha |= this.stack[this.tickIndex]; break;
      case 3: acc.alpha |= ~this.r[this.tickIndex] & 0b1111; break;
      case 4: if (this.l === 0) acc.alpha |= 0xA; break;
      case 5: acc.alpha |= this.s; break;
      case 6: acc.alpha |= 4; break;
      case 7: acc.beta |= this.s; break;
      case 8: acc.beta |= ~this.s & 0b1111; break;
      case 9: acc.beta |= this.s1; break;
      case 10: acc.beta |= 6; break;
      case 11: acc.beta |= 1; break;
      case 12: acc.gamma |= this.l & 1; break;
      case 13: acc.gamma |= ~this.l & 1; break;
      case 14: acc.gamma |= ~this.t & 1; break;
      case 15: this.r[this.tickIndex] = this.r[(this.tickIndex + 3) % 42]; break;
      case 16: this.r[this.tickIndex] = acc.sigma; break;
      case 17: this.r[this.tickIndex] = this.s; break;
      case 18: this.r[this.tickIndex] = this.r[this.tickIndex] | this.s | acc.sigma; break;
      case 19: this.r[this.tickIndex] = this.s | acc.sigma; break;
      case 20: this.r[this.tickIndex] = this.r[this.tickIndex] | this.s; break;
      case 21: this.r[this.tickIndex] = this.r[this.tickIndex] | acc.sigma; break;
      case 22: this.r[(this.tickIndex + 41) % 42] = acc.sigma; break;
      case 23: this.r[(this.tickIndex + 40) % 42] = acc.sigma; break;
      case 24: this.memory[this.tickIndex] = this.s; break;
      case 25: this.l = this.carry; break;
      case 26: this.s = this.s1; break;
      case 27: this.s = acc.sigma; break;
      case 28: this.s = this.s1 | acc.sigma; break;
      case 29: this.s1 = acc.sigma; break;
      case 30: break;
      case 31: this.s1 = this.s1 | acc.sigma; break;
      case 32:
        this.stack[(this.tickIndex + 2) % 42] = this.stack[(this.tickIndex + 1) % 42];
        this.stack[(this.tickIndex + 1) % 42] = this.stack[this.tickIndex];
        this.stack[this.tickIndex] = acc.sigma;
        break;
      case 33: {
        const x = this.stack[this.tickIndex];
        this.stack[this.tickIndex] = this.stack[(this.tickIndex + 1) % 42];
        this.stack[(this.tickIndex + 1) % 42] = this.stack[(this.tickIndex + 2) % 42];
        this.stack[(this.tickIndex + 2) % 42] = x;
        break;
      }
      case 34: {
        const x = this.stack[this.tickIndex];
        const y = this.stack[(this.tickIndex + 1) % 42];
        const z = this.stack[(this.tickIndex + 2) % 42];
        this.stack[this.tickIndex] = acc.sigma | y;
        this.stack[(this.tickIndex + 1) % 42] = x | z;
        this.stack[(this.tickIndex + 2) % 42] = y | x;
        break;
      }
    }
  }

  tick() {
    if (this.tickIndex === 0) {
      this.command = this.commandRom[this.r[36] + 16 * this.r[39]];
      if (((this.command >>> 16) & 0b111111) === 0) this.t = 0;
    }

    const nine = this.tickIndex / 9 | 0;
    const tickInNine = this.tickIndex - nine * 9;
    if (tickInNine === 0 && !(nine > 0 && nine < 3)) {
      if (nine < 3) this.syncAddress = this.command & 0b1111111;
      else if (nine === 3) this.syncAddress = (this.command >>> 7) & 0b1111111;
      else if (nine === 4) {
        this.syncAddress = (this.command >>> 14) & 0b11111111;
        if (this.syncAddress > 31) {
          if (this.tickIndex === 36) {
            this.r[37] = this.syncAddress & 0b1111;
            this.r[40] = this.syncAddress >>> 4;
          }
          this.syncAddress = 95;
        }
      }
    }

    let syncMicrocommand = this.syncRom[this.syncAddress * 9 + this.j[this.tickIndex]];
    syncMicrocommand &= 0b111111;
    if (syncMicrocommand > 59) {
      syncMicrocommand = (syncMicrocommand - 60) * 2;
      if (this.l === 0) syncMicrocommand++;
      syncMicrocommand += 60;
    }

    let microcommand = this.microcommandRom[syncMicrocommand];
    const microOrders = [];
    for (let i = 0; i < 28; i++) {
      microOrders.push(microcommand & 1);
      microcommand >>>= 1;
    }

    const trio = this.tickIndex / 3 | 0;
    const acc = { alpha: 0, beta: 0, gamma: 0, sigma: 0 };

    if (microOrders[25] === 1 && trio !== this.keyX - 1) this.s1 |= this.keyY;

    for (let i = 0; i < 12; i++) {
      if (microOrders[i] === 1) this.executeMicroOrder(i, acc);
    }

    if (((this.command >>> 16) & 0b111111) > 0) {
      if (this.keyY === 0) this.t = 0;
    } else {
      if (trio === this.keyX - 1 && this.keyY > 0) {
        this.s1 = this.keyY;
        this.t = 1;
      }
      this.commas[trio] = this.l > 0;
    }

    for (let i = 12; i < 15; i++) {
      if (microOrders[i] === 1) this.executeMicroOrder(i, acc);
    }

    const sum = acc.alpha + acc.beta + acc.gamma;
    acc.sigma = sum & 0b1111;
    this.carry = (sum >>> 4) & 1;

    if (((this.command >>> 22) & 1) === 0 || nine === 4) {
      const field = (microOrders[17] << 2) | (microOrders[16] << 1) | microOrders[15];
      if (field > 0) this.executeMicroOrder(field + 14, acc);
      for (let i = 18; i < 20; i++) {
        if (microOrders[i] === 1) this.executeMicroOrder(i + 4, acc);
      }
    }

    for (let i = 20; i < 22; i++) {
      if (microOrders[i] === 1) this.executeMicroOrder(i + 4, acc);
    }

    for (let i = 0; i < 3; i++) {
      const field = (microOrders[23 + i * 2] << 1) | microOrders[22 + i * 2];
      if (field > 0) this.executeMicroOrder(field + 25 + i * 3, acc);
    }

    this.output = this.memory[this.tickIndex];
    this.memory[this.tickIndex] = this.input;
    this.tickIndex++;
    if (this.tickIndex === 42) this.tickIndex = 0;
  }

  closeRing(value) {
    this.memory[(this.tickIndex + 41) % 42] = value;
  }
}

class MK61 {
  constructor(options = {}) {
    this.extended = options.extended !== false;
    this.angleMode = parseAngleMode(options.angleMode || options.angle || 'rad');
    this.ir2a = new IR2();
    this.ir2b = new IR2();
    this.ik1302 = new IK13(ROM.ИК1302);
    this.ik1303 = new IK13(ROM.ИК1303);
    this.ik1306 = new IK13(ROM.ИК1306);
    this.displayDigits = [];
    this.displayCommas = [];
    this.powered = false;
    this.powerOn();
  }

  reset() {
    this.ir2a.reset();
    this.ir2b.reset();
    this.ik1302.reset();
    this.ik1303.reset();
    this.ik1306.reset();
    this.displayDigits = [];
    this.displayCommas = [];
  }

  powerOn() {
    if (this.powered) return this;
    this.powered = true;
    this.frame();
    return this;
  }

  powerOff() {
    this.powered = false;
    this.reset();
    return this;
  }

  tick() {
    this.ik1302.input = this.ir2b.output;
    this.ik1302.tick();
    this.ik1303.input = this.ik1302.output;
    this.ik1303.tick();
    if (this.extended) {
      this.ik1306.input = this.ik1303.output;
      this.ik1306.tick();
      this.ir2a.input = this.ik1306.output;
    } else {
      this.ir2a.input = this.ik1303.output;
    }
    this.ir2a.tick();
    this.ir2b.input = this.ir2a.output;
    this.ir2b.tick();
    this.ik1302.closeRing(this.ir2b.output);
  }

  frame(pressedKey = null) {
    this.ik1303.keyY = 1;
    this.ik1303.keyX = this.angleMode;

    if (pressedKey) {
      const [x, y] = normalizeKey(pressedKey);
      this.ik1302.keyX = x;
      this.ik1302.keyY = y;
    }

    for (let i = 0; i < 560; i++) {
      for (let j = 0; j < 42; j++) this.tick();
    }

    this.updateDisplay();
    this.ik1302.keyX = 0;
    this.ik1302.keyY = 0;
    return this;
  }

  press(keyOrX, y = undefined) {
    const key = y === undefined ? keyOrX : [keyOrX, y];
    this.frame(key);
    this.frame();
    return this;
  }

  pressSequence(keys) {
    for (const key of keys) this.press(key);
    return this;
  }

  inputNumber(value, options = {}) {
    const text = String(value).trim().replace('.', ',').toUpperCase();
    if (options.clear) this.press('Сx');
    for (const ch of text) {
      if (/[0-9]/.test(ch)) this.press(ch);
      else if (ch === ',' || ch === '.') this.press(',');
      else if (ch === '-') this.press('/-/');
      else if (ch === 'E') this.press('ВП');
      else if (ch !== ' ') throw new Error(`Unsupported input character: ${ch}`);
    }
    return this;
  }

  runFrames(count) {
    for (let i = 0; i < count; i++) this.frame();
    return this;
  }

  runUntilStable(options = {}) {
    const maxFrames = options.maxFrames || 200;
    const stableFrames = options.stableFrames || 4;
    let previous = null;
    let stable = 0;
    for (let frame = 0; frame < maxFrames; frame++) {
      this.frame();
      const signature = `${this.programCounter()}|${this.displayText()}`;
      if (signature === previous) {
        stable++;
        if (stable >= stableFrames) return { stopped: true, frames: frame + 1, signature };
      } else {
        previous = signature;
        stable = 0;
      }
    }
    return { stopped: false, frames: maxFrames, signature: previous };
  }

  updateDisplay() {
    const digits = [];
    const commas = [];
    for (let j = 0; j < 9; j++) digits[j] = this.ik1302.r[(8 - j) * 3];
    for (let j = 0; j < 3; j++) digits[j + 9] = this.ik1302.r[(11 - j) * 3];
    for (let j = 0; j < 9; j++) commas[j] = this.ik1302.commas[9 - j];
    for (let j = 0; j < 3; j++) commas[j + 9] = this.ik1302.commas[12 - j];
    this.displayDigits = digits;
    this.displayCommas = commas;
  }

  displayCells() {
    return this.displayDigits.map((digit, index) => ({
      symbol: DIGIT_SYMBOLS[digit] || '?',
      comma: Boolean(this.displayCommas[index]),
      digit,
    }));
  }

  displayText(options = {}) {
    const cells = this.displayCells();
    const raw = cells
      .map((cell) => cell.symbol + (cell.comma ? ',' : ' '))
      .join('');
    if (options.raw) return raw;
    return cells.map((cell) => cell.symbol + (cell.comma ? ',' : '')).join('').trim();
  }

  programCounter() {
    const high = DIGIT_SYMBOLS[this.ik1302.r[34]] || '?';
    const low = DIGIT_SYMBOLS[this.ik1302.r[31]] || '?';
    return `${high}${low}`;
  }

  memoryPhase() {
    return this.ir2a.tickIndex / 84 | 0;
  }

  syncMemoryPhase(target = 1) {
    for (let i = 0; i < 3; i++) {
      if (this.ir2a.tickIndex === target * 84) return this;
      this.frame();
    }
    throw new Error(`Cannot sync memory phase to ${target}; current tick is ${this.ir2a.tickIndex}`);
  }

  commandLimit() {
    return this.extended ? 105 : 98;
  }

  commandAddress(index, phase = this.memoryPhase()) {
    const whole = index / 7 | 0;
    const rest = index % 7;
    const permutation = this.extended ? PAGE_PERMUTATIONS_EXTENDED : PAGE_PERMUTATIONS_BASIC;
    const base = MEMORY_PAGE_ADDRESSES[permutation[phase][whole]];
    if (rest === 0) return base;
    return [base[0], base[1] - 42 + rest * 6];
  }

  writeCommand(index, code, phase = this.memoryPhase()) {
    const [chip, address] = this.commandAddress(index, phase);
    const hi = code / 16 | 0;
    const lo = code % 16;
    const memory = this.memoryForChip(chip);
    memory[address] = hi;
    memory[address - 3] = lo;
  }

  readCommand(index, phase = this.memoryPhase()) {
    const [chip, address] = this.commandAddress(index, phase);
    const memory = this.memoryForChip(chip);
    return memory[address] * 16 + memory[address - 3];
  }

  readProgramCodes(count = this.commandLimit()) {
    this.syncMemoryPhase(1);
    const codes = [];
    for (let i = 0; i < count; i++) codes.push(this.readCommand(i, 1));
    return codes;
  }

  loadProgram(program, options = {}) {
    this.powerOn();
    if (options.syncPhase !== false) this.syncMemoryPhase(1);

    const parsed = Array.isArray(program) ? { codes: program, assignments: [], diagnostics: [] } : parseProgramText(program);
    for (const assignment of parsed.assignments) this.setRegister(assignment.register, assignment.value, { syncPhase: false });

    const limit = this.commandLimit();
    if (parsed.codes.length > limit) {
      parsed.diagnostics.push(`Program was truncated from ${parsed.codes.length} to ${limit} commands.`);
    }

    const count = Math.min(parsed.codes.length, limit);
    for (let i = 0; i < count; i++) this.writeCommand(i, parsed.codes[i], 1);
    if (options.clearRest !== false) {
      for (let i = count; i < limit; i++) this.writeCommand(i, 0, 1);
    }
    return parsed;
  }

  setRegister(register, value, options = {}) {
    this.powerOn();
    if (options.syncPhase !== false) this.syncMemoryPhase(1);

    const target = normalizeRegister(register);
    const parsed = parseNumberLiteral(value);
    let address;
    if (target.kind === 'memory') {
      const permutation = this.extended ? PAGE_PERMUTATIONS_EXTENDED : PAGE_PERMUTATIONS_BASIC;
      address = MEMORY_PAGE_ADDRESSES[permutation[1][target.index]];
      this.writeNumber(address[0], address[1] - 8, parsed);
    } else {
      const permutation = this.extended ? STACK_PERMUTATIONS_EXTENDED : STACK_PERMUTATIONS_BASIC;
      address = STACK_ADDRESSES[permutation[1][target.index]];
      this.writeNumber(address[0], address[1], parsed);
    }
    return this;
  }

  readRegister(register, options = {}) {
    if (options.syncPhase !== false) this.syncMemoryPhase(1);
    const target = normalizeRegister(register);
    let address;
    if (target.kind === 'memory') {
      const permutation = this.extended ? PAGE_PERMUTATIONS_EXTENDED : PAGE_PERMUTATIONS_BASIC;
      address = MEMORY_PAGE_ADDRESSES[permutation[1][target.index]];
      return this.readNumber(address[0], address[1] - 8);
    }
    const permutation = this.extended ? STACK_PERMUTATIONS_EXTENDED : STACK_PERMUTATIONS_BASIC;
    address = STACK_ADDRESSES[permutation[1][target.index]];
    return this.readNumber(address[0], address[1]);
  }

  writeNumber(chip, address, number) {
    const memory = this.memoryForChip(chip);
    memory[address] = number.exponentSign === '-' ? 9 : 0;
    memory[address - 3] = number.exponent / 10 | 0;
    memory[address - 6] = number.exponent % 10;
    memory[address - 9] = number.mantissaSign === '-' ? 9 : 0;
    for (let i = 0; i < 8; i++) {
      memory[address - 3 * (i + 4)] = number.mantissa[i] ? parseInt(number.mantissa[i], 16) : 0;
    }
  }

  readNumber(chip, address) {
    const memory = this.memoryForChip(chip);
    let exponent = memory[address - 3] * 10 + memory[address - 6];
    if (memory[address] === 9) exponent = -(100 - exponent);
    let index = 0;
    while (memory[address - 33 + index * 3] === 0) {
      if (exponent === 7 - index || index === 7) break;
      index++;
    }
    const digits = [];
    while (index < 8) {
      digits.push(memory[address - 33 + index * 3]);
      index++;
    }
    digits.reverse();
    let mantissa = memory[address - 9] === 9 ? '-' : '';
    let comma = false;
    for (index = 0; index < digits.length; index++) {
      mantissa += DIGIT_SYMBOLS[digits[index]];
      if ((index === 0 && (exponent < 0 || exponent > 7)) || index === exponent) {
        mantissa += ',';
        comma = true;
      }
    }
    if (!comma) mantissa += ',';
    if (exponent < 0 || exponent > 7) return `${mantissa.padEnd(12, ' ')}${exponent}`;
    return mantissa;
  }

  memoryForChip(chip) {
    switch (chip) {
      case 1: return this.ir2a.memory;
      case 2: return this.ir2b.memory;
      case 3: return this.ik1302.memory;
      case 4: return this.ik1303.memory;
      case 5: return this.ik1306.memory;
      default: throw new Error(`Unknown chip number: ${chip}`);
    }
  }
}

function parseProgramText(text) {
  const diagnostics = [];
  const assignments = [];
  let source = String(text);

  const setup = source.match(/`([\s\S]+?)`/);
  if (setup) {
    source = source.replace(setup[0], ' ');
    for (const part of setup[1].split(';')) {
      const trimmed = part.replace(/\s+/g, '');
      if (!trimmed) continue;
      const match = trimmed.match(/^[РR]?([0-9A-EАВСДЕXYZT]|X1)=(-?[0-9A-FАВСДЕLСГЕ_\-,.]+(?:E-?[0-9]{1,2})?)$/i);
      if (!match) {
        diagnostics.push(`Cannot parse setup item: ${part.trim()}`);
        continue;
      }
      assignments.push({ register: match[1], value: match[2] });
    }
  }

  const addressedLines = source.split(/\r?\n/)
    .map((line) => line.match(/^\s*([0-9A-F.-]{2})\s+(.+?)\s*$/i))
    .filter(Boolean)
    .map((match) => `${match[1].toUpperCase()}.${match[2]}`);
  const tokens = addressedLines.length > 0 ? addressedLines : (source.trim() ? source.trim().split(/\s+/) : []);
  const labels = new Map();
  const commands = [];

  for (const token of tokens) {
    const label = token.match(/^(([0-9A-F.-]{2})\.)?(.+):$/i);
    if (label) {
      const address = label[2] ? parseProgramAddress(label[2]) : commands.length;
      labels.set(normalizeLabel(label[3]), address);
    } else {
      commands.push(token);
    }
  }

  const codes = [];
  let writeIndex = 0;
  for (const command of commands) {
    const parsed = parseCommandToken(command, writeIndex, labels);
    const index = parsed.address !== null ? parsed.address : writeIndex;
    codes[index] = parsed.code;
    writeIndex = index + 1;
    if (parsed.warning) diagnostics.push(parsed.warning);
  }

  for (let i = 0; i < codes.length; i++) {
    if (codes[i] === undefined) codes[i] = 0;
  }
  return { codes, assignments, diagnostics };
}

function parseCommandToken(token, fallbackAddress, labels = new Map()) {
  let command = String(token).trim();
  let address = null;
  const addressed = command.match(/^([0-9A-F.-]{2})\.(.*)$/i);
  if (addressed) {
    address = parseProgramAddress(addressed[1]);
    command = addressed[2];
  }
  if (command === '') return { address, code: 0 };

  const normalized = normalizeMnemonic(command);
  for (const [baseCode, aliases, registerMode] of MNEMONICS) {
    for (const alias of aliases) {
      const normalizedAlias = normalizeMnemonic(alias);
      if (registerMode === 1) {
        if (normalized.startsWith(normalizedAlias) && normalized.length > normalizedAlias.length) {
          const register = parseRegisterNibble(normalized.slice(normalizedAlias.length));
          if (register !== null) return { address, code: baseCode + register };
        }
      } else if (normalized === normalizedAlias) {
        return { address, code: baseCode };
      }
    }
  }

  const labelAddress = labels.get(normalizeLabel(command));
  if (labelAddress !== undefined) return { address, code: addressNumberToOpcode(labelAddress) };

  const hex = normalized.replace(/[.-]/g, 'A');
  if (/^[0-9A-F]{1,2}$/.test(hex)) return { address, code: parseInt(hex, 16) };

  return {
    address,
    code: 0,
    warning: `Command at ${fallbackAddress} was not recognized: ${token}`,
  };
}

function parseNumberLiteral(value) {
  let text = String(value).trim().toUpperCase();
  if (/^-?\d+\.\d+(E-?\d{1,2})?$/.test(text)) text = text.replace('.', ',');
  text = text.replace(/[AВB]/g, (ch) => (ch === 'A' ? '-' : ch === 'B' ? 'L' : 'В'));
  // Latin hex nibbles C/D/F map to the register glyph alphabet (E stays the
  // exponent marker; nibble E is the Cyrillic Е). This lets compiler-owned
  // indirect-flow selectors such as "C3" or "D0" be preloaded directly.
  text = text.replace(/C/g, 'С').replace(/D/g, 'Г').replace(/F/g, '_');
  const match = text.match(/^(-)?([0-9\-LСГЕ_]+)(?:,([0-9\-LСГЕ_]+))?(?:E(-)?([0-9]{1,2}))?$/);
  if (!match) throw new Error(`Cannot parse MK-61 number literal: ${value}`);

  const integerPart = match[2];
  const fractionalPart = match[3] || '';
  let exponent = match[5] ? parseInt(match[5], 10) : 0;
  exponent += (integerPart.length - 1) * (match[4] === '-' ? -1 : 1);
  if (match[4] === '-') exponent = 100 - exponent;
  if (exponent >= 100) throw new Error(`Number exponent is out of range: ${value}`);

  return {
    mantissaSign: match[1] === '-' ? '-' : '+',
    mantissa: (integerPart + fractionalPart)
      .replace(/-/g, 'A')
      .replace(/L/g, 'B')
      .replace(/С/g, 'C')
      .replace(/Г/g, 'D')
      .replace(/Е/g, 'E')
      .replace(/_/g, 'F')
      .split(''),
    exponentSign: match[4] === '-' ? '-' : '+',
    exponent,
  };
}

function normalizeKey(key) {
  if (Array.isArray(key)) return [Number(key[0]), Number(key[1])];
  const normalized = normalizeMnemonic(String(key));
  for (const [name, code] of Object.entries(KEY_CODES)) {
    if (normalizeMnemonic(name) === normalized) return code;
  }
  throw new Error(`Unknown key: ${key}`);
}

function normalizeRegister(register) {
  let text = String(register).trim().toUpperCase().replace(/^Р|^R/, '');
  text = text.replace(/А/g, 'A').replace(/В/g, 'B').replace(/С/g, 'C').replace(/Д/g, 'D').replace(/Е/g, 'E');
  if (text === 'X1') return { kind: 'stack', index: 0 };
  if (text === 'X') return { kind: 'stack', index: 1 };
  if (text === 'Y') return { kind: 'stack', index: 2 };
  if (text === 'Z') return { kind: 'stack', index: 3 };
  if (text === 'T') return { kind: 'stack', index: 4 };
  const index = parseInt(text, 16);
  if (!Number.isNaN(index) && index >= 0 && index <= 14) return { kind: 'memory', index };
  throw new Error(`Unknown register: ${register}`);
}

function parseRegisterNibble(text) {
  const normalized = String(text).toUpperCase()
    .replace(/А/g, 'A')
    .replace(/В/g, 'B')
    .replace(/С/g, 'C')
    .replace(/Д/g, 'D')
    .replace(/Е/g, 'E');
  if (!/^[0-9A-E]$/.test(normalized)) return null;
  return parseInt(normalized, 16);
}

function parseProgramAddress(text) {
  const normalized = String(text).toUpperCase().replace(/[.-]/g, 'A');
  if (!/^[0-9A-F]{2}$/.test(normalized)) return 0;
  return formalAddressInfo(parseInt(normalized, 16)).actual;
}

function addressNumberToOpcode(address) {
  if (address >= 100) return 0xA0 + (address - 100);
  return ((address / 10) | 0) * 16 + (address % 10);
}

function formalAddressInfo(opcode) {
  const high = opcode >> 4;
  const low = opcode & 0x0f;
  const ordinal = high * 10 + low;
  if (ordinal <= 104) return { actual: ordinal };
  if (ordinal <= 111) return { actual: ordinal - 105 };
  if (ordinal <= 165) return { actual: ordinal - 112 };
  return { actual: 0 };
}

function normalizeLabel(label) {
  return normalizeMnemonic(label).replace(/[.:]$/g, '');
}

function normalizeMnemonic(token) {
  return String(token)
    .trim()
    .replace(/\u00A0/g, '')
    .replace(/\s+/g, '')
    .replace(/[−–—]/g, '-')
    .replace(/⋅|×/g, '*')
    .replace(/÷|:/g, '/')
    .replace(/√¯?/g, '√')
    .replace(/[кК]/g, 'K')
    .replace(/[хХx]/g, 'X')
    .replace(/([<>=#≠≥⩾])(?:о|o)/gi, '$10')
    .replace(/≥|⩾/g, '>=')
    .replace(/≠|<>|!=/g, '#')
    .toUpperCase();
}

function parseAngleMode(mode) {
  const normalized = String(mode).trim().toLowerCase();
  if (['rad', 'r', 'р', 'radian', 'radians'].includes(normalized)) return 10;
  if (['deg', 'degree', 'degrees', 'г'].includes(normalized)) return 11;
  if (['grad', 'grads', 'грд'].includes(normalized)) return 12;
  if ([10, 11, 12].includes(Number(mode))) return Number(mode);
  throw new Error(`Unknown angle mode: ${mode}`);
}

module.exports = {
  MK61,
  parseProgramText,
  parseCommandToken,
  normalizeMnemonic,
  DIGIT_SYMBOLS,
};
