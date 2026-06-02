# 99 бутылок

- Author: [Евгений Степанищев](https://bolknote.ru/)
- Source: [bolknote.ru](https://bolknote.ru/all/4418/)
- Program: [99-bottles.txt](99-bottles.txt)
- MK-Pro port: [examples/99-bottles.mkpro](../../examples/99-bottles.mkpro)

## Description

MK-61 version of the "99 Bottles of Beer" demonstration program. The program
does not print the whole song; it takes a verse number and displays a compact
calculator-style phrase such as `BEEr 42`.

Before running, put the starting value into `R0`, for example:

```text
99 хП0
```

Then start with `В/О С/П`. Because the program has no outer loop, enter the
next verse number manually and run it again.

## Setup

The source article gives this setup sequence before switching to program mode:

```text
8112000 хПc 8 + КИНВ хПb FВx 10 ÷ К[x] хПd 8 + КИНВ хПe В/О ПРГ
```

The listing in [99-bottles.txt](99-bottles.txt) starts after `ПРГ`.

## Notes

The original publication uses compact calculator notation:

| Source form | Listing form |
| --- | --- |
| `Х→П R` | `хПR` |
| `П→Х R` | `ПхR` |
| `Вх` | `FВx` |
| `[x]` | `К[x]` |
| `{x}` | `К{x}` |
| `ИНВ` | `КИНВ` |
| `^` | `В↑` |
| `К П→Х R` | `КПхR` |
