# Boxing

- Author: Mikhail Karasev
- Source: [Lord_BSS](https://lordbss.narod.ru/pmk00.html)
- Program: [boxing.txt](boxing.txt)

## Listing Note

The listing and play instructions were additionally cross-checked against pages 18-19 of Aleksandr Dolgushin's notebook.

## Description

### Registers
- 55555555 Кинв K{x} ВП 7 хПВ; 5555 Кинв K{x} ВП 3 хПС; 731 Кинв ВП 2 хП0; 22005008 Кинв K{x} ВП 7 хП4; number from 0 to 1 хП5; 8 хП8; 59 хПЕ; 10 хП6; 9 хПД.

You are about to test your agility and resourcefulness in a bout against a PMK opponent. You and the PMK take turns striking each other and blocking (defending against blows).

**Start:** В/О С/П... RX="0".

Ξ Enter the number of the target area for your blow against the opponent,
В↑, the number of the area to block, С/П...

RX="number of the area your blow hit".
RY="the PMK's blow against you".

The following may also appear:
RX="-------" - the PMK is knocked out.
RX="---" - the PMK is knocked down.
RX="ВСЕ" - you are knocked out.
Then continue from Ξ.

If RX="Г - 7", the round is over. Now
Fπ "your points"; Fπ "PMK points". С/П...

At the end of the game, the display shows the total score: RX="player's points". RY="PMK points".

**Strike/block**

0 | misses the body

1 | abdomen

2 | chest

3 | shoulder

4 | eyebrow

5 | ear

6 | forehead

7 | nose

8 | jaw

9 | neck

---

### About the Program and Its Author
- The program was first published in issue 10 of the Kyiv magazine Odnoklassnik in 1990.

- The program has a remake: [Boxing 2](boks-2.md).

- I know nothing about the program's author, M. Karasev.

- It looks as if the wildly simple idea behind the "Boxing" program has now grown into the online gaming megaproject ["Fight Club"](http://combats.ru/). ;)
