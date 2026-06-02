# Cave

- Author: illi4
- Source: [Habr article](https://habr.com/ru/articles/125484/)
- Program: [cave.txt](cave.txt)

## Description

A dungeon crawl where you must collect treasure while avoiding the monster.

## Gameplay

1. Start with `В/О С/П`; after a short delay, the field appears.
2. `8` marks the treasure, `-` is the player, `E` is the monster, and `C` is a door.
3. When you move into a doorway candidate, two short numbers are shown; entering them correctly opens the door.
4. Wrong answers cause the monster to advance.

## Scoring

- Successful door opening gives **+3** points.
- A failed attempt removes **3** points.
- Reaching treasure gives bonus points.

## End Conditions

If the monster catches you, you lose. If all doors are handled and treasure reached, the final score is shown.
