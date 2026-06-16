# Белая ладья и король против черного короля

- Source: [xbasoft PMK package](https://xvadim.github.io/xbasoft/pmk/rook.zip)
- Program: [shakhmaty-ladya-protiv-korolya.txt](shakhmaty-ladya-protiv-korolya.txt)
- Decoded from the Android emulator state file inside the package.

## Description

Шахматный эндшпиль "ладья и король против короля", где матует ПМК. Игрок задает начальные координаты фигур в регистрах, затем водит черного короля и получает ответы белых.

ПМК может ходить королем или ладьей, а разные форматы вывода показывают тип ответа и шах. Иногда исходная позиция требует поворота доски перед началом, чтобы соответствовать ограничениям алгоритма.

## Import Note

This listing is kept as a separate source variant because its opcode sequence does not match the existing local listing by exact hash or by seven-command page set.

Nearest existing listing during import audit: `games/brodyaga.txt` (14.3% similarity, edit distance 90).
