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
## Tekhnika Molodyzhi / KEI + early issue

Source:

- *Tekhnika Molodyzhi* — Kei/Club Electronic Games material (1987),
- [Техника-молодёжи No. 7, 1985](https://epizodsspace.airbase.ru/bibl/tehnika_-_molodyoji/1985/7/put.html)

Files are stored in [`tehnika-molodyzhi/`](./tehnika-molodyzhi/):

- `soft-landing.txt` — landing game program, based on `Lunolot-1` conventions.
- `planet-constructor.txt` — PC-1 utility for constructing custom planetary parameters.
- `lunolot-d.txt` — dynamic *Lunar Lander* («Лунолёт-Д»), 80 steps.
- `treasure-cave.txt` — original *Treasure Cave* («Пещера сокровищ»), 105 steps.
- `winner.txt` — *Winner* («Победитель»), MK-61 adaptation, 101 steps.
- Matching `*.md` descriptions and `manifest.tsv`.

The demo in [`anvarov/`](./anvarov/) reimplements *Treasure Cave* as a modified
variant.

Programs: **5**.

## mk61.narod.ru

Source: [mk61.narod.ru](https://mk61.narod.ru)

Files are stored in [`mk61-narod/`](./mk61-narod/):

- `lamer-andrew-meets-america.txt` — plain listing for *Lamer Andrew Meets America*.
- `lamer-andrew-meets-america.md` — description and setup notes.
- `tamagotchi.txt` — plain listing for *Tamagotchi*.
- `tamagotchi.md` — description and setup notes.
- `the-land-of-psycho-lamers.md` — translated concept/controls from the map game; source snapshot has no extractable program table.
- `manifest.tsv` — tab-separated index with `program`, `description`, `title`, `author`, and `source_url`.

Programs: **2** (one listed source page (`game2.htm`) has no extracted listing in
this local snapshot).

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

The Treasure Cave demo in `anvarov/demo.txt` is a modified showcase program. The KEI
and early *Tekhnika* material listings live in [`tehnika-molodyzhi/`](./tehnika-molodyzhi/).

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

## xvadim PMK61 Programs

Source: [xvadim/pmk61-programms](https://github.com/xvadim/pmk61-programms)

Files are stored in [`xvadim/`](./xvadim/):

- `harvest-e.txt`, `harvest-i.txt` - two variants of «Урожай».
- `naval-battle.txt` - Г. Горовой's «Морской бой» adaptation.
- `wolves-and-goat-9x9.txt` - original 9x9 «Волки и козлик».
- `square-equation.txt` - quadratic equation utility.
- `rally.txt` - Rally listing from the source `.pmk`.
- Matching `*.md` descriptions and `manifest.tsv`.

The original source files are serialized Android emulator state; this
repository keeps decoded clean listings only.

Programs: **6**.

## Fink L.M., "Papa, Mom, Me and Micro-Calculator" (1988)

Source: Финк Л.М., Папа, мама, я и микрокалькулятор (1988)

Files are stored in [`fink-1988/`](./fink-1988/):

- `matchsticks-bashe.txt` — Bash matchstick game listing.
- `odd-even-trick.txt` — Odd-even / Who can outsmart who listing.
- `sportloto-model.txt` — Sportloto 5-of-36 model listing.
- `sailing-regatta.txt` — Sailing regatta simulator listing.
- `tic-tac-toe.txt` — Unbeatable Tic-Tac-Toe listing.
- Matching `*.md` descriptions and `manifest.tsv`.

Programs: **5**.
