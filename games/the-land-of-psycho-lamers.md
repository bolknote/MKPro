# The Land of Psycho Lamers

- Author: illi4
- Source: [mk61.narod.ru/game2.htm](https://mk61.narod.ru/game2.htm)
- Program: [the-land-of-psycho-lamers.txt](the-land-of-psycho-lamers.txt)

## Description

The original program by illi4 has survived only as a short description and map; the listing was lost. This is a completed reconstruction from the preserved page text and map. The restored shape follows the page instruction literally: each map cell is a program address, and the player moves by pressing `БП`, entering the cell number, and pressing `С/П`.

You are an adventurer looking for enough parts to build a computer. The calculator shows codes from the places and items list. Move only to adjacent cells on the paper map. Because movement is manual `БП address` control, the calculator does not enforce adjacency or prevent deliberately jumping across the map.

## Setup

Before starting, load the program and initialize registers:

```text
0  XП0
11 XП1
12 XП2
13 XП3
14 XП4
15 XП5
16 XП6
17 XП7
18 XП8
19 XП9
30 XПa
20 XПb
5  XПd
0  XПe
```

Register `d` is health. Register `e` is the number of computer parts found.

## Controls

Start at cell `00`:

```text
БП 00 С/П
```

To move, choose an adjacent cell from the map:

```text
БП NN С/П
```

If you enter a computer-part cell, the display shows the new number of collected parts. Mark that part on paper and do not collect it again. If you enter a lamer cell, the display shows remaining health. At `0` health the game is lost. Cell `91` is the exit: it shows `30` when at least three parts have been found, otherwise `20`.

## Map

```text
                  82  85
00  29  32  59  62  79  88  91
03  26  35  56  67
06  21  38  53  70
09  18  41  50  73
12  15  44  49  76
```

Special cells:

```text
21, 44, 62  computer parts
35, 73, 82  psycho lamers
91          final computer assembly point
49, 50      same place code; 49 falls through to 50
```

## Places And Items List

```text
0   start / safe cell
11  green street
12  broken house
13  junk passage
14  empty workshop
15  old terminal room
16  road with tracks
17  cellar
18  radio tower
19  concrete yard

1   one computer part found
2   two computer parts found
3   all computer parts found

5..1 health after a fight
0   no health left
20  not enough parts at the final cell
30  computer assembled; victory
```

## Notes

This reconstruction uses the MK-61 map-address style implied by the surviving page. It is intentionally compact enough to fit the original 105-step memory window and leaves the paper-map discipline to the player, just as the `БП NN С/П` control scheme suggests.
