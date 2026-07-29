# Охота на мустанга

- Source: [xbasoft PMK package](https://xvadim.github.io/xbasoft/pmk/mustang_8x8.zip)
- Program: [mustang-8x8-variant.txt](mustang-8x8-variant.txt)
- Decoded from the Android emulator state file inside the package.

## Description

Вариант "Охоты на мустанга" на доске 8x8. Калькулятор управляет мустангом, который ходит как шахматный конь, а игрок передвигает охотников по диагоналям на любое число свободных клеток.

Фигуры не бьют друг друга: задача охотников - занять такие клетки, чтобы у мустанга не осталось допустимого хода. Координаты доски задаются парами от `1.1` до `8.8`.

## Import Note

This listing is kept as a separate source variant because its opcode sequence does not match the existing local listing by exact hash or by seven-command page set.

Nearest existing listing during import audit: `games/mustang-8x8.txt` (85.7% similarity, edit distance 14).
