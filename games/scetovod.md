# Счетовод

- Source: [xbasoft PMK package](https://xvadim.github.io/xbasoft/pmk/scetovod.zip)
- Program: [scetovod.txt](scetovod.txt)
- Decoded from the Android emulator state file inside the package.

## Description

Игровая программа-помощник для настольных экономических игр с денежными счетами, например "Монополии" или "Конверсии-1". В регистр `R0` заносится число игроков, счета игроков хранятся в `R1` и далее, а программа проводит переводы между игроками и банком.

Это не самостоятельная игра, а электронный счетовод для партии: он снимает ручной учет денег и помогает быстро выполнять платежи, начисления и списания.

## Import Note

This listing is kept as a separate source variant because its opcode sequence does not match the existing local listing by exact hash or by seven-command page set.

Nearest existing listing during import audit: `games/odd-even-1.txt` (33.3% similarity, edit distance 70).
