# Lamer Andrew Meets America

- Author: illi4
- Source: [Habr article](https://habr.com/ru/articles/125484/)
- Program: [lamer-andrew-meets-america.txt](./lamer-andrew-meets-america.txt)

## Description

This is the first game from the notebook shown in the Habr article. The author describes it as a shooter-style animation game.

- Press `ВО С/П` to watch the cartoon/intro.
- Press `BP 53` to shoot the monster.
- Press `BP 74` to destroy the enemy car.
- On loss the display shows `8 *****`.
- On win the display shows `ECC`.
- The game can use multiple image styles (front and top view) at once.

## Animation registers

| Register | Normal view | Advanced view |
| --- | --- | --- |
| 0 | `80013` | `80013` |
| 1 | `8` | `8` |
| 2 | `81` | `81` |
| 3 | `801` | `8573` |
| 4 | `800077` | `857` |
| 5 | `80109` | `81308333` |
| 6 | `80108` | `81300876` |
| 7 | `8077099` | `8570757` |
| 8 | `8015` | `8135` |
| 9 | `80105` | `81305` |
| a, b | `5` | `5` |
| c | `807799` | `857757` |
| d | `80779` | `8577333` |

## Notes

A few commands from the published source code were cleaned manually from the article and translated into the same collection format.
