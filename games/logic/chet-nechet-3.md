# Чет-нечет-3

- Source: [xbasoft PMK package](https://xvadim.github.io/xbasoft/pmk/chnch3.zip)
- Additional source: [Habr article by illi4](https://habr.com/ru/articles/125484/)
- Program: [chet-nechet-3.txt](chet-nechet-3.txt)
- Decoded from the Android emulator state file inside the package.

## Description

Игра-угадайка "чет-нечет". Игрок каждый ход выбирает `0` или `1`, а ПМК пытается предсказать выбор, анализируя предыдущие ответы.

Счет начинается с нуля: если калькулятор ошибся, игрок получает очко, если угадал - очко теряется. Партия идет до десяти очков одной из сторон, а счет выводится с тремя служебными нулями.

## Import Note

The Habr `odd-even-3` listing was byte-identical to this file. The duplicate file was removed and both sources are recorded here.
