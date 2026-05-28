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

Source: *Tekhnika Molodezhi*, 1987 — «Клуб электронных игр» (KEI column)

Files are stored in [`kei/`](./kei/):

- `lunolot-d.txt` — dynamic *Lunar Lander* («Лунолёт-Д»), 80 steps.
- `treasure-cave.txt` — original *Treasure Cave* («Пещера сокровищ»), 105 steps.
- `winner.txt` — *Winner* («Победитель»), MK-61 adaptation, 101 steps.
- Matching `*.md` descriptions and `manifest.tsv`.

The two 105/101-step listings come from the same KEI column in TM No. 7/1987.
The modified demo in
[`anvarov/`](./anvarov/) reimplements *Treasure Cave* with a different program.

Programs: **3**.

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

## MonatkoDenis Blog

Source: [«Поиграем на микрокалькуляторе»](https://monatkodenis.blogspot.com/2014/01/blog-post.html)

Files are stored in [`monatkodenis/`](./monatkodenis/):

- `fox-hunt-mk61.txt` - improved MK-61 variant of «Охота на лис», restored from the source scan.
- `clock.txt` - «Программа-часы» by С. Конин and А. Шарапов.
- Matching `*.md` descriptions and `manifest.tsv`.

Programs: **2**.

## Semico MK Programs

Source: [Semico MK games](http://mk.semico.ru/mkpr_r9.htm)

Files are stored in [`semico/`](./semico/):

- `lunolet-1.txt`, `lunolet-2.txt`, `lunolet-3.txt` - MK-61-sized listings
  restored from Semico `*.mkp` files.
- Matching `*.md` descriptions include setup registers decoded from Semico
  `*.mkd` files.
- `manifest.tsv` - tab-separated index with program file, description file,
  title, author, and source URL.

Programs: **3**.
