# MK-61 Games

This directory contains cleaned game materials for Elektronika MK-52/MK-61.

## Lord_BSS Collection

Source: [Lord_BSS PMK programs](https://lordbss.narod.ru/pmk.html)

Files are stored in [`lordbss/`](./lordbss/):

- `pmkNNN.txt` - plain program listings, one command per addressed step.
- `pmkNNN.md` - Markdown descriptions converted from the source pages.
- `manifest.tsv` - tab-separated index with program file, description file,
  title, author, and source URL.

Cleaned game programs: 254.

## Tekhnika Molodezhi / KEI

Source: *Tekhnika Molodezhi*, No. 7, 1987 — «Клуб электронных игр» (KEI column)

Files are stored in [`kei/`](./kei/):

- `treasure-cave.txt` — original *Treasure Cave* («Пещера сокровищ»), 105 steps.
- `winner.txt` — *Winner* («Победитель»), MK-61 adaptation, 101 steps.
- Matching `*.md` descriptions and `manifest.tsv`.

Both listings come from the same KEI column in TM No. 7/1987. The modified demo in
[`anvarov/`](./anvarov/) reimplements *Treasure Cave* with a different program.

## Serge Anvarov Collection

Sources:

- [Games index](https://sergeanvarov.github.io/russian/mk61/games/index.html)
- [Other programs](https://sergeanvarov.github.io/russian/mk61/other/index.html)

Files are stored in [`anvarov/`](./anvarov/):

- `slug.txt` — plain program listings, one command per addressed step.
- `slug.md` — Markdown descriptions from Anvarov's HTML pages.
- `manifest.tsv` — tab-separated index with program file, description file, title, author, and source URL.

Programs: **21** (catalog games/utilities plus the Treasure Cave demo).

Re-import:

```sh
node scripts/import-emulator-listings.cjs
```

The Treasure Cave demo in `anvarov/demo.txt` is a modified showcase program. The KEI column
listings live in [`kei/`](./kei/).

## Bolknote Collection

Source: [Evgeny Stepanishchev / bolknote.ru](https://bolknote.ru/)

Files are stored in [`bolknote/`](./bolknote/):

- `99-bottles.txt` - plain program listing, one command per addressed step.
- `99-bottles.md` - Markdown description and setup notes from the source page.
- `manifest.tsv` - tab-separated index with program file, description file, title, author, and source URL.

Programs: **1**.
