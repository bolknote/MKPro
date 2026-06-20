# Охота на лис (вариант для МК-61)

- Source: [Alexander Bulatov GeoCities page, archived](https://web.archive.org/web/20091020020922/http://geocities.com/CapeCanaveral/Hall/5525/Foxhunt/Foxhuntru.htm)
- Mirror source: [MonatkoDenis blog](https://monatkodenis.blogspot.com/2014/01/blog-post.html)
- Program: [fox-hunt-mk61.txt](fox-hunt-mk61.txt)
- Listing restored from the archived `fh.jpg` image; byte-code checked against the mirror image.

## Description

This is an improved MK-61 variant of «Охота на лис», a logic game on a 10×10
board. The original MK-54 publication is credited in the source post to
A. Neschetny from Leningrad (*Nauka i Zhizn*, December 1985), while the author
of this specific MK-61 variant is not named.

There are nine hidden foxes on the board. In this version no two foxes can share
the same cell. The player enters hunter coordinates and receives the number of
foxes visible from that cell.

## Setup

After loading the program, preload constants:

```text
2288899 В↑ 8888236 К⊕ хП1 ↔ 1085593 К⊕ К{x} /-/ ВП 5 хП0
```

Then enter a random number from `0` to `1`, for example `0.123456`, and start:

```text
БП 54 С/П
```

The display `7.` means the foxes were placed successfully.

## Play

Enter cell coordinates as a two-digit number, for example `51`, then press
`С/П` (or `В/О С/П`). A normal answer has the form `--51-- 3.`, where `51` is
the entered cell and `3` is the number of visible foxes.

If the entered cell contains a fox, the calculator displays `-20RRO.-`. Press
`С/П` again to see the number of foxes.
