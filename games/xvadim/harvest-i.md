# Урожай (интенсивная модель)

- Author/source credit: В. Алексеев; Android conversion by А. Лаврик
- Source: [xvadim/pmk61-programms](https://github.com/xvadim/pmk61-programms/blob/master/harvest-i.pmk)
- Source description: `harvest-i.html`
- Program: [harvest-i.txt](./harvest-i.txt)

## Description

Экономический симулятор совхоза с тем же годовым циклом, что и harvest-e: урожайность, посевные площади, сбор грибов и ягод, охрана урожая и выплаты в бюджет.

Это интенсивная модель: прибыль повышается за счёт роста урожайности уже имеющейся пашни.

## Setup

- Set angle switch to Р.
- R7: display letter E; R8: 82; R9: 93.
- R3: 300000; R4: 30; R5: 1.1; R6: 1.2; Ra: 35000; Rb: 3500; Rd: 1000.

## Import Note

The original source file is serialized Android emulator state. This repository keeps only the decoded clean MK-61 listing.
