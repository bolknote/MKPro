# Tamagotchi

- Author: illi4
- Source: [Habr article](https://habr.com/ru/articles/125484/)
- Program: [tamagotchi.txt](tamagotchi.txt)

## Description

This is the second game from the notebook and a virtual pet simulation for MK-61.
The game is a Tamagotchi-like simulator about Andrew.

Goal:

- press `C/P` to show Andrew,
- keep him healthy through player actions,
- grow him by cycles up to age `20`,
- prevent him from dying.

`C/P` starts the game after displaying Andrew. His growth advances as you perform life actions.
Every five successful life cycles adds 5 years.

## Controls

1. Feed: `BP 65`, then `C/P`  
   Decreases the hunger level. If hunger reaches `10`, Andrew dies.
2. Walk: `BP 84`, then `C/P`  
   Walking also makes him go to the toilet; if the anti-walk counter reaches `10`, Andrew dies.
3. Sleep: `BP 75`, then `C/P`  
   Press `C/P` once more to wake him up.
4. Doctor visit: `BP 55`, then `C/P`  
   Recommended at age `10` (as indicated in the article).

## Win / Lose

- Win: display shows `8 EC` (Cyrillic "ЕС" in the original display mapping).
- Lose: display shows `8-----8`

At any time, if a tracked life counter reaches the death threshold (`10`), the game ends with the lose state.

## Note

The program listing in `tamagotchi.txt` is reconstructed from snippets published in the Habr article.
