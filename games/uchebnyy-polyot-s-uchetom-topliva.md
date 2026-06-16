# Учебный полет — Вариант с учетом топлива

- Source: [xbasoft PMK package](https://xvadim.github.io/xbasoft/pmk/fly-2.zip)
- Program: [uchebnyy-polyot-s-uchetom-topliva.txt](uchebnyy-polyot-s-uchetom-topliva.txt)
- Decoded from the Android emulator state file inside the package.

## Description

Вариант авиационного тренажера с запасом топлива. Игрок также управляет траекторией переключателем `Р-ГРД-Г`, проходит гору и облако, затем должен вывести самолет к посадке.

Дополнительное ограничение - топливо в регистре `Re`: посадку надо выполнить до его окончания. В исходном описании предлагаются разные стартовые запасы для опытных игроков, обычной игры и обучения.

## Import Note

This listing is kept as a separate source variant because its opcode sequence does not match the existing local listing by exact hash or by seven-command page set.

Nearest existing listing during import audit: `games/uchebnyy-polyot-modernized.txt` (61.9% similarity, edit distance 40).
