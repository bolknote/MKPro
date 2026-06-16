# Чёт-нечёт-3

- Source: [xbasoft PMK package](https://xvadim.github.io/xbasoft/pmk/chnch3.zip)
- Program: [chet-nechet-3.txt](chet-nechet-3.txt)
- Decoded from the Android emulator state file inside the package.

## Description

Игра-угадайка "чет-нечет". Игрок каждый ход выбирает `0` или `1`, а ПМК пытается предсказать выбор, анализируя предыдущие ответы.

Счет начинается с нуля: если калькулятор ошибся, игрок получает очко, если угадал - очко теряется. Партия идет до десяти очков одной из сторон, а счет выводится с тремя служебными нулями.

## Import Note

This listing is kept as a separate source variant because its opcode sequence does not match the existing local listing by exact hash or by seven-command page set.

Nearest existing listing during import audit: `games/odd-even-3.txt` (89.5% similarity, edit distance 11).
