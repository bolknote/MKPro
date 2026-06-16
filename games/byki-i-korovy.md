# Быки и коровы

- Source: [xbasoft PMK package](https://xvadim.github.io/xbasoft/pmk/bullscows.zip)
- Program: [byki-i-korovy.txt](byki-i-korovy.txt)
- Decoded from the Android emulator state file inside the package.

## Description

Классическая игра "Быки и коровы". ПМК загадывает четырехзначное число, причем цифры могут повторяться, а игрок вводит варианты и получает ответ о совпадениях.

Двузначный ответ `mn` означает общее число быков и коров, а однозначный ответ показывает, что быков нет. После угадывания программа выводит число попыток.

## Import Note

This listing is kept as a separate source variant because its opcode sequence does not match the existing local listing by exact hash or by seven-command page set.

Nearest existing listing during import audit: `games/bulls-and-cows-2.txt` (81.6% similarity, edit distance 18).
