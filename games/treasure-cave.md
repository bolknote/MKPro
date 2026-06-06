# Treasure Cave

- Author: Vasiliy Zakharenko
- Source: *Tekhnika Molodezhi*, No. 7, 1987, «Клуб электронных игр» (KEI column)
- Program: [treasure-cave.txt](treasure-cave.txt)
- Listing restored from the public scan at `https://img-fotki.yandex.ru/get/44951/137106206.6d5/0_1d841d_dc897752_orig` (macOS Vision OCR + manual table check)

## Description

«Пещера сокровищ» (*Treasure Cave*) is a three-floor maze 7×4. Each cell is
encoded as a bit mask in registers `R1`..`R3`. The player explores the cave,
searches for caches, spends water and grenades, avoids a robber, and exits
through the left edge of the third floor.

Coordinates are stored in `Ra` as `floor.000000N`, where:

- the integer part is the floor number (1..3);
- the position of the last digit in the fractional part is the horizontal
  coordinate (1..7);
- the digit itself is the vertical coordinate (1, 2, 4, or 8 on the floor bit
  map).

The article’s starting point is `1.0000008`: floor 1, horizontal 7, vertical 8.

## Resources

- `Re` — water (20 cups at start in the article);
- `Rd` — grenades (2 at start);
- `Rc` — treasure.

Each move costs one unit of water. A cache on floor 1 adds water (+10), on
floor 2 adds grenades (+1), on floor 3 adds treasure (+1). After a cache is
taken, it disappears from the same cell on every floor.

## Controls

After `С/П`, enter a move code:

| Command | Action |
| --- | --- |
| `2` | Down |
| `4` | Left (toward the exit) |
| `6` | Right |
| `8` | Up |
| `5` | One floor up |
| `-` then `5` | One floor down |
| `0` then `С/П` | Search for a cache |
| `F pi` | Blast a wall (after a failed move) |
| `В↑` then `С/П` | Fight the robber after a find |

A successful move shows the new coordinates. A blocked move shows `0`. Leaving
the cave shows `11` (the article’s “route 11 tram” joke). Running out of water
shows `ЕГГ0Г`.

## Memory setup

Set the `Р-ГРД-Г` switch to `Р`.

- `R4`: letter `E` (`1 К- ВП хП4`);
- `R7`: `97`;
- `R8`: `0.1`;
- `Re`: `20`;
- `Rd`: `2`;
- `Rc`: `0`;
- `R1`: disjunction of `1.0080808` and `1.7062264` → `8.70E2-6C`;
- `R2`: `1.808` and `1.6715401` → `8.E795401`;
- `R3`: `1.8088088` and `1.2260335` → `8.-2E83LГ`;
- `R6`: `π` (treasure map);
- `Ra`: `1.0000008` (start).

## Program blocks (from the article)

| Steps | Purpose |
| --- | --- |
| 00–01 | Prepare for move input |
| 02–05 | Check remaining water |
| 06–21 | Decode the command code |
| 22–34 | Compute new coordinates |
| 35–39 | Check maze boundaries |
| 40–49 | Check for a wall |
| 50–51 | Perform the move |
| 54–61 | Remove a wall with a grenade |
| 62–96 | Search for a cache and handle the find |
| 97–104 | Resource consumption subroutine |

## Related material

This folder holds the **original** 1987 listing from the KEI article. Serge
Anvarov’s modified demonstration (different listing, auto-generated maze) is
in [`../anvarov/demo.txt`](../anvarov/demo.txt).
