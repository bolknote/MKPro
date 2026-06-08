# Программа-часы

- Authors: С. Конин, А. Шарапов
- Source: [MonatkoDenis blog](https://monatkodenis.blogspot.com/2014/01/blog-post.html)
- Program: [programma-chasy.txt](programma-chasy.txt)
- Listing restored from the image `01.jpg` in the source post

## Description

This program uses the predictable execution time of MK-61 instructions as a
simple clock. The current time is stored in memory registers and periodically
shown on the display.

## Setup

Load the program, then preload constants:

| Register | Value | Role |
| --- | --- | --- |
| `Ra` | `10` | Timing calibration constant |
| `R2` | `11` | Timing constant |
| `R4` | `24` | Hours limit |
| `R6` | `60` | Minutes limit |
| `R7` | `100` | Display scale |
| `R8` | current hours | Initial hour |
| `R9` | current minutes | Initial minute |

Start with `В/О С/П`.

The display shows a three- or four-digit `HHMM`-style value in the left part of
the indicator. The source notes that the clock can run fast or slow, and should
be calibrated by adjusting `Ra`.
