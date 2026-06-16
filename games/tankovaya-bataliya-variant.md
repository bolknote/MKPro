# Танковая баталия

- Source: [xbasoft PMK package](https://xvadim.github.io/xbasoft/pmk/tanksbattler.zip)
- Program: [tankovaya-bataliya-variant.txt](tankovaya-bataliya-variant.txt)
- Decoded from the Android emulator state file inside the package.

## Description

Дуэль двух танков с движением, стрельбой, топливом, боезапасом и прочностью брони. В этом варианте стрелок не знает погодный коэффициент, поэтому поправки к выстрелу приходится подбирать по результатам.

Игроки по очереди перемещаются и стреляют; расход топлива зависит от квадрата скорости, дистанция между танками может только уменьшаться, столкновение дает ничью. Когда кончаются снаряды или топливо, танк теряет соответствующую возможность.

## Import Note

This listing is kept as a separate source variant because its opcode sequence does not match the existing local listing by exact hash or by seven-command page set.

Nearest existing listing during import audit: `games/tank-battle-realistic-variant.txt` (86.7% similarity, edit distance 14).
